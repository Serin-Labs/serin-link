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
    f'CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL="{FEED}/beta/manifest.json"\n')
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
            cls.signed[name] = cls.signed_image(key=name)
        cls.signed["next"] = cls.signed_image(version=b"0.1.7")
        cls.signed["beta"] = cls.signed_image(version=b"0.1.7-beta.1")

    @classmethod
    def signed_image(cls, version=b"0.1.6", key="release", mutate=None):
        # esp_app_desc_t is 256 bytes, with version[32] at offset 16. Build
        # the surrounding ESP32-S3 image with esptool, independently of the
        # publisher's descriptor reader, and sign with a disposable key.
        desc = struct.pack("<IIII32s32s16s16s32s32s80x", 0xABCD5432, 0, 0, 0,
                           version, b"serin_link", b"12:00:00", b"Sep 18 2026",
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
                (bdir / publish.BIN_NAME).write_bytes(self.signed["release"])

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
        out.write_bytes(b"\xff" * 0x20000 + (bdir / publish.BIN_NAME).read_bytes())

    def test_no_build_verifies_without_private_key_and_hashes_exact_staged_bytes(self):
        code, output = self.run_publish("--no-build")
        self.assertEqual(code, 0, output)
        stage = self.root / "dist/stable"
        manifest = json.loads((stage / "manifest.json").read_text())
        self.assertEqual([entry["board"] for entry in manifest["builds"]], ["link15", "link21"])
        for entry in manifest["builds"]:
            self.assertEqual((stage / entry["path"]).read_bytes(), self.signed["release"])
            self.assertEqual(entry["sha256"], hashlib.sha256(self.signed["release"]).hexdigest())

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
        archive = []
        for channel, version in (("stable", "0.1.6"), ("beta", "0.1.7-beta.1")):
            folder = distribution / "firmware/link"
            if channel == "beta":
                folder /= "beta"
            (folder / "link15").mkdir(parents=True)
            ciphertext = f"archived encrypted {channel} release".encode()
            (folder / "link15/app_secure.bin").write_bytes(ciphertext)
            build = {"board": "link15", "path": "link15/app_secure.bin", "size": 8192,
                     "enc_size": len(ciphertext), "sha256": "0" * 64}
            (folder / "manifest.json").write_text(json.dumps({
                "name": "Legacy Link", "version": version, "channel": channel, "builds": [build]}))
            archive.append({"version": version, "build": build,
                            "ciphertext_sha256": hashlib.sha256(ciphertext).hexdigest()})
        (distribution / "scripts/legacy-link-artifacts.json").write_text(json.dumps(archive))
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

    def test_public_distribution_preserves_encrypted_feeds_and_other_products(self):
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
            self.assertEqual((stage / build["path"]).read_bytes(), self.signed["release"])
        after = self.distribution_files(distribution)
        self.assertEqual({name: after[name] for name in previous}, previous)

    def test_builtin_urls_resolve_to_staged_channels_without_cross_channel_changes(self):
        distribution = self.make_distribution()
        kconfig = (REPO / "main/Kconfig.projbuild").read_text()
        for channel, version, image in (("stable", "0.1.6", "release"),
                                        ("beta", "0.1.7-beta.1", "beta"),
                                        ("stable", "0.1.7", "next")):
            with self.subTest(channel=channel, version=version):
                before = self.distribution_files(distribution)
                for board in ("viewe15", "viewe21"):
                    (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed[image])
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
                    self.assertEqual(data, self.signed[image])
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
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["next"])
        code, output = self.run_publish("--no-build", "--version", "0.1.7", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertIn("sha256 does not match", output)
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

    def test_development_images_may_use_their_own_feeds(self):
        own = RELEASE_SDKCONFIG.replace(FEED, "https://updates.example/link")
        for kind in ("pub", "fac"):
            for board in ("viewe15", "viewe21"):
                (self.root / f"build_{kind}_{board}/sdkconfig").write_text(own)
                (self.root / f"build_{kind}_{board}" / publish.BIN_NAME).write_bytes(self.signed["other"])
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
        (distribution / "firmware/link/ota").symlink_to(distribution / "firmware/link", target_is_directory=True)
        code, output = self.run_publish("--no-build", "--distribution", distribution)
        self.assertNotEqual(code, 0, output)
        self.assertIn("symlink", output)
        (distribution / "firmware/link/ota").unlink()
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
        (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(self.signed["other"])
        stage, previous = self.existing_release()
        with patch.object(publish, "build", side_effect=lambda board, *_:
                          self.root / f"build_pub_{board}" / publish.BIN_NAME):
            self.assert_rejected(["--signing-key", self.keys / "release.pem"], stage, previous)

    def test_invalid_second_board_preserves_entire_previous_release(self):
        valid = self.signed["release"]
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
        cases = {"wrong signer": self.signed["other"], "unsigned": valid[:-4096],
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
            out.write_bytes(b"\xff" * 0x20000 + self.signed["other"])

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
            self.assertEqual(data[0x20000:], self.signed["release"])
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())

    def test_factory_no_build_merges_without_idf_or_private_key(self):
        bdir = self.root / "build_fac_viewe15"
        (bdir / "flash_args").write_text(
            "--flash_mode keep --flash_freq keep --flash_size keep\n"
            "0x20000 serin_link.bin\n")
        # Python and esptool remain available through sys.executable, while
        # idf.py, the compiler, and production private keys are unavailable.
        with patch.dict(os.environ, {"PATH": str(self.root / "empty-path")}):
            try:
                code, output = self.run_publish("--factory", "--no-build", "--boards", "viewe15")
            except FileNotFoundError as error:
                self.fail(f"--no-build unexpectedly needs a build tool: {error}")
        self.assertEqual(code, 0, output)
        data = (self.root / "dist_factory/serin_link_viewe15_factory.bin").read_bytes()
        self.assertEqual(data[0x20000:], self.signed["release"])

    def test_factory_build_with_matching_key_stages_verified_images(self):
        with patch.object(publish, "build_factory", side_effect=lambda board, *_:
                          self.root / f"build_fac_{board}"), \
                patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--signing-key", self.keys / "release.pem")
        self.assertEqual(code, 0, output)
        self.assertTrue((self.root / "dist_factory/factory-manifest.json").is_file())

    def test_factory_invalid_second_board_preserves_previous_release(self):
        (self.root / "build_fac_viewe21" / publish.BIN_NAME).write_bytes(self.signed["other"])
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
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["other"])
        out = self.root / "self-hosted"
        code, output = self.run_publish("--no-build", "--development", "--out", out)
        self.assertEqual(code, 0, output)
        stage = out / "stable"
        previous = {p.name: p.read_bytes() for p in stage.iterdir()}
        (self.root / "build_pub_viewe21" / publish.BIN_NAME).write_bytes(self.signed["release"])
        self.assert_rejected(["--no-build", "--development", "--out", out], stage, previous)

    def test_development_factory_uses_separate_output_and_own_key(self):
        (self.root / "build_fac_viewe15" / publish.BIN_NAME).write_bytes(self.signed["other"])
        with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
            code, output = self.run_publish("--factory", "--no-build", "--boards", "viewe15",
                                            "--development", "--out", self.root / "own-factory")
        self.assertEqual(code, 0, output)
        self.assertEqual((self.root / "own-factory/serin_link_viewe15_factory.bin").read_bytes()[0x20000:],
                         self.signed["other"])
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

    def test_second_board_version_mismatch_preserves_previous_release(self):
        for factory, kind in ((False, "pub"), (True, "fac")):
            with self.subTest(factory=factory):
                (self.root / f"build_{kind}_viewe21" / publish.BIN_NAME).write_bytes(self.signed["next"])
                stage, previous = self.existing_release(factory=factory)
                args = ["--no-build"] + (["--factory"] if factory else [])
                with patch.object(publish, "merge_factory", side_effect=self.merge_fixture):
                    self.assert_rejected(args, stage, previous, message="version")

    def test_matching_beta_version_is_staged(self):
        for board in ("viewe15", "viewe21"):
            (self.root / f"build_pub_{board}" / publish.BIN_NAME).write_bytes(self.signed["beta"])
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
            out.write_bytes(b"\xff" * 0x20000 + self.signed["next"])
        with patch.object(publish, "merge_factory", side_effect=merge_wrong_version):
            self.assert_rejected(["--no-build", "--factory"], stage, previous, message="version")

    def test_builds_use_override_then_reset_cached_version_to_version_file(self):
        # Simulate IDF's cached PROJECT_VER at the subprocess boundary. The
        # actual publisher build functions, configuration overlay, signature
        # verification, descriptor checks, and staging writes all run.
        for factory in (False, True):
            with self.subTest(factory=factory):
                cached_version = "0.1.6"
                def idf_build(cmd, *, cwd, check):
                    nonlocal cached_version
                    self.assertEqual(cmd[0], "idf.py")
                    self.assertEqual(cmd[-1], "build")
                    for arg in cmd:
                        if arg.startswith("-DPROJECT_VER="):
                            cached_version = arg.split("=", 1)[1]
                    bdir = Path(cmd[cmd.index("-B") + 1])
                    (bdir / "sdkconfig").write_text(RELEASE_SDKCONFIG)
                    data = self.signed["next" if cached_version == "0.1.7" else "release"]
                    (bdir / publish.BIN_NAME).write_bytes(data)

                args = ["--signing-key", self.keys / "release.pem"]
                if factory:
                    args.append("--factory")
                with patch.object(publish.subprocess, "run", side_effect=idf_build), \
                        patch.object(publish, "merge_factory", side_effect=self.merge_fixture), \
                        patch.object(publish, "_tree_dirty", return_value=True):
                    code, output = self.run_publish(*args, "--version", "0.1.7")
                    self.assertEqual(code, 0, output)
                    stage = self.root / ("dist_factory" if factory else "dist/stable")
                    filename = "serin_link_viewe15_factory.bin" if factory else "link15-app.bin"
                    data = (stage / filename).read_bytes()
                    self.assertEqual(data[0x20000:] if factory else data, self.signed["next"])
                    code, output = self.run_publish(*args)
                    self.assertEqual(code, 0, output)
                    data = (stage / filename).read_bytes()
                    self.assertEqual(data[0x20000:] if factory else data, self.signed["release"])

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
