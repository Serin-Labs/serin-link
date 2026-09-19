#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
trap 'rm -f /tmp/test_wifi_sta' EXIT
cd "$(dirname "$0")"
# stub/ stands in for the ESP-IDF headers, so main/wifi_sta.c compiles as-is.
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Istub -I../../main test_wifi_sta.c ../../main/wifi_sta.c -o /tmp/test_wifi_sta
/tmp/test_wifi_sta
