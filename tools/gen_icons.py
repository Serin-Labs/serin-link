#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Generate the Serin A8 icon set (main/icons_serin.c) from the PNG masters.

One A8 variant per (icon, display size): each is an independent Lanczos
resample of icon_masters/icon_assets/png/<name>.png done here, offline — the
firmware then draws every icon at scale ~1.0 (icon_fit() in ui_widgets.c picks
the variant; the only residual runtime scaling is the 466-panel's global
0.9708x and the fan tiles' deliberate above-master upscale).

The size lists mirror the call-site matrix in the icon-crispness requirements: sizes
are 480-space design px (what call sites pass to SERIN_PX), and a variant's
height follows the master's aspect. Baking a size the master cannot supply
without upscaling is an error unless (name, size) is in UPSCALE_OK — an
accidental upscale should fail the build, not silently ship mush.

A size equal to the master's own width is emitted byte-identical (no resample).

Toolchain: Pillow only (system-wide). No network, no cairosvg.
Re-run is idempotent: it overwrites main/icons_serin.c.
    python3 tools/gen_icons.py [--out path]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNGDIR = os.path.join(ROOT, "icon_masters", "icon_assets", "png")
OUT_C = os.path.join(ROOT, "main", "icons_serin.c")
CONTACT = "/tmp/icons_contact.png"

ICONS = [
    ("mode_heat", [54, 80]), ("mode_cool", [54, 80]), ("mode_auto", [54, 80]),
    ("mode_dry", [54, 80]), ("mode_off", [54, 80]),
    ("mode_fan", [40, 45, 54, 60, 80, 86]),
    ("nav_temp", [60, 86]), ("nav_mode_mark", [60, 86]), ("nav_gear", [60, 86]),
    ("nav_back", [45, 46, 60, 66]),
    ("set_brightness", [45, 60]), ("set_units", [45, 60]), ("set_wifi", [45, 60]),
    ("set_homekit", [45, 60]), ("set_system_chip", [45, 60]), ("set_advanced", [45, 60]),
    ("set_lock", [26, 45, 60]), ("set_idle", [45, 60]), ("set_reset", [45, 60]),
    ("set_night", [45, 60]), ("set_transitions", [45, 60]),
    ("set_link_row", [32, 45, 46, 60, 66, 86]),
    ("digit_1", [46, 66]), ("digit_2", [46, 66]), ("digit_3", [46, 66]),
    ("digit_4", [46, 66]), ("digit_5", [46, 66]), ("digit_6", [46, 66]),
    ("digit_7", [46, 66]),
    ("serin_logo", [45, 60, 130, 160]),
    ("room_heatpump", [45, 60]), ("room_link", [45, 60]),
    ("room_sensor", [45, 60]),
    ("hero_alert", [16, 32, 36]),
    ("hero_hotspot", [80]),
    ("hero_batt", [104]),
    ("hero_check", [104]),
    ("coach_arrow", [48]),
    ("coach_turn_cw_l", [128]), ("coach_turn_cw_r", [128]),
    ("coach_turn_ccw_l", [128]), ("coach_turn_ccw_r", [128]),
    ("vane_v_auto", [45, 60, 240]),
    ("vane_v_1", [240]), ("vane_v_2", [240]), ("vane_v_3", [240]),
    ("vane_v_4", [240]), ("vane_v_5", [240]), ("vane_v_swing", [240]),
    ("vane_h_auto", [45, 60, 68, 90, 262]),
    ("vane_h_left", [262]), ("vane_h_cleft", [262]), ("vane_h_center", [262]),
    ("vane_h_cright", [262]), ("vane_h_right", [262]), ("vane_h_swing", [262]),
]

UPSCALE_OK = {
    ("hero_alert", 32), ("hero_alert", 36),
    ("nav_back", 60), ("nav_back", 66),
    ("set_brightness", 60), ("set_units", 60), ("set_wifi", 60),
    ("set_homekit", 60), ("set_system_chip", 60), ("set_advanced", 60),
    ("set_lock", 60), ("set_idle", 60), ("set_reset", 60),
    ("set_night", 60), ("set_transitions", 60),
    ("set_night", 60),
    ("set_link_row", 60), ("set_link_row", 66), ("set_link_row", 86),
}

def render_alpha(name, w, master):
    """One variant: the master's alpha at width w (aspect preserved)."""
    mw, mh = master.size
    if w == mw:
        return master.copy()
    if w > mw and (name, w) not in UPSCALE_OK:
        raise SystemExit(f"{name}@{w}: master is only {mw}px wide — an upscale "
                         f"must be whitelisted in UPSCALE_OK to be deliberate")
    h = max(1, round(mh * w / mw))
    return master.resize((w, h), Image.LANCZOS)

def emit_variant(cname, alpha):
    w, h = alpha.size
    data = alpha.tobytes()
    out = [f"static const uint8_t {cname}_map[] = {{"]
    for i in range(0, len(data), 16):
        out.append(",".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    out += ["};",
            f"static const lv_image_dsc_t {cname} = {{",
            "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
            "    .header.cf = LV_COLOR_FORMAT_A8,",
            f"    .header.w = {w},",
            f"    .header.h = {h},",
            f"    .header.stride = {w},",
            f"    .data_size = {w * h},",
            f"    .data = {cname}_map,",
            "};", ""]
    return "\n".join(out)

def emit_family(name, variants):
    refs = ", ".join(f"&{name}_{w}_dsc" for w, _ in variants)
    return (f"static const lv_image_dsc_t *const {name}_v[] = {{ {refs} }};\n"
            f"const serin_icon_t {name} = {{ {name}_v, {len(variants)} }};\n")

def contact_sheet(items):
    pad, lh, cols = 14, 14, 8
    cell = max(a.size[0] for _, a in items)
    rows = (len(items) + cols - 1) // cols
    W = cols * (cell + pad) + pad
    H = rows * (cell + pad + lh) + pad
    sheet = Image.new("RGB", (W, H), (16, 16, 18))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None
    for idx, (name, alpha) in enumerate(items):
        aw, ah = alpha.size
        cx = pad + (idx % cols) * (cell + pad)
        cy = pad + (idx // cols) * (cell + pad + lh)
        cellimg = Image.new("RGB", (cell, cell), (16, 16, 18))
        tint = Image.new("RGB", (aw, ah), (245, 200, 120))
        cellimg.paste(tint, ((cell - aw) // 2, (cell - ah) // 2), alpha)
        sheet.paste(cellimg, (cx, cy))
        draw.text((cx, cy + cell + 1), name, fill=(190, 190, 190), font=font)
    sheet.save(CONTACT)

def main():
    out_c = OUT_C
    if len(sys.argv) == 3 and sys.argv[1] == "--out":
        out_c = sys.argv[2]
    blocks, sheet_items = [], []
    for name, widths in ICONS:
        assert widths == sorted(set(widths)), f"{name}: sizes must be ascending/unique"
        master = Image.open(os.path.join(PNGDIR, name + ".png")) \
                      .convert("RGBA").split()[3]
        variants = [(w, render_alpha(name, w, master)) for w in widths]
        for w, alpha in variants:
            blocks.append(emit_variant(f"{name}_{w}_dsc", alpha))
        blocks.append(emit_family(name, variants))
        sheet_items.append((name, variants[-1][1]))
    header = ("/* SPDX-License-Identifier: GPL-3.0-only\n"
              " * Copyright (C) 2026 Mehmet Bayram\n"
              " *\n"
              " * icons_serin.c — A8 (alpha-only) Serin icon set: one variant per\n"
              " * (icon, display size), baked from icon_masters/icon_assets/png/ and\n"
              " * recolored at runtime (icon_fit() in ui_widgets.c picks the variant).\n"
              " * Generated by tools/gen_icons.py; do not hand-edit.\n"
              " */\n"
              '#include "icons.h"\n\n')
    with open(out_c, "w") as f:
        f.write(header + "\n".join(blocks))
    contact_sheet(sheet_items)
    n_var = sum(len(w) for _, w in ICONS)
    print(f"wrote {out_c} ({len(ICONS)} icons, {n_var} variants)")
    print(f"wrote {CONTACT}")

if __name__ == "__main__":
    main()
