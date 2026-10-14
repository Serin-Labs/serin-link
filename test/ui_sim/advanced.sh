#!/usr/bin/env bash
# Behavioral coverage for Advanced's binary rows and their contextual help.
set -euo pipefail

: "${SERIN_SIM_BIN:?set SERIN_SIM_BIN to a built serin-sim binary}"
[ -x "$SERIN_SIM_BIN" ] || { echo "advanced: simulator is not executable: $SERIN_SIM_BIN"; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

run() {
    (cd "$WORK" && "$SERIN_SIM_BIN" --headless --run "$1")
}

require() {
    local out="$1" needle="$2" message="$3"
    grep -Fq "$needle" <<<"$out" || { echo "FAIL: $message"; echo "$out"; exit 1; }
}

# One encoder detent always advances one Advanced row. Help is touch-only and
# never changes preferences or the selected row.
run 'screen advanced; info' >/dev/null
PREFS_BEFORE=$(cksum "$WORK/.prefs")
OUT=$(run 'screen advanced; turn 1; info')
require "$OUT" 'adv_sel=lock' 'one encoder turn did not select Child Lock'
OUT=$(run 'screen advanced; turn 1; turn 1; info')
require "$OUT" 'adv_sel=idle' 'one encoder turn per Advanced row was not restored'
OUT=$(run 'screen advanced; turn 2; turn -1; info')
require "$OUT" 'adv_sel=lock' 'reverse browsing did not return to the previous row'
[ "$PREFS_BEFORE" = "$(cksum "$WORK/.prefs")" ] || {
    echo 'FAIL: browsing Advanced rows changed preferences'; exit 1;
}

# The touch info target is still available on the selected Child Lock row.
HELP_MAP=$(run 'screen advanced; turn 1; wait 100; hitmap')
HELP_X=$(awk '$1 == "hit" && $NF == "text=i" { sub("x=", "", $2); print $2; exit }' <<<"$HELP_MAP")
HELP_Y=$(awk '$1 == "hit" && $NF == "text=i" { sub("y=", "", $3); print $3; exit }' <<<"$HELP_MAP")
[ -n "$HELP_X" ] && [ -n "$HELP_Y" ] || { echo 'FAIL: Advanced info target is missing'; exit 1; }
OUT=$(run "screen advanced; turn 1; wait 200; tap $HELP_X $HELP_Y; labels; info")
require "$OUT" 'label text=Child Lock' 'help press did not open the existing dialog'
require "$OUT" 'dlg=other' 'help press did not leave an info dialog open'
[ "$PREFS_BEFORE" = "$(cksum "$WORK/.prefs")" ] || {
    echo 'FAIL: touching Advanced help changed preferences'; exit 1;
}

OUT=$(run 'screen advanced; turn 1; press; turn 1; info')
require "$OUT" 'lock=1' 'a turn while editing Child Lock traversed focus instead of editing'

# The renamed row still explains itself through its touch target.
TRANS_MAP=$(run 'screen advanced; turn 4; wait 100; hitmap')
TRANS_X=$(awk '$1 == "hit" && $NF == "text=i" { sub("x=", "", $2); print $2; exit }' <<<"$TRANS_MAP")
TRANS_Y=$(awk '$1 == "hit" && $NF == "text=i" { sub("y=", "", $3); print $3; exit }' <<<"$TRANS_MAP")
OUT=$(run "screen advanced; turn 4; wait 200; labels; tap $TRANS_X $TRANS_Y; labels")
require "$OUT" 'label text=Transitions' 'Motion was not renamed to Transitions'
require "$OUT" 'label text=Fades between screens. Off changes screens immediately.' 'Transitions explainer copy is wrong'

# Back always returns to Advanced's Settings stop, regardless of entry path.
OUT=$(run 'screen advanced; turn 7; press; info')
require "$OUT" 'screen=settings' 'Advanced Back did not return to Settings'
require "$OUT" 'set_sel=advanced' 'Advanced Back did not land on the Advanced Settings row'

# Every binary option owns a real minimum-size, non-overlapping hit slot. The
# slots themselves carry the label for hitmap, so this catches a regression to
# one parent-wide target or a floor supplied only by colliding ext_click_area.
check_options() { # turns left-label right-label [has-info]
    local turns="$1" left="$2" right="$3" has_info="${4:-yes}" out floor l r info
    out=$(run "screen advanced; turn $turns; wait 200; hitmap; labelbox $left; labelbox $right")
    floor=$(sed -n 's/^hitmap floor=\([0-9]*\).*/\1/p' <<<"$out")
    l=$(awk -v t="text=$left" '$1 == "hit" && $NF == t { print; exit }' <<<"$out")
    r=$(awk -v t="text=$right" '$1 == "hit" && $NF == t { print; exit }' <<<"$out")
    info=$(awk '$1 == "hit" && $NF == "text=i" { print; exit }' <<<"$out")
    [ -n "$l" ] && [ -n "$r" ] || {
        echo "FAIL: missing option or info hit slot for $left/$right"; echo "$out"; exit 1;
    }
    if [ "$has_info" = yes ] && [ -z "$info" ]; then
        echo "FAIL: missing info hit slot for $left/$right"; echo "$out"; exit 1
    fi
    for line in "$l" "$r"; do
        local w h
        w=$(sed -n 's/.* w=\([0-9]*\).*/\1/p' <<<"$line")
        h=$(sed -n 's/.* h=\([0-9]*\).*/\1/p' <<<"$line")
        [ "$w" -ge "$floor" ] && [ "$h" -ge "$floor" ] || {
            echo "FAIL: $line is below the ${floor}px touch floor"; exit 1;
        }
    done

    # Option slots must not overlap. advanced_touch.py also checks the full
    # help target and dispatch at the edges of every option and ring control.
    overlaps() { # <hitmap line> <hitmap line>
        awk -v a="$1" -v b="$2" '
            function n(s) { sub("^[^=]*=", "", s); return s + 0 }
            BEGIN {
                split(a, A, " "); split(b, B, " ");
                ax=n(A[2]); ay=n(A[3]); aw=n(A[4]); ah=n(A[5]);
                bx=n(B[2]); by=n(B[3]); bw=n(B[4]); bh=n(B[5]);
                ax1=ax-int((aw-1)/2); ax2=ax+int(aw/2);
                ay1=ay-int((ah-1)/2); ay2=ay+int(ah/2);
                bx1=bx-int((bw-1)/2); bx2=bx+int(bw/2);
                by1=by-int((bh-1)/2); by2=by+int(bh/2);
                exit !(ax1 <= bx2 && bx1 <= ax2 && ay1 <= by2 && by1 <= ay2)
            }'
    }
    if overlaps "$l" "$r"; then
        echo "FAIL: $left/$right has overlapping live touch rectangles"
        echo "$l"; echo "$r"; echo "$info"; exit 1
    fi

    pill_inside_slot() { # <labelbox line> <hitmap slot line>
        awk -v label="$1" -v slot="$2" '
            function readbox(s, p, a) {
                match(s, /box=[0-9,-]+/); p=substr(s, RSTART + 4, RLENGTH - 4);
                split(p, a, ","); x1=a[1]; y1=a[2]; x2=a[3]; y2=a[4];
            }
            BEGIN {
                readbox(label); lx1=x1; ly1=y1; lx2=x2; ly2=y2;
                readbox(slot);  sx1=x1; sy1=y1; sx2=x2; sy2=y2;
                exit !(lx1 >= sx1 && ly1 >= sy1 && lx2 <= sx2 && ly2 <= sy2)
            }'
    }
    local llabel rlabel
    llabel=$(awk -v t="$left" 'index($0, "labelbox text=" t " box=") == 1 { sub(/^.* box=/, "box="); print; exit }' <<<"$out")
    rlabel=$(awk -v t="$right" 'index($0, "labelbox text=" t " box=") == 1 { sub(/^.* box=/, "box="); print; exit }' <<<"$out")
    [ -n "$llabel" ] && [ -n "$rlabel" ] || {
        echo "FAIL: missing visible pill geometry for $left/$right"; echo "$out"; exit 1;
    }
    if ! pill_inside_slot "$llabel" "$l" || ! pill_inside_slot "$rlabel" "$r"; then
        echo "FAIL: a $left/$right pill background extends outside its touch slot"
        echo "$llabel"; echo "$rlabel"; echo "$l"; echo "$r"; exit 1
    fi
}

check_options 0 '°F' '°C' no
check_options 1 Off On
check_options 2 Glance Off
check_options 3 Off On
check_options 4 Off On

python3 "$(dirname "$0")/advanced_touch.py"
python3 "$(dirname "$0")/advanced_layout.py"

echo 'advanced: OK'
