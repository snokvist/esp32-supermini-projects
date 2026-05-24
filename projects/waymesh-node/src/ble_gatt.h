#pragma once
#include <stdint.h>

// Meshtastic-compatible BLE GATT server — Phase G.
// See docs/hybrid-mesh/11-mobile-gateway-meshtastic-compat.md.
//
// LAYER 2: real Meshtastic protobufs over the three core characteristics
// (FromRadio/read, ToRadio/write, FromNum/read+notify). On a ToRadio
// want_config_id the node answers the connect handshake — MyNodeInfo,
// DeviceMetadata, the self NodeInfo (with the live GPS position), then
// config_complete_id — so an unmodified Meshtastic client (`meshtastic --ble
// --info`, phone app) sees this node as a Meshtastic device. FromRadio is a
// drained queue: each read pops the next frame, empty when caught up; FromNum
// notifies when new frames are queued. nanopb runtime + generated code are
// vendored in lib/nanopb/ (regen: proto/README.md). No bonding yet.
//
// Layer 1 (bare transport, stub payloads) was device-verified on the XR2
// 2026-05-24 (advertise/connect/read/notify/write, ATT MTU 517, LoRa
// coexistence). TX (phone->mesh) and channel/config-write are later increments.

// Bring up the GATT server + advertising. nodeId names the device (Waymesh_XXXX)
// and is the Meshtastic node number reported to the client.
void bleGattBegin(uint32_t nodeId);

// Periodic service: flushes a pending FromNum notify to a connected client.
void bleGattLoop();

// Feed the latest GPS fix for the self NodeInfo/Position reported in the
// handshake. lat_i/lon_i are degrees * 1e7 (the Meshtastic Position encoding).
void bleGattSetPosition(int32_t lat_i, int32_t lon_i, uint32_t sats_in_view,
                        bool valid);

// Feed the current UTC epoch (seconds) from GNSS, 0 if unknown. Used for
// NodeInfo.last_heard on self and peers.
void bleGattSetTime(uint32_t epoch);

// Record a peer heard over LoRa (Phase G 1b). Upserts the gateway node DB; while
// a client is connected it also enqueues a live NodeInfo (rate-limited) so the
// peer appears/updates in the Meshtastic app. Called from the main loop task
// (handleRx). lat_i/lon_i are degrees * 1e7; pos_valid gates the Position.
void bleGattOnPeer(uint32_t node_id, int32_t lat_i, int32_t lon_i,
                   uint32_t sats_in_view, bool pos_valid, float snr);
