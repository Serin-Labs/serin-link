# Hardware guide

A Serin Link consists of an ESP32-S3 round display module, an optional carrier
PCB with an SHT41 sensor, and an optional stand. **A vendor can revise a display
module without changing its name.** Buy by the model strings below, and report
any difference with the **display revision report** issue template.

## What is open, and what is not

Project-authored firmware, tools, and icon artwork in this repository are
GPL-3.0-only, with [third-party license exceptions](THIRD_PARTY.md). PCB and
stand design files (Gerbers, KiCad project, and STL) are not included in this
repository, and the firmware license grants no rights to them. The stand file
is distributed separately under its own
[usage terms](https://serin-labs.com/link/build/case-printing.html#usage-terms).
The [build guide](https://serin-labs.com/link/build-guide.html) describes
assembly.

The firmware runs on a supported display module without the PCB or the stand.
The carrier PCB adds one part: the SHT41 sensor. With this sensor, the dial can
offer the controller a room-temperature source. The driver probes for the
sensor at boot. Without it, the dial behaves the same in every other way.
Sensor support differs between the two boards; see
[The SHT41 carrier sensor](#the-sht41-carrier-sensor).

## Supported display modules

Every module is an ESP32-S3R8 with 16 MB flash and 8 MB octal PSRAM. Hardware
release testing covers the VIEWE 1.5".

| Board | Panel | Notes |
|---|---|---|
| **VIEWE 1.5" `UEDX46460015-MD50ET`** | SH8601 466×466 QSPI AMOLED | Knob and touch. True black, and per-pixel dimming through panel command 0x51. |
| VIEWE 2.1" `UEDX48480021-MD80ESP32` | ST7701S 480×480 RGB | Knob and touch. The backlight is active-low, and the knob pins differ from the 1.5". |

Both boards have a knob and a touch panel. The 1.5" panel is AMOLED; the 2.1"
panel is an RGB LCD.

Buy by model string. Check the string on the listing, and again on the module
silkscreen when it arrives:

- VIEWE 1.5": `UEDX46460015-MD50ET`
- VIEWE 2.1": `UEDX48480021-MD80ESP32`

The model strings come from the BSP file headers. The pin maps are in
`main/bsp_viewe.c` and `main/bsp_viewe21.c`.

## The SHT41 carrier sensor

`SERIN_SHT40` is on by default for both boards, so the driver is in both
builds. The two carriers do not share their I2C pins.

On the VIEWE 1.5", SDA is GPIO39 (carrier CN3 pin 6) and SCL is GPIO40 (CN3
pin 7).

On the VIEWE 2.1", `SERIN_SHT40_SDA_GPIO` and `SERIN_SHT40_SCL_GPIO` default to
`-1`. The bus does not start, `main/sensor_sht40.c` logs
`bring-up failed — sensor disabled`, and the dial reports no sensor. It behaves
as if no carrier were fitted. Do not use the 1.5" pins: GPIO39 and GPIO40 are
RGB panel data lines on the 2.1" (`RGB_DATA_PINS`, `main/bsp_viewe21.c`). An I2C
master on those two pins would contend with a bus that the panel drives
continuously.

To enable the sensor on a 2.1":

1. Trace carrier CN3 to the module.
2. Check that the pins are free. GPIO4, GPIO43, and GPIO44 are the only pins
   that `main/bsp_viewe21.c` does not use. Verify each one on your carrier.
   GPIO43 and GPIO44 are the default UART0 pins.
3. Set `SERIN_SHT40_SDA_GPIO` and `SERIN_SHT40_SCL_GPIO` in
   `sdkconfig.defaults.viewe21`.
4. Build, flash, and run the `i2c-scan` console command. The SHT41 answers at
   `0x44` on `I2C_NUM_1`. Refer to
   [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md#console-commands).

### Calibration

`sdkconfig.defaults.viewe` sets the 1.5" self-heating offset to 100
centi-degrees C (subtract 1.00 °C) and the humidity gain to 1110 parts per
thousand (multiply by 1.110). The offset is constant; it does not depend on
heat-pump operation. The idle-screen reduction defaults to 0. A saved
`sensor-offset` override takes precedence over the temperature default; it
does not change the humidity gain.

`sdkconfig.defaults.viewe21` sets neither, so a 2.1" uses offset 0 and gain
1000, which apply no correction. Measure the offset on an assembled 2.1" with
the `sensor-offset` console command before relying on its readings.

## Bill of materials (per dial)

| Part | Source |
|---|---|
| Display module (above) | vendor listing, by model string |
| Serin Link PCB (optional) | Serin Link kit |
| Stand, screws, and silicone pad (optional) | Serin Link kit |
| USB-C cable and 5 V supply | any |

A display module alone runs the firmware with a USB-C cable and a 5 V supply.
The kit fits only the 1.5-inch `UEDX46460015-MD50ET`.

## Revision drift — read this before you file a bug

A display vendor can change the panel, the touch controller, or the knob
encoder without changing the product name. Known revision differences include
mirrored sprites, an inverted knob direction, and a different touch controller.
If a new dial shows a problem that these docs do not describe:

1. Photograph the front silkscreen of the module.
2. Photograph the back silkscreen of the module.
3. Note the seller, the listing link, and the purchase date.
4. Open a **display revision report** issue. Describe the symptoms.
