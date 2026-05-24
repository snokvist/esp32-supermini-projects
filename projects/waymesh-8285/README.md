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

Serial logs are CSV on UART0 @115200 (same schema as `waybeam-node`):
`ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra` (role `poc0`).

## GPS (Tier 2, `-DWAYMESH_GPS=1`)

The ESP8285 has **no spare UART** — UART1 is TX-only and its pin is the SX1280
`RST`, and `Serial.swap()`'s alternate pins are the SX1280 SPI. So **UART0
(GPIO1 TX / GPIO3 RX) is time-shared** between the debug console and the GPS:

```
boot ─► DEBUG @115200 (CSV console, RX ignored)
        └─ after GPS_GRACE_MS (25 s) ─► PROBE: listen GPS_PROBE_MS (4 s) for valid NMEA
              ├─ NMEA seen  ─► GPS: parse, fill the v1 beacon position, go silent*
              └─ silence    ─► back to DEBUG (stays a console on a GPS-less board)
```

\*The production `bayck_7pwm` build goes silent on UART0 once locked (the line is
the GPS's). The **`bayck_gpstest`** build keeps the CSV plus a 1 Hz
`gps,...,sats=,chars=,lat=,lon=` line so you can verify wiring/lock on the bench
**without a sky fix**:

```bash
pio run -e bayck_gpstest          # bring-up (observable in GPS mode)
pio run -e bayck_7pwm             # production (silent once GPS-locked)
```

The grace window is the maintenance window: power on → ~25 s of readable logs →
it decides. Once locked, power-cycle back into the grace window to debug again
(with auto-detect, just unplug the GPS first).

**Vendor-neutral by design.** The firmware parses standard NMEA (TinyGPSPlus,
`$GP/$GN/$GA/$GB/$GL` `RMC`/`GGA`) from *any* module and **emits no config
bytes** — so wiring an arbitrary GPS can't trigger the wrong vendor's commands.
u-blox modules are tuned once, out-of-band, with `tools/gps_provision.py`
(1 Hz + RMC/GGA-only, ~7× less UART traffic); it polls UBX `MON-VER` first and
**skips anything that isn't u-blox**. The config persists in the module's flash.

**Wiring & flashing caveats:**

- GPS **TX → GPIO3 (8285 RX)** is all the firmware needs (it only reads). GND +
  VCC (3.3–5 V) as usual. (Wire GPS RX ← GPIO1 only if you want to re-provision
  in place; not required.)
- **Flash with the GPS unplugged.** A GPS streaming NMEA into GPIO3 at power-on
  can jam the ROM downloader and floods the debug input.
- **GPS owns GPIO1/3 — PWM channels 2 & 3** in the 7-PWM list. Fine for a GPS
  tracker; it can't also drive all 7 servos.

Tunables (`board_config.h`): `GPS_GRACE_MS`, `GPS_PROBE_MS`, `GPS_BAUD` (default
115200 = debug baud, so the switch is software-only), `GPS_PROBE_MIN_SENTENCES`,
`GPS_FIX_MAX_AGE_MS`.

## Pin map (ELRS "Generic 2400 PWMP7")

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| SX1280 SCK | 14 (fixed HW-SPI) | | SX1280 NSS | 15 |
| SX1280 MISO | 12 (fixed HW-SPI) | | SX1280 DIO1 | 4 |
| SX1280 MOSI | 13 (fixed HW-SPI) | | SX1280 BUSY | 5 (see traps) |
| LED | 16 | | SX1280 RST | 2 (see traps) |

7 PWM outputs (Tier 2, not used in PoC #0): GPIO `[0,1,3,9,10,5,2]`.
