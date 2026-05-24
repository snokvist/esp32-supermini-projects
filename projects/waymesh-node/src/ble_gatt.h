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

// Bring up the GATT server + advertising. nodeId names the device (Waymesh_XXXX).
void bleGattBegin(uint32_t nodeId);

// Periodic service: bumps + notifies FromNum while a client is connected.
void bleGattLoop();
