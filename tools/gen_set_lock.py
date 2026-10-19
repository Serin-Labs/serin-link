#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_lock PNG master: a padlock (Advanced/Child Lock rim node — task 3.5,
Advanced screen's rim-radial rework). Same recovery move as gen_set_advanced.py:
no hand-authored master exists for this glyph, so it is drawn procedurally —
authored in glyph-space, supersampled and LANCZOS-downscaled to remove the
jaggies a direct low-res draw would leave. Baked to 52x52 to match every other
set_* master exactly (gen_icons.py's UPSCALE_OK already whitelists that
family's 52->60 bake; set_lock's 60 variant is added there too).

Procedural glyph; no hand-authored master exists.
If a real design pass ever produces a proper set_lock master, swap the PNG and
delete this script's role.

    python3 tools/gen_set_lock.py   # writes tools/assets/icons/png/set_lock.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_lock"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    lw = 4.2 / 52 * s

    shackle_box = [0.28 * s, 0.10 * s, 0.72 * s, 0.54 * s]
    d.arc(shackle_box, start=180, end=360, fill=255, width=round(lw))

    body = [0.18 * s, 0.42 * s, 0.82 * s, 0.86 * s]
    d.rounded_rectangle(body, radius=0.08 * s, fill=255)

    kx, ky = 0.50 * s, 0.58 * s
    kr = 0.07 * s
    d.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=0)
    d.polygon([(kx - kr * 0.55, ky + kr * 0.3), (kx + kr * 0.55, ky + kr * 0.3),
               (kx + kr * 0.3, ky + kr * 1.9), (kx - kr * 0.3, ky + kr * 1.9)], fill=0)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
