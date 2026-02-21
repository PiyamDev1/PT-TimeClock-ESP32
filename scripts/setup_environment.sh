#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

log() {
  printf '[setup] %s\n' "$*"
}

ensure_python() {
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required but not found." >&2
    exit 1
  fi
}

ensure_pio() {
  if command -v pio >/dev/null 2>&1; then
    echo "pio"
    return
  fi

  local fallback="$HOME/.local/bin/pio"
  if [[ -x "$fallback" ]]; then
    echo "$fallback"
    return
  fi

  log "PlatformIO CLI not found; installing with pip --user"
  python3 -m pip install --user platformio

  if [[ -x "$fallback" ]]; then
    echo "$fallback"
    return
  fi

  echo "PlatformIO install completed, but pio binary is still not found." >&2
  echo "Ensure ~/.local/bin is in PATH, then re-run this script." >&2
  exit 1
}

main() {
  ensure_python
  local pio_cmd
  pio_cmd="$(ensure_pio)"

  log "Using PlatformIO CLI: $pio_cmd"

  cd "$ROOT_DIR"

  if [[ ! -f local.env && -f local.env.example ]]; then
    cp local.env.example local.env
    log "Created local.env from local.env.example"
  fi

  log "Installing PlatformIO package dependencies"
  "$pio_cmd" pkg install -e esp32-s3-devkitc-1

  log "Environment setup complete"
  log "Next: edit local.env (if needed), then run: $pio_cmd run -e esp32-s3-devkitc-1"
}

main "$@"
