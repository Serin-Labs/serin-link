#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Exercise release staging with real RSA signatures and disposable keys.

Only the expensive IDF build/merge is replaced. Key parsing, signature checks,
artifact writes, hashes, manifests, and failure behavior run for real.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib
from urllib.parse import urlsplit

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from esptool.bin_image import ESP32S3FirmwareImage, ImageSegment

REPO = Path(__file__).resolve().parents[2]
FEED = "https://raw.githubusercontent.com/Serin-Labs/serin-cn105/main/firmware/link/ota"
OLD_FEED = "https://github.com/Serin-Labs/serin-link/releases/latest/download/manifest.json"
# What a fresh build on the release chain writes: the fields the publisher checks.
RELEASE_SDKCONFIG = (
    "CONFIG_PARTITION_TABLE_OFFSET=0x8000\n"
    "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y\n"
    f'CONFIG_SERIN_LINK_OTA_MANIFEST_URL="{FEED}/stable/manifest.json"\n'
    f'CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL="{FEED}/beta/manifest.json"\n'
    "CONFIG_SERIN_SHT40_OFFSET_CC=100\n"
    "CONFIG_SERIN_SHT40_RH_GAIN_PPT=1110\n")
# The checkout's defaults chain. Like the real files, only the 1.5" overlay
# sets the sensor trim that RELEASE_SDKCONFIG carries.
TRIM = {"CONFIG_SERIN_SHT40_OFFSET_CC": "100", "CONFIG_SERIN_SHT40_RH_GAIN_PPT": "1110"}
CHECKOUT_DEFAULTS = {
    "sdkconfig.defaults": "CONFIG_PARTITION_TABLE_OFFSET=0x8000\n",
    "sdkconfig.defaults.viewe": ("CONFIG_SERIN_BOARD_VIEWE_15=y\n"
                                 "# Kit enclosure trim\n"
                                 "CONFIG_SERIN_SHT40_OFFSET_CC=100\n"
                                 "CONFIG_SERIN_SHT40_RH_GAIN_PPT=1110\n"),
    "sdkconfig.defaults.viewe21": "CONFIG_SERIN_BOARD_VIEWE_21=y\n",
}
BOARD_PROJECTS = {"viewe15": b"serin_link_viewe15", "viewe21": b"serin_link_viewe21"}
OTA_BOARDS = {"link15": "viewe15", "link21": "viewe21"}
sys.path.insert(0, str(REPO / "tools"))
spec = importlib.util.spec_from_file_location("publish_fw", REPO / "tools/publish_fw.py")
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class StagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys_dir = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.keys_dir.cleanup)
        cls.keys = Path(cls.keys_dir.name)
        cls.signed = {}
        for name in ("release", "other"):
            private = rsa.generate_private_key(public_exponent=65537, key_size=3072)
            (cls.keys / f"{name}.pem").write_bytes(private.private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()))
            (cls.keys / f"{name}.pub").write_bytes(private.public_key().public_bytes(
                serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        for name, key, version in (("release", "release", b"0.1.6"),
                                   ("other", "other", b"0.1.6"),
                                   ("next", "release", b"0.1.7"),
                                   ("beta", "release", b"0.1.7-beta.1")):
            cls.signed[name] = {board: cls.signed_image(version=version, key=key, project=project)
                                for board, project in BOARD_PROJECTS.items()}

    @classmethod
    def signed_image(cls, version=b"0.1.6", key="release", mutate=None, project=b"serin_link_viewe15"):
        # esp_app_desc_t is 256 bytes, with version[32] at offset 16. Build
        # the surrounding ESP32-S3 image with esptool, independently of the
        # publisher's descriptor reader, and sign with a disposable key.
        desc = struct.pack("<IIII32s32s16s16s32s32s80x", 0xABCD5432, 0, 0, 0,
                           version, project, b"12:00:00", b"Sep 18 2026",
                           b"v5.5.4", b"\x01" * 32)
        image = ESP32S3FirmwareImage()
        image.segments = [ImageSegment(0x3c000020, desc)]
        unsigned = cls.keys / "unsigned.bin"
        image.save(str(unsigned))
        if mutate:
            data = bytearray(unsigned.read_bytes())
            mutate(data)
            unsigned.write_bytes(data)
        signed = cls.keys / "signed.bin"
        subprocess.run([sys.executable, "-m", "espsecure", "sign_data", "--version", "2",
                        "--keyfile", str(cls.keys / f"{key}.pem"),
                        "--output", str(signed), str(unsigned)],
                       check=True, capture_output=True)
        return signed.read_bytes()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "version.txt").write_text("0.1.6\n")
        for name, text in CHECKOUT_DEFAULTS.items():
            (self.root / name).write_text(text)
        for field, value in (("ROOT", self.root),
                             ("RELEASE_PUBLIC_KEY", self.keys / "release.pub"),
                             ("SIGNKEY_OVERLAY", self.root / "sdkconfig.signkey.tmp"),
                             ("DEV_SIGNING_KEY", self.keys / "other.pem")):
            p = patch.object(publish, field, value, create=True)
            p.start()
            self.addCleanup(p.stop)
        for kind in ("pub", "fac"):
            for board in ("viewe15", "viewe21"):
                bdir = self.root / f"build_{kind}_{board}"
                bdir.mkdir()
                (bdir / "sdkconfig").write_text(RELEASE_SDKCONFIG)
                (bdir / publish.BIN_NAME).write_bytes(self.signed["release"][board])
                if kind == "fac":
                    (bdir / "boot.bin").write_bytes(b"\xe9boot")
                    (bdir / "table.bin").write_bytes(b"\xaa\x50tbl")
                    (bdir / "flash_args").write_text(
                        "--flash_mode keep --flash_freq keep --flash_size keep\n"
                        "0x0 boot.bin\n0x8000 table.bin\n0x20000 serin_link.bin\n")
        (self.root / ".gitignore").write_text(
            "build_*/\ndist*/\npublished/\nrestaged*/\ndistribution/\nsaved-ota/\n"
            "own-*/\nself-hosted/\ninvalid.pem\nsdkconfig.signkey.tmp\n")
        self.git("init", "-q")
        self.git("config", "user.name", "Release Test")
        self.git("config", "user.email", "release@example.invalid")
        self.git("add", ".")
        self.git("commit", "-qm", "fixture release source")
        for version in ("0.1.6", "0.1.7", "0.1.7-beta.1"):
            self.git("tag", "v" + version)
        self.seed_receipts()

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True,
                              capture_output=True, text=True).stdout.strip()

    def seed_receipts(self, version="0.1.6", development=False, kinds=(False, True)):
        # Generate genuine receipts through the fresh-build public seam. Only
        # IDF and the merger are replaced, as in all release-tool fixtures.
        with patch.object(publish, "build", side_effect=lambda board, *_:
                          self.root / f"build_pub_{board}" / publish.BIN_NAME), \
                patch.object(publish, "build_factory", side_effect=lambda board, *_:
                             self.root / f"build_fac_{board}"), \
                patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            for factory in kinds:
                out = self.root / ("own-factory" if factory else "own-ota") if development else \
                      self.root / ("dist_factory" if factory else "dist")
                args = ["--signing-key", self.keys / ("other.pem" if development else "release.pem"),
                        "--version", version, "--out", out]
                if factory:
                    args.append("--factory")
                if "-" in version:
                    args.extend(["--channel", "beta"])
                if development:
                    args.append("--development")
                # Preserve previous staging when refreshing receipts for a new
                # fixture build, just as a separate release output would.
                previous = {str(p.relative_to(out)): p.read_bytes() for p in out.rglob("*") if p.is_file()}
                code, output = self.run_publish(*args)
                self.assertEqual(code, 0, output)
                shutil.rmtree(out)
                for name, data in previous.items():
                    path = out / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)

    def run_publish(self, *args):
        output = io.StringIO()
        with patch.object(sys, "argv", ["publish_fw.py", *map(str, args)]), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            try:
                code = publish.main()
            except SystemExit as error:
                code = error.code
                output.write(str(error.code))
        return code, output.getvalue()

    def existing_release(self, factory=False):
        stage = self.root / ("dist_factory" if factory else "dist/stable")
        stage.mkdir(parents=True)
        manifest = "factory-manifest.json" if factory else "manifest.json"
        asset = "serin_link_viewe15_factory.bin" if factory else "link15-app.bin"
        (stage / manifest).write_bytes(b"previous manifest")
        (stage / asset).write_bytes(b"previous firmware")
        return stage, {p.name: p.read_bytes() for p in stage.iterdir()}

    def assert_rejected(self, args, stage, previous, message="signature"):
        code, output = self.run_publish(*args)
        self.assertNotEqual(code, 0, output)
        self.assertIn(message, output.lower())
        self.assertEqual({p.name: p.read_bytes() for p in stage.iterdir()}, previous)

    @staticmethod
    def merge_fixture(bdir, out):
        head = bytearray(b"\xff" * 0x20000)
        head[:6] = b"\xe9boot\xff"           # ends in 0xFF, like a padded bootloader
        head[0x8000:0x8005] = b"\xaa\x50tbl"
        out.write_bytes(bytes(head) + (bdir / publish.BIN_NAME).read_bytes())

    def test_production_refuses_real_dirty_staged_untracked_and_untagged_source(self):
        stage, previous = self.existing_release()
        version_file = self.root / "version.txt"
        version_file.write_text("0.1.6\n# edited\n")
        self.assert_rejected(["--no-build", "--version", "0.1.6"], stage, previous, "clean source")
        self.git("add", "version.txt")
        self.assert_rejected(["--no-build", "--version", "0.1.6"], stage, previous, "clean source")
        self.git("restore", "--staged", "version.txt")
        version_file.write_text("0.1.6\n")
        untracked = self.root / "new_source.c"
        untracked.write_text("int release_drift;\n")
        self.assert_rejected(["--no-build"], stage, previous, "untracked")
        untracked.unlink()
        self.git("tag", "-d", "v0.1.6")
        self.assert_rejected(["--no-build"], stage, previous, "tag v0.1.6")
        self.git("tag", "v9.9.9")
        self.assert_rejected(["--no-build"], stage, previous, "tag v0.1.6")

    def test_production_checks_head_contents_even_when_git_status_hides_edits(self):
        stage, previous = self.existing_release()
        version_file = self.root / "version.txt"
        for flag in ("--assume-unchanged", "--skip-worktree"):
            with self.subTest(flag=flag):
                self.git("update-index", flag, "version.txt")
                version_file.write_text("0.1.6\n# hidden edit\n")
                self.assertEqual(self.git("status", "--porcelain"), "")
                self.assert_rejected(["--no-build", "--version", "0.1.6"],
                                     stage, previous, "tagged head contents/modes")
                version_file.write_text("0.1.6\n")
                self.git("update-index", "--no-assume-unchanged", "--no-skip-worktree", "version.txt")
        self.git("config", "core.fileMode", "false")
        version_file.chmod(0o755)
        self.assertEqual(self.git("status", "--porcelain"), "")
        self.assert_rejected(["--no-build"], stage, previous, "tagged head contents/modes")

    def test_ignored_font_source_cannot_evade_the_release_snapshot(self):
        fonts = self.root / "main/fonts"
        fonts.mkdir(parents=True)
        hidden = fonts / "local_font.c"
        hidden.write_text("int compiled_by_font_glob;\n")
        (self.root / ".git/info/exclude").write_text("main/fonts/local_font.c\n")
        self.assertEqual(self.git("status", "--porcelain"), "")
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build"], stage, previous, "ignored untracked source")

    def test_git_status_error_cannot_masquerade_as_clean_source(self):
        # Run a real Git fixture, but simulate Git's failed status command
        # returning no stdout, the unsafe case in the original dirty check.
        bindir = self.root / "build_git_failure"
        bindir.mkdir()
        real_git = shutil.which("git")
        script = bindir / "git"
        script.write_text('#!/bin/sh\nif [ "$1" = status ]; then exit 128; fi\n'
                          f'exec "{real_git}" "$@"\n')
        script.chmod(0o755)
        stage, previous = self.existing_release()
        with patch.dict(os.environ, {"PATH": str(bindir) + os.pathsep + os.environ["PATH"]}):
            self.assert_rejected(["--no-build"], stage, previous, "git command failed")
        shutil.rmtree(self.root / ".git")
        self.assert_rejected(["--no-build"], stage, previous, "git command failed")
        (self.root / ".git").write_text("gitdir: missing-git-directory\n")
        self.assert_rejected(["--no-build"], stage, previous, "git command failed")

    def test_fresh_build_refuses_source_export_inside_another_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            ancestor = Path(temporary)
            def git(*args):
                return subprocess.run(["git", *args], cwd=ancestor, check=True,
                                      capture_output=True, text=True).stdout.strip()
            (ancestor / ".gitignore").write_text("export/\n")
            git("init", "-q")
            git("config", "user.name", "Release Test")
            git("config", "user.email", "release@example.invalid")
            git("add", ".")
            git("commit", "-qm", "unrelated ancestor")
            git("tag", "v0.1.6")
            export = ancestor / "export"
            shutil.copytree(self.root, export, ignore=shutil.ignore_patterns(".git"))
            self.assertEqual(git("status", "--porcelain"), "")
            for development in (False, True):
                for board in BOARD_PROJECTS:
                    (export / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(
                        self.signed["other" if development else "release"][board])
                out = export / ("own-export" if development else "dist")
                args = ["--signing-key", self.keys / ("other.pem" if development else "release.pem"),
                        "--out", out]
                if development:
                    args.append("--development")
                with self.subTest(development=development), \
                        patch.object(publish, "ROOT", export), \
                        patch.object(publish, "build", side_effect=lambda board, *_:
                                     export / f"build_pub_{board}" / publish.BIN_NAME) as build:
                    code, output = self.run_publish(*args)
                    self.assertNotEqual(code, 0, output)
                    self.assertIn("git worktree root", output.lower())
                    build.assert_not_called()
                    self.assertFalse(out.exists())

    def test_source_provenance_accepts_linked_worktree_root(self):
        linked = self.root / "build_linked_checkout"
        self.git("worktree", "add", "--detach", str(linked), "HEAD")
        self.assertTrue((linked / ".git").is_file())
        self.assertEqual(publish.provenance.source_state(linked, "0.1.6", False),
                         publish.provenance.source_state(self.root, "0.1.6", False))

    def test_no_build_requires_receipt_and_rejects_unrelated_config_drift(self):
        stage, previous = self.existing_release()
        bdir = self.root / "build_pub_viewe21"
        receipt = bdir / publish.provenance.RECEIPT
        saved = receipt.read_bytes()
        for content in (None, b"not json", b"{}"):
            with self.subTest(content=content):
                if content is None:
                    receipt.unlink()
                else:
                    receipt.write_bytes(content)
                self.assert_rejected(["--no-build"], stage, previous, "provenance")
        receipt.write_bytes(saved)
        (bdir / "sdkconfig").write_text(RELEASE_SDKCONFIG + "CONFIG_UNRELATED=y\n")
        self.assert_rejected(["--no-build"], stage, previous, "source/config mismatch")

    def test_no_build_rejects_other_valid_signed_binary_at_same_version(self):
        replacement = self.signed_image(mutate=lambda data: data.__setitem__(36, 1),
                                        project=BOARD_PROJECTS["viewe21"])
        (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(replacement)
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build"], stage, previous, "artifact/hash mismatch")

    def test_no_build_rejects_source_commit_even_with_matching_version_and_config(self):
        (self.root / "source.c").write_text("int changed_source;\n")
        self.git("add", "source.c")
        self.git("commit", "-qm", "new source, same version")
        self.git("tag", "-f", "v0.1.6")
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build"], stage, previous, "source/config mismatch")

    def test_factory_receipt_binds_every_flash_input_and_merge_options(self):
        stage, previous = self.existing_release(factory=True)
        bdir = self.root / "build_fac_viewe21"
        # These changes do not affect our fake merger's output. Hashing only
        # the final app/merged image would miss them entirely.
        (bdir / "otadata.bin").write_bytes(b"\xff" * 8192)
        flash = bdir / "flash_args"
        original = flash.read_bytes()
        flash.write_bytes(original + b"0xf000 otadata.bin\n")
        self.seed_receipts(kinds=(True,))
        for name in ("boot.bin", "table.bin", "otadata.bin", "flash_args"):
            path = bdir / name
            saved = path.read_bytes()
            path.write_bytes(saved + (b"\n" if name == "flash_args" else b"changed"))
            with self.subTest(input=name), patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                self.assert_rejected(["--factory", "--no-build"], stage, previous, "source/config mismatch")
            path.write_bytes(saved)

    def test_fresh_build_refuses_source_change_before_any_staged_replacement(self):
        stage, previous = self.existing_release()
        def changing_build(board, *_):
            if board == "viewe21":
                (self.root / "source_changed_during_build.c").write_text("int changed;\n")
            return self.root / f"build_pub_{board}" / publish.BIN_NAME
        with patch.object(publish, "build", side_effect=changing_build):
            self.assert_rejected(["--signing-key", self.keys / "release.pem"],
                                 stage, previous, "clean source")

    def test_development_receipt_tracks_dirty_untagged_source_without_exposing_paths(self):
        self.git("tag", "-d", "v0.1.6")
        (self.root / "local_source.c").write_text("int local_work;\n")
        for kind in ("pub", "fac"):
            for board in BOARD_PROJECTS:
                cfg = self.root / f"build_{kind}_{board}/sdkconfig"
                cfg.write_text(RELEASE_SDKCONFIG + f'CONFIG_SECURE_BOOT_SIGNING_KEY="{self.keys}/other.pem"\n')
                (cfg.parent / publish.BIN_NAME).write_bytes(self.signed["other"][board])
        self.seed_receipts(development=True)
        code, output = self.run_publish("--development", "--no-build", "--out", self.root / "own-ota")
        self.assertEqual(code, 0, output)
        manifest = json.loads((self.root / "own-ota/stable/manifest.json").read_text())
        self.assertTrue(manifest["development"])
        self.assertTrue(manifest["builds"][0]["provenance"]["source"]["dirty"])
        self.assertIsNone(manifest["builds"][0]["provenance"]["source"]["tag"])
        serialized = json.dumps(manifest)
        for secret in (str(self.root), str(self.keys), "other.pem", "CONFIG_SECURE_BOOT_SIGNING_KEY"):
            self.assertNotIn(secret, serialized)
        (self.root / "local_source.c").write_text("int different_local_work;\n")
        stage = self.root / "own-ota/stable"
        previous = {p.name: p.read_bytes() for p in stage.iterdir()}
        self.assert_rejected(["--development", "--no-build", "--out", self.root / "own-ota"],
                             stage, previous, "source/config mismatch")

    def test_development_snapshot_includes_staged_additions(self):
        added = self.root / "staged_source.c"
        added.write_text("int staged_version_one;\n")
        self.git("add", "staged_source.c")
        for board in BOARD_PROJECTS:
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["other"][board])
        self.seed_receipts(development=True, kinds=(False,))
        added.write_text("int staged_version_two;\n")
        self.git("add", "staged_source.c")
        stage = self.root / "own-ota/stable"
        stage.mkdir(parents=True)
        (stage / "manifest.json").write_bytes(b"previous development manifest")
        previous = {p.name: p.read_bytes() for p in stage.iterdir()}
        self.assert_rejected(["--development", "--no-build", "--out", self.root / "own-ota"],
                             stage, previous, "source/config mismatch")

    def test_factory_rejects_inputs_or_config_changed_by_merge(self):
        stage, previous = self.existing_release(factory=True)
        for name in ("boot.bin", "sdkconfig"):
            path = self.root / "build_fac_viewe21" / name
            saved = path.read_bytes()
            def changing_merge(bdir, out):
                self.merge_fixture(bdir, out)
                if bdir.name == "build_fac_viewe21":
                    path.write_bytes(saved + b"\nchanged during merge")
            with self.subTest(input=name), patch.object(publish, "merge_factory", side_effect=changing_merge):
                self.assert_rejected(["--factory", "--no-build"], stage, previous, "changed during merge")
            path.write_bytes(saved)

    def test_split_existing_requires_and_preserves_original_provenance(self):
        published = self.root / "published"
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--out", published)
        self.assertEqual(code, 0, output)
        manifest, _ = self.staged_factory(published)
        original = [entry["provenance"] for entry in manifest["builds"]]
        # The republisher can have unrelated dirty source and no release tag.
        self.git("tag", "-d", "v0.1.6")
        (self.root / "republisher_source.c").write_text("int republisher;\n")
        code, output = self.run_publish("--factory", "--split-existing", published,
                                        "--out", self.root / "restaged")
        self.assertEqual(code, 0, output)
        again, _ = self.staged_factory(self.root / "restaged")
        self.assertEqual([entry["provenance"] for entry in again["builds"]], original)
        stage, previous = self.existing_release(factory=True)
        del manifest["builds"][1]["provenance"]
        (published / "factory-manifest.json").write_text(json.dumps(manifest))
        self.assert_rejected(["--factory", "--split-existing", published], stage, previous, "legacy")

    def test_no_build_verifies_without_private_key_and_hashes_exact_staged_bytes(self):
        code, output = self.run_publish("--no-build")
        self.assertEqual(code, 0, output)
        stage = self.root / "dist/stable"
        manifest_bytes = (stage / "manifest.json").read_bytes()
        # Read the actual firmware boundary, not a mirrored Python constant.
        device_limit = int(re.search(r"#define MANIFEST_MAX\s+(\d+)",
                                     (REPO / "main/link_ota.c").read_text())[1])
        self.assertLess(len(manifest_bytes), device_limit)
        manifest = json.loads(manifest_bytes)
        self.assertEqual([entry["board"] for entry in manifest["builds"]], ["link15", "link21"])
        for entry in manifest["builds"]:
            expected = self.signed["release"][OTA_BOARDS[entry["board"]]]
            self.assertEqual((stage / entry["path"]).read_bytes(), expected)
            self.assertEqual(entry["sha256"], hashlib.sha256(expected).hexdigest())

    def test_oversized_ota_manifest_leaves_previous_staging_intact(self):
        stage, previous = self.existing_release()
        # argparse accepts repeated valid boards. They used to produce a
        # manifest too large for the dial's fixed fetch buffer.
        self.assert_rejected(["--no-build", "--boards", *(["viewe15", "viewe21"] * 3)],
                             stage, previous, "device limit is 4095")
        receipts = [self.root / f"build_pub_{board}" / publish.provenance.RECEIPT
                    for board in BOARD_PROJECTS]
        for receipt in receipts:
            receipt.write_bytes(b"prior build receipt")
        with patch.object(publish, "build", side_effect=lambda board, *_:
                          self.root / f"build_pub_{board}" / publish.BIN_NAME):
            self.assert_rejected(["--signing-key", self.keys / "release.pem", "--boards",
                                  *(["viewe15", "viewe21"] * 3)],
                                 stage, previous, "device limit is 4095")
        self.assertTrue(all(receipt.read_bytes() == b"prior build receipt" for receipt in receipts))

    def make_distribution(self):
        distribution = self.root / "distribution"
        source = Path(os.environ.get("SERIN_DISTRIBUTION_ROOT", REPO.parent / "serin-cn105"))
        if not (source / "scripts/validate-manifests.py").is_file():
            # CI checks the validator out, so a miss there is a broken job.
            message = f"no distribution validator at {source}; set SERIN_DISTRIBUTION_ROOT"
            if os.environ.get("CI"):
                self.fail(message)
            self.skipTest(message)
        shutil.copytree(source / "scripts", distribution / "scripts",
                        ignore=shutil.ignore_patterns("__pycache__"))
        (distribution / "scripts/keys/serin-link-release.pub").write_bytes(
            (self.keys / "release.pub").read_bytes())
        for channel, version, image in (("stable", "0.1.6", "release"),
                                        ("beta", "0.1.7-beta.1", "beta")):
            folder = distribution / "firmware/link/ota" / channel
            folder.mkdir(parents=True)
            builds = []
            for board in ("link15", "link21"):
                name = f"{board}-app.bin"
                data = self.signed[image][OTA_BOARDS[board]]
                (folder / name).write_bytes(data)
                builds.append({"board": board, "path": name, "size": len(data),
                               "sha256": hashlib.sha256(data).hexdigest()})
            (folder / "manifest.json").write_text(json.dumps({
                "name": "Serin Link", "version": version, "channel": channel,
                "builds": builds}, indent=2) + "\n")
        other = distribution / "firmware/esphome"
        other.mkdir()
        (other / "firmware.bin").write_bytes(b"other product")
        (other / "manifest.json").write_text(json.dumps({"name": "ESPHome", "version": "2026.7.4",
            "builds": [{"board": "nanoc6", "chipFamily": "ESP32-C6",
                        "sha256": hashlib.sha256(b"other product").hexdigest(),
                        "parts": [{"path": "firmware.bin", "offset": 0}]}]}))
        return distribution

    @staticmethod
    def distribution_files(distribution):
        return {str(path.relative_to(distribution)): path.read_bytes()
                for path in (distribution / "firmware").rglob("*") if path.is_file()}

    def test_public_distribution_preserves_other_channel_and_products(self):
        distribution = self.make_distribution()
        previous = self.distribution_files(distribution)
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertEqual(code, 0, output)
        stage = distribution / "firmware/link/ota/stable"
        manifest = json.loads((stage / "manifest.json").read_text())
        self.assertEqual(manifest["version"], "0.1.6")
        self.assertEqual(manifest["channel"], "stable")
        self.assertNotIn("release_url", manifest)
        self.assertEqual({b["board"] for b in manifest["builds"]}, {"link15", "link21"})
        for build in manifest["builds"]:
            self.assertNotIn("enc_size", build)
            self.assertEqual((stage / build["path"]).read_bytes(), self.signed["release"][OTA_BOARDS[build["board"]]])
        after = self.distribution_files(distribution)
        untouched = {name: data for name, data in previous.items()
                     if not name.startswith("firmware/link/ota/stable/")}
        self.assertEqual({name: after[name] for name in untouched}, untouched)

    def test_builtin_urls_resolve_to_staged_channels_without_cross_channel_changes(self):
        distribution = self.make_distribution()
        kconfig = (REPO / "main/Kconfig.projbuild").read_text()
        for channel, version, image in (("stable", "0.1.6", "release"),
                                        ("beta", "0.1.7-beta.1", "beta"),
                                        ("stable", "0.1.7", "next")):
            with self.subTest(channel=channel, version=version):
                before = self.distribution_files(distribution)
                for board in ("viewe15", "viewe21"):
                    (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed[image][board])
                self.seed_receipts(version=version, kinds=(False,))
                code, output = self.run_publish("--no-build", "--channel", channel, "--version", version,
                                                "--distribution", distribution)
                self.assertEqual(code, 0, output)
                key = "SERIN_LINK_OTA_MANIFEST_BETA_URL" if channel == "beta" else "SERIN_LINK_OTA_MANIFEST_URL"
                default = re.search(rf'config {key}\n.*?\n    default "([^"]+)"', kconfig, re.S)[1]
                url = urlsplit(default)
                self.assertEqual(url.scheme, "https")
                self.assertEqual(url.netloc, "raw.githubusercontent.com")
                relative = url.path.removeprefix("/Serin-Labs/serin-cn105/main/")
                manifest_path = distribution / relative
                manifest = json.loads(manifest_path.read_text())
                self.assertEqual(manifest["version"], version)
                self.assertEqual(manifest["channel"], channel)
                for build in manifest["builds"]:
                    data = (manifest_path.parent / build["path"]).read_bytes()
                    self.assertEqual(data, self.signed[image][OTA_BOARDS[build["board"]]])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), build["sha256"])
                after = self.distribution_files(distribution)
                untouched = {name: data for name, data in before.items()
                             if not name.startswith(f"firmware/link/ota/{channel}/")}
                self.assertEqual({name: after[name] for name in untouched}, untouched)

    def test_invalid_other_product_prevents_replacing_public_feed(self):
        distribution = self.make_distribution()
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertEqual(code, 0, output)
        (distribution / "firmware/esphome/firmware.bin").write_bytes(b"corrupted other product")
        before = self.distribution_files(distribution)
        for board in ("viewe15", "viewe21"):
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["next"][board])
        self.seed_receipts(version="0.1.7", kinds=(False,))
        code, output = self.run_publish("--no-build", "--version", "0.1.7", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertIn("sha256 does not match", output)
        self.assertEqual(self.distribution_files(distribution), before)

    def test_retired_encrypted_feed_prevents_publication(self):
        distribution = self.make_distribution()
        legacy = distribution / "firmware/link/manifest.json"
        legacy.write_text(json.dumps({
            "name": "Retired encrypted Link", "version": "0.1.6", "builds": [{
                "board": "link15", "path": "ota/stable/link15-app.bin",
                "size": len(self.signed["release"]["viewe15"]), "enc_size": 123,
                "sha256": hashlib.sha256(self.signed["release"]["viewe15"]).hexdigest()}]}))
        before = self.distribution_files(distribution)
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertIn("unknown Link OTA channel directory", output)
        self.assertEqual(self.distribution_files(distribution), before)

    def test_distribution_refuses_cached_build_with_old_update_urls(self):
        distribution = self.make_distribution()
        before = self.distribution_files(distribution)
        cfg = self.root / "build_pub_viewe21/sdkconfig"
        cfg.write_text(cfg.read_text().replace(f"{FEED}/stable/manifest.json", OLD_FEED))
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertIn("manifest URL", output)
        self.assertEqual(self.distribution_files(distribution), before)

    def test_every_production_image_must_use_the_public_feeds(self):
        # A stale build dir signs and verifies cleanly; the factory and plain
        # OTA paths must still refuse an updater that polls the old feeds.
        for kind, factory in (("pub", False), ("fac", True)):
            cfg = self.root / f"build_{kind}_viewe21/sdkconfig"
            stage, previous = self.existing_release(factory=factory)
            args = ["--no-build"] + (["--factory"] if factory else [])
            for channel in ("stable", "beta"):
                with self.subTest(factory=factory, channel=channel):
                    cfg.write_text(RELEASE_SDKCONFIG.replace(f"{FEED}/{channel}/manifest.json", OLD_FEED))
                    with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                        self.assert_rejected(args, stage, previous, message=f"{channel} manifest url")
            cfg.write_text(RELEASE_SDKCONFIG)

    def test_no_build_refuses_a_build_dir_with_stale_sensor_trim(self):
        # IDF reads the overlays only when it creates sdkconfig, so a build dir
        # configured before a trim change still signs and verifies cleanly.
        stale = {"CONFIG_SERIN_SHT40_OFFSET_CC": "153", "CONFIG_SERIN_SHT40_RH_GAIN_PPT": "1097"}
        for kind, factory in (("pub", False), ("fac", True)):
            cfg = self.root / f"build_{kind}_viewe15/sdkconfig"
            stage, previous = self.existing_release(factory=factory)
            args = ["--no-build"] + (["--factory"] if factory else [])
            for key, old in stale.items():
                changed = RELEASE_SDKCONFIG.replace(f"{key}={TRIM[key]}", f"{key}={old}")
                missing = RELEASE_SDKCONFIG.replace(f"{key}={TRIM[key]}\n", "")
                for case, text in (("stale", changed), ("missing", missing)):
                    with self.subTest(factory=factory, key=key, case=case):
                        self.assertNotEqual(text, RELEASE_SDKCONFIG)
                        cfg.write_text(text)
                        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                            self.assert_rejected(args, stage, previous, message=key.lower())
            cfg.write_text(RELEASE_SDKCONFIG)
            shutil.rmtree(stage)

    def test_no_build_follows_an_overlay_edit_not_the_old_build(self):
        overlay = self.root / "sdkconfig.defaults.viewe"
        overlay.write_text(overlay.read_text().replace("OFFSET_CC=100", "OFFSET_CC=90"))
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build"], stage, previous, message="clean source")
        self.git("add", "sdkconfig.defaults.viewe")
        self.git("commit", "-qm", "new calibration")
        self.git("tag", "-f", "v0.1.6")
        self.assert_rejected(["--no-build"], stage, previous, message="config_serin_sht40_offset_cc")
        cfg = self.root / "build_pub_viewe15/sdkconfig"
        cfg.write_text(RELEASE_SDKCONFIG.replace("OFFSET_CC=100", "OFFSET_CC=90"))
        self.assert_rejected(["--no-build"], stage, previous, message="provenance")

    def test_no_build_needs_the_board_overlays_it_compares_against(self):
        (self.root / "sdkconfig.defaults.viewe").unlink()
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build"], stage, previous, message="clean source")

    def test_board_trim_comes_from_the_checkout_overlay_chain(self):
        # Later files in the chain win; a board whose chain sets no trim has
        # nothing to compare.
        (self.root / "sdkconfig.defaults").write_text("CONFIG_SERIN_SHT40_OFFSET_CC=153\n")
        self.assertEqual(publish.board_calibration("viewe15"), TRIM)
        self.assertEqual(publish.board_calibration("viewe21"), {"CONFIG_SERIN_SHT40_OFFSET_CC": "153"})
        (self.root / "sdkconfig.defaults").write_text("")
        self.assertEqual(publish.board_calibration("viewe21"), {})
        # The real 1.5" overlay pins both values, so the guard is never vacuous there.
        with patch.object(publish, "ROOT", REPO):
            self.assertEqual(set(publish.board_calibration("viewe15")), set(TRIM))

    def test_development_images_may_use_their_own_feeds(self):
        own = RELEASE_SDKCONFIG.replace(FEED, "https://updates.example/link")
        for kind in ("pub", "fac"):
            for board in ("viewe15", "viewe21"):
                (self.root / f"build_{kind}_{board}/sdkconfig").write_text(own)
                (self.root / f"build_{kind}_{board}" / publish.BIN_NAME).write_bytes(self.signed["other"][board])
        self.seed_receipts(development=True)
        code, output = self.run_publish("--no-build", "--development", "--out", self.root / "own-ota")
        self.assertEqual(code, 0, output)
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--no-build", "--factory", "--development",
                                            "--out", self.root / "own-factory")
        self.assertEqual(code, 0, output)

    def test_distribution_rejects_missing_tools_development_factory_and_partial_releases(self):
        distribution = self.make_distribution()
        before = self.distribution_files(distribution)
        cases = (("--development", "--out", self.root / "own-feed"),
                 ("--factory",), ("--boards", "viewe15"))
        for args in cases:
            with self.subTest(args=args):
                code, output = self.run_publish("--no-build", "--distribution", distribution, *args)
                self.assertNotEqual(code, 0, output)
                self.assertIn("--distribution", output)
                self.assertEqual(self.distribution_files(distribution), before)
        (distribution / "scripts/validate-manifests.py").unlink()
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertEqual(self.distribution_files(distribution), before)

    def test_distribution_rejects_symlinks_and_overlapping_output_paths(self):
        distribution = self.make_distribution()
        before = self.distribution_files(distribution)
        code, output = self.run_publish("--no-build", "--distribution", distribution,
                                        "--out", distribution / "firmware/link/ota")
        self.assertNotEqual(code, 0, output)
        self.assertEqual(self.distribution_files(distribution), before)
        ota = distribution / "firmware/link/ota"
        saved_ota = self.root / "saved-ota"
        ota.rename(saved_ota)
        try:
            ota.symlink_to(distribution / "firmware/link", target_is_directory=True)
            code, output = self.run_publish("--no-build", "--distribution", distribution)
            self.assertNotEqual(code, 0, output)
            self.assertIn("symlink", output)
        finally:
            ota.unlink()
            saved_ota.rename(ota)
        self.assertEqual(self.distribution_files(distribution), before)

    def test_explicit_matching_private_key_allows_staging(self):
        code, output = self.run_publish("--no-build", "--signing-key", self.keys / "release.pem")
        self.assertEqual(code, 0, output)

    def test_supplied_key_is_validated_even_without_a_build(self):
        invalid = self.root / "invalid.pem"
        invalid.write_text("not a signing key")
        stage, previous = self.existing_release()
        for key in (invalid, self.keys / "other.pem", self.keys / "release.pub"):
            with self.subTest(key=key.name):
                self.assert_rejected(["--no-build", "--signing-key", key],
                                     stage, previous, message="key")

    def test_build_requires_production_key_before_invoking_idf(self):
        stage, previous = self.existing_release()
        with patch.object(publish, "build", side_effect=lambda board, *_:
                          self.root / f"build_pub_{board}" / publish.BIN_NAME):
            self.assert_rejected([], stage, previous, message="--signing-key")

    def test_new_build_output_is_verified(self):
        (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(self.signed["other"]["viewe21"])
        stage, previous = self.existing_release()
        with patch.object(publish, "build", side_effect=lambda board, *_:
                          self.root / f"build_pub_{board}" / publish.BIN_NAME):
            self.assert_rejected(["--signing-key", self.keys / "release.pem"], stage, previous)

    def test_invalid_second_board_preserves_entire_previous_release(self):
        valid = self.signed["release"]["viewe21"]
        tampered = bytearray(valid)
        tampered[10] ^= 1
        bad_signature = bytearray(valid)
        bad_signature[-4096 + 812] ^= 1
        struct.pack_into("<I", bad_signature, len(valid) - 4096 + 1196,
                         zlib.crc32(bad_signature[-4096:-4096 + 1196]))
        bad_embedded_key = bytearray(valid)
        bad_embedded_key[-4096 + 36] ^= 1
        struct.pack_into("<I", bad_embedded_key, len(valid) - 4096 + 1196,
                         zlib.crc32(bad_embedded_key[-4096:-4096 + 1196]))
        bad_crc = bytearray(valid)
        bad_crc[-4096 + 1196] ^= 1
        later_valid_block = (valid[:-4096] + bytes(bad_signature[-4096:-4096 + 1216])
                             + valid[-4096:-4096 + 1216] + b"\xff" * (4096 - 2432))
        stage, previous = self.existing_release()
        cases = {"wrong signer": self.signed["other"]["viewe21"], "unsigned": valid[:-4096],
                 "changed payload": tampered, "bad signature": bad_signature,
                 "changed embedded key": bad_embedded_key, "bad CRC": bad_crc,
                 "truncated": valid[:-1], "empty": b"",
                 "valid later signature cannot rescue first": later_valid_block}
        for name, data in cases.items():
            with self.subTest(name=name):
                (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(data)
                self.assert_rejected(["--no-build"], stage, previous)

    def test_factory_checks_final_merged_app(self):
        stage, previous = self.existing_release(factory=True)

        def corrupt_merge(bdir, out):
            # Simulates a stale/wrong app included by merge, even though the
            # standalone build output carries the correct signature.
            out.write_bytes(b"\xff" * 0x20000 + self.signed["other"]["viewe15"])

        with patch.object(publish, "build_factory", side_effect=lambda board, *_:
                          self.root / f"build_fac_{board}"), \
                patch.object(publish, "merge_factory", side_effect=corrupt_merge):
            self.assert_rejected(["--factory", "--signing-key", self.keys / "release.pem"],
                                 stage, previous)

    def test_factory_no_build_checks_existing_app_without_private_key(self):
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture), \
                patch.object(publish, "build_factory", side_effect=AssertionError("unexpected build")):
            code, output = self.run_publish("--factory", "--no-build")
        self.assertEqual(code, 0, output)
        stage = self.root / "dist_factory"
        manifest = json.loads((stage / "factory-manifest.json").read_text())
        for entry in manifest["builds"]:
            data = (stage / entry["path"]).read_bytes()
            self.assertEqual(data[0x20000:], self.signed["release"][entry["board"]])
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())

    def test_factory_no_build_merges_without_idf_or_private_key(self):
        bdir = self.root / "build_fac_viewe15"
        (bdir / "boot.bin").write_bytes(b"\xe9boot")
        (bdir / "table.bin").write_bytes(b"\xaa\x50tbl")
        (bdir / "flash_args").write_text(
            "--flash_mode keep --flash_freq keep --flash_size keep\n"
            "0x0 boot.bin\n0x8000 table.bin\n0x20000 serin_link.bin\n")
        # Python and esptool remain available through sys.executable, while
        # idf.py, the compiler, and production private keys are unavailable.
        bindir = self.root / "build_tools"
        bindir.mkdir()
        (bindir / "git").symlink_to(shutil.which("git"))
        with patch.dict(os.environ, {"PATH": str(bindir)}):
            try:
                code, output = self.run_publish("--factory", "--no-build", "--boards", "viewe15")
            except FileNotFoundError as error:
                self.fail(f"--no-build unexpectedly needs a build tool: {error}")
        self.assertEqual(code, 0, output)
        data = (self.root / "dist_factory/serin_link_viewe15_factory.bin").read_bytes()
        self.assertEqual(data[0x20000:], self.signed["release"]["viewe15"])

    def staged_factory(self, stage):
        manifest = json.loads((stage / "factory-manifest.json").read_text())
        return manifest, {p.name: p.read_bytes() for p in stage.iterdir() if p.suffix == ".bin"}

    def test_factory_parts_reassemble_to_the_merged_image_and_skip_nvs(self):
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build")
        self.assertEqual(code, 0, output)
        manifest, files = self.staged_factory(self.root / "dist_factory")
        for entry in manifest["builds"]:
            merged = files[entry["path"]]
            rebuilt = bytearray(b"\xff" * len(merged))
            self.assertEqual([p["offset"] for p in entry["parts"]], [0, 0x8000, 0xf000, 0x20000])
            for part in entry["parts"]:
                chunk = files[part["path"]]
                self.assertEqual(part["sha256"], hashlib.sha256(chunk).hexdigest())
                self.assertFalse(part["offset"] < 0xf000 and part["offset"] + len(chunk) > 0x9000,
                                 f"{part['path']} reaches into nvs")
                rebuilt[part["offset"]:part["offset"] + len(chunk)] = chunk
            self.assertEqual(bytes(rebuilt), merged)
            self.assertEqual(files[entry["parts"][2]["path"]], b"\xff" * 0x2000)
        self.assertEqual(len(files), 10)  # no unreferenced file for the distribution validator to fail on

    def test_factory_refuses_a_merged_image_that_writes_into_nvs(self):
        def merge_with_nvs(bdir, out):
            self.merge_fixture(bdir, out)
            data = bytearray(out.read_bytes())
            data[0x9000:0x9004] = b"nvs!"
            out.write_bytes(data)
        stage, previous = self.existing_release(factory=True)
        with patch.object(publish, "merge_factory", side_effect=merge_with_nvs):
            self.assert_rejected(["--factory", "--no-build"], stage, previous, message="outside")

    def test_beta_factory_stages_to_its_own_directory_with_channel(self):
        for board in ("viewe15", "viewe21"):
            (self.root / f"build_fac_{board}" / publish.BIN_NAME).write_bytes(self.signed["beta"][board])
        self.seed_receipts(version="0.1.7-beta.1", kinds=(True,))
        stable, previous = self.existing_release(factory=True)
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--channel", "beta",
                                            "--version", "0.1.7-beta.1")
        self.assertEqual(code, 0, output)
        manifest, _ = self.staged_factory(self.root / "dist_factory/beta")
        self.assertEqual((manifest["channel"], manifest["version"]), ("beta", "0.1.7-beta.1"))
        self.assertEqual({p.name: p.read_bytes() for p in stable.iterdir() if p.is_file()}, previous)
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--channel", "beta")
        self.assertNotEqual(code, 0, output)  # 0.1.6 is not a pre-release

    def test_split_existing_adds_parts_without_changing_the_published_images(self):
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            self.run_publish("--factory", "--no-build")
        published = self.root / "published"
        published.mkdir()
        manifest, files = self.staged_factory(self.root / "dist_factory")
        for entry in manifest["builds"]:
            (published / entry["path"]).write_bytes(files[entry["path"]])
            del entry["parts"]
        manifest["dirty"] = False
        (published / "factory-manifest.json").write_text(json.dumps(manifest))
        with patch.object(publish, "merge_factory", side_effect=AssertionError("unexpected merge")):
            code, output = self.run_publish("--factory", "--split-existing", published,
                                            "--out", self.root / "restaged")
        self.assertEqual(code, 0, output)
        again, refiles = self.staged_factory(self.root / "restaged")
        self.assertFalse(again["dirty"])  # provenance is the published release's, not this checkout's
        self.assertEqual(refiles, files)
        self.assertTrue(all(entry["parts"] for entry in again["builds"]))
        # A published image that no longer matches its manifest is not republished.
        (published / "serin_link_viewe21_factory.bin").write_bytes(b"tampered")
        code, output = self.run_publish("--factory", "--split-existing", published,
                                        "--out", self.root / "restaged-2")
        self.assertNotEqual(code, 0, output)
        self.assertIn("sha256", output)

    def test_factory_build_with_matching_key_stages_verified_images(self):
        with patch.object(publish, "build_factory", side_effect=lambda board, *_:
                          self.root / f"build_fac_{board}"), \
                patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--signing-key", self.keys / "release.pem")
        self.assertEqual(code, 0, output)
        self.assertTrue((self.root / "dist_factory/factory-manifest.json").is_file())

    def test_factory_invalid_second_board_preserves_previous_release(self):
        (self.root / "build_fac_viewe21" / publish.BIN_NAME).write_bytes(self.signed["other"]["viewe21"])
        stage, previous = self.existing_release(factory=True)
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            self.assert_rejected(["--factory", "--no-build"], stage, previous)

    def test_factory_subset_removes_unverified_stale_assets(self):
        stage, _ = self.existing_release(factory=True)
        stale = stage / "serin_link_viewe21_factory.bin"
        stale.write_bytes(b"unverified old image")
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--boards", "viewe15")
        self.assertEqual(code, 0, output)
        self.assertFalse(stale.exists(), "Unverified factory image remains in the release output")

    def test_development_requires_separate_explicit_output(self):
        stage, previous = self.existing_release()
        self.assert_rejected(["--no-build", "--development"], stage, previous, message="--out")
        self.assert_rejected(["--no-build", "--development", "--out", self.root / "dist"],
                             stage, previous, message="--out")

    def test_development_still_verifies_own_signer(self):
        for board in ("viewe15", "viewe21"):
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["other"][board])
        self.seed_receipts(development=True, kinds=(False,))
        out = self.root / "self-hosted"
        code, output = self.run_publish("--no-build", "--development", "--out", out)
        self.assertEqual(code, 0, output)
        stage = out / "stable"
        previous = {p.name: p.read_bytes() for p in stage.iterdir()}
        (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(self.signed["release"]["viewe21"])
        self.assert_rejected(["--no-build", "--development", "--out", out], stage, previous)

    def test_development_factory_uses_separate_output_and_own_key(self):
        (self.root / "build_fac_viewe15" / publish.BIN_NAME).write_bytes(self.signed["other"]["viewe15"])
        (self.root / "build_fac_viewe21" / publish.BIN_NAME).write_bytes(self.signed["other"]["viewe21"])
        self.seed_receipts(development=True, kinds=(True,))
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--boards", "viewe15",
                                            "--development", "--out", self.root / "own-factory")
        self.assertEqual(code, 0, output)
        self.assertEqual((self.root / "own-factory/serin_link_viewe15_factory.bin").read_bytes()[0x20000:],
                         self.signed["other"]["viewe15"])
        self.assertFalse((self.root / "dist_factory").exists())

    def test_no_build_cannot_relabel_old_firmware(self):
        for factory in (False, True):
            with self.subTest(factory=factory):
                stage, previous = self.existing_release(factory=factory)
                args = ["--no-build", "--version", "0.1.7"]
                if factory:
                    args.append("--factory")
                with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                    self.assert_rejected(args, stage, previous, message="version")

    def test_signed_app_for_the_other_board_preserves_previous_release(self):
        # Catch checking only signatures, versions or build-directory names:
        # these images have the right signer/version and matching sdkconfigs.
        for factory, kind in ((False, "pub"), (True, "fac")):
            stage, previous = self.existing_release(factory=factory)
            for fresh in (False, True):
                for target, actual in (("viewe15", "viewe21"), ("viewe21", "viewe15")):
                    with self.subTest(factory=factory, fresh=fresh, target=target):
                        for board in BOARD_PROJECTS:
                            data = self.signed["release"][actual if board == target else board]
                            (self.root / f"build_{kind}_{board}" / publish.BIN_NAME).write_bytes(data)
                        args = ["--signing-key", self.keys / "release.pem"] if fresh else ["--no-build"]
                        if factory:
                            args.append("--factory")
                        with patch.object(publish, "build", side_effect=lambda board, *_:
                                          self.root / f"build_pub_{board}" / publish.BIN_NAME), \
                                patch.object(publish, "build_factory", side_effect=lambda board, *_:
                                             self.root / f"build_fac_{board}"), \
                                patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                            self.assert_rejected(args, stage, previous, message="project")

    def test_project_name_must_be_specific_ascii_and_terminated(self):
        # A valid signature cannot rescue an absent, generic or malformed
        # board identity; reading beyond project_name[32] is also invalid.
        cases = {"generic": b"serin_link", "empty": b"", "unknown": b"serin_link_viewe21_extra",
                 "non-ASCII": b"serin_link_viewe21\xff", "unterminated": b"serin_link_viewe21" + b"x" * 14}
        apps = {name: self.signed_image(project=project) for name, project in cases.items()}
        for factory, kind in ((False, "pub"), (True, "fac")):
            stage, previous = self.existing_release(factory=factory)
            for name, app in apps.items():
                with self.subTest(factory=factory, project=name):
                    (self.root / f"build_{kind}_viewe21" / publish.BIN_NAME).write_bytes(app)
                    args = ["--no-build"] + (["--factory"] if factory else [])
                    with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                        self.assert_rejected(args, stage, previous, message="project")

    def test_factory_checks_board_in_final_merged_app(self):
        stage, previous = self.existing_release(factory=True)
        def merge_wrong_board(bdir, out):
            self.merge_fixture(bdir, out)
            if bdir.name == "build_fac_viewe21":
                out.write_bytes(out.read_bytes()[:0x20000] + self.signed["release"]["viewe15"])
        with patch.object(publish, "merge_factory", side_effect=merge_wrong_board):
            self.assert_rejected(["--no-build", "--factory"], stage, previous, message="project")

    def test_split_existing_rejects_crossed_boards_and_generic_archives(self):
        published = self.root / "published"
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--out", published)
        self.assertEqual(code, 0, output)
        manifest, files = self.staged_factory(published)
        stage, previous = self.existing_release(factory=True)
        cases = (("viewe15", self.signed["release"]["viewe21"]),
                 ("viewe21", self.signed["release"]["viewe15"]),
                 ("viewe21", self.signed_image(project=b"serin_link")))
        for target, app in cases:
            with self.subTest(target=target, project=app[80:112]):
                for entry in manifest["builds"]:
                    data = files[entry["path"]]
                    if entry["board"] == target:
                        data = data[:0x20000] + app
                    (published / entry["path"]).write_bytes(data)
                    entry["sha256"] = hashlib.sha256(data).hexdigest()
                    entry["size"] = len(data)
                (published / "factory-manifest.json").write_text(json.dumps(manifest))
                self.assert_rejected(["--factory", "--split-existing", published],
                                     stage, previous, message="project")

    def test_second_board_version_mismatch_preserves_previous_release(self):
        for factory, kind in ((False, "pub"), (True, "fac")):
            with self.subTest(factory=factory):
                (self.root / f"build_{kind}_viewe21" / publish.BIN_NAME).write_bytes(self.signed["next"]["viewe21"])
                stage, previous = self.existing_release(factory=factory)
                args = ["--no-build"] + (["--factory"] if factory else [])
                with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                    self.assert_rejected(args, stage, previous, message="version")

    def test_matching_beta_version_is_staged(self):
        for board in ("viewe15", "viewe21"):
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["beta"][board])
        self.seed_receipts(version="0.1.7-beta.1", kinds=(False,))
        code, output = self.run_publish("--no-build", "--channel", "beta", "--version", "0.1.7-beta.1")
        self.assertEqual(code, 0, output)
        manifest = json.loads((self.root / "dist/beta/manifest.json").read_text())
        self.assertEqual(manifest["version"], "0.1.7-beta.1")
        self.assertEqual(manifest["channel"], "beta")
        code, output = self.run_publish("--no-build", "--channel", "beta", "--version", "0.1.7-beta.9")
        self.assertNotEqual(code, 0, output)
        self.assertIn("version", output.lower())
        self.assertEqual(json.loads((self.root / "dist/beta/manifest.json").read_text()), manifest)

    def test_factory_checks_version_in_merged_app(self):
        stage, previous = self.existing_release(factory=True)
        def merge_wrong_version(bdir, out):
            out.write_bytes(b"\xff" * 0x20000 + self.signed["next"]["viewe15"])
        with patch.object(publish, "merge_factory", side_effect=merge_wrong_version):
            self.assert_rejected(["--no-build", "--factory"], stage, previous, message="version")

    def test_builds_use_override_then_reset_cached_version_to_version_file(self):
        # Simulate IDF's cached PROJECT_VER at the subprocess boundary. The
        # actual publisher build functions, configuration overlay, signature
        # verification, descriptor checks, and staging writes all run.
        for factory in (False, True):
            with self.subTest(factory=factory):
                cached_version = "0.1.6"
                real_run = subprocess.run
                def idf_build(cmd, *, cwd, check=False, **kwargs):
                    if cmd[0] != "idf.py":
                        return real_run(cmd, cwd=cwd, check=check, **kwargs)
                    nonlocal cached_version
                    self.assertEqual(cmd[0], "idf.py")
                    self.assertEqual(cmd[-1], "build")
                    for arg in cmd:
                        if arg.startswith("-DPROJECT_VER="):
                            cached_version = arg.split("=", 1)[1]
                    bdir = Path(cmd[cmd.index("-B") + 1])
                    (bdir / "sdkconfig").write_text(RELEASE_SDKCONFIG)
                    board = bdir.name.split("_")[-1]
                    data = self.signed["next" if cached_version == "0.1.7" else "release"][board]
                    (bdir / publish.BIN_NAME).write_bytes(data)

                args = ["--signing-key", self.keys / "release.pem"]
                if factory:
                    args.append("--factory")
                with patch.object(publish.subprocess, "run", side_effect=idf_build), \
                        patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                    code, output = self.run_publish(*args, "--version", "0.1.7")
                    self.assertEqual(code, 0, output)
                    stage = self.root / ("dist_factory" if factory else "dist/stable")
                    filename = "serin_link_viewe15_factory.bin" if factory else "link15-app.bin"
                    data = (stage / filename).read_bytes()
                    self.assertEqual(data[0x20000:] if factory else data, self.signed["next"]["viewe15"])
                    code, output = self.run_publish(*args)
                    self.assertEqual(code, 0, output)
                    data = (stage / filename).read_bytes()
                    self.assertEqual(data[0x20000:] if factory else data, self.signed["release"]["viewe15"])

    def test_malformed_descriptor_is_rejected_even_when_signature_is_valid(self):
        stage, previous = self.existing_release()
        cases = {
            "no descriptor magic": self.signed_image(mutate=lambda d: struct.pack_into("<I", d, 32, 0)),
            "not an ESP image": self.signed_image(mutate=lambda d: d.__setitem__(0, 0)),
            "descriptor outside first segment": self.signed_image(mutate=lambda d: struct.pack_into("<I", d, 28, 16)),
            "unterminated version": self.signed_image(version=b"1" * 32),
            "non-ASCII version": self.signed_image(version=b"0.1.6\xff"),
            "empty version": self.signed_image(version=b""),
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(data)
                self.assert_rejected(["--no-build"], stage, previous, message="version")

    def test_version_file_and_override_must_fit_app_descriptor(self):
        stage, previous = self.existing_release()
        for version in ("", "not-a-version", "１.2.3", "1.2." + "3" * 28):
            for override in (False, True):
                with self.subTest(version=version, override=override):
                    (self.root / "version.txt").write_text("0.1.6" if override else version)
                    args = ["--no-build"] + (["--version", version] if override else [])
                    self.assert_rejected(args, stage, previous, message="version")


if __name__ == "__main__":
    unittest.main()
