# 02 — Hardware & RF Platform

The physical reality the architecture must respect. The first target is the
**RadioMaster XR2 Nano**, which is **ESP32-C3 + Semtech LR1121** — the exact
chipset this program assumes — but configured as a **2.4 GHz-only** board. All
current figures are datasheet-derived and **must be bench-verified** in Phase 0.

## Node hardware tiers (heterogeneous)

The program deliberately mixes capability tiers — the *cheapest silicon that can
do each job* — rather than full smart nodes everywhere. Thesis: a few smart
gateways + many cheap GPS beacons + sparse dumb relays scales, powers, and
deploys better than an all-C3 mesh ([01](01-vision-and-requirements.md),
[03](03-comparative-analysis.md)).

| Tier | Example board | MCU | LoRa radio | Local / aux | Role |
|------|---------------|-----|------------|-------------|------|
| **1 — smart** | RadioMaster XR2 Nano | ESP32-C3 | **LR1121** 2.4 GHz | WiFi/BLE, ESP-NOW, GNSS | gateway, aggregation, mobility, BLE/phone |
| **2 — mid** | Bayck 7PWM RX | ESP8285 | **SX1280** 2.4 GHz | GNSS via remapped PWM-pin UART | GPS beacon + packet relay, battery tracker |
| **3 — dumb relay** | BetaFPV Nano RX | ESP8285 | **SX1280** 2.4 GHz | none | forward-only repeater, low-power always-on |

All three are ELRS-class boards already in hand (ELRS is the known-good driver
reference for each radio). They share exactly **one** thing on the wire: the
2.4 GHz LoRa long-range plane. The rest of this doc details Tier 1 (the XR2, our
first target); Tier 2/3 hardware notes follow the interop section.

### The interop crux: LR1121 (Tier 1) ↔ SX1280 (Tier 2/3)

Tier 1 runs a **Semtech LR1121**; Tiers 2/3 run a **Semtech SX1280** (the ELRS
2.4 GHz radio). These are *different chips* — the heterogeneous mesh only works
if they share the air. They can: the **LR1121's 2.4 GHz LoRa mode is designed to
be SX1280-compatible**, so with *exactly matched* PHY parameters a frame TX'd by
one is RX'd by the other. This is unproven in our setup and is the first thing to
validate — **PoC #0** ([09](09-poc-roadmap.md)):

- **Sync word** encoding differs between the families; the on-air value must
  match (Tier-1 default is LoRa "private").
- **Coding rate:** the SX1280 offers "long-interleave" (LI) CR variants the
  LR1121 lacks — use the *standard* CR modes on both.
- Tier-1 config to match: 2450 MHz, BW 812.5 kHz, SF9, CR4/5, preamble — all
  valid SX1280 values, so the SX1280 side is configured to these.

Until PoC #0 passes, the heterogeneous mesh is unproven.

### ESP8285 (Tier 2/3) at a glance

