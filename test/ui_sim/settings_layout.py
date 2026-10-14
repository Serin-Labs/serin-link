"""Keep Settings titles and controls steady as optional information changes."""
import re
from touch_helpers import expect, geometry, hits, ownership, points, run, target


def label_box(output, text):
    for line in output.splitlines():
        match = re.fullmatch(r'vis label\s+(-?\d+),(-?\d+),(-?\d+),(-?\d+) "(.*)"', line)
        if match and match[5] == text:
            return tuple(map(int, match.groups()[:4]))
    raise AssertionError(f"Missing visible label {text!r}\n{output}")


def center_y(box):
    return (box[1] + box[3]) / 2


fixture = ("caps features=65535; bridge 1 1 -42; zones 3 0; "
           "state wifi_connected=1 wifi_provisioned=1 wifi_setup_avail=1 "
           "wifi_ip=192.168.1.50 hk_code=548-94-669 hk_paired=1 sensor_batt_pct=87; "
           "roomsrc 1 Internal,Bedroom,Serin-Link; ")
names = ["Brightness", "Wi-Fi", "Controller", "HomeKit", "Room Sensor", "About", "Advanced", "Back"]
controls = [None, "Connected", "Add zone", "Paired", "Bedroom", "Details", "Open", "Return"]
title_y = control_y = None
for index, (name, control) in enumerate(zip(names, controls)):
    setup = fixture + f"screen menu; screen settings; turn {index}; wait 300"
    output = run(setup + "; tree; hitmap")
    y = center_y(label_box(output, name))
    if title_y is None:
        title_y = y
    assert abs(y - title_y) <= 1, f"{name} title moved: {y} vs {title_y}"
    if control:
        y = center_y(label_box(output, control))
        if control_y is None:
            control_y = y
        assert abs(y - control_y) <= 1, f"{name} control moved: {y} vs {control_y}"

    # Changing auxiliary information must not move either anchor.
    for change in ("state hk_paired=0 sensor_batt_pct=-1", "zones 0 0"):
        changed = run(setup + f"; {change}; wait 300; tree")
        assert center_y(label_box(changed, name)) == title_y, changed
        current = "Ready to pair" if name == "HomeKit" and "hk_paired=0" in change else control
        if current:
            assert abs(center_y(label_box(changed, current)) - control_y) <= 1, changed
print("PASS Settings titles and control rows stay aligned across optional content")

# Every new pill owns its center, edges, and corners, with the correct route.
for origin in ("menu", "thermostat"):
    for index, label, destination in ((6, "Open", "advanced"), (7, "Return", origin)):
        setup = fixture + f"screen {origin}; screen settings; turn {index}; wait 300"
        output = run(setup + "; hitmap")
        action = target(output, label)
        geometry(output, [label])
        ownership(setup, action)
        for x, y in points(action):
            result = run(setup + f"; tap {x} {y}; info")
            expect(result, f"screen={destination}")
        result = run(setup + "; press; info")
        expect(result, f"screen={destination}")
print("PASS Settings Open/Return pills and knob preserve navigation from Menu and Home")

# The longest labels also fit when removing HomeKit moves the ring targets.
for features in (65535, 65533):
    for row, label, change in (
        (1, "Controller offline", "bridge 1 0 -90"),
        (4 if features & 2 else 3, "ABCDEFGHIJKLMNOPQRSTUVW",
         "roomsrc 1 Internal,ABCDEFGHIJKLMNOPQRSTUVW"),
    ):
        setup = fixture + f"caps features={features}; {change}; screen settings; turn {row}; wait 300"
        output = run(setup + "; hitmap; fontcheck")
        expect(output, "fontcheck missing=0")
        geometry(output, [label])
        ownership(setup, target(output, label))
        for node in hits(output):
            if node["text"] == "-":
                ownership(setup, node)
print("PASS Long Settings status/source labels keep their touch area clear of the ring")

# The brightness track must stay inside its target on both physical panels.
setup = fixture + "screen settings; wait 300"
output = run(setup + "; hitmap; tree")
brightness = target(output, "100%")
geometry(output, ["100%"])
ownership(setup, brightness)
x1, y1, x2, y2 = brightness["box"]
bars = [tuple(map(int, match)) for match in re.findall(
    r"vis obj\s+(-?\d+),(-?\d+),(-?\d+),(-?\d+)", output)]
bars = [b for b in bars if b[2] - b[0] > 100 and 1 < b[3] - b[1] < 12]
assert bars, output
for bx1, by1, bx2, by2 in bars:
    assert x1 <= bx1 <= bx2 <= x2 and y1 <= by1 <= by2 <= y2, output
for x, value in ((x1 + 1, 10), (x2 - 1, 100)):
    result = run(setup + f"; tap {x} {brightness['y']}; info; labels")
    expect(result, f"text={value}%", "screen=settings", "set_sel=bright")
print("PASS Brightness track stays visible and touch adjustment reaches both limits")
