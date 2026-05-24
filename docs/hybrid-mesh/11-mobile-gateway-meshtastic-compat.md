# 11 — Mobile/PC Gateway: Meshtastic Client Compatibility (BLE + Serial)

A design proposal (no firmware yet) — **promoted to the near-term track as
Phase G** ([09](09-poc-roadmap.md)). Goal: a connected client sees a Waymesh node
as a **Meshtastic device** — a **phone over BLE** *and* a **PC over USB serial**
(and optionally TCP) — so we reuse the polished Meshtastic apps / CLI
(Android / iOS / web / desktop / `meshtastic` Python CLI) as our UI instead of
building one. This serves the "lightweight mobile interaction" requirement
([01](01-vision-and-requirements.md)) at near-zero UI cost. The Meshtastic
client protocol is **transport-agnostic** (same protobufs over BLE, Serial, and
TCP), so once the protobuf layer exists, every transport is nearly free.

## Scope: client compatibility only (not over-the-air)

There are two distinct levels of "Meshtastic compatible":

- **(A) Client / BLE compatibility** — the phone app connects over BLE and sees
  our node as a Meshtastic device. **This is what we're building.**
- **(B) Over-the-air compatibility** — real Meshtastic nodes talk to ours over
  LoRa. This requires adopting Meshtastic's LoRa PHY + managed-flood protocol,
  which fights our hybrid two-plane design. **Explicit non-goal.**

The phone only ever sees the **BLE GATT interface**. It cannot tell that our radio
plane is our own protocol — **near-term a flat 2.4-LoRa mesh**
([04](04-architecture.md)); later, ESP-NOW clusters too
([12](12-end-goal-full-hybrid-mesh.md)). So this is a **compatibility shim at the
gateway** — the radio stays entirely ours. The node is a *translator* between its
internal model and the Meshtastic client protocol.

```
   Meshtastic app  <--BLE/GATT-->  [ Waymesh node: gateway shim ]  <--our radio-->  the mesh
   (thinks it's                     FromRadio/ToRadio protobufs        near-term: flat 2.4-LoRa
    talking to a                    <-> internal node/msg model        (deferred: ESP-NOW clusters,
    Meshtastic radio)                                                   aggregation, roles, DTN)
```

## BLE GATT surface to expose

Single service, three core characteristics (verify against the current
meshtastic/protobufs before implementing — these evolve):

| Element | UUID | Direction |
|---------|------|-----------|
| Service | `6ba1b218-15a8-461f-9fa8-5dcae273eafd` | — |
| **ToRadio** | `f75c76d2-129e-4dad-a1dd-7866124401e7` | phone → node (write) |
| **FromRadio** | `2c55e69e-4993-11ed-b878-0242ac120002` | node → phone (read) |
| **FromNum** | `ed9da18c-a800-4f66-a670-aa7547e34453` | node → phone (notify counter) |
| LogRadio (optional) | `5a3d6e49-06e6-4423-9944-e9de8cdf9547` | node → phone (notify) |

Implementation notes:
- Use **NimBLE-Arduino** (already used elsewhere in this repo) for the GATT
  server on the C3.
- **MTU:** negotiate a large MTU (~512). FromRadio packets can approach the
  Meshtastic `MAXPACKET` (~256–512 B); a 23-byte default MTU will fragment badly.
- **Bonding/PIN:** Meshtastic bonds with a PIN. Our node has no screen → use a
  fixed PIN (configurable), documented per-node.
- **Framing:** on BLE, each ToRadio write / FromRadio read carries **one**
  protobuf message — no stream framing. (Serial/TCP add stream framing — below.)

## Transports: BLE, Serial (USB), TCP

The same `FromRadio`/`ToRadio` protobufs ride every Meshtastic transport; only
the wrapping differs. Supporting **Serial** alongside BLE means the node also
appears as a Meshtastic device when plugged into a PC (`meshtastic --info`, the
web/desktop client over serial) — high value, low marginal cost.

| Transport | Wrapping | Use |
|-----------|----------|-----|
| **BLE** | one protobuf per GATT read/write; FromNum notifies | phone app |
| **Serial (USB-CDC)** | stream framing `0x94 0xC3 <len_hi> <len_lo> <protobuf>` @115200 | PC CLI / web client |
| **TCP** (optional) | same stream framing on port 4403 | LAN client; needs WiFi (power) |

### Serial framing & coexisting debug logs

- Each packet on the wire is `0x94 0xC3` + 16-bit big-endian length + protobuf
  (max ~512 B). The client scans the byte stream for the `0x94 0xC3` magic.
