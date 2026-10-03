#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Build every Link board and stage signed images for the public distribution.

Usage (from the serin-link repo root, with ESP-IDF exported):
    python3 tools/publish_fw.py --signing-key /path/to/production-private.pem
    python3 tools/publish_fw.py --signing-key /path/to/production-private.pem --boards viewe15
    python3 tools/publish_fw.py --no-build             # re-stage existing bins
    python3 tools/publish_fw.py --no-build --distribution ../serin-cn105
    python3 tools/publish_fw.py --signing-key /path/to/production-private.pem --channel beta --version 0.1.7-beta.1
    python3 tools/publish_fw.py --signing-key /path/to/production-private.pem --factory
    python3 tools/publish_fw.py --signing-key /path/to/production-private.pem --factory --channel beta --version 0.1.9-beta.1
    python3 tools/publish_fw.py --factory --split-existing ~/serin-cn105/firmware/link

Release builds use fresh build_pub_<board>/ dirs on the plain chain
(sdkconfig.defaults + board overlay). That chain is unsecured by default, and a
published OTA app has to match the dials you flashed: partition table at 0x8000
(partitions.csv), no NVS encryption, and no perf or dev overlay drift. Nothing
here uploads anything. --distribution validates a complete candidate checkout
before replacing only firmware/link/ota/<channel>/ in that local checkout.

Each board's app lands beside manifest.json as <id>-app.bin, and the manifest
carries its size and sha256. The dial resolves each path relative to the
manifest URL under the public Serin-Labs/serin-cn105 repo. Stable and beta use
separate link/ota/ directories. Retired encrypted feeds are not valid release
inputs; the complete candidate distribution must use the current manifest contract.

The staged app bin is plaintext, and the dial verifies the download by that
sha256 and by the RSA-3072 app signature it checks before it switches boot
partitions. The source repository's visibility does not control downloads:
production update files are hosted separately in the public distribution repo.

--factory writes one merged raw image per board to dist_factory/, flashable at
offset 0x0. The merge covers the bootloader, the partition table, and the app,
so a board can be programmed with esptool alone and no ESP-IDF toolchain. These
images hold no key and no credential. The ESP-NOW PMK is the public constant in
main/sl2_proto.h, compiled into every build, and trust comes from pairing
instead -- see the "Link protocol" section of README.md.
Beside each merged image, --factory writes the same bytes as split parts
(bootloader, partition table, blank otadata, app) and lists them under `parts`
in factory-manifest.json. The merged image pads across the nvs partition at
0x9000, so flashing it resets the dial's settings; the parts leave nvs alone,
which is how the web flasher reinstalls without losing pairings. The parts are
sliced from the merged image, never built separately, so the two cannot differ.
--split-existing adds parts to an already published release without a rebuild.
--channel beta stages pre-release factory images to dist_factory/beta/, for
firmware/link/factory/beta/ in the distribution repo.
factory-manifest.json records a `dirty` flag, which
is true when HEAD is not an exact tag or the tracked tree has edits, so a
downstream flashing step can refuse a build that is not a release.
"""
import argparse
import contextlib
import hashlib
import io
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import NamedTuple

try:
    import espsecure
    from esptool import FatalError
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
except ImportError:
    sys.exit("Missing release tooling; run: python -m pip install -r tools/requirements-release.txt")

ROOT = pathlib.Path(__file__).resolve().parent.parent
DISTRIBUTION_BASE = "https://raw.githubusercontent.com/Serin-Labs/serin-cn105/main/firmware/link/ota"
BOARDS = {
    "viewe15":   "sdkconfig.defaults;sdkconfig.defaults.viewe",
    "viewe21":   "sdkconfig.defaults;sdkconfig.defaults.viewe21",
}
BIN_NAME = "serin_link.bin"

# Board -> the id the dial matches against manifest `builds[].board`
# (LINK_BOARD_ID in main/link_ota.c). Changing one orphans every dial already
# running that board's firmware.
PUBLIC_IDS = {"viewe15": "link15", "viewe21": "link21"}

def _tree_dirty() -> bool:
    """True if tracked files have uncommitted changes or HEAD is not exactly on a
    tag — stamped into factory-manifest.json so a downstream flashing step can
    refuse to treat a dirty/untagged build as a release. Untracked files are
    ignored (matches `git describe --dirty` semantics). Fails safe (True) on any
    git error."""
    def _git(*a):
        return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True)
    try:
        if _git("status", "--porcelain", "--untracked-files=no").stdout.strip():
            return True
        return _git("describe", "--tags", "--exact-match").returncode != 0
    except Exception:
        return True

def merge_factory(bdir: pathlib.Path, out: pathlib.Path) -> None:
    """Merge existing flash inputs, without IDF's implicit build dependency."""
    subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
         "--format", "raw", "-o", str(out.resolve()), "@flash_args"],
        cwd=bdir, check=True)
    if not out.exists():
        sys.exit(f"merge-bin produced no output at {out}")

