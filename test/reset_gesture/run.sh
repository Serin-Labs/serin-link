#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
trap 'rm -f /tmp/t_reset_gesture' EXIT
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -DHOST_TEST -I../../main \
    test.c ../../main/factory_reset.c -o /tmp/t_reset_gesture
/tmp/t_reset_gesture
