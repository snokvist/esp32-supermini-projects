#pragma once
#include <stddef.h>
#include <stdint.h>

#include "waymesh_config.h"  // wm_config_t — the advertised channel set

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

// Register the provisioned channel set (docs/hybrid-mesh/13 §7): the gateway
// advertises one FromRadio{channel} per channel during the want_config
// handshake so the stock app adopts our groups, and tags emitted NodeInfo /
// Position with the channel index a node was heard on. Call once after
// wm_config_init (the pointer must outlive the gateway — gCfg is process
// lifetime). Channels are read-only in this phase (runtime set_channel is a
// later step), so the BLE-task handshake reads cfg without a lock.
void bleGattSetChannels(const wm_config_t *cfg);

// Bring up the GATT server + advertising. nodeId names the device (Waymesh_XXXX)
// and is the Meshtastic node number reported to the client.
void bleGattBegin(uint32_t nodeId);

// Periodic service: flushes a pending FromNum notify to a connected client.
void bleGattLoop();

// Feed the latest GPS fix for the self NodeInfo/Position. lat_i/lon_i are
// degrees * 1e7 (the Meshtastic Position encoding). Besides updating the value
// reported in the want_config handshake, while a client is connected this also
// enqueues a live self NodeInfo (rate-limited, mirrors bleGattOnPeer) so THIS
// node's position keeps streaming after ConfigComplete, not only once. Call
// bleGattSetTime() before this so the emitted NodeInfo carries the fresh epoch.
void bleGattSetPosition(int32_t lat_i, int32_t lon_i, uint32_t sats_in_view,
                        bool valid);

// Feed the current UTC epoch (seconds) from GNSS, 0 if unknown. Used for
// NodeInfo.last_heard on self and peers.
void bleGattSetTime(uint32_t epoch);

// Periodic self announcement (rate-limited, gated on the want_config handshake)
// so this node stays in a connected client's node list like the LoRa peers do,
// even with no GPS fix — emits a live self NodeInfo carrying the last known fix
// if any, else a bare no-pos NodeInfo (never a 0,0 Position). Call from the
// beacon-TX cadence in loop(); shares the rate-limit gate with bleGattSetPosition.
void bleGattHeartbeat();

// Record a peer heard over LoRa (Phase G 1b). Upserts the gateway node DB; while
// a client is connected it also enqueues a live NodeInfo (rate-limited) so the
// peer appears/updates in the Meshtastic app. Called from the main loop task
// (handleRx). lat_i/lon_i are degrees * 1e7; pos_valid gates the Position.
// chan_hash is the group the peer was heard on (the v2 beacon chanHash); the
// gateway maps it to the Meshtastic channel index so the app shows the peer on
// the right channel (§7).
void bleGattOnPeer(uint32_t node_id, int32_t lat_i, int32_t lon_i,
                   uint32_t sats_in_view, bool pos_valid, float snr,
                   uint8_t chan_hash);

// A decrypted channel TEXT message heard over LoRa (§7.3): projected to a
// MeshPacket{TEXT_MESSAGE_APP} on the matching channel index so it shows in the
// app's channel chat. Called from the main loop (handleRx) after wm_beacon_open.
void bleGattOnText(uint32_t src_id, uint8_t chan_hash, const uint8_t *text,
                   size_t len, float snr);

// Drain one app->OTA text the phone wrote (write path, §7.3). Copies the text
// into out (cap bytes) and its channel hash into *chan_hash; returns the length
// (>=0) or -1 if none pending. Called from the main loop, which then builds +
// transmits a v2 TEXT beacon (it owns the radio + packetId/NVS). */
int bleGattPopAppText(uint8_t *chan_hash, uint8_t *out, size_t cap);
