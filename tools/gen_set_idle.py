#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_idle PNG master: a crescent moon (Advanced/Idle screen rim node —
task 3.5, Advanced screen's rim-radial rework). Same recovery move as
gen_set_advanced.py: no hand-authored master exists for this glyph, so it is
drawn procedurally — authored in glyph-space, supersampled and
LANCZOS-downscaled to remove the jaggies a direct low-res draw would leave.
Baked to 52x52 to match every other set_* master exactly (gen_icons.py's
UPSCALE_OK already whitelists that family's 52->60 bake; set_idle's 60 variant
is added there too).

Procedural glyph; no hand-authored master exists.
If a real design pass ever produces a proper set_idle master, swap the PNG and
delete this script's role.

    python3 tools/gen_set_idle.py   # writes tools/assets/icons/png/set_idle.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_idle"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    r = 0.36 * s
    cx, cy = 0.50 * s, 0.50 * s
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)

    r2 = 0.32 * s
    cx2, cy2 = cx + 0.22 * s, cy - 0.12 * s
    d.ellipse([cx2 - r2, cy2 - r2, cx2 + r2, cy2 + r2], fill=0)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
