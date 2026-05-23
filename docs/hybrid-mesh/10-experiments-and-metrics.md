# 10 — Experiments & Metrics

The measurement discipline that turns opinions into findings. Every POC phase
([09](09-poc-roadmap.md)) reports against these definitions so results are
comparable across phases and against the baselines.

## Core metrics (definitions)

| Metric | Definition | Why it matters |
|--------|------------|----------------|
| **PDR** (packet delivery ratio) | delivered / sent, per link or end-to-end | the reliability number |
| **End-to-end latency** | originate → first delivery at destination | responsiveness |
| **Airtime utilization** | fraction of channel time occupied (and % of duty-cycle budget used) | the LRP hard ceiling |
| **Redundancy factor R** | total transmissions / unique messages delivered | flood efficiency; airtime driver |
| **Hop-count distribution** | hops to delivery | flood radius / topology depth |
| **Neighbor discovery time** | join → appears in neighbor table | local-plane agility |
| **Neighbor churn rate** | neighbor add/remove events per minute | mobility stress level |
| **Range** | distance vs PDR (the PDR-vs-distance curve) | real coverage |
| **RSSI / SNR distribution** | per received frame | link margin, capture analysis |
| **Avg current / runtime** | mean mA; hours to cutoff | the power claim |
| **Energy per delivered message** | joules / delivered message | cross-design efficiency comparison |
| **Election convergence / thrash** | time to stable Head; re-elections per hour | hierarchy stability under mobility |
| **Cross-partition delivery** | DTN: eventual delivery ratio + delay | delay-tolerance |

Always report **mean + spread** over **N≥5 runs**; single runs are anecdotes.

## Test rigs

| Rig | Composition | Phases |
|-----|-------------|--------|
| **2-node link** | A↔B, controlled distance/attenuation | P0, P1, P3 |
| **5-node cluster** | one cluster, ESP-NOW | P1, P2, P4 |
| **2-cluster bridge** | two clusters + LRP | P4, P5 |
| **Line/triangle multi-hop** | 3+ clusters | P5 |
| **Mule / partition** | two islands + ferry node | P8 |
| **Mobility group** | RPGM convoy/hike | P9 |
| **Bench power rig** | node + current meter + scope on rail | P0, P6, P7 |
| **Coexistence rig** | node with both radios + spectrum analyzer + attenuators | P7 |

## Controlled vs field

- **Controlled (bench):** coax + step attenuators or an RF chamber to make
  "distance" repeatable; isolates PHY behavior from weather/terrain. Use for
  PDR-vs-attenuation, desense, capture-effect, power.
- **Field:** open-field range walks (GPS-tagged distance), urban/foliage for
  realism, mobility trials. Use for true range, mobility, end-to-end.
- Record environment every run: location, weather, co-channel activity (a quick
  spectrum scan), antenna height/orientation, node firmware hash.

## Configuration matrix (sweep, don't guess)

Vary one axis at a time, hold the rest:
- **SF:** 7 / 9 / 12 · **BW:** 125 (others later) · **TX power:** +10 / +14 / +22 dBm
- **Beacon/digest rate:** fast / nominal / slow
- **Cluster size:** 1 / 4 / 8 · **Node count N:** 2 / 5 / 20 / 40 (sim for large N)
- **Suppression mechanisms:** each on/off (for the R-vs-PDR curves)
- **Mobility speed:** static / walk (2 m/s) / vehicle (10 m/s) / drone (20 m/s)

## Baselines for honest comparison

Every "the hybrid wins" claim is measured **against**:
1. **Flat LoRa flood baseline** (Meshtastic-style: every node, everything on
   sub-GHz with managed flood) — same scenario, same hardware.
2. **Pure ESP-NOW baseline** (no long-range plane) — shows what local-only loses.

A finding is only valid if the hybrid beats *both* on the metric that matters
for that scenario (or we document that it doesn't, and narrow scope).

## On-node logging schema

Each node logs structured CSV over USB-CDC serial (115200, repo standard) and/or
to flash, one event per line, so logs from multiple nodes merge by timestamp +
NodeID:

```
ts_ms, nodeId, role, event, plane, msgId, srcId, dstId, seq, hop, rssi, snr, batt_mV, lat, lon, extra
```

- `event` ∈ {tx, rx, dup_drop, suppress, relay, elect, join, leave, gps_fix,
  sleep, wake, dtn_offer, dtn_deliver, ...}
- `plane` ∈ {lp, lrp}
- Heads also periodically emit an `airtime` event with cumulative TX time and
  duty-budget usage.
- A small Python post-processor merges per-node CSVs → computes PDR, latency, R,
  hop distribution, churn, airtime. (Lives with the firmware project once Phase 1
  exists.)

## Power measurement method

- Inline current meter (e.g., shunt + INA-class sensor, or a Nordic PPK-class
  tool) on the battery rail; log average and capture TX peaks.
- Scope the 3.3 V rail during +22 dBm sub-GHz and WiFi-TX bursts to catch sag /
  brownout (especially 250 mAh cells).
- Report: per-state mA (replaces estimates in [07](07-power-and-runtime.md)),
  per-profile average mA, and extrapolated runtime per battery size.
- Long-run validation: discharge a real cell on a fixed profile; compare actual
  runtime to the table.

## Repeatability & rigor

- Pin firmware by git hash in every log; never compare across uncontrolled
  firmware changes.
- N≥5 runs per config; report mean ± spread.
- One variable at a time.
- Keep raw logs (commit summaries + analysis, not giant raw dumps, to the repo).
- Each phase's `docs/TASK_LOG.md` records: config, runs, results, and which
  estimate in these docs the measurement confirmed or overturned.

## Feedback into the design

Measurements **edit these documents**. When Phase 0 returns real currents,
[07](07-power-and-runtime.md) is updated. When Phase 4 returns the real
aggregation airtime win, [08](08-mobility-and-topology.md)'s numbers are
replaced. The docs are living; the metrics are how reality overwrites the
estimates.
