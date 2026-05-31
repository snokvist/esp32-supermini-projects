# Hybrid Local + Long-Range Mesh Communication Platform (single-band 2.4 GHz)

> Working codename: **Waymesh** (provisional). A research and proof-of-concept
> program, not a product. The goal is to *discover* the optimal balance between
> local synchronization, longer-range relaying, mobility, power efficiency, and
> decentralized usability on **ExpressLRS-class 2.4 GHz hardware** — not to clone
> Meshtastic or ExpressLRS.

## The one-paragraph thesis

A small, battery-powered, GPS-capable node carries **one 2.4 GHz band but two
radios with complementary strengths**: the **LR1121** transceiver running
**2.4 GHz LoRa** for cheap-to-listen, longer-range, low-rate links, and the
**ESP32-C3's own WiFi radio (ESP-NOW)** for fast, bursty, local peer sync. Both
live in 2.4 GHz on the same tiny board, so they **cannot transmit at once** —
the architecture is built around **time-division (a super-frame)** between them.
The non-obvious lever that still makes this work: **LR1121 2.4 GHz LoRa RX
costs ~6 mA while WiFi RX costs ~95 mA** — so LoRa is the always-listening
control/wake plane and ESP-NOW is a scheduled high-speed burst plane. That
asymmetry survives the single-band constraint and is the heart of the design.

## What we're building now (and the end goal)

> **Near-term (May 2026):** the active build is a **flat 2.4 GHz LoRa node you
> inspect with the stock Meshtastic app over BLE-GATT** — *one* radio (the LR1121
> LoRa link) for the mesh, plus the C3's **BLE** for a zero-custom-UI Meshtastic
> client. ESP-NOW, clusters, and digest aggregation (the two-radio thesis above)
> are **deferred, not cut** — they're the end goal that justifies the second radio.
>
> - **Build now:** [04 Architecture](04-architecture.md) ·
>   [05 Protocol](05-protocol.md) · [09 Roadmap](09-poc-roadmap.md) ·
>   [11 Meshtastic client](11-mobile-gateway-meshtastic-compat.md)
> - **End goal:** [12 — The full hybrid mesh](12-end-goal-full-hybrid-mesh.md)
>   (absorbs the former coexistence / power / mobility deep-dives)

## Hardware baseline: RadioMaster XR2 Nano (the first target)

The concrete first device is the **RadioMaster XR2 Nano** — which happens to be
exactly **ESP32-C3 + LR1121**, the chipset this program was designed around.

