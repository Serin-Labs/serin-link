"""Advanced keeps its controls still and routes its new pills safely."""
import re
from touch_helpers import expect, geometry, hits, ownership, points, run, target


def label_box(output, text):
    for line in output.splitlines():
        match = re.fullmatch(r'vis label\s+(-?\d+),(-?\d+),(-?\d+),(-?\d+) "(.*)"', line)
        if match and match[5] == text:
            return tuple(map(int, match.groups()[:4]))
    raise AssertionError(f"Missing label {text!r}\n{output}")


def center_y(box):
    return (box[1] + box[3]) / 2


fixture = "caps features=65535; bridge 1 1 -42; zones 3 0; "
reference = run(fixture + "screen settings; turn 6; wait 300; tree")
title_y = center_y(label_box(reference, "Advanced"))
control_y = center_y(label_box(reference, "Open"))
for index, name, value in (
    (0, "Units", "°F"), (1, "Child Lock", "Off"), (2, "Idle screen", "Glance"),
    (3, "Night", "Off"), (4, "Transitions", "On"),
    (5, "Heat Pump", "Connected"), (6, "Reset Link", "Hold the knob"), (7, "Back", "Return"),
):
    setup = fixture + f"screen advanced; turn {index}; wait 300"
    output = run(setup + "; tree")
    assert abs(center_y(label_box(output, name)) - title_y) <= 1, output
    assert abs(center_y(label_box(output, value)) - control_y) <= 1, output
    if index < 5:
        edited = run(setup + "; press; wait 300; tree")
        assert label_box(edited, name) == label_box(output, name), edited
        assert label_box(edited, value) == label_box(output, value), edited
        if index:
            assert label_box(edited, "i") == label_box(output, "i"), edited
print("PASS Advanced title/control positions match Settings and help stays still while editing")

for origin in ("menu", "thermostat"):
    setup = fixture + f"screen {origin}; screen settings; turn 6; press; wait 300; turn 7; wait 300"
    output = run(setup + "; hitmap")
    geometry(output, ["Return"])
    action = target(output, "Return")
    ownership(setup, action)
    for x, y in points(action):
        result = run(setup + f"; tap {x} {y}; wait 300; info")
        expect(result, "screen=settings", "set_sel=advanced")
print("PASS Advanced Return pill returns to the Advanced Settings item from either entry path")

for label, state in (
    ("Connected", "cn105_connected=1 err_count=0"),
    ("Not connected", "cn105_connected=0"),
    ("65535 errors", "cn105_connected=1 err_count=65535"),
):
    setup = fixture + f"state {state}; screen advanced; turn 5; wait 300"
    output = run(setup + "; tree; hitmap")
    assert abs(center_y(label_box(output, label)) - control_y) <= 1, output
    geometry(output, [label])
    action = target(output, label)
    ownership(setup, action)
    result = run(setup + f"; tap {action['x']} {action['y']}; wait 300; info")
    expect(result, "screen=zones")
    changed = run(setup + "; state diag_seen=1 comp_freq_hz=120 sub_mode=Powerful outside_valid=1 outside_temp=-12; wait 300; tree")
    assert label_box(changed, label) == label_box(output, label), changed

single = run("zones 1 0; screen advanced; turn 5; wait 300; hitmap; press; info")
assert not [h for h in hits(single) if h["text"] == "Connected" and h["h"] < 200], single
expect(single, "screen=advanced")
print("PASS Heat Pump pills keep their slot across diagnostic states and open Zones only when available")

reset = "screen advanced; turn 6; wait 300"
output = run(reset + "; tree")
x1, y1, x2, y2 = label_box(output, "Hold the knob")
expect(run(reset + f"; tap {(x1+x2)//2} {(y1+y2)//2}; press; info"),
       "screen=advanced", "dlg=none", "reset_fired=0")
expect(run(reset + "; down; wait 400; up; info"), "dlg=none", "reset_fired=0")
expect(run(reset + "; down; wait 1800; up; wait 300; info"), "dlg=reset", "reset_fired=0")
print("PASS Reset still requires a knob hold and a separate confirmation")
