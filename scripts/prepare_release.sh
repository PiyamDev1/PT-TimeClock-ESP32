#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="esp32-s3-devkitc-1"
OUT_ROOT="release-artifacts"
TAG=""
SKIP_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --env)
            ENV_NAME="${2:-}"
            shift 2
            ;;
        --tag)
            TAG="${2:-}"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 [--env <platformio-env>] [--tag <vX.Y.Z>] [--skip-build]"
            exit 1
            ;;
    esac
done

if [[ -z "$TAG" ]]; then
    TAG="$(date +%Y%m%d-%H%M%S)"
fi

if command -v pio >/dev/null 2>&1; then
    PIO_CMD="pio"
elif [[ -x "$HOME/.local/bin/pio" ]]; then
    PIO_CMD="$HOME/.local/bin/pio"
else
    echo "PlatformIO CLI not found. Install first (python3 -m pip install --user platformio)."
    exit 1
fi

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    "$PIO_CMD" run -e "$ENV_NAME"
fi

SRC_BIN=".pio/build/$ENV_NAME/firmware.bin"
if [[ ! -f "$SRC_BIN" ]]; then
    echo "Firmware not found: $SRC_BIN"
    exit 1
fi

OUT_DIR="$OUT_ROOT/$TAG"
mkdir -p "$OUT_DIR"
cp "$SRC_BIN" "$OUT_DIR/firmware.bin"

cat > "$OUT_DIR/README.txt" <<EOF
Release artifact for tag: $TAG
PlatformIO environment: $ENV_NAME

Upload this file to GitHub Release assets with exact name:
firmware.bin
EOF

echo "Prepared OTA artifact: $OUT_DIR/firmware.bin"
