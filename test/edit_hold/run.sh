#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
trap 'rm -f /tmp/test_edit_hold' EXIT
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -I../../main \
    test_edit_hold.c -o /tmp/test_edit_hold
/tmp/test_edit_hold
python3 test_client_edits.py