# Flash regions a factory image may write, from partitions.csv: bootloader,
# partition table, otadata, ota_0. nvs (0x9000) and phy_init (0x11000) lie
# between them and must be 0xFF in the merged image.
FACTORY_OTADATA = (0xf000, 0x2000)
FACTORY_PART_TABLE = (0x8000, 0xc00)

def split_factory(data: bytes, board: str) -> list[tuple[str, int, bytes]]:
    """Slice a merged factory image into (name, offset, bytes) flash parts.

    otadata is written in full and blank on purpose: a dial that has taken an
    OTA boots ota_1, and blank otadata is what returns the bootloader to the
    ota_0 this install just wrote.
    """
    boot = data[:FACTORY_PART_TABLE[0]].rstrip(b"\xff")
    boot += b"\xff" * (-len(boot) % 4)
    table_at, table_len = FACTORY_PART_TABLE
    ota_at, ota_len = FACTORY_OTADATA
    parts = [(f"serin_link_{board}_bootloader.bin", 0, boot),
             (f"serin_link_{board}_partition-table.bin", table_at, data[table_at:table_at + table_len]),
             (f"serin_link_{board}_otadata.bin", ota_at, b"\xff" * ota_len),
             (f"serin_link_{board}_app.bin", FACTORY_APP_OFFSET, data[FACTORY_APP_OFFSET:])]
    rebuilt = bytearray(b"\xff" * len(data))
    for name, offset, chunk in parts:
        if not chunk.strip(b"\xff") and offset != ota_at:
            sys.exit(f"{name}: the merged image has nothing at {offset:#x}")
        rebuilt[offset:offset + len(chunk)] = chunk
    if bytes(rebuilt) != data:
        sys.exit(f"{board}: merged image writes flash outside the bootloader, partition table, "
                 "blank otadata and ota_0; split parts would not install the same bytes")
    return parts

def factory_entry(board: str, asset: str, data: bytes, artifacts: dict[str, bytes]) -> dict:
    """Manifest build entry for a verified merged image; adds it and its parts to `artifacts`."""
    artifacts[asset] = data
    parts = []
    for name, offset, chunk in split_factory(data, board):
        artifacts[name] = chunk
        parts.append({"path": name, "offset": offset, "sha256": hashlib.sha256(chunk).hexdigest()})
    return {"board": board, "path": asset, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "parts": parts}

DEV_SIGNING_KEY = ROOT / "secure_boot_signing_key.pem"
RELEASE_PUBLIC_KEY = ROOT / "tools/keys/serin-link-release.pub"
# ota_0 in partitions.csv; a merged factory image starts at flash offset zero.
FACTORY_APP_OFFSET = 0x20000

# Secure Boot v2 signature sector at the end of a signed app: one 1216-byte
# block per signature, the rest of the 4 KB sector 0xFF. The key digest covers
# the block's RSA-3072 key material (n, e, R^2 mod n, M').
# https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/security/secure-boot-v2.html#signature-block-format
SIG_SECTOR = 4096
SIG_BLOCK_LEN = 1216
SIG_BLOCK_MAGIC = b"\xe7\x02"  # magic byte, then block version 2
SIG_BLOCK_KEY = slice(36, 812)

