#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
# Link the production edit/CAPS/STATE paths with the current real UI objects.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
sim=${SERIN_SIM_BIN:?Set SERIN_SIM_BIN using test/ui_sim/build.sh}
build=$(dirname "$sim")
case "$build" in */viewe15) board_def=CONFIG_SERIN_BOARD_VIEWE_15 ;; */viewe21) board_def=CONFIG_SERIN_BOARD_VIEWE_21 ;; *) echo 'Unknown simulator board' >&2; exit 2 ;; esac
scratch=$(mktemp -d); trap 'rm -rf "$scratch"' EXIT
python3 "$root/test/setpoint_ranges/extract_client.py" "$scratch/commands.inc" ui
lvgl=$(python3 "$root/sim/prepare_lvgl.py" "${SERIN_SIM_CACHE:-$root/sim/.cache}")
objects=()
for obj in "$build"/*.o; do
    [[ "$obj" == *sim_main.c.o ]] || objects+=("$obj")
done
cc -std=gnu17 -Wall -Wextra -Werror -Wno-unused-function \
    -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE=1 -D"$board_def"=1 -DSERIN_SIM=1 \
    -I"$root/sim" -I"$root/sim/stubs" -I"$root/main" -I"$lvgl" -I"$scratch" \
    $(sdl2-config --cflags) "$root/test/ui_sim/canonical_band_gap.c" "${objects[@]}" \
    $(sdl2-config --libs) -lm -o "$scratch/check"
(cd "$scratch" && ./check)
