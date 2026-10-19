#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build and run every applicable public UI regression, for both panels by default.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
case "${1:-all}" in
    all) boards=(viewe15 viewe21) ;;
    viewe15|viewe21) boards=("$1") ;;
    *) echo 'Usage: test/ui_sim/run-all.sh [all|viewe15|viewe21]' >&2; exit 2 ;;
esac
python3 -c 'from PIL import Image' 2>/dev/null || {
    echo 'UI test prerequisite missing: Python 3 with Pillow (see test/ui_sim/README.md)' >&2; exit 2;
}
for board in "${boards[@]}"; do
    export SERIN_SIM_BIN
    SERIN_SIM_BIN=$("$root/test/ui_sim/build.sh" "$board")
    for check in navigation_wifi_modes wifi_cancel room_source name_fonts airflow advanced settings_touch child_lock coach_taps home_taps; do
        printf '\nUI %s: %s\n' "$board" "$check"
        "$root/test/ui_sim/$check.sh"
    done
    printf '\nUI %s: all 10 checks passed\n' "$board"
done
