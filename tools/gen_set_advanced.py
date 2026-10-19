#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_advanced PNG master: three horizontal slider tracks with offset knobs
(the Settings-ring "Advanced" node).

Unlike its set_* siblings (hand-authored 52x52 PNGs), this glyph has no
hand-authored master, so it is drawn procedurally here, like
gen_hero_check.py / gen_hero_hotspot.py: authored in
glyph-space, supersampled and LANCZOS-downscaled to remove the jaggies a
direct low-res draw would leave. Baked to 52x52 to match every other set_*
master exactly (gen_icons.py's UPSCALE_OK already whitelists that family's
52->60 bake; set_advanced's 60 variant is added there too).

Flag this as a placeholder if a real design pass ever produces a proper
set_advanced master — swap the PNG and delete this script's role, same as the
hero_* masters' own note about their eventual real artwork.

    python3 tools/gen_set_advanced.py   # writes tools/assets/icons/png/set_advanced.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_advanced"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "assets", "icons", "png", NAME + ".png")

ROWS = [(0.30, 0.65), (0.50, 0.35), (0.70, 0.55)]

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)

    lw = 4.2 / 52 * s
    kr = 6.0 / 52 * s
    x0, x1 = 0.14 * s, 0.86 * s

    for yf, kf in ROWS:
        y = yf * s
        d.line([(x0, y), (x1, y)], fill=255, width=round(lw))
        for x in (x0, x1):
            d.ellipse([x - lw / 2, y - lw / 2, x + lw / 2, y + lw / 2], fill=255)
        kx = kf * s
        d.ellipse([kx - kr, y - kr, kx + kr, y + kr], fill=255)

    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
