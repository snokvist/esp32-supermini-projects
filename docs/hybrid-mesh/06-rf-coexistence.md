# 06 — RF Coexistence & Spectrum Behavior

A **core research area**, not an implementation detail. The node carries
multiple radios that can interfere; understanding and scheduling that
interaction is a primary success criterion.

## The radios in play

| Radio | Band | Role | Concurrency |
|-------|------|------|-------------|
| C3 WiFi (ESP-NOW) | 2.4 GHz | local burst plane | shares the C3 radio with BLE (time-sliced) |
| C3 BLE | 2.4 GHz | ephemeral phone gateway | shares the C3 radio with WiFi |
| LR1121 sub-GHz LoRa | 868/915 MHz | long-range plane | **separate band → cleanly parallel** |
| LR1121 2.4 GHz LoRa (optional) | 2.4 GHz | optional medium-range | **contends with WiFi/BLE** |

## The good news: band separation is mostly free

Sub-GHz (868/915 MHz) and 2.4 GHz are far apart in frequency. Running **LR1121
sub-GHz RX/TX concurrently with C3 WiFi/BLE** is the design's default and should
be largely interference-free *in-band*. Residual coupling paths to watch:

- **Supply/ground noise:** a 90 mA sub-GHz TX burst can sag a small LiPo rail
  and inject noise that hurts a sensitive WiFi RX. Mitigation: decoupling, rail
  budgeting (see [07](07-power-and-runtime.md)), staggering big TX bursts.
- **Harmonics/spurs:** 868×N or 915×N rarely lands in 2.4 GHz cleanly, but
  filter/front-end quality matters. Verify with a spectrum capture.
- **Antenna proximity:** two antennas centimeters apart on a matchbox board
  couple even across bands. Measure isolation.

This is why **sub-GHz is the long-range plane**: it buys near-free concurrency
with the local 2.4 GHz plane.

## The hard case: two 2.4 GHz transmitters on one tiny board

If we ever use **LR1121 2.4 GHz LoRa**, it shares the band with C3 WiFi and BLE.
A +13 dBm 2.4 GHz LoRa TX a few cm from the WiFi RX front-end will **desensitize
or fully block** that receiver (near-far / front-end overload), regardless of
channel offset, because the receiver's LNA is swamped. Channel planning alone
does **not** solve this — physical proximity dominates.

**Rule:** never transmit on two 2.4 GHz radios simultaneously; never RX on one
2.4 GHz radio while another 2.4 GHz radio TXs nearby. They must be
**time-division multiplexed**.

## The super-frame schedule (coexistence mechanism)

Time is divided so 2.4 GHz emitters never overlap, while cheap sub-GHz RX runs
underneath the whole frame.

```
|<------------------------------ super-frame (1–10 s) ------------------------------>|
sub-GHz LoRa RX : ████████████████████████████████████████████████████████████████  (always on, ~6 mA, different band)
2.4 GHz medium  : [ ESP-NOW window ][   sleep / quiet   ][ BLE GW? ][ 2.4-LoRa? ]....  (mutually exclusive sub-slots)
sub-GHz LoRa TX : .................................[ TX slot ]......................  (Head/Relay only, duty-budgeted)
```

- **ESP-NOW window:** short, members aligned to the Head's beacon; WiFi radio on
  only here.
- **BLE gateway slot:** only when a phone is actively connected; preempts ESP-NOW
  cleanly because they share the C3 radio anyway.
- **2.4-LoRa slot (if used):** explicitly disjoint from the ESP-NOW/BLE slots.
- **Sub-GHz TX slot:** scheduled and duty-budgeted; can overlap 2.4 GHz activity
  (different band) but is staggered to avoid supply-rail collisions with WiFi TX.

Loose time sync comes from Head beacons (no global master); guard intervals
absorb drift. Quantify required guard size vs C3 clock drift in Phase 7.

## Coexistence with the *external* RF environment

Field reality, not just our own board:

- **2.4 GHz is a warzone:** WiFi APs, BLE, Zigbee, microwaves, other ESP-NOW.
  ESP-NOW uses CSMA/backoff but throughput/PDR degrade under congestion.
  Mitigation: keep ESP-NOW windows short and infrequent; consider channel
  selection by scan; lean on the long-range plane when local 2.4 is unusable.
- **Sub-GHz is quieter but duty-cycle regulated.** Fewer interferers, but the
  1 % (EU) / dwell rules cap us. Other LoRa users (Meshtastic, LoRaWAN) can
  collide; LoRa's capture effect helps but isn't magic. Use distinct
  spreading/sync words and the GroupID tag.
- **Co-located FPV/drone gear** (the use cases!) hammers 2.4 GHz. Another reason
  to keep the *reliable* plane on sub-GHz and treat 2.4 GHz local sync as
  best-effort.

## Empirical test plan (Phase 7, see roadmap)

Concrete experiments with measurable outputs:

1. **Self-desense (sub-GHz TX → WiFi RX):** measure ESP-NOW PDR on node A while
   node A's LR1121 transmits sub-GHz at +10/+14/+22 dBm. Expect small in-band
   effect; quantify any supply-noise hit. *Output:* PDR vs TX power.
2. **Self-desense (2.4-LoRa TX → WiFi RX):** same, but LR1121 in 2.4 GHz mode.
   Expect severe blocking. *Output:* the dB/PDR cliff that justifies strict TDM.
3. **Antenna isolation:** S21 between the sub-GHz and 2.4 GHz antennas at
   various separations/orientations. *Output:* isolation (dB) vs geometry.
4. **Supply coupling:** scope the 3.3 V rail during a +22 dBm sub-GHz burst on
   250/500/1000 mAh cells; correlate sag with WiFi RX errors. *Output:* min
   decoupling / max safe TX power per battery size.
5. **External congestion sweep:** ESP-NOW PDR/latency vs offered 2.4 GHz load
   (co-located WiFi/BLE/FPV). *Output:* the congestion level at which we should
   fail over to sub-GHz.
6. **Sub-GHz coexistence:** PDR with N co-channel LoRa interferers; capture-
   effect threshold. *Output:* graceful-degradation curve.
7. **Schedule validation:** run the full super-frame on 5 nodes; confirm zero
   concurrent 2.4 GHz TX and measure guard-interval adequacy vs clock drift.

Logging schema and metric definitions: [10](10-experiments-and-metrics.md).

## Coexistence design decisions (current)

1. **Primary reliable plane = sub-GHz LoRa.** Free concurrency with local 2.4.
2. **2.4 GHz is time-division only.** WiFi, BLE, and (optional) 2.4-LoRa never
   overlap. Enforced by the super-frame.
3. **2.4-LoRa is optional and last-resort** for medium range when sub-GHz is
   saturated and local density is low — not a default.
4. **Stagger big sub-GHz TX bursts** away from WiFi RX windows to dodge supply
   coupling, even though the bands don't overlap.
5. **Fail over to sub-GHz** when local 2.4 GHz congestion crosses a measured
   threshold.
