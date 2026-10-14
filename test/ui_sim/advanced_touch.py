"""Catch help stealing option taps, including after editing changes the layout."""
from touch_helpers import expect, geometry, hits, ownership, points, run, target


for anim in (0, 1):
    for row, left, right, key in (
        (0, "°F", "°C", None), (1, "Off", "On", "lk_en"),
        (2, "Glance", "Off", "idle_off"), (3, "Off", "On", "night_en"),
        (4, "Off", "On", "anim_en"),
    ):
        for editing in (False, True):
            prefs = {"anim_en": anim, "lk_en": 0, "idle_off": 0, "night_en": 0}
            setup = f"screen advanced; turn {row}; " + ("press; " if editing else "") + "wait 300"
            output = run(setup + "; hitmap", prefs)
            labels = [left, right] + (["i"] if row else [])
            geometry(output, labels)
            # Help and options each reserve a full touch target without taking
            # a tap from another control or the ring.
            for node in hits(output):
                if node["text"] == "-":
                    ownership(setup, node, prefs)
            for label in labels:
                hit = target(output, label)
                ownership(setup, hit, prefs)
                for x, y in points(hit):
                    result = run(setup + f"; tap {x} {y}; info", prefs)
                    if label == "i":
                        expect(result, "dlg=other")
                        for name, value in prefs.items():
                            expect(result, f"prefs {name}={value}\n")
                    else:
                        expect(result, "dlg=none", "screen=advanced")
                        if key:
                            expect(result, f"prefs {key}={int(label == right)}\n")
                        else:
                            expect(result, f"units={'C' if label == right else 'F'}")

# Enabling Child Lock adds its home-unlock hint while browsing.
setup = "screen advanced; turn 1; wait 300"
prefs = {"lk_en": 1}
output = run(setup + "; hitmap", prefs)
geometry(output, ["Off", "On", "i"])
for label in ("Off", "On", "i"):
    ownership(setup, target(output, label), prefs)
print("PASS Advanced option/help edge dispatch, editing, and ring clearance")
