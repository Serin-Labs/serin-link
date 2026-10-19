#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")"
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I../../main test.c -o "$bin"
"$bin"
