#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate every UI font offline with the locked lv_font_conv toolchain."""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARTWORK = ROOT / "tools/artwork"
BASE = ["--bpp", "4", "--format", "lvgl", "--no-compress", "--force-fast-kern-format"]
# The bundled fonts omit soft hyphen (00AD), ŉ (0149), and ſ (017F).
LATIN = ["0x00A0-0x00AC", "0x00AE-0x0148", "0x014A-0x017E"]
TEXT = ["0x20-0x7F", "0x00B0", "0x00B7"]
NAME_METRICS = {18: (19, 4), 27: (30, 6), 40: (43, 9)}
FACES = [
    (600, 58, TEXT), (600, 40, TEXT), (600, 15, TEXT),
    (600, 27, TEXT + ["0x2026"]),
    (600, 22, TEXT + ["0x2014", "0x2026"]),
    (600, 18, TEXT + ["0x2014", "0x2192", "0x2194"]),
    (800, 15, ["0x20-0x7F", "0x00B7", "0x2026"]),
    (600, 230, ["0x30-0x39", "0x2014"]),
    (600, 128, ["0x30-0x39"]), (500, 56, ["0x00B0"]),
    (600, 33, ["0x30-0x39", "0x0025"]),
]
WEIGHTS = {500: "Medium", 600: "SemiBold", 800: "ExtraBold"}


def generate(output):
    try:
        node_version = subprocess.check_output(["node", "--version"], text=True).strip()
    except FileNotFoundError:
        raise SystemExit("Missing Node 24.18.0: install it, then run npm ci --prefix tools/artwork --ignore-scripts")
    if node_version != "v24.18.0":
        raise SystemExit(f"Node 24.18.0 required (found {node_version}); select the pinned version before regenerating")
    tool = ARTWORK / "node_modules/lv_font_conv/lv_font_conv.js"
    if not tool.is_file():
        raise SystemExit("Missing font converter: run npm ci --prefix tools/artwork --ignore-scripts")
    version = json.loads(tool.with_name("package.json").read_text())["version"]
    if version != "1.5.3":
        raise SystemExit("Wrong lv_font_conv version; run npm ci --prefix tools/artwork --ignore-scripts")
    # lv_font_conv silently skips unsupported requested characters. Verify the
    # promised Latin repertoire with its locked OpenType parser before output.
    coverage_check = """
const path = require('path');
const converter = process.argv[1];
const opentype = require(require.resolve('opentype.js', {paths: [path.dirname(converter)]}));
for (const source of process.argv.slice(2)) {
    const font = opentype.loadSync(source);
    for (let cp = 0xA0; cp <= 0x17E; cp++) {
        if (cp === 0xAD || cp === 0x149) continue;
        if (!font.charToGlyphIndex(String.fromCodePoint(cp))) {
            throw new Error(source + ': missing promised Latin glyph U+' + cp.toString(16));
        }
    }
}
"""
    subprocess.run(["node", "-e", coverage_check, str(tool),
                    str(ROOT / "tools/PlusJakartaSans-SemiBold.ttf")], check=True)
    output.mkdir(parents=True, exist_ok=True)
    for weight, size, ranges in FACES:
        if weight == 600 and size in NAME_METRICS:
            ranges = ranges + LATIN
        source = f"tools/PlusJakartaSans-{WEIGHTS[weight]}.ttf"
        if not (ROOT / source).is_file():
            raise SystemExit(f"Missing bundled font: {source}; restore it from the public checkout")
        name = f"jakarta_{weight}_{size}.c"
        target = output / name
        args = ["--font", source, "--size", str(size)] + BASE
        for subset in ranges:
            args += ["--range", subset]
        args += ["-o", str(target)]
        subprocess.run(["node", str(tool)] + args, cwd=ROOT, check=True)
        # lv_font_conv embeds its output path. Keep that comment independent of
        # temporary output directories; leave every glyph and metric untouched.
        text = target.read_text()
        lines = text.splitlines(keepends=True)
        lines[3] = lines[3].replace(str(target), f"main/fonts/{name}")
        text = "".join(lines)
        if weight == 600 and size in NAME_METRICS:
            # Share expanded tables, preserving historical ASCII layout metrics.
            # Names with non-ASCII glyphs use an alias with room for full accents.
            start = text.index("/*Initialize a public general font descriptor*/")
            end = text.index("\n};", start) + len("\n};")
            latin = text[start:end].replace(name[:-2], name[:-2] + "_latin")
            height, baseline = NAME_METRICS[size]
            text = re.sub(r"(\.line_height = )\d+", rf"\g<1>{height}", text)
            text = re.sub(r"(\.base_line = )-?\d+", rf"\g<1>{baseline}", text)
            text = text.replace("/*The maximum line height required by the font*/",
                                "/*Historical ASCII line height; names use _latin*/")
            text = text.replace("/*Baseline measured from the bottom of the line*/",
                                "/*Historical ASCII baseline*/")
            end = text.index("\n};", start) + len("\n};")
            text = text[:end] + "\n\n/* Full accent metrics; glyph tables are shared. */\n" + latin + text[end:]
        target.write_text(text)
        print(f"generated main/fonts/{name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "main/fonts")
    generate(parser.parse_args().out_dir.resolve())
