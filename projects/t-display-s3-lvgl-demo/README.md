# t-display-s3-lvgl-demo

LilyGO T-Display-S3 demo using `LVGL` with a rounded tab/widget interface (`Arduino_GFX` flush bridge).

## Project Summary

- Name: `t-display-s3-lvgl-demo`
- Board: `lilygo-t-display-s3`
- PlatformIO environment: `tdisplays3`
- Function: prove LVGL is viable on T-Display-S3 with a smoother multi-tab UI

## Goals

- Confirm LVGL initialization on T-Display-S3
- Render rounded cards and tabbed widgets (`Dash`, `Inputs`, `System`)
- Read BOOT and GPIO14 buttons
- Show live refresh and CPU metrics
- Demonstrate button-driven navigation/control without touch input

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

- No external wiring required.
- Observe LCD:
  - top tab bar with rounded active tab styling
  - `Dash`: CPU arc, animated load bar, uptime/refresh
  - `Inputs`: BOOT/GPIO14 live LEDs + counters + accent switch state
  - `System`: refresh and CPU bars + summarized metrics + button counters
- Buttons:
  - short `BOOT`: next tab
  - long `BOOT` (>1s): previous tab
  - short `GPIO14`: toggle accent color theme (cyan/orange)
  - long `GPIO14` (>1s): reset input counters and jump to `Inputs` tab
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

- Rounded LVGL tab interface appears on screen after reset.
- Widgets stay inside card bounds (no clipped/overflow bars).
- Tabs/pages are non-scrollable (no runtime scrollbars expected).
- `Ref: X.XHz` and `CPU: X.X%` update about once per second.
- BOOT/GPIO14 presses are reflected on-screen and in serial logs.

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
