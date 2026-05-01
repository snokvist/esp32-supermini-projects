# Claude/Agent guide for tx-backpack-usb

Standalone fork of the `ESP32C3_TX_Backpack_via_USB` env from
[`snokvist/Backpack`](https://github.com/snokvist/Backpack). See `README.md`
for the full feature set and protocols.

## The flash trap that wastes hours

> **NEVER** `esptool.py write_flash -z 0x0 firmware.bin`.

`firmware.bin` is the **app image**, not a merged image. `0x0` is the
bootloader region. Flashing the app there overwrites the bootloader → ROM
watchdog fires → ~3-second-period boot loop. The flash itself succeeds
(`Hash of data verified`); the chip is silently bricked from a software
perspective. Always use the multi-blob recipe in `README.md` or
`pio run -t upload`.

## Logging quirk

`lib/logging/logging.cpp:debugPrintf` only handles `%s %d %u %x` —
width/padding flags silently misformat. Use plain conversions plus your own
separator.

## USB-CDC stuck state after flash

Even on a correct flash, the C3's HWCDC channel often comes up half-broken:
the device enumerates (`/dev/ttyACM0` exists) but `pyserial` opens then
immediately returns "device disconnected". Fix: physical unplug + replug.
RTS-driven `--after hard_reset` does **not** clear it.

## Diagnostic table

| Symptom | Likely cause |
|---|---|
| `lsusb` device# climbs every ~3s | Boot loop. Check the flash command first — almost always the offset bug above. |
| Stable enumeration but reads fail with "device disconnected" | HWCDC stuck. Physical replug. |
| Bouncy on phone OTG only | Phone supplying marginal OTG current. Usually not chip-side. |
| Stable on dev box but not on phone | Different problem — investigate phone-side USB stack, not firmware. |

Before blaming hardware (cables, brownout, USB connector), confirm both
firmwares (current and prior known-good) misbehave **on the same chip with
the same flash recipe**. If only one boot-loops, it's a real firmware
regression.

## Backporting from upstream

Recipe is in `README.md`. Touch-points to expect conflicts in:

- `src/Tx_main.cpp` — sniffer ISR, MSP_WAYBEAM handlers, wired-CRSF pump,
  OLED loop, passthrough early branch.
- `lib/MSP/msptypes.h` — Waybeam opcodes 0x42-0x45.
- `lib/BUTTON/devButton.cpp` — release-based gesture dispatch (~30 lines diff
  vs. upstream's mid-press long-hold callback).
- `lib/config/config.{h,cpp}` — `oled_dual` / `crsf_pass` NVS keys.

## Conventions

- Branches: `feature/<slug>` for hand-authored, `claude/<slug>` for
  Claude-authored.
- Never commit `user_defines.txt` (it's `.gitignore`'d) — the `.example` file
  is the checked-in template.
- All Waybeam-specific PRs go to `snokvist/esp32-supermini-projects`. Upstream
  ELRS fixes get backported via the README recipe.

## Coordination repo links

This project is registered in the parent
[`waybeam-coordination`](https://github.com/snokvist/waybeam-coordination)
repo. The cross-repo workflow doc lives at `docs/codex-workflow.md` there.
