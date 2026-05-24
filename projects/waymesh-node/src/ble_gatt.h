#pragma once
#include <stdint.h>

// Meshtastic-compatible BLE GATT server — Phase G, increment 1 (transport bring-up).
// See docs/hybrid-mesh/11-mobile-gateway-meshtastic-compat.md.
//
// LAYER 1 ONLY: stands up the Meshtastic client service + its three core
// characteristics (FromRadio/read, ToRadio/write, FromNum/read+notify) with
// STUB payloads, so the BLE path (C3 radio, XR2 antenna, host link, MTU) can be
// validated from a host (bluetoothctl / bleak) before the protobuf handshake
// (Layer 2) is layered on top. No protobufs, no bonding yet.
//
// Device-verified on the RadioMaster XR2 (2026-05-24): advertises Waymesh_XXXX,
// host connects (ATT MTU negotiated to 517), service + 3 chars discovered,
// FromRadio stub read returns DE AD BE EF, FromNum notifies a rising counter,
// ToRadio write echoes to serial — all while the Phase 0 LoRa beacon loop keeps
// running on the shared 2.4 GHz radio (badcrc=0). Harness: tools/ble_gatt_test.py.

// Bring up the GATT server + advertising. nodeId names the device (Waymesh_XXXX).
void bleGattBegin(uint32_t nodeId);

// Periodic service: bumps + notifies FromNum while a client is connected.
void bleGattLoop();
