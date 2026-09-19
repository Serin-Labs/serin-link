#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
#
# Every LVGL call on the display bring-up path must hold the esp_lvgl_port
# mutex. Once lvgl_port_init() has run, taskLVGL renders at priority 4 on
# either core, and lv_display_create() arms an immediate first refresh — an
# unguarded lv_obj_*() there trips LVGL's invalidate-during-render assert
# (lv_refr.c), which in this build is a silent while(1) and then a task
# watchdog panic. It boots most of the time, which is what makes it nasty.
#
# Only direct lv_*() calls are visible to this check; LVGL work hidden behind
# a helper still has to lock itself (see perf_stats_init).
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)

# file:marker — scanning starts after the marker line and stops at the end of
# the enclosing function (a '}' in column 0).
checks=(
    "main/bsp_viewe.c:lvgl_port_init("
    "main/bsp_viewe21.c:lvgl_port_init("
    "main/dial_main.c:bsp_display_start("
    "main/perf_stats.c:void perf_stats_init("
)

status=0
for check in "${checks[@]}"; do
    file=${check%%:*}
    marker=${check#*:}
    awk -v file="$file" -v marker="$marker" '
        # strip comments so prose about lv_*() does not read as a call
        {
            code = $0
            sub(/\/\/.*/, "", code)
            if (code ~ /^[ \t]*(\*|\/\*)/) code = ""
        }
        !armed {
            if (index(code, marker)) { armed = 1; ever_armed = 1 }
            next
        }
        /^}/ { armed = 0; next }
        index(code, "lvgl_port_lock(")   { depth++ }
        index(code, "lvgl_port_unlock(") { depth-- }
        depth == 0 && code ~ /(^|[^A-Za-z0-9_])lv_[a-z0-9_]+\(/ {
            sub(/^[ \t]+/, "", code)
            printf "%s:%d: LVGL call outside lvgl_port_lock: %s\n", file, FNR, code > "/dev/stderr"
            bad = 1
        }
        END {
            if (!ever_armed) {
                printf "%s: marker never matched, check is dead: %s\n", file, marker > "/dev/stderr"
                bad = 1
            }
            exit bad
        }
    ' "$root/$file" || status=1
done

if [ "$status" -ne 0 ]; then
    echo 'unguarded LVGL access on the display bring-up path' >&2
    exit 1
fi

echo 'lvgl_lock: OK'
