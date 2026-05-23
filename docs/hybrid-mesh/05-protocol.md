# 05 — Protocol Investigation

Concrete, compact, binary. These are **proposals to test**, not a frozen spec.
The governing constraint is long-range airtime: every byte on the LRP costs
milliseconds of a duty-limited channel, so the wire format is miserly.

## Design rules

1. **Bytes, not text, on the wire.** Human strings only inside text messages.
2. **One dedup key everywhere:** `MessageID = (NodeID:32, seq:16)`.
3. **Aggregate on the LRP; be generous on the LP.** LoRa frames pack many
   members into one digest; ESP-NOW frames can be roomy (≤250 B v1).
4. **Both PHYs already carry a hardware CRC** (LoRa CRC, ESP-NOW FCS). The app
   layer adds only `MessageID` for dedup and an *optional* MIC for auth.
5. **Delta-encode positions.** Absolute lat/lon once per digest; per-node deltas.

## Identifiers

| Field | Size | Notes |
|-------|------|-------|
| NodeID | 32 bit | from C3 MAC / provisioned key; globally unique-enough |
| ShortAddr | 16 bit | low bits of NodeID, used within a cluster (collisions resolved at join) |
| ClusterID | 16 bit | derived from current Head's NodeID; changes on re-election |
| GroupID | 16 bit | optional logical-group tag to share spectrum without merging |
| seq | 16 bit | per-originator monotonic; wraps; with NodeID forms MessageID |

## Message classes (both planes)

| Class | Code | LP | LRP | Airtime posture |
|-------|------|----|----|-----------------|
| PRESENCE | 0x1 | beacon | aggregated into digest | periodic, cheap, decimated |
| GPS | 0x2 | full fix | delta, aggregated | periodic, aggregated |
| TEXT | 0x3 | yes | yes (deduped, rate-limited) | event-driven, capped length |
| TELEMETRY | 0x4 | yes | sampled/aggregated | decimated to configured rate |
| ROUTING/ROLE | 0x5 | election/role beacons | head/relay coordination | small, infrequent |
| DTN_OFFER | 0x6 | n/a | summary vector | on neighbor discovery |
| DTN_REQ/DATA | 0x7 | n/a | bundle pull/deliver | opportunistic |
| ACK | 0x8 | optional | optional (selective) | only where needed |

## Long-range plane frame (LR1121 / LoRa)

Fixed header kept to 8 bytes; LoRa's own CRC protects it.

```
LRP header (8 bytes):
 byte 0  : [ver:3][type:5]            protocol version + message class
 byte 1  : [hopLimit:4][flags:4]      flags = {ackReq, aggregated, dtn, unicast}
 byte 2-5: srcNodeID (32 bit)         originator (dedup + relay identity)
 byte 6-7: seq (16 bit)               with srcNodeID => MessageID
 [if unicast flag] +4 bytes: dstNodeID (32 bit)
 payload follows ...
```

### PRESENCE/GPS digest payload (the aggregation win)

```
 byte 0    : count N
 byte 1-4  : refLat (int32, 1e-7 deg)   } cluster reference, once per digest
 byte 5-8  : refLon (int32, 1e-7 deg)   }
 then N records, 7 bytes each:
   byte 0-1: shortAddr (16)
   byte 2-3: dLat (int16, ~1 m/LSB rel. to ref)   ±32 km range
   byte 4-5: dLon (int16, ~1 m/LSB rel. to ref)
   byte 6  : [ageQuant:4][battQuant:2][flags:2]    coarse freshness/battery
```

Size for N members: `8 (hdr) + 9 (ref+count) + 7N`.
- N=8 → **73 bytes**, ~240 ms at SF10/BW406 (2.4 GHz LoRa).
- 8 separate single-node position frames (~20 B each, ~114 ms) → **~912 ms**.
- **≈3.8× airtime saving from aggregation alone**, before suppression/dedup.

### TEXT payload

```
 byte 0  : len
 byte 1..: UTF-8 bytes (cap ~48 B on LRP; longer text discouraged — airtime)
```

A 48 B text at SF10/BW406 ≈ ~177 ms; at SF12/BW406 ≈ ~607 ms (2.4 GHz LoRa).
Length is an airtime decision, surfaced to the user/UX, not unlimited.

### DTN (store-and-forward) frames

