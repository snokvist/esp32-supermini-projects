# Hardware Notes - t-display-s3-lvgl-demo

## Target Board

- Board config: `lilygo-t-display-s3`
- Environment: `tdisplays3`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- Baud: `115200`

## Pin Map (Initial)

- Display bus (LilyGO mapping):
  - `DC=7`, `CS=6`, `WR=8`, `RD=9`, `RST=5`
  - `D0..D7 = 39,40,41,42,45,46,47,48`
- Backlight: `GPIO38`
- Buttons:
  - `BOOT` on `GPIO0` (active-low)
  - user button on `GPIO14` (active-low)

## Wiring

No external wiring required for this LVGL demo.

Bring-up:

1. Connect T-Display-S3 over USB-C.
2. Upload firmware to `/dev/ttyACM0`.
3. If upload fails, use BOOT/RST manual download-mode entry, then retry.
