"""Exercise the Settings actions through real touch paths from Home."""
import re
from touch_helpers import expect, geometry, hits, no_command, ownership, points, run, tap, target


def rim(output):
    return [hit for hit in hits(output) if hit["text"] == "-" and hit["w"] == hit["h"]]


def settings_path(fixture):
    output = run(fixture + "; hitmap")
    display = int(re.search(r"hitmap floor=\d+ disp=(\d+)", output)[1])
    menu = fixture + f"; tap {display // 2} {display // 2}; wait 300"
    output = run(menu + "; hitmap; info")
    expect(output, "screen=menu")
    # Settings precedes Back, including when optional menu items are hidden.
    settings = menu + "; " + tap(rim(output)[-2])
    output = run(settings + "; hitmap; info")
    expect(output, "screen=settings")
    return settings, output


for features in (65535, 65533):  # HomeKit present / absent.
    for online in (1, 0):
        fixture = f"caps features={features}; zones 3 0; bridge 1 {online} -42; wait 300"
        path, settings = settings_path(fixture)
        controller = path + "; " + tap(rim(settings)[2])
        output = run(controller + "; hitmap; info")
        expect(output, "screen=settings", "set_sel=link")
        action = target(output, "Add zone")
        geometry(output, ["Add zone"] + (["Offline"] if not online else []))
        ownership(controller, action)
        for x, y in points(action):
            result = run(controller + f"; tap {x} {y}; wait 300; info; labels")
            assert result.count("CMD: begin pairing") == 1, result
            expect(result, "text=Pairing")
            no_command(result, "CMD: forget bond")
        if not online:
            status = target(output, "Offline")
            ownership(controller, status)
            result = run(controller + "; " + tap(status) + "; info")
            expect(result, "dlg=other")
            no_command(result, "CMD: begin pairing")

        # The hold on this touch action cannot arm the knob's forget action.
        held = controller + f'; touchhold {action["x"]} {action["y"]}; wait 1800; info'
        result = run(held)
        expect(result, "hold=0", "screen=settings")
        no_command(result, "CMD: forget bond")
        result = run(held + "; touchup; info")
        assert result.count("CMD: begin pairing") == 1, result

        long_name = controller + "; zone 0 name=ABCDEFGHIJKLMNOPQRSTUVWXYZ12345; wait 300"
        output = run(long_name + "; hitmap; fontcheck")
        geometry(output, ["Add zone"] + (["Offline"] if not online else []))
        expect(output, "fontcheck missing=0")

        # Hold feedback must not expand the status and push Add zone over a
        # rim target, including after releasing early to cancel forgetting.
        for release in ("", "; up"):
            feedback = controller + "; down; wait 900" + release
            output = run(feedback + "; hitmap; info")
            geometry(output, ["Add zone"])
            ownership(feedback, target(output, "Add zone"))
            source_node = rim(output)[4 if features & 2 else 3]
            ownership(feedback, source_node)
            if release:
                result = run(feedback + "; " + tap(source_node) + "; info")
                expect(result, "set_sel=heatpump")
                no_command(result, "CMD: begin pairing")
                no_command(result, "CMD: forget bond")

fixture = "caps features=65535; zones 3 0; bridge 1 1 -42; wait 300"
path, settings = settings_path(fixture)
controller = path + "; " + tap(rim(settings)[2])
action = target(run(controller + "; hitmap"), "Add zone")
begin = controller + "; " + tap(action)
output = run(begin + "; hitmap")
result = run(begin + "; " + tap(target(output, "Cancel")) + "; info")
expect(result, "screen=settings", "set_sel=link")
assert result.count("CMD: begin pairing") == 1, result

failed = begin + "; pair fail; wait 300"
output = run(failed + "; hitmap")
result = run(failed + "; " + tap(target(output, "Retry")) + "; labels")
assert result.count("CMD: begin pairing") == 2, result
expect(result, "text=Pairing")
for state in ("full", "mismatch"):
    prefix = begin + f"; pair {state}; wait 300"
    output = run(prefix + "; hitmap")
    result = run(prefix + "; " + tap(target(output, "Back")) + "; info")
    expect(result, "screen=settings", "set_sel=link")
    assert result.count("CMD: begin pairing") == 1, result

# A controller status update during a touch keeps the action intact.
result = run(controller + f'; touchhold {action["x"]} {action["y"]}; bridge 1 0 -90; touchup; labels')
assert result.count("CMD: begin pairing") == 1, result
no_command(result, "CMD: forget bond")
result = run("screen settings; turn 2; press; wait 300; labels")
expect(result, "CMD: begin pairing", "text=Pairing")
print("PASS Controller Add zone via touch, offline status, cancellation/retry, and hold safety")

# Source selection also has a complete touch-only entry from Home.
sources = "10:0:Internal,20:1:Bedroom,30:2:Serin-Link,40:1:Kitchen,50:1:Hallway,60:1:Office"
path, settings = settings_path(fixture + "; roomsrc_ids 10 " + sources)
source_row = path + "; " + tap(rim(settings)[4])
output = run(source_row + "; hitmap")
source_picker = source_row + "; " + tap(target(output, "Internal"))
output = run(source_picker + "; hitmap")
result = run(source_picker + "; " + tap(rim(output)[1]) + "; info")
expect(result, "CMD: room_source_id=20\n", "screen=settings", "set_sel=heatpump")
assert result.count("CMD: room_source_id=") == 1, result
print("PASS Home to Room Sensor and selection using taps only")

# Holding the Wi-Fi row starts a network change. Releasing that same touch
# must not also run the row's tap action, which opens the address QR code.
wifi_fixture = ("caps features=65535; bridge 1 1 -42; state wifi_setup_avail=1"
                " wifi_provisioned=1 wifi_connected=1 wifi_setup_ap=0"
                " wifi_ip=192.168.1.50; wait 300")
path, settings = settings_path(wifi_fixture)
wifi_row = path + "; " + tap(rim(settings)[1])
row = target(run(wifi_row + "; hitmap"), "Connected")
# LVGL recognizes a long press at 600 ms; the confirmation arc completes at
# 1500 ms. Releasing between them must cancel without opening the address QR.
partial = wifi_row + f'; touchhold {row["x"]} {row["y"]}; wait 900'
result = run(partial + "; info")
expect(result, "screen=settings", "hold=1", "wz=hidden")
no_command(result, "CMD: wifi setup")
result = run(partial + "; touchup; wait 300; info")
expect(result, "screen=settings", "hold=0", "wz=hidden")
no_command(result, "CMD: wifi setup")
# Cancellation must not consume the next independent short tap.
result = run(partial + "; touchup; wait 300; " + tap(row) + "; info")
expect(result, "screen=wifi", "wz=ipqr")
no_command(result, "CMD: wifi setup")
held = wifi_row + f'; touchhold {row["x"]} {row["y"]}; wait 1800'
result = run(held + "; info")
expect(result, "screen=wifi", "wz=starting")
result = run(held + "; touchup; wait 300; info")
expect(result, "screen=wifi", "wz=starting")
no_command(result, "wz=ipqr")
assert result.count("CMD: wifi setup") == 1, result
# A plain tap on the same row still opens the address QR code.
result = run(wifi_row + "; " + tap(row) + "; info")
expect(result, "screen=wifi", "wz=ipqr")
print("PASS Wi-Fi row early release cancels, short taps show QR, and completed holds change the network")
