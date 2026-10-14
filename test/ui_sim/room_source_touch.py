"""Verify direct source selection for realistic, small sensor catalogs."""
from touch_helpers import expect, hits, no_command, ownership, run, tap

CATALOG = "10:0:Heat-pump,20:1:Bedroom,30:2:Serin-Link,40:1:Kitchen,50:1:Hallway,60:1:Office"
IDS = (10, 20, 30, 40, 50, 60)

for anim in (0, 1):
    prefs = {"anim_en": anim}
    for count in (1, 3, 5, 6):
        entries = ",".join(CATALOG.split(",")[:count])
        for applied in (IDS[0], IDS[count - 1]):
            setup = f"roomsrc_ids {applied} {entries}; screen room_source; wait 300"
            output = run(setup + "; hitmap", prefs)
            nodes = [hit for hit in hits(output) if hit["text"] == "-"]
            assert len(nodes) == count + 1, output
            for node, expected_id in zip(nodes, IDS[:count]):
                ownership(setup, node, prefs)
                result = run(setup + "; " + tap(node) + "; info", prefs)
                expect(result, f"CMD: room_source_id={expected_id}\n", "screen=settings", "set_sel=heatpump")
                assert result.count("CMD: room_source_id=") == 1, result
            result = run(setup + "; " + tap(nodes[-1]) + "; info", prefs)
            expect(result, "screen=settings", "set_sel=heatpump")
            no_command(result, "CMD: room_source_id=")

# Catalog updates while a source is held must cancel that stale touch.
setup = "roomsrc_ids 10 " + CATALOG + "; screen room_source; wait 300"
node = [hit for hit in hits(run(setup + "; hitmap")) if hit["text"] == "-"][1]
for replacement in ("10:0:Heat-pump,30:2:Serin-Link,20:1:Bedroom", "10:0:Heat-pump", "-"):
    result = run(setup + f'; touchhold {node["x"]} {node["y"]}; roomsrc_ids 10 ' + replacement + "; touchup; info")
    no_command(result, "CMD: room_source_id=")
    expect(result, "screen=room_source")
print("PASS Room Sensor direct taps for 1–6 sources, Back, and catalog updates during touch")
