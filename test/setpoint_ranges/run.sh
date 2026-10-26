#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
build=$(mktemp -d); trap 'rm -rf "$build"' EXIT
python3 extract_client.py "$build/commands.inc"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -I../../main -I"$build" \
    test_setpoint_ranges.c -o "$build/check" -lm
"$build/check"
