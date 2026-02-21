#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="esp32-s3-devkitc-1"
OUT_ROOT="release-artifacts"
TAG=""
SKIP_BUILD=0

usage() {
    cat <<'EOF'
Usage:
  ./scripts/make_fullflash.sh [options]

Options:
  --env <platformio-env>   PlatformIO environment (default: esp32-s3-devkitc-1)
  --tag <tag>              Output folder tag (default: timestamp)
  --skip-build             Reuse existing build output
  -h, --help               Show help

Output:
  release-artifacts/<tag>/fullflash.bin
  release-artifacts/<tag>/firmware.bin
EOF
}

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
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            usage
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

BUILD_DIR=".pio/build/$ENV_NAME"
BOOTLOADER_BIN="$BUILD_DIR/bootloader.bin"
PARTITIONS_BIN="$BUILD_DIR/partitions.bin"
FIRMWARE_BIN="$BUILD_DIR/firmware.bin"
BOOT_APP0_BIN="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
ESPTOOL_PY="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"

for required in "$BOOTLOADER_BIN" "$PARTITIONS_BIN" "$FIRMWARE_BIN" "$BOOT_APP0_BIN" "$ESPTOOL_PY"; do
    if [[ ! -f "$required" ]]; then
        echo "Missing required file: $required"
        exit 1
    fi
done

OUT_DIR="$OUT_ROOT/$TAG"
mkdir -p "$OUT_DIR"
cp "$FIRMWARE_BIN" "$OUT_DIR/firmware.bin"

python3 "$ESPTOOL_PY" --chip esp32s3 merge_bin -o "$OUT_DIR/fullflash.bin" \
    0x0 "$BOOTLOADER_BIN" \
    0x8000 "$PARTITIONS_BIN" \
    0xe000 "$BOOT_APP0_BIN" \
    0x10000 "$FIRMWARE_BIN"

cat > "$OUT_DIR/FLASH_WINDOWS.txt" <<EOF
Easy flash (Windows, COM5):

Option A - esptool command (single file):
python -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 fullflash.bin

Option B - Espressif Flash Download Tool:
- Select fullflash.bin at address 0x0
- Select COM5 and flash
EOF

echo "Created: $OUT_DIR/fullflash.bin"
echo "Created: $OUT_DIR/firmware.bin"
