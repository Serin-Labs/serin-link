# Room Sensor icons

These SVGs preserve the geometry of the existing Serin project assets:

- `room_heatpump.svg`: the Serin indoor wall-mounted unit illustration.
- `room_link.svg`: `serin-labs.github.io/assets/icons/serin-link.svg`.
- `room_sensor.svg`: a Serin thermometer icon
  (Phosphor's regular thermometer, MIT; see `room_sensor.LICENSE`).

Run `python3 tools/gen_room_source_icons.py` with CairoSVG installed to render
temporary 240-pixel PNG masters under `icon_masters/`, then `python3 tools/gen_icons.py` to regenerate the
45- and 60-pixel alpha masks used by the shared firmware ring. Firmware builds
use the committed masks and do not need CairoSVG.
