#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")"
root=$(cd ../.. && pwd)
tmp=$(mktemp -d /tmp/t_knob_power_save.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
source=${1:-"$tmp/iot_knob.c"}
if [ $# -eq 0 ]; then
    python3 "$root/tools/patch_knob_power_save.py" \
        "$root/test/knob_power_save/upstream/iot_knob.c" "$source"
fi
gcc -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-unused-parameter \
    -Istub -I"$root/test/knob_power_save/upstream/include" \
    -DKNOB_DRIVER_SOURCE="\"$source\"" \
    -DCONFIG_KNOB_PERIOD_TIME_MS=3 -DCONFIG_KNOB_DEBOUNCE_TICKS=2 \
    -DCONFIG_KNOB_HIGH_LIMIT=1000 -DCONFIG_KNOB_LOW_LIMIT=-1000 \
    test.c -o "$tmp/test"
if [ $# -gt 1 ]; then
    "$tmp/test" "$2"
else
    "$tmp/test"
    "$tmp/test" --early-sample
    "$tmp/test" --timer-failure
    # Exercise the production CMake helper with a real compiled component target.
    # No managed_components directory is needed by this host/CI regression.
    cp -R upstream "$tmp/upstream"
    cmake -S cmake -B "$tmp/build" -DSERIN_UPSTREAM_DIR="$tmp/upstream" >"$tmp/configure.log" 2>&1
    cmake --build "$tmp/build" >"$tmp/build.log" 2>&1
    cmake -S cmake -B "$tmp/build" -DCONFIG_SERIN_INPUT_POWER_SAVE=OFF >>"$tmp/configure.log" 2>&1
    cmake --build "$tmp/build" >>"$tmp/build.log" 2>&1
    cmake -S cmake -B "$tmp/build" -DCONFIG_SERIN_INPUT_POWER_SAVE=ON >>"$tmp/configure.log" 2>&1
    cmake --build "$tmp/build" >>"$tmp/build.log" 2>&1
    cp "$tmp/build/serin_patches/knob/iot_knob.c" "$tmp/accepted.c"
    printf '\n/* unexpected upstream change */\n' >>"$tmp/upstream/iot_knob.c"
    # CMAKE_CONFIGURE_DEPENDS must force patch validation again on plain build.
    if cmake --build "$tmp/build" >"$tmp/drift.log" 2>&1; then
        echo 'FAIL: changed upstream source was accepted' >&2
        exit 1
    fi
    if ! python3 -c 'import pathlib, sys; sys.exit("SHA256 changed" not in pathlib.Path(sys.argv[1]).read_text())' "$tmp/drift.log"; then
        cat "$tmp/drift.log" >&2
        exit 1
    fi
    cmp "$tmp/accepted.c" "$tmp/build/serin_patches/knob/iot_knob.c"
    echo 'knob_power_save: CMake baseline toggles and fail-closed rebuild OK'
fi
