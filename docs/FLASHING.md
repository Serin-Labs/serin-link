# Building and flashing

## Prebuilt images (no toolchain)

The public distribution repo holds one merged image per board, under
[`Serin-Labs/serin-cn105/firmware/link`](https://github.com/Serin-Labs/serin-cn105/tree/main/firmware/link).
`factory-manifest.json` beside the images lists the SHA-256 of each image.
Flash the image at offset `0x0` with any esptool-compatible flasher:

```bash
pip install esptool
esptool --chip esp32s3 -b 460800 write_flash 0x0 serin_link_viewe15_factory.bin
```

The 2.1" image is `serin_link_viewe21_factory.bin`. Each merged image contains
the bootloader, the partition table, and the app. Nothing goes to any other
offset. The `ota/stable/` directory contains `link15-app.bin` and
`link21-app.bin`. These are the OTA payloads. Do not flash them at `0x0`.

### Browser flasher

The merged image pads across the settings partition (`nvs` at `0x9000`), so
flashing it at `0x0` with esptool resets Link's settings and pairings. The
[browser flasher](https://serin-labs.com/flash-link.html) does not write the
merged image. It writes the same bytes as four parts listed under `parts` in
`factory-manifest.json`: the bootloader at `0x0`, the partition table at
`0x8000`, a blank `otadata` at `0xf000` and the app at `0x20000`. Nothing is
written to `nvs`, so a reinstall with **Erase everything first** unchecked keeps
Link's settings, pairings and update channel. The blank `otadata` makes Link
boot the newly written app even if its last over-the-air update left it running
from the second app slot. Check **Erase everything first** for a factory-fresh
dial; that erases the whole flash, settings included.

To do the same by hand:

```bash
esptool --chip esp32s3 -b 460800 write_flash \
  0x0 serin_link_viewe15_bootloader.bin 0x8000 serin_link_viewe15_partition-table.bin \
  0xf000 serin_link_viewe15_otadata.bin 0x20000 serin_link_viewe15_app.bin
```

Pre-release USB images, when one is published, are under `factory/beta/` in the
same directory, and the browser flasher offers them behind **Install the
pre-release build**. A pre-release build follows the beta update channel unless
`ota-channel stable` is entered on the USB console.

The browser flasher downloads every part and checks each file against the
manifest's SHA-256 before it erases or writes anything, so a failed download
leaves the board unchanged. After writing, it verifies every written part
against the board's flash checksum before rebooting. A mismatch or unreadable
checksum stops the install at the failure screen; use **Try the write again**
after checking the USB connection. Keep the cable connected until the flasher
reports completion.

If the flasher reports missing or invalid firmware integrity information,
reload the page to fetch the current manifest. It will not erase or write
the board until the download checks pass.

## Building from source

You need **ESP-IDF v5.5.4**. Do the one-time setup with Espressif's standard
[Get Started](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/)
guide. Then:

```bash
source <idf-path>/export.sh
python -m pip install -r tools/requirements-build.txt

idf.py -B build_viewe15 -DSDKCONFIG=build_viewe15/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.viewe" build
idf.py -B build_viewe15 -p /dev/ttyACM0 flash monitor
```

For the 2.1" board, change **both** the build directory and the overlay
(`sdkconfig.defaults.viewe21`). Never change one without the other. Never omit
`-DSDKCONFIG=<builddir>/sdkconfig`. See
[build troubleshooting](TROUBLESHOOTING.md#build) for the stale-sdkconfig trap.

Both boards write `serin_link.bin`, but each image records its board in the app
descriptor's project name: `serin_link_viewe15` or `serin_link_viewe21`. The
name follows the board chosen in the build's `sdkconfig`, not the build
directory. A dial reads the name from an update's header and refuses an image
for the other board with **Wrong board image** before it writes to flash.
Images named plain `serin_link`, from releases before the per-board names,
still install; only the manifest's board id selects those.

The dial also checks the incoming app descriptor's version before writing the
image. It must match the manifest version exactly and be newer than the running
firmware. Release versions follow `maj.min.patch[-pre.N]` and fit in 31 ASCII
characters; `v`/`V` prefixes and other aliases do not match a published version.
A malformed, mismatched, same-version, or older image is rejected with **Wrong
image version**, leaving the current firmware selected. A malformed manifest
version is rejected with **Bad manifest**. Prerelease ordering is unchanged:
the numeric suffix orders prereleases of the same core version, and the stable
release follows its prereleases. Reinstalling or deliberately downgrading
remains available over USB with an image for the correct board.

There is no key ceremony. The first configure generates one machine-local key,
`secure_boot_signing_key.pem`. `.gitignore` excludes this file. This key signs
the OTA image. A dial accepts an update only if the update is signed with the
key from the build of its running firmware. Keep your key if you plan to serve
your own updates. This never affects flashing over USB.

Your key and your update origin go together. A dial built from this source
still asks the built-in manifest URL. The images there carry the maintainer's
signature. The dial downloads an upstream update in full and only then rejects
it. Point the dial at your own manifest with the `ota-url` console command. The
[update console reference](TROUBLESHOOTING.md#updates) has the details.

## Signing your own update images

`tools/publish_fw.py` builds and stages signed OTA and USB factory images. For
your own devices, first build normally to generate the local key, or supply an
existing RSA-3072 private key. Use an output directory outside the source
checkout:

```bash
python tools/publish_fw.py --development --out /tmp/serin-link-own-ota \
    --signing-key /path/to/your-private.pem
python tools/publish_fw.py --development --factory --out /tmp/serin-link-own-factory \
    --signing-key /path/to/your-private.pem
```

Development mode accepts untagged and modified source and verifies each
signature against the selected key. Manifests carry `development: true`;
factory manifests also carry `dirty: true`. Omitting `--signing-key` selects
the repository-local `secure_boot_signing_key.pem`. `--boards viewe15` or
`--boards viewe21` selects one board. `--version` sets the version, which must
follow `maj.min.patch[-pre.N]` and fit in 31 ASCII characters. `--no-build`
re-stages existing output and requires the receipts written by a fresh build.

These images update only devices that trust your key. Serve the OTA files from
your own origin and configure `ota-url`.

## Public OTA feeds

Release updates are served from the public `Serin-Labs/serin-cn105`
distribution repository:

| Channel | Path in the distribution repo |
| --- | --- |
| Stable OTA | `firmware/link/ota/stable/manifest.json` |
| Beta OTA | `firmware/link/ota/beta/manifest.json` |
| Stable USB factory images | `firmware/link/factory-manifest.json` |
| Beta USB factory images | `firmware/link/factory/beta/factory-manifest.json` |

Each OTA manifest references `link15-app.bin` and `link21-app.bin` beside it.
These are signed application images, not merged USB images. Image paths
resolve relative to the selected manifest. Stable and beta are separate feeds.

To follow the beta feed, enter `ota-channel beta` at the USB console, then open
**Settings → About → Details → Update**. `ota-url default` clears a saved URL
override for the selected channel. USB installation can use the beta factory
manifest instead.

## The DOWNLOAD-mode trap (knob boards)

On a VIEWE knob board, **GPIO0 is the knob push-button**. If you power the
board while the knob is pressed, the chip straps into DOWNLOAD mode. The screen
stays blank. Serial prints a `waiting for download` line. Nothing else happens.
This is easy to do while you seat the board in the stand. The board is not
damaged. Release the knob. Then power-cycle the board.

Some USB pass-through setups also block the DTR/RTS auto-reset that
`idf.py flash` needs. WSL's usbipd is the known case. The chip then stays in
DOWNLOAD mode after a flash. If a flash succeeds but the board does not boot
afterwards, power-cycle the board. Prefer to flash from the host OS.
[`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) has more.

## First boot

An unpaired dial shows the unpaired home face. To pair it:

1. Open the pairing window on your controller. With the HomeKit-compatible
   firmware, select **Pair** in the Link card or press the controller's button
   once. With ESPHome, select **Pair Serin Link** in Home Assistant or in the
   enabled ESPHome web interface.
2. On Serin Link, open **Settings → Link** and press the knob, or press the
   knob on the unpaired home face. There is no separate **Pair** item; the
   Link row's knob push is the pair action.

To reset Serin Link, use **Settings → Advanced → Reset Link**. Hold the knob
until the confirmation dialog appears. Select **Reset** to confirm.

Do not hold the knob while you connect power. The knob uses GPIO0, which
selects the chip's boot mode. Holding it during power-on starts firmware
download mode. To return to normal operation, release the knob. Disconnect
power. Connect power again with the knob released.
