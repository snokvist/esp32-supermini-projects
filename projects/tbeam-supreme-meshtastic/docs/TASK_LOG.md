# Task Log — tbeam-supreme-meshtastic

## 2026-05-28 — initial scaffold + first flash

**Outcome**: New project directory for flashing upstream Meshtastic firmware
to a LilyGO T-Beam Supreme connected on `/dev/ttyACM0`.

**What landed**:
- `tools/flash.sh` — downloads pinned Meshtastic release zip
  (`firmware-esp32s3-<ver>.zip`) and flashes `tbeam-s3-core` via the
  PlatformIO-bundled `esptool.py`. Defaults to factory flash (wipes config);
  `--update` does OTA-style app-only flash.
- `README.md`, `docs/HARDWARE.md` — usage and pin/USB reference.
- No `platformio.ini` — this project flashes prebuilt upstream firmware,
  it does not build anything.

**Pinned stable**: `v2.7.15.567b8ea` (published 2025-11-18).

**Verification on first run**:
- USB enumeration: `303a:1001 Espressif Systems LilyGo TBeam-S3-Core`
- Port: `/dev/ttyACM0`
- Flash mode: factory (full erase, fresh install)

**Next steps**:
- After first boot, configure region + LongFast channel via Meshtastic app.
- Bump `VERSION_DEFAULT` when a newer stable releases.
