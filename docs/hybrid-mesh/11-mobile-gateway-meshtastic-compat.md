# 11 — Mobile Gateway: Meshtastic BLE Client Compatibility

A design proposal (no firmware yet). Goal: a connected phone sees a Waymesh node
as a **Meshtastic device**, so we reuse the polished Meshtastic apps
(Android / iOS / web / desktop) as our mobile UI instead of building one. This
serves the "lightweight mobile interaction" requirement ([01](01-vision-and-requirements.md))
at near-zero UI cost.

## Scope: client compatibility only (not over-the-air)

There are two distinct levels of "Meshtastic compatible":

- **(A) Client / BLE compatibility** — the phone app connects over BLE and sees
  our node as a Meshtastic device. **This is what we're building.**
- **(B) Over-the-air compatibility** — real Meshtastic nodes talk to ours over
  LoRa. This requires adopting Meshtastic's LoRa PHY + managed-flood protocol,
  which fights our hybrid two-plane design. **Explicit non-goal.**

The phone only ever sees the **BLE GATT interface**. It cannot tell that our
long-range plane is our own 2.4-LoRa protocol and our local plane is ESP-NOW. So
this is a **compatibility shim at the gateway** — the planes
([04](04-architecture.md)) stay entirely ours. The node is a *translator* between
its internal model and the Meshtastic client protocol.

```
   Meshtastic app  <--BLE/GATT-->  [ Waymesh node: gateway shim ]  <--our planes-->  the mesh
   (thinks it's                     FromRadio/ToRadio protobufs        ESP-NOW + 2.4-LoRa
    talking to a                    <-> internal node/msg model        clusters, aggregation,
    Meshtastic radio)                                                  roles, DTN
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
  protobuf message — no stream framing. (The `0x94 0xC3 LEN_MSB LEN_LSB` framing
  is only for the serial/TCP *stream* transports, not BLE.)

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

**De-aggregation:** our long-range plane carries *aggregated* cluster digests
([05](05-protocol.md)). The gateway expands each digest back into per-node
`NodeInfo`/`Position` updates so the phone sees individual nodes on its map —
which is exactly the desired UX.

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

BLE shares the C3's 2.4 GHz radio and contends with ESP-NOW and 2.4-LoRa, so the
**BLE-gateway slot is part of the single-band super-frame** ([06](06-rf-coexistence.md)):
when a phone is connected, the gateway slot preempts the other 2.4 GHz activity.
A connected phone therefore *reduces* mesh throughput while active — acceptable
for an ephemeral, on-demand gateway, not a tether.

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

## Phasing (gateway is Phase 4+, see [09](09-poc-roadmap.md))

Design the gateway abstraction now (node state ↔ `FromRadio`/`ToRadio`), build
incrementally once the core architecture is proven:

1. **Read-only** — handshake + node DB + positions + text RX. Instant value, low
   risk: open the Meshtastic app and watch our mesh appear on the map.
2. **TX** — phone sends text → decrypt with advertised PSK → inject into our mesh.
3. **Channels / config-write** — multiple channels, accept-and-reflect config.

## Open questions

- Exact protobuf/firmware version to pin; how often the app breaks old versions.
- iOS bonding quirks with a fixed-PIN, screenless node.
- How to surface Waymesh-specific concepts (cluster, role, plane) without
  confusing the app — `long_name` tag vs a custom telemetry field vs nothing.
- nanopb flash/RAM cost measured on the XR2 alongside the radio stack.
- Whether to also expose the Serial/TCP client transports (same protobufs) for a
  laptop, essentially free once the protobuf layer exists.

## Sources

- [Meshtastic Client API (Serial/TCP/BLE)](https://meshtastic.org/docs/development/device/client-api/)
- [Meshtastic protobufs — mesh.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)
- [Meshtastic Bluetooth API notes](https://github.com/artemisoftnian/Meshtastic-esp32/blob/master/docs/software/bluetooth-api.md)
