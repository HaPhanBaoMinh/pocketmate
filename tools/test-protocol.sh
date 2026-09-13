#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp /tmp/pocketmate-ancs-test.XXXXXX)"
trap 'rm -f -- "$test_binary"' EXIT
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I "$repo_dir/firmware/main" \
    "$repo_dir/firmware/main/ancs_protocol.c" "$repo_dir/tests/test_ancs_protocol.c" \
    -o "$test_binary"
"$test_binary"
