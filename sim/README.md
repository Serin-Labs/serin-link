# Native UI simulator

This harness compiles this checkout's real `main/ui*.c`, Child Lock, reset face,
icons, fonts and LVGL for a Linux desktop. It exercises layout, rendering and
interaction. It does not establish hardware performance, memory use, ESP-NOW
behavior, physical input behavior or device compatibility; verify those on the
firmware and hardware.

From the repository root:

```bash
sudo apt install build-essential libsdl2-dev python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r test/ui_sim/requirements.txt
test/ui_sim/run-all.sh
```

See [the test guide](../test/ui_sim/README.md) for individual boards, build/cache
locations and offline preparation. The first build downloads the checksum-pinned
LVGL 9.5.0 archive, matching `dependencies.lock`; no ESP-IDF is needed. Sources are cached locally, not vendored into this repository.

To use the native window or capture a screenshot:

```bash
binary=$(test/ui_sim/build.sh viewe15)
"$binary"
"$binary" --headless --run 'screen settings; wait 300; shot settings.png'
```

Mouse input acts as touch, the wheel or Up/Down turns the encoder, and Space
presses the knob (hold for the long action). Escape or Q exits. Interactive
preferences persist in `.prefs` in the current working directory. Run from a
scratch directory for a fresh session. Headless tests isolate their preferences.

`--run` takes semicolon-separated commands; `--script FILE` takes one command
per line. Common commands include `screen NAME`, `turn N`, `press`, `long`,
`tap X Y`, `swipe X0 Y0 X1 Y1 MS`, `wait MS`, `state FIELD=VALUE`,
`caps FIELD=VALUE`, `info`, `labels`, `fontcheck`, `hitmap` and `shot FILE.png`.
The grammar is described in [sim_cmd.c](sim_cmd.c). Unknown commands return a
failure status. `fontcheck` checks each visible label against its actual font,
prints missing code points and a summary, and fails if any glyph is missing.
In headless mode, both `--run` and `--script` stop at that command and return a
nonzero process status. A successful check prints `fontcheck missing=0`.
Headless commands use virtual time and need no display server.
Only the native simulator is supported here.

Source origins and licensing: [PROVENANCE.md](PROVENANCE.md).
