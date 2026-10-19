#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_reset PNG master: a circular rotate-arrow (Advanced/Reset dial rim node
— task 3.5, Advanced screen's rim-radial rework). Same recovery move as
gen_set_advanced.py: no hand-authored master exists for this glyph, so it is
drawn procedurally — authored in glyph-space, supersampled and
LANCZOS-downscaled to remove the jaggies a direct low-res draw would leave.
Baked to 52x52 to match every other set_* master exactly (gen_icons.py's
UPSCALE_OK already whitelists that family's 52->60 bake; set_reset's 60
variant is added there too).

Procedural glyph; no hand-authored master exists.
If a real design pass ever produces a proper set_reset master, swap the PNG
and delete this script's role.

    python3 tools/gen_set_reset.py   # writes tools/assets/icons/png/set_reset.png
"""
import math
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_reset"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    lw = 4.6 / 52 * s
    cx, cy = 0.50 * s, 0.50 * s
    r = 0.32 * s
    box = [cx - r, cy - r, cx + r, cy + r]

    start, end = -55, 235
    d.arc(box, start=start, end=end, fill=255, width=round(lw))

    end_rad = math.radians(end)
    tip = (cx + r * math.cos(end_rad), cy + r * math.sin(end_rad))
    tangent = end_rad + math.pi / 2
    ah = 0.155 * s
    aw = 0.60

    back = (tip[0] - ah * math.cos(tangent), tip[1] - ah * math.sin(tangent))
    perp = tangent + math.pi / 2
    p1 = (back[0] + ah * aw * math.cos(perp), back[1] + ah * aw * math.sin(perp))
    p2 = (back[0] - ah * aw * math.cos(perp), back[1] - ah * aw * math.sin(perp))
    fwd = (tip[0] + ah * 0.35 * math.cos(tangent), tip[1] + ah * 0.35 * math.sin(tangent))
    d.polygon([fwd, p1, p2], fill=255)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
