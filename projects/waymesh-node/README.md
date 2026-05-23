# waymesh-node

Phase 0 bring-up firmware for the **Waymesh** hybrid local + long-range mesh
research program. See the design docs in
[`docs/hybrid-mesh/`](../../docs/hybrid-mesh/README.md) (architecture, protocol,
RF coexistence, power, roadmap). This project implements **Phase 0** from
[`09-poc-roadmap.md`](../../docs/hybrid-mesh/09-poc-roadmap.md).

## Project Summary

- Name: `waymesh-node`
- Board: `esp32-c3-devkitm-1` (the **RadioMaster XR2 Nano** is an ESP32-C3)
- PlatformIO environment: `esp32c3_supermini`
- Target hardware: **RadioMaster XR2 Nano** = ESP32-C3 + Semtech LR1121,
  **2.4 GHz only** (integrated 2.4 GHz antenna, no sub-GHz path)

## Goals (Phase 0 — bench bring-up & baseline)

1. Bring up the **LR1121 over SPI** as a 2.4 GHz LoRa radio (via RadioLib).
2. **Loopback test:** two nodes exchange beacons and report PDR / RSSI / SNR.
3. Read a **GNSS** module on the spare UART and log fixes (TinyGPSPlus).
4. **LED heartbeat** + **structured CSV serial logs** for later analysis.

Non-goals here: ESP-NOW (Phase 1), aggregation, the super-frame. Kept minimal on
purpose.

## ⚠️ Not hardware-verified yet

This firmware is **compile-verified only** (built in a cloud session with no
board attached). Critically:

- **The pin map, RF-switch/TCXO config, and radio routing in
  `src/board_config.h` are PLACEHOLDERS.** They are not the real XR2 values.
- Before flashing, **extract the real pins from the open-source ExpressLRS
  hardware target for the RadioMaster XR2** (<https://github.com/ExpressLRS/targets>,
  RX layout JSON) and fill them into `board_config.h`. This is literally Phase 0,
  task #1.
- The LR1121 also needs the board-correct **RF-switch (DIO) table** and **TCXO
  voltage** to route TX/RX; until set, the radio may not transmit/receive
  correctly even if `begin()` succeeds.

If the SPI pins are wrong, `begin()` prints `begin_err=<code>` over serial and RF
is halted — that's the first thing to fix on the bench.

## How To Build and Flash

```bash
# from repo root, one-time: source ./scripts/setup.sh   (installs pio)
cd projects/waymesh-node
pio run                                            # build (compile check)
pio run -t upload --upload-port /dev/ttyACM0       # flash (after wiring/pins verified)
pio device monitor -p /dev/ttyACM0 -b 115200       # watch CSV logs
```

Flashing the XR2: reflash the ESP32-C3 via the ELRS WiFi bootloader (upload the
`.bin` from `.pio/build/esp32c3_supermini/`) or over UART. Powering: the XR2
expects ~5 V — feed 5 V (boost from a 1S LiPo) or tap the 3.3 V rail. See
`docs/HARDWARE.md`.

## How To Interact With This Project

- **Wiring:** GNSS module on the XR2 spare UART (`PIN_GNSS_RX/TX`, costs the CRSF
  interface). LR1121 is on-board (SPI). LED is on-board.
- **Runtime:** open the serial monitor at 115200. The node prints a banner, then
  one CSV line per event. Power up **two** nodes to see the loopback.
- **Tuning:** change `LORA_SF` / `LORA_BW_KHZ` / `LORA_POWER_DBM` in
  `board_config.h` to explore the range/airtime tradeoff.

## Expected Behavior

On boot (per node), over USB-CDC serial at 115200:

```
# waymesh-node Phase 0 (XR2: ESP32-C3 + LR1121, 2.4 GHz LoRa)
# nodeId=XXXXXXXX freq=2450.0MHz bw=812.5kHz sf=9 cr=4/5 pwr=10dBm
# WARNING: pins/RF-switch in board_config.h are PLACEHOLDERS ...
ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra
<t>,<id>,phase0,boot,node,<id>,,,,,,radio_ok
<t>,<id>,phase0,tx,lrp,<id>,0,,,,,beacon
<t>,<id>,phase0,rx,lrp,<peer>,5,-78.0,8.5,,,beacon      # appears with a 2nd node
<t>,<id>,phase0,gps_fix,node,,,,,57.700000,11.970000,   # appears with a GNSS fix
<t>,<id>,phase0,status,node,,,,,,,tx=10 rx=9 gaps=1 badcrc=0 pdr=90.0% sats=7
```

- A node alone: `tx` lines + `status` with `rx=0`. LED toggles each beacon.
- Two nodes in range: each shows `rx` lines from the other with RSSI/SNR, and a
  rolling `pdr` in `status`. That confirms the loopback link.
- With a GNSS fix: `gps_fix` lines and lat/lon populated in subsequent rows.

## Project Layout

- `src/main.cpp`: Phase 0 firmware (radio loopback + GNSS + logging)
- `src/board_config.h`: **all** board-specific config (pins, radio, behavior)
- `docs/HARDWARE.md`: XR2 pin map, flashing, powering, bring-up checklist
- `docs/TASK_LOG.md`: implementation log
- `include/ lib/ test/`: standard PlatformIO dirs
