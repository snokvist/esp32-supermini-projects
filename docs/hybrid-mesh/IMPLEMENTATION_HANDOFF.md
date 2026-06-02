# Implementation Handoff — Auth & Group Identity

A ready-to-use master prompt for a fresh Claude Code CLI session to continue
implementing the auth/group-identity work specified in
[13 — Auth & Groups](13-auth-and-groups.md). Paste the block below as the opening
prompt.

```
You are continuing work on the Waymesh project (ESP32 LoRa positioning mesh) in
this repo. Branch: claude/waystastic-auth-network-Ptc46 (open as PR #39 — push here,
don't open a new PR). Develop on that branch; commit + push as you go.

GOAL: implement authentication + group identity for Waymesh by reusing Meshtastic's
channel+PSK scheme, so the stock Meshtastic app stays the UI. The full, reviewed
spec is the source of truth — READ IT FIRST:
  docs/hybrid-mesh/13-auth-and-groups.md
Supporting context: docs/hybrid-mesh/05-protocol.md (wire format),
11-mobile-gateway-meshtastic-compat.md (BLE gateway), 04-architecture.md,
09-poc-roadmap.md (phases). Firmware: projects/waymesh-node (ESP32-C3 + LR1121, the
BLE gateway) and projects/waymesh-8285 (ESP8285 + SX1280 relay). Proto lives at
projects/waymesh-node/proto/waymesh_mesh.proto (+ vendored lib/nanopb).

NON-NEGOTIABLE CONSTRAINTS (from the spec):
- Beacon header (magic..packetId incl. chanHash, srcNodeID, packetId) stays
  PLAINTEXT so the keyless Tier-3 dumb relay keeps re-flooding verbatim. Crypto is
  end-to-end originator->gateway only.
- Only THREE things must byte-match upstream Meshtastic: the channel hash, the
  PSK/default-key expansion, and decrypting the phone's CTR-encrypted MeshPacket on
  the TX path. Our OTA payload cipher is ours: use AES-CCM (AEAD), 4-byte tag.
- packetId must be a reboot-safe monotonic counter (NVS reserve-ahead) — nonce reuse
  under one key is a hard break. NO NVS/Preferences layer exists yet; build one.
- Keep it simpler than Meshtastic: shared-key per channel, no PKI/per-node sigs.

BUILD ORDER (dependency-ordered; each step builds + is tested before the next):
1. Crypto core, host-unit-tested, NO radio: chanHash + PSK/key-expansion validated
   against vectors GENERATED from the upstream `meshtastic` CLI at the pinned
   version (generate-with, don't copy-from — upstream is GPL-3.0); AES-CCM
   seal/open against own known-answer vectors. This is the highest-value gate —
   green before any RF.
2. NVS/config store: channel list + packetId reserve-ahead high-water.
3. Phase 1 — group filter only (v2 beacon chanHash + acceptance step 3 + relay
   policy relay-all/relay-known), positions still clear.
4. Phase 2 — AES-CCM encryption + tag on the beacon; gateway opens/strips.
5. Proto additions (Channel, ChannelSettings, trimmed AdminMessage,
   FromRadio.channel=10) + gateway: advertise channels, TX text, runtime
   set_channel->NVS->re-advertise.
6. ESP8285 ELRS-style WiFi config portal (SoftAP 10.0.0.1) + relay-known work.

LOCK THESE TWO BEFORE WRITING WIRE/PROTO CODE (currently the only open items):
- The exact pinned upstream meshtastic/protobufs commit (record it alongside the
  pinned firmware_version in ble_gatt.cpp).
- The AdminMessage field numbers + session-passkey behavior, verified against that
  pinned admin.proto / channel.proto.

REPO CONVENTION: each phase ends with build -> flash -> observe -> report. Start
with step 1; show me the test-vector harness and proposed file layout before
expanding scope. Ask me if anything in the spec is ambiguous rather than guessing.
```
