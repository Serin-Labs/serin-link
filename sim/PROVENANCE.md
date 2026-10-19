# Simulator source provenance and licenses

The native harness is project code distributed under GPL-3.0-only, as
indicated by its SPDX headers and the repository [LICENSE](../LICENSE):

- `sim_main.c`, `bsp_sim.c`, `sim_time.c`, `sim_shot.c`, `sim_cmd.c`
- `sim.h`, `sim_time.h`, `sim_shot.h`, `sim_cmd.h`, `lv_conf.h`, `Makefile`
- `stubs/esp_stubs.c`, `stubs/esp_system.h`, `stubs/esp_task_wdt.h`,
  `stubs/esp_timer.h`, `stubs/sdkconfig.h`

Simulator addresses are synthetic fixtures.

`stb_image_write.h` is Sean Barrett's stb_image_write v1.16, copyright © 2017 Sean Barrett (the MIT license notice).
It retains the complete upstream MIT and public-domain/Unlicense alternatives
at the end of the file. It is an exception to this repository's GPL-only source
policy and is used here under the MIT option. Upstream:
<https://github.com/nothings/stb/blob/master/stb_image_write.h>.

LVGL is fetched from its public upstream.
`prepare_lvgl.py` pins the v9.5.0 commit archive by SHA-256, checks that version against
`dependencies.lock`, and extracts only library sources, root headers,
`LICENCE.txt`, and `COPYRIGHTS.md`. The v9.5.0 tag resolved to commit
`85aa60d18b3d5e5588d7b247abf90198f07c8a63` at import. The immutable archive URL is
<https://codeload.github.com/lvgl/lvgl/tar.gz/85aa60d18b3d5e5588d7b247abf90198f07c8a63>.
Save it as `lvgl-9.5.0.tar.gz` for offline cache preparation. Its SHA-256 is
`b32747cf6568d86a5cb9d30f53970bec1c7a322cb624c4158d4b0e348324146b`.
LVGL is MIT licensed; retain its fetched `LICENCE.txt` and the notices for its
bundled libraries when redistributing it.
SDL2 is a separately installed system dependency, under the zlib license.
