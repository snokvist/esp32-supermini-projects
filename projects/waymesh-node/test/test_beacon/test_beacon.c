/* Host unit test: Waymesh v2 beacon codec + RX/relay decisions (doc 13 §3/§6).
 * Locks the exact wire byte layout the keyless relay depends on, the v0/v1
 * back-compat parse, and the group-filter / relay-policy decisions.
 * Run: pio test -e native -f test_beacon */
#include <string.h>
#include <unity.h>

#include "wm_beacon.h"
#include "waymesh_config.h"

/* minimal in-RAM store so we can build a wm_config_t for the decision tests */
typedef struct { uint32_t pid; int pid_set; uint8_t blob[512]; size_t blen; int bset; } flash;
static int g_u32(void *c, const char *k, uint32_t *o){ flash*m=c; (void)k; if(m->pid_set){*o=m->pid;return 0;} return -1; }
static int p_u32(void *c, const char *k, uint32_t v){ flash*m=c; (void)k; m->pid=v; m->pid_set=1; return 0; }
static int g_blob(void *c, const char *k, void *o, size_t cap, size_t *ol){ flash*m=c; (void)k; if(!m->bset||m->blen>cap)return -1; memcpy(o,m->blob,m->blen);*ol=m->blen;return 0; }
static int p_blob(void *c, const char *k, const void *d, size_t l){ flash*m=c; (void)k; if(l>sizeof(m->blob))return -1; memcpy(m->blob,d,l);m->blen=l;m->bset=1;return 0; }
static uint32_t g_rng(void *c){ (void)c; return 42; }

static flash g_flash;
static wm_config_t g_cfg;

void setUp(void) {
    memset(&g_flash, 0, sizeof(g_flash));
    wm_store_t s = { g_u32, p_u32, g_blob, p_blob, g_rng, &g_flash };
    wm_config_init(&g_cfg, &s);  /* seeds default channel -> hash 8 */
}
void tearDown(void) {}

/* The v2 header byte layout is exactly what a keyless relay reads. */
static void test_v2_byte_layout(void)
{
    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_clear(buf, 0x2A, 0x11223344, 0xAABBCCDD,
                                        true, 595000000, 168600000, 7);
    TEST_ASSERT_EQUAL_UINT(WM_BEACON_V2_HDR + WM_BEACON_POS_LEN, n); /* 22 */
    TEST_ASSERT_EQUAL_HEX8(0x57, buf[0]);   /* magic */
    TEST_ASSERT_EQUAL_HEX8(2,    buf[1]);   /* version */
    TEST_ASSERT_EQUAL_HEX8(0x2A, buf[2]);   /* chanHash */
    TEST_ASSERT_EQUAL_HEX8(WM_BFLAG_POS, buf[3]); /* clear, POS, no MIC */
    /* srcNodeID LE at 4-7, packetId LE at 8-11 — the dedup/relay fields */
    TEST_ASSERT_EQUAL_HEX8(0x44, buf[4]); TEST_ASSERT_EQUAL_HEX8(0x11, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, buf[8]); TEST_ASSERT_EQUAL_HEX8(0xAA, buf[11]);
}

/* build -> parse round-trips all fields (with and without POS). */
static void test_v2_roundtrip(void)
{
    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_clear(buf, 0x2A, 0x11223344, 0xAABBCCDD,
                                        true, 595000000, 168600000, 7);
    wm_beacon_t b;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_EQUAL_UINT8(WM_BEACON_V2, b.version);
    TEST_ASSERT_EQUAL_HEX8(0x2A, b.chan_hash);
    TEST_ASSERT_EQUAL_HEX32(0x11223344, b.src_id);
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDD, b.packet_id);
    TEST_ASSERT_TRUE(b.has_pos);
    TEST_ASSERT_EQUAL_INT32(595000000, b.lat_i);
    TEST_ASSERT_EQUAL_INT32(168600000, b.lon_i);
    TEST_ASSERT_EQUAL_UINT8(7, b.sats);

    /* presence-only beacon = bare 12-byte header */
    n = wm_beacon_build_v2_clear(buf, 0x2A, 1, 2, false, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT(WM_BEACON_V2_HDR, n);
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_FALSE(b.has_pos);
}

