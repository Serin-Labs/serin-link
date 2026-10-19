# Room Sensor icons

These SVGs preserve the geometry of existing Serin artwork:

- `room_heatpump.svg`: the Serin indoor wall-mounted unit illustration.
- `room_link.svg`: the Serin Link icon from the Serin Labs website.
- `room_sensor.svg`: Phosphor's regular thermometer (MIT; see
  `room_sensor.LICENSE`).

The public regeneration inputs are the matching PNGs in
[`../assets/icons/png/`](../assets/icons/png/). Their alpha channels reproduce
the checked-in firmware icons without requiring a system SVG renderer.
See [`../assets/README.md`](../assets/README.md) for setup, provenance, and checks.

To edit a room icon, install the optional pinned authoring dependencies in the
same virtual environment (`python -m pip install -r
tools/artwork/requirements-svg.txt`; system libcairo is also required), then run
`python tools/gen_room_source_icons.py` followed by `python tools/artwork.py`.
This updates the tracked 240-pixel PNG sources and their 45- and 60-pixel masks.
SVG rasterization can vary with system libcairo; inspect any alpha changes and
update `tools/assets/SOURCES.json` when a source changes. Firmware builds use
committed masks and need neither Python nor CairoSVG.
