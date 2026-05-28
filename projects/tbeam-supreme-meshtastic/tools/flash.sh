#!/usr/bin/env bash
# Flash latest stable Meshtastic firmware to a LilyGO T-Beam Supreme
# (Meshtastic target: tbeam-s3-core, ESP32-S3, 8 MB flash, BigDB layout).
#
# Default behavior: factory flash (erase + app + bleota + littlefs). Wipes any
# existing config. Use `--update` to OTA-flash the app partition only and keep
# littlefs / saved channels intact.
#
# Calls the PlatformIO-bundled esptool directly so no `pip install esptool`
# step is needed. Caches the GitHub release zip under `.cache/` next to this
# script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_DIR="$SCRIPT_DIR/.cache"
PORT="${PORT:-/dev/ttyACM0}"
VERSION_DEFAULT="v2.7.15.567b8ea"
TARGET="tbeam-s3-core"

# Flash layout for BIGDB_8MB targets (verified against meshtastic/firmware
# device-install.sh in v2.7.15.567b8ea).
APP_OFFSET=0x00
BLEOTA_OFFSET=0x340000
LFS_OFFSET=0x670000

MODE="factory"
VERSION="$VERSION_DEFAULT"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--update] [--version <tag>] [--port <dev>]
  --update           OTA-style flash: app partition only, preserve littlefs (channels, prefs)
  --version <tag>    Meshtastic release tag (default: $VERSION_DEFAULT)
  --port <dev>       Serial port (default: \$PORT or /dev/ttyACM0)
  --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --update)  MODE="update"; shift ;;
    --version) VERSION="$2"; shift 2 ;;
    --port)    PORT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

ESPTOOL="$(find "$HOME/.platformio/packages/tool-esptoolpy" -maxdepth 1 -name 'esptool.py' | head -n1)"
if [[ -z "$ESPTOOL" ]]; then
  echo "Error: esptool.py not found under ~/.platformio/packages/tool-esptoolpy" >&2
  echo "       Run \`pio platform install espressif32\` or install esptool via pip." >&2
  exit 1
fi

if [[ ! -e "$PORT" ]]; then
  echo "Error: serial port $PORT not present. Connected via USB?" >&2
  exit 1
fi

mkdir -p "$CACHE_DIR"
ZIP_URL="https://github.com/meshtastic/firmware/releases/download/${VERSION}/firmware-esp32s3-${VERSION#v}.zip"
ZIP_PATH="$CACHE_DIR/firmware-esp32s3-${VERSION#v}.zip"

if [[ ! -f "$ZIP_PATH" ]]; then
  echo "==> Downloading $ZIP_URL"
  curl -fSL --retry 3 -o "$ZIP_PATH.part" "$ZIP_URL"
  mv "$ZIP_PATH.part" "$ZIP_PATH"
else
  echo "==> Using cached $ZIP_PATH"
fi

WORK_DIR="$CACHE_DIR/${VERSION#v}"
mkdir -p "$WORK_DIR"
unzip -o -q "$ZIP_PATH" \
  "firmware-${TARGET}-${VERSION#v}.bin" \
  "firmware-${TARGET}-${VERSION#v}-update.bin" \
  "littlefs-${TARGET}-${VERSION#v}.bin" \
  "bleota-s3.bin" \
  -d "$WORK_DIR"

APP_BIN="$WORK_DIR/firmware-${TARGET}-${VERSION#v}.bin"
UPDATE_BIN="$WORK_DIR/firmware-${TARGET}-${VERSION#v}-update.bin"
LFS_BIN="$WORK_DIR/littlefs-${TARGET}-${VERSION#v}.bin"
BLEOTA_BIN="$WORK_DIR/bleota-s3.bin"

echo "==> esptool: $ESPTOOL"
python3 "$ESPTOOL" version | head -1
echo "==> port:    $PORT"
echo "==> target:  $TARGET ($VERSION)"
echo "==> mode:    $MODE"
echo

# Kick the running firmware into the ROM bootloader. T-Beam Supreme uses
# tinyusb-CDC, so esptool's DTR/RTS reset dance doesn't reach the chip ROM
# while user firmware is running. Opening the port at 1200 baud and closing
# it is the Arduino/Meshtastic convention to request a reboot-to-bootloader.
echo "==> 1200bps reset -> bootloader"
python3 -c "import serial,time; s=serial.Serial('$PORT',1200); time.sleep(0.2); s.close()" 2>/dev/null || true
sleep 2

if [[ "$MODE" == "factory" ]]; then
  echo "==> Erasing flash (full wipe)"
  python3 "$ESPTOOL" --port "$PORT" erase_flash
  echo "==> Flashing app @ $APP_OFFSET"
  python3 "$ESPTOOL" --port "$PORT" write_flash "$APP_OFFSET" "$APP_BIN"
  echo "==> Flashing bleota-s3 @ $BLEOTA_OFFSET"
  python3 "$ESPTOOL" --port "$PORT" write_flash "$BLEOTA_OFFSET" "$BLEOTA_BIN"
  echo "==> Flashing littlefs @ $LFS_OFFSET"
  python3 "$ESPTOOL" --port "$PORT" write_flash "$LFS_OFFSET" "$LFS_BIN"
else
  echo "==> OTA update: writing app partition only @ $APP_OFFSET (littlefs preserved)"
  python3 "$ESPTOOL" --port "$PORT" write_flash "$APP_OFFSET" "$UPDATE_BIN"
fi

echo
echo "==> Done. Reset the board (unplug/replug USB) and connect via the Meshtastic app or:"
echo "    pio device monitor -p $PORT -b 115200"