/* v0 (8 B) and v1 (18 B) legacy frames parse as the open group. */
static void test_legacy_parse(void)
{
    /* v0: magic,ver0,srcId LE,seq LE */
    uint8_t v0[8] = {0x57, 0, 0x44,0x33,0x22,0x11, 0x39,0x05};
    wm_beacon_t b;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(v0, sizeof(v0), &b));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, b.src_id);
    TEST_ASSERT_EQUAL_UINT32(0x0539, b.packet_id);
    TEST_ASSERT_EQUAL_INT(WM_OPEN_GROUP_HASH, b.chan_hash);
    TEST_ASSERT_FALSE(b.has_pos);

    /* v1: + lat,lon,sats,flags(POS) */
    uint8_t v1[18] = {0x57, 1, 0x44,0x33,0x22,0x11, 0x39,0x05,
                      0x00,0x5E,0x73,0x23, 0x00,0xCA,0x0A,0x0A, 9, 0x01};
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(v1, sizeof(v1), &b));
    TEST_ASSERT_TRUE(b.has_pos);
    TEST_ASSERT_EQUAL_UINT8(9, b.sats);
    TEST_ASSERT_EQUAL_INT(WM_OPEN_GROUP_HASH, b.chan_hash);
}

/* malformed frames are rejected. */
static void test_parse_rejects(void)
{
    wm_beacon_t b;
    uint8_t bad_magic[12] = {0x58, 2};
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_parse(bad_magic, 12, &b));
    uint8_t short_v2[11] = {0x57, 2};
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_parse(short_v2, 11, &b)); /* < 12 hdr */
    uint8_t unknown[12] = {0x57, 9};
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_parse(unknown, 12, &b));
    uint8_t short_v0[7] = {0x57, 0};
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_parse(short_v0, 7, &b));
}

/* group filter: self drops, foreign chanHash drops, accepted accepts; legacy
 * frames ride the open group (default channel hash 8 is configured). */
static void test_accept_group_filter(void)
{
    wm_beacon_t b;
    uint8_t buf[WM_BEACON_V2_MAX];

    /* own srcId -> DROP_SELF */
    size_t n = wm_beacon_build_v2_clear(buf, 8, 0xDEAD, 1, false, 0,0,0);
    wm_beacon_parse(buf, n, &b);
    TEST_ASSERT_EQUAL_INT(WM_RX_DROP_SELF, wm_beacon_accept(&b, 0xDEAD, &g_cfg));

    /* accepted group (default hash 8) -> ACCEPT */
    TEST_ASSERT_EQUAL_INT(WM_RX_ACCEPT, wm_beacon_accept(&b, 0x1, &g_cfg));

    /* foreign group (hash 99 not configured) -> DROP_FOREIGN_GROUP */
    n = wm_beacon_build_v2_clear(buf, 99, 0xBEEF, 1, false, 0,0,0);
    wm_beacon_parse(buf, n, &b);
    TEST_ASSERT_EQUAL_INT(WM_RX_DROP_FOREIGN_GROUP, wm_beacon_accept(&b, 0x1, &g_cfg));

    /* a private group, once added, is accepted */
    uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    wm_config_add_channel(&g_cfg, "team-blue", key, 16, 1); /* hash 46 */
    n = wm_beacon_build_v2_clear(buf, 46, 0xBEEF, 1, false, 0,0,0);
    wm_beacon_parse(buf, n, &b);
    TEST_ASSERT_EQUAL_INT(WM_RX_ACCEPT, wm_beacon_accept(&b, 0x1, &g_cfg));
}

/* relay policy: relay-all carries foreign groups; relay-known only configured. */
static void test_relay_policy(void)
{
    wm_beacon_t b;
    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_clear(buf, 200, 0xBEEF, 1, false, 0,0,0);
    wm_beacon_parse(buf, n, &b);

    TEST_ASSERT_TRUE(wm_beacon_should_relay(&b, &g_cfg));   /* default relay-all */
    wm_config_set_relay_policy(&g_cfg, WM_RELAY_KNOWN);
    TEST_ASSERT_FALSE(wm_beacon_should_relay(&b, &g_cfg));  /* foreign dropped */

    n = wm_beacon_build_v2_clear(buf, 8, 0xBEEF, 1, false, 0,0,0); /* default */
    wm_beacon_parse(buf, n, &b);
    TEST_ASSERT_TRUE(wm_beacon_should_relay(&b, &g_cfg));   /* known kept */
}

/* --- phase 2: AES-CCM encryption + MIC (§5, §10 crypto round-trip/negative) - */

/* The home channel's expanded key (default "LongFast" -> the famous default key,
 * 16 B). Sealing/opening uses exactly the key the channel store derives. */
static const wm_channel_t *home(void) { return wm_config_home(&g_cfg); }