- Tensilica L106 @80 MHz, ~80 KB usable RAM, 1 MB flash — **no BLE**, WiFi only.
- UARTs: UART0 (flash/log) + UART1 (TX-only, GPIO2). GNSS RX therefore rides a
  **SoftwareSerial on a remapped PWM-output GPIO** (the 7PWM's servo pins) — a
  Tier-2 PoC in itself ([09](09-poc-roadmap.md)).
- Flashed via UART pads + GPIO0-low (esptool/FTDI); ELRS is the SX1280 driver
  reference.
- Tight RAM → dumb relays use **low-memory dedup** (small seen-set), not a full
  node DB.

## Target device: RadioMaster XR2 Nano

| Spec | Value | Consequence |
|------|-------|-------------|
| MCU | ESP32-C3 | matches the repo's whole baseline; WiFi+BLE 2.4 GHz; USB-CDC via the C3 |
| Radio | Semtech LR1121 | multi-band silicon, but on this board only the 2.4 GHz path is wired |
| Frequency | 2.400–2.479 GHz **only** | **no sub-GHz** — the long-range plane is 2.4 GHz LoRa |
| Antenna | integrated 2.4 GHz tower antenna | fixed, tiny → modest range; no external antenna on stock board |
| Size / weight | 16 × 12 × 6 mm / 0.8 g | genuinely wearable/drone-class |
| Power | 5 V working voltage (onboard reg) | LiPo node needs a boost to 5 V *or* a tap into the 3.3 V rail |
| IO | CRSF (TX/RX) + a secondary UART | **very few pins** — GNSS competes with CRSF (see below) |
| Telemetry power | 10 mW (10 dBm) | the stock 2.4-LoRa TX power; LR1121 can do up to +13 dBm |
| Firmware | ExpressLRS v3.5.1 preinstalled | **open-source ELRS already drives the LR1121 here** |

### Why the XR2 is a good first target

- **Chipset is exactly right** (ESP32-C3 + LR1121); no custom PCB or wiring to
  bring up a radio.
- **ELRS is open source** and runs on this board, so we have a known-good pin
  map, SPI setup, LR1121 driver, and PA/frequency config to crib from for
  Phase 0 — a huge head start.
- **It forces the core research** (two 2.4 GHz radios coexisting) immediately.
- Tiny, cheap, battery-friendly, and already in hand.

### What the XR2 *cannot* do

- **No sub-GHz** → can't demonstrate the multi-km sub-GHz long-range advantage.
  That needs a sub-GHz-antenna'd LR1121 board or a BAYCK dual-band "Gemini" RX
  later.
- **No clean dual-band concurrency** → both planes share 2.4 GHz; strict
  time-division is mandatory.
- **Limited IO** → hard to attach many peripherals; GNSS uses the spare UART.

## Node block diagram (XR2, single-band)

```
                +--------------------------------------------------+
                |              ESP32-C3 (on XR2 Nano)              |
                |   RISC-V @160 MHz · WiFi+BLE 2.4 GHz · 400 KB SR |
                |                                                  |
   2.4 GHz <----+ [WiFi/BLE radio] ESP-NOW (local plane) + BLE GW |
   (its own     |                                                  |
    antenna)    |   SPI ----------------+   UART(spare) ----+      |
                +-------------+---------+---------+---------+------+
                              |                   |
                       +------v------+     +------v-------+
                       |   LR1121    |     |  GNSS         |
                       | 2.4 GHz     |     |  (u-blox      |
                       | LoRa only   |     |   MAX-M10S)   |
                       +------+------+     +--------------+
                              |
                  2.4 GHz LoRa | (integrated 2.4 GHz tower antenna)
                              v
                    long-range plane (2.4 GHz)
```

**Two 2.4 GHz emitters on a 0.8 g board** (C3 WiFi/BLE + LR1121 2.4-LoRa), each
with its own antenna millimetres apart. This is the defining hardware constraint:
they desensitize each other if active simultaneously → **time-division only**
([12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem);
near-term it's BLE vs 2.4-LoRa, [04](04-architecture.md)).

## ESP32-C3 (host + local radio)

- Single-core RISC-V @160 MHz, 400 KB SRAM, 2.4 GHz WiFi (b/g/n) + BLE 5.0.
- **One 2.4 GHz WiFi/BLE radio, time-shared** between ESP-NOW and BLE — and now
  also time-shared (at the schedule level) with the LR1121's 2.4-LoRa to avoid
  mutual desense.
- USB-CDC per repo convention (`-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`)
  — on the XR2, USB is via the C3; serial logging works once reflashed.

### ESP32-C3 current (estimates)

| State | Current | Note |
|-------|---------|------|
| Deep sleep | ~5 µA | |
| Light sleep (wake on LR1121 IRQ) | ~130 µA | RAM retained, <1 ms wake |
| Modem-sleep (CPU on, RF off) | ~20–30 mA | |
| WiFi RX (ESP-NOW window) | ~95–100 mA | **the power villain** |
| WiFi TX | ~180–240 mA peak | brief bursts |
| BLE TX/RX | ~tens of mA | advertising duty-cycles cheaply |

## LR1121 (long-range radio, 2.4 GHz on this board)

- Semtech 3rd-gen ultra-low-power LoRa transceiver. Silicon supports 150–960 MHz,
  2.4 GHz, S/L-band — **but the XR2 only wires 2.4 GHz**.
- **2.4 GHz LoRa**: SF5–SF12, bandwidths 203 / 406 / 812 / 1625 kHz (range vs
  airtime knob). Also supports (G)FSK and **FLRC** (fast, ELRS uses it) — FLRC is
  a possible medium-range/high-rate option but ESP-NOW already covers fast local.
- 2.4 GHz PA: up to **+13 dBm** (XR2 ships at 10 dBm telemetry).
- **No GNSS/WiFi scanner** (unlike LR1110/LR1120) → dedicated GNSS module needed.
- Interface: SPI + BUSY + IRQ/DIO + NRESET; respect BUSY handshaking. ELRS's
  LR1121 driver on this board is the reference.

### LR1121 current at 2.4 GHz (estimates)

| State | Current | Note |
|-------|---------|------|
| Sleep (retention) | ~1.4 µA | |
| Standby RC / XOSC | ~1.8 / ~1.9 µA | |
| **RX 2.4 GHz** | **~6.1 mA** | **cheap to listen — the key fact** |
| TX +10 dBm (2.4 GHz) | ~30–40 mA | XR2 stock telemetry power |
| TX +13 dBm (2.4 GHz) | ~50 mA | LR1121 2.4 GHz max |

Takeaway: **LoRa 2.4 RX (~6 mA) is ~16× cheaper than WiFi RX (~95 mA)** even on
one band. LoRa stays the always-listening plane; ESP-NOW stays the burst plane.

## GNSS (kept in the early phases, on the spare UART)

- Reference: u-blox MAX-M10S class, ~25–30 mA tracking; duty-cycle it
  (fix-then-sleep) and gate fix rate by motion.
- **Wiring on the XR2:** the GNSS connects to the **secondary UART pad**. With
  only CRSF + one spare UART exposed, **using the UART for GNSS typically means
  giving up the CRSF interface** — acceptable for a standalone mesh node (we're
  not flying it on a flight controller during these POCs).
- Put the GNSS rail behind a load switch (GPIO-controlled) so a sleeping node
  draws ~0 from it — IO permitting on the tiny board; otherwise power-save mode
  via UBX commands.
- Position is shared as compact deltas, not raw NMEA — see [05](05-protocol.md).

## RF front-end & antennas (the hard part now)

- **Both radios are in 2.4 GHz**, each on its own antenna a few mm apart. A
  +13 dBm (or even +10 dBm) 2.4-LoRa TX next to the WiFi RX front-end will
  desensitize/block it; the same the other way. Channel offset does **not** fix
  near-field front-end overload. → **strict time-division** ([12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem)).
- The integrated 2.4 GHz tower antenna is fixed and small → expect modest range;
  optimize via SF/BW choice rather than antenna gain.
- ELRS already lives with "WiFi mode vs RF mode are never simultaneous" on this
  exact hardware — that precedent is our coexistence baseline.

## Powering the XR2 as a standalone battery node

- Stock "working voltage: 5 V" with an onboard 3.3 V regulator. Options for a
  battery node:
  1. **1S LiPo (3.7–4.2 V) → 5 V boost → XR2 5 V in.** Simple, small boost loss.
  2. **Tap the 3.3 V rail directly** from a LiPo via an LDO/buck — fewer
     conversion losses but bypasses the onboard reg; verify the rail can take it.
- Battery voltage sense on an ADC pin for graceful low-power degradation — IO is
  tight; may need to share/repurpose a pad.
- Phase 0 must measure brownout behavior: a +13 dBm LoRa burst or a 240 mA WiFi
  TX peak on a tiny cell can sag the rail.

## What hardware constrains in the architecture

1. **One 2.4 GHz band, two radios → mandatory time-division (super-frame).**
2. **WiFi RX is expensive (~95 mA) → ESP-NOW is burst-only, scheduled.**
3. **LoRa 2.4 RX is cheap (~6 mA) → LoRa is the always-on wake/control plane.**
4. **No sub-GHz on the XR2 → "long-range" = high-SF 2.4-LoRa, modest range.**
5. **No LR1121 geolocation → dedicated GNSS on the spare UART (costs CRSF).**
6. **Very limited IO and 5 V powering → real integration constraints to solve in
   Phase 0.**
7. **ELRS open-source on this board → reuse its LR1121 driver and pin map.**

## Sources

- [RadioMaster XR2 Nano product page](https://radiomasterrc.com/products/xr2-nano-2-4ghz-expresslrs-receiver)
- [Oscar Liang — RadioMaster XR1/XR2/XR3/XR4 ELRS (LR1121) receivers review](https://oscarliang.com/radiomaster-xr1-xr2-xr3-xr4-elrs-receivers/)
- [LR1121 datasheet (rev 2.0, PDF)](https://files.waveshare.com/wiki/Core1121/LR1121_H2_DS_v2_0.pdf)
- [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html)
