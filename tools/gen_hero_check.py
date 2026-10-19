#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""hero_check PNG master: a ring with a check mark, drawn with Pillow.

Like gen_hero_batt.py, this icon has no recovered PNG master — it is new
(the Wi-Fi wizard's success faces) — so its master is drawn here,
supersampled and LANCZOS-downscaled, then baked by gen_icons.py like any other.

Baked at 104x104 = the display size (the success faces show it at SERIN_PX(104)),
so gen_icons.py emits the variant byte-identical and mkicon() draws at ~1:1.
The geometry is authored in 64-space and scaled, so the stroke weight stays
proportional at any BAKE.

    python3 tools/gen_hero_check.py     # writes tools/assets/icons/png/hero_check.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 104
SS = 16
K = BAKE / 64
NAME = "hero_check"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    ring_w = 4.5 * K * SS
    inset = ring_w / 2 + 1.5 * K * SS
    d.ellipse([inset, inset, s - inset, s - inset], outline=255, width=round(ring_w))

    pts = [(19 * K * SS, 33 * K * SS), (28 * K * SS, 42 * K * SS), (45 * K * SS, 24 * K * SS)]
    lw = 5.5 * K * SS
    d.line(pts, fill=255, width=round(lw), joint="curve")
    for x, y in (pts[0], pts[2]):
        r = lw / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
