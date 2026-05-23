# 06 — RF Coexistence & Spectrum Behavior

On a single-band 2.4 GHz node this is **the central design problem**, not a side
study. The XR2 carries two 2.4 GHz radios millimetres apart; making them share
the band without crippling each other is a primary success criterion.

## The radios in play (all 2.4 GHz)

| Radio | Band | Role | Concurrency |
|-------|------|------|-------------|
| C3 WiFi (ESP-NOW) | 2.4 GHz | local burst plane | shares the C3 radio with BLE |
| C3 BLE | 2.4 GHz | ephemeral phone gateway | shares the C3 radio with WiFi |
| LR1121 2.4 GHz LoRa | 2.4 GHz | long-range plane | **separate chip/antenna, same band** |

There is **no band separation to exploit** on this board — the dual-band escape
hatch (sub-GHz LoRa parallel to 2.4 WiFi) does not exist on the XR2. Everything
is in 2.4 GHz, so everything is time-division multiplexed.

## The core problem: two 2.4 GHz transmitters on a 0.8 g board

A +10…+13 dBm 2.4-LoRa TX a few mm from the WiFi RX front-end **desensitizes or
fully blocks** that receiver (near-field front-end overload), and vice-versa.
This is **independent of channel offset** — putting LoRa on one part of the band
and WiFi on another does not help, because the victim receiver's LNA is swamped
by the nearby transmitter's raw power, not by co-channel energy.

**Hard rules:**
1. Never transmit on two 2.4 GHz radios at once.
2. Never receive on one 2.4 GHz radio while another 2.4 GHz radio transmits nearby.
3. Therefore: **strict time-division** between LoRa, WiFi/ESP-NOW, and BLE.

ExpressLRS already lives by a version of this on the very same hardware — its
WiFi "config mode" and its RF link are never active simultaneously. That
precedent is our starting point.

## The super-frame is the coexistence mechanism

(See [04](04-architecture.md) for the architectural view.) Time is divided so the
2.4 GHz medium has exactly one user per slot:

```
|<------------------------------ super-frame (250 ms – 2 s) -------------------------------->|
2.4 GHz medium : [ LoRa RX (cheap, default) ][ ESP-NOW window ][ sleep ][ LoRa TX? ][ BLE? ]
                   one user                     one user                  one user   one user
guard          :  | g |                       | g |                     | g |       | g |
```

- **LoRa RX is the default gap-filler** — cheap (~6 mA), keeps the node reachable
  on the LRP whenever it isn't bursting WiFi or transmitting LoRa.
- **ESP-NOW window:** short, Head-aligned; WiFi on only here.
- **LoRa TX slot:** Head/Relay only, airtime-budgeted.
- **BLE slot:** only when a phone is connected; preempts the others.
- **Guard intervals (g):** absorb inter-node clock drift so slots don't overlap.
  Their required size vs C3 clock drift is a Phase-7 measurement.

The cost of single-band: the LoRa RX duty is reduced (it yields the medium during
ESP-NOW/BLE/LoRa-TX slots), so a node is *not* always listening on the LRP — it
listens in the gaps. This trades a little LRP reachability/latency for
coexistence, and it slightly raises average current vs the (impossible-here)
parallel-band case. Quantify the reachability hit in Phase 7.

## Residual coupling beyond the obvious

Even with TDM, watch:
- **Supply/ground noise:** a 50 mA LoRa TX or 240 mA WiFi TX burst sags a small
  LiPo/the XR2's 3.3 V rail; budget decoupling, stagger heavy TX. (See
  [07](07-power-and-runtime.md).)
- **Antenna coupling:** two 2.4 GHz antennas mm apart couple strongly; measure
  isolation, even though TDM means they're not active together.
- **Thermal:** sustained TX on a 0.8 g board heats the LR1121/PA; watch drift.

## Coexistence with the *external* RF environment

2.4 GHz is a warzone, and our use cases (FPV/drones) make it worse:
- **WiFi APs, BLE, Zigbee, microwaves, other ESP-NOW, other ELRS links** all
  share the band. ESP-NOW uses CSMA/backoff; LoRa relies on processing gain +
  capture. PDR degrades under congestion.
- **Co-located FPV/drone control + video** hammer 2.4 GHz. Since our long-range
  plane is *also* 2.4 GHz here, there's no quiet sub-GHz refuge — mitigation is
  channel selection, low duty, high-SF LoRa for processing gain, and the GroupID
  tag to ignore foreign traffic.
- **No EU868-style duty cap**, but ETSI EN 300 328 / FCC 15.247 impose power
  limits and (EN 300 328) adaptivity/medium-utilization expectations. The XR2's
  10 mW telemetry power is well within limits.

## SF / bandwidth as the range–airtime–robustness knob

Single-band means we tune the LoRa link instead of changing band:
- **High SF + narrow BW (e.g. SF12/BW203):** best sensitivity/range and capture,
  worst airtime. Good for sparse long bridges.
- **Low SF + wide BW (e.g. SF8/BW812):** short airtime, less range. Good when
  airtime is tight and nodes are closer.
- **FLRC** (LR1121 supports it): fast, moderate range — a possible middle option,
  though ESP-NOW already owns "fast local." Evaluate only if needed.

Pick per link/role; measure the curves in Phase 3/7.

## Empirical test plan (Phase 7)

Concrete experiments, all on the XR2:

1. **Self-desense (2.4-LoRa TX → WiFi RX):** measure ESP-NOW PDR on a node while
   its own LR1121 transmits 2.4-LoRa at +10/+13 dBm. *Output:* the PDR cliff that
   justifies strict TDM and sets guard requirements.
2. **Self-desense (WiFi TX → LoRa RX):** the reverse — LoRa frame errors while the
   C3 transmits WiFi. *Output:* symmetry of the problem.
3. **Antenna isolation:** S21 between the C3 WiFi antenna and the LR1121 2.4
   antenna on the XR2. *Output:* isolation (dB), informs guard/scheduling.
4. **Supply coupling:** scope the 3.3 V rail during +13 dBm LoRa and 240 mA WiFi
   bursts on 250/500/1000 mAh cells; correlate sag with RX errors. *Output:*
   decoupling/brownout limits per battery size.
5. **Guard-interval adequacy:** run the full super-frame on 5 XR2 nodes; confirm
   zero concurrent 2.4 GHz TX; measure overlap vs C3 clock drift. *Output:* the
   minimum safe guard size.
6. **External congestion sweep:** ESP-NOW and 2.4-LoRa PDR/latency vs offered
   2.4 GHz load (co-located WiFi/BLE/FPV). *Output:* the congestion level where we
   shed optional traffic / change SF.
7. **LoRa-vs-LoRa coexistence:** PDR with N co-channel 2.4-LoRa interferers;
   capture-effect threshold. *Output:* graceful-degradation curve.

Metric definitions and logging schema: [10](10-experiments-and-metrics.md).

## Coexistence design decisions (current)

1. **Single band → strict time-division is mandatory.** The super-frame owns the
   2.4 GHz medium; exactly one radio per slot.
2. **LoRa RX is the default gap-filler**; it yields the medium for ESP-NOW/BLE/
   LoRa-TX slots.
3. **Tune SF/BW, not band**, to trade range vs airtime vs robustness.
4. **Stagger heavy TX bursts** and budget decoupling to dodge supply-rail sag.
5. **Use processing gain + GroupID** to survive the crowded external 2.4 GHz band,
   since there's no sub-GHz refuge on this hardware.
6. **Guard intervals sized from measured clock drift** (Phase 7) before any
   multi-node field test.
