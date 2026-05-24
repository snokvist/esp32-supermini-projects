# Task Log - waymesh-node

Use this file as a running implementation log.

## Entries

### 2026-05-23 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `esp32-c3-devkitm-1`
- Env: `esp32c3_supermini`

### 2026-05-23 - Phase 0 bring-up firmware scaffolded

**What changed**
- Replaced the LED-blink template with Phase 0 bring-up firmware targeting the
  RadioMaster XR2 Nano (ESP32-C3 + LR1121, 2.4 GHz LoRa).
- `src/board_config.h`: centralized all board-specific config (LR1121 SPI pins,
  GNSS UART pins, LED, 2.4 GHz LoRa params, behavior timings). **All pin/RF
  values are clearly-marked PLACEHOLDERS** pending verification against the ELRS
  XR2 hardware target.
- `src/main.cpp`: LR1121 init via RadioLib; symmetric beacon/listen loopback with
  PDR/RSSI/SNR tracking; GNSS parse via TinyGPSPlus on the spare UART; LED
  heartbeat; structured CSV serial logs (schema per `docs/hybrid-mesh/10-...`).
- `platformio.ini`: added `jgromes/RadioLib` and `mikalhart/TinyGPSPlus`.
- README/HARDWARE updated with the bring-up checklist and powering/flashing notes.

**Validation**
- `pio run` build (compile check) — see commit/CI. No hardware attached in this
  session, so flash/observe (the rest of the Phase 0 verification loop) is
  pending bench access.

**Known limitations / next steps**
- Pin map + RF-switch (DIO) table + TCXO voltage are NOT the real XR2 values yet
  → fill from the ELRS XR2 target before flashing (Phase 0 task #1).
- PDR estimate assumes a single peer (seq-gap based); fine for a 2-node loopback,
  revisit for >2 nodes.
- Next: bench bring-up (verify pins, RF routing), 2-node loopback PDR at
  SF8/10/12 × BW406/812, GNSS TTFF, per-state current measurement → feed numbers
  back into `docs/hybrid-mesh/07-power-and-runtime.md`. Then Phase 1 (ESP-NOW).
