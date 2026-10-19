#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Rebuild the vane louver PNG masters from their SVGs — the true vectors.

The 14 louver states in tools/assets/icons/svg/ are the source vectors.
This renders each at 4x
its SVG canvas with cairosvg and writes tools/assets/icons/png/<name>.png
for gen_icons.py to bake, replacing the old bake-resolution recoveries.

Two deliberate choices:
- `currentColor` is replaced with #FFFFFF before rasterizing. Only the alpha
  channel matters downstream, but rasterizers disagree on what currentColor
  means; solid white sidesteps the whole pitfall. The per-slat fill-opacity
  (bright vs faint louvers) lands in the alpha exactly as designed.
- The v masters keep their full square canvas (ui_fan's offsets anchor the
  blade region against it); the h masters are alpha-cropped tight, matching
  how their rasters were always framed. NOTE the h SVGs' natural proportions
  are WIDER than the earlier shipped rasters (which were a squashed
  export) — adopting the vector look was a deliberate call, and ui_fan's
  FAN_HVANE_* nudges are tuned against THIS framing.

    python3 tools/gen_vane_masters.py     # then: python3 tools/gen_icons.py
"""
import io
import os
import re

import cairosvg
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVGDIR = os.path.join(ROOT, "tools", "assets", "icons", "svg")
PNGDIR = os.path.join(ROOT, "tools", "assets", "icons", "png")
SS = 4

STATES = (["vane_v_auto"] + [f"vane_v_{i}" for i in range(1, 6)] + ["vane_v_swing"] +
          ["vane_h_" + s for s in
           ["auto", "left", "cleft", "center", "cright", "right", "swing"]])

def main():
    for name in STATES:
        svg = open(os.path.join(SVGDIR, name + ".svg")).read()
        svg = svg.replace("currentColor", "#FFFFFF")
        w_attr = int(re.search(r'width="(\d+)"', svg).group(1))
        png = cairosvg.svg2png(bytestring=svg.encode(), output_width=w_attr * SS)
        alpha = Image.open(io.BytesIO(png)).convert("RGBA").split()[3]
        if name.startswith("vane_h"):
            alpha = alpha.crop(alpha.getbbox())
        out = Image.merge("RGBA", (*(Image.new("L", alpha.size, 255),) * 3, alpha))
        out.save(os.path.join(PNGDIR, name + ".png"))
        print(f"wrote {name:16s} {alpha.size[0]}x{alpha.size[1]}")
    print("now run: python3 tools/gen_icons.py")

if __name__ == "__main__":
    main()
