# Third-party code and assets

This inventory describes the tracked imports and pinned dependencies in this
checkout. Project-authored code is GPL-3.0-only under [LICENSE](../LICENSE).
The entries below retain their upstream terms; this inventory does not
relicense them or replace their full license texts and source-file notices.

## Imports tracked in this repository

| Material | Location | Upstream license and retained notice |
| --- | --- | --- |
| Plus Jakarta Sans | `tools/PlusJakartaSans-*.ttf`, generated `main/fonts/*.c` | OFL-1.1; [source notice](../tools/PlusJakartaSans-OFL.txt), [generated-font notice](../main/fonts/LICENSE-OFL.txt). Upstream revision and hashes: [asset inventory](../tools/assets/SOURCES.json). |
| VIEWE SH8601 and ST7701S panel initialization tables | `main/bsp_viewe.c`, `main/bsp_viewe21.c` | MIT, Copyright (c) 2025 VIEWE-CN; [full notice](../main/LICENSE-VIEWE.txt), plus the attribution at each table. Upstream [1.5-inch license](https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display/blob/main/LICENSE) and [2.1-inch license](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/main/LICENSE). The surrounding project code is GPL-3.0-only. |
| Phosphor thermometer icon and generated variants | `tools/icon_sources/room_sensor.svg`, `tools/assets/icons/png/room_sensor.png`, room-source variants in `main/icons_serin.c` | MIT, Copyright (c) 2023 Phosphor Icons; [full notice](../tools/icon_sources/room_sensor.LICENSE), [provenance](../tools/icon_sources/README.md). |
| Espressif knob 1.0.0 host-test fixture | `test/knob_power_save/upstream/iot_knob.c`, `include/iot_knob.h`, `include/knob_gpio.h` | Apache-2.0, Espressif Systems; source/header copyright notices and [full license](../test/knob_power_save/upstream/license.txt) are retained. [Pinned upstream and fixture hash](../test/knob_power_save/upstream/README.md). These files are not GPL-only test code. |
| Serin Link public controller protocol header | `test/sl2_public_compat/upstream/sl2_proto.h` | Apache-2.0; unmodified header and [full upstream license](../test/sl2_public_compat/upstream/LICENSE) retained. [Immutable public revision, hashes, and update procedure](../test/sl2_public_compat/README.md). This fixture is not GPL-only test code. |
| Sean Barrett's stb_image_write v1.16 | `sim/stb_image_write.h` | Complete MIT and public-domain/Unlicense alternatives remain at the end of [the file](../sim/stb_image_write.h); used here under MIT. [Simulator provenance](../sim/PROVENANCE.md). |

The [asset inventory](../tools/assets/SOURCES.json) also identifies the project
artwork and recovered raster inputs. The Serin name and logo are trademarks;
the source-code license does not grant trademark rights.

## Components fetched for firmware builds

[dependencies.lock](../dependencies.lock) pins these registry versions and
content hashes. They are downloaded into `managed_components/`, which is not
tracked here. License paths in this table are relative to each downloaded
component. The license files and source headers in those pinned packages were
checked for this inventory; preserve them when redistributing the packages.
A board's linked subset depends on its configuration.

