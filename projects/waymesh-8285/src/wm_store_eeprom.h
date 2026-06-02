#pragma once
// =============================================================================
// wm_store_eeprom — ESP8266/ESP8285 EEPROM-emulation backend for waymesh_config
// (doc 13 §8.2 / §8.4). The portable wm_config layer (lib/waymesh_config) talks
// to a pluggable wm_store_t key-value backend; the C3 binds it to NVS/Preferences,
// the 8285 tier binds it here to the ESP8266 EEPROM emulation (one flash sector,
// RAM-buffered, .commit()-flushed). This is the same store the WiFi config portal
// writes — the BLE-less twin of the C3's BLE channel-set, both ending at one store.
//
// The wm_store_t *logic* is host-tested (lib mock, waymesh-node/test/test_config);
// this shim is just the device wiring (verified on-device: provision -> reboot ->
// dump persists). Holds exactly the two keys the lib uses: WM_KEY_PID (the
// reboot-safe packetId high-water, u32) and WM_KEY_CHANS (the channel blob).
// =============================================================================

#include "waymesh_config.h"  // wm_store_t, WM_KEY_* sizes

#ifdef __cplusplus
extern "C" {
#endif

// Initialise EEPROM emulation and populate *out with accessors bound to it.
// Call ONCE in setup() before wm_config_init(out-bound cfg, ...).
void wm_store_eeprom_begin(wm_store_t *out);

#ifdef __cplusplus
}
#endif
