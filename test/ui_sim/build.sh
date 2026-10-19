#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# stdout is only the absolute executable path, for SERIN_SIM_BIN=$(...).
set -euo pipefail
board=${1:-viewe15}
case "$board" in viewe15|viewe21) ;; *) echo 'Board must be viewe15 or viewe21' >&2; exit 2;; esac
root=$(cd "$(dirname "$0")/../.." && pwd)
for prerequisite in python3 make "${CC:-cc}" sdl2-config; do
    command -v "$prerequisite" >/dev/null || {
        echo "UI simulator prerequisite missing: $prerequisite (see test/ui_sim/README.md)" >&2; exit 2;
    }
done
python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' || {
    echo 'UI simulator prerequisite missing: Python 3.11 or newer' >&2; exit 2;
}
build=${SERIN_SIM_BUILD_DIR:-$root/sim/build}
mkdir -p "$build"
build=$(cd "$build" && pwd)
lvgl=$(python3 "$root/sim/prepare_lvgl.py" "${SERIN_SIM_CACHE:-$root/sim/.cache}")
make -C "$root/sim" --no-print-directory -j"${SERIN_SIM_JOBS:-8}" ROOT="$root" LVGL="$lvgl" BOARD="$board" BUILD="$build/$board" >&2
printf '%s\n' "$build/$board/serin-sim"
