# 09 — Phased POC Roadmap

Small, testable milestones. Each phase has an **Outcome**, what it **Validates**,
the **Experiments**, the **Metrics**, and a **Go/No-Go gate**. Each phase, when
built, becomes a project under `projects/` following repo conventions
(`platformio.ini`, `src/`, `README.md`, `docs/HARDWARE.md`, `docs/TASK_LOG.md`)
and must pass the repo verification loop (build → flash → observe → report).

The gate question for every phase: **"Does this provide a measurable real-world
advantage over plain LoRa-mesh *and* plain ESP-NOW?"** If no, narrow scope or stop.

Suggested first firmware project name: `projects/waymesh-node` (incremental;
later phases extend the same project rather than spawning many).

---

## Phase 0 — Bench bring-up & baseline (foundation)

- **Outcome:** one node where LR1121 (SPI), GNSS (UART), LED, USB-CDC serial all
  work; two nodes do a raw LoRa TX/RX loopback; baseline currents measured.
- **Validates:** the hardware is real and the datasheet numbers in
  [07](07-power-and-runtime.md) hold.
- **Experiments:** SPI bring-up + BUSY handshake; LR1121 ping-pong between 2
  nodes at SF7/SF9/SF12; GNSS cold/hot TTFF; current draw per state with a meter.
- **Metrics:** loopback PDR at 1 m; TTFF; measured mA for sleep / LoRa-RX /
  LoRa-TX(+14/+22) / WiFi-RX / GNSS.
- **Go/No-Go:** loopback works; measured currents within ~30 % of estimates
  (else revise the power model before proceeding).
- **Deliverable:** `docs/HARDWARE.md` pin map; a "measured vs estimated" current
  table feeding back into [07](07-power-and-runtime.md).

## Phase 1 — Local discovery (the local plane)

- **Outcome:** nodes find each other over ESP-NOW; maintain a neighbor table with
  soft-state TTLs; presence beacons in the [05](05-protocol.md) LP format.
- **Validates:** local plane discovery, churn handling, ESP-NOW range/PDR.
- **Experiments:** N=2..5 nodes; walk in/out of range; vary beacon rate; ESP-NOW
  standard vs LR mode range walk.
- **Metrics:** discovery time, neighbor PDR vs distance, churn detection latency,
  ESP-NOW range (standard vs LR mode).
- **Go/No-Go:** reliable discovery < a few seconds; sane neighbor tables under
  in/out churn.

## Phase 2 — GPS propagation (local)

- **Outcome:** positions shared on the LP; each node holds a live local map of
  neighbor positions.
- **Validates:** position encoding, freshness/age handling, the data the digest
  will later aggregate.
- **Experiments:** moving nodes; verify positions update; test stale-position
  aging; phone reads the map (BLE, Phase 4-lite).
- **Metrics:** position freshness vs beacon rate; error vs GNSS truth.
- **Go/No-Go:** neighbor map is accurate and timely at a sane beacon rate.

## Phase 3 — Long-range beacon (the long-range plane)

- **Outcome:** two *isolated* nodes (no local plane) exchange presence/GPS over
  sub-GHz LoRa; first real range test.
- **Validates:** LRP link, real airtime, real range/PDR vs SF, regulatory duty.
- **Experiments:** open-field range walk at SF7/SF9/SF12, +14/+22 dBm; measure
  ToA on a scope vs the [05](05-protocol.md) table; confirm duty-cycle accounting.
- **Metrics:** PDR vs distance per SF; measured ToA; achieved range; energy per
  delivered beacon.
- **Go/No-Go:** usable range (target ≥1 km LOS at SF9) and ToA within ~20 % of
  predicted.

## Phase 4 — Cluster head election + aggregation (the core claim)

- **Outcome:** a local cluster elects a Head; the Head aggregates member
  positions into one LoRa digest; a second cluster's Head de-aggregates into its
  LP. Ephemeral BLE phone gateway for inspection.
- **Validates:** the **aggregate-then-bridge** thesis and the airtime win from
  [08](08-mobility-and-topology.md).
- **Experiments:** 2 clusters of 3–4; measure LRP airtime with aggregation
  on/off; force re-election (kill the Head); connect a phone, read state, inject
  a message.
- **Metrics:** **airtime with vs without aggregation** (expect ≥3×), election
  convergence time, election thrash rate, end-to-end position freshness across
  clusters.
- **Go/No-Go:** aggregation delivers a clear airtime reduction *and* elections
  converge without thrash. **This is the make-or-break gate for the whole thesis.**

