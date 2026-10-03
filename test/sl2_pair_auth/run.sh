#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
pair_test_dir=$(mktemp -d /tmp/serin-pair-auth.XXXXXX)
trap 'rm -rf "$pair_test_dir"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -I../../main test.c -o "$pair_test_dir/test" -lm
"$pair_test_dir/test"
