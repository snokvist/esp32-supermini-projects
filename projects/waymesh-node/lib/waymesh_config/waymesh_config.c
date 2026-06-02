/* Portable persistent config + crash-safe packetId counter (doc 13 §8).
 * Storage is the pluggable wm_store_t KV backend; this file holds only the
 * portable model + logic, host-tested in test/test_config. */
#include "waymesh_config.h"

#include <string.h>

/* bounded strlen (strnlen is POSIX, not guaranteed on all our toolchains) */
static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

/* --- derived fields ------------------------------------------------------- */

static void channel_derive(wm_channel_t *c)
{
    c->key_len = 0;
    c->hash = -1;
    uint8_t key[WM_MAX_KEY_LEN];
    size_t klen = 0;
    if (wm_expand_psk(c->psk, c->psk_len, key, &klen) == 0) {
        memcpy(c->expanded_key, key, klen);
        c->key_len = klen;
        if (klen > 0)
            c->hash = wm_chan_hash_expanded(c->name, key, klen);
    }
}

/* --- channel blob (de)serialisation --------------------------------------
 * [relay_policy u8][home_slot u8][count u8]
 *   per channel: [index u8][psk_len u8][name_len u8][name..][psk..]
 */
#define WM_CONFIG_BLOB_MAX \
    (3 + WM_MAX_CHANNELS * (3 + WM_CHAN_NAME_MAX + WM_PSK_MAX))

static size_t config_serialize(const wm_config_t *cfg, uint8_t *buf)
{
    size_t n = 0;
    buf[n++] = (uint8_t)cfg->relay_policy;
    buf[n++] = cfg->home_slot;
    buf[n++] = cfg->channel_count;
    for (uint8_t i = 0; i < cfg->channel_count; i++) {
        const wm_channel_t *c = &cfg->channels[i];
        uint8_t name_len = (uint8_t)bounded_len(c->name, WM_CHAN_NAME_MAX);
        buf[n++] = c->index;
        buf[n++] = c->psk_len;
        buf[n++] = name_len;
        memcpy(buf + n, c->name, name_len); n += name_len;
        memcpy(buf + n, c->psk, c->psk_len); n += c->psk_len;
    }
    return n;
}

/* Returns 0 on success, -1 on a malformed/oversized blob. */
static int config_deserialize(wm_config_t *cfg, const uint8_t *buf, size_t len)
{
    size_t n = 0;
    if (len < 3) return -1;
    wm_relay_policy_t policy = (wm_relay_policy_t)buf[n++];
    uint8_t home = buf[n++];
    uint8_t count = buf[n++];
    if (count > WM_MAX_CHANNELS) return -1;

    wm_channel_t chans[WM_MAX_CHANNELS];
    memset(chans, 0, sizeof(chans));
    for (uint8_t i = 0; i < count; i++) {
        if (n + 3 > len) return -1;
        uint8_t index = buf[n++];
        uint8_t psk_len = buf[n++];
        uint8_t name_len = buf[n++];
        if (psk_len > WM_PSK_MAX || name_len >= WM_CHAN_NAME_MAX) return -1;
        if (n + name_len + psk_len > len) return -1;
        memcpy(chans[i].name, buf + n, name_len); n += name_len;
        chans[i].name[name_len] = '\0';
        chans[i].psk_len = psk_len;
        memcpy(chans[i].psk, buf + n, psk_len); n += psk_len;
        chans[i].index = index;
        channel_derive(&chans[i]);
    }
    if (count > 0 && home >= count) home = 0;
    cfg->relay_policy = (policy == WM_RELAY_KNOWN) ? WM_RELAY_KNOWN : WM_RELAY_ALL;
    cfg->home_slot = home;
    cfg->channel_count = count;
    memcpy(cfg->channels, chans, sizeof(chans));
    return 0;
}

static int config_persist(wm_config_t *cfg)
{
    uint8_t buf[WM_CONFIG_BLOB_MAX];
    size_t n = config_serialize(cfg, buf);
    return cfg->store.put_blob(cfg->store.ctx, WM_KEY_CHANS, buf, n);
}

/* --- packetId reserve-ahead ----------------------------------------------- */

/* Persist a higher ceiling, THEN adopt it — so any issued id is always below an
 * already-committed ceiling. A persist failure leaves the ceiling unchanged. */
static int pid_reserve(wm_config_t *cfg)
{
    uint32_t newceil = cfg->pid_ceiling + WM_PKTID_BLOCK;
    if (cfg->store.put_u32(cfg->store.ctx, WM_KEY_PID, newceil) != 0)
        return -1;
    cfg->pid_ceiling = newceil;
    return 0;
}

