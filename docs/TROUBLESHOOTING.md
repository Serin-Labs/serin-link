# Troubleshooting

## Build

**The build behaves like a different board, and the overlay did nothing.** You
almost certainly built without `-DSDKCONFIG=<builddir>/sdkconfig`. IDF then
reads a stale root `./sdkconfig`. In that file, every symbol is already set,
so the whole `SDKCONFIG_DEFAULTS` chain does nothing. Delete the stale root
`sdkconfig`. Always pin the build directory's own file. An image built from a
stale config can even boot and then fail at run time. Wi-Fi `NO_MEM` at link
time is one such failure.

## Flashing

**Blank screen, and `waiting for download` on serial.** The chip is strapped
into DOWNLOAD mode. There are two causes. Either the knob (GPIO0) was pressed
at power-on, or your USB path dropped the DTR/RTS reset after the flash.
Release the knob. Power-cycle the board.

**On WSL specifically:** `usbipd` USB pass-through blocks DTR/RTS. This has
two effects. `idf.py flash` can leave the chip in DOWNLOAD mode. The device
also detaches from WSL on every chip reset. The reliable method is to flash
natively from Windows (`pip install esptool` in PowerShell) and to use WSL
only to monitor.

If you open a serial port with DTR/RTS asserted, the chip also resets. For a
capture with no reset, use pyserial and set `dtr=False, rts=False` *before*
you call `open()`.

**`idf.py monitor` needs a real PTY.** A pipe into another program breaks the
monitor. Run the monitor in a real terminal.

## Runtime

**The dial pairs, but a reading looks absurd (for example 455.5°).** The two
protocol versions differ: your controller firmware is older than the
centi-degree wire format. Update the controller. The two copies of
`sl2_proto.h` must agree on wire layouts, constants, and field units; their
comments and helper functions can differ. Run
`test/sl2_public_compat/run.sh` to check this dial against the pinned public
controller fixture without a controller checkout or network. New pairing requires
protocol v5 on both peers; the decoder's older-frame floor does not permit older
pairing handshakes. The About screen shows the version handshake as
`v<dial> ↔ v<controller>`.

**The dial came back on the previous version after an update.** If the dial
reboots before the new image is accepted, the bootloader can restore a suitable
previous image; run Update again. The new image stays on probation
(`ESP_OTA_IMG_PENDING_VERIFY`) until the guard observes 30 s of consecutive
healthy controller-link polls, sees at least one rendered UI frame, and
successfully marks the image valid (`VALID_SOAK_MS` in `main/link_ota.c`). Polls
require a compatible STATE from the selected zone's bonded controller received
less than 4 s ago and applied by the link worker. Radio/task startup, radio
ACKs, and synthetic receive grace do not establish this health. During
probation, the existing 1 s active probes request STATE even with radio power
saving off; an idle controller's normal 10 s heartbeat alone is insufficient.
Heat-pump faults or a disconnected CN105 cable do not fail controller health.
Health polls run every 2 s; an unhealthy poll or a gap over 4 s resets the link
window. The render flag records the first frame only, not continuing UI health. Failed
mark-valid calls retry within the same 10-minute boot deadline. At that
deadline the dial attempts rollback; guard task allocation failure also
attempts rollback immediately. Neither failure accepts the image.

Each queued STATE carries its own receipt timestamp, captured before enqueue.
The worker publishes only the receipt timestamp of the STATE it applied; a
newer unapplied reply cannot make older queued state appear fresh.

The SDK rejects rollback before changing pending metadata when no suitable
previous image exists. Link then keeps running unaccepted; USB reinstallation
is the recovery path. A flash-write failure does not guarantee that metadata
is unchanged. While metadata still reports probation, Reset Link and zone
forget refuse with **Keep the dial powered while checking** instead of
restarting. Successful pairing saves its bond without restarting.

**Update check failed on a network that blocks NTP.** The dial checks the
validity dates on the update server's certificate. After it joins Wi-Fi it asks
`pool.ntp.org` for the time (UDP port 123) and waits up to 6 s. Without an
answer, it sets its clock to the firmware's build time and logs
`no SNTP time in 6000 ms; clock set to the build time ...`. Updates still work
that way, unless the server's certificate was issued after the build date; then
every TLS handshake fails until the dial gets NTP. Allow outbound NTP on that
network, or update from another one.

**The panel looks mirrored, the knob turns backwards, or touch is dead.** The
likely cause is a silent revision of the display module. Read the
revision-drift section in [`HARDWARE.md`](HARDWARE.md). Then file a display
revision report.

