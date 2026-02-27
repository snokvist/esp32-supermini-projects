# Hardware Notes - supermini-oled-hello

## Target Board

- Board config: `esp32-c3-devkitm-1`
- Environment: `esp32c3_supermini`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- Baud: `115200`

## OLED Module

- Type: 0.96" I2C OLED (`SSD1306`, typically `128x64`)
- Typical address: `0x3C` (some boards use `0x3D`)
- Logic level: use `3.3V`

## Pin Map

| ESP32-C3 SuperMini | OLED Pin | Notes |
|---|---|---|
| `3V3` | `VCC` | Module power |
| `GND` | `GND` | Common ground |
| `GPIO4` | `SDA` | I2C data |
| `GPIO5` | `SCL` | I2C clock |

## Controls

- Onboard BOOT button: `GPIO9` (active-low input with pull-up)
- Action in firmware: cycles `HELLO -> GRAPH -> PIXEL_TEST -> RECOVERY -> HELLO`
- `PIXEL_TEST` mode flashes full-screen ON/OFF every 2 seconds for dead-pixel checks
- `RECOVERY` mode runs fast full-screen invert/checker/noise patterns to exercise stuck pixels

## Wiring Checklist

1. Confirm USB is connected and board enumerates as `/dev/ttyACM0`.
2. Power OLED from `3V3` (not `5V`).
3. Verify `SDA` and `SCL` are not swapped.
4. If screen stays blank, scan alternate address `0x3D` in firmware.

## Bring-Up Notes

- This project uses `Wire.begin(4, 5)` explicitly.
- I2C clock is set to `400kHz`.
- If initialization fails, firmware remains running but does not draw further frames and reports the error over serial.
