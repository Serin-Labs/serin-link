#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""set_transitions PNG master: a disc with two receding motion trails
(Advanced/Transitions rim node — the screen fade toggle). Same recovery move as
gen_set_night.py: no hand-authored master exists, so it is drawn procedurally in
glyph-space, supersampled and LANCZOS-downscaled, baked to 52x52 to match the
set_* family (gen_icons.py whitelists the 52->60 bake in UPSCALE_OK).

Placeholder: if a design pass produces a real set_transitions master, swap
the PNG and delete this script's role.

    python3 tools/gen_set_transitions.py  # writes icon_masters/icon_assets/png/set_transitions.png
"""
import os

from PIL import Image, ImageDraw

BAKE = 52
SS = 16
NAME = "set_transitions"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "icon_masters", "icon_assets", "png", NAME + ".png")

def main():
    s = BAKE * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)
    cy = 0.50 * s
    r = 0.19 * s
    cx = 0.62 * s
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    for k, (dx, w, a) in enumerate(((0.20, 0.055, 200), (0.38, 0.045, 120))):
        ccx = cx - dx * s
        rr = r
        d.arc([ccx - rr, cy - rr, ccx + rr, cy + rr], 110, 250, fill=a, width=int(w * s))
    im = im.resize((BAKE, BAKE), Image.LANCZOS)
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rgba.save(OUT)
    print("wrote", OUT, rgba.size)

if __name__ == "__main__":
    main()
