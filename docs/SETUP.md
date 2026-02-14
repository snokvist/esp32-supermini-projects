# Setup Guide

## Prerequisites

- Git
- Python 3.10+
- PlatformIO Core (`pio`) OR VS Code with PlatformIO extension
- USB cable/data cable for ESP32 board

## Install PlatformIO Core

```bash
source ./scripts/setup.sh
```

`scripts/setup.sh` is idempotent. It:
- creates/updates `~/.local/share/pio-venv`
- installs/updates PlatformIO
- links `pio` into `~/.local/bin/pio`
- ensures `~/.local/bin` is in your shell PATH

## Typical workflow

1. Create a new project from template:

```bash
./scripts/new-project.sh my-project
```

2. Enter project directory:

```bash
cd projects/my-project
```

3. Build firmware:

```bash
pio run
```

4. Upload firmware:

```bash
pio run -t upload
```

5. Open serial monitor:

```bash
pio device monitor
```

## Project documentation workflow

Every generated project includes:

- `README.md`
- `docs/HARDWARE.md`
- `docs/TASK_LOG.md`

Keep these files updated as implementation changes.
For full task execution process, see `docs/WORKFLOW.md`.

## Useful PlatformIO commands

```bash
pio device list
pio run -t clean
pio run -t upload --upload-port /dev/ttyUSB0
```

## Choosing a board

Change `board` in each project's `platformio.ini` to match your hardware.
Examples:

- `esp32-c3-devkitm-1` (good starting point for many ESP32-C3 SuperMini clones)
- `esp32dev` (classic ESP32 dev boards)
- `esp32-s3-devkitc-1` (ESP32-S3 boards)

See full list:

```bash
pio boards espressif32
```
