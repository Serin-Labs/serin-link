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
# one safe Back row and must not dereference a vanished option.
LOG=$(run 'roomsrc_ids 10 10:Internal; screen room_source; roomsrc_ids 10 -; wait 150; labels; hitmap; press; wait 150; info')
expect "$LOG" 'text=No sensors available' 'empty catalog did not state that no sensors remain'
expect "$LOG" 'text=Back$' 'empty catalog did not keep Back available'
expect_not "$LOG" '^CMD: room_source_id=' 'empty catalog sent a source ID'
expect "$LOG" 'screen=settings.*set_sel=heatpump' 'empty-catalog Back did not land on Room Sensor settings'

# Rows are separated in the list even when one is clipped by scrolling.
LOG=$(run 'roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway; screen room_source; wait 150; hitmap')
floor=$(sed -n 's/^hitmap floor=\([0-9][0-9]*\).*/\1/p' <<<"$LOG")
row_width=$(awk '
    /text=(Internal|Bedroom|Hallway|Back)$/ {
        for (i = 1; i <= NF; i++) if ($i ~ /^drawn=/) {
            split(substr($i, 7), a, "x")
            if (!min || a[1] < min) min = a[1]
        }
    }
    END { if (min) print min }
' <<<"$LOG")
if [[ -z "$floor" || -z "$row_width" ]] || ! awk -v row_width="$row_width" '
    /text=(Internal|Bedroom|Hallway|Back)$/ {
        h = y1 = y2 = -1
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^h=/) { split($i, a, "="); h = a[2] }
            if ($i ~ /^drawn=/) { split(substr($i, 7), a, "x"); w = a[1] }
            if ($i ~ /^box=/)   { split(substr($i, 5), a, ","); y1 = a[2]; y2 = a[4] }
        }
        if (w != row_width) next
        if (last_y2 >= 0 && y1 <= last_y2) bad = 1
        last_y2 = y2
        rows++
    }
    END { exit (rows == 4 && !bad) ? 0 : 1 }
' <<<"$LOG"; then
    echo "FAIL: picker row boxes overlap" >&2
    fail=1
fi

# Each row becomes a fully visible physical target when focused, including Back.
for target in '0 Internal' '1 Bedroom' '2 Hallway' '3 Back'; do
    read -r turns label <<<"$target"
    command='roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway; screen room_source'
    for ((i = 0; i < turns; i++)); do command+='; turn 1'; done
    LOG=$(run "$command; wait 150; hitmap")
    height=$(awk -v label="$label" -v row_width="$row_width" '
        $0 ~ /^hit / && $0 ~ ("text=" label "$") {
            w = 0
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^drawn=/) { split(substr($i, 7), a, "x"); w = a[1] }
                if ($i ~ /^h=/) { split($i, a, "="); h = a[2] }
            }
            if (w == row_width) { print h; exit }
        }
    ' <<<"$LOG")
    if [[ -z "$height" || "$height" -lt "$floor" ]]; then
        echo "FAIL: focused $label row is below the physical touch floor" >&2
        fail=1
    fi
done

# Full names remain in the labels, but their visible boxes are bounded.  The
# selected room source and displayed zone use LVGL's delayed scroll mode to
# reveal the rest of a name without changing the layout.
# The wire field has 24 bytes, including the terminator used by the simulator.
room_name=ABCDEFGHIJKLMNOPQRSTUVW
zone_name=ABCDEFGHIJKLMNOPQRSTUVWXYZ12345
LOG=$(run "roomsrc_ids 10 10:$room_name; screen room_source; wait 150; labelbox $room_name")
room_box=$(sed -n "s/^labelbox text=$room_name box=//p" <<<"$LOG")
LOG=$(run "zones 2 0; zone 0 name=$zone_name; screen zones; wait 150; labelbox $zone_name")
zone_box=$(sed -n "s/^labelbox text=$zone_name box=//p" <<<"$LOG")
for bounded in "$room_box" "$zone_box"; do
    IFS=, read -r x1 _ x2 _ <<<"$bounded"
    if [[ -z "$x1" || -z "$x2" || $((x2 - x1 + 1)) -gt "$row_width" ]]; then
        echo "FAIL: a long name can expand its layout instead of staying bounded" >&2
        fail=1
    fi
done

# A catalog long enough to scroll must stay where a finger left it.  Every
# telemetry push refreshes the current screen, so a picker that re-centres on
# its focused row during a refresh yanks the list away mid-scroll.
overflow='roomsrc_ids 10 10:Internal,20:Bedroom,30:Hallway,40:Kitchen,50:Garage,60:Attic,70:Office,80:Porch'
LOG=$(run "$overflow; screen room_source; wait 200; hitmap")
mid_x=$(($(sed -n 's/^hitmap floor=[0-9][0-9]* disp=\([0-9][0-9]*\).*/\1/p' <<<"$LOG") / 2))
row_mid_y() {   # vertical centre of the named row, which only a row box owns
    awk -v label="$1" -v row_width="$row_width" '
        $0 ~ /^hit / && $0 ~ ("text=" label "$") {
            w = 0
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^drawn=/) { split(substr($i, 7), a, "x"); w = a[1] }
                if ($i ~ /^box=/)   { split(substr($i, 5), b, ",") }
            }
            if (w == row_width) { print int((b[2] + b[4]) / 2); exit }
        }' <<<"$2"
}
drag_from=$(row_mid_y Hallway "$LOG")
drag_to=$(row_mid_y Internal "$LOG")
LOG=$(run "$overflow; screen room_source; wait 200; hitmap;
           swipe $mid_x $drag_from $mid_x $drag_to 300; wait 800; hitmap;
           state room_temp=71; wait 200; hitmap")
mapfile -t back_box < <(sed -n 's/^hit .* box=\([-0-9,]*\) text=Back$/\1/p' <<<"$LOG")
if [[ -z "$mid_x" || -z "$drag_from" || -z "$drag_to" || ${#back_box[@]} -ne 3 ]]; then
    echo 'FAIL: could not read the picker geometry for the scroll check' >&2
    fail=1
elif [[ "${back_box[0]}" == "${back_box[1]}" ]]; then
    echo 'FAIL: the swipe did not scroll the picker, so the check proves nothing' >&2
    fail=1
elif [[ "${back_box[1]}" != "${back_box[2]}" ]]; then
    echo 'FAIL: a state refresh scrolled the picker away from where touch left it' >&2
    fail=1
fi

if ((fail)); then exit 1; fi
echo 'PASS room-source'
