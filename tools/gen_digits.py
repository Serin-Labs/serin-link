#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""digit_1..digit_7 PNG masters: the zone-number rim glyphs (Zones picker —
each zone node draws its slot number instead of the shared set_link_row glyph).

Rendered from the dial's own typeface, tools/PlusJakartaSans-SemiBold.ttf
(weight 600 == the jakarta_600_* hero faces), so the rim numbers match the
dial's numerals. Same supersample+LANCZOS move as gen_set_lock.py: authored
large, downscaled to kill jaggies. Each numeral is centered on an identical
square 132x132 canvas so every digit shares one footprint (uniform node size
on the rim) and the 46/66 bakes in gen_icons.py are clean downscales.

    python3 tools/gen_digits.py   # writes tools/assets/icons/png/digit_N.png
"""
import os

from PIL import Image, ImageDraw, ImageFont

BAKE = 132
SS = 8
GLYPH_FRAC = 0.72
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TTF = os.path.join(ROOT, "tools", "PlusJakartaSans-SemiBold.ttf")
PNGDIR = os.path.join(ROOT, "tools", "assets", "icons", "png")

def render_digit(ch):
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)
    px = int(GLYPH_FRAC * s)
    font = ImageFont.truetype(TTF, px)
    d.text((int(0.2 * s), int(0.1 * s)), ch, fill=255, font=font)
    bbox = im.getbbox()
    glyph = im.crop(bbox)
    gw, gh = glyph.size
    canvas = Image.new("L", (s, s), 0)
    canvas.paste(glyph, ((s - gw) // 2, (s - gh) // 2))
    im = canvas.resize((BAKE, BAKE), Image.LANCZOS)
    return Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])

def main():
    os.makedirs(PNGDIR, exist_ok=True)
    for n in range(1, 8):
        out = os.path.join(PNGDIR, f"digit_{n}.png")
        render_digit(str(n)).save(out)
        print("wrote", out)

if __name__ == "__main__":
    main()
