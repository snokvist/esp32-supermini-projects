# AGENTS.md

Operating guide for AI agent sessions. Read this first, then act.

## Repo at a Glance

Monorepo of ESP32 PlatformIO/Arduino projects. Four active firmware projects under `projects/`:

| Project | Board | What it does |
|---------|-------|-------------|
| `supermini-smoke` | ESP32-C3 SuperMini | LED blink smoke test |
| `t-display-s3-screen-demo` | LilyGO T-Display-S3 | GFX library UI demo |
| `t-display-s3-lvgl-demo` | LilyGO T-Display-S3 | LVGL 8.3.11 tabbed UI |
| `hdzero-headtracker-monitor` | ESP32-C3 SuperMini | PPM→CRSF bridge, servo PWM, web config, NVS |

Key paths: `scripts/` (setup, scaffolding), `templates/` (project starter), `docs/` (SETUP.md, WORKFLOW.md).

## Execution Philosophy

1. **Plan first, then execute.** For non-trivial work, outline the approach (outcome, constraints, acceptance criteria) before writing code. Iterate on the plan until it's solid, then implement — Claude should usually one-shot a good plan.
2. **Verify aggressively.** Every change must pass a feedback loop before handoff. The verification step is not optional — it is what separates working code from plausible code.
3. **Ship concrete results.** Prefer implemented + validated over planning-only responses. Don't stop for confirmation unless requirements are genuinely ambiguous or the action is destructive/irreversible.
4. **Update docs in the same changeset.** If behavior, wiring, or setup changes, the corresponding README.md, HARDWARE.md, and TASK_LOG.md update ships with the code.

## Verification Loop (Mandatory)

Every firmware change must complete this loop before handoff:

```
Code → Build → Flash → Observe → Report
       pio run   pio run -t upload   serial/LED/web   what changed + what was verified
```

Minimum checks per change:
- `pio run` succeeds (build)
- `pio run -t upload --upload-port /dev/ttyACM0` succeeds (flash, when board connected)
- Runtime signal confirmed (serial output, LED behavior, web UI response — whatever applies)

If any step fails: diagnose root cause, fix, re-run the full loop. Do not skip steps or hand off broken builds.

## Task Brief Format

When decomposing work, structure each task with:
- **Outcome**: What should be true when finished
- **Context**: Where in the codebase this lives, existing patterns to follow
- **Constraints**: Pin assignments, protocol specs, memory limits, style rules
- **Non-goals**: What you're explicitly not doing (prevents scope creep)
- **Acceptance criteria**: Concrete checks — build passes, serial output matches, endpoint returns expected JSON

## Per-Project Structure

Every project under `projects/<name>/` must contain:

```
platformio.ini          # board, env, build flags, lib_deps
src/main.cpp            # primary firmware source
README.md               # purpose, build/flash, interaction, expected behavior (mandatory)
docs/HARDWARE.md        # board config, pin map, wiring, bring-up checklist
docs/TASK_LOG.md        # dated entries: what changed, validation, next steps
include/ lib/ test/     # standard PlatformIO dirs
```

## Code Conventions

- 2-space indent, LF line endings, UTF-8 (see `.editorconfig`)
- `gCamelCase` for globals, `camelCase` for locals, `UPPER_CASE` for compile-time constants
- USB CDC enabled on all ESP32-C3 builds: `-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`
- Serial baud: 115200 across all projects
- PROGMEM for large static data (HTML strings, bitmaps)
- Keep firmware in a single `main.cpp` per project unless complexity demands splitting

## Hardware Environment

- USB device: `Espressif USB JTAG/serial debug unit` (303a:1001)
- Serial port: `/dev/ttyACM0`
- PlatformIO: `/home/snokvist/.local/bin/pio` (v6.1.19)
- Quick checks:
  ```
  lsusb | rg -i espressif
  ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
  ```

## Session Startup

1. Read this file.
2. `git status --short` — know the repo state.
3. `pio --version` — confirm toolchain. Run `./scripts/setup.sh` if missing.
4. If hardware work: verify board connection before flashing.
5. If GitHub work: `gh auth status` to confirm auth.

## Git & PR Workflow

```bash
git checkout main && git pull --ff-only          # sync
git checkout -b <type>/<short-name>              # branch
# ... implement + verify ...
git add <specific-files> && git commit -m "..."  # commit
git push -u origin <branch>                      # push
gh pr create --base main --head <branch> \       # PR
  --title "..." --body-file .pr_body.md
rm .pr_body.md                                   # cleanup
```

Use `--body-file` over inline `--body` to avoid shell quoting issues.

Push auth fallback: `git config --global credential.helper "!$(command -v gh) auth git-credential"`

## New Project Bootstrap

```bash
./scripts/new-project.sh <name> [board] [env]
cd projects/<name> && pio run                    # must build clean
```

Then: update README.md (goals), HARDWARE.md (pins/wiring), TASK_LOG.md (initial entry).

## Error Correction Protocol

When something goes wrong — a build fails unexpectedly, a setting doesn't persist, a signal decodes incorrectly — and you discover the root cause:

1. **Fix it** in the current changeset.
2. **Add the lesson** to the relevant project's docs or to this file if it's a repo-wide pattern.
3. **Verify the fix** through the full verification loop.

This file is a living document. Update it when workflows, tooling, or conventions change.