- **Human-readable debug logs can share the same UART**: any bytes *not* inside a
  `0x94 0xC3` frame are treated by Meshtastic clients as **debug/log text** (this
  is how real Meshtastic devices behave). So our structured logs and the protobuf
  frames interleave on one port without a separate channel.
- Cleaner option: emit logs as `LogRecord` protobufs (the `LogRadio`
  characteristic on BLE) instead of raw text, gated by a build flag.
- **Relationship to Phase 0:** the `waymesh-node` bring-up firmware currently
  prints **CSV metrics** ([10](10-experiments-and-metrics.md)) over USB — the
  right tool for bench measurement. The Meshtastic serial framing is a gateway
  (Phase 4) feature; when added, CSV logs simply become non-framed debug bytes,
  or move behind a `--meshtastic-serial` build flag. Phase 0 is unaffected.

## Protocol: protobufs over the characteristics

Payloads are Protocol Buffers. Embed **nanopb** + the Meshtastic `.proto`
definitions (small; Meshtastic itself runs on ESP32-class parts, and
waymesh-node is at ~23% flash). The two top-level messages:

- **`ToRadio`** (phone→node): oneof of `want_config_id` (start config),
  `packet` (a `MeshPacket` to send), `disconnect`, `heartbeat`.
- **`FromRadio`** (node→phone): oneof of `my_info` (`MyNodeInfo`), `node_info`
  (`NodeInfo`), `config`, `moduleConfig`, `channel`, `metadata`
  (`DeviceMetadata`), `packet` (`MeshPacket`), `config_complete_id`.

### Connect handshake

```mermaid
sequenceDiagram
    participant App as Meshtastic app
    participant GW as Waymesh node (gateway)
    App->>GW: BLE connect + bond (PIN)
    App->>GW: ToRadio{ want_config_id = nonce }
    GW-->>App: FromRadio{ my_info: MyNodeInfo }
    GW-->>App: FromRadio{ metadata: DeviceMetadata (fw version pin) }
    GW-->>App: FromRadio{ node_info: NodeInfo }   %% one per known node
    GW-->>App: FromRadio{ channel: Channel }      %% advertised channel(s)+PSK
    GW-->>App: FromRadio{ config / moduleConfig }
    GW-->>App: FromRadio{ config_complete_id = nonce }
    Note over App,GW: steady state
    GW-->>App: FromNum++ (notify) ; App reads FromRadio until caught up
    App->>GW: ToRadio{ packet: MeshPacket } (e.g. text to send)
```

The node bumps **FromNum** whenever new FromRadio data is queued; the app reads
FromRadio until it catches up to that counter.

## Data-model mapping (ours → Meshtastic)

The read path is clean because we already hold all of this:

| Waymesh | Meshtastic | Notes |
|---------|------------|-------|
| NodeID (32-bit) | node num (`uint32`) | direct |
| node identity | `NodeInfo.user` (`User.long_name/short_name/id`) | `id` as `!aabbccdd` |
| LP position (int32 1e-7°) | `Position.latitude_i/longitude_i` | **identical encoding** |
| neighbor presence + freshness | `NodeInfo` (`last_heard`, `snr`) | de-aggregated, see below |
| text | `MeshPacket.decoded` portnum `TEXT_MESSAGE_APP` (1) | direct |
| position update | portnum `POSITION_APP` (3) | direct |
| node announce | portnum `NODEINFO_APP` (4) | direct |
| battery / metrics | `Telemetry.DeviceMetrics` portnum `TELEMETRY_APP` (67) | `battery_level`, voltage |
| role (Head/Relay/Member) | (no native field) | surface via `long_name` suffix or telemetry; don't overload routing |
| RSSI/SNR (from our planes) | `MeshPacket.rx_rssi/rx_snr` | direct |

