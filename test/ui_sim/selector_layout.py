"""Stable selector slots, safe action targets, and unchanged commit semantics."""
import re
import sys
import tempfile
from pathlib import Path
from touch_helpers import expect, geometry, hits, no_command, ownership, points, run, tap, target


def box(output, text):
    for line in output.splitlines():
        match = re.fullmatch(r'vis label\s+(-?\d+),(-?\d+),(-?\d+),(-?\d+) "(.*)"', line)
        if match and match[5] == text:
            return tuple(map(int, match.groups()[:4]))
    raise AssertionError(f"Missing visible label {text!r}\n{output}")


def y(bounds):
    return (bounds[1] + bounds[3]) / 2


FIXTURE = "caps features=65535 modes=65535; bridge 1 1 -42; "
ref = run(FIXTURE + "screen settings; turn 6; wait 300; tree")
TITLE = y(box(ref, "Advanced"))
CONTROL = y(box(ref, "Open"))


def aligned(output, name, value):
    assert abs(y(box(output, name)) - TITLE) <= 1, output
    assert abs(y(box(output, value)) - CONTROL) <= 1, output


def action(setup, label):
    output = run(setup + "; tree; hitmap")
    geometry(output, [label])
    hit = target(output, label)
    ownership(setup, hit)
    return hit


