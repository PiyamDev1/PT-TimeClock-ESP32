#!/usr/bin/env bash
set -euo pipefail

TAG=""
ENV_NAME="esp32-s3-devkitc-1"
REPO=""
TITLE=""
NOTES=""
SKIP_BUILD=0

usage() {
    cat <<'EOF'
Usage:
  ./scripts/publish_release.sh --tag <vX.Y.Z> [options]

Options:
  --tag <tag>           Release tag (required)
  --env <pio-env>       PlatformIO environment (default: esp32-s3-devkitc-1)
  --repo <owner/name>   GitHub repo (default: current gh repo)
  --title <title>       Release title (default: tag)
  --notes <text>        Release notes body
  --skip-build          Reuse existing firmware build
  -h, --help            Show this help

Behavior:
  1) Prepares OTA artifact via scripts/prepare_release.sh
  2) Creates release if missing
  3) Uploads/overwrites asset as firmware.bin
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)
            TAG="${2:-}"
            shift 2
            ;;
        --env)
            ENV_NAME="${2:-}"
            shift 2
            ;;
        --repo)
            REPO="${2:-}"
            shift 2
            ;;
        --title)
            TITLE="${2:-}"
            shift 2
            ;;
        --notes)
            NOTES="${2:-}"
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
    echo "Error: --tag is required"
    usage
    exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
    echo "GitHub CLI (gh) is not installed."
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "GitHub CLI is not authenticated. Run: gh auth login"
    exit 1
fi

PREP_CMD=("./scripts/prepare_release.sh" "--tag" "$TAG" "--env" "$ENV_NAME")
if [[ "$SKIP_BUILD" -eq 1 ]]; then
    PREP_CMD+=("--skip-build")
fi

"${PREP_CMD[@]}"

ARTIFACT_PATH="release-artifacts/$TAG/firmware.bin"
if [[ ! -f "$ARTIFACT_PATH" ]]; then
    echo "Artifact missing: $ARTIFACT_PATH"
    exit 1
fi

if [[ -z "$TITLE" ]]; then
    TITLE="$TAG"
fi

REPO_ARGS=()
if [[ -n "$REPO" ]]; then
    REPO_ARGS=("--repo" "$REPO")
fi

if gh release view "$TAG" "${REPO_ARGS[@]}" >/dev/null 2>&1; then
    echo "Release $TAG already exists."
else
    CREATE_ARGS=("$TAG" "--title" "$TITLE")
    if [[ -n "$NOTES" ]]; then
        CREATE_ARGS+=("--notes" "$NOTES")
    else
        CREATE_ARGS+=("--notes" "OTA release $TAG")
    fi
    gh release create "${CREATE_ARGS[@]}" "${REPO_ARGS[@]}"
fi

gh release upload "$TAG" "$ARTIFACT_PATH#firmware.bin" --clobber "${REPO_ARGS[@]}"

echo "Release publish complete for $TAG"