**De-aggregation (deferred).** *Near-term the LoRa mesh is flat* — every node
beacons its own position ([04](04-architecture.md)) — so the gateway maps each
beacon to a `NodeInfo`/`Position` **directly**. Once the cluster arc lands, the
long-range plane will carry *aggregated* digests
([12 §2](12-end-goal-full-hybrid-mesh.md#2--protocol-extensions-for-aggregation--dtn))
and the gateway will expand each digest back into per-node updates — same phone
UX, computed from a digest instead of from individual beacons.

## Channels & crypto (the one fiddly part)

- **Read path needs no crypto.** A real Meshtastic radio decrypts before handing
  packets to the phone; we do the same by populating `MeshPacket.decoded`
  (plaintext `Data`). So FromRadio packets are already-decoded.
- **Write path:** the phone *encrypts* outgoing text with the **channel PSK**.
  Because *we* advertise the `Channel` set (with its PSK) during the handshake,
  we know the key and can decrypt the inbound `MeshPacket.encrypted`, recover the
  text, and route it onto our own protocol. Advertising the default channel
  (`psk = {0x01}`, the well-known default key) is the simplest start.
- We are the authority on channels here; the phone adopts what we send.

## What we synthesize

To satisfy the app without lying in ways that break it:
- **`MyNodeInfo`** — `my_node_num` = our NodeID; reboot count; `min_app_version`.
- **`DeviceMetadata`** — pin a **known-compatible `firmware_version`** and
  capability flags (`hasBluetooth=true`, a sane `hw_model`, `role`). This is the
  field the app uses to gate features/protocol — pinning it is what keeps us
  compatible.
- **`Config` / `ModuleConfig`** — sane, mostly-display values. LoRa region/preset
  fields don't map to our radio; present plausible values and **accept-and-ignore
  (or accept-and-reflect)** writes that try to change radio internals.

## Coexistence

**Near-term this is a two-radio problem, not three:** BLE shares the C3's 2.4 GHz
radio with the **2.4-LoRa** link only (the ESP-NOW local plane is deferred). When
a phone is connected, the BLE slot preempts LoRa airtime, so a connected phone
*reduces* mesh throughput while active — acceptable for an ephemeral, on-demand
gateway, not a tether. This BLE↔LoRa cost is measured in the near-term P7
([09](09-poc-roadmap.md)). Once the local plane lands, the BLE-gateway slot
rejoins the full single-band super-frame alongside ESP-NOW + 2.4-LoRa
([12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem)).

## Tradeoffs & risks

1. **Version-tracking is the main long-term cost.** Meshtastic protobufs + apps
   evolve and the app checks `DeviceMetadata`. We pin a version and chase updates.
2. **Config-write impedance.** Reads map cleanly; letting the phone change LoRa
   region/channel internals maps poorly to our non-Meshtastic radio →
   accept-and-reflect/ignore, which can confuse power users.
3. **UX expectation gap.** Looking like Meshtastic invites expectations of
   Meshtastic-only features (traceroute, range test, specific modules) we don't
   implement.
4. **Memory/complexity.** nanopb + GATT server + a node-DB projection layer; modest
   on the C3 but real.

## Phasing (PROMOTED to the near-term track — Phase G, see [09](09-poc-roadmap.md))

This gateway is now a **near-term priority**, built directly on the flat 2.4-LoRa
link (P3) — *ahead* of the deferred cluster/aggregation arc, not after it. Build
incrementally:

1. **Read-only** — handshake + node DB + positions + text RX over the flat LoRa
   mesh. Instant value, low risk: open the Meshtastic app and watch our mesh
   appear on the map. **Increment 1a device-verified on the XR2 (2026-05-24):**
   the `want_config_id` handshake (MyNodeInfo + DeviceMetadata + self NodeInfo
   with live GPS Position + config_complete_id) renders in the unmodified
   `meshtastic --ble --info` CLI — node `!b17506dc` / `Waymesh_06DC`,
   `PRIVATE_HW`, position. nanopb vendored in `lib/nanopb/`; the trimmed proto is
   wire-compatible with upstream. **Increment 1b adds the peer node DB:** each
   LoRa beacon (v1, with position — [05](05-protocol.md)) is upserted and emitted
   as a `NodeInfo`/`Position` in the handshake *and* live (rate-limited FromNum
   bump), so other nodes appear/update in the app. The peers are Tier-2/3 ESP8285
   nodes ([02](02-hardware-and-rf-platform.md)). Still to do here: **text RX**.
2. **TX** — phone sends text → decrypt with advertised PSK → flood onto the LoRa
   mesh.
3. **Channels / config-write** — multiple channels, accept-and-reflect config.

(De-aggregation of cluster digests is **not** part of Phase G — it arrives with
the deferred P4 aggregation work; near-term the mesh is flat, so beacons map to
`NodeInfo` directly.)

## Open questions

- Exact protobuf/firmware version to pin; how often the app breaks old versions.
- iOS bonding quirks with a fixed-PIN, screenless node.
- How to surface Waymesh-specific concepts (cluster, role, plane) without
  confusing the app — `long_name` tag vs a custom telemetry field vs nothing.
- nanopb flash/RAM cost measured on the XR2 alongside the radio stack.
- Whether to keep CSV debug logs interleaved on the Meshtastic serial stream
  (non-framed bytes) or move them to `LogRecord`/`LogRadio` behind a build flag.

## Sources

- [Meshtastic Client API (Serial/TCP/BLE)](https://meshtastic.org/docs/development/device/client-api/)
- [Meshtastic protobufs — mesh.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)
- [Meshtastic Bluetooth API notes](https://github.com/artemisoftnian/Meshtastic-esp32/blob/master/docs/software/bluetooth-api.md)
