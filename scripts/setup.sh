#!/usr/bin/env bash
set -euo pipefail

PIO_VENV="${PIO_VENV:-$HOME/.local/share/pio-venv}"
LOCAL_BIN="${LOCAL_BIN:-$HOME/.local/bin}"

shell_name="$(basename "${SHELL:-bash}")"
case "$shell_name" in
  bash) SHELL_RC="${SHELL_RC:-$HOME/.bashrc}" ;;
  zsh) SHELL_RC="${SHELL_RC:-$HOME/.zshrc}" ;;
  *) SHELL_RC="${SHELL_RC:-$HOME/.profile}" ;;
esac

PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'
added_path_line="false"

echo "[setup] PlatformIO venv: $PIO_VENV"
python3 -m venv "$PIO_VENV"
"$PIO_VENV/bin/pip" install --upgrade pip
"$PIO_VENV/bin/pip" install --upgrade platformio

mkdir -p "$LOCAL_BIN"
ln -sf "$PIO_VENV/bin/pio" "$LOCAL_BIN/pio"

mkdir -p "$(dirname "$SHELL_RC")"
touch "$SHELL_RC"
if ! grep -Fqx "$PATH_LINE" "$SHELL_RC"; then
  printf '\n%s\n' "$PATH_LINE" >> "$SHELL_RC"
  added_path_line="true"
fi

# Make pio available in this running shell immediately.
if [[ ":$PATH:" != *":$LOCAL_BIN:"* ]]; then
  export PATH="$LOCAL_BIN:$PATH"
fi

if ! command -v pio >/dev/null 2>&1; then
  echo "[setup] ERROR: pio still not found in PATH after setup." >&2
  echo "[setup] Try: export PATH=\"$LOCAL_BIN:\$PATH\"" >&2
  exit 1
fi

echo "[setup] pio path: $(command -v pio)"
pio --version

# Reload shell config as part of setup.
# If this script is sourced, it updates the current shell.
# If executed normally, it only affects this process.
# shellcheck disable=SC1090
source "$SHELL_RC" || true

if [[ "$added_path_line" == "true" ]]; then
  echo "[setup] Added PATH update to: $SHELL_RC"
else
  echo "[setup] PATH update already present in: $SHELL_RC"
fi

if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
  echo "[setup] Done. $SHELL_RC sourced in current shell."
else
  echo "[setup] Done. $SHELL_RC sourced in setup process."
  echo "[setup] To update this terminal now, run: source ./scripts/setup.sh"
fi
