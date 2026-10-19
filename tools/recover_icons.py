#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Export baked A8 descriptors as PNG previews, including current per-size variants.

This is an inspection/migration utility, NOT the artwork regeneration pipeline.
Exports contain already-resampled display pixels and cannot reconstruct a lost
higher-resolution source. Public source masters live in tools/assets/icons/.

Usage: python tools/recover_icons.py [icons_serin.c] --out-dir /path/to/exports
"""
import argparse
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAP_RE = re.compile(
    r"static const uint8_t (\w+)_map\[\]\s*=\s*\{([^}]*)\};", re.S)
DSC_RE = re.compile(
    r"const lv_image_dsc_t (\w+)\s*=\s*\{(.*?)\};", re.S)
FIELD_RE = re.compile(r"\.header\.(w|h|stride)\s*=\s*(\d+)")

def parse(src_text):
    """-> {name: (w, h, stride, bytes)} for every baked icon in the file."""
    maps = {m.group(1): bytes(
                int(tok, 16) for tok in re.findall(r"0x[0-9A-Fa-f]{2}", m.group(2)))
            for m in MAP_RE.finditer(src_text)}
    icons = {}
    for m in DSC_RE.finditer(src_text):
        name, body = m.group(1), m.group(2)
        fields = dict(FIELD_RE.findall(body))
        if not {"w", "h", "stride"} <= fields.keys():
            raise SystemExit(f"{name}: dsc missing header fields")
        w, h, stride = int(fields["w"]), int(fields["h"]), int(fields["stride"])
        if name not in maps:
            raise SystemExit(f"{name}: dsc without a _map array")
        icons[name] = (w, h, stride, maps[name])
    if not icons:
        raise SystemExit("no icons parsed — wrong file?")
    return icons

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", default=os.path.join(ROOT, "main", "icons_serin.c"))
    parser.add_argument("--out-dir", required=True, help="destination for baked previews, never source masters")
    args = parser.parse_args()
    outdir = os.path.abspath(args.out_dir)
    source_dir = os.path.join(ROOT, "tools", "assets", "icons")
    if os.path.commonpath([os.path.realpath(outdir), source_dir]) == source_dir:
        raise SystemExit("Export to a separate directory; baked variants must not replace source masters")
    os.makedirs(outdir, exist_ok=True)
    with open(args.source) as f:
        icons = parse(f.read())
    for name, (w, h, stride, data) in sorted(icons.items()):
        if len(data) != stride * h:
            raise SystemExit(f"{name}: {len(data)} bytes != stride {stride} x h {h}")
        alpha = b"".join(data[r * stride: r * stride + w] for r in range(h))
        im = Image.merge("RGBA", (
            *(Image.new("L", (w, h), 255),) * 3,
            Image.frombytes("L", (w, h), alpha)))
        assert im.split()[3].tobytes() == alpha, f"{name}: round-trip mismatch"
        im.save(os.path.join(outdir, name + ".png"))
        print(f"exported {name:20s} {w}x{h}")
    print(f"exported {len(icons)} baked previews to {outdir}")

if __name__ == "__main__":
    main()
