#!/usr/bin/env bash
set -euo pipefail

archive_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd -- "$archive_root/../.." && pwd)"
bundled_python="$repository_root/runtime/linux/python/bin/python"

export ZIMA_CAD_APPLICATION_ROOT="$repository_root"
export PYTHONPATH="$archive_root${PYTHONPATH:+:$PYTHONPATH}"

if [[ -x "$bundled_python" ]]; then
    exec "$bundled_python" "$archive_root/main.py" "$@"
fi

exec python3 "$archive_root/main.py" "$@"
