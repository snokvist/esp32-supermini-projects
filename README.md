# ESP32 SuperMini Projects

Starter monorepo for ESP32 embedded + DIY electronics projects.

## What this gives you

- Reusable project template using PlatformIO + Arduino framework
- Script to create new projects quickly
- Clean structure for many small firmware experiments
- Basic repo standards (`.gitignore`, `.editorconfig`, docs)

## Repo layout

```text
.
├── docs/
│   ├── SETUP.md
│   └── WORKFLOW.md
├── projects/
│   └── (your generated projects live here)
├── scripts/
│   ├── setup.sh
│   └── new-project.sh
└── templates/
    └── platformio-project/
        ├── docs/
        │   ├── HARDWARE.md
        │   └── TASK_LOG.md
        ├── include/
        ├── lib/
        ├── README.md
        ├── src/
        │   └── main.cpp
        ├── test/
        └── platformio.ini
```

## Quick start

1. Run local setup once:

```bash
source ./scripts/setup.sh
```

2. `pio` is now available in this terminal.
3. Create a new project from template:

```bash
./scripts/new-project.sh blink-led
```

4. Build and upload:

```bash
cd projects/blink-led
pio run
pio run -t upload
pio device monitor
```

## Create a project with custom board/env

```bash
./scripts/new-project.sh weather-node esp32dev esp32dev
```

Arguments:

- `project-name` (required)
- `board` (optional, default: `esp32-c3-devkitm-1`)
- `env-name` (optional, default: `esp32c3_supermini`)

## Notes

- Default template targets `ESP32-C3 SuperMini`-compatible board config.
- Default template includes per-project documentation files.
- You can change board/env per project depending on hardware.
- Keep each project self-contained inside `projects/<name>`.

See `docs/SETUP.md` for prerequisites and `docs/WORKFLOW.md` for execution/documentation workflow.
