#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""hero_hotspot PNG master: the WiFi fan — three arcs over a dot.

The recovered master is 58x58, which caps the bake below hero size — and the
DIAL_WIFI_OFF face wants the glyph as its hero, not a badge. So the master is
redrawn here at hero size like gen_hero_check.py: authored in glyph-space,
supersampled and LANCZOS-downscaled, then baked by gen_icons.py.

Baked at 80x80 = the display size (the home entry face shows it at
SERIN_PX(80)), so gen_icons.py emits the variant byte-identical and mkicon()
draws at ~1:1.

    python3 tools/gen_hero_hotspot.py   # writes icon_masters/icon_assets/png/hero_hotspot.png
"""
import math
import os

from PIL import Image, ImageDraw

BAKE = 80
SS = 16
NAME = "hero_hotspot"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "icon_masters", "icon_assets", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    k = BAKE / 80 * SS
    cx, cy = 40 * k, 63 * k
    lw = 6.5 * k
    a0, a1 = 225, 315

    for r in (16 * k, 29 * k, 42 * k):
        box = [cx - r, cy - r, cx + r, cy + r]
        d.arc(box, start=a0, end=a1, fill=255, width=round(lw))
        rm = r - lw / 2
        for a in (a0, a1):
            x = cx + rm * math.cos(math.radians(a))
            y = cy + rm * math.sin(math.radians(a))
            cr = lw / 2
            d.ellipse([x - cr, y - cr, x + cr, y + cr], fill=255)

    dot = 5.5 * k
    d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=255)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
