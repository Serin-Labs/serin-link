#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Rasterize the room-source SVG masters. Requires CairoSVG and Pillow.

Run this before tools/gen_icons.py when a room-source SVG changes.
See tools/icon_sources/README.md for source attribution.
"""
from pathlib import Path

import cairosvg

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "tools" / "icon_sources"
PNG = ROOT / "icon_masters" / "icon_assets" / "png"
PNG.mkdir(parents=True, exist_ok=True)

for name in ("room_heatpump", "room_link", "room_sensor"):
    cairosvg.svg2png(
        url=str(SOURCES / f"{name}.svg"),
        write_to=str(PNG / f"{name}.png"),
        output_width=240,
        output_height=240,
    )