# esp_app_desc_t sits right after the 24-byte image header and the 8-byte
# first segment header, whose last word is that segment's length.
IMAGE_MAGIC = 0xE9
FIRST_SEGMENT_LEN = slice(28, 32)
APP_DESC_OFFSET = 24 + 8
APP_DESC_LEN = 256
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_VERSION = slice(APP_DESC_OFFSET + 16, APP_DESC_OFFSET + 48)  # char version[32]

class TrustedKey(NamedTuple):
    """The public key every staged app must be signed by, and IDF's digest of it."""
    pem: bytes
    digest: bytes

def verification_key(signing_key: pathlib.Path | None, development: bool) -> TrustedKey:
    """Validate any supplied private key and return the key staged apps must match.

    Production trust comes only from the checked-in public key, including when
    no private key is available for --no-build. Never infer trust from an image.
    """
    try:
        public = None if development else serialization.load_pem_public_key(RELEASE_PUBLIC_KEY.read_bytes())
        if signing_key is not None:
            private = serialization.load_pem_private_key(signing_key.read_bytes(), password=None)
            if not isinstance(private, rsa.RSAPrivateKey) or private.key_size != 3072:
                sys.exit(f"{signing_key}: signing key must be an unencrypted RSA-3072 private key")
            if development:
                public = private.public_key()
            elif private.public_key().public_numbers() != public.public_numbers():
                sys.exit(f"{signing_key}: signing key does not match the pinned production public key")
        if not isinstance(public, rsa.RSAPublicKey) or public.key_size != 3072:
            sys.exit("Verification key must be an RSA-3072 public key")
        pem = public.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
        # Use Espressif's encoding, including the RSA hardware coefficients,
        # to match the public-key digest the device checks on OTA updates.
        with tempfile.TemporaryDirectory(prefix="serin-key-check-") as tmp:
            keyfile = pathlib.Path(tmp) / "public.pem"
            digestfile = pathlib.Path(tmp) / "digest.bin"
            keyfile.write_bytes(pem)
            with keyfile.open("rb") as key, contextlib.redirect_stdout(io.StringIO()):
                espsecure.digest_sbv2_public_key(argparse.Namespace(keyfile=key, output=str(digestfile)))
            return TrustedKey(pem, digestfile.read_bytes())
    except (OSError, ValueError, TypeError, FatalError) as error:
        sys.exit(f"Cannot validate signing/verification key: {error}")

def verify_signed_app(data: bytes, key: TrustedKey, label: str) -> None:
    """Verify the exact bytes to stage against the device's single-key policy."""
    try:
        if len(data) < 2 * SIG_SECTOR or len(data) % SIG_SECTOR:
            raise ValueError("missing or truncated signature sector")
        sector = data[-SIG_SECTOR:]
        # IDF's CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT verifies only
        # block zero. espsecure accepts any valid block, so constrain it first.
        if sector[:2] != SIG_BLOCK_MAGIC or sector[SIG_BLOCK_LEN:] != b"\xff" * (SIG_SECTOR - SIG_BLOCK_LEN):
            raise ValueError("expected one RSA signature in block zero")
        if hashlib.sha256(sector[SIG_BLOCK_KEY]).digest() != key.digest:
            raise ValueError("embedded key does not match the trusted signing key")
        with contextlib.redirect_stdout(io.StringIO()):
            espsecure.verify_signature_v2(argparse.Namespace(
                keyfile=io.BytesIO(key.pem), datafile=io.BytesIO(data), hsm=False))
    except (ValueError, FatalError) as error:
        sys.exit(f"{label}: signature verification failed: {error}")

