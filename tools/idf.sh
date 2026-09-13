#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_idf_dir="${POCKETMATE_IDF_DIR:-$HOME/.local/share/pocketmate/esp-idf}"
if [[ ! -f "$task_idf_dir/export.sh" ]]; then
    echo "ESP-IDF v5.5.1 missing. See docs/ancs-prototype.vi.md." >&2
    exit 1
fi
source "$task_idf_dir/export.sh" >/dev/null
cd "$repo_dir/firmware"
exec idf.py "$@"
