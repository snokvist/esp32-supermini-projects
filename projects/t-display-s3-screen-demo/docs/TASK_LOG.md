# Task Log - t-display-s3-screen-demo

Use this file as a running implementation log.

## Entries

### 2026-02-14 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `lilygo-t-display-s3`
- Env: `tdisplays3`
- Initial firmware: LED blink + serial output

### 2026-02-14 - LCD Init Demo

- Switched firmware to LilyGO T-Display-S3 `Arduino_GFX` demo-style pin mapping
- Added screen init + text rendering + 16x16 bitmap icon + animated bar
- Added `Arduino_GFX` dependency in `platformio.ini` (pinned to `1.4.9` for PlatformIO core compatibility)
- Updated README/HARDWARE docs with usage, expected behavior, and pin map
- Validation:
  - `pio run` successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - Upload required ROM boot mode (`BOOT` + `RST`) because board was initially in CircuitPython mode

### 2026-02-14 - Flicker Reduction + Refresh Metric

- Reduced progress bar flicker by drawing only changed bar pixels (delta update)
- Moved stats redraw to once per second instead of every frame
- Added on-screen refresh readout (`Refresh: X.XHz`)

### 2026-02-14 - Buttons + CPU Metric

- Added button handling for `BOOT` (`GPIO0`) and `GPIO14`
- Added visual button reaction: progress bar turns red while a button is pressed
- Added on-screen button state and press counters
- Added approximate CPU load metric (`CPU: X.X%`) based on UI work time
