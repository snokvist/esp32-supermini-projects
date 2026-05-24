# 09 — POC Roadmap (active near-term track)

Small, testable milestones. Each phase has an **Outcome**, what it **Validates**,
the **Experiments**, the **Metrics**, and a **Go/No-Go gate**. Each phase, when
built, becomes a project under `projects/` following repo conventions
(`platformio.ini`, `src/`, `README.md`, `docs/HARDWARE.md`, `docs/TASK_LOG.md`)
and must pass the repo verification loop (build → flash → observe → report).

Hardware target: **RadioMaster XR2 Nano** (ESP32-C3 + LR1121, 2.4 GHz-only),
reusing ExpressLRS's open-source LR1121 driver / pin map. GNSS on the spare UART.
Firmware project: `projects/waymesh-node` (incremental; later phases extend it).

> **Reprioritization (May 2026): lead with the 2.4-LoRa link + a
> Meshtastic-compatible BLE-GATT client; defer the ESP-NOW local plane and the
> cluster-head/aggregation arc.** The BLE-GATT gateway
> ([11](11-mobile-gateway-meshtastic-compat.md)) gives a polished phone/PC UI at
> ~zero UI cost and doesn't depend on clustering, so a **flat 2.4-LoRa node you
> inspect with the stock Meshtastic app** is a coherent, shippable near-term
> target. ESP-NOW + clusters + digest aggregation remain the long-term thesis
> (the airtime win) — **deferred, not cut** (ESP-NOW also reaches LoRa-less ESP32
> nodes). The full deferred arc and its design live in
> [12 — End Goal](12-end-goal-full-hybrid-mesh.md).

