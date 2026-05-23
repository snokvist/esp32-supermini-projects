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

## Hardware baseline: RadioMaster XR2 Nano (the first target)

The concrete first device is the **RadioMaster XR2 Nano** — which happens to be
exactly **ESP32-C3 + LR1121**, the chipset this program was designed around.

| Part | Role | Key facts |
|------|------|-----------|
| ESP32-C3 (on XR2) | Host MCU + local radio | RISC-V @160 MHz, WiFi+BLE 2.4 GHz, deep-sleep ~5 µA, WiFi RX ~95 mA, TX peak ~180–240 mA |
| LR1121 (on XR2) | Long-range radio, **2.4 GHz only on this board** | chip is multi-band, but the XR2 has an **integrated 2.4 GHz tower antenna and no sub-GHz RF path**. 2.4 GHz LoRa: RX ~6 mA, TX +13 dBm ~50 mA, sleep ~1.4 µA |
| GNSS module (e.g. u-blox MAX-M10S) | Position | ~25 mA tracking; wired to the XR2's spare UART (costs the CRSF interface) |
| Power | 5 V working voltage (onboard reg to 3.3 V); LiPo node needs boost or a 3.3 V tap | runtime is RX-dominated; see [07](07-power-and-runtime.md) |
| Firmware | ELRS v3.5.1 preinstalled | **open-source ELRS already drives the LR1121 on this exact board** → known pin map + driver to crib from; reflash via WiFi/UART |

See [02 — Hardware & RF Platform](02-hardware-and-rf-platform.md) for the full
electrical picture, IO map, and flashing notes.

## Single-band consequence (read this first)

Because both planes are in 2.4 GHz on the XR2:
- **RF coexistence / time-division is the central, unavoidable design problem**
  ([06](06-rf-coexistence.md)), not an optional mode. The two 2.4 GHz radios sit
  millimeters apart and will desensitize each other if active at once.
- **"Long-range" is relative.** 2.4 GHz LoRa at high spreading factor with a
  0.8 g integrated antenna reaches hundreds of metres to ~km in the open, far
  less in clutter — not the multi-km of sub-GHz. Range/airtime/sensitivity is a
  tunable knob (SF + bandwidth), measured in the POCs.
- **No EU868-style 1 % duty cycle.** 2.4 GHz ISM rules are power-limited (FCC
  15.247 / ETSI EN 300 328), so the airtime ceiling is **self-coexistence**
  (sharing the super-frame with ESP-NOW/BLE) and the crowded band, not regulation.
- **The cheap-listen power thesis still holds** (6 mA vs 95 mA), so the design
  shape is preserved; only the band plan changes.

What the XR2 can and can't prove:

| XR2 validates now | XR2 can't show (needs other hardware later) |
|---|---|
| ESP-NOW local discovery/sync | the sub-GHz long-range advantage (no sub-GHz antenna) |
| 2.4 GHz LoRa beacon + range | clean dual-band concurrency |
| **coexistence: 2.4-LoRa vs WiFi self-desense** | multi-km link budgets |
| cluster head/aggregation, power model | EU868 duty-cycle behavior |

## How these docs are organized

| # | Document | What it delivers |
|---|----------|------------------|
| 01 | [Vision & Requirements](01-vision-and-requirements.md) | Goals, non-goals, success metrics, target use cases, design principles |
| 02 | [Hardware & RF Platform](02-hardware-and-rf-platform.md) | XR2 (ESP32-C3 + LR1121, 2.4-only): bands, IO map, GNSS-on-UART, power, flashing |
| 03 | [Comparative Analysis](03-comparative-analysis.md) | Meshtastic / ExpressLRS / ESP-NOW / BLE-mesh / DTN / MANET — what we borrow, where the niche is |
| 04 | [Architecture](04-architecture.md) | Single-band two-radio design, mandatory super-frame, node roles, addressing, "what crosses long-range" logic, flows |
| 05 | [Protocol](05-protocol.md) | Binary frame formats, message classes, 2.4 GHz LoRa airtime budgets, store-and-forward |
| 06 | [RF Coexistence](06-rf-coexistence.md) | **The core problem:** two 2.4 GHz radios, self-desense, the TDM schedule, empirical test plan |
| 07 | [Power & Runtime](07-power-and-runtime.md) | Per-state current model, duty-cycling, runtime tables |
| 08 | [Mobility & Topology](08-mobility-and-topology.md) | Mobility models, flooding-vs-routing, suppression, leadership, scalability, simulation plan |
| 09 | [POC Roadmap](09-poc-roadmap.md) | Phased milestones on the XR2 with go/no-go gates |
| 10 | [Experiments & Metrics](10-experiments-and-metrics.md) | Measurement methodology, metrics, rigs, logging schema |
| 11 | [Mobile/PC Gateway: Meshtastic Compat](11-mobile-gateway-meshtastic-compat.md) | Make a phone (BLE) or PC (USB serial) see the node as a Meshtastic device — reuse the Meshtastic apps/CLI as our UI |

## Current status

- **Phase:** Pre-Phase-0 (paper architecture), now re-centered on single-band
  2.4 GHz targeting the RadioMaster XR2 Nano.
- **Decided:** single 2.4 GHz band; ESP-NOW (local burst) + LR1121 2.4-LoRa
  (long-range, cheap-listen) **time-division multiplexed**; hierarchy/cluster-
  head bridging over pure flooding on the long-range plane; GPS included from the
  early phases via the XR2's spare UART.
- **Open questions for the bench:** real 2.4-LoRa↔WiFi self-desense and the TDM
  guard intervals needed; achievable 2.4-LoRa range/PDR vs SF/BW with the
  integrated antenna; election thrash under mobility; real per-state currents and
  5 V/LiPo powering of the XR2.
- **Next concrete step:** Phase 0 bench bring-up on the XR2 (reflash C3,
  LR1121 2.4-LoRa loopback, GNSS-on-UART fix, baseline currents). See
  [09](09-poc-roadmap.md).

## Guiding constraints (so this stays a research program, not a framework)

1. **Architecture over features.** No polished UI, no internet/cloud, no media.
2. **Small, measurable POCs** on real XR2 hardware. Every phase ends with numbers
   and a go/no-go.
3. **Don't assume mesh is always good.** Each phase must show the hybrid beats
   plain 2.4-LoRa-flood *and* plain ESP-NOW for some real scenario, or it's cut.
4. **Airtime and µA are the budgets**, and on one shared band, **time itself is
   the scarcest resource** — the super-frame governs everything.
