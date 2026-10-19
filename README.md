# Serin Link

<p align="center">
  <img src="https://serin-labs.com/assets/serin-link-v5-34-orange.webp" width="320" alt="The Serin Link: a round dial with a rotary knob display on an orange stand">
</p>

Firmware for the **Serin Link**, a round climate-control display with a rotary
knob and touchscreen. It communicates over ESP-NOW with a Serin Controller,
an ESPHome climate entity using the Serin Link core component, or another ESP32
implementing the open protocol. The controller's advertised capabilities
determine which controls are available. Daily control needs no home Wi-Fi or
internet connection.

[User manual](https://serin-labs.com/link/manual.html) · [Installation](docs/FLASHING.md) ·
[Documentation](docs/README.md)

## Getting started

Install a prebuilt image with the
[browser flasher](https://serin-labs.com/flash-link.html), or follow the
[flashing guide](docs/FLASHING.md) for USB installation and source builds.
The [manual's quick start](https://serin-labs.com/link/manual.html#quick-start) covers controller
setup, pairing, and checking control.

A supported display module can run the firmware without the custom PCB or
stand. Without an SHT41 sensor, it cannot offer the controller a built-in room
temperature source. See the [hardware guide](docs/HARDWARE.md) for module
revisions, sensor support, and required parts.

## Boards

Both supported boards use an ESP32-S3R8 with 16 MB flash and 8 MB octal PSRAM.

| Board | Display module | Build overlay |
| --- | --- | --- |
| VIEWE 1.5" | `UEDX46460015-MD50ET`, 466×466 AMOLED | `sdkconfig.defaults.viewe` |
| VIEWE 2.1" | `UEDX48480021-MD80ESP32`, 480×480 RGB LCD | `sdkconfig.defaults.viewe21` |

Hardware release testing covers the 1.5-inch board.

Check the exact module revision against the
[supported hardware](docs/HARDWARE.md#supported-display-modules) before buying.

## Build and flash

Install **ESP-IDF v5.5.4** using the
[source-build guide](docs/FLASHING.md#building-from-source), then build the
1.5-inch board from the repository root:

```bash
source ~/esp/esp-idf/export.sh
python -m pip install -r tools/requirements-build.txt

idf.py -B build_viewe15 -DSDKCONFIG=build_viewe15/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.viewe" build
idf.py -B build_viewe15 -p /dev/ttyACM0 flash monitor
```

Keep each board's build directory, `SDKCONFIG` path, and overlay together.
Omitting `-DSDKCONFIG=<builddir>/sdkconfig` can reuse a stale root `sdkconfig`
and select the wrong board. Use the pinned Python requirements and locked
component versions in `dependencies.lock`.

The first configure generates `secure_boot_signing_key.pem`, which Git ignores.
Keep it private and retain it for your own OTA updates. A dial accepts only
updates signed with the key trusted by its running build; a source build with
its own key needs its own signed feed or USB reflashing. See
[signing and update setup](docs/FLASHING.md#building-from-source).

## Compatibility

Development uses the
[Serin Link core ESPHome component](https://github.com/Serin-Labs/serin-link-core).
Its [pinned quickstart](https://github.com/Serin-Labs/serin-link-core/blob/86cfdd202441c980129d776a8c8d830014548a6b/README.md#esphome-quickstart)
requires **ESPHome 2026.3.0 or newer**, an ESP32, and the **ESP-IDF** framework.
The component owns the radio callbacks; omit ESPHome's built-in `espnow:`
component. Website-generated controller configurations require **2026.7.4 or
newer**. Credential sharing for Link updates is optional and cannot relay
WPA-Enterprise credentials; see the
[ESPHome update reference](https://serin-labs.com/link/technical-reference.html#esphome-updates).

New pairing and re-pairing require **protocol v5 on both peers**. Older-frame
decoding does not imply support for older pairing handshakes. The
[public-controller regression](test/sl2_public_compat/README.md) checks a
pinned core revision on the host; it does not establish hardware
interoperability with every controller or climate platform.

## Development

The [architecture guide](docs/ARCHITECTURE.md) maps the source and explains
startup, data flow, concurrency, and relevant checks. The
[native UI simulator](sim/README.md) runs the real UI on a desktop without
ESP-IDF. Follow the
[simulator test guide](test/ui_sim/README.md) for prerequisites and regression
checks. Simulator results cover layout and interaction; radio, timing, power,
and physical inputs require device testing.

## Related repositories

| Repository | Purpose |
| --- | --- |
| [serin-link](https://github.com/Serin-Labs/serin-link) | Dial firmware, simulator, tests, and documentation. |
| [serin-link-core](https://github.com/Serin-Labs/serin-link-core) | Controller-side protocol and ESPHome integration, including the wire specification. |
| [serin-cn105](https://github.com/Serin-Labs/serin-cn105) | Prebuilt controller and Link firmware, distribution manifests, and browser flashing. |

## Security

First pairing trusts the first controller key received. Pair in a trusted
environment with only the intended controller's pairing window open. Default
builds verify signed updates but do not enable hardware Secure Boot, flash
encryption, or NVS encryption.

## License and source scope

Project-authored firmware, tests, tools, and icon artwork are
**GPL-3.0-only**; see [LICENSE](LICENSE). Imported code and assets retain their
upstream licenses. The Serin name and logo are trademarks. The
[third-party notices](docs/THIRD_PARTY.md) cover exceptions, fetched components,
and ESP-IDF binary-library limitations.

Each release tag (`v<version>`) holds the source of that release's images. PCB
and enclosure design files (Gerbers, KiCad projects, and stand STL) are not
included in this repository; see
[hardware scope](docs/HARDWARE.md#what-is-open-and-what-is-not).
