# Reproducible artwork

A public checkout contains every input required to regenerate
`main/icons_serin.c` and `main/fonts/*.c`. Firmware builds use those committed
C files and do not require the artwork tools.

From the repository root, use Python 3.11 or newer and Node **24.18.0**:

```sh
python3 -m venv tools/artwork/.venv
tools/artwork/.venv/bin/python -m pip install -r tools/artwork/requirements.txt
npm ci --prefix tools/artwork --ignore-scripts

tools/artwork/.venv/bin/python tools/artwork.py --check
tools/artwork/.venv/bin/python tools/test_artwork.py
tools/artwork/.venv/bin/python tools/test_recover_icons.py
```

Only dependency installation needs network access. The check runs the real icon
resampler and font converter in a temporary directory and byte-compares all
outputs, including missing or obsolete font C files. It leaves the checkout
untouched and exits nonzero with affected paths when output differs. Run
`tools/artwork/.venv/bin/python tools/artwork.py` to regenerate, then review and
commit both sources and outputs. All generators resolve inputs relative to
this repository, regardless of the working directory. The dedicated
[artwork CI workflow](../../.github/workflows/artwork.yml) verifies regeneration.

Pillow **12.3.0** is the only Python dependency of the regeneration contract.
`tools/artwork/package-lock.json` locks **lv_font_conv 1.5.3**, its bundled
JavaScript dependencies, and package integrity. The font converter is invoked
locally; no global converter, mutable `npx`, system font, or `/tmp` font is used.
See the [font recipe](../../main/fonts/README_fonts.md) for ranges and licensing.

## Icon provenance

`icons/png/` contains the 56 input images used by `tools/gen_icons.py`; only
alpha contributes to the 110 firmware variants. These are public source inputs,
not all original editable vectors. `SOURCES.json` records their SHA-256 digests,
dimensions, origin, and license alongside the bundled font hashes.

The original larger PNG masters are not available. The older masters were
recovered from baked A8 firmware arrays at bake resolution (roughly 26–192
pixels). This repository includes those recovered PNGs directly, so
regeneration needs no other files. The recovery tool is not used as a source
generator.

The available inputs are:

- The `mode_*`, `nav_*`, older `set_*`, `hero_alert`, and `serin_logo` PNGs
  are recovered rasters.
- Battery and fourteen vane geometries survive in `icons/svg/`. The battery's
  Pillow recipe is `tools/gen_hero_batt.py`; vanes use
  `tools/gen_vane_masters.py`.
- Digits, coaching arrows, hotspot/check, and newer settings glyphs have
  procedural `tools/gen_*.py` recipes. Their PNGs remain explicit committed
  inputs. `set_transitions.png` is rendered by its recipe.
- The three room PNGs were rendered from the already-public
  `tools/icon_sources/room_*.svg` files using CairoSVG 2.8.2. Their 45- and
  60-pixel masks exactly match the prior committed firmware masks.

Preserving these inputs reproduces every existing icon alpha byte. The few
intentional upscales are listed in `UPSCALE_OK`; a larger replacement source
removes the need for its exception.

## Authoring and inspection

PNG files are the stable raster inputs for the reproducibility check. To change
a procedural icon, run its `tools/gen_*.py` recipe, then `tools/artwork.py`.
SVG authoring additionally needs system libcairo and the optional fully pinned
Python package set in `tools/artwork/requirements-svg.txt`:

```sh
tools/artwork/.venv/bin/python -m pip install -r tools/artwork/requirements-svg.txt
tools/artwork/.venv/bin/python tools/gen_vane_masters.py
tools/artwork/.venv/bin/python tools/gen_room_source_icons.py
```

These optional SVG rasterizers are authoring tools. Their output can vary with
system libcairo, so they do not run as part of the byte-reproduction contract.
Inspect intentional changes, update input hashes in `SOURCES.json`, regenerate,
and describe any resulting UI/UX change in the pull request.

For a contact sheet, run `python tools/gen_icons.py --contact-sheet /path/icons.png`.
For PNG previews of existing baked variants, run:

```sh
python tools/recover_icons.py --out-dir /path/to/baked-previews
```

The exporter understands current per-size descriptors and requires an explicit
output directory. It refuses to write into `tools/assets/icons/`: resampled
variants are not replacements for higher-resolution input masters.

## Licenses and attribution

Project tools and icon artwork are GPL-3.0-only under the repository's existing
license declaration. The Serin name and logo remain trademarks. The
`room_sensor` source and its derived alpha masks retain the Phosphor Icons MIT
exception: Copyright (c) 2023 Phosphor Icons, with the full notice in
[`../icon_sources/room_sensor.LICENSE`](../icon_sources/room_sensor.LICENSE).
The other room source paths remain documented in
[`../icon_sources/README.md`](../icon_sources/README.md).

The three Plus Jakarta Sans TTFs and generated font subsets are SIL OFL 1.1,
Copyright 2020 The Plus Jakarta Sans Project Authors. Existing OFL texts remain
in `tools/PlusJakartaSans-OFL.txt` and `main/fonts/LICENSE-OFL.txt`. Medium and
ExtraBold were fetched from the same immutable upstream revision whose SemiBold
file exactly matches the previously tracked font. No third-party notice has
been replaced by the project GPL notice.
