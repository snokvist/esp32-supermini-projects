#!/usr/bin/env bash
# Flash latest stable Meshtastic firmware to a Seeed SenseCAP T1000-E tracker
# (Meshtastic target: tracker-t1000-e, nRF52840 + LR1110).
#
# The T1000-E ships with the Adafruit nRF52 *serial* DFU bootloader (CDC ACM
# only — no UF2 mass-storage interface). Updates use `adafruit-nrfutil` with
# a Nordic DFU `.zip` package over the bootloader serial port. We install
# adafruit-nrfutil into a project-local venv (no system Python pollution,
# no PEP 668 fight) and cache the Meshtastic release zip under `.cache/`.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_DIR="$SCRIPT_DIR/.cache"
VENV_DIR="$SCRIPT_DIR/.venv"
PORT="${PORT:-/dev/ttyACM0}"
VERSION_DEFAULT="v2.7.15.567b8ea"
TARGET="tracker-t1000-e"
BAUD=115200

VERSION="$VERSION_DEFAULT"
FORCE_BOOTLOADER=false

usage() {
  cat <<EOF
Usage: $(basename "$0") [--version <tag>] [--port <dev>] [--force-bootloader]
  --version <tag>      Meshtastic release tag (default: $VERSION_DEFAULT)
  --port <dev>         DFU bootloader serial port (default: \$PORT or /dev/ttyACM0)
  --force-bootloader   Send 1200bps reset to kick a running Meshtastic app into the DFU bootloader
  --help               Show this help

Before flashing: put the device into the DFU bootloader.
  Order of reliability (try in this order):
    1. Hardware: press the side button 5x rapidly (within ~1 s) — Seeed-documented
       method. May need a few attempts; LED feedback varies by firmware.
    2. From the Meshtastic mobile app over BLE: Settings -> Update firmware.
    3. meshtastic --port /dev/ttyACM0 --enter-dfu  (needs device awake + Meshtastic
       API responding; tracker may be sleeping).
  When in bootloader, lsusb shows: 239a:8029 Adafruit T1000-E-BOOT
  User firmware enumerates as:    2886:0057 Seeed Technology Co. T1000-E
  The Seeed app firmware does NOT honor the 1200bps reset trick (--force-bootloader
  will not work from a running app on T1000-E).
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)           VERSION="$2"; shift 2 ;;
    --port)              PORT="$2"; shift 2 ;;
    --force-bootloader)  FORCE_BOOTLOADER=true; shift ;;
    -h|--help)           usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

# ---------- venv bootstrap ----------
if [[ ! -x "$VENV_DIR/bin/adafruit-nrfutil" ]]; then
  echo "==> Bootstrapping venv at $VENV_DIR (one-time, ~30s)"
  python3 -m venv "$VENV_DIR"
  "$VENV_DIR/bin/pip" install --quiet --upgrade pip
  "$VENV_DIR/bin/pip" install --quiet adafruit-nrfutil meshtastic
fi
NRFUTIL="$VENV_DIR/bin/adafruit-nrfutil"

# ---------- USB-disconnect workaround patch ----------
# Adafruit nRF52 bootloader on Seeed T1000-E briefly drops USB CDC during the
# post-start flash erase. Patch the venv copy of dfu_transport_serial.py to
# close + reopen the port after start_dfu, so subsequent writes don't fail
# with PortNotOpenError. Idempotent: skipped if already patched.
DFU_PY="$(find "$VENV_DIR" -path '*nordicsemi/dfu/dfu_transport_serial.py' | head -n1)"
if [[ -n "$DFU_PY" ]] && ! grep -q 'WAYBEAM-PATCH' "$DFU_PY"; then
  echo "==> Patching $DFU_PY (USB-reopen after start_dfu)"
  python3 - "$DFU_PY" <<'PYEOF'
