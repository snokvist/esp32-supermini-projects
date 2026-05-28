# tbeam-supreme-meshtastic

Flashing recipe for the LilyGO **T-Beam Supreme** (ESP32-S3 + LoRa + GPS +
OLED) running upstream **Meshtastic** stable firmware.

> Unlike other projects under `projects/`, this directory does **not** ship a
> PlatformIO build. The T-Beam Supreme runs the official Meshtastic firmware
> we pull as prebuilt binaries from the upstream GitHub release. There is no
> `platformio.ini` / `src/main.cpp` here on purpose.

## Hardware

- LilyGO T-Beam Supreme (ESP32-S3, 8 MB flash, BigDB partition layout)
- Meshtastic target: `tbeam-s3-core` (covers both SX1262 and LR1121 radio
  variants — modern Meshtastic firmware autodetects the radio at boot)
- Enumerates over native USB-CDC as `303a:1001 Espressif Systems LilyGo
  TBeam-S3-Core`, default port `/dev/ttyACM0`

See [`docs/HARDWARE.md`](docs/HARDWARE.md) for pin map and bring-up checks.

## Quick start

```bash
./tools/flash.sh                       # factory flash latest pinned stable (wipes config)
./tools/flash.sh --update              # OTA-style flash, preserves channels/prefs
./tools/flash.sh --version v2.7.15.567b8ea --port /dev/ttyACM1
```

The pinned default is whatever `VERSION_DEFAULT` is set to in
`tools/flash.sh`. Bump that line to track a newer stable release.

## What the script does

1. Downloads `firmware-esp32s3-<version>.zip` from
   `meshtastic/firmware` GitHub Releases into `tools/.cache/` (cached
   across runs).
2. Extracts the three files needed for `tbeam-s3-core`:
   - `firmware-tbeam-s3-core-<version>.bin` (app, full flash)
   - `firmware-tbeam-s3-core-<version>-update.bin` (app, OTA-style)
   - `littlefs-tbeam-s3-core-<version>.bin` (filesystem)
   - `bleota-s3.bin` (BLE OTA stub)
3. Invokes the PlatformIO-bundled `esptool.py` (no system-wide
   `pip install esptool` step required) with the flash layout for
   `BIGDB_8MB` targets verified against upstream
   [`device-install.sh`](https://github.com/meshtastic/firmware/blob/master/bin/device-install.sh):
   - `0x00`       — app
   - `0x340000`   — bleota-s3
   - `0x670000`   — littlefs

`--update` writes only the app partition (`-update.bin`), preserving
saved channels and node config. Use this for routine upgrades on a
configured node.

## Verification

After flashing, unplug + replug USB and check:

```bash
lsusb | rg -i tbeam              # 303a:1001 LilyGo TBeam-S3-Core
ls -l /dev/ttyACM0               # serial port back
pio device monitor -p /dev/ttyACM0 -b 115200   # Meshtastic boot banner
```

Or pair the node from the Meshtastic mobile app over BLE and check the
firmware version field.

## Bumping the pinned stable

```bash
# Latest stable tag:
gh release view --repo meshtastic/firmware --json tagName -q .tagName
# Edit tools/flash.sh -> VERSION_DEFAULT="<new tag>"
```

Pin to a specific tag rather than tracking "latest" so flashes are
reproducible.
