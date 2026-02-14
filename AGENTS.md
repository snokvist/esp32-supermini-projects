# AGENTS.md

Project operating guide for Codex sessions in this repository.

## Scope

- Repository: `esp32-supermini-projects`
- Goal: manage multiple ESP32 embedded/DIY projects with consistent build, flash, and documentation workflows.

## Execution Mode (Default)

- Codex should execute tasks end-to-end with high autonomy.
- Unless requirements are ambiguous or risky, do not stop for confirmations.
- Always implement, validate, and document changes in the same task.
- Prefer concrete results over planning-only responses.

## Standard Repository Structure

- `projects/`: firmware projects (`projects/<project-name>/`)
- `templates/platformio-project/`: starter project template
- `scripts/setup.sh`: install/update local PlatformIO and PATH
- `scripts/new-project.sh`: project bootstrap script
- `docs/SETUP.md`: local setup instructions
- `docs/WORKFLOW.md`: Codex workflow runbook

## Per-Project Structure (Required)

Each project should contain:

- `platformio.ini`
- `src/`, `include/`, `lib/`, `test/`
- `README.md` (purpose, usage, status)
- `docs/HARDWARE.md` (board, pin map, wiring)
- `docs/TASK_LOG.md` (dated implementation log)

## Session Startup Checklist

At the start of a working session, Codex should:

1. Read `AGENTS.md` and `docs/WORKFLOW.md`.
2. Check repository state (`git status --short`).
3. Confirm tool availability (`pio --version`), and run `./scripts/setup.sh` if missing.
4. If hardware work is requested, detect board/serial port before flashing.

## New ESP32 Project Checklist

When user asks to start a new project:

1. Create project with `./scripts/new-project.sh <name> [board] [env]`.
2. Build immediately with `pio run`.
3. If board is connected, upload (`pio run -t upload --upload-port <port>`) and verify serial output.
4. Update project docs:
   - goals/status in `README.md`
   - wiring and pin decisions in `docs/HARDWARE.md`
   - dated entry in `docs/TASK_LOG.md`
5. Report exact files changed and verification results.

## Hardware Notes (Current Machine)

Known board connection:

- USB: `Espressif USB JTAG/serial debug unit` (`303a:1001`)
- Serial: `/dev/ttyACM0`
- Driver: `cdc_acm`

Useful checks:

```bash
lsusb | rg -i espressif
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
udevadm info -q property -n /dev/ttyACM0 | rg 'ID_VENDOR|ID_MODEL|ID_SERIAL'
```

## PlatformIO Notes

- `pio` path on this machine: `/home/snokvist/.local/bin/pio`
- validated version: `6.1.19`
- ESP32-C3 projects should keep USB CDC enabled in `build_flags`:
  - `-DARDUINO_USB_CDC_ON_BOOT=1`
  - `-DARDUINO_USB_MODE=1`

## Documentation Rule

For every implementation task, keep docs aligned in the same change set:

- Update the affected project's `README.md`, `docs/HARDWARE.md`, and `docs/TASK_LOG.md` when behavior, wiring, or setup changes.
- If only infra-level files changed, update `AGENTS.md` and/or `docs/WORKFLOW.md` as needed.

## Maintenance Rule

When workflow, tooling, or folder conventions change, update `AGENTS.md` immediately in the same task.
