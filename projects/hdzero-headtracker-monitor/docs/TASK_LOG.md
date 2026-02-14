# Task Log - hdzero-headtracker-monitor

Use this file as a running implementation log.

## Entries

### 2026-02-14 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `esp32-c3-devkitm-1`
- Env: `esp32c3_supermini`
- Initial firmware: LED blink + serial output

### 2026-02-14 - Implement PPM Monitor

- Replaced blink sketch with interrupt-based PPM decoder on `GPIO2`
- Added serial output for channel widths, frame rate, invalid pulse count
- Added BoxPro+ channel interpretation note (`ch1 pan`, `ch2 roll`, `ch3 tilt`)
- Updated hardware doc with 3.5mm TS wiring and bring-up checklist
- Validation:
  - `pio run` successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - Device serial output confirmed (`No PPM frame detected yet...`) before jack wiring

### 2026-02-14 - First Live Signal Validation

- Confirmed live PPM stream from BoxPro+ on ESP32 monitor
- Observed stable `ch=3` and `rate=50.0Hz`
- Confirmed channel values update with headset movement
- Finalized project README with explicit usage + interaction instructions
