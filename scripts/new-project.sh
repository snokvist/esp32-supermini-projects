#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE_DIR="$ROOT_DIR/templates/platformio-project"
PROJECTS_DIR="$ROOT_DIR/projects"

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "Usage: $0 <project-name> [board] [env-name]"
  echo "Example: $0 blink-led esp32-c3-devkitm-1 esp32c3_supermini"
  exit 1
fi

PROJECT_NAME="$1"
BOARD="${2:-esp32-c3-devkitm-1}"
ENV_NAME="${3:-esp32c3_supermini}"
CREATED_DATE="$(date +%Y-%m-%d)"
TARGET_DIR="$PROJECTS_DIR/$PROJECT_NAME"

if [[ ! -d "$TEMPLATE_DIR" ]]; then
  echo "Template directory not found: $TEMPLATE_DIR"
  exit 1
fi

if [[ -e "$TARGET_DIR" ]]; then
  echo "Target already exists: $TARGET_DIR"
  exit 1
fi

mkdir -p "$PROJECTS_DIR"
cp -R "$TEMPLATE_DIR" "$TARGET_DIR"

# Apply project-specific defaults into platformio.ini.
sed -i "s/__ENV_NAME__/$ENV_NAME/g" "$TARGET_DIR/platformio.ini"
sed -i "s/__BOARD__/$BOARD/g" "$TARGET_DIR/platformio.ini"

# Apply template placeholders in project files.
sed -i "s/__PROJECT_NAME__/$PROJECT_NAME/g" "$TARGET_DIR/src/main.cpp"
sed -i "s/__PROJECT_NAME__/$PROJECT_NAME/g" "$TARGET_DIR/README.md"
sed -i "s/__PROJECT_NAME__/$PROJECT_NAME/g" "$TARGET_DIR/docs/TASK_LOG.md"
sed -i "s/__PROJECT_NAME__/$PROJECT_NAME/g" "$TARGET_DIR/docs/HARDWARE.md"
sed -i "s/__BOARD__/$BOARD/g" "$TARGET_DIR/README.md"
sed -i "s/__BOARD__/$BOARD/g" "$TARGET_DIR/docs/TASK_LOG.md"
sed -i "s/__BOARD__/$BOARD/g" "$TARGET_DIR/docs/HARDWARE.md"
sed -i "s/__ENV_NAME__/$ENV_NAME/g" "$TARGET_DIR/README.md"
sed -i "s/__ENV_NAME__/$ENV_NAME/g" "$TARGET_DIR/docs/TASK_LOG.md"
sed -i "s/__ENV_NAME__/$ENV_NAME/g" "$TARGET_DIR/docs/HARDWARE.md"
sed -i "s/__CREATED_DATE__/$CREATED_DATE/g" "$TARGET_DIR/docs/TASK_LOG.md"

cat <<MSG
Created project: $TARGET_DIR
Board: $BOARD
Environment: $ENV_NAME

Next steps:
  cd projects/$PROJECT_NAME
  pio run
  pio run -t upload
  pio device monitor
  # docs: README.md, docs/TASK_LOG.md, docs/HARDWARE.md
MSG
