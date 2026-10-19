#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""coach_arrow PNG master: an up arrow — shaft + chevron head — the coach
chain's flick-up hint, drawn procedurally like the set_* family.

    python3 tools/gen_coach_arrow.py  # writes tools/assets/icons/png/coach_arrow.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 48
SS = 16
NAME = "coach_arrow"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    lw = 4.6 / 52 * s
    cx = 0.50 * s

    top, bot = 0.12 * s, 0.90 * s
    d.line([cx, top, cx, bot], fill=255, width=round(lw))
    wing = 0.30 * s
    d.line([cx, top, cx - wing, top + wing], fill=255, width=round(lw))
    d.line([cx, top, cx + wing, top + wing], fill=255, width=round(lw))

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