## Phase 5 — Managed-flood relay across clusters

- **Outcome:** multi-hop LRP delivery with hop limit, dedup, SNR-delay, overhear
  suppression.
- **Validates:** controlled flooding without retransmission storms.
- **Experiments:** 3+ clusters in a line/triangle; inject text; measure
  redundancy factor R with each suppression mechanism on/off; storm stress test.
- **Metrics:** R vs PDR per mechanism (the curve from [08](08-mobility-and-topology.md)),
  hop-count distribution, no-storm confirmation under load.
- **Go/No-Go:** multi-hop delivery with R kept low (target R ≲ 2) and no storms.

## Phase 6 — Duty-cycling & power (the runtime claim)

- **Outcome:** the super-frame schedule from [04](04-architecture.md)/[06](06-rf-coexistence.md):
  always-on LoRa RX, scheduled ESP-NOW windows, duty-cycled GPS, sleep between.
- **Validates:** the runtime tables in [07](07-power-and-runtime.md).
- **Experiments:** run profiles A/D/E for hours/days on real cells; log average
  current; small-cell brownout test under TX peaks.
- **Metrics:** measured avg current per profile; achieved runtime; brownout
  threshold per battery size.
- **Go/No-Go:** standby member ≥3 days on ≤1000 mAh; no brownouts at chosen TX
  power / cell size.

## Phase 7 — RF coexistence trials (core research)

- **Outcome:** the empirical coexistence results from [06](06-rf-coexistence.md).
- **Validates:** the super-frame actually prevents harmful interference.
- **Experiments:** the seven tests in [06](06-rf-coexistence.md) (self-desense
  sub-GHz & 2.4-LoRa, antenna isolation, supply coupling, external congestion,
  sub-GHz coexistence, schedule validation).
- **Metrics:** desense (dB / PDR cliff), antenna isolation (dB), supply sag vs
  TX power, congestion failover threshold, guard-interval adequacy.
- **Go/No-Go:** a documented, repeatable schedule with quantified, tolerable
  interference; decision on whether 2.4-LoRa is ever worth enabling.

## Phase 8 — Store-and-forward / DTN

- **Outcome:** bundle-lite carry-and-forward across a partition via a mule node.
- **Validates:** delay-tolerant delivery across space-gaps.
- **Experiments:** two islands out of range; a node ferries between them; verify
  summary-vector dedup; TTL expiry.
- **Metrics:** cross-partition delivery ratio, delivery delay, duplicate rate,
  storage used.
- **Go/No-Go:** reliable eventual delivery across a partition without unbounded
  duplication/storage.

## Phase 9 — Mobility field trials (the real test)

- **Outcome:** a convoy/hike/swarm scenario end-to-end with all planes.
- **Validates:** the whole system under real mobility; sim vs reality.
- **Experiments:** RPGM-style group walk/drive; induce merges/splits/orphans;
  compare against the Tier-2 sim predictions; A/B vs a Meshtastic-style baseline
  for the same scenario.
- **Metrics:** PDR, latency, airtime, churn, runtime — all under motion; head-to-
  head vs the flat-flood baseline.
- **Go/No-Go (program-level):** the hybrid demonstrably beats *both* plain
  LoRa-mesh and plain ESP-NOW for at least one real scenario, with numbers.

---

## Phase dependency graph

```
P0 bring-up ──┬─> P1 local discovery ──> P2 GPS prop ──┐
              │                                          ├─> P4 head+aggregation ──> P5 relay ──┐
              └─> P3 LR beacon ──────────────────────────┘                                       │
                                                                                                 ├─> P9 field trials
P6 power ───────────────────────────────────────────────────────────────────────────────────────┤
P7 coexistence ──────────────────────────────────────────────────────────────────────────────────┤
P8 DTN ───────────────────────────────────────────────────────────────────────────────────────────┘
```

P0→P2 and P0→P3 can run in parallel. P4 is the thesis gate. P6/P7 can begin
once P0 hardware is stable. P9 needs P4/P5 plus enough of P6/P7 to run in the field.

## What we deliberately defer

- Security/crypto hardening (own track, after P4 proves the architecture).
- Polished mobile app (Phase-4 gateway is a debug inspector, not a product).
- 2.4-LoRa mode (only if Phase 7 shows it's worth the coexistence cost).
- LR-FHSS uplink (evaluate after P3/P4 if digest robustness needs it).