def assert_app_version(data: bytes, expected: str, label: str) -> None:
    """Check the descriptor in a verified signed app, never a build sidecar.

    https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/system/app_image_format.html
    """
    try:
        payload = data[:-SIG_SECTOR]  # Exclude the already verified signature sector.
        if (len(payload) < APP_DESC_OFFSET + APP_DESC_LEN or payload[0] != IMAGE_MAGIC
                or not 1 <= payload[1] <= 16):
            raise ValueError("missing ESP application header/descriptor")
        segment_size = int.from_bytes(payload[FIRST_SEGMENT_LEN], "little")
        if not APP_DESC_LEN <= segment_size <= len(payload) - APP_DESC_OFFSET:
            raise ValueError("application descriptor is outside the first segment")
        if int.from_bytes(payload[APP_DESC_OFFSET:APP_DESC_OFFSET + 4], "little") != APP_DESC_MAGIC:
            raise ValueError("invalid application descriptor magic")
        raw, terminator, _ = payload[APP_DESC_VERSION].partition(b"\0")
        if not raw or not terminator:
            raise ValueError("version must be nonempty and null-terminated")
        actual = raw.decode("ascii")
        if actual != expected:
            raise ValueError(f"embedded version {actual!r} does not match release version {expected!r}; "
                             "rebuild or select the matching --version")
    except ValueError as error:
        sys.exit(f"{label}: app version verification failed: {error}")

def assert_factory_chain(bdir: pathlib.Path) -> None:
    """Refuse a build dir that isn't on the release chain — a stale sdkconfig
    from before that became the default. The partition-table offset is compiled
    into the app, so a bin built at another offset would boot on a dial flashed
    from partitions.csv and look for the table at the wrong place in flash."""
    cfg = bdir / "sdkconfig"
    try:
        text = cfg.read_text()
    except OSError:
        sys.exit(f"{cfg} missing — cannot prove the bin's config chain; "
                 f"re-run without --no-build")
    for want in ("CONFIG_PARTITION_TABLE_OFFSET=0x8000",
                 "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y"):
        if want not in text:
            sys.exit(f"{cfg} lacks {want} — stale build dir from before the "
                     f"release chain; re-run without --no-build")

def assert_distribution_urls(bdir: pathlib.Path) -> None:
    """Refuse a production image whose updater would not poll the public feeds.

    A build dir configured before the feeds moved still signs and verifies
    cleanly; only its sdkconfig shows that the dials would never see an update.
    """
    config = (bdir / "sdkconfig").read_text().splitlines()
    for channel, setting in (("stable", "SERIN_LINK_OTA_MANIFEST_URL"),
                             ("beta", "SERIN_LINK_OTA_MANIFEST_BETA_URL")):
        expected = f'CONFIG_{setting}="{DISTRIBUTION_BASE}/{channel}/manifest.json"'
        if expected not in config:
            sys.exit(f"{bdir}: {channel} manifest URL differs from the public feed; "
                     "rebuild without --no-build")

