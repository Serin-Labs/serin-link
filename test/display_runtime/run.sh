#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
bin=$(mktemp /tmp/t_display_runtime.XXXXXX)
trap 'rm -f "$bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -pthread -Istub -I../../main \
    test.c ../../main/display_runtime.c -o "$bin"
"$bin"
"$bin" --polling
"$bin" --startup-timeout
"$bin" --stop-error
