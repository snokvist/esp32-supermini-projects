# tx-backpack-usb

Standalone PlatformIO firmware for the ESP32-C3 SuperMini acting as a USB-CDC
sniffer + injector for an ELRS TX backpack ESP-NOW link. Companion app:
[`Waybeam-backpack-android`](https://github.com/snokvist/Waybeam-backpack-android).

Extracted from [`snokvist/Backpack`](https://github.com/snokvist/Backpack)
(env `ESP32C3_TX_Backpack_via_USB`) at base commit `4b2fd92`. See
`docs/extraction_plan.md` in that repo for the inventory and rationale.

## Build

```sh
cp user_defines.txt.example user_defines.txt
# edit user_defines.txt to set MY_BINDING_PHRASE
pio run
```

Output at `.pio/build/tx-backpack-usb/`.

## Flash — read this before flashing

> **Never** `esptool.py write_flash -z 0x0 firmware.bin`.

PIO's `firmware.bin` for the ESP32 family is the **app image**, not a merged
image. Flashing it at offset `0x0` overwrites the bootloader → ROM watchdog
fires → ~3-second-period boot loop where `lsusb` shows the bus device number
climbing every cycle. The flash itself succeeds (`Hash of data verified`); the
chip is silently bricked from a software perspective.

### Correct multi-blob flash

```sh
BD=.pio/build/tx-backpack-usb
pio pkg exec -- esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 460800 \
  write_flash \
    0x0000  "$BD/bootloader.bin" \
    0x8000  "$BD/partitions.bin" \
    0xe000  "$BD/boot_app0.bin" \
    0x10000 "$BD/firmware.bin"
```

Or use PIO directly (does the same thing):

```sh
pio run --target upload --upload-port /dev/ttyACM0
```

### Re-flashing over a running USB-CDC firmware

`pio run -t upload` deadlocks against firmware that already owns the CDC
endpoint (esptool stub baud-switch fails). Park the chip in ROM download mode
first:

1. Hold BOOT (GPIO9 to GND).
2. Plug USB while holding BOOT.
3. Keep BOOT held during the entire flash.
4. Add `--before no_reset` to the esptool command above.
5. After "Hard resetting via RTS pin..." → physical unplug + replug to clear
   HWCDC stuck state before the host can re-open the port.

## BOOT button gestures

| Gesture | Action |
|---|---|
| Short press (<500 ms) | Reboot into WiFi update mode |
| Long-press 500 ms .. ~3 s, release | Flip OLED layout MONO ↔ DUAL, persist to NVS |
| Long-press ≥ ~3 s, release | Toggle CRSF passthrough mode + reboot |

GPIO 9 is the C3's strap pin, so a "hold-during-plug" toggle is **not**
possible — that path puts the chip in ROM download mode and the firmware never
runs. Always toggle after boot.

## Pin map

| Pin | Function |
|---|---|
| GPIO 4  | OLED SDA (I²C) |
| GPIO 5  | OLED SCL (I²C) |
| GPIO 8  | Status LED |
| GPIO 9  | BOOT button (also strap pin) |
| GPIO 20 | Wired CRSF RX (from receiver TX pad) |
| GPIO 21 | Wired CRSF TX (to receiver RX pad) |
| USB-D±  | USB-CDC to host |

OLED is optional — if no SSD1306 is soldered the dashboard silently disables.

Either wired-CRSF pin can be left disconnected; the firmware never asserts a
pin that isn't used.

## USB-CDC protocol summary

The host speaks MSP V2 over USB-CDC. Payloads:

| MSP function | Direction | Payload |
|---|---|---|
| `MSP_WAYBEAM_SNIFFER_CTRL` (`0x0042`) | host → device | sniffer mode (off / bound / promiscuous) |
| `MSP_WAYBEAM_SNIFFED_CRSF` (`0x0043`) | device → host | `src_mac` (6) + `rssi_dbm` (i8) + `channel` (u8) + raw vendor payload |
| `MSP_WAYBEAM_WIRED_CRSF` (`0x0044`) | device → host | full CRSF frame from UART1 verbatim |
| `MSP_WAYBEAM_INJECT_CRSF` (`0x0045`) | host → device | full CRSF frame; firmware re-validates CRC before emitting on UART1 |

Sniffer mode defaults to OFF at boot — no `Serial.write` from the sniffer path
until the host opts in. The runtime gate exists because **any device-side
`Serial.write` during a host MSP transaction stucks the C3 USB-CDC**. Throttling
alone isn't enough — the gate must auto-pause around request/response
round-trips.

The wired-CRSF bridge is **always-on at boot** (no opt-in). Reuses
`GENERIC_CRC8(0xD5)` from `lib/CRC` — same polynomial as the canonical CRSF
DVB-S2 table. Frames are forwarded verbatim; the 4-implementation drift list
(coordination repo `/audit-protocols`) stays at 4.

## CRSF passthrough mode

`crsf_pass=1` in NVS makes the firmware boot into a transparent USB-CDC ↔
UART1 bridge: `/dev/ttyACM0` ⟷ GPIO 20 RX / GPIO 21 TX at 420 000 8N1. The host
sees "an ELRS receiver" — useful for tools that speak raw CRSF (Configurator,
ELRS Lua, `crsf_config`).

Toggle with the ≥3 s BOOT gesture above. The same gesture flips back.

## OLED dashboard

Optional 128 × 64 SSD1306, addr `0x3C`. Layout refreshed every 200 ms:

```
┌──────────────────────────────────────┐
│ Waybeam BP-USB              123KB    │  header (free heap)
├──────────────────────────────────────┤
│ ESP 49.5Hz ch11 BND                  │  ESP-NOW rx rate, channel, sniffer mode
│ p aabbccddeeff                       │  bound peer MAC
├──────────────────────────────────────┤
│ WIR 49.8Hz t16 e0                    │  wired CRSF rx rate, last frame type, error count
│ 1500 1500 1000 1500                  │  RC ch1-4 in microseconds (when type=0x16 fresh)
├──────────────────────────────────────┤
│ HST in 1234 out 0                    │  host bytes in/out
│ inj 87  00:14:31                     │  inject count, uptime
└──────────────────────────────────────┘
```

The MONO / DUAL toggle exists because there's no electrical difference between
a mono SSD1306 and one with a yellow band — the colour is purely a property of
the glass. The firmware can't autodetect; the layout is selected at boot from
NVS (`Preferences` namespace `waybeam_bp`, key `oled_dual`).

## Backporting upstream changes

When `ExpressLRS/Backpack` lands a TX-backpack fix:

```sh
# In an upstream checkout
git diff <prev-base>..<new-base> -- \
  src/Tx_main.cpp src/module_*.cpp src/module_*.h \
  src/options.cpp src/EspFlashStream.* \
  lib/MSP lib/CRC lib/BUTTON lib/DEVICE lib/EEPROM lib/LED \
  lib/Channels lib/CrsfProtocol lib/MAVLink lib/WIFI lib/config lib/logging \
  include/
```

Apply the diff to this repo, resolving conflicts against the Waybeam touch
points:

- `src/Tx_main.cpp` — sniffer ISR, MSP_WAYBEAM handlers, wired-CRSF pump,
  OLED loop, passthrough early branch (all bracketed by `#if defined(USB_*)`
  guards).
- `lib/MSP/msptypes.h` — opcodes `MSP_WAYBEAM_SNIFFER_CTRL` (0x42),
  `MSP_WAYBEAM_SNIFFED_CRSF` (0x43), `MSP_WAYBEAM_WIRED_CRSF` (0x44),
  `MSP_WAYBEAM_INJECT_CRSF` (0x45).
- `lib/BUTTON/devButton.cpp` — release-based gesture dispatch.
- `lib/config/config.{h,cpp}` — `oled_dual` and `crsf_pass` NVS keys.

Bump the base commit reference at the top of this README on each backport.

## License

Inherits the upstream `ExpressLRS/Backpack` GPL-3.0 license. See the parent
repo (`esp32-supermini-projects/LICENSE.md`) for project-wide terms.
