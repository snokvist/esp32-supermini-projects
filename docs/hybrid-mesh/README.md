# Hybrid Long-Range + Local Mesh Communication Platform

> Working codename: **Waymesh** (provisional). A research and proof-of-concept
> program, not a product. The goal is to *discover* the optimal balance between
> local synchronization, long-range relaying, mobility, power efficiency, and
> decentralized usability — not to clone Meshtastic or ExpressLRS.

## The one-paragraph thesis

A small, battery-powered, GPS-capable node carries **two radios with
complementary strengths**: an **LR1121** multi-band LoRa transceiver for
cheap-to-listen, expensive-to-talk **long-range** links (sub-GHz), and the
**ESP32-C3's own 2.4 GHz radio (ESP-NOW)** for fast, bursty, **local** peer
sync. The architecture keeps chatter local and rations the long-range channel:
*nearby nodes talk cheaply over ESP-NOW; only digests and selected traffic
cross LoRa.* The non-obvious lever is that **LoRa RX costs ~6 mA while WiFi RX
costs ~95 mA** — so LoRa becomes the always-listening control/wake-up plane and
ESP-NOW becomes a scheduled high-speed burst plane. That asymmetry is the heart
of the design.

## Hardware baseline (this program)

| Part | Role | Key facts |
|------|------|-----------|
| ESP32-C3 SuperMini | Host MCU + local radio | RISC-V @160 MHz, WiFi+BLE 2.4 GHz, deep-sleep ~5 µA, WiFi RX ~95 mA, TX peak ~180–240 mA |
| LR1121 | Long-range radio | 150–960 MHz + 2.4 GHz + S/L-band, LoRa/GFSK/LR-FHSS, RX ~6 mA, TX +22 dBm ~90 mA (sub-GHz) / +13 dBm ~50 mA (2.4 GHz), sleep ~1.4 µA. **No GNSS/WiFi scanner** (unlike LR1110/LR1120) → a dedicated GNSS module is required for positioning |
| GNSS module (e.g. u-blox MAX-M10S) | Position | ~25 mA tracking; must be duty-cycled |
| LiPo 250–2000 mAh | Power | Runtime is RX-dominated; see [07](07-power-and-runtime.md) |

See [02 — Hardware & RF Platform](02-hardware-and-rf-platform.md) for the full
electrical picture and wiring.

## How these docs are organized

Read in order for the full argument, or jump to a deliverable.

| # | Document | What it delivers |
|---|----------|------------------|
| 01 | [Vision & Requirements](01-vision-and-requirements.md) | Distilled goals, non-goals, success metrics, target use cases, design principles |
| 02 | [Hardware & RF Platform](02-hardware-and-rf-platform.md) | ESP32-C3 + LR1121 + GNSS reality: bands, SPI wiring, power rails, antennas |
| 03 | [Comparative Analysis](03-comparative-analysis.md) | Meshtastic / ExpressLRS / ESP-NOW / BLE-mesh / DTN / MANET / swarm — what we borrow and where our niche is |
| 04 | [Architecture](04-architecture.md) | Two-plane design, node roles, addressing, the "what crosses long-range" decision logic, mobile interaction, flow diagrams |
| 05 | [Protocol](05-protocol.md) | Concrete binary frame formats for both planes, message classes, airtime budgets, store-and-forward envelope |
| 06 | [RF Coexistence](06-rf-coexistence.md) | **Core research area:** 2.4 GHz (WiFi/BLE/LoRa) vs sub-GHz interaction, desense, scheduling, empirical test plan |
| 07 | [Power & Runtime](07-power-and-runtime.md) | Per-state current model, duty-cycling strategies, runtime tables for real batteries |
| 08 | [Mobility & Topology](08-mobility-and-topology.md) | Group mobility models, flooding-vs-routing analysis, relay suppression, cluster leadership, scalability, simulation plan |
| 09 | [POC Roadmap](09-poc-roadmap.md) | Phased, testable milestones with go/no-go gates |
| 10 | [Experiments & Metrics](10-experiments-and-metrics.md) | Measurement methodology, metrics, rigs, logging schema |

## Current status

- **Phase:** Pre-Phase-0 (paper architecture). No firmware committed yet.
- **Decided:** Two-plane architecture (LoRa sub-GHz long-range + ESP-NOW local).
  Hierarchy/cluster-head bridging over pure flooding for the long-range plane.
- **Open questions to resolve empirically:** real LR1121↔ESP-NOW coexistence
  desense; achievable LoRa range/PDR vs mobility; whether cluster-head election
  churn is tolerable; real per-state currents on the SuperMini.
- **Next concrete step:** Phase 0 bench bring-up (LR1121 SPI loopback + GNSS
  fix + baseline current measurement). See [09](09-poc-roadmap.md).

## Guiding constraints (so this stays a research program, not a framework)

1. **Architecture over features.** No polished UI, no internet/cloud, no media.
2. **Small, measurable POCs.** Every phase ends with numbers and a go/no-go.
3. **Don't assume mesh is always good.** Each phase must show the hybrid beats
   plain LoRa-mesh *and* plain ESP-NOW for some real scenario, or it's cut.
4. **Airtime and µA are the budgets.** Every design choice is judged against
   long-range airtime and average current.
