#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="$PROJECT_ROOT/runtime/linux"
ARCHIVE_PATH="$RUNTIME_DIR/zima-cad-runtime-linux.tar.gz"
TARGET_DIR="$RUNTIME_DIR/python"

if [[ ! -f "$ARCHIVE_PATH" ]]; then
    echo "Runtime archive not found: $ARCHIVE_PATH" >&2
    exit 1
fi

if [[ -e "$TARGET_DIR" ]]; then
    if [[ "${1:-}" != "--force" ]]; then
        echo "Target already exists: $TARGET_DIR. Re-run with --force to replace it." >&2
        exit 1
    fi
    rm -rf -- "$TARGET_DIR"
fi

mkdir -p "$TARGET_DIR"
tar -xzf "$ARCHIVE_PATH" -C "$TARGET_DIR"

UNPACK_SCRIPT="$TARGET_DIR/bin/conda-unpack"
if [[ -x "$UNPACK_SCRIPT" ]]; then
    PATH="$TARGET_DIR/bin:$PATH" "$UNPACK_SCRIPT"
fi

echo "Linux runtime unpacked to:"
echo "$TARGET_DIR"
