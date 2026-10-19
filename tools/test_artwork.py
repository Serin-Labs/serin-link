#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Integration contracts: real generation, drift detection, and missing inputs."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ArtworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="serin-artwork-test-")
        cls.repo = Path(cls.temp.name) / "checkout"
        shutil.copytree(ROOT / "tools", cls.repo / "tools", ignore=shutil.ignore_patterns(
            "node_modules", ".venv", "__pycache__"))
        (cls.repo / "tools/artwork").mkdir(exist_ok=True)
        (cls.repo / "tools/artwork/node_modules").symlink_to(
            ROOT / "tools/artwork/node_modules", target_is_directory=True)
        shutil.copytree(ROOT / "main/fonts", cls.repo / "main/fonts")
        shutil.copy2(ROOT / "main/icons_serin.c", cls.repo / "main/icons_serin.c")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_artwork(self):
        return subprocess.run([sys.executable, str(self.repo / "tools/artwork.py"), "--check"],
                              cwd=self.temp.name, text=True, capture_output=True)

    def test_checkout_regenerates_from_outside_repository(self):
        result = self.check_artwork()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_font_is_reported_without_overwriting_it(self):
        path = self.repo / "main/fonts/jakarta_600_15.c"
        original = path.read_bytes()
        changed = original + b"\n/* accidental drift */\n"
        try:
            path.write_bytes(changed)
            result = self.check_artwork()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("main/fonts/jakarta_600_15.c", result.stdout + result.stderr)
            self.assertEqual(path.read_bytes(), changed)
        finally:
            path.write_bytes(original)

    def test_missing_node_has_setup_instruction(self):
        result = subprocess.run(
            [sys.executable, str(self.repo / "tools/gen_fonts.py"), "--out-dir", str(self.repo / "unused")],
            env={**os.environ, "PATH": ""}, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Node 24.18.0", result.stdout + result.stderr)

    def test_missing_master_names_the_missing_input(self):
        path = self.repo / "tools/assets/icons/png/mode_heat.png"
        saved = path.read_bytes()
        try:
            path.unlink()
            result = self.check_artwork()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("mode_heat.png", result.stdout + result.stderr)
        finally:
            path.write_bytes(saved)


if __name__ == "__main__":
    unittest.main()
