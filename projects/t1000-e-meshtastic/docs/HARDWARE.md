# Hardware — Seeed Studio SenseCAP T1000-E

## Board

- MCU: nRF52840 (ARM Cortex-M4F, 1 MB flash, 256 KB RAM, BLE 5.0)
- LoRa + GNSS: **LR1110** (sub-GHz LoRa + S-band GNSS scanning, multi-band)
- Display: small monochrome OLED + RGB status LED
- Sensors: accelerometer, optional environment sensors per SKU
- Power: built-in LiPo, charged over USB-C
- Bootloader: Adafruit nRF52 serial DFU bootloader (Seeed fork, no UF2 MSC)

## USB identities

| State | VID:PID | Product string | Linux port |
|---|---|---|---|
| User firmware | `2886:0057` | `Seeed Technology Co., Ltd. T1000-E` | `/dev/ttyACM0` |
| DFU bootloader | `239a:8029` | `Adafruit T1000-E-BOOT` | `/dev/ttyACM0` |

(The two states present different USB descriptors but enumerate at the same
`/dev/ttyACM0` node since only one is active at a time.)

## Entering DFU bootloader

Try in this order:

1. **Side button x5 rapidly** — Seeed-documented method. Five quick
   presses within ~1 second. May take a few attempts. Confirm with
   `lsusb | grep 239a:8029`.
2. **Meshtastic mobile app** → Settings → Update firmware → trigger DFU.
   App stays connected over BLE, device reboots to bootloader.
3. **`meshtastic --port /dev/ttyACM0 --enter-dfu`** from the CLI in
   `tools/.venv/bin/`. Only works if the device is awake and currently
   answering the Meshtastic API over USB.

What does **not** work (verified on this unit):

- 1200bps reset on the user-firmware port. Seeed firmware ignores it.
- Single-tap of the side button — interpreted as a soft reset, comes
  back to user firmware after ~6 s offline.

## Verification after flashing

- Wait for the device to reboot and re-enumerate as `2886:0057`.
- Pair from the Meshtastic mobile app over BLE.
- Check firmware version in the app (should match the flashed tag).
- For a power-managed tracker, GPS fix and first beacon may take several
  minutes outdoors.

## Flash layout

The Meshtastic OTA package is a Nordic DFU zip containing:

```
manifest.json    # init packet metadata
firmware.dat     # init packet (SoftDevice / app version constraints)
firmware.bin     # application image (~486 KB for tracker-t1000-e v2.7.15)
```

The bootloader writes the new app into the inactive bank and swaps on
next reset. `--singlebank` is required because the T1000-E partition
layout doesn't fit the dual-bank scheme.