```
DTN_OFFER  : summary vector = list of held MessageIDs (NodeID:32, seq:16)
             (sent when a new neighbor appears)
DTN_REQ    : list of MessageIDs the requester is missing
DTN_DATA   : { MessageID, class, TTL, payload }   one bundle
```

TTL is a coarse lifetime (e.g., quantized minutes) + optional hop budget.
Dedup by MessageID; **no custody transfer** (keep it simple) — epidemic spread
with summary-vector pruning.

## Local plane frame (ESP-NOW)

Cheaper per byte (within a window), so the LP can be more generous. ≤250 B (v1)
or ≤1470 B (v2). ESP-NOW FCS covers integrity.

```
LP header (8 bytes): same shape as LRP header (ver/type, flags, srcNodeID, seq)

PRESENCE/ROLE beacon payload (intra-cluster, full fidelity):
 byte 0   : role (M/H/R/GW)
 byte 1-2 : electionScore (16)
 byte 3   : battery (%, 1 B)
 byte 4-7 : lat (int32, 1e-7 deg)
 byte 8-11: lon (int32, 1e-7 deg)
 byte 12  : numNeighbors
 byte 13-14: clusterID
 byte 15  : seqOfHeadDigest (so members know if they're current)
 ... optional neighbor list for centrality calc ...
```

The Head builds the LRP digest by collecting these LP beacons over a period and
collapsing the positions into the delta-encoded digest above.

## 2.4 GHz LoRa airtime reference (CR 4/5, explicit header, CRC on)

Time-on-air for representative payloads on the LR1121 2.4 GHz LoRa. Unlike
sub-GHz (BW125), 2.4 GHz LoRa offers wide bandwidths (203/406/812/1625 kHz), so
airtime is much shorter — at the cost of sensitivity/range. BW is the
range-vs-airtime knob.

| Payload | SF8 / BW812 (fast) | SF10 / BW406 (mid) | SF12 / BW406 (range) |
|--------:|----:|----:|-----:|
| 20 B  | ~16 ms  | ~114 ms | ~406 ms |
| 73 B (N=8 digest) | ~36 ms | ~240 ms | ~860 ms |

Symbol time `Tsym = 2^SF / BW`; `ToA = (8 + 4.25 + n_payload)·Tsym` with the
standard Semtech payload-symbol formula. Lower BW / higher SF = more range,
much more airtime. Pick per link/role; measure the range curves in Phase 3/7.

### The airtime ceiling on 2.4 GHz (no EU868 duty cap)

2.4 GHz ISM has **no EU868-style 1 % duty cycle**; ETSI EN 300 328 / FCC 15.247
impose power limits and (EN 300 328) adaptivity/medium-utilization expectations
instead. So the binding limit is **self-coexistence**, not regulation:
- The LoRa TX shares the super-frame slot budget with ESP-NOW and BLE
  ([06](06-rf-coexistence.md)); LoRa airtime is bounded by *how much of the frame
  it can take without starving the local plane*, not by a legal duty cap.
- The crowded external 2.4 GHz band (WiFi/BLE/FPV/other ELRS) is the other limit.

This is *why* only Heads transmit, *why* we aggregate, and *why* store-and-forward
exists — to keep the shared 2.4 GHz medium usable, not to obey a duty rule.
**Confirm regional power/EIRP limits before any field test.**

## Suppression & dedup state (per node)

- **Seen set:** recent `MessageID`s (ring buffer / Bloom filter) → drop dups.
- **Overhear suppression:** if a pending rebroadcast's `MessageID` is heard from
  someone else with equal/better hop progress, cancel it.
- **SNR-proportional rebroadcast delay:** weaker-SNR receivers wait longer, so
  the best-placed relay transmits first and suppresses the rest (Meshtastic-
  proven; we re-measure the constants).
- **Hop limit:** decremented per relay; 0 = stop.
- **Airtime budget:** local accounting so a node never exceeds its share of the
  super-frame's LoRa TX slots (self-coexistence, not a regulatory cap on 2.4 GHz).

## Open protocol questions (for the POCs)

- Optimal digest period vs mobility (stale positions vs airtime)?
- ShortAddr collision rate in practice; is 16 bit enough per cluster?
- Is LR-FHSS (TX-only) worth using for the digest uplink robustness?
- Selective ACK vs pure epidemic for TEXT reliability — what's the airtime cost?
- Bloom filter sizing for the seen-set under realistic message rates.
- Minimal MIC/auth scheme that fits the byte budget (security track, later).
