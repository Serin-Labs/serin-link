"""Real-LVGL touch assertions shared by the simulator regression scripts."""
import os
from pathlib import Path
import re
import subprocess
import tempfile


def run(commands, prefs=None):
    with tempfile.TemporaryDirectory(prefix="serin-touch-test-") as work:
        path = Path(work) / ".prefs"
        if prefs:
            path.write_text("".join(f"{key}={value}\n" for key, value in prefs.items()))
        result = subprocess.run(
            [os.path.abspath(os.environ["SERIN_SIM_BIN"]), "--headless", "--run",
             "wait 1800; " + commands], cwd=work, text=True,
            capture_output=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        saved = path.read_text() if path.exists() else ""
        return result.stdout + "".join(f"prefs {line}\n" for line in saved.splitlines())


def hits(output):
    result = []
    for line in output.splitlines():
        if not line.startswith("hit "):
            continue
        fields, label = line.split(" text=", 1)
        fields = dict(part.split("=", 1) for part in fields.split()[1:])
        fields.update(text=label, box=tuple(map(int, fields["box"].split(","))))
        for key in ("x", "y", "w", "h"):
            fields[key] = int(fields[key])
        result.append(fields)
    return result


def target(output, label):
    candidates = [hit for hit in hits(output) if hit["text"] == label]
    assert candidates, f"Missing touch target {label!r}\n{output}"
    return min(candidates, key=lambda hit: hit["w"] * hit["h"])


def points(hit):
    x1, y1, x2, y2 = hit["box"]
    # One pixel inside each edge, including corners, plus the center.
    return [(x, y) for x in (x1 + 1, hit["x"], x2 - 1)
            for y in (y1 + 1, hit["y"], y2 - 1)]


def tap(hit):
    return f'tap {hit["x"]} {hit["y"]}; wait 300'


def geometry(output, labels, clipped=()):
    floor, display = map(int, re.search(r"hitmap floor=(\d+) disp=(\d+)", output).groups())
    controls = [hit for hit in hits(output) if hit["h"] < display // 2]
    for label in labels:
        hit = target(output, label)
        x1, y1, x2, y2 = hit["box"]
        assert min(hit["w"], hit["h"], x2 - x1 + 1, y2 - y1 + 1) >= floor, \
            f"{label}: target does not reserve the {floor}px floor: {hit}"
        for other in controls:
            # Custom hit-test clipping is verified with actual ownership probes.
            if other == hit or other["text"] in clipped:
                continue
            # Use true hit bounds, including extended areas on neighbors.
            ox1 = other["x"] - (other["w"] - 1) // 2
            oy1 = other["y"] - (other["h"] - 1) // 2
            ox2, oy2 = ox1 + other["w"] - 1, oy1 + other["h"] - 1
            assert x2 < ox1 or ox2 < x1 or y2 < oy1 or oy2 < y1, \
                f"{label}: overlaps {other}"


def ownership(commands, hit, prefs=None):
    output = run(commands + "; " + "; ".join(f"hitat {x} {y}" for x, y in points(hit)), prefs)
    actual = [line.split(" text=", 1)[1] for line in output.splitlines() if line.startswith("hitat ")]
    assert actual == [hit["text"]] * 9, f"Touch target is covered: {hit}\n{output}"


def expect(output, *texts):
    for text in texts:
        assert text in output, f"Missing {text!r}\n{output}"


def no_command(output, command):
    assert command not in output, f"Unexpected {command!r}\n{output}"
