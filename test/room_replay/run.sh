#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
test_dir=$(mktemp -d /tmp/serin-room-replay.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I../../main test.c -o "$test_dir/test" -lm
"$test_dir/test"