int wm_config_next_packet_id(wm_config_t *cfg, uint32_t *out_id)
{
    /* >= (not ==): if a prior reserve failed and pid_next ever overshot the
     * committed ceiling, keep trying to re-establish it rather than running
     * unbounded above an un-persisted high-water. */
    if (cfg->pid_next >= cfg->pid_ceiling) {
        if (pid_reserve(cfg) != 0)
            return -1; /* persist failed — refuse to issue past the committed
                        * ceiling (an unsynced reboot would re-issue it → nonce
                        * reuse, §8). The caller skips originating the frame. */
    }
    *out_id = cfg->pid_next++;
    return 0;
}

/* --- init / seeding ------------------------------------------------------- */

static void seed_default_channel(wm_config_t *cfg)
{
    /* The Meshtastic default "open" channel: explicit name "LongFast" + the
     * 1-byte default PSK index -> the famous default key -> chanHash 8, so an
     * unprovisioned node interoperates on the open group (§8). */
    memset(cfg->channels, 0, sizeof(cfg->channels));
    wm_channel_t *c = &cfg->channels[0];
    strncpy(c->name, "LongFast", WM_CHAN_NAME_MAX - 1);
    c->psk[0] = 0x01;
    c->psk_len = 1;
    c->index = 0;
    channel_derive(c);
    cfg->channel_count = 1;
    cfg->home_slot = 0;
    cfg->relay_policy = WM_RELAY_ALL;
}

int wm_config_init(wm_config_t *cfg, const wm_store_t *store)
{
    if (!cfg || !store) return -1;
    memset(cfg, 0, sizeof(*cfg));
    cfg->store = *store;

    uint8_t buf[WM_CONFIG_BLOB_MAX];
    size_t blen = 0;
    if (store->get_blob(store->ctx, WM_KEY_CHANS, buf, sizeof(buf), &blen) != 0 ||
        config_deserialize(cfg, buf, blen) != 0) {
        seed_default_channel(cfg);
    }

    uint32_t ceil;
    if (store->get_u32(store->ctx, WM_KEY_PID, &ceil) != 0)
        ceil = store->rng ? store->rng(store->ctx) : 0; /* fresh/corrupt NVS */
    cfg->pid_next = ceil;
    cfg->pid_ceiling = ceil;
    (void)pid_reserve(cfg); /* commit a fresh block before issuing any id */
    return 0;
}

/* --- channel management --------------------------------------------------- */

int wm_config_add_channel(wm_config_t *cfg, const char *name,
                          const uint8_t *psk, size_t psk_len, uint8_t index)
{
    if (!cfg || !name || name[0] == '\0') return -1;
    if (strlen(name) >= WM_CHAN_NAME_MAX) return -1;
    if (psk_len > WM_PSK_MAX) return -1;
    if (psk_len > 0 && !psk) return -1;

    /* replace a channel with the same Meshtastic index, else append */
    int slot = -1;
    for (uint8_t i = 0; i < cfg->channel_count; i++)
        if (cfg->channels[i].index == index) { slot = i; break; }
    if (slot < 0) {
        if (cfg->channel_count >= WM_MAX_CHANNELS) return -1;
        slot = cfg->channel_count++;
    }

    wm_channel_t *c = &cfg->channels[slot];
    memset(c, 0, sizeof(*c));
    size_t nlen = strlen(name);          /* < WM_CHAN_NAME_MAX (checked above) */
    memcpy(c->name, name, nlen);
    c->name[nlen] = '\0';                /* explicit NUL — don't lean on memset */
    if (psk_len) memcpy(c->psk, psk, psk_len);
    c->psk_len = (uint8_t)psk_len;
    c->index = index;
    channel_derive(c);
    return config_persist(cfg);
}

int wm_config_set_home(wm_config_t *cfg, uint8_t index)
{
    for (uint8_t i = 0; i < cfg->channel_count; i++)
        if (cfg->channels[i].index == index) {
            cfg->home_slot = i;
            return config_persist(cfg);
        }
    return -1;
}

int wm_config_set_relay_policy(wm_config_t *cfg, wm_relay_policy_t policy)
{
    cfg->relay_policy = policy;
    return config_persist(cfg);
}

const wm_channel_t *wm_config_home(const wm_config_t *cfg)
{
    if (cfg->channel_count == 0) return NULL;
    return &cfg->channels[cfg->home_slot];
}

const wm_channel_t *wm_config_channel_by_hash(const wm_config_t *cfg, uint8_t h)
{
    for (uint8_t i = 0; i < cfg->channel_count; i++)
        if (cfg->channels[i].hash == (int)h) return &cfg->channels[i];
    return NULL;
}

bool wm_config_accepts_hash(const wm_config_t *cfg, uint8_t h)
{
    return wm_config_channel_by_hash(cfg, h) != NULL;
}

bool wm_config_relays_hash(const wm_config_t *cfg, uint8_t h)
{
    if (cfg->relay_policy == WM_RELAY_ALL) return true;
    return wm_config_accepts_hash(cfg, h);
}
