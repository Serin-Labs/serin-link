#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Variant exports must preserve alpha pixels and never overwrite source masters."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = """
static const uint8_t sample_2_dsc_map[] = {0x00,0xFF};
static const lv_image_dsc_t sample_2_dsc = {
 .header.w = 2, .header.h = 1, .header.stride = 2,
};
const serin_icon_t sample = {sample_v, 1};
"""


class ExportTests(unittest.TestCase):
    def test_current_variant_export_keeps_exact_alpha(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source = folder / "icons.c"
            source.write_text(FIXTURE)
            result = subprocess.run([sys.executable, str(ROOT / "tools/recover_icons.py"),
                                     str(source), "--out-dir", str(folder / "exports")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with Image.open(folder / "exports/sample_2_dsc.png") as image:
                self.assertEqual(image.size, (2, 1))
                self.assertEqual(image.getchannel("A").tobytes(), b"\x00\xff")
            self.assertEqual(source.read_text(), FIXTURE)


if __name__ == "__main__":
    unittest.main()
