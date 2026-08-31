#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Recover the icon masters from the baked A8 arrays in main/icons_serin.c.

The designed PNG masters in icon_masters/icon_assets/png/ were never committed;
only the baked output survived. The bakes are the artwork at 80-192px — larger
than every display size — so decoding them back to PNGs re-establishes a master
set that tools/gen_icons.py can consume. These recovered masters are
bake-resolution, NOT the lost 2x originals (see the README written next to them).

Round-trip safety: for every icon, re-encoding the recovered PNG's alpha channel
must reproduce the original byte array exactly. The tool asserts this itself.

Usage: python3 tools/recover_icons.py [path/to/icons_serin.c]
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "main", "icons_serin.c")
OUTDIR = os.path.join(ROOT, "icon_masters", "icon_assets", "png")

MAP_RE = re.compile(
    r"static const uint8_t (\w+)_map\[\]\s*=\s*\{([^}]*)\};", re.S)
DSC_RE = re.compile(
    r"const lv_image_dsc_t (\w+)\s*=\s*\{(.*?)\};", re.S)
FIELD_RE = re.compile(r"\.header\.(w|h|stride)\s*=\s*(\d+)")

def parse(src_text):
    """-> {name: (w, h, stride, bytes)} for every baked icon in the file."""
    if "serin_icon_t" in src_text:
        raise SystemExit(
            "this looks like the per-size-variant format (serin_icon_t) — the\n"
            "masters were already recovered and live in\n"
            "icon_masters/icon_assets/png/. Recovering variant bakes would only\n"
            "pollute that set with <name>_<px>_dsc.png files. If you really need\n"
            "to re-run this, point it at the pre-variant icons_serin.c from git\n"
            "history: git show 0406d5c~1:main/icons_serin.c > /tmp/old.c")
    maps = {m.group(1): bytes(
                int(tok, 16) for tok in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(2)))
            for m in MAP_RE.finditer(src_text)}
    icons = {}
    for m in DSC_RE.finditer(src_text):
        name, body = m.group(1), m.group(2)
        fields = dict(FIELD_RE.findall(body))
        if not {"w", "h", "stride"} <= fields.keys():
            raise SystemExit(f"{name}: dsc missing header fields")
        w, h, stride = int(fields["w"]), int(fields["h"]), int(fields["stride"])
        if name not in maps:
            raise SystemExit(f"{name}: dsc without a _map array")
        icons[name] = (w, h, stride, maps[name])
    if not icons:
        raise SystemExit("no icons parsed — wrong file?")
    return icons

def main():
    os.makedirs(OUTDIR, exist_ok=True)
    with open(SRC) as f:
        icons = parse(f.read())
    for name, (w, h, stride, data) in sorted(icons.items()):
        if len(data) != stride * h:
            raise SystemExit(f"{name}: {len(data)} bytes != stride {stride} x h {h}")
        alpha = b"".join(data[r * stride: r * stride + w] for r in range(h))
        im = Image.merge("RGBA", (
            *(Image.new("L", (w, h), 255),) * 3,
            Image.frombytes("L", (w, h), alpha)))
        assert im.split()[3].tobytes() == alpha, f"{name}: round-trip mismatch"
        im.save(os.path.join(OUTDIR, name + ".png"))
        print(f"recovered {name:20s} {w}x{h}")
    print(f"wrote {len(icons)} masters to {OUTDIR}")

if __name__ == "__main__":
    main()
