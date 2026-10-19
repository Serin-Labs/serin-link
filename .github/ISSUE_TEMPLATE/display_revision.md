---
name: Display revision report
about: Report a display module that behaves differently from the docs.
labels: hardware, revision-report
---

<!-- A display vendor can revise the panel, the touch IC, or the encoder without
     changing the product name. These reports are how the next buyer avoids a
     bad listing. Thank you.
     Remove private keys, passwords, pairing/setup QR codes and codes, order
     details, and personal/network identifiers (MAC/IP addresses, SSIDs,
     serial numbers, room names) before sharing photos, logs, or config. -->

**Module**
- Product name, or model string, on the listing:
- Seller, and the listing link:
- Purchase date:
- Model string on the PCB silkscreen, if it differs:

**Photos**
<!-- The front and the back of the module. Keep the silkscreen readable. -->

**Symptoms**
<!-- For example: mirrored sprites, inverted knob direction, dead touch, wrong colors. -->

**Firmware**
- The version and the board overlay you built and flashed:
- Source commit and ESP-IDF version (if self-built):
- Controller implementation/version and About protocol versions (if relevant):
- Does any existing sdkconfig or BSP change fix it? Attach the minimal diff.

**Reproduction and evidence**
- Exact input sequence and initial state:
- Expected and actual result; how often it occurs:
- Tested on physical hardware, simulator, or both:
- Redacted logs or screenshots, if available:
