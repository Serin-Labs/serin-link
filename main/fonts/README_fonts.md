# Fonts

## Licensing

The `.c` files in this directory are generated subsets of **Plus Jakarta Sans**,
and they are *not* under the repository's GPL-3.0-only licence. The font
software is:

> Copyright 2020 The Plus Jakarta Sans Project Authors
> (https://github.com/tokotype/PlusJakartaSans)

licensed under the SIL Open Font License 1.1. The full text is in
[`LICENSE-OFL.txt`](LICENSE-OFL.txt) beside this file.

The generated C files carry no SPDX header; the OFL notice lives beside them.
`tools/PlusJakartaSans-{Medium,SemiBold,ExtraBold}.ttf` are the bundled inputs,
covered by the same OFL notice in `tools/PlusJakartaSans-OFL.txt`. Their upstream
revision and SHA-256 digests are recorded in
[`tools/assets/SOURCES.json`](../../tools/assets/SOURCES.json).


Every face the UI renders is a generated **Plus Jakarta Sans** subset in this
directory. `main/ui_fonts_dial.h` declares them all, and it documents the type
scale, the four single-purpose faces, and the subset ranges. The build compiles
no built-in LVGL font. `sdkconfig.defaults` leaves every Montserrat size off,
and it points `LV_FONT_DEFAULT` at `unscii_8` only because LVGL needs *some*
default. No label uses that default.

`main/CMakeLists.txt` globs `fonts/*.c` and defines `LV_LVGL_H_INCLUDE_SIMPLE`,
so any `.c` file you drop here compiles.

**You must pass `--no-compress` when you generate a face.** This build leaves
`LV_USE_FONT_COMPRESSED` off, so a compressed glyph bitmap decodes to blank.

## Serin set (Plus Jakarta Sans) — `ui_fonts_dial.h`

The UI uses Plus Jakarta Sans for all text. Generate it at the **480 design
sizes**; on the 466 board those sizes are scaled by about 3%. The source is the
**tokotype/PlusJakartaSans** static TTF set (OFL). `main/ui_fonts_dial.h` declares each face as `FONT_*`.

> **A centre hint or caption** ("Turn or tap", "Room", and so on) renders in
> `FONT_HINT` (`jakarta_600_15`, sentence case).

**The type scale is six steps, about 1.22 apart: 15 · 18 · 22 · 27 · 40 · 58.**
New text picks an existing step. The four faces outside that scale (230, 128,
56, 33) are single-purpose glyph subsets, not steps. `ui_fonts_dial.h` states
the rule. This file is how you reproduce it.

From the repository root, install the isolated toolchain once:

```bash
python3 -m venv tools/artwork/.venv
tools/artwork/.venv/bin/python -m pip install -r tools/artwork/requirements.txt
npm ci --prefix tools/artwork --ignore-scripts
```

Use Node **24.18.0**. `tools/artwork/package-lock.json` pins **lv_font_conv
1.5.3**, including its bundled dependency integrity. Font generation reads only
the three checked-in static TTFs from upstream revision
`6d4ffaf7a0c04a1bc2f31be2f1069e84d2433797`; it never downloads a font.

```bash
# Regenerate all icon and font C files (offline after dependency setup).
tools/artwork/.venv/bin/python tools/artwork.py

# Independently regenerate in a temporary directory, compare, and leave files intact.
tools/artwork/.venv/bin/python tools/artwork.py --check

# Font-only generation, if needed.
tools/artwork/.venv/bin/python tools/gen_fonts.py
```

[`tools/gen_fonts.py`](../../tools/gen_fonts.py) is the executable subset recipe.
It preserves `--no-compress`, the four-bit bitmap, fast kerning format, all
sizes, and the explicit ranges of every face. The 600 / 18, 27 and 40 faces
used for controller names also include U+00A0–00AC, U+00AE–0148,
U+014A–017E. This is the supported part of Latin-1 Supplement and Latin
Extended-A: the bundled TTF omits soft hyphen (U+00AD), ŉ (U+0149), and
ſ (U+017F). The generator verifies every promised Latin code point against the
bundled SemiBold TTF with the locked OpenType parser before conversion.
Precomposed names such as Küche, Niñez, Łódź and İğdır are supported;
combining accents, emoji, CJK and arbitrary Unicode are outside this subset.
No fallback font or transliteration is configured.

Each expanded face has a `_latin` descriptor with the converter's full accent
line height and baseline. It shares the same glyph and kerning tables with the
original descriptor, which retains its historical line height and baseline.
`ui_name_label` selects the taller descriptor only when a name contains
non-ASCII bytes and gives that label a full single-line height. ASCII labels
retain their previous glyph bitmaps, metrics, effective kerning and positioning.
The other eight generated faces remain byte-identical. This avoids both clipped
accents in scrolling name labels and layout changes for existing text.

The original subsets and extra punctuation are:

| Face | Subset |
| --- | --- |
| 600 / 58, 15 | ASCII, °, · |
| 600 / 40 | ASCII and supported Latin ranges above |
| 600 / 27 | ASCII, supported Latin, … |
| 600 / 22 | ASCII, °, ·, —, … |
| 600 / 18 | ASCII, supported Latin, —, →, ↔ |
| 800 / 15 | ASCII, ·, … only |
| 600 / 230 | Digits and — only |
| 600 / 128 | Digits only |
| 500 / 56 | ° only |
| 600 / 33 | Digits and % only |

Generation reproduces checked-in files byte for byte. Generated header comments
use repository-relative paths, so running in another checkout or temporary
folder does not change them. When this reproducibility toolchain was introduced,
only those comments changed: all glyph bitmaps, character maps, kerning, and
metrics remained byte-identical. `--check` reports the path of each changed,
missing, or obsolete generated file. Review intentional changes and commit the
source and regenerated output together. The CI artwork workflow runs this check.

**Subsets bite, and the simulator catches them.** A label that reaches for a
glyph its face lacks renders a placeholder box, called tofu. A `labels` dump
still contains the original text. Run `fontcheck` after a label is visible to
check its actual face; a missing glyph fails the command and, in headless
`--run` or `--script`, the process. Face-specific punctuation remains limited:
`—` (U+2014), `…` (U+2026), `→` (U+2192), and `↔` (U+2194) appear only in the
faces listed above. Plus Jakarta Sans contains no U+2713, so the wizard chip's
check mark is the baked hero_check icon at 16 px, not a glyph.

Check whenever you widen a range, repoint a `FONT_*` macro, or write copy outside
plain ASCII. `test/ui_sim/name_fonts.sh` checks all supported non-ASCII Latin
code points through a real picker label, representative zone and source names
on other surfaces, UTF-8 truncation boundaries, and missing-glyph failures.
Controller name fields remain 32 bytes for zones and 24 for sources. Existing
NUL-terminated storage leaves 31/23 UTF-8 bytes, respectively. Display copies
stop before a malformed or partial UTF-8 sequence; this does not add glyphs or
normalize combining sequences.

**You must pass `--no-compress`.** This build leaves `LV_USE_FONT_COMPRESSED`
off, as LVGL's built-ins do, so a compressed glyph bitmap decodes to blank.
