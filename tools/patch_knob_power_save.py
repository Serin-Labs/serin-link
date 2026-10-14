#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Generate a build-local startup fix for the pinned Espressif knob 1.0.0.

The upstream source and its Apache-2.0 notice remain intact. This deliberately
rejects other source versions rather than silently applying an outdated patch.
"""

import argparse
import hashlib
from pathlib import Path


PINNED_SHA256 = "bfe06d2eaf02cb7751b591c8207b3a470b8dba714f2067522ff10bdc6759dd4d"


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError("knob startup patch anchor is not unique; review pinned source")
    return source.replace(old, new, 1)


def patch(source: bytes) -> str:
    if hashlib.sha256(source).hexdigest() != PINNED_SHA256:
        raise ValueError("knob 1.0.0 source SHA256 changed; review startup patch before building")
    text = source.decode("utf-8")
    interrupt_lines = (
        "        knob_gpio_init_intr(config->gpio_encoder_a, !knob->encoder_a_level == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL, knob_power_save_isr_handler, knob->encoder_a);\n"
        "        knob_gpio_init_intr(config->gpio_encoder_b, !knob->encoder_b_level == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL, knob_power_save_isr_handler, knob->encoder_b);\n"
    )
    text = replace_once(text, interrupt_lines, "")
    publish = "    knob->next = s_head_handle;\n    s_head_handle = knob;\n"
    text = replace_once(text, publish + "\n", "")
    text = replace_once(
        text,
        "        esp_timer_create(&knob_timer, &s_knob_timer_handle);",
        "        ret = esp_timer_create(&knob_timer, &s_knob_timer_handle);\n"
        '        KNOB_CHECK_GOTO(ESP_OK == ret, "knob timer init failed", _encoder_deinit);',
    )
    text = replace_once(
        text,
        "    if (!knob->enable_power_save && !s_is_timer_running) {",
        "    /* Publish complete state and timer before GPIO registration enables IRQs. */\n"
        + publish
        + "\n    if (knob->enable_power_save) {\n"
        + interrupt_lines
        + "    }\n\n"
        + "    if (!knob->enable_power_save && !s_is_timer_running) {",
    )
    text = replace_once(
        text,
        "    knob_gpio_deinit(config->gpio_encoder_a);\n    return NULL;",
        "    knob_gpio_deinit(config->gpio_encoder_a);\n    free(knob);\n    return NULL;",
    )
    return (
        "/* Build-generated startup-order fix for Espressif knob 1.0.0.\n"
        " * Source: managed_components/espressif__knob/iot_knob.c\n"
        " * Generator: tools/patch_knob_power_save.py; upstream license below.\n"
        " * Local modifications: Copyright (C) 2026 Mehmet Bayram, Apache-2.0.\n"
        " */\n"
        + text
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("output must be a separate build-local file")
    try:
        generated = patch(args.source.read_bytes())
    except (OSError, ValueError) as error:
        parser.exit(1, f"knob startup patch: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Avoid triggering a rebuild when CMake reconfigures unchanged input.
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)


if __name__ == "__main__":
    main()
