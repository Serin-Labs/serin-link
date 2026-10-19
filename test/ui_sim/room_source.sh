#!/usr/bin/env bash
# Room-source picker regressions.  The simulator is intentionally supplied by
# the caller so this test has no build-directory or display-server dependency.
set -euo pipefail

: "${SERIN_SIM_BIN:?set SERIN_SIM_BIN to the serin-sim binary}"
SIM=$(realpath "$SERIN_SIM_BIN")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run() { (cd "$tmp" && "$SIM" --headless --run "$1"); }
fail=0

expect() {
    local text=$1 pattern=$2 message=$3
    if ! grep -q -- "$pattern" <<<"$text"; then
        echo "FAIL: $message" >&2
        fail=1
    fi
}

expect_not() {
    local text=$1 pattern=$2 message=$3
    if grep -q -- "$pattern" <<<"$text"; then
        echo "FAIL: $message" >&2
        fail=1
    fi
}

# Replacing a catalog must retain the highlighted ID, not its former position.
LOG=$(run 'roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway; screen room_source; turn 1; roomsrc_ids 10 20:Bedroom,30:Hallway,10:Internal; press; wait 150; info')
expect "$LOG" '^CMD: room_source_id=20$' 'reordered catalog lost the focused source ID'
# `heatpump` is the simulator's legacy readback name for enum SET_SYSTEM.
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'choosing a source did not return to Room Sensor settings'

# When that ID disappears, Back is selected, tells the user why, and never
# sends the source which happened to occupy its old array index.
LOG=$(run 'roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway; screen room_source; turn 1; roomsrc_ids 10 10:Internal,30:Hallway; wait 150; labels; press; wait 150; info')
expect "$LOG" 'text=Sensor no longer available' 'removed source did not explain the fallback'
expect_not "$LOG" '^CMD: room_source_id=' 'removed source committed another row'
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'Back after source removal did not return to Room Sensor settings'

# An empty replacement happens while this lazy screen is active.  It still has
# one safe Back stop and must not dereference a vanished option.
LOG=$(run 'roomsrc_ids 10 10:Internal; screen room_source; roomsrc_ids 10 -; wait 150; labels; hitmap; press; wait 150; info')
expect "$LOG" 'text=No sensors available' 'empty catalog did not state that no sensors remain'
expect "$LOG" 'text=Back$' 'empty catalog did not keep Back available'
expect_not "$LOG" '^CMD: room_source_id=' 'empty catalog sent a source ID'
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'empty-catalog Back did not land on Room Sensor settings'

# The four ring targets must apply their own source IDs. Back sits at the left, like the other firmware pickers.
setup='roomsrc_ids 10 10:0:Internal,20:1:Bedroom,30:2:Link; screen room_source; wait 150'
LOG=$(run "$setup; hitmap")
disp=$(sed -n 's/^hitmap floor=[0-9]* disp=\([0-9]*\).*/\1/p' <<<"$LOG")
# Cardinal stops in 480-space: Internal top, Bedroom right, Link bottom.
for target in '240 54 10' '426 240 20' '240 426 30'; do
    read -r x y id <<<"$target"
    LOG=$(run "$setup; tap $((x * disp / 480)) $((y * disp / 480)); wait 150; info")
    expect "$LOG" "^CMD: room_source_id=$id$" "rim tap did not choose source $id"
    expect "$LOG" 'screen=settings.*set_sel=heatpump' 'rim tap did not return to Room Sensor settings'
done
LOG=$(run "$setup; tap $((54 * disp / 480)) $((disp / 2)); wait 150; info")
expect_not "$LOG" '^CMD: room_source_id=' 'Back rim tap sent a source ID'
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'Back rim tap did not return to Room Sensor settings'

# Long focused names stay bounded and retain their full text for scrolling.
room_name=ABCDEFGHIJKLMNOPQRSTUVW
LOG=$(run "roomsrc_ids 10 10:$room_name; screen room_source; wait 150; labelbox $room_name")
room_box=$(sed -n "s/^labelbox text=$room_name box=//p" <<<"$LOG")
IFS=, read -r x1 _ x2 _ <<<"$room_box"
if [[ -z "$x1" || -z "$x2" || $((x2 - x1 + 1)) -gt $(((310 * disp + 240) / 480)) ]]; then
    echo 'FAIL: a long sensor name expands beyond the center' >&2
    fail=1
fi

# Preserve the companion zone-name regression: both pickers bound long names.
zone_name=ABCDEFGHIJKLMNOPQRSTUVWXYZ12345
LOG=$(run "zones 2 0; zone 0 name=$zone_name; screen zones; wait 150; labelbox $zone_name")
zone_box=$(sed -n "s/^labelbox text=$zone_name box=//p" <<<"$LOG")
IFS=, read -r x1 _ x2 _ <<<"$zone_box"
if [[ -z "$x1" || -z "$x2" || $((x2 - x1 + 1)) -gt $(((310 * disp + 240) / 480)) ]]; then
    echo 'FAIL: a long zone name expands beyond the center' >&2
    fail=1
fi

# All twelve catalog entries remain reachable when more than seven sensors
# need multiple rings. Turning back crosses the boundary in the other direction.
overflow='roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway,40:Kitchen,50:Garage,60:Attic,70:Office,80:Porch,90:Den,100:Study,110:Loft,120:Basement'
LOG=$(run "$overflow; screen room_source; turn 11; wait 150; labels; state room_temp=71; press")
expect "$LOG" 'text=Basement$' 'last catalog source cannot be focused'
expect "$LOG" '^CMD: room_source_id=120$' 'last catalog source cannot be applied'
LOG=$(run "$overflow; screen room_source; turn 7; turn -1; press")
expect "$LOG" '^CMD: room_source_id=70$' 'turning back across rings lost the source'
LOG=$(run "$overflow; screen room_source; turn 7; wait 150; tap $((54 * disp / 480)) $((disp / 2)); info")
expect_not "$LOG" '^CMD: room_source_id=' 'Back on the second ring sent a source ID'
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'Back on the second ring did not return to settings'
LOG=$(run "$overflow; screen room_source; turn 12; labels; press; info")
expect "$LOG" 'text=Back$' 'Back cannot be reached after a full catalog'
expect_not "$LOG" '^CMD: room_source_id=' 'Back after a full catalog sent a source ID'

if ((fail)); then exit 1; fi
python3 "$(dirname "$0")/room_source_touch.py"
echo 'PASS room-source'
