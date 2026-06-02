#pragma once
// =============================================================================
// wm_portal — ESP8285 ELRS-style WiFi config portal (doc 13 §8.4).
//
// The 8285 is WiFi-only (no BLE), so it cannot be provisioned from the Meshtastic
// app like the C3. This stands up a SoftAP (`Waymesh_XXXX` @ 10.0.0.1, WPA2) +
// captive web form so the home channel (name+PSK), the relay policy
// (relay-all | relay-known) are set over WiFi and written to the SAME wm_config
// store the C3 fills over BLE. Both end at one store (here: EEPROM emulation).
//
// RF coexistence (§8.4): WiFi and the SX1280 are both 2.4 GHz, so the CALLER must
// suspend LoRa (radio sleep, stop beacon/relay) before wmPortalBegin() and resume
// after wmPortalEnd(). On Save the portal asks for a reboot (clean reload of the
// new config); on inactivity it just closes.
//
// Compiled only in -DWAYMESH_WIFI_CONFIG builds.
// =============================================================================

#if defined(WAYMESH_WIFI_CONFIG) && WAYMESH_WIFI_CONFIG

#include <stdint.h>
#include "waymesh_config.h"

// Bring up the SoftAP + DNS captive redirect + HTTP form, bound to *cfg (read for
// prefill, written on Save). nodeId -> SSID "Waymesh_%04X". Returns true on success.
bool wmPortalBegin(wm_config_t *cfg, uint32_t nodeId);

// Pump DNS + HTTP once per loop. Returns true while the portal should stay up;
// returns false when it should close (Save applied, or inactivity timeout).
bool wmPortalService(void);

// True once a Save has been applied and the device should reboot to reload config.
bool wmPortalRebootRequested(void);

// Tear down HTTP + DNS + SoftAP (caller then resumes LoRa, or reboots).
void wmPortalEnd(void);

#endif  // WAYMESH_WIFI_CONFIG
