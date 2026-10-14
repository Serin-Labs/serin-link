# Pinned upstream test fixture

These files are unmodified Espressif `knob` 1.0.0 sources, copied from the
component pinned by `dependencies.lock`. They let host CI compile the actual
driver without downloading firmware dependencies.

Upstream: [esp-iot-solution knob source](https://github.com/espressif/esp-iot-solution/tree/17c072bdd192f5bdbe26976b50ae410ccdde1ff9/components/knob).
The copyright notices and Apache-2.0 license are retained in each source/header
and `license.txt`.

`iot_knob.c` SHA256:
`bfe06d2eaf02cb7751b591c8207b3a470b8dba714f2067522ff10bdc6759dd4d`.

Production builds patch the managed component's source into their own build
directory. Tests run the same generator on this fixture. Updating the pinned
component requires reviewing the patch and refreshing this fixture explicitly.