| Registry component | Pinned version | License evidence in the downloaded package |
| --- | --- | --- |
| [espressif/button](https://components.espressif.com/components/espressif/button/versions/3.3.1) | 3.3.1 | Apache-2.0, `license.txt` and source headers |
| [espressif/cmake_utilities](https://components.espressif.com/components/espressif/cmake_utilities/versions/0.5.3) | 0.5.3 | Apache-2.0, `license.txt`; build tooling |
| [espressif/esp_lcd_sh8601](https://components.espressif.com/components/espressif/esp_lcd_sh8601/versions/1.0.0) | 1.0.0 | Apache-2.0, `license.txt` and source headers |
| [espressif/esp_lcd_touch](https://components.espressif.com/components/espressif/esp_lcd_touch/versions/1.2.1) | 1.2.1 | Apache-2.0, `license.txt` and source headers |
| [espressif/esp_lcd_touch_cst816s](https://components.espressif.com/components/espressif/esp_lcd_touch_cst816s/versions/1.1.1~1) | 1.1.1~1 | Apache-2.0, `license.txt` and source headers |
| [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.8.0~1) | 2.8.0~1 | Apache-2.0, `license.txt` and source headers |
| [espressif/knob](https://components.espressif.com/components/espressif/knob/versions/1.0.0) | 1.0.0 | Apache-2.0, `license.txt` and source headers; the build patches a copy to enable idle power saving ([patch generator](../tools/patch_knob_power_save.py)) |
| [espressif/libsodium](https://components.espressif.com/components/espressif/libsodium/versions/1.0.22~1) | 1.0.22~1 | ISC, `LICENSE` and `libsodium/LICENSE`; Espressif port files additionally carry Apache-2.0 headers (`port/`, `port_include/`) |
| [kodediy/esp_lcd_touch_cst820](https://components.espressif.com/components/kodediy/esp_lcd_touch_cst820/versions/1.0.1) | 1.0.1 | Apache-2.0, `license.txt`, manifest, and source headers |
| [lvgl/lvgl](https://github.com/lvgl/lvgl/tree/85aa60d18b3d5e5588d7b247abf90198f07c8a63) | 9.5.0 | MIT, `LICENCE.txt`; bundled libraries have additional notices indexed by `COPYRIGHTS.md` (including the enabled QR generator's `src/libs/qrcode/LICENSE.txt`) |

The simulator fetches the same LVGL revision by archive SHA-256 and preserves
its root and nested notices; see [sim/PROVENANCE.md](../sim/PROVENANCE.md).
SDL2 is a separately installed simulator dependency under the zlib license.
Artwork regeneration and build/release tools use separately installed packages
listed in `tools/artwork/package-lock.json`, `tools/artwork/requirements.txt`,
and `tools/requirements-*.txt`; they are not bundled firmware source.

## ESP-IDF and SDK binary libraries

The build uses [ESP-IDF v5.5.4](https://github.com/espressif/esp-idf/tree/v5.5.4).
Its original source is Apache-2.0 under its root `LICENSE`, with third-party
exceptions described in the upstream
[copyright inventory](https://github.com/espressif/esp-idf/blob/v5.5.4/docs/en/COPYRIGHT.rst).
That inventory and the individual component notices apply, rather than a
blanket GPL grant from this repository. Dependencies used by the application
include FreeRTOS (MIT), lwIP (BSD), cJSON (MIT), Mbed TLS (Apache-2.0 option), console
libraries (BSD and other retained terms), and the C runtime; retain the actual
SDK/toolchain notices for the build being distributed.

ESP-IDF also supplies precompiled RF archives. In the pinned SDK, the Wi-Fi
submodule is `b87cce81803b85c3639fb14bac6c24cff6d0fbad`, PHY is
`3d57415af6e4c92eff2c4c3463e20a51d7340aba`, and coexistence is
`ee5cd79583c02f23e43e62931ffb55f5a4992d0f`. The
[Wi-Fi README](https://github.com/espressif/esp32-wifi-lib/blob/b87cce81803b85c3639fb14bac6c24cff6d0fbad/README.rst)
and [PHY README](https://github.com/espressif/esp-phy-lib/blob/3d57415af6e4c92eff2c4c3463e20a51d7340aba/README.rst)
identify their binaries as Apache-2.0 object form. The
[coexistence package license](https://github.com/espressif/esp-coex-lib/blob/ee5cd79583c02f23e43e62931ffb55f5a4992d0f/LICENSE)
is also Apache-2.0. These packages provide archives rather than the source
needed to rebuild those RF libraries. Publishing this application therefore does not
make every byte of the SDK rebuildable from public source. Chip ROM code is
also outside this application's source publication; ESP-IDF documents its
separate attributions in the copyright inventory above.

## Prebuilt images and hardware design scope

This inventory describes this source tree and its dependencies. The source of
each published image is the matching release tag (`v<version>`).

The PCB and stand are separate hardware designs. This repository includes no
Gerbers, KiCad project, or stand STL; see [HARDWARE.md](HARDWARE.md#what-is-open-and-what-is-not).
The firmware license grants no rights to those designs.
