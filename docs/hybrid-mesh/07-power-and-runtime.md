# 07 — Power & Runtime

Runtime is **RX-dominated**, and the dominant RX cost is *which* radio listens.
All numbers below are datasheet-derived estimates — **Phase 0 bench measurement
replaces them.** They exist to size the design and expose the swing factors.

## Per-state current budget (estimates)

| Subsystem / state | Current | Source |
|---|---|---|
| LR1121 2.4 GHz LoRa RX (gap-filler, ~continuous) | ~6 mA | datasheet |
| LR1121 2.4 GHz TX +10 dBm | ~30–40 mA | datasheet |
| LR1121 2.4 GHz TX +13 dBm | ~50 mA | datasheet |
| LR1121 sleep (retention) | ~1.4 µA | datasheet |
| C3 light-sleep (wake on LoRa IRQ) | ~130 µA | datasheet |
| C3 deep-sleep | ~5 µA | datasheet |
| C3 "LoRa-listening idle" (light-sleep + brief CPU wakes) | ~1 mA (budget) | estimate |
| C3 WiFi RX (ESP-NOW window) | ~95 mA | datasheet |
| C3 WiFi TX peak | ~180–240 mA | datasheet |
| GNSS tracking | ~25 mA | u-blox MAX-M10S class |

**The headline:** an idle but *reachable* node sits at **~7 mA**
(LoRa RX 6 + C3 idle 1), because LoRa listens cheaply — even at 2.4 GHz, LoRa RX
(~6 mA) beats WiFi RX (~95 mA) by ~16×. Everything above that is the price of GPS
fixes, ESP-NOW windows, and LRP transmits — all **duty-cycled, not continuous.**
Note the single-band twist: LoRa RX is now a *gap-filler* (it yields the 2.4 GHz
medium during ESP-NOW/BLE/LoRa-TX slots, [06](06-rf-coexistence.md)), but those
slots are a small fraction of time, so ~6 mA average still holds.

## Duty-cycle contributions (the swing factors)

Average added current `= peak × (on-time / period)`:

| Activity | Example duty | Avg added |
|---|---|---|
| ESP-NOW window 100 ms / 10 s | 1 % | ~0.95 mA |
| ESP-NOW window 200 ms / 2 s | 10 % | ~9.5 mA |
| GPS fix 10 s / 5 min | 3.3 % | ~0.83 mA |
| GPS fix 5 s / 10 s (mobile, hot) | 50 % | ~12.5 mA |
| LRP digest 240 ms / 30 s @ +13 dBm (2.4-LoRa) | 0.8 % | ~0.4 mA |
| LRP beacon 114 ms / 60 s @ +10 dBm (2.4-LoRa) | 0.2 % | ~0.06 mA |

GPS and ESP-NOW windows dominate active profiles. **Mobility costs power**
(faster GPS fixes + denser sync) — an explicit freshness-vs-runtime tradeoff.

## Node profiles

| Profile | Composition | Avg current |
|---|---|---|
| **A — Deep-standby member** | LoRa RX + C3 idle (7) + ESP-NOW 1 % (0.95) + GPS 10 s/5 min (0.83) | **~8.8 mA** |
| **B — Active member (moving)** | 7 + ESP-NOW 10 % (9.5) + GPS hot 50 % (4.2*) | **~20.7 mA** |
| **C — Cluster Head (active)** | B + LRP digest 240 ms/30 s @ +13 dBm (0.4) | **~21.1 mA** |
| **D — Lone sparse node** | LoRa RX + C3 idle (7) + GPS 10 s/5 min (0.83) + LRP beacon (0.06), WiFi off | **~8 mA** |
| **E — Ultra-low beacon (wearable)** | LoRa RX duty-cycled ~25 % (1.5) + C3 mostly deep-sleep (~0.2) + GPS 10 s/15 min (0.28) | **~2 mA** |

\* Profile B uses a moderated GPS duty (5 s/30 s ≈ 4.2 mA), not full mobile.

## Runtime (hours), 80 % usable capacity assumed

`runtime ≈ 0.8 × capacity_mAh / avg_mA`

| Profile (mA) | 250 mAh | 500 mAh | 1000 mAh | 2000 mAh |
|---|---|---|---|---|
| A — standby (8.8) | ~23 h | ~1.9 d | ~3.8 d | ~7.6 d |
| B — active (20.7) | ~10 h | ~19 h | ~1.6 d | ~3.2 d |
| C — head (21.1) | ~9.5 h | ~19 h | ~1.6 d | ~3.2 d |
| D — lone (8.0) | ~25 h | ~2.1 d | ~4.2 d | ~8.3 d |
| E — wearable (2.0) | ~4.2 d | ~8.3 d | ~16.7 d | ~33 d |

These meet the ≥3-day success target for a standby member on ≤1000 mAh, and
multi-week for a duty-cycled wearable beacon — *if* GPS and ESP-NOW are
disciplined.

## Strategies that produce these numbers

1. **LoRa is the default gap-filler plane; WiFi is bursted.** Never leave WiFi in
   RX; let cheap LoRa RX (~6 mA) fill the gaps. The single biggest lever (95 vs 6 mA).
2. **Synchronized wake windows.** Members align ESP-NOW windows to the Head's
   beacon so the WiFi radio is on briefly and *together* — short duty, real sync.
3. **Duty-cycle GPS behind a load switch.** Fix-then-sleep; raise the fix rate
   only with motion (accelerometer-gated or speed-gated). Cut it entirely when
   stationary.
4. **Ration LRP TX.** Only Heads/Relays transmit; aggregate; respect the
   super-frame's LoRa TX budget. Lower TX power when link margin allows
   (~30 mA @ +10 dBm vs ~50 mA @ +13 dBm on 2.4 GHz).
5. **Rotate the Head role** so the higher-current head burden is shared and no
   single battery dies first.
6. **Graceful low-power degradation.** As battery voltage drops: slow beacons,
   widen GPS interval, drop optional telemetry, finally shed Head role. Never go
   fully dark without a last "low-battery" beacon.
7. **LoRa RX duty-cycling for wearables.** Preamble-sniff / channel-activity-
   detection wake trades reach latency for sub-2 mA averages (Profile E).

## Caveats (why Phase 0 measurement is mandatory)

- **Peak-current sag on tiny cells.** A 250 mAh LiPo may brown out on ~240 mA
  WiFi TX peaks or a +13 dBm 2.4-LoRa burst; the XR2 regulator + cell ESR decide.
  Note the XR2 expects 5 V in — boost or 3.3 V-tap powering affects this. Measure
  before trusting small cells; budget decoupling.
- **Regulator efficiency & quiescent draw** are not in the datasheet figures.
- **Temperature** (cold field use) cuts LiPo capacity and raises ESR.
- **GNSS cold-start TTFF** spikes energy; warm/hot start assumptions matter.
- **C3 "idle" budget (~1 mA)** is a guess — the real light-sleep + wake pattern
  must be profiled with a current meter (Phase 0 / Phase 6).

Measurement methodology and the current-logging rig: [10](10-experiments-and-metrics.md).
