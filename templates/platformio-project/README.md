# __PROJECT_NAME__

Firmware project for ESP32.

## Project Summary

- Name: `__PROJECT_NAME__`
- Board: `__BOARD__`
- PlatformIO environment: `__ENV_NAME__`

## Goals

- Add project purpose and requirements here.

## Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
