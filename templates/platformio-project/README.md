# __PROJECT_NAME__

Firmware project for ESP32.

## Project Summary

- Name: `__PROJECT_NAME__`
- Board: `__BOARD__`
- PlatformIO environment: `__ENV_NAME__`

## Goals

- Add project purpose and requirements here.

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

- Describe physical connections required (pins, wiring, modules).
- Describe runtime interaction (serial monitor commands, buttons, movement, inputs).
- Describe what successful behavior looks like.

## Expected Behavior

- Define expected output/signals here (serial logs, GPIO behavior, timing/rate).
- Include known normal ranges/units if applicable.

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