/* encrypted build is 26 B with a CLEAR header (relays/dedup read it keyless) and
 * the POS bits hidden; parse exposes no position until opened. */
static void test_v2_enc_layout(void)
{
    const wm_channel_t *h = home();
    TEST_ASSERT_EQUAL_size_t(16, h->key_len);   /* default PSK -> AES-128 */

    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_enc(buf, (uint8_t)h->hash, 0x11223344,
                                      0xAABBCCDD, 595000000, 168600000, 7,
                                      h->expanded_key, h->key_len);
    TEST_ASSERT_EQUAL_UINT(WM_BEACON_V2_MAX, n); /* 12 + 10 + 4 = 26 */

    /* header is clear and exactly the relay/dedup fields */
    TEST_ASSERT_EQUAL_HEX8(WM_BEACON_MAGIC, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(WM_BEACON_V2, buf[1]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)h->hash, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(WM_BFLAG_POS | WM_BFLAG_ENCRYPTED | WM_BFLAG_HASMIC,
                           buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x44, buf[4]); TEST_ASSERT_EQUAL_HEX8(0xDD, buf[8]);

    /* parse leaves has_pos false (ciphertext is never trusted as a position) */
    wm_beacon_t b;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_FALSE(b.has_pos);
    TEST_ASSERT_EQUAL_size_t(WM_BEACON_POS_LEN, b.payload_len);
    TEST_ASSERT_EQUAL_size_t(WM_BEACON_MIC_LEN, b.mic_len);

    /* ciphertext must NOT equal the cleartext POS bytes (it is encrypted) */
    uint8_t clear[WM_BEACON_V2_MAX];
    wm_beacon_build_v2_clear(clear, (uint8_t)h->hash, 0x11223344, 0xAABBCCDD,
                             true, 595000000, 168600000, 7);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buf + WM_BEACON_V2_HDR,
                                    clear + WM_BEACON_V2_HDR, WM_BEACON_POS_LEN));
}

/* seal -> parse -> open recovers the exact position. */
static void test_v2_enc_roundtrip(void)
{
    const wm_channel_t *h = home();
    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_enc(buf, (uint8_t)h->hash, 0xCAFEBABE,
                                      0x00DECADE, -123456789, 987654321, 12,
                                      h->expanded_key, h->key_len);
    wm_beacon_t b;
    uint8_t pt[WM_BEACON_TEXT_MAX]; size_t pl = 0;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_open(&b, h->expanded_key, h->key_len,
                                            pt, sizeof(pt), &pl));
    TEST_ASSERT_TRUE(b.has_pos);
    TEST_ASSERT_EQUAL_size_t(WM_BEACON_POS_LEN, pl);
    TEST_ASSERT_EQUAL_INT32(-123456789, b.lat_i);
    TEST_ASSERT_EQUAL_INT32(987654321, b.lon_i);
    TEST_ASSERT_EQUAL_UINT8(12, b.sats);
}

/* TEXT beacon: build -> parse (has_pos stays false) -> open recovers the bytes. */
static void test_v2_text_roundtrip(void)
{
    const wm_channel_t *h = home();
    const char *msg = "hello waymesh \xf0\x9f\x93\xa1"; /* UTF-8 incl. a 4-byte glyph */
    size_t mlen = strlen(msg);
    uint8_t buf[WM_BEACON_FRAME_MAX];
    size_t n = wm_beacon_build_v2_text(buf, (uint8_t)h->hash, 0x11223344,
                                       0x55667788, (const uint8_t *)msg, mlen,
                                       h->expanded_key, h->key_len);
    TEST_ASSERT_EQUAL_UINT(WM_BEACON_V2_HDR + mlen + WM_BEACON_MIC_LEN, n);
    TEST_ASSERT_EQUAL_HEX8(WM_BFLAG_TEXT | WM_BFLAG_ENCRYPTED | WM_BFLAG_HASMIC,
                           buf[3]);

    wm_beacon_t b;
    uint8_t pt[WM_BEACON_TEXT_MAX]; size_t pl = 0;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_FALSE(b.has_pos);             /* TEXT is never read as a position */
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_open(&b, h->expanded_key, h->key_len,
                                            pt, sizeof(pt), &pl));
    TEST_ASSERT_FALSE(b.has_pos);
    TEST_ASSERT_EQUAL_size_t(mlen, pl);
    TEST_ASSERT_EQUAL_MEMORY(msg, pt, mlen);

    /* a flipped ciphertext byte fails the MIC -> DROP, no plaintext trusted */
    buf[WM_BEACON_V2_HDR] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_open(&b, h->expanded_key, h->key_len,
                                             pt, sizeof(pt), &pl));

    /* build rejects an over-long or empty text */
    TEST_ASSERT_EQUAL_UINT(0, wm_beacon_build_v2_text(buf, 8, 1, 2,
                                                      (const uint8_t *)msg, 0,
                                                      h->expanded_key,
                                                      h->key_len));
    TEST_ASSERT_EQUAL_UINT(0, wm_beacon_build_v2_text(buf, 8, 1, 2,
                                                      (const uint8_t *)msg,
                                                      WM_BEACON_TEXT_MAX + 1,
                                                      h->expanded_key,
                                                      h->key_len));
}

