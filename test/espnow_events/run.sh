#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
test_bin=$(mktemp /tmp/test_espnow_events.XXXXXX)
trap 'rm -f "$test_bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Istub -I../../main test_espnow_events.c -o "$test_bin"
"$test_bin"
