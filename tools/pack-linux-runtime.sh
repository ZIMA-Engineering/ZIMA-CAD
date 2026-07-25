#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="$PROJECT_ROOT/runtime/linux"
ARCHIVE_PATH="$RUNTIME_DIR/zima-cad-runtime-linux.tar.gz"
ENVIRONMENT_NAME="${ZIMA_CAD_ENVIRONMENT:-zima-cad}"

mkdir -p "$RUNTIME_DIR"

if command -v conda-pack >/dev/null 2>&1; then
    CONDA_PACK=(conda-pack)
elif command -v mamba >/dev/null 2>&1; then
    CONDA_PACK=(mamba run -n "$ENVIRONMENT_NAME" conda-pack)
else
    echo "Conda or mamba was not found. Activate Miniforge first." >&2
    exit 1
fi

"${CONDA_PACK[@]}" -n "$ENVIRONMENT_NAME" -o "$ARCHIVE_PATH" --force

echo "Linux runtime archive written to:"
echo "$ARCHIVE_PATH"