import sys, re, pathlib
p = pathlib.Path(sys.argv[1])
src = p.read_text()
marker = "time.sleep( self.get_erase_wait_time() )"
patch = """time.sleep( self.get_erase_wait_time() )
        # WAYBEAM-PATCH: Seeed T1000-E bootloader briefly drops USB CDC during
        # the post-start flash erase; reopen the port so subsequent writes don't
        # fail with PortNotOpenError after kernel re-enumeration.
        try:
            self.serial_port.close()
        except Exception:
            pass
        for _attempt in range(40):
            try:
                self.serial_port = Serial(port=self.com_port, baudrate=self.baud_rate, rtscts=self.flow_control, timeout=self.timeout)
                break
            except Exception:
                time.sleep(0.25)
        else:
            raise NordicSemiException("WAYBEAM-PATCH: serial port {0} did not re-appear after start_dfu".format(self.com_port))"""
if marker not in src:
    sys.exit("marker not found, refusing to patch")
src = src.replace(marker, patch, 1)
p.write_text(src)
PYEOF
fi

# ---------- optional 1200bps kick into bootloader ----------
if [[ "$FORCE_BOOTLOADER" == true ]]; then
  if [[ -e "$PORT" ]]; then
    echo "==> 1200bps reset on $PORT -> request bootloader"
    "$VENV_DIR/bin/python" -c "import serial,time; s=serial.Serial('$PORT',1200); time.sleep(0.2); s.close()" 2>/dev/null || true
    sleep 2
  else
    echo "==> $PORT not present; skipping 1200bps reset (board may already be in bootloader)"
  fi
fi

if [[ ! -e "$PORT" ]]; then
  echo "Error: serial port $PORT not present." >&2
  echo "       Put the T1000-E into DFU bootloader (double-tap side button) and try again." >&2
  exit 1
fi

# Verify the port belongs to the DFU bootloader, not user firmware.
BOOTLOADER_PRESENT=$(lsusb | grep -cE '239a:8029' || true)
if [[ "$BOOTLOADER_PRESENT" -eq 0 ]]; then
  echo "Warning: 239a:8029 T1000-E-BOOT not seen in lsusb. The port $PORT may be" >&2
  echo "         running user firmware. Double-tap the side button to enter DFU," >&2
  echo "         or re-run with --force-bootloader." >&2
  exit 1
fi

# ---------- fetch Meshtastic release ----------
mkdir -p "$CACHE_DIR"
ZIP_URL="https://github.com/meshtastic/firmware/releases/download/${VERSION}/firmware-nrf52840-${VERSION#v}.zip"
ZIP_PATH="$CACHE_DIR/firmware-nrf52840-${VERSION#v}.zip"

if [[ ! -f "$ZIP_PATH" ]]; then
  echo "==> Downloading $ZIP_URL"
  curl -fSL --retry 3 -o "$ZIP_PATH.part" "$ZIP_URL"
  mv "$ZIP_PATH.part" "$ZIP_PATH"
else
  echo "==> Using cached $ZIP_PATH"
fi

WORK_DIR="$CACHE_DIR/${VERSION#v}"
mkdir -p "$WORK_DIR"
OTA_NAME="firmware-${TARGET}-${VERSION#v}-ota.zip"
unzip -o -q "$ZIP_PATH" "$OTA_NAME" -d "$WORK_DIR"
OTA_ZIP="$WORK_DIR/$OTA_NAME"

# ---------- flash ----------
echo "==> nrfutil: $NRFUTIL"
"$NRFUTIL" version | head -1
echo "==> port:    $PORT @ $BAUD"
echo "==> target:  $TARGET ($VERSION)"
echo "==> package: $OTA_ZIP"
echo
echo "==> Flashing via Nordic DFU over serial..."
"$NRFUTIL" --verbose dfu serial --package "$OTA_ZIP" --port "$PORT" --baudrate $BAUD --singlebank

echo
echo "==> Done. The T1000-E will reboot into the new firmware."
echo "    Pair via the Meshtastic app over BLE to verify."
