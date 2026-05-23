# 02 — Hardware & RF Platform

The physical reality the architecture must respect. All current figures are
datasheet-derived and **must be bench-verified** in Phase 0 — they are design
inputs, not measurements.

## Node block diagram

```
                +--------------------------------------------------+
                |                  ESP32-C3 SuperMini              |
                |   RISC-V @160 MHz · WiFi+BLE 2.4 GHz · 400 KB SR |
                |                                                  |
   2.4 GHz <----+ [WiFi/BLE radio] ESP-NOW (local plane) + BLE GW |
                |                                                  |
                |   SPI  ----------------+    UART ----------+     |
                +-------------+----------+--------+----------+-----+
                              |                   |
                       +------v------+     +------v------+
                       |   LR1121    |     |  GNSS (u-blox|
                       | LoRa multi- |     |  MAX-M10S)   |
                       | band radio  |     |  ~25 mA trk  |
                       +------+------+     +-------------+
                              |
              sub-GHz 868/915 |  (and optional 2.4 GHz LoRa)
                              v
                    long-range plane
```

Power: single LiPo (250–2000 mAh) → 3.3 V regulator. Battery sense on an ADC
pin. Optional load switch on the GNSS rail so it can be fully powered down.

## ESP32-C3 SuperMini

- Single-core RISC-V @160 MHz, 400 KB SRAM, integrated 2.4 GHz WiFi (b/g/n) and
  BLE 5.0. USB-CDC on boot per repo convention
  (`-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`).
- **One 2.4 GHz radio, time-shared** between WiFi (ESP-NOW) and BLE. They cannot
  both be truly simultaneous; the stack coexists by time-slicing. This is a
  scheduling constraint, not a feature — see [06](06-rf-coexistence.md).
- Pin budget is tight on the SuperMini. SPI for LR1121 (SCK/MISO/MOSI/NSS) +
  BUSY + DIO/IRQ + RESET = ~7 pins; GNSS UART = 2 pins; battery ADC = 1; LED.
  A concrete pin map is a Phase-0 deliverable in `docs/HARDWARE.md`.

### ESP32-C3 current (datasheet-class estimates)

| State | Current | Note |
|-------|---------|------|
| Deep sleep (RTC only) | ~5 µA | RAM not retained unless configured |
| Light sleep | ~130 µA | fast (<1 ms) wake, RAM retained |
| Modem-sleep (CPU on, RF off) | ~20–30 mA | depends on CPU freq |
| WiFi RX (listening) | ~95–100 mA | **the power villain** |
| WiFi TX | ~180–240 mA peak | brief bursts |
| BLE TX/RX | ~tens of mA | advertising can be duty-cycled cheaply |

Takeaway: **keeping the WiFi radio in RX is the dominant drain.** ESP-NOW must
be used in scheduled bursts, never continuous listen, on a battery node.

## LR1121 long-range radio

Semtech 3rd-gen ultra-low-power LoRa transceiver.

- **Bands:** 150–960 MHz (sub-GHz), 2.4 GHz, plus S-band (~2 GHz) and L-band
  (1.55 GHz) for satellite. This program uses **sub-GHz (868/915) as the primary
  long-range plane**; 2.4 GHz LoRa is an *optional* medium-range mode that
  contends with ESP-NOW/BLE (see coexistence).
- **Modulations:** LoRa, (G)FSK, **LR-FHSS** (LR-FHSS is **TX-only** on this
  part — useful for robust uplink digests, not for symmetric links).
- **PA paths:** sub-GHz +22 dBm (HP) / +15 dBm (LP); 2.4 GHz +13 dBm (HF).
- **No GNSS or WiFi passive scanning.** The LR1110/LR1120 can sniff GNSS/WiFi
  for cloud geolocation; the LR1121 trades that for S/L-band. **Consequence: we
  need a dedicated GNSS receiver for position.** Don't design around LR1121
  geolocation — it isn't there.
- Interface: SPI + BUSY + IRQ/DIO + NRESET. Has its own command/state machine;
  the host must respect BUSY handshaking.

### LR1121 current (datasheet-class estimates)

| State | Current | Note |
|-------|---------|------|
| Sleep (retention) | ~1.4 µA | config retained |
| Sleep (no retention) | ~0.4 µA | cold |
| Standby RC / XOSC | ~1.8 / ~1.9 µA | |
| RX sub-GHz | ~5.9 mA | **cheap to listen** |
| RX 2.4 GHz | ~6.1 mA | |
| TX +10 dBm (sub-GHz) | ~25 mA | |
| TX +14 dBm (sub-GHz) | ~40 mA | |
| TX +22 dBm (sub-GHz) | ~90 mA | regulatory-limited duty cycle |
| TX +13 dBm (2.4 GHz) | ~50 mA | |

Takeaway: **LoRa RX (~6 mA) is ~16× cheaper than WiFi RX (~95 mA).** This single
fact drives the whole architecture: LoRa is the always-listening plane.

## GNSS

- Reference: u-blox MAX-M10S class. Acquisition/tracking ~25–30 mA continuous;
  power-save / on-off duty cycling brings the average down dramatically.
- For a *mobile tracker*, a fix every N seconds with the receiver asleep between
  is the right model; cold-start TTFF is the cost to budget.
- Put the GNSS rail behind a load switch so a sleeping node draws ~0 from it.
- Position is shared as compact deltas, not full NMEA — see [05](05-protocol.md).

## RF front-end & antennas

- **Sub-GHz (868/915) and 2.4 GHz are naturally band-isolated** — this is the
  cleanest split and the reason sub-GHz is the long-range plane. Cross-band
  coupling is mostly via supply noise, shared ground, and antenna proximity.
- **Two 2.4 GHz emitters on one tiny board** (C3 WiFi/BLE + optional LR1121
  2.4 GHz LoRa) is the hard case: a +13 dBm 2.4 GHz LoRa TX next to a WiFi RX
  front-end will desensitize/block it. If 2.4 GHz LoRa is used at all, it must
  be **time-division multiplexed** with WiFi/BLE — never concurrent.
- Antenna strategy: separate antennas for sub-GHz vs 2.4 GHz; maximize physical
  separation and consider orthogonal orientation. Quantify isolation in
  [06](06-rf-coexistence.md).

## Battery & power tree

- LiPo cell 250 / 500 / 1000 / 2000 mAh covering wearable → vehicle.
- Runtime is **RX-dominated**; see the full model and tables in
  [07](07-power-and-runtime.md).
- Battery voltage on an ADC for low-power graceful degradation (slow beacons,
  shed optional traffic as voltage drops).

## What hardware constrains in the architecture

1. **WiFi RX is expensive → ESP-NOW is burst-only, scheduled.**
2. **LoRa RX is cheap → LoRa is the always-on wake/control plane.**
3. **One 2.4 GHz radio on the C3 → WiFi and BLE are mutually time-sliced.**
4. **No LR1121 geolocation → dedicated GNSS, duty-cycled behind a load switch.**
5. **Sub-GHz/2.4 GHz are clean to coexist; 2.4-LoRa/WiFi are not.**
6. **LR-FHSS is TX-only → it's an uplink-robustness tool, not a link mode.**
