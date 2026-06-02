/*
 * config_nvs — ESP32-C3 NVS (Preferences) backend for waymesh_config.
 *
 * Device-only (Arduino/Preferences); it is in src/ so it is compiled into the
 * firmware but NOT into the host test build (pio test -e native does not build
 * project src). The portable reserve-ahead / channel logic is host-tested with
 * a mock backend (test/test_config); this is the thin shim onto real flash.
 */
#ifndef WAYMESH_CONFIG_NVS_H
#define WAYMESH_CONFIG_NVS_H

#include "waymesh_config.h"

/* A wm_store_t backed by the "waymesh" NVS namespace (opened on first call).
 * Keys: WM_KEY_PID (u32 high-water), WM_KEY_CHANS (channel blob). rng() uses the
 * hardware RNG to seed the packetId base on fresh/corrupt NVS. */
wm_store_t wm_nvs_store(void);

#endif /* WAYMESH_CONFIG_NVS_H */
