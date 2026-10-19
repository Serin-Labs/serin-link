#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Rebuild hero_batt's PNG master from its SVG — the one surviving vector.

hero_batt.svg lives in tools/assets/icons/svg/. Other surviving vectors and
procedural recipes are documented in tools/assets/README.md; some older icons
have only recovered bake-resolution PNG sources. This script re-rasters the
battery geometry into that PNG set, and gen_icons.py bakes it like any icon.

The SVG is three rounded rects, so it is rasterised directly with Pillow
rather than an SVG library — no cairosvg, no `currentColor` pitfall. Drawn at
SS x supersample and downscaled LANCZOS.

    python3 tools/gen_hero_batt.py        # writes png/hero_batt.png
"""
import os

from PIL import Image, ImageDraw

VB_W, VB_H = 52, 26
BAKE_W, BAKE_H = 104, 52
SS = 16

NAME = "hero_batt"

def render_alpha():
    """The three rects of hero_batt.svg, drawn into an alpha-only image."""
    s = SS * BAKE_W / VB_W
    im = Image.new("L", (BAKE_W * SS, BAKE_H * SS), 0)
    d = ImageDraw.Draw(im)

    def rr(x, y, w, h, r, fill=False, stroke_w=0.0):
        box = [x * s, y * s, (x + w) * s, (y + h) * s]
        if fill:
            d.rounded_rectangle(box, radius=r * s, fill=255)
        else:
            d.rounded_rectangle(box, radius=r * s, outline=255,
                                width=max(1, round(stroke_w * s)))

    rr(1.5, 4, 42, 18, 4.5, fill=False, stroke_w=2.4)
    rr(45.5, 9.5, 4.2, 7, 1.6, fill=True)
    rr(5, 7.5, 8, 11, 2, fill=True)

    return im.resize((BAKE_W, BAKE_H), Image.LANCZOS)

if __name__ == "__main__":
    alpha = render_alpha()
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "tools", "assets", "icons", "png", NAME + ".png")
    Image.merge("RGBA", (*(Image.new("L", alpha.size, 255),) * 3, alpha)).save(out)
    print(f"wrote {out} — now run: python3 tools/gen_icons.py")
