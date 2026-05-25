# waymesh-8285 — ESP8285 + SX1280 (Waymesh Tier 2/3)

Firmware for the 2.4 GHz LoRa nodes of the Waymesh heterogeneous mesh that run on
**ExpressLRS-class ESP8285 + Semtech SX1280** hardware:

- **Tier 2** — BayckRC 7PWM (`bayck_7pwm`): plain SX1280; beacon + relay + optional GPS
- **Tier 2/3** — BetaFPV Nano RX (`betafpv_nano`): SX1280 + external PA/LNA, **same
  firmware** — relay always on, GPS auto-detected if a module is wired

> The Nano was originally speced as a Tier-3 *dumb forward-only relay*; it instead
> runs the full Tier-2 GPS-or-relay build (the relay + GPS-autodetect logic is
> board-agnostic, so there was no reason to ship a reduced firmware). Its only
> hardware difference from the bayck is the external PA/LNA — see the pin map.

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

**Result — PoC #0 PASSED & device-verified (2026-05-24):** a BayckRC 7PWM
(`006D2929`) and the XR2 (`B17506DC`) exchange the byte-identical beacon at
**100% PDR both ways**, `badcrc=0`, RSSI ~−28 dBm / SNR ~12 dB; the 8285 surfaces
as a peer in a stock Meshtastic app via the XR2 gateway. Both "traps" below were
non-issues on this board: `0x12` matched across families on the first flash, and
**BUSY=GPIO5 / RST=GPIO2 are physically wired** (no `-707`). The managed-flood
relay (below) is built on top.

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
pio run -e bayck_7pwm        # BayckRC 7PWM (plain SX1280)
pio run -e betafpv_nano      # BetaFPV Nano RX (SX1280 + PA/LNA)
```

ESP8266 produces a single image. Flash over the UART pads (GPIO1 TX / GPIO3 RX,
GND, 3V3) with the board in download mode (hold GPIO0 low + power-cycle). Both
boards use the identical procedure — only the `firmware.bin` path differs:

```bash
esptool.py --chip esp8266 --port /dev/ttyUSB0 --baud 460800 \
  --before no_reset --after no_reset \
  write_flash 0x0 .pio/build/betafpv_nano/firmware.bin   # or .../bayck_7pwm/...
```

These bare RX boards have no DTR/RTS auto-reset, so `--before no_reset` (enter
download mode by hand: hold GPIO0 low + power-cycle, then release). **After
flashing, do a clean power-cycle — not `--after soft_reset`:** a soft reset leaves
the SX1280 latched and `begin()` returns **-2 (CHIP_NOT_FOUND)**. The radio comes
up fine on a real power-on. (The bootloader probe is finicky; retry the connect a
couple of times.)

Serial logs are CSV on UART0 @115200 (same schema as `waymesh-node`):
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
pio run -e betafpv_nano_gpstest   # Nano bring-up
pio run -e betafpv_nano           # Nano production
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

## Managed-flood relay (Tier 2/3)

Built into this firmware behind `-DWAYMESH_RELAY=1` (set in the `bayck_7pwm` env;
unset it to reproduce the pure PoC #0 build). The relay re-floods beacons it hasn't
seen so a beacon reaches nodes outside the originator's range — the forwarding
primitive shared by Tier 2 and the Tier-3 dumb relay. Spec:
`../../docs/hybrid-mesh/05-protocol.md` §"Suppression & dedup state". It is
silicon-identical on the BetaFPV Nano, which runs the same relay from its own
`betafpv_nano` env (pin map confirmed against ELRS "Generic 2400 PA" — below).

Mechanisms (`board_config.h` tunables):

- **Seen-set dedup** — a small ring of recent `MessageID = (srcId, seq)`; a node
  relays each MessageID **at most once** and ignores its own `srcId`, so the flood
  terminates. Low-mem (a few hundred bytes) for the Tier-3 target.
- **SNR-proportional delay + jitter** — a weaker-SNR receiver waits longer, so the
  best-placed relay transmits first.
- **Overhear suppression** — hearing the same MessageID again cancels a pending
  rebroadcast.
- **Verbatim re-flood** — the rebroadcast keeps the original `srcId/seq`, so the
  gateway still attributes presence to the true originator and downstream nodes
  dedup correctly.
- **No hop-limit** — the as-built beacon carries no hop field; the seen-set alone
  bounds the flood. A decrementing hop-limit arrives with the LRP-header migration.

Status counters (the `status` log line): `relay=` rebroadcasts sent, `supp=`
pending rebroadcasts cancelled by overhear, `qfull=` new messages dropped on a
full pending queue.

**Device-verified (2026-05-24), XR2 + one BayckRC relay:**

- *Forward + dedup* (relay side): every received XR2 beacon is relayed **exactly
  once** — `relay == rx` (67/67), `gaps=0 badcrc=0 supp=0 qfull=0`, relay fired
  ~55–72 ms after RX.
- *Round-trip* (witness side, via `waymesh-node` `esp32c3_xr2_relaytest`): the XR2
  hears its own beacons relayed back at **100% relay PDR** (`relayback == tx`).
- *Not yet on hardware:* overhear suppression and multi-hop reach-extension need a
  2nd relay / 3rd node (`supp`/`qfull` correctly stayed 0 on the 2-node bench).

## Pin map

Both boards are ESP8285 + SX1280 and share the radio/LED/UART pins below (the
SPI lines are the ESP8266 fixed HW-SPI pins). They differ only in the front-end.

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| SX1280 SCK | 14 (fixed HW-SPI) | | SX1280 NSS | 15 |
| SX1280 MISO | 12 (fixed HW-SPI) | | SX1280 DIO1 | 4 |
| SX1280 MOSI | 13 (fixed HW-SPI) | | SX1280 BUSY | 5 |
| LED | 16 | | SX1280 RST | 2 |
| GPS / debug (UART0) | TX 1 / RX 3 | | | |

**BayckRC 7PWM** (ELRS "Generic 2400 PWMP7"): no PA. That layout sacrifices the
MCU BUSY/RST GPIOs for PWM, so `board_config.h` *borrows* GPIO5/GPIO2 for BUSY/RST
(see the traps above — fall back to `RADIOLIB_NC` if `begin()` returns `-707`).
7 PWM outputs (Tier 2, unused here): GPIO `[0,1,3,9,10,5,2]`.

**BetaFPV Nano RX** (ELRS "Generic 2400 PA"): adds an external **PA/LNA** the
SX1280 gates via **RXEN = GPIO9, TXEN = GPIO10** (`PIN_LORA_RXEN`/`PIN_LORA_TXEN`,
set only for `WAYMESH_BOARD_BETAFPV_NANO`; RadioLib drives them — IDLE `{L,L}`,
RX `{H,L}`, TX `{L,H}`). Without this the PA never powers and the link collapses.
Here BUSY=GPIO5 / RST=GPIO2 are **physically wired** by the board, so the `-707`
trap does not apply. GPIO9/10 are the SDIO pins the ESP8285's DIO-mode embedded
flash frees. (Confirmed against ELRS `targets` `RX/Generic 2400 PA.json`.)
