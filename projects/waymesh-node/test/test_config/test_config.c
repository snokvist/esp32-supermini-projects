/* Host unit test: waymesh_config — channel store + crash-safe packetId.
 * The headline check is the nonce-uniqueness soak (doc 13 §10): packetId never
 * repeats under one key across forced reboots/crashes, past a 16-bit wrap.
 * Run: pio test -e native -f test_config */
#include <string.h>
#include <unity.h>

#include "waymesh_config.h"

/* --- mock NVS: the only two keys the store uses are wm.pid (u32) + wm.chans
 * (blob). Survives a simulated reboot (struct kept; cfg discarded); a simulated
 * power-loss-during-commit is modelled by rolling back the last pid write. --- */
typedef struct {
    uint32_t pid; int pid_set;
    uint32_t prev_pid; int prev_pid_set;
    uint8_t blob[1024]; size_t blob_len; int blob_set;
    uint32_t seed;
    int fail_pid_put;   /* when set, put_u32(wm.pid) returns -1 (commit failure) */
} mock_flash;

static int mf_get_u32(void *ctx, const char *key, uint32_t *out) {
    mock_flash *m = ctx;
    if (strcmp(key, WM_KEY_PID) == 0 && m->pid_set) { *out = m->pid; return 0; }
    return -1;
}
static int mf_put_u32(void *ctx, const char *key, uint32_t v) {
    mock_flash *m = ctx;
    if (strcmp(key, WM_KEY_PID) != 0) return -1;
    if (m->fail_pid_put) return -1;   /* simulate a flash commit failure */
    m->prev_pid = m->pid; m->prev_pid_set = m->pid_set;
    m->pid = v; m->pid_set = 1; return 0;
}
static int mf_get_blob(void *ctx, const char *key, void *out, size_t cap,
                       size_t *out_len) {
    mock_flash *m = ctx;
    if (strcmp(key, WM_KEY_CHANS) != 0 || !m->blob_set || m->blob_len > cap)
        return -1;
    memcpy(out, m->blob, m->blob_len); *out_len = m->blob_len; return 0;
}
static int mf_put_blob(void *ctx, const char *key, const void *data, size_t len) {
    mock_flash *m = ctx;
    if (strcmp(key, WM_KEY_CHANS) != 0 || len > sizeof(m->blob)) return -1;
    memcpy(m->blob, data, len); m->blob_len = len; m->blob_set = 1; return 0;
}
static uint32_t mf_rng(void *ctx) { return ((mock_flash *)ctx)->seed; }

static wm_store_t mock_store(mock_flash *m) {
    wm_store_t s = { mf_get_u32, mf_put_u32, mf_get_blob, mf_put_blob, mf_rng, m };
    return s;
}
/* Roll back the most recent pid write — models power loss during that commit. */
static void mf_rollback_pid(mock_flash *m) {
    if (m->prev_pid_set) { m->pid = m->prev_pid; }
    else { m->pid_set = 0; }
}

void setUp(void) {}
void tearDown(void) {}

/* Fresh NVS seeds the Meshtastic default channel -> chanHash 8. */
static void test_default_seed(void)
{
    mock_flash m = {0}; m.seed = 0x10000000;
    wm_store_t s = mock_store(&m);
    wm_config_t cfg;
    TEST_ASSERT_EQUAL_INT(0, wm_config_init(&cfg, &s));
    TEST_ASSERT_EQUAL_UINT(1, cfg.channel_count);
    const wm_channel_t *home = wm_config_home(&cfg);
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("LongFast", home->name);
    TEST_ASSERT_EQUAL_INT(8, home->hash);
    TEST_ASSERT_EQUAL_INT(WM_RELAY_ALL, cfg.relay_policy);
    TEST_ASSERT_TRUE(wm_config_accepts_hash(&cfg, 8));
    TEST_ASSERT_FALSE(wm_config_accepts_hash(&cfg, 9));
}