def temperature_centering(setup):
    """The degree suffix must not pull the visible digits off the dial axis."""
    from PIL import Image

    values = (70, 7, 100, 0, -5, 70)
    with tempfile.TemporaryDirectory(prefix="serin-zone-reading-") as work:
        commands = [setup]
        for index, value in enumerate(values):
            commands.append(f"zone 0 temp={value}; wait 100; shotfb {work}/{index}.png")
        run("; ".join(commands))
        for index, value in enumerate(values):
            with Image.open(Path(work) / f"{index}.png") as image:
                image = image.convert("RGB")
                # Isolate the large reading between the detail and status rows.
                # Group ink by column so this works with joined or separate labels.
                groups = []
                for px in range(image.width // 5, image.width * 4 // 5):
                    ys = [py for py in range(image.height * 51 // 100, image.height * 65 // 100)
                          if min(image.getpixel((px, py))) > 130]
                    if not ys:
                        continue
                    if not groups or groups[-1][-1][0] < px - 1:
                        groups.append([])
                    groups[-1].extend((px, py) for py in ys)
                assert len(groups) == len(str(value)) + 1, (value, len(groups))
                degree = groups.pop()
                digits = [point for group in groups for point in group]
                left, right = min(p[0] for p in digits), max(p[0] for p in digits)
                offset = (left + right - image.width + 1) / 2
                assert abs(offset) <= 3, f"{value}°: numeric ink is {offset}px off center"
                gap = min(p[0] for p in degree) - right
                assert 2 <= gap <= 14, f"{value}°: degree gap is {gap}px"
                assert abs(min(p[1] for p in degree) - min(p[1] for p in digits)) <= 2, value
    print("PASS Zones numeric ink stays centered as the temperature changes width")


def zones():
    fixture = FIXTURE + "zones 3 0; zone 0 name=Lounge badge=online temp=70 mode=heat status=active set=72; "
    setup = fixture + "screen zones; wait 300"
    temperature_centering(setup)
    normal = run(setup + "; tree")
    aligned(normal, "Lounge", "70")
    assert y(box(normal, "°")) == y(box(normal, "70")), normal
    for change, reading, detail, status in (
        ("state=0 badge=checking", "--", "No reading yet", "Checking · Active"),
        ("badge=offline", "70", "Last · Heating to 72°", "Offline · Active"),
        ("badge=stale", "70", "Last · Heating to 72°", "Stale · Active"),
    ):
        changed = run(setup + f"; zone 0 {change}; wait 100; tree")
        assert box(changed, "Lounge") == box(normal, "Lounge"), changed
        assert abs(y(box(changed, reading)) - CONTROL) <= 1, changed
        assert box(changed, detail) == box(normal, "Heating to 72°"), changed
        assert y(box(changed, status)) == y(box(normal, "Online · Active")), changed
        if reading == "--":
            assert '"°"' not in changed, changed
        else:
            assert box(changed, "°") == box(normal, "°"), changed
    restored = run(setup + "; zone 0 state=0; wait 100; zone 0 state=1 temp=70; wait 100; tree")
    assert box(restored, "°") == box(normal, "°"), restored
    # A's reading is informational: touch does not switch the zone.
    read = box(normal, "70")
    unchanged = run(setup + f"; tap {(read[0]+read[2])//2} {(read[1]+read[3])//2}; info")
    expect(unchanged, "screen=zones", "zone=0/3")
    no_command(unchanged, "CMD: zone_select=")
    expect(run(setup + "; turn 1; press; wait 300; info"), "screen=thermostat", "zone=1/3")
    # Return must retrace Home, Menu, and the diagnostic entry path.
    for origin in ("thermostat", "menu", "advanced"):
        back = fixture + f"screen {origin}; screen zones; turn 3; wait 300"
        hit = action(back, "Return")
        back_tree = run(back + "; tree")
        aligned(back_tree, "Back", "Return")
        assert '"°"' not in back_tree, back_tree
        restored = run(back + "; turn -3; wait 300; tree")
        assert box(restored, "°") == box(normal, "°"), restored
        for px, py in points(hit):
            result = run(back + f"; tap {px} {py}; wait 300; info")
            expect(result, f"screen={origin}", "zone=0/3")
    for count in range(2, 8):
        back = FIXTURE + f"zones {count} 0; screen zones; turn {count}; wait 300"
        action(back, "Return")
    print("PASS Zones readings and status keep their slots; Return retraces without changing zones")


def mode():
    setup = FIXTURE + "state mode=heat; screen mode; wait 300"
    normal = run(setup + "; tree")
    aligned(normal, "Heat", "Use mode")
    for steps, name, description in ((2, "Heat/Cool", "Sets a temperature range"), (3, "Auto", "Automatic mode"), (7, "Back", "")):
        output = run(setup + f"; turn {steps}; tree")
        aligned(output, name, "Return" if steps == 7 else "Use mode")
        assert box(output, name)[1::2] == box(normal, "Heat")[1::2], output
        if description:
            box(output, description)
    # Every supported ring density must leave the center's full target clear.
    for mask in (0, 2, 6, 14, 30, 62, 126):
        small = FIXTURE + f"caps modes={mask}; screen mode; wait 300"
        hit = action(small, "Use mode")
        for node in [h for h in hits(run(small + "; hitmap")) if h["text"] == "-"]:
            ownership(small, node)
        expect(run(small + "; " + tap(hit) + "; info"), "screen=thermostat")
    cool = setup + "; turn 1"
    hit = action(cool, "Use mode")
    for px, py in points(hit):
        result = run(cool + f"; tap {px} {py}; wait 300; info")
        expect(result, "screen=thermostat", "mode=cool", "CMD: mode=COOL")
        assert result.count("CMD: mode=") == 1, result
    syncing = run(cool + f"; state alert=syncing; {tap(hit)}; info")
    expect(syncing, "screen=thermostat", "mode=heat")
    no_command(syncing, "CMD: mode=")
    for origin in ("thermostat", "menu"):
        back = FIXTURE + f"screen {origin}; screen mode; turn 7; wait 300"
        ret = action(back, "Return")
        result = run(back + "; " + tap(ret) + "; info")
        expect(result, f"screen={origin}")
        no_command(result, "CMD: mode=")
    print("PASS Mode slots, full action/ring targets, commit, syncing refusal, and Return")


def source():
    catalog = "10:0:Heat-pump,20:1:Bedroom,30:2:Serin-Link,40:1:Kitchen,50:1:Hall,60:1:Office,70:1:Den,80:1:Loft,90:1:Guest"
    setup = FIXTURE + "roomsrc_ids 20 " + ",".join(catalog.split(",")[:3]) + "; screen room_source; wait 300"
    normal = run(setup + "; tree")
    aligned(normal, "Bedroom", "Selected")
    paged = FIXTURE + "roomsrc_ids 20 " + catalog + "; screen room_source; wait 300"
    output = run(paged + "; tree")
    assert box(output, "Bedroom") == box(normal, "Bedroom"), output
    assert box(output, "Selected") == box(normal, "Selected"), output
    box(output, "2 / 9")
    for count in range(1, 10):
        current = FIXTURE + "roomsrc_ids 10 " + ",".join(catalog.split(",")[:count]) + "; screen room_source; wait 300"
        hit = action(current, "Selected")
        # Selected still accepts the same explicit confirmation as the knob.
        expect(run(current + "; " + tap(hit)), "CMD: room_source_id=10")
    choose = setup + "; turn -1"
    choose_hit = action(choose, "Choose")
    for px, py in points(choose_hit):
        result = run(choose + f"; tap {px} {py}; wait 300; info")
        expect(result, "CMD: room_source_id=10", "screen=settings", "set_sel=heatpump")
        assert result.count("CMD: room_source_id=") == 1, result
    last_page = paged + "; turn 7; wait 300"
    hit = action(last_page, "Choose")
    expect(run(last_page + "; " + tap(hit)), "CMD: room_source_id=90")
    for change, reason in (("roomsrc_ids 20 10:0:Heat-pump", "Sensor no longer available"), ("roomsrc_ids 20 -", "No sensors available")):
        back = setup + "; " + change + "; wait 300"
        hit = action(back, "Return")
        output = run(back + "; tree")
        aligned(output, "Back", "Return")
        box(output, reason)
        result = run(back + "; " + tap(hit) + "; info")
        expect(result, "screen=settings", "set_sel=heatpump")
        no_command(result, "CMD: room_source_id=")
    # Rebuilding the catalog while Choose is held cancels the stale action.
    result = run(choose + f'; touchhold {choose_hit["x"]} {choose_hit["y"]}; roomsrc_ids 20 30:2:Serin-Link; touchup; info')
    no_command(result, "CMD: room_source_id=")
    print("PASS Room Sensor pagination, action targets, selected/choose actions, and safe catalog changes")


def airflow():
    for origin in ("thermostat", "menu"):
        setup = FIXTURE + f"screen {origin}; screen fanvane; wait 300; press; turn 3; wait 300"
        output = run(setup + "; tree; hitmap")
        aligned(output, "Back", "Return")
        hit = action(setup, "Return")
        # Only the ring, Return, and the full-panel swipe target remain active.
        small = [h for h in hits(output) if h["h"] < 300]
        assert len(small) == 5, output
        for px, py in points(hit):
            result = run(setup + f"; tap {px} {py}; wait 300; info")
            expect(result, f"screen={origin}")
            no_command(result, "CMD: mode=")
        restored = run(setup + "; turn -1; wait 300; tree; hitmap; press; turn 1; info")
        assert '"Return"' not in restored, restored
        expect(restored, "screen=fanvane", "CMD: mode=")
    print("PASS Airflow Back replaces the control, Return retraces, and leaving Back restores adjustment")


checks = {"zones": zones, "mode": mode, "source": source, "airflow": airflow}
for name in sys.argv[1:] or checks:
    checks[name]()
