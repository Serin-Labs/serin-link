#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_night PNG master: a setting half-sun over a horizon bar (Advanced/
Night rim node). Like gen_set_idle.py: no hand-authored master
exists, so it is drawn procedurally in glyph-space, supersampled and
LANCZOS-downscaled. Baked to 52x52 to match the set_* family (gen_icons.py
whitelists the 52->60 bake in UPSCALE_OK).

Flag this as a placeholder: if a design pass produces a real set_night
master, swap the PNG and delete this script's role.

    python3 tools/gen_set_night.py  # writes tools/assets/icons/png/set_night.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_night"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    hy = 0.66 * s
    r = 0.22 * s
    cx = 0.50 * s
    d.ellipse([cx - r, hy - r, cx + r, hy + r], fill=255)
    d.rectangle([0, hy, s, s], fill=0)
    bar = 0.045 * s
    d.rectangle([0.14 * s, hy, 0.86 * s, hy + bar], fill=255)
    ray_w = 0.045 * s
    for ang_dx, x0, y0, y1 in ((0.0, cx, 0.24 * s, 0.34 * s),
                               (-0.20 * s, cx - 0.20 * s, 0.30 * s, 0.385 * s),
                               (0.20 * s, cx + 0.20 * s, 0.30 * s, 0.385 * s)):
        d.line([x0, y0, x0 + ang_dx * 0.0, y1], fill=255, width=int(ray_w))

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