**Active order (this doc):** P0 ✓ → **P3** flat 2.4-LoRa link → **Phase G**
Meshtastic BLE-GATT client → **P5** managed-flood relay → **P6/P7** power +
coexistence (two-radio) → **P9** field capstone.
**Deferred ([12 §6](12-end-goal-full-hybrid-mesh.md#6--deferred-roadmap-phases)):**
P1, P2, P4, P8, and the three-radio extensions of P6/P7.

Phases keep their original research-plan numbers (so the deferred set in
[12](12-end-goal-full-hybrid-mesh.md) stays consistent); the active order is the
list above, not the numeric order.

The gate question for every phase: **"Does this provide a measurable real-world
advantage over plain 2.4-LoRa-flood?"** If no, narrow scope or stop.

---

## Phase 0 — Bench bring-up & baseline — ✅ DONE (single-node)

- **Outcome:** reflash the XR2's C3; LR1121 (SPI) 2.4-LoRa, GNSS (spare UART),
  LED, serial all work; baseline behaviour observed.
- **Status (2026-05-24):** radio_ok + beacon TX (seq climbing) + GPS 3D fix
  (sats=5) + badcrc=0 verified on one XR2. **2-node loopback still pending a
  second XR2.** Firmware on `feature/xr2-phase0-bringup` (RadioLib 7.6.0, verified
  pin map). Current-draw measurement deferred into P6.
- **Deliverable:** `docs/HARDWARE.md` XR2 pin map + flashing/powering notes (done).

## Phase 3 — Flat 2.4-LoRa link (the mesh radio) — **ACTIVE, next**

- **Outcome:** two XR2 nodes exchange per-node presence/GPS over **2.4 GHz LoRa**;
  first real range test with the integrated antenna.
- **Validates:** the LoRa link, real airtime/ToA, range/PDR vs SF/BW.
- **Experiments:** ping-pong at SF8/SF10/SF12 across BW 406/812, +10/+13 dBm;
  open-field range walk; measure ToA on a scope vs the [05](05-protocol.md) table.
- **Metrics:** PDR vs distance per SF/BW; measured ToA; achieved range; energy per
  delivered beacon.
- **Go/No-Go:** usable range (target ≥500 m LOS at high SF on the integrated
  antenna) and ToA within ~20 % of predicted. **Needs a second XR2.**

## Phase G — Meshtastic BLE-GATT client gateway — **ACTIVE (centerpiece)**

- **Outcome:** a phone (BLE) and PC (USB serial) see the node as a Meshtastic
  device via the client-compat shim
  ([11](11-mobile-gateway-meshtastic-compat.md)), inspecting/injecting over the
  flat P3 LoRa mesh. No cluster digests yet → the gateway maps per-node LoRa
  beacons **directly** to `NodeInfo` (de-aggregation arrives with the deferred P4).
- **Validates:** the zero-custom-UI requirement
  ([01](01-vision-and-requirements.md) goal 6) — the stock Meshtastic app/CLI is
  our UI.
- **Increments (ship in order):**
  1. **Read-only** — NimBLE GATT server + `want_config_id` handshake + node DB +
     positions + text RX. Open the Meshtastic app and watch the P3 mesh appear.
  2. **TX** — phone sends text → decrypt with the advertised channel PSK → flood
     onto the 2.4-LoRa mesh.
  3. **Channels / config-write** — multiple channels, accept-and-reflect config.
- **Experiments:** Android + `meshtastic` Python CLI against 2–3 P3 nodes; MTU
  negotiation (~512); fixed-PIN bonding on a screenless node; measure BLE-active
  impact on LoRa PDR (the **two-radio** coexistence question — BLE + 2.4-LoRa).
- **Metrics:** handshake success across app/CLI versions; FromRadio/MTU
  throughput; LoRa PDR with BLE idle vs advertising vs connected.
- **Go/No-Go:** an unmodified Meshtastic client reliably lists nodes/positions and
  round-trips text over our LoRa mesh, with quantified BLE↔LoRa coexistence cost.

## Phase 5 — Managed-flood relay (flat multi-hop) — **ACTIVE (as the mesh grows)**

- **Outcome:** multi-hop LoRa delivery with hop limit, dedup, SNR-delay, overhear
  suppression — Meshtastic-style discipline over the flat mesh (no clusters).
- **Validates:** controlled flooding without retransmission storms.
- **Experiments:** 3+ nodes in a line/triangle; inject text via the gateway;
  measure redundancy factor R with each suppression mechanism on/off; storm stress.
- **Metrics:** R vs PDR per mechanism, hop-count distribution, no-storm
  confirmation under load.
- **Go/No-Go:** multi-hop delivery with R kept low (target R ≲ 2) and no storms.

## Phase 6 — Power & runtime (two-radio) — **ACTIVE once P3 is stable**

- **Outcome:** measured per-state currents for the near-term node (LoRa RX/TX,
  C3-idle, BLE, GNSS); validated runtime on real cells.
- **Validates:** the LoRa-RX-dominated power model (the ~6 mA cheap-listen claim).
- **Experiments:** current draw per state with a meter; LoRa-RX vs BLE-connected
  vs GNSS-on; 5 V-boost vs 3.3 V-tap powering; small-cell brownout under +13 dBm
  LoRa peaks.
- **Metrics:** measured mA per state; achieved runtime per battery size; brownout
  threshold per cell.
- **Go/No-Go:** standby (LoRa RX + idle) ≥3 days on ≤1000 mAh; no brownouts at the
  chosen TX power / cell size. *(ESP-NOW windows + Head profiles are the
  three-radio extension → [12 §4](12-end-goal-full-hybrid-mesh.md#4--power--runtime).)*

## Phase 7 — RF coexistence (two-radio: BLE + 2.4-LoRa) — **ACTIVE once P3 is stable**

- **Outcome:** the BLE↔2.4-LoRa coexistence picture for the near-term node.
- **Validates:** that a connected phone doesn't cripple the LoRa mesh.
- **Experiments:** antenna isolation (S21, C3 vs LR1121 antennas); supply coupling
  (scope the 3.3 V rail during +13 dBm LoRa bursts); LoRa PDR with BLE idle vs
  advertising vs connected.
- **Metrics:** isolation (dB); supply sag vs TX power; LoRa PDR cliff vs BLE
  activity.
- **Go/No-Go:** a documented BLE-gateway-slot policy with tolerable LoRa impact.
  *(The four ESP-NOW/WiFi self-desense + guard-interval tests are the three-radio
  extension → [12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem).)*

## Phase 9 — Field trial (capstone) — **ACTIVE (program-level)**

- **Outcome:** a real hike/convoy scenario end-to-end on the near-term node:
  several XR2s flooding presence/GPS/text over LoRa, watched live in the
  Meshtastic app, under motion.
- **Validates:** the whole near-term system in the field; sim vs reality.
- **Metrics:** PDR, latency, range, churn — under motion; head-to-head vs a flat
  2.4-LoRa-flood baseline.
- **Go/No-Go (program-level):** the node is genuinely useful for a real mobile
  group over LoRa, with numbers. *(The all-planes A/B-vs-flood version is the
  deferred end-goal P9 → [12 §6](12-end-goal-full-hybrid-mesh.md#6--deferred-roadmap-phases).)*

---

## Active dependency graph

```
P0 bring-up ✓ ──> P3 flat 2.4-LoRa link ──> Phase G Meshtastic BLE-GATT client ──┐
                          └─> P6 power ──> P7 coexistence (BLE + 2.4-LoRa) ───────┼─> P5 relay ──> P9 field
```

P0→P3→Phase G is the critical path. P6/P7 begin once P3 hardware is stable and are
scoped to **two** co-located 2.4 GHz radios (BLE + 2.4-LoRa), not three. P5 and P9
follow as the mesh grows.

## Deferred (the local-plane / cluster arc)

Full descriptions and design in
[12 — End Goal](12-end-goal-full-hybrid-mesh.md):

- **P1 — Local discovery (ESP-NOW)**, **P2 — GPS propagation (local)**.
- **P4 — Cluster head election + aggregation** — the make-or-break gate for the
  *aggregation thesis* (the ≥3× LRP-airtime win), now sequenced behind a working,
  app-visible LoRa node rather than in front of it.
- **P8 — Store-and-forward / DTN.**
- The **three-radio extensions** of P6 (ESP-NOW/Head power profiles) and P7
  (ESP-NOW/WiFi self-desense, guard intervals, external congestion).
- **Sub-GHz dual-band** (needs different hardware than the 2.4-only XR2) and
  **FLRC / LR-FHSS** modes — evaluate only if the 2.4-LoRa link needs them.
