# 05 — Protocol (near-term: flat LoRa frames + Meshtastic mapping)

Concrete, compact, binary — the **near-term** wire format for the flat 2.4-LoRa
mesh ([04](04-architecture.md)). These are **proposals to test**, not a frozen
spec. The aggregation/digest, ESP-NOW local-plane, role, and DTN frames that the
full hybrid mesh adds live in
[12 §2](12-end-goal-full-hybrid-mesh.md#2--protocol-extensions-for-aggregation--dtn).

## Design rules

1. **Bytes, not text, on the wire.** Human strings only inside text messages.
2. **One dedup key everywhere:** `MessageID = (NodeID:32, seq:16)`.
3. **The PHY already carries a hardware CRC** (LoRa CRC). The app layer adds only
   `MessageID` for dedup and an *optional* MIC for auth.
4. **Per-node positions near-term** (no digest yet — that's the end-goal
   aggregation win).

## Identifiers (near-term subset)

| Field | Size | Notes |
|-------|------|-------|
| NodeID | 32 bit | from C3 MAC / provisioned key; globally unique-enough |
| GroupID | 16 bit | optional logical-group tag to share spectrum without merging |
| seq | 16 bit | per-originator monotonic; wraps; with NodeID forms MessageID |

(`ShortAddr` / `ClusterID` are cluster identifiers — deferred,
[12](12-end-goal-full-hybrid-mesh.md#addressing-extensions-beyond-the-near-term-nodeidmessageid).)

## Message classes (near-term)

| Class | Code | LoRa | Airtime posture |
|-------|------|------|-----------------|
| PRESENCE | 0x1 | per-node beacon | periodic, cheap |
| GPS | 0x2 | per-node fix | periodic |
| TEXT | 0x3 | deduped, rate-limited | event-driven, capped length |
| ACK | 0x8 | optional (selective) | only where needed |

(TELEMETRY 0x4, ROLE 0x5, DTN 0x6/0x7 arrive with the end goal.)

## Long-range plane frame (LR1121 / LoRa)

Fixed 8-byte header; LoRa's own CRC protects it.

```
LRP header (8 bytes):
 byte 0  : [ver:3][type:5]            protocol version + message class
 byte 1  : [hopLimit:4][flags:4]      flags = {ackReq, aggregated, dtn, unicast}
 byte 2-5: srcNodeID (32 bit)         originator (dedup + relay identity)
 byte 6-7: seq (16 bit)               with srcNodeID => MessageID
 [if unicast flag] +4 bytes: dstNodeID (32 bit)
 payload follows ...
```

(The `aggregated` and `dtn` flags exist in the header for forward-compatibility
but are unused near-term.)

### PRESENCE / GPS payload (per-node, near-term)

```
 byte 0-3 : lat (int32, 1e-7 deg)
 byte 4-7 : lon (int32, 1e-7 deg)
 byte 8   : [ageQuant:4][battQuant:2][flags:2]    coarse freshness/battery
```

~20 B with the header. Note the `1e-7 deg` lat/lon encoding is **identical to
Meshtastic's** `Position.latitude_i/longitude_i`, so the gateway mapping
([11](11-mobile-gateway-meshtastic-compat.md)) is a direct copy.

### TEXT payload

```
 byte 0  : len
 byte 1..: UTF-8 bytes (cap ~48 B on LoRa; longer text discouraged — airtime)
```

A 48 B text at SF10/BW406 ≈ ~177 ms; at SF12/BW406 ≈ ~607 ms. Length is an
airtime decision surfaced to the UX, not unlimited.

## 2.4 GHz LoRa airtime reference (CR 4/5, explicit header, CRC on)

| Payload | SF8 / BW812 (fast) | SF10 / BW406 (mid) | SF12 / BW406 (range) |
|--------:|----:|----:|-----:|
| 20 B (beacon) | ~16 ms | ~114 ms | ~406 ms |
| 48 B (text) | ~24 ms | ~177 ms | ~607 ms |

`Tsym = 2^SF / BW`; `ToA = (8 + 4.25 + n_payload)·Tsym` (Semtech payload-symbol
formula). Lower BW / higher SF = more range, much more airtime. Pick per
link/role; measure the range curves in P3.

### The airtime ceiling on 2.4 GHz (no EU868 duty cap)

2.4 GHz ISM has **no EU868-style 1 % duty cycle**; ETSI EN 300 328 / FCC 15.247
impose power limits + adaptivity/medium-utilization expectations instead. So the
binding near-term limits are (a) **BLE coexistence** — the gateway slot shares the
band with LoRa ([04](04-architecture.md)) — and (b) the **crowded external 2.4 GHz
band** (WiFi/BLE/FPV/other ELRS). **Confirm regional power/EIRP limits before any
field test.** (Once the local plane lands, self-coexistence with ESP-NOW becomes
the dominant limit → [12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem).)

## Suppression & dedup state (per node) — managed flood

Needed even for the flat mesh so multi-hop text (P5) doesn't storm:

- **Seen set:** recent `MessageID`s (ring buffer / Bloom filter) → drop dups.
- **Overhear suppression:** if a pending rebroadcast's `MessageID` is heard from
  someone else with equal/better hop progress, cancel it.
- **SNR-proportional rebroadcast delay:** weaker-SNR receivers wait longer, so the
  best-placed relay transmits first and suppresses the rest (Meshtastic-proven; we
  re-measure the constants).
- **Hop limit:** decremented per relay; 0 = stop.

## Meshtastic client mapping

The gateway translates these frames to/from the Meshtastic client protobufs over
BLE/serial — `PRESENCE`/`GPS` → `NodeInfo`/`Position` (portnum POSITION_APP),
`TEXT` → `MeshPacket` portnum TEXT_MESSAGE_APP. Full mapping table, UUIDs, and the
crypto/channel handling: [11](11-mobile-gateway-meshtastic-compat.md).

## Open protocol questions (near-term)

- Beacon period vs mobility (stale positions vs airtime)?
- TEXT reliability: selective ACK vs pure epidemic — what's the airtime cost?
- Bloom-filter sizing for the seen-set under realistic message rates.
- Minimal MIC/auth scheme that fits the byte budget (security track, later).
