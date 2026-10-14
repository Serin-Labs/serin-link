#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")"
bin=$(mktemp /tmp/t_perf_stats.XXXXXX)
trap 'rm -f "$bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -Dprintf=test_printf -Dputs=test_puts \
    -Istub -I../../main test.c ../../main/perf_stats.c -o "$bin"
"$bin"
