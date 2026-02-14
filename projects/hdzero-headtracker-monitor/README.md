# hdzero-headtracker-monitor

Read and print HDZero BoxPro+ head-tracker PPM output with an ESP32-C3 SuperMini.

## Project Summary

- Name: `hdzero-headtracker-monitor`
- Board: `esp32-c3-devkitm-1`
- PlatformIO environment: `esp32c3_supermini`
- Function: monitor PPM head-tracker stream over serial for validation/debug

## Goals

- Confirm BoxPro+ HT output is present and stable
- Observe channel pulse widths in microseconds
- Verify axis movement before integrating with RC/servo logic

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
3. Start monitor:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

4. Move headset and observe `ch1/ch2/ch3` values changing.

## Expected Behavior

- Serial lines at roughly `~50Hz` frame rate (printed at ~5Hz)
- `ch1/ch2/ch3` widths around `1000..2000us`
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