| Part | Role | Key facts |
|------|------|-----------|
| ESP32-C3 (on XR2) | Host MCU + local radio | RISC-V @160 MHz, WiFi+BLE 2.4 GHz, deep-sleep ~5 µA, WiFi RX ~95 mA, TX peak ~180–240 mA |
| LR1121 (on XR2) | Long-range radio, **2.4 GHz only on this board** | chip is multi-band, but the XR2 has an **integrated 2.4 GHz tower antenna and no sub-GHz RF path**. 2.4 GHz LoRa: RX ~6 mA, TX +13 dBm ~50 mA, sleep ~1.4 µA |
| GNSS module (e.g. u-blox MAX-M10S) | Position | ~25 mA tracking; wired to the XR2's spare UART (costs the CRSF interface) |
| Power | 5 V working voltage (onboard reg to 3.3 V); LiPo node needs boost or a 3.3 V tap | runtime is RX-dominated; see [12 §4](12-end-goal-full-hybrid-mesh.md#4--power--runtime) |
| Firmware | ELRS v3.5.1 preinstalled | **open-source ELRS already drives the LR1121 on this exact board** → known pin map + driver to crib from; reflash via WiFi/UART |

See [02 — Hardware & RF Platform](02-hardware-and-rf-platform.md) for the full
electrical picture, IO map, and flashing notes.

## Single-band consequence (read this first)

Because the radios are all in 2.4 GHz on the XR2:
- **RF coexistence / time-division is unavoidable.** The two 2.4 GHz radios sit
  millimeters apart and desensitize each other if active at once. *Near-term*
  that's **BLE vs the 2.4-LoRa link** (two radios); the full three-radio
  super-frame (LoRa + ESP-NOW + BLE) is the end goal
  ([12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem)).
- **"Long-range" is relative.** 2.4 GHz LoRa at high spreading factor with a
  0.8 g integrated antenna reaches hundreds of metres to ~km in the open, far
  less in clutter — not the multi-km of sub-GHz. Range/airtime/sensitivity is a
  tunable knob (SF + bandwidth), measured in the POCs.
- **No EU868-style 1 % duty cycle.** 2.4 GHz ISM rules are power-limited (FCC
  15.247 / ETSI EN 300 328), so the airtime ceiling is **self-coexistence** and
  the crowded band, not regulation.
- **The cheap-listen power thesis holds** (6 mA LoRa RX vs 95 mA WiFi RX).

What the XR2 can and can't prove:

| XR2 validates | XR2 can't show (needs other hardware) |
|---|---|
| flat 2.4-LoRa mesh: beacon, range, managed flood | the sub-GHz long-range advantage (no sub-GHz antenna) |
| Meshtastic BLE-GATT client over LoRa | clean dual-band concurrency |
| **BLE↔2.4-LoRa coexistence** | multi-km link budgets |
| (end goal) ESP-NOW, cluster head/aggregation, power model | EU868 duty-cycle behavior |

## How these docs are organized

| # | Document | What it delivers |
|---|----------|------------------|
| 01 | [Vision & Requirements](01-vision-and-requirements.md) | Goals, non-goals, success metrics, target use cases, design principles |
| 02 | [Hardware & RF Platform](02-hardware-and-rf-platform.md) | XR2 (ESP32-C3 + LR1121, 2.4-only): bands, IO map, GNSS-on-UART, power, flashing |
| 03 | [Comparative Analysis](03-comparative-analysis.md) | Meshtastic / ExpressLRS / ESP-NOW / BLE-mesh / DTN / MANET — what we borrow, where the niche is |
| 04 | [Architecture](04-architecture.md) | **Near-term:** the flat 2.4-LoRa mesh + the Meshtastic BLE-GATT gateway; what's deferred |
| 05 | [Protocol](05-protocol.md) | **Near-term:** flat LoRa frame formats, message classes, 2.4-LoRa airtime budgets, Meshtastic mapping |
| 09 | [POC Roadmap](09-poc-roadmap.md) | **Active track:** P0✓ → P3 link → Phase G client → P5 relay → P6/P7 → P9, with go/no-go gates |
| 10 | [Experiments & Metrics](10-experiments-and-metrics.md) | **Near-term:** metrics, rigs, config matrix, logging schema |
| 11 | [Mobile/PC Gateway: Meshtastic Compat](11-mobile-gateway-meshtastic-compat.md) | The centerpiece: a phone (BLE) / PC (serial) sees the node as a Meshtastic device — reuse their apps as our UI |
| 12 | [End Goal: Full Hybrid Mesh](12-end-goal-full-hybrid-mesh.md) | **Deferred:** the two-plane (ESP-NOW + LoRa) mesh — coexistence, power/runtime, mobility/clustering, aggregation, DTN, deferred phases |
| 13 | [Auth & Groups](13-auth-and-groups.md) | **Spec (no firmware yet):** group identity + authentication by reusing the Meshtastic channel hash + PSK/AES-CTR on a `v2` beacon — wire format, RX/relay/gateway changes, phasing |

## Current status

- **Phase 0: done (single-node).** Bench bring-up on a RadioMaster XR2 Nano —
  LR1121 2.4-LoRa radio + GNSS up, beacon TX, CSV logging, badcrc=0. 2-node
  loopback pending a second XR2. Firmware: `projects/waymesh-node`
  (RadioLib 7.6.0, verified XR2 pin map).
- **Reprioritization (May 2026): lead with the 2.4-LoRa link + a
  Meshtastic-compatible BLE-GATT client; defer the ESP-NOW local plane and the
  cluster-head/aggregation arc.** The BLE-GATT gateway
  ([11](11-mobile-gateway-meshtastic-compat.md)) gives a polished phone/PC UI at
  ~zero UI cost and doesn't need clustering, so a flat 2.4-LoRa node inspected
  with the stock Meshtastic app is the shippable near-term target. ESP-NOW +
  clusters + aggregation remain the long-term thesis — **deferred, not cut**
  (ESP-NOW also reaches LoRa-less ESP32 nodes). Near-term coexistence is two
  co-located radios (BLE + 2.4-LoRa), not three. Full deferred design:
  [12](12-end-goal-full-hybrid-mesh.md).
- **Open questions for the bench:** achievable 2.4-LoRa range/PDR vs SF/BW with
  the integrated antenna; **BLE↔2.4-LoRa coexistence**; Meshtastic app/CLI
  compatibility + MTU/bonding on a screenless node; real per-state currents and
  5 V/LiPo powering. *(Deferred with the local plane: 2.4-LoRa↔WiFi self-desense,
  TDM guard intervals, election thrash.)*
- **Next concrete step:** the active track in [09](09-poc-roadmap.md) — **P3**
  (2-node flat 2.4-LoRa link, needs a 2nd XR2) → **Phase G** (Meshtastic BLE-GATT
  client: read-only → TX → config).

## Guiding constraints (so this stays a research program, not a framework)

1. **Architecture over features.** No polished UI, no internet/cloud, no media.
2. **Small, measurable POCs** on real XR2 hardware. Every phase ends with numbers
   and a go/no-go.
3. **Don't assume mesh is always good.** Each near-term phase must earn its keep
   vs a plain flat 2.4-LoRa flood; each end-goal phase must beat *both* flat flood
   *and* plain ESP-NOW for some real scenario, or it's cut.
4. **Airtime and µA are the budgets**, and on one shared band, **time itself is
   the scarcest resource** — near-term it's BLE-vs-LoRa slotting; the full
   super-frame governs everything once the local plane lands.
