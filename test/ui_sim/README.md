# LVGL interaction regression tests

These scripts compile and run this checkout's real UI with the public native
harness in `sim/`. They are separate from `test/run-all.sh`, which only needs a
host C compiler. A missing prerequisite or failed check returns a nonzero status.

Prerequisites: Linux, a C compiler, GNU Make, SDL2 development headers/libraries,
and Python **3.11 or newer** with Pillow. On Debian/Ubuntu:

```bash
sudo apt install build-essential libsdl2-dev python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r test/ui_sim/requirements.txt
```

Run all ten current UI checks and their Python layout/touch helpers on both
`viewe15` (466×466) and `viewe21` (480×480):

```bash
test/ui_sim/run-all.sh
```

Run one board or an individual check:

```bash
test/ui_sim/run-all.sh viewe21
export SERIN_SIM_BIN=$(test/ui_sim/build.sh viewe15)
test/ui_sim/home_taps.sh
```

The first build downloads upstream LVGL **9.5.0** over HTTPS and verifies its
SHA-256 before extraction. It checks that the simulator's pin still matches
`dependencies.lock`; updating firmware LVGL requires reviewing this pin too.
No ESP-IDF install or managed components are needed.
Only LVGL library sources, root headers and its license are extracted.
The approximately 97 MB upstream archive includes media that is not extracted.

Builds default to `sim/build/<board>/serin-sim`, dependencies to `sim/.cache/`.
Optional `SERIN_SIM_BUILD_DIR`, `SERIN_SIM_CACHE` and `SERIN_SIM_JOBS` override
those locations and compile parallelism. For offline use, first run a build with
network access, then retain the cache. Alternatively place the pinned upstream
`lvgl-9.5.0.tar.gz` in `SERIN_SIM_CACHE`; it is still checksum verified.
Source URL and checksum are recorded in [`sim/PROVENANCE.md`](../../sim/PROVENANCE.md).
CI runs the same command per board.

Each check uses temporary working directories so simulator preferences cannot
contaminate later scenarios. The harness includes stable-ID catalog commands:

```
roomsrc_ids 20 10:Internal,20:Bedroom,30:Hallway
roomsrc_ids 20 30:Hallway,20:Bedroom,10:Internal
roomsrc_ids 20 -
roomsrc_ids 20 10:0:Heat-pump,20:1:Bedroom,30:2:Serin-Link
```

The first argument is the applied source ID. An optional kind between the ID
and name uses the protocol values (0 internal, 1 remote sensor, 2 Link, 3 auto,
4 aggregate). `-` means an empty catalog. This
lets tests exercise catalog removal and reordering while keeping IDs stable.
These commands live in the harness; no source-text injection is needed.
The firmware has no simulator-only hooks.

Inspect screenshots at both resolutions after layout changes. Simulator checks
cover interaction and layout; firmware builds and hardware checks cover the
embedded dependencies and device behavior.

`navigation_wifi_modes.sh` also runs `selector_layout.py` to check the shared
slots in Zones, Mode, Room Sensor, and Airflow Back. It verifies
that missing readings and pagination do not move the title or controls, that
central actions clear every supported ring density, and that selection and
Return preserve their navigation and command behavior. Its Zones screenshot
check requires Pillow (`python3 -m pip install Pillow`) and verifies that the
temperature digits remain centered independently of the degree symbol as the
reading changes width.

`settings_touch.sh` follows taps from Home through Settings to Add zone and
Room Sensor. It checks offline status, pairing recovery, and hold feedback.
On the Wi-Fi status row, it checks that releasing after the long-press threshold
but before the confirmation arc completes cancels without opening the web QR
code, that short taps still open the QR code (including after cancellation),
and that completed holds start network change without a release tap.
`advanced.sh` and `room_source.sh` also probe target edges and corners with
real LVGL hit testing, in addition to sending taps. These checks use Python 3
and isolate preferences for each scenario.

`child_lock.sh` checks that Child Lock gates the live, offline, and dismissed
offline home faces, and that the pairing and Wi-Fi setup faces stay reachable.
It also checks that a home dialog action stays blocked after temporary unlocking
expires, while touch and knob Reset confirmations in Advanced still work after
31 seconds of inactivity closes the unlock window.

`wifi_cancel.sh` exercises cancellation requests and controller replies, including
stale replies, timeouts, trial waits, legacy Back, and initial setup. The test-only
`wifi_ack <status> [session]` command supplies a controller result; omitting the
session uses the most recent setup session. The simulator captures requests but
does not invent a successful controller response.

`name_fonts.sh` exercises controller-supplied accented Latin zone and sensor
names in both pickers, the home chip, and Settings. It checks UTF-8 byte
boundaries and requires unsupported glyphs to fail both `--run` and `--script`
with a nonzero exit status before subsequent commands run. The board runner
executes it at both resolutions.
