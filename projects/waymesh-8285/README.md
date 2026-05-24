# waymesh-8285 — ESP8285 + SX1280 (Waymesh Tier 2/3)

Firmware for the 2.4 GHz LoRa nodes of the Waymesh heterogeneous mesh that run on
**ExpressLRS-class ESP8285 + Semtech SX1280** hardware:

- **Tier 2** — BayckRC 7PWM (GPS via remapped PWM-pin UART, beacon + relay)
- **Tier 3** — BetaFPV Nano RX (dumb forward-only relay, no GPS/BLE)

The Tier-1 smart gateway (ESP32-C3 + LR1121, RadioMaster XR2) lives in
`../waymesh-node`. Architecture: `../../docs/hybrid-mesh/` (02 tiers + interop
crux, 09 Phase H roadmap, 05 beacon wire format).

## PoC #0 — LR1121 ↔ SX1280 interop (the gate)

`src/main.cpp` is **PoC #0**: the heterogeneous mesh only works if the SX1280 on
these boards talks on-air to the LR1121 on the XR2. This firmware mirrors the
XR2's Phase 0 loopback **exactly** — byte-identical beacon (`Beacon` struct,
magic `0x57`) and matched radio params (2450 MHz, BW 812.5 kHz, SF9, CR 4/5,
preamble 8, sync `0x12` PRIVATE) — so a successful PoC means:

1. this node's serial logs `rx=` climbing (it hears the XR2), and
2. the XR2 logs `rx=` climbing and surfaces this node as a **peer in an
   unmodified Meshtastic app** via its BLE gateway (Phase G 1b).

PoC #0 carries no GPS/PWM — it is pure radio interop. Tier 2/3 behaviour is built
on top once interop is proven.

### Interop traps (documented, watch on first bring-up)

- **BUSY/RST pins** — the ELRS "Generic 2400 PWMP7" layout sacrifices the MCU
  BUSY/RST GPIOs to free PWM outputs. PoC #0 uses no PWM, so `board_config.h`
  wires them (busy=GPIO5, rst=GPIO2, the PWMP5/6 assignment) to give RadioLib a
  real BUSY line. If `begin()` returns **-707 (SPI_CMD_TIMEOUT)**, the board
  follows the PWMP7 "no BUSY" wiring → set `PIN_LORA_BUSY`/`PIN_LORA_RST` to
  `RADIOLIB_NC` and rebuild.
- **Sync word** — SX128x `begin()` takes no sync-word arg (unlike LR11x0/SX126x),
  so it is set explicitly. Whether `0x12` produces the same on-air sync across
  the two chip families is the open interop question; if `begin()` is OK but
  `rx=0` with the XR2 beaconing nearby, sweep `LORA_SYNC_WORD` first.
- **Coding rate** — forced to standard interleaving (`setCodingRate(cr, false)`);
  the SX1280-only long-interleave CR variants are unreadable by the LR1121.

## Build & flash

```bash
pio run -e bayck_7pwm
```

ESP8266 produces a single image. Flash over the UART pads (GPIO1 TX / GPIO3 RX,
GND, 3V3) with the board in download mode (hold GPIO0 low + power-cycle):

```bash
esptool.py --chip esp8266 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 .pio/build/bayck_7pwm/firmware.bin
```

Serial logs are CSV on UART0 @115200 (same schema as `waymesh-node`):
`ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra` (role `poc0`).

## Pin map (ELRS "Generic 2400 PWMP7")

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| SX1280 SCK | 14 (fixed HW-SPI) | | SX1280 NSS | 15 |
| SX1280 MISO | 12 (fixed HW-SPI) | | SX1280 DIO1 | 4 |
| SX1280 MOSI | 13 (fixed HW-SPI) | | SX1280 BUSY | 5 (see traps) |
| LED | 16 | | SX1280 RST | 2 (see traps) |

7 PWM outputs (Tier 2, not used in PoC #0): GPIO `[0,1,3,9,10,5,2]`.
