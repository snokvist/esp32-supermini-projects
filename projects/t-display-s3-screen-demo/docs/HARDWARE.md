# Hardware Notes - t-display-s3-screen-demo

## Target Board

- Board config: `lilygo-t-display-s3`
- Environment: `tdisplays3`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- Baud: `115200`

## Pin Map (Initial)

- Display bus (from LilyGO Arduino_GFX demo):
  - `DC=7`, `CS=6`, `WR=8`, `RD=9`
  - `RST=5`
  - `D0..D7 = 39,40,41,42,45,46,47,48`
- Backlight: `GPIO38`
- Buttons:
  - `BOOT` on `GPIO0` (active-low)
  - second user button on `GPIO14` (active-low)

## Wiring

No external wiring required for this demo.

Bring-up steps:

1. Connect T-Display-S3 via USB-C.
2. Upload firmware to `/dev/ttyACM0`.
3. If display stays blank, tap `RST` once and reconnect monitor.

Boot mode notes:

- If uploads time out, enter ROM download mode manually: hold `BOOT`, tap `RST`, release `BOOT`.
- After successful flash, tap `RST` once to exit ROM mode and run app if needed.
