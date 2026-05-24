# 10 — Experiments & Metrics (near-term)

The measurement discipline that turns opinions into findings. Every active POC
phase ([09](09-poc-roadmap.md)) reports against these definitions so results are
comparable across phases and against the baseline. Metrics specific to the
deferred cluster/aggregation/mobility arc live with the end goal
([12 §5](12-end-goal-full-hybrid-mesh.md#5--mobility-topology--scalability)).

## Core metrics (near-term)

| Metric | Definition | Why it matters |
|--------|------------|----------------|
| **PDR** (packet delivery ratio) | delivered / sent, per link or end-to-end | the reliability number |
| **Range** | distance vs PDR (the PDR-vs-distance curve) | real coverage |
| **RSSI / SNR distribution** | per received frame | link margin, capture analysis |
| **End-to-end latency** | originate → first delivery at destination | responsiveness |
| **Airtime / ToA** | measured time-on-air vs predicted; channel occupancy | the LoRa airtime budget |
| **Redundancy factor R** | total transmissions / unique messages delivered (P5 flood) | flood efficiency; airtime driver |
| **Hop-count distribution** | hops to delivery (P5) | flood radius / topology depth |
| **Avg current / runtime** | mean mA; hours to cutoff (P6) | the power claim |
| **Energy per delivered message** | joules / delivered message | efficiency |
| **BLE↔LoRa coexistence** | LoRa PDR with BLE idle / advertising / connected (P7) | the two-radio question |
| **Meshtastic compat** | handshake success + FromRadio/MTU throughput across app/CLI versions (Phase G) | the zero-UI claim |

Always report **mean + spread** over **N≥5 runs**; single runs are anecdotes.

(Election convergence/thrash, neighbor churn, and cross-partition delivery are
deferred metrics — [12 §6](12-end-goal-full-hybrid-mesh.md#6--deferred-roadmap-phases).)

## Test rigs (near-term)

| Rig | Composition | Phases |
|-----|-------------|--------|
| **2-node link** | A↔B, controlled distance/attenuation | P0, P3 |
| **Line/triangle multi-hop** | 3+ flat nodes | P5 |
| **Gateway rig** | node + phone (Meshtastic app) + PC (`meshtastic` CLI) | Phase G |
| **Bench power rig** | node + current meter + scope on rail | P6 |
| **Coexistence rig** | node + BLE phone + spectrum analyzer + attenuators | P7 |
| **Mobility group** | several XR2s under motion | P9 |

## Controlled vs field

- **Controlled (bench):** coax + step attenuators or an RF chamber to make
  "distance" repeatable; isolates PHY behaviour. Use for PDR-vs-attenuation,
  BLE-vs-LoRa desense, power.
- **Field:** open-field range walks (GPS-tagged distance), urban/foliage for
  realism, mobility trials. Use for true range and end-to-end.
- Record environment every run: location, weather, co-channel activity (a quick
  spectrum scan), antenna height/orientation, node firmware hash.

## Configuration matrix (sweep, don't guess)

Vary one axis at a time, hold the rest:
- **SF:** 8 / 10 / 12 · **BW:** 406 / 812 kHz · **TX power:** +10 / +13 dBm
- **Beacon rate:** fast / nominal / slow
- **Node count N:** 2 / 3 / 5 (flat multi-hop)
- **Suppression mechanisms:** each on/off (for the R-vs-PDR curves, P5)
- **BLE state:** idle / advertising / connected (P7)
- **Mobility speed:** static / walk (2 m/s) / vehicle (10 m/s) / drone (20 m/s)

## Baselines for honest comparison

Every "this helps" claim is measured **against**:
1. **Flat 2.4-LoRa flood baseline** (Meshtastic-style discipline on the XR2's
   2.4-LoRa: every node, everything floods) — same scenario, same hardware. This
   *is* essentially what the near-term node is, so the baseline is mostly about
   tuning (SF/BW, suppression) rather than architecture.
2. **Functional Meshtastic-app compatibility** — an unmodified client connects and
   the node behaves as a Meshtastic device (Phase G go/no-go).

(The aggregation-vs-flat-flood architectural A/B is the deferred P4 win →
[12 §5](12-end-goal-full-hybrid-mesh.md#scalability-the-airtime-argument-why-hierarchy-is-non-optional).)

## On-node logging schema

Each node logs structured CSV over serial (115200, repo standard) and/or to
flash, one event per line, so multi-node logs merge by timestamp + NodeID:

```
ts_ms, nodeId, role, event, plane, msgId, srcId, dstId, seq, hop, rssi, snr, batt_mV, lat, lon, extra
```

- `event` ∈ {tx, rx, dup_drop, suppress, relay, gps_fix, sleep, wake, ble_conn,
  ble_disc, ...}
- `plane` is `lrp` near-term (`lp` arrives with ESP-NOW).
- A small Python post-processor merges per-node CSVs → PDR, latency, R, hop
  distribution, airtime. (Lives with the firmware project.)

The `waymesh-node` Phase 0 firmware already emits a compatible CSV stream
(`ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra`).

## Power measurement method

- Inline current meter (shunt + INA-class sensor, or a Nordic PPK-class tool) on
  the battery rail; log average and capture TX peaks.
- Scope the 3.3 V rail during +13 dBm 2.4-LoRa bursts to catch sag / brownout
  (especially 250 mAh cells; note the XR2's 5 V input / regulator).
- Report: per-state mA, per-profile average mA, extrapolated runtime per battery
  size. Long-run validation: discharge a real cell on a fixed profile; compare to
  the estimate.

## Repeatability & rigor

- Pin firmware by git hash in every log; never compare across uncontrolled
  firmware changes.
- N≥5 runs per config; report mean ± spread. One variable at a time.
- Keep commit summaries + analysis (not giant raw dumps) in the repo.
- Each phase's `docs/TASK_LOG.md` records config, runs, results, and which
  estimate the measurement confirmed or overturned.

## Feedback into the design

Measurements **edit these documents.** When P6 returns real currents, the power
figures in [12 §4](12-end-goal-full-hybrid-mesh.md#4--power--runtime) are updated;
when P3 returns real range/airtime, [05](05-protocol.md)'s reference table is
replaced. The docs are living; the metrics overwrite the estimates.