/* §10 negative: a wrong key fails the MIC and leaks no plaintext. */
static void test_v2_enc_wrong_key(void)
{
    const wm_channel_t *h = home();
    uint8_t buf[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_enc(buf, (uint8_t)h->hash, 1, 2,
                                      595000000, 168600000, 7,
                                      h->expanded_key, h->key_len);
    wm_beacon_t b;
    TEST_ASSERT_EQUAL_INT(0, wm_beacon_parse(buf, n, &b));

    uint8_t wrong[16]; memset(wrong, 0xA5, sizeof(wrong));
    uint8_t pt[WM_BEACON_TEXT_MAX]; size_t pl = 0;
    TEST_ASSERT_EQUAL_INT(-1, wm_beacon_open(&b, wrong, sizeof(wrong),
                                             pt, sizeof(pt), &pl));
    TEST_ASSERT_FALSE(b.has_pos);   /* no position exposed on auth failure */
}

/* §10 negative: a bit-flip in the ciphertext, the MIC, or the CLEAR header
 * (AAD) all fail MIC verification -> DROP, never silent acceptance. */
static void test_v2_enc_tamper(void)
{
    const wm_channel_t *h = home();
    uint8_t good[WM_BEACON_V2_MAX];
    size_t n = wm_beacon_build_v2_enc(good, (uint8_t)h->hash, 0xABCD1234,
                                      0x0BADF00D, 595000000, 168600000, 7,
                                      h->expanded_key, h->key_len);

    /* flip a ciphertext byte (payload) */
    int idx[] = { WM_BEACON_V2_HDR,            /* ciphertext */
                  WM_BEACON_V2_HDR + WM_BEACON_POS_LEN, /* MIC tail */
                  2,                            /* chanHash (AAD) */
                  3,                            /* flags (AAD) */
                  8 };                          /* packetId LSB (AAD + nonce) */
    for (size_t i = 0; i < sizeof(idx) / sizeof(idx[0]); i++) {
        uint8_t buf[WM_BEACON_V2_MAX];
        memcpy(buf, good, n);
        buf[idx[i]] ^= 0x01;
        wm_beacon_t b;
        /* a flipped header byte may change parsed fields but must still fail
         * open (AAD mismatch); a flipped ct/MIC byte fails the tag. */
        if (wm_beacon_parse(buf, n, &b) != 0) continue; /* unparseable = also rejected */
        uint8_t pt[WM_BEACON_TEXT_MAX]; size_t pl = 0;
        TEST_ASSERT_EQUAL_INT(-1, wm_beacon_open(&b, h->expanded_key, h->key_len,
                                                 pt, sizeof(pt), &pl));
        TEST_ASSERT_FALSE(b.has_pos);
    }
}

/* a keyless channel (key_len 0) cannot encrypt: build returns 0 so the caller
 * falls back to a clear beacon (header always clear, §5). */
static void test_v2_enc_no_key(void)
{
    uint8_t buf[WM_BEACON_V2_MAX];
    uint8_t key[16] = {0};
    TEST_ASSERT_EQUAL_UINT(0, wm_beacon_build_v2_enc(buf, 8, 1, 2, 0, 0, 0,
                                                     key, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_v2_byte_layout);
    RUN_TEST(test_v2_roundtrip);
    RUN_TEST(test_legacy_parse);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_accept_group_filter);
    RUN_TEST(test_relay_policy);
    RUN_TEST(test_v2_enc_layout);
    RUN_TEST(test_v2_enc_roundtrip);
    RUN_TEST(test_v2_text_roundtrip);
    RUN_TEST(test_v2_enc_wrong_key);
    RUN_TEST(test_v2_enc_tamper);
    RUN_TEST(test_v2_enc_no_key);
    return UNITY_END();
}
