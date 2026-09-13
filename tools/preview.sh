#!/usr/bin/env bash
# Render one UI scenario (maps|stale|passkey|idle) to preview-<scenario>.png in $OUT (default /tmp).
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="${OUT:-/tmp}"
bin="$(mktemp /tmp/pocketmate-preview.XXXXXX)"
trap 'rm -f -- "$bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O1 -I "$repo_dir/firmware/main" \
    "$repo_dir/firmware/main/gfx.c" "$repo_dir/firmware/main/fonts.c" "$repo_dir/firmware/main/ui.c" \
    "$repo_dir/tests/render_preview.c" -o "$bin"
for scenario in "$@"; do
    "$bin" "$scenario" > "$out/preview-$scenario.ppm"
    python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])" \
        "$out/preview-$scenario.ppm" "$out/preview-$scenario.png"
    echo "$out/preview-$scenario.png"
done
