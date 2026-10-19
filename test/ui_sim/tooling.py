#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build boundary regressions: enforce pins and propagate subprocess failures."""
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_lvgl", ROOT / "sim/prepare_lvgl.py")
prepare_lvgl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_lvgl)


class DependencyPinTests(unittest.TestCase):
    def notice_archive(self, directory):
        archive = directory / "lvgl-9.5.0.tar.gz"
        files = {
            "LICENCE.txt": b"upstream MIT license\n",
            "COPYRIGHTS.md": b"upstream third-party inventory\n",
            "src/libs/qrcode/LICENSE.txt": b"QR code MIT notice\n",
            "src/lvgl.c": b"/* library source */\n",
            "examples/unneeded.c": b"/* excluded example */\n",
        }
        with tarfile.open(archive, "w:gz") as output:
            for name, content in files.items():
                member = tarfile.TarInfo(f"lvgl-{prepare_lvgl.REVISION}/{name}")
                member.size = len(content)
                output.addfile(member, io.BytesIO(content))
        return archive, files

    def test_retains_root_and_nested_upstream_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, files = self.notice_archive(root)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            destination = root / "output"
            with mock.patch.object(prepare_lvgl, "SHA256", digest):
                prepare_lvgl.unpack(archive, destination)
            for name, content in files.items():
                if name.startswith("examples/"):
                    self.assertFalse((destination / name).exists())
                else:
                    self.assertEqual((destination / name).read_bytes(), content)

    def test_repairs_old_cache_missing_upstream_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            archive, files = self.notice_archive(cache)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            target = cache / f"lvgl-{prepare_lvgl.VERSION}-{digest[:12]}"
            target.mkdir()
            (target / ".source-sha256").write_text(digest + "\n")
            with mock.patch.object(prepare_lvgl, "SHA256", digest), \
                    mock.patch.object(prepare_lvgl.urllib.request, "urlopen") as download:
                self.assertEqual(prepare_lvgl.prepare(ROOT, cache), target.resolve())
                download.assert_not_called()
            for name in ("LICENCE.txt", "COPYRIGHTS.md"):
                self.assertEqual((target / name).read_bytes(), files[name])

    def test_rejects_firmware_lvgl_version_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "dependencies.lock"
            lock.write_text("dependencies:\n  lvgl/lvgl:\n    dependencies: []\n    version: 9.4.0\n")
            with self.assertRaisesRegex(ValueError, "dependencies.lock"):
                prepare_lvgl.verify_lock(lock)
        prepare_lvgl.verify_lock(ROOT / "dependencies.lock")

    def test_rejects_corrupted_download_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "lvgl.tar.gz"
            archive.write_bytes(b"invalid or substituted upstream download")
            destination = Path(directory) / "output"
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                prepare_lvgl.unpack(archive, destination)
            self.assertFalse(destination.exists())


class RunnerFailureTests(unittest.TestCase):
    def run_case(self, build_fails):
        # The boundary is subprocess status. Small scripts stand in for a
        # failed compiler or failed interaction assertion; run the real runner.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ui = root / "test/ui_sim"
            ui.mkdir(parents=True)
            runner = ui / "run-all.sh"
            runner.write_text((ROOT / "test/ui_sim/run-all.sh").read_text())
            build = ui / "build.sh"
            build.write_text("#!/bin/bash\nexit 23\n" if build_fails else
                             "#!/bin/bash\nprintf '/bin/true\\n'\n")
            build.chmod(0o755)
            first = ui / "navigation_wifi_modes.sh"
            first.write_text("#!/bin/bash\necho 'FAIL: interaction regression'\nexit 19\n")
            first.chmod(0o755)
            result = subprocess.run(["bash", str(runner), "viewe15"], text=True,
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 23 if build_fails else 19,
                             result.stdout + result.stderr)
            self.assertNotIn("all 10 checks passed", result.stdout)
            if not build_fails:
                self.assertIn("FAIL: interaction regression", result.stdout)

    def test_failed_build_fails_the_batch(self):
        self.run_case(build_fails=True)

    def test_failed_interaction_check_fails_the_batch(self):
        self.run_case(build_fails=False)


if __name__ == "__main__":
    unittest.main()
