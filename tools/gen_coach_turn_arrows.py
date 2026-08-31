#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""coach_turn_* PNG masters: the coach Turn step's curved rotation arrows
Four masters — {10, 2 o'clock}
x {CW, CCW} — because LVGL can neither mirror an image at runtime nor make a
CCW arrow by rotating a CW one (rotation preserves chirality). Only the two
CW shapes are drawn; the CCW pair is their vertical mirror (mirroring maps
clock angle a -> -a AND flips chirality, so mirror(CW@2) = CCW@10 and
mirror(CW@10) = CCW@2), which keeps the pairs exactly symmetric for free.
Drawn procedurally, like the set_* family.

Each canvas is BOX x BOX centred on the arc segment's midpoint ON the dial
circle (radius R at the slot's clock angle) — ui_coach.c places the image by
centring it on serin_px_x/y(slot_clock, R), so the master's own crop IS the
positioning contract.

    python3 tools/gen_coach_turn_arrows.py  # writes 4 PNGs, then re-run gen_icons.py
"""
import math
import os

from PIL import Image, ImageDraw

BOX = 128
SS = 8
R = 190
SPAN = 30.0
LW = 5.0
WING = 14.0
HEAD_DEG = 35
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "icon_masters", "icon_assets", "png")

def draw_cw(clock_deg):
    """CW arrow whose arc midpoint sits at clock_deg (0 = 12 o'clock, CW
    positive, y down), head at the +SPAN/2 end (the direction CW travel
    moves). Returns an L-mode BOX x BOX image."""
    s = BOX * SS
    im = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(im)
    a_mid = math.radians(clock_deg)

    def pt(t_deg, rad=R):
        t = math.radians(t_deg)
        x = rad * math.sin(t) - R * math.sin(a_mid)
        y = -rad * math.cos(t) + R * math.cos(a_mid)
        return ((x + BOX / 2) * SS, (y + BOX / 2) * SS)

    a0, a1 = clock_deg - SPAN / 2, clock_deg + SPAN / 2
    steps = 48
    poly = [pt(a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
    d.line(poly, fill=255, width=round(LW * SS), joint="curve")

    t1 = math.radians(a1)
    back = math.atan2(-math.sin(t1), -math.cos(t1))
    tip = pt(a1)
    for sgn in (+1, -1):
        ang = back + sgn * math.radians(HEAD_DEG)
        d.line([tip, (tip[0] + math.cos(ang) * WING * SS,
                      tip[1] + math.sin(ang) * WING * SS)],
               fill=255, width=round(LW * SS), joint="curve")
    return im.resize((BOX, BOX), Image.LANCZOS)

def save(im, name):
    rgba = Image.merge("RGBA", [Image.new("L", im.size, 255)] * 3 + [im])
    p = os.path.join(OUT, name + ".png")
    rgba.save(p)
    print("wrote", p, rgba.size)

def main():
    cw_r = draw_cw(+60)
    cw_l = draw_cw(-60)
    save(cw_r, "coach_turn_cw_r")
    save(cw_l, "coach_turn_cw_l")
    save(cw_r.transpose(Image.FLIP_LEFT_RIGHT), "coach_turn_ccw_l")
    save(cw_l.transpose(Image.FLIP_LEFT_RIGHT), "coach_turn_ccw_r")

if __name__ == "__main__":
    main()
