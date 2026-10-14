#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
bin=$(mktemp /tmp/t_perf_window.XXXXXX)
trap 'rm -f "$bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -I../../main test.c -o "$bin"
"$bin"
