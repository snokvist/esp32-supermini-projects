# hdzero-headtracker-monitor

Decode HDZero BoxPro+ head-tracker PPM on an ESP32-C3 SuperMini and emit CRSF RC channel frames over serial.

## Project Summary

- Name: `hdzero-headtracker-monitor`
- Board: `esp32-c3-devkitm-1`
- PlatformIO environment: `esp32c3_supermini`
- Function: decode PPM head-tracker stream and output CRSF RC channels (with optional human-readable monitor mode)

## Goals

- Default runtime output: CRSF serial RC frame packets (`0xC8`, type `0x16`) from decoded PPM frames
- Optional runtime output: human-readable PPM monitor text for validation/debug
- Keep axis interpretation visible in monitor mode (`ch1 pan`, `ch2 roll`, `ch3 tilt`)

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

1. Enable head tracker output in BoxPro+ settings.
2. Wire BoxPro 3.5mm HT jack:
   - tip -> ESP32 `GPIO2`
   - sleeve -> ESP32 `GND`
3. Power and run firmware. Default mode is **CRSF serial output** at `115200`.
4. Toggle runtime output mode with ESP32 BOOT button (`GPIO9`, active low):
   - one short press switches between CRSF and PPM monitor mode
   - debounce + edge detection are handled in firmware
5. For human-readable monitor output, switch to monitor mode and open serial monitor:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

6. Move headset and observe `ch1/ch2/ch3` values changing in monitor mode.

## Expected Behavior

- Default mode sends binary CRSF RC frames only when a decoded PPM frame arrives (no idle spam when no PPM frame is present).
- Monitor mode prints PPM status at roughly `~5Hz` with frame decode running around `~50Hz`.
- After switching into monitor mode, first stats line appears after one monitor interval (~200ms) so `win` rate is measured over a valid window.
- Monitor mode includes rate diagnostics:
  - `inst`: instantaneous frame-to-frame rate
  - `ema`: smoothed rate estimate
  - `win`: measured frame rate in the 5Hz monitor window with `(ok/warn)` check against `45..55Hz`
  - `age` and `invalid(+delta)` for stale-signal/noise debugging
- `ch1/ch2/ch3` widths are typically around `1000..2000us`.
- For BoxPro+, source-code mapping indicates:
  - `ch1 = pan`
  - `ch2 = roll`
  - `ch3 = tilt`

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
