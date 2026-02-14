# Codex Workflow Runbook

Default workflow for this repository.

## 1) Understand Task

- Identify whether task is: new project, firmware feature, hardware integration, bugfix, or repo/tooling update.
- Assume execution-first mode: implement and validate directly.

## 2) Gather Context

- Check repo status:

```bash
git status --short
```

- Ensure tools are available (run once per machine if needed):

```bash
source ./scripts/setup.sh
pio --version
```

- Check board connection when relevant:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
pio device list
```

## 3) Start New Project (When Requested)

```bash
./scripts/new-project.sh <project-name> [board] [env]
cd projects/<project-name>
pio run
```

If board is connected:

```bash
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## 4) Implement Changes

- Keep firmware changes in `src/` and related includes/libs.
- Preserve per-project isolation; avoid cross-project coupling unless requested.

## 5) Document in Project Folder

For project-level work, update:

- `projects/<project-name>/README.md`
- `projects/<project-name>/docs/HARDWARE.md`
- `projects/<project-name>/docs/TASK_LOG.md`

`README.md` requirement (mandatory):

- summary/goal
- build + flash commands
- how to interact with the project (wiring + runtime steps)
- expected behavior/output

Task log entry format:

- date
- what changed
- validation performed
- next step (optional)

## 6) Validate Before Handoff

Minimum validation when applicable:

1. build success (`pio run`)
2. upload success (`pio run -t upload`)
3. runtime signal (serial output, sensor reading, LED behavior)

## 7) Handoff Format

Include:

- what was changed
- exact files changed
- validation results
- any blockers or assumptions

## 8) GitHub / PR Flow

When asked to submit a PR:

1. Sync base branch:
   - `git checkout main`
   - `git pull --ff-only`
2. Create branch:
   - `git checkout -b <type>/<short-name>`
3. Stage and commit:
   - `git add -A`
   - `git commit -m \"...\"`
4. Push:
   - `git push -u origin <branch>`
5. Create PR:
   - create `.pr_body.md` in repo root
   - `gh pr create --base main --head <branch> --title \"...\" --body-file .pr_body.md`
   - remove `.pr_body.md` after PR is created

Push auth recovery:

- If push fails with `could not read Username for 'https://github.com'`:
  - `git config --global credential.helper \"!$(command -v gh) auth git-credential\"`
  - retry `git push -u origin <branch>`

## 9) Keep Workflow Current

If this process changes, update `AGENTS.md` and `docs/WORKFLOW.md` in the same task.
