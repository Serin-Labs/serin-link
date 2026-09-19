#!/usr/bin/env bash
# Capability-driven Airflow regression.  The simulator is supplied by the
# caller so this test never depends on a particular board build location.
set -euo pipefail

: "${SERIN_SIM_BIN:?set SERIN_SIM_BIN to a serin-sim executable}"
[ -x "$SERIN_SIM_BIN" ] || { echo "SERIN_SIM_BIN is not executable: $SERIN_SIM_BIN" >&2; exit 2; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/serin-airflow.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

run() { "$SERIN_SIM_BIN" --headless --run "$1"; }
need() {
    local text=$1 pattern=$2
    grep -Fq -- "$pattern" <<<"$text" || {
        echo "FAIL: expected $pattern" >&2
        printf '%s\n' "$text" >&2
        exit 1
    }
}

# Each process starts with a clean simulator state.  The eight combinations
# must select and directly edit the first real control, including
# horizontal-only (the old 0/1/2 vane configuration made that unavailable).
check_face() {
    local fan=$1 vv=$2 hh=$3 expected=$4 screen=$5
    local out
    out=$(run "bridge 1 1 -50; wait 60; state fan=60 vvane=1 hvane=left; caps fan_steps=$fan vane_v=$vv vane_h=$hh; wait 60; screen fanvane; wait 120; turn 1; wait 100; info")
    need "$out" "$expected"
    need "$out" "info screen=$screen"
}

check_face 0 0    0    "info screen=thermostat" thermostat
check_face 5 0    0    "fan=80"                 fanvane
check_face 0 0x12 0    "vvane=2"                fanvane
check_face 0 0    0x02 "hvane=2"                fanvane
check_face 5 0x12 0    "fan=80"                 fanvane
check_face 5 0    0x02 "fan=80"                 fanvane
check_face 0 0x12 0x02 "vvane=2"                fanvane
check_face 5 0x12 0x02 "fan=80"                 fanvane

# A lone horizontal vane edits immediately and returns directly.  Moving it
# must send an H-vane command, rather than falling through to the thermostat.
out=$(run "bridge 1 1 -50; wait 60; caps fan_steps=0 vane_v=0 vane_h=0x02; state hvane=left; screen fanvane; wait 100; turn 1; wait 100; press; wait 100; info")
need "$out" "hvane=2"
need "$out" "info screen=thermostat"

# A caps update preserves a still-valid focused control, replaces one that
# vanished, and ultimately leaves Airflow when every control is removed.
out=$(run "bridge 1 1 -50; wait 60; state hvane=left; caps fan_steps=5 vane_v=0x12 vane_h=0x02; screen fanvane; wait 100; press; turn 2; press; wait 80; caps fan_steps=0 vane_v=0 vane_h=0x02; wait 100; turn 1; wait 80; caps fan_steps=0 vane_v=0 vane_h=0; wait 120; info")
need "$out" "hvane=2"
need "$out" "info screen=thermostat"
out=$(run "bridge 1 1 -50; wait 60; state vvane=1; caps fan_steps=5 vane_v=0x12 vane_h=0x02; screen fanvane; wait 100; caps fan_steps=0 vane_v=0x12 vane_h=0; wait 100; turn 1; wait 80; info")
need "$out" "vvane=2"

# Vane tiles slide along their drawn axis: vertical up/down, horizontal
# left/right.  Steps clamp at the ends of the knob's cycle, a slide is not
# also a tap, and a slide across the axis changes nothing.
last_cmd() { grep '^CMD:' <<<"$1" | tail -1; }
need_last() {
    local text=$1 pattern=$2 last
    last=$(last_cmd "$text")
    grep -Fq -- "$pattern" <<<"$last" || {
        echo "FAIL: expected last command with $pattern, got: ${last:-<none>}" >&2
        printf '%s\n' "$text" >&2
        exit 1
    }
}
disp=$(run hitmap | sed -n 's/.*disp=\([0-9]*\).*/\1/p')
c=$((disp / 2))
vv="bridge 1 1 -50; wait 60; caps fan_steps=0 vane_v=0x35 vane_h=0; wait 60; screen fanvane; wait 200;"
hh="bridge 1 1 -50; wait 60; caps fan_steps=0 vane_v=0 vane_h=0x25; wait 60; screen fanvane; wait 200;"

out=$(run "state vvane=1; $vv swipe $c $((c - 80)) $c $((c + 40)) 300; wait 150")
need_last "$out" "vvane=4"
out=$(run "state vvane=3; $vv swipe $c $((c + 100)) $c $((c - 100)) 300; swipe $c $((c + 100)) $c $((c - 100)) 300; wait 150")
need_last "$out" "vvane=0"
out=$(run "state vvane=3; $vv swipe $c $((c - 100)) $c $((c + 100)) 300; swipe $c $((c - 100)) $c $((c + 100)) 300; wait 150")
need_last "$out" "vvane=6"
out=$(run "state vvane=3; $vv swipe $((c - 80)) $c $((c + 80)) $c 300; wait 150")
[ -z "$(last_cmd "$out")" ] || { echo "FAIL: sideways slide changed the vertical vane" >&2; printf '%s\n' "$out" >&2; exit 1; }
out=$(run "state vvane=3; $vv tap $c $c; wait 150")
need_last "$out" "vvane=4"

out=$(run "state hvane=left; $hh swipe $((c - 80)) $c $((c + 40)) $c 300; wait 150")
need_last "$out" "hvane=4"
out=$(run "state hvane=center; $hh swipe $((c + 100)) $c $((c - 100)) $c 300; wait 150")
need_last "$out" "hvane=1"
out=$(run "state hvane=center; $hh swipe $((c - 100)) $c $((c + 100)) $c 300; swipe $((c - 100)) $c $((c + 100)) $c 300; wait 150")
need_last "$out" "hvane=6"
out=$(run "state hvane=center; $hh swipe $c $((c - 80)) $c $((c + 80)) 300; wait 150")
[ -z "$(last_cmd "$out")" ] || { echo "FAIL: vertical slide changed the horizontal vane" >&2; printf '%s\n' "$out" >&2; exit 1; }

# A slide on a vane highlighted from the ring also makes it ready, so the
# next knob turn keeps changing that vane instead of moving the highlight.
out=$(run "bridge 1 1 -50; wait 60; state vvane=1; caps fan_steps=5 vane_v=0x35 vane_h=0; wait 60; screen fanvane; wait 200; press; turn 1; wait 100; swipe $c $((c - 60)) $c $((c + 20)) 300; wait 150; turn 1; wait 150")
need_last "$out" "vvane=4"

echo "PASS: airflow capability and focus regression"
