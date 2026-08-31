#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
trap 'rm -f /tmp/test_zone_badge' EXIT
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -I../../main \
    test_zone_badge.c -o /tmp/test_zone_badge
/tmp/test_zone_badge