## Console commands

The dial runs a console on USB serial-JTAG. Open the port in any terminal
(`idf.py -B <builddir> monitor`, or `screen /dev/ttyACM0 115200`) and press
Enter for the `dial>` prompt. The prompt only appears while a USB host is
attached. `help` lists everything, including the ESP-IDF built-ins.

These are maintenance and diagnostic commands. Nothing here is part of the
product UI, and the [user manual](https://serin-labs.com/link/manual.html) does not document them.

### Link and zones

| Command | What it does |
|---|---|
| `espnow-mac` | Prints this dial's STA MAC — the address a controller pairs against. |
| `espnow-pair <mac> <32-hex-lmk>` | Writes a bond record by hand and restarts. This is a development path: it skips the signed handshake, so the controller's key is **not** pinned. Use `pair` for the real thing. |
| `pair` | Starts the ordinary signed pairing sweep (45 s). Same as the UI's pair verb. |
| `zones` | Dumps the bond table: radio channel, active zone, and per zone the MAC, its last known channel, badge, synced flag, ack and RX ages, RSSI, caps sequence, and name. |
| `zone <idx>` | Switches the active zone, by index from `zones`. |
| `cmd <mode\|temp\|fan> <value>` | Sends one CMD field to the active zone, bypassing the UI. `temp` is deci-degrees Celsius (`220` = 22.0 °C); `mode` and `fan` are the raw wire values from `main/sl2_proto.h`. |
| `radio-ps [off\|min [interval_ms [window_ms]]]` | Reads or sets Wi-Fi modem power save (`min` = min-modem, with an optional listen interval and window). Saved in NVS and applied immediately if the radio is up. |

`espnow-pair` restarts the dial as soon as it writes the bond.

### Sensor

Registered on every build. On a board with no SHT4x, `sensor` answers
`sht4x: not present`. The offset is in centi-degrees Celsius and persists in
NVS.

| Command | What it does |
|---|---|
| `sensor` | One line of SHT4x state: raw and filtered temperature, the effective offset with its parts (base and idle-screen term), the corrected temperature, humidity, and reading age. |
| `sensor-offset <cc>` | Sets the base self-heating offset. It applies whether or not the heat pump runs. |
| `i2c-scan` | Probes the sensor I2C bus several times and prints the addresses that answered every pass. Unstable addresses are counted, not listed. |

The 1.5" defaults subtract 1.00 °C (`100`) from temperature and multiply
relative humidity by 1.110 (`CONFIG_SERIN_SHT40_RH_GAIN_PPT=1110`). A saved
temperature override survives firmware updates and takes precedence over the
default. Use `sensor` to inspect it, or `sensor-offset 100` to restore the
default value. Humidity gain is a build setting; `sensor-offset` does not
change it. The offset does not depend on heat-pump operation. The idle-screen
reduction defaults to 0.

`sensor-offset` prints the new effective offset. **Settings → Advanced →
Reset Link** erases the saved value, because it wipes the whole `serin` NVS
namespace.

### Updates

| Command | What it does |
|---|---|
| `ota-channel [stable\|beta]` | Reads or sets the update channel, and prints the manifest URL in force. `beta` fails if the build has no beta manifest URL. |
| `ota-url [<url>\|default]` | Reads or overrides the manifest URL. **The override applies only to the channel the dial is on right now**; stable and beta hold separate overrides (`ota_url` and `ota_url_b`). `default` clears the selected channel's override. With no argument it prints both. |

An `http://` override resolves but fails at download time unless the build sets
`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`.

### Crash and display

| Command | What it does |
|---|---|
| `wdt-last` | Prints the stored breadcrumb from the last abnormal reset — reset reason, uptime at the time, and the task-watchdog detail — then this boot's reset reason and uptime. `(none recorded)` means no abnormal reset since the last erase. |
| `panel-sleep [shallow\|deep]` | **1.5" board only.** How **Idle screen → Off** darkens the panel: `deep` (the default) issues SLPIN, `shallow` only drives brightness to 0. Saved in NVS. |
| `bl-dip [out ms] [in ms] [curve 1-4] [dd on\|off]` | **1.5" board only.** Tunes the screen-transition brightness dip: fade-out and fade-in durations, the gamma exponent that makes a linear ramp read as an even fade, and `dd`, the panel's own DCS display-dimming flag (0x53 bit 3, off by default because its smoothing lags the ramp). **Not** saved, so a restart reverts it. |

Neither `panel-sleep` nor `bl-dip` exists on the 2.1" board: its BSP registers
no console commands.