/* Add a channel; accept/relay queries reflect it and the relay policy. */
static void test_channel_add_and_policy(void)
{
    mock_flash m = {0}; m.seed = 1;
    wm_store_t s = mock_store(&m);
    wm_config_t cfg;
    wm_config_init(&cfg, &s);

    uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    TEST_ASSERT_EQUAL_INT(0, wm_config_add_channel(&cfg, "team-blue", key, 16, 1));
    const wm_channel_t *ch = wm_config_channel_by_hash(&cfg, 46); /* team-blue */
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_STRING("team-blue", ch->name);
    TEST_ASSERT_EQUAL_UINT(16, ch->key_len);

    /* relay-all relays everything; relay-known only configured hashes */
    TEST_ASSERT_TRUE(wm_config_relays_hash(&cfg, 200)); /* foreign, ALL */
    wm_config_set_relay_policy(&cfg, WM_RELAY_KNOWN);
    TEST_ASSERT_FALSE(wm_config_relays_hash(&cfg, 200));
    TEST_ASSERT_TRUE(wm_config_relays_hash(&cfg, 46));  /* known */
    TEST_ASSERT_TRUE(wm_config_relays_hash(&cfg, 8));   /* default still known */
}

/* Channels + relay policy + home survive a reboot (re-init from same store). */
static void test_persist_roundtrip(void)
{
    mock_flash m = {0}; m.seed = 2;
    wm_store_t s = mock_store(&m);
    wm_config_t cfg;
    wm_config_init(&cfg, &s);
    uint8_t psk[1] = {0x02};
    wm_config_add_channel(&cfg, "alpha", psk, 1, 1);
    wm_config_set_relay_policy(&cfg, WM_RELAY_KNOWN);
    wm_config_set_home(&cfg, 1);

    wm_config_t cfg2;
    TEST_ASSERT_EQUAL_INT(0, wm_config_init(&cfg2, &s)); /* reboot */
    TEST_ASSERT_EQUAL_UINT(2, cfg2.channel_count);
    TEST_ASSERT_EQUAL_INT(WM_RELAY_KNOWN, cfg2.relay_policy);
    const wm_channel_t *home = wm_config_home(&cfg2);
    TEST_ASSERT_EQUAL_STRING("alpha", home->name);
    TEST_ASSERT_EQUAL_INT(117, home->hash); /* alpha/index-2 vector */
    TEST_ASSERT_TRUE(wm_config_accepts_hash(&cfg2, 8)); /* default kept too */
}

/* Fresh NVS seeds packetId from rng(); ids are monotonic. */
static void test_pktid_fresh_seed(void)
{
    mock_flash m = {0}; m.seed = 0xABCD0000;
    wm_store_t s = mock_store(&m);
    wm_config_t cfg;
    wm_config_init(&cfg, &s);
    uint32_t a, b;
    TEST_ASSERT_EQUAL_INT(0, wm_config_next_packet_id(&cfg, &a));
    TEST_ASSERT_EQUAL_INT(0, wm_config_next_packet_id(&cfg, &b));
    TEST_ASSERT_EQUAL_UINT32(0xABCD0000, a);
    TEST_ASSERT_EQUAL_UINT32(0xABCD0001, b);
    /* the ceiling was persisted ahead of use */
    TEST_ASSERT_TRUE(m.pid_set);
    TEST_ASSERT_EQUAL_UINT32(0xABCD0000 + WM_PKTID_BLOCK, m.pid);
}

/* tiny deterministic LCG for crash-point variation (no Math/rand). */
static uint32_t lcg(uint32_t *st) { *st = *st * 1664525u + 1013904223u; return *st; }

/* Headline: across many forced reboots/crashes and >70k ids (past a 16-bit
 * wrap), packetId is strictly increasing -> never reused under one key. */
