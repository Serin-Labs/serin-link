#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
# Isolate outputs so concurrent checkouts cannot overwrite one another.
work=$(mktemp -d "${TMPDIR:-/tmp}/sl2-public-compat.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
(cd upstream && sha256sum --check SHA256SUMS)
for peer in dial controller; do
    include=../../main
    [ "$peer" != controller ] || include=upstream
    gcc -std=c11 -Wall -Wextra -Werror -I"$include" probe.c -o "$work/$peer" -lm
    "$work/$peer" > "$work/$peer.txt"
done
diff -u --label public-controller --label dial "$work/controller.txt" "$work/dial.txt"
echo 'sl2_public_compat: shared layouts, constants, and wire vectors match the pinned public controller'
