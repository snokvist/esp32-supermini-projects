# t1000-e-meshtastic

Flashing recipe for the **Seeed Studio SenseCAP T1000-E** Meshtastic tracker
(nRF52840 + LR1110, GPS, OLED, RGB LED). Pulls upstream Meshtastic stable
firmware and pushes it via Nordic DFU over serial.

> Unlike other projects under `projects/`, this directory does **not** ship a
> PlatformIO build. The T1000-E runs upstream Meshtastic firmware we pull as
> prebuilt binaries from GitHub Releases. There is no `platformio.ini` /
> `src/main.cpp` here on purpose.

## Hardware

- Seeed Studio SenseCAP T1000-E (nRF52840 SoC, LR1110 multi-band LoRa + GNSS)
- Meshtastic firmware target: `tracker-t1000-e`
- USB VID:PID identities:
  - **User firmware**: `2886:0057 Seeed Technology Co., Ltd. T1000-E`
  - **DFU bootloader**: `239a:8029 Adafruit T1000-E-BOOT`
- Default serial port (Linux): `/dev/ttyACM0`

The bootloader is **serial DFU only** — it exposes CDC ACM with no UF2
mass-storage interface, so drag-and-drop flashing is not available.
Flashing goes through `adafruit-nrfutil` with a Nordic DFU `.zip`.

See [`docs/HARDWARE.md`](docs/HARDWARE.md) for bring-up checks.

## Recommended path: Meshtastic mobile app over BLE

The wired CLI flash path (see below) hits a USB-stack bug on Linux where
the bootloader briefly drops its CDC interface during the post-start
flash erase, killing the DFU session. We've patched the venv-installed
`adafruit-nrfutil` to reopen the port, but it's still finicky and each
retry consumes a fresh bootloader entry. The mobile app uses BLE and
the bootloader's BLE DFU profile, which doesn't have this problem.

1. Install the Meshtastic mobile app (iOS / Android).
2. Pair the T1000-E.
3. App settings → Update firmware → pick the latest stable.

## Wired CLI path (may need retries)

```bash
# 1) Put the T1000-E into DFU bootloader: side button x5 rapidly (within ~1 s).
#    Confirm with: lsusb | grep 239a   ->   239a:8029 Adafruit T1000-E-BOOT

# 2) Immediately flash (bootloader has a ~30 s active window):
./tools/flash.sh                    # latest pinned stable
./tools/flash.sh --version v2.7.15.567b8ea --port /dev/ttyACM0
```

The script auto-applies a small patch to the venv-installed
`adafruit-nrfutil` (`tools/.venv/.../dfu_transport_serial.py`) that
closes + reopens the serial port after `send_start_dfu`'s flash-erase
sleep — works around the kernel invalidating the fd when the bootloader
briefly drops USB.

If you keep landing back in user firmware, the click pattern isn't being
recognized — try again with shorter intervals between presses. T1000-E
firmware does NOT support the Arduino-style 1200bps-reset → bootloader
trick, so `--force-bootloader` won't help once user firmware is running.

## What the script does

1. Bootstraps `tools/.venv/` (one-time, ~30 s) and installs
   `adafruit-nrfutil` (+ `meshtastic` Python CLI as a side benefit).
2. Downloads `firmware-nrf52840-<version>.zip` from `meshtastic/firmware`
   into `tools/.cache/` (cached across runs).
3. Extracts `firmware-tracker-t1000-e-<version>-ota.zip` (Nordic DFU
   package: `manifest.json` + `firmware.dat` + `firmware.bin`).
4. Verifies the bootloader VID:PID is enumerated (`239a:8029`).
5. Runs `adafruit-nrfutil dfu serial --singlebank` over the bootloader's
   CDC port at 115200 baud.

The bootloader has a ~30 s active-DFU window when a valid app is
installed; the venv bootstrap is one-time so the active flash starts in
<2 s and easily fits inside that window on subsequent runs.

## DFU entry quirks observed on this unit

- 1200bps reset on the **user-firmware** port (`2886:0057`) does NOT
  reboot to bootloader. The Seeed firmware ignores the convention.
- 1200bps reset on the **bootloader** port (`239a:8029`) actually exits
  DFU and boots user firmware. Don't do it.
- A single button press is interpreted as a soft reset by user firmware
  (device disappears ~6 s, comes back as `2886:0057`).
- `meshtastic --enter-dfu` will time out if the tracker is asleep or
  isn't currently servicing the USB Meshtastic API.

## Bumping the pinned stable

```bash
gh release view --repo meshtastic/firmware --json tagName -q .tagName
# Edit tools/flash.sh -> VERSION_DEFAULT="<new tag>"
```
