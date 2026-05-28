# Hardware — LilyGO T-Beam Supreme

## Board

- MCU: ESP32-S3 (dual-core Xtensa LX7, 8 MB flash, BIGDB partition layout)
- LoRa: SX1262 (most common) or LR1121 (newer multi-band variant) — both
  supported by the same `tbeam-s3-core` firmware build; autodetected at boot
- GPS: u-blox / quectel L76K class, on its own UART
- Display: 0.96" SSD1306 OLED on I2C
- Power: AXP2101 PMIC, 18650 battery holder + USB-C input
- Native USB-CDC (no FTDI/CP210x in the chain)

## USB enumeration

When connected:

```
$ lsusb | rg -i tbeam
Bus 007 Device 003: ID 303a:1001 Espressif Systems LilyGo TBeam-S3-Core
$ ls -l /dev/ttyACM*
crw-rw---- 1 root dialout 166, 0 ... /dev/ttyACM0
```

If the port doesn't appear:

1. Try a known-data USB-C cable (many are power-only)
2. Hold BOOT + tap RESET to force download mode, then `lsusb` should
   still show 303a:1001
3. Check `dmesg | tail` for enumeration errors

## Flash layout (Meshtastic BIGDB_8MB)

| Offset | Size | Contents |
|---|---|---|
| 0x000000 | ~2.1 MB | App (firmware-tbeam-s3-core-*.bin) |
| 0x340000 | ~0.5 MB | BLE OTA stub (bleota-s3.bin) |
| 0x670000 | ~1.5 MB | LittleFS (channels, prefs, region) |

Verified against meshtastic/firmware `bin/device-install.sh` v2.7.15.

## Bring-up checklist

After flashing:

- [ ] OLED shows Meshtastic boot screen + node ID
- [ ] BLE advertises as `Meshtastic_xxxx` (visible in Meshtastic mobile app)
- [ ] GPS LED blinks once it gets a fix (may need clear sky view)
- [ ] Serial monitor (115200) prints boot banner with firmware version
- [ ] Region is set (defaults to UNSET — must configure in app on first boot)
