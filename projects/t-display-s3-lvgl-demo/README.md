# t-display-s3-lvgl-demo

LilyGO T-Display-S3 demo using `LVGL` (with `Arduino_GFX` display flush bridge).

## Project Summary

- Name: `t-display-s3-lvgl-demo`
- Board: `lilygo-t-display-s3`
- PlatformIO environment: `tdisplays3`
- Function: prove LVGL is viable on T-Display-S3 with a simple dashboard UI

## Goals

- Confirm LVGL initialization on T-Display-S3
- Render text + image/canvas + progress bar
- Read BOOT and GPIO14 buttons
- Show live refresh and CPU metrics

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

- No external wiring required.
- Observe LCD:
  - LVGL title/subtitle
  - small icon (canvas-drawn)
  - animated progress bar
  - uptime + refresh Hz + CPU% metrics
  - button states/counters (`BOOT`, `GPIO14`)
- Press `BOOT` or `GPIO14`: bar turns red while pressed and counters increment.
- Optional serial monitor:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

If upload times out while board is in other firmware/boot mode:

1. Hold `BOOT`
2. Tap `RST`
3. Release `BOOT`
4. Retry upload

## Expected Behavior

- LVGL interface appears on screen after reset.
- Progress bar animates continuously.
- `Ref: X.XHz` and `CPU: X.X%` update about once per second.
- BOOT/GPIO14 presses are reflected both on screen and serial logs.

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
