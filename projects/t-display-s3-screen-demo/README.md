# t-display-s3-screen-demo

LilyGO T-Display-S3 screen initialization demo using `Arduino_GFX`.

## Project Summary

- Name: `t-display-s3-screen-demo`
- Board: `lilygo-t-display-s3`
- PlatformIO environment: `tdisplays3`
- Function: initialize LCD, render text, draw a small bitmap icon, animate a progress bar

## Goals

- Verify screen init on T-Display-S3
- Show text rendering works
- Show bitmap draw works
- Provide a known-good base for future UI work

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

- No external wiring required (USB only).
- Flash firmware, then power cycle/reset board if needed.
- Observe LCD:
  - title + subtitle text
  - yellow 16x16 bitmap icon
  - moving green progress bar
  - uptime counter updating
  - refresh-rate number updating (`Ref: X.XHz`)
  - CPU load estimate (`CPU: X.X%`)
  - button state/counters for `BOOT` and `GPIO14`
- Optional serial monitor:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

If upload times out while board is running CircuitPython firmware:

1. Hold `BOOT`
2. Tap `RST`
3. Release `BOOT`
4. Run upload again

If the device remains in download mode as `ESP32-S3 (303a:0009)` after upload, tap `RST` once to run the flashed app.

## Expected Behavior

- Backlight turns on and screen clears to black.
- Static UI appears immediately.
- Progress bar sweeps repeatedly with reduced flicker.
- Refresh metric updates about once per second.
- Pressing either button turns progress bar red while pressed and increments on-screen counters.
- Serial prints `Display initialized.` after successful init.

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
