// =============================================================================
// wm_store_eeprom — see wm_store_eeprom.h. Fixed binary layout in one EEPROM
// (flash-sector) region:
//
//   off 0  : magic  u32   0x574D4543 "WMEC" — set on the first write; absent =>
//                          fresh/erased flash, so get_* return -1 and the lib
//                          seeds the LongFast/psk=01 default + a fresh packetId
//                          block (wm_config_init).
//   off 4  : pid_ceiling u32   (WM_KEY_PID — the reboot-safe high-water, §8)
//   off 8  : chans_len   u16   (0 / 0xFFFF => unprovisioned: default re-seeded)
//   off 10 : chans_blob[]      (WM_KEY_CHANS — waymesh_config's channel blob)
//
// One u32 write per packetId reserve-ahead; one blob write per channel-set — the
// same write pattern documented for the C3 NVS backend.
//
// Compiled ONLY in -DWAYMESH_WIFI_CONFIG builds (chain+ LDF): the silent relay
// firmware never pulls in waymesh_config / EEPROM.
// =============================================================================
#if defined(WAYMESH_WIFI_CONFIG) && WAYMESH_WIFI_CONFIG

#include "wm_store_eeprom.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

// Mirror waymesh_config.c's private WM_CONFIG_BLOB_MAX from the public sizes.
#define WM_EE_BLOB_MAX \
    (3 + WM_MAX_CHANNELS * (3 + WM_CHAN_NAME_MAX + WM_PSK_MAX))

#define WM_EE_OFF_MAGIC   0
#define WM_EE_OFF_PID     4
#define WM_EE_OFF_CHANLEN 8
#define WM_EE_OFF_CHANS   10
#define WM_EE_SIZE        (WM_EE_OFF_CHANS + WM_EE_BLOB_MAX)
#define WM_EE_MAGIC       0x574D4543u  // "WMEC"

static bool magic_ok(void) {
    uint32_t m = 0;
    EEPROM.get(WM_EE_OFF_MAGIC, m);
    return m == WM_EE_MAGIC;
}

static void set_magic(void) {
    uint32_t m = WM_EE_MAGIC;
    EEPROM.put(WM_EE_OFF_MAGIC, m);  // RAM buffer; caller commits
}

// WM_KEY_PID is the only u32 key the lib uses.
static int ee_get_u32(void *ctx, const char *key, uint32_t *out) {
    (void)ctx; (void)key;
    if (!magic_ok()) return -1;          // fresh NVS -> lib seeds from rng()
    EEPROM.get(WM_EE_OFF_PID, *out);
    return 0;
}

static int ee_put_u32(void *ctx, const char *key, uint32_t v) {
    (void)ctx; (void)key;
    set_magic();
    EEPROM.put(WM_EE_OFF_PID, v);
    return EEPROM.commit() ? 0 : -1;     // a failed persist is a hard fault (§8)
}

// WM_KEY_CHANS is the only blob key the lib uses.
static int ee_get_blob(void *ctx, const char *key, void *out, size_t cap,
                       size_t *out_len) {
    (void)ctx; (void)key;
    if (!magic_ok()) return -1;
    uint16_t len = 0;
    EEPROM.get(WM_EE_OFF_CHANLEN, len);
    if (len == 0 || len == 0xFFFF) return -1;        // unprovisioned -> default
    if (len > WM_EE_BLOB_MAX || (size_t)len > cap) return -1;
    uint8_t *o = (uint8_t *)out;
    for (uint16_t i = 0; i < len; i++) o[i] = EEPROM.read(WM_EE_OFF_CHANS + i);
    *out_len = len;
    return 0;
}

static int ee_put_blob(void *ctx, const char *key, const void *data, size_t len) {
    (void)ctx; (void)key;
    if (len > WM_EE_BLOB_MAX) return -1;
    const uint8_t *d = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) EEPROM.write(WM_EE_OFF_CHANS + i, d[i]);
    uint16_t l = (uint16_t)len;
    EEPROM.put(WM_EE_OFF_CHANLEN, l);
    set_magic();
    return EEPROM.commit() ? 0 : -1;
}

// HW-ish RNG seed — used ONLY to seed the packetId high-water on fresh/corrupt
// NVS (§8). RANDOM_REG32 is weak without RF active, so mix in micros()+chipId;
// monotonicity thereafter is guaranteed by the reserve-ahead ceiling, not this.
static uint32_t ee_rng(void *ctx) {
    (void)ctx;
    return (uint32_t)RANDOM_REG32 ^ (uint32_t)micros() ^ ESP.getChipId();
}

void wm_store_eeprom_begin(wm_store_t *out) {
    EEPROM.begin(WM_EE_SIZE);
    out->get_u32 = ee_get_u32;
    out->put_u32 = ee_put_u32;
    out->get_blob = ee_get_blob;
    out->put_blob = ee_put_blob;
    out->rng = ee_rng;
    out->ctx = NULL;
}

#endif  // WAYMESH_WIFI_CONFIG
