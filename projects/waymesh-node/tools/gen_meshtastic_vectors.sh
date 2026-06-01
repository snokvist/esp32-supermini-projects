#!/usr/bin/env bash
#
# Generate test/test_channel_hash/vectors_channel.h from upstream Meshtastic at
# the pinned ref (doc 13 §10: "generate-with, don't copy-from" — upstream is
# GPL-3.0). Clones meshtastic/firmware, extracts the verbatim defaultpsk[] key
# constant, builds tools/gen_vectors_main.c against it, and runs it.
#
# The committed repo holds only the generator + the produced vectors header;
# the GPL clone lives in a temp dir and is never committed.
#
# Usage: tools/gen_meshtastic_vectors.sh
set -euo pipefail

# --- pinned upstream (keep in sync with WAYMESH_FW_VERSION in ble_gatt.cpp) ---
FW_TAG="v2.6.4.b89355f"
FW_SHA="b89355ffa60b3893417004b07e7b96f04b17022c"
FW_REPO="https://github.com/meshtastic/firmware.git"
REF_STR="meshtastic/firmware ${FW_TAG} (${FW_SHA})"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
OUT="$PROJ_DIR/test/test_channel_hash/vectors_channel.h"

CLONE="${WMGEN_CLONE:-/tmp/mtfw}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# 1. Clone upstream at the pinned tag (reuse an existing clone if it matches).
if [ -d "$CLONE/.git" ] && [ "$(git -C "$CLONE" rev-parse HEAD)" = "$FW_SHA" ]; then
  echo "Reusing upstream clone at $CLONE ($FW_SHA)"
else
  echo "Cloning $FW_REPO @ $FW_TAG ..."
  rm -rf "$CLONE"
  git clone --depth 1 --branch "$FW_TAG" --no-recurse-submodules "$FW_REPO" "$CLONE" >/dev/null 2>&1
fi
HEAD_SHA="$(git -C "$CLONE" rev-parse HEAD)"
[ "$HEAD_SHA" = "$FW_SHA" ] || { echo "ERROR: clone HEAD $HEAD_SHA != pinned $FW_SHA" >&2; exit 1; }

# 2. Extract the verbatim defaultpsk[] (16 bytes) from upstream Channels.h.
CH_H="$CLONE/src/mesh/Channels.h"
PSK_BYTES="$(awk '/static const uint8_t defaultpsk\[\] =/{f=1} f{printf "%s ", $0} /};/{if(f){exit}}' "$CH_H" \
            | grep -oiE '0x[0-9a-f]{2}' | tr '[:upper:]' '[:lower:]' | paste -sd, -)"
COUNT="$(printf '%s' "$PSK_BYTES" | tr ',' '\n' | grep -c .)"
[ "$COUNT" = "16" ] || { echo "ERROR: extracted $COUNT defaultpsk bytes, expected 16" >&2; exit 1; }
# Sanity: the famous default key.
EXPECT="0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01"
[ "$PSK_BYTES" = "$EXPECT" ] || { echo "ERROR: defaultpsk mismatch: $PSK_BYTES" >&2; exit 1; }

cat > "$WORK/upstream_defaultpsk.h" <<EOF
/* GENERATED at vector-gen time from $REF_STR (src/mesh/Channels.h). */
#define UPSTREAM_REF "$REF_STR"
static const uint8_t UPSTREAM_DEFAULTPSK[16] = { $PSK_BYTES };
EOF

# 3. Build + run the generator.
cc -O2 -Wall -I"$WORK" "$SCRIPT_DIR/gen_vectors_main.c" -o "$WORK/gen"
mkdir -p "$(dirname "$OUT")"
"$WORK/gen" "$OUT"
echo "Wrote $OUT"
