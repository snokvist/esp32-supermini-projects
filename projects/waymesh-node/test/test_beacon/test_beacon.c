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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_v2_byte_layout);
    RUN_TEST(test_v2_roundtrip);
    RUN_TEST(test_legacy_parse);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_accept_group_filter);
    RUN_TEST(test_relay_policy);
    return UNITY_END();
}