def replace_artifacts(out_dir: pathlib.Path, artifacts: dict[str, bytes], pattern: str) -> None:
    """Write the verified artifacts, then drop any other `pattern` file.

    A file left by an earlier run with different --boards is not in this
    manifest; removing it keeps everything in the dir publishable as-is.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (out_dir / name).write_bytes(data)
    for old in sorted(out_dir.glob(pattern)):
        if old.name not in artifacts:
            old.unlink()
            print(f"removed stale {old.name}")

SIGNKEY_OVERLAY = ROOT / "sdkconfig.signkey.tmp"

def _chain_with_key(defaults: str, signing_key: pathlib.Path) -> str:
    """Append a throwaway overlay that points CONFIG_SECURE_BOOT_SIGNING_KEY at
    `signing_key`. Written fresh each build; untracked, gitignored."""
    SIGNKEY_OVERLAY.write_text(
        f'CONFIG_SECURE_BOOT_SIGNING_KEY="{signing_key}"\n')
    return f"{defaults};{SIGNKEY_OVERLAY.name}"

def build_factory(board: str, defaults: str, signing_key: pathlib.Path, version: str) -> pathlib.Path:
    """Fresh factory-chain build for `board`; returns its build dir."""
    bdir = ROOT / f"build_fac_{board}"
    (bdir / "sdkconfig").unlink(missing_ok=True)
    cmd = ["idf.py", "-B", str(bdir),
           f"-DSDKCONFIG={bdir}/sdkconfig",
           f"-DSDKCONFIG_DEFAULTS={_chain_with_key(defaults, signing_key)}",
           f"-DPROJECT_VER={version}"]
    cmd.append("build")
    print(f"== factory build {board} ==", flush=True)
    try:
        subprocess.run(cmd, cwd=ROOT, check=True)
    finally:
        SIGNKEY_OVERLAY.unlink(missing_ok=True)
    return bdir

def build(board: str, defaults: str, signing_key: pathlib.Path,
          version: str) -> pathlib.Path:
    bdir = ROOT / f"build_pub_{board}"
    (bdir / "sdkconfig").unlink(missing_ok=True)
    cmd = [
        "idf.py", "-B", str(bdir),
        f"-DSDKCONFIG={bdir}/sdkconfig",
        f"-DSDKCONFIG_DEFAULTS={_chain_with_key(defaults, signing_key)}",
        f"-DPROJECT_VER={version}",
    ]
    cmd.append("build")
    print(f"== building {board} ==", flush=True)
    try:
        subprocess.run(cmd, cwd=ROOT, check=True)
    finally:
        SIGNKEY_OVERLAY.unlink(missing_ok=True)
    return bdir / BIN_NAME

def stage_distribution(distribution: pathlib.Path, stage: pathlib.Path, channel: str) -> None:
    """Validate a complete candidate tree, then replace only its plaintext channel.

    The other OTA channel, factory images and other products remain unchanged.
    This edits a local checkout; it never commits or pushes anything.
    """
    distribution = distribution.expanduser().resolve()
    destination = distribution / "firmware/link/ota" / channel
    if any(path.is_symlink() for path in (destination, *destination.parents)
           if path != distribution and path.is_relative_to(distribution)):
        sys.exit("Distribution OTA path must not contain symlinks")
    if not (distribution / "scripts/requirements-validation.txt").is_file():
        sys.exit("Distribution validation tools are missing; update the serin-cn105 checkout first")
    try:
        with tempfile.TemporaryDirectory(prefix=".ota-stage-", dir=distribution) as tmp:
            candidate = pathlib.Path(tmp)
            shutil.copytree(distribution / "scripts", candidate / "scripts", symlinks=True)
            shutil.copytree(distribution / "firmware", candidate / "firmware", symlinks=True)
            channel_dir = candidate / "firmware/link/ota" / channel
            if channel_dir.exists():
                shutil.rmtree(channel_dir)
            channel_dir.mkdir(parents=True)
            # Copy exactly the files produced by this invocation, not unrelated
            # files someone may have left in the local staging directory.
            manifest = json.loads((stage / "manifest.json").read_text())
            for name in ["manifest.json", *[build["path"] for build in manifest["builds"]]]:
                shutil.copyfile(stage / name, channel_dir / name)
            result = subprocess.run([sys.executable, "-B", str(candidate / "scripts/validate-manifests.py")],
                                    capture_output=True, text=True)
            if result.returncode:
                sys.exit(f"Distribution validation failed:\n{result.stdout}{result.stderr}")
            print(result.stdout, end="")
            destination.parent.mkdir(parents=True, exist_ok=True)
            previous = candidate / "previous-channel"
            if destination.exists():
                destination.rename(previous)
            try:
                channel_dir.rename(destination)
            except OSError:
                if previous.exists():
                    previous.rename(destination)
                raise
    except OSError as error:
        sys.exit(f"Cannot stage distribution: {error}")
    print(f"validated and staged {destination}; review, then validate again immediately before pushing")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--boards", nargs="+", choices=sorted(BOARDS),
                    help="default: all boards")
    ap.add_argument("--no-build", action="store_true", help="verify and stage existing build outputs")
    ap.add_argument("--out", type=pathlib.Path,
                    help="OTA staging root (default: dist/), or factory output dir (default: dist_factory/)")
    ap.add_argument("--distribution", type=pathlib.Path,
                    help="also stage a complete production OTA channel into a local serin-cn105 checkout; "
                         "validate the full candidate distribution before replacing files; never push")
    ap.add_argument("--factory", action="store_true", help="build merged single-file images (flashable at 0x0) plus "
                         "their split parts to dist_factory/ (dist_factory/beta/ with --channel beta)")
    ap.add_argument("--split-existing", type=pathlib.Path, metavar="DIR",
                    help="with --factory: take the verified merged images and factory-manifest.json already in DIR "
                         "instead of building, and stage them unchanged with split parts added")
    ap.add_argument("--channel", choices=["stable", "beta"], default="stable",
                    help="beta: stage for the public beta feed (the dial's opt-in "
                         "channel, CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL); manifest gains channel=beta")
    ap.add_argument("--version", dest="version_override",
                    help="release version (default: version.txt); must match every app descriptor, "
                         "including --no-build; format maj.min.patch[-pre.N], at most 31 ASCII characters")
    ap.add_argument("--signing-key", type=pathlib.Path,
                    help="RSA-3072 private key matching the pinned production key; "
                         "required for builds, optional for --no-build")
    ap.add_argument("--development", action="store_true",
                    help="use your own signing key (default: repo dev key); requires a separate --out")
    args = ap.parse_args()

    if args.distribution and (args.factory or args.development or
                              set(args.boards or BOARDS) != set(BOARDS)):
        sys.exit("--distribution requires production OTA images for both boards; "
                 "it cannot be combined with --factory or --development")
    if args.split_existing and not args.factory:
        sys.exit("--split-existing needs --factory")
    factory_out = ROOT / "dist_factory" / ("beta" if args.channel == "beta" else "")
    out = (args.out or (factory_out if args.factory else ROOT / "dist")).expanduser().resolve()
    if args.distribution:
        distribution = args.distribution.expanduser().resolve()
        if out.is_relative_to(distribution) or distribution.is_relative_to(out):
            sys.exit("--out and --distribution must be separate directories")
    signing_key = args.signing_key.expanduser().resolve() if args.signing_key else None
    if args.development:
        production_dirs = [(ROOT / name).resolve() for name in ("dist", "dist_factory")]
        if args.out is None or any(out.is_relative_to(path) or path.is_relative_to(out)
                                   for path in production_dirs):
            sys.exit("--development requires --out outside dist/ and dist_factory/")
        signing_key = signing_key or DEV_SIGNING_KEY
        print("DEVELOPMENT staging: for devices using your own key; not a Serin production release.")
    elif not args.no_build and not args.split_existing and signing_key is None:
        sys.exit("Production builds require --signing-key <production private key>; "
                 "use --development --out <separate directory> for your own key")
    key = verification_key(signing_key, args.development)

    existing = None
    if args.split_existing:
        source = args.split_existing.expanduser().resolve()
        if source == out:
            sys.exit("--split-existing DIR and the output directory must differ")
        existing = json.loads((source / "factory-manifest.json").read_text())
        if args.version_override not in (None, existing.get("version")):
            sys.exit(f"--version differs from the release in {source}")
        args.version_override = existing.get("version") or ""
        if "-" in args.version_override:
            args.channel = "beta"
            if args.out is None:
                out = (ROOT / "dist_factory/beta").resolve()
    version = args.version_override if args.version_override is not None \
              else (ROOT / "version.txt").read_text().strip()
    if len(version) > 31 or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z]+\.[0-9]+)?", version):
        sys.exit(f"Invalid release version {version!r}: want maj.min.patch or maj.min.patch-pre.N, "
                 "at most 31 ASCII characters (the app descriptor limit)")
    if args.channel == "beta" and "-" not in version:
        sys.exit("--channel beta needs a pre-release --version (e.g. 0.1.7-beta.1): a plain "
                 "triple on the beta channel would outrank the stable release it shadows")
    if args.channel == "stable" and "-" in version:
        sys.exit(f"{version} is a pre-release; publish it with --channel beta")

    boards = args.boards or sorted(BOARDS)

    if args.factory:
        out_dir = out
        builds = []
        artifacts = {}
        for board in boards:
            asset = f"serin_link_{board}_factory.bin"
            if existing is not None:
                entry = next((b for b in existing.get("builds", []) if b.get("board") == board), None)
                if entry is None or entry.get("path") != asset:
                    sys.exit(f"{source}/factory-manifest.json has no {asset} build for {board}")
                data = (source / asset).read_bytes()
                if hashlib.sha256(data).hexdigest() != entry.get("sha256"):
                    sys.exit(f"{source / asset}: sha256 differs from its manifest")
            else:
                bdir = ROOT / f"build_fac_{board}" if args.no_build \
                       else build_factory(board, BOARDS[board], signing_key, version)
                assert_factory_chain(bdir)
                if not args.development:
                    assert_distribution_urls(bdir)
                with tempfile.TemporaryDirectory(prefix="serin-factory-check-") as tmp:
                    merged = pathlib.Path(tmp) / asset
                    merge_factory(bdir, merged)
                    data = merged.read_bytes()
            verify_signed_app(data[FACTORY_APP_OFFSET:], key, asset)
            assert_app_version(data[FACTORY_APP_OFFSET:], version, asset)
            builds.append(factory_entry(board, asset, data, artifacts))
            print(f"{board}: factory {len(data)} B  {builds[-1]['sha256'][:12]}…")
        # Validate every board before replacing any previous release artifact.
        replace_artifacts(out_dir, artifacts, "serin_link_*.bin")
        # A republished release keeps its recorded provenance: the tree this
        # runs from says nothing about the tree those images were built from.
        dirty = bool(existing.get("dirty", True)) if existing is not None else _tree_dirty()
        manifest = {"name": "Serin Link (factory)", "version": version}
        if args.channel == "beta":
            manifest["channel"] = "beta"
        manifest.update({"dirty": dirty, "builds": builds})
        (out_dir / "factory-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"staged {out_dir} (version {version}) — merged factory images, "
              f"flashable at 0x0 with esptool, and their split parts")
        return 0

    # Each public channel uses the same flat asset names in its own directory.
    stage = out / args.channel

    builds = []
    artifacts = {}
    for board in boards:
        pid = PUBLIC_IDS[board]
        binary = (ROOT / f"build_pub_{board}" / BIN_NAME) if args.no_build \
                 else build(board, BOARDS[board], signing_key, version)
        if not binary.exists():
            sys.exit(f"missing {binary}; build it first or drop --no-build")
        assert_factory_chain(ROOT / f"build_pub_{board}")
        if not args.development:
            assert_distribution_urls(ROOT / f"build_pub_{board}")
        data = binary.read_bytes()
        verify_signed_app(data, key, str(binary))
        assert_app_version(data, version, str(binary))
        asset = f"{pid}-app.bin"
        artifacts[asset] = data
        builds.append({
            "board": pid,
            "path": asset,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
        print(f"{board}: {len(data)} B  {builds[-1]['sha256'][:12]}…")

    replace_artifacts(stage, artifacts, "*-app.bin")

    manifest = {
        "name": "Serin Link",
        "version": version,
        "channel": args.channel,
        "builds": builds,
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"staged {stage} (version {version}, channel {args.channel})")
    for name in ["manifest.json", *artifacts]:
        print(f"  {name}")
    if args.development:
        print("Serve these files from your own update origin for devices using this signing key.")
        return 0
    if args.distribution:
        stage_distribution(args.distribution, stage, args.channel)
    else:
        print("next: use --distribution <serin-cn105 checkout> to validate and stage the public OTA feed")
    print(f"public feed after publication: {DISTRIBUTION_BASE}/{args.channel}/manifest.json")
    return 0

if __name__ == "__main__":
    sys.exit(main())
