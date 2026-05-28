# Task Log — t1000-e-meshtastic

## 2026-05-28 — initial scaffold, wired flash flaky

**Outcome**: New project directory for flashing upstream Meshtastic firmware
to a Seeed Studio SenseCAP T1000-E. Wired CLI flash works partially but is
flaky on Linux due to a USB-CDC re-enumeration bug in the bootloader.

**What landed**:
- `tools/flash.sh` — bootstraps `tools/.venv/` with `adafruit-nrfutil` and
  the `meshtastic` Python CLI, caches the Meshtastic release zip, validates
  the bootloader is enumerated as `239a:8029`, then runs
  `adafruit-nrfutil dfu serial --singlebank` at 115200 baud.
- `tools/flash.sh` also self-applies a small idempotent patch to the
  venv-installed `dfu_transport_serial.py` that closes + reopens the
  serial port after `send_start_dfu`'s erase-wait sleep, working around
  the kernel invalidating the fd when the bootloader briefly drops USB.
- `README.md`, `docs/HARDWARE.md` — usage, USB ID reference, DFU entry
  quirks, recommended BLE path.
- No `platformio.ini` — this project flashes prebuilt upstream firmware.

**Pinned stable**: `v2.7.15.567b8ea` (published 2025-11-18).

**Observations from this session**:
- T1000-E bootloader (`239a:8029 Adafruit T1000-E-BOOT`) exposes CDC ACM
  only, no UF2 MSC interface. Drag-and-drop flashing is not available.
- 1200bps reset on the user-firmware port (`2886:0057`) does NOT reboot
  to bootloader. The Seeed-customized firmware ignores the convention.
- 1200bps reset on the bootloader port DOES exit DFU back to user app.
- Even with the venv patch, wired DFU flashing remained flaky on this
  machine — either start_dfu times out, or succeeds and then the port
  drops during the erase wait. Each retry burns one bootloader entry
  (5x side-button click).
- The Meshtastic mobile app's BLE update path is far more reliable for
  this device. Recommended as the primary supported path.

**Next steps**:
- Capture a working `meshtastic --enter-dfu` session once we can get the
  device to answer the API consistently (may need a wake-button press
  immediately before the call).
- Test whether running the flash from a different USB port / hub
  (powered, USB 2.0) avoids the CDC drop.
- After successful flash, configure region + channels via Meshtastic
  mobile app on first boot.