static void test_pktid_reboot_no_reuse_soak(void)
{
    mock_flash m = {0}; m.seed = 100;
    wm_store_t s = mock_store(&m);
    uint32_t rngst = 0xC0FFEE;

    uint64_t total = 0;
    int have_prev = 0;
    uint32_t prev = 0;
    uint32_t first = 0;
    int reboots = 0;

    /* run boots until we've issued well past a 16-bit wrap */
    while (total < 80000) {
        wm_config_t cfg;
        TEST_ASSERT_EQUAL_INT(0, wm_config_init(&cfg, &s));
        reboots++;

        /* Faithful power-loss-during-commit: it can only lose init's just-made
         * reserve write, and only when NO id has been issued from that block
         * yet (issuing one requires the commit to have already succeeded). So
         * model it as: crash right after init, roll the write back, reboot. */
        if ((lcg(&rngst) & 7) == 0) { mf_rollback_pid(&m); continue; }

        /* otherwise a clean reboot: issue a variable number of ids (often
         * crossing block boundaries -> more reserve-ahead writes) */
        uint32_t n = 200 + (lcg(&rngst) % 2500);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t id;
            TEST_ASSERT_EQUAL_INT(0, wm_config_next_packet_id(&cfg, &id));
            if (!have_prev) { first = id; have_prev = 1; }
            else {
                /* strictly increasing across the whole run == no reuse */
                TEST_ASSERT_TRUE_MESSAGE(id > prev, "packetId reuse/non-monotonic");
            }
            prev = id;
            total++;
        }
    }

    TEST_ASSERT_TRUE(total >= 80000);
    /* sanity: waste is bounded (each reboot skips at most ~2 blocks) */
    uint64_t span = (uint64_t)prev - first;
    TEST_ASSERT_TRUE(span < total + (uint64_t)reboots * 2 * WM_PKTID_BLOCK);
}

/* H1 regression: when the ceiling persist FAILS (flash commit error, distinct
 * from a power-loss-after-success), next_packet_id must REFUSE to issue an id
 * at/above the committed ceiling — otherwise a reboot re-issues it (nonce reuse,
 * §8). It must recover cleanly once the persist works again. */
static void test_pktid_persist_fail_refuses(void)
{
    mock_flash m = {0}; m.seed = 0x5000;
    wm_store_t s = mock_store(&m);
    wm_config_t cfg;
    TEST_ASSERT_EQUAL_INT(0, wm_config_init(&cfg, &s)); /* ceiling=0x5000+BLOCK */

    /* Drain the reserved block: every id stays strictly below the ceiling. */
    uint32_t id = 0, last = 0;
    for (uint32_t i = 0; i < WM_PKTID_BLOCK; i++) {
        TEST_ASSERT_EQUAL_INT(0, wm_config_next_packet_id(&cfg, &id));
        last = id;
    }
    TEST_ASSERT_EQUAL_UINT32(0x5000 + WM_PKTID_BLOCK - 1, last);

    /* The next id needs a fresh reserve; make that commit fail. */
    m.fail_pid_put = 1;
    uint32_t sentinel = 0xDEADBEEF;
    id = sentinel;
    TEST_ASSERT_EQUAL_INT(-1, wm_config_next_packet_id(&cfg, &id)); /* refused */
    TEST_ASSERT_EQUAL_UINT32(sentinel, id);  /* *out_id untouched on failure */
    /* Refuses repeatedly while the flash stays broken — never hands one out. */
    TEST_ASSERT_EQUAL_INT(-1, wm_config_next_packet_id(&cfg, &id));

    /* Flash recovers: it issues again, strictly above the last good id. */
    m.fail_pid_put = 0;
    TEST_ASSERT_EQUAL_INT(0, wm_config_next_packet_id(&cfg, &id));
    TEST_ASSERT_TRUE_MESSAGE(id > last, "reissued an id at/below old ceiling");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_seed);
    RUN_TEST(test_channel_add_and_policy);
    RUN_TEST(test_persist_roundtrip);
    RUN_TEST(test_pktid_fresh_seed);
    RUN_TEST(test_pktid_reboot_no_reuse_soak);
    RUN_TEST(test_pktid_persist_fail_refuses);
    return UNITY_END();
}
