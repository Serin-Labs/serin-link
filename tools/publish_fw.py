#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Build every Link board and stage the GitHub release assets for serin-link.

Usage (from the serin-link repo root, with ESP-IDF exported):
    python3 tools/publish_fw.py                        # build all boards -> dist/stable/
    python3 tools/publish_fw.py --boards viewe15       # subset
    python3 tools/publish_fw.py --no-build             # re-stage existing bins
    python3 tools/publish_fw.py --channel beta --version 0.1.7-beta.1
    python3 tools/publish_fw.py --factory              # merged single-file images -> dist_factory/

Release builds use fresh build_pub_<board>/ dirs on the plain chain
(sdkconfig.defaults + board overlay). That chain is unsecured by default, and a
published OTA app has to match the dials you flashed: partition table at 0x8000
(partitions.csv), no NVS encryption, and no perf or dev overlay drift. Nothing
here uploads anything -- it stages files and tells you which release to put them
on, so publishing stays a deliberate act.

Staging is flat because a GitHub release asset name cannot contain '/': each
board's app lands beside manifest.json as <id>-app.bin, and the manifest carries
its size and sha256. The dial resolves a manifest `path` against the directory
of the manifest URL it fetched, so a manifest at
  https://github.com/Serin-Labs/serin-link/releases/latest/download/manifest.json
sends it to .../releases/latest/download/link15-app.bin -- see ota_task() in
main/link_ota.c.

The staged app bin is plaintext, and the dial verifies the download by that
sha256 and by the RSA-3072 app signature it checks before it switches boot
partitions. There is no image encryption: this firmware is public source, so a
shipped decryption key protects nothing (SECURITY.md).

--factory writes one merged raw image per board to dist_factory/, flashable at
offset 0x0. The merge covers the bootloader, the partition table, and the app,
so a board can be programmed with esptool alone and no ESP-IDF toolchain. These
images hold no key and no credential. The ESP-NOW PMK is the public constant in
main/sl2_proto.h, compiled into every build, and trust comes from pairing
instead -- see SECURITY.md. factory-manifest.json records a `dirty` flag, which
is true when HEAD is not an exact tag or the tracked tree has edits, so a
downstream flashing step can refuse a build that is not a release.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO_URL = "https://github.com/Serin-Labs/serin-link"
BOARDS = {
    "viewe15":   "sdkconfig.defaults;sdkconfig.defaults.viewe",
    "viewe21":   "sdkconfig.defaults;sdkconfig.defaults.viewe21",
}
BIN_NAME = "serin_dial.bin"

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
    """Merge the build's flasher_args into one raw image flashable at 0x0."""
    subprocess.run(
        ["idf.py", "-B", str(bdir), "merge-bin", "--format", "raw", "-o", str(out)],
        cwd=ROOT, check=True)
    if not out.exists():
        sys.exit(f"merge-bin produced no output at {out}")

DEV_SIGNING_KEY = ROOT / "secure_boot_signing_key.pem"

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

SIGNKEY_OVERLAY = ROOT / "sdkconfig.signkey.tmp"

def _chain_with_key(defaults: str, signing_key: pathlib.Path) -> str:
    """Append a throwaway overlay that points CONFIG_SECURE_BOOT_SIGNING_KEY at
    `signing_key`. Written fresh each build; untracked, gitignored."""
    SIGNKEY_OVERLAY.write_text(
        f'CONFIG_SECURE_BOOT_SIGNING_KEY="{signing_key}"\n')
    return f"{defaults};{SIGNKEY_OVERLAY.name}"

def build_factory(board: str, defaults: str, signing_key: pathlib.Path) -> pathlib.Path:
    """Fresh factory-chain build for `board`; returns its build dir."""
    bdir = ROOT / f"build_fac_{board}"
    (bdir / "sdkconfig").unlink(missing_ok=True)
    cmd = ["idf.py", "-B", str(bdir),
           f"-DSDKCONFIG={bdir}/sdkconfig",
           f"-DSDKCONFIG_DEFAULTS={_chain_with_key(defaults, signing_key)}"]
    cmd.append("build")
    print(f"== factory build {board} ==", flush=True)
    try:
        subprocess.run(cmd, cwd=ROOT, check=True)
    finally:
        SIGNKEY_OVERLAY.unlink(missing_ok=True)
    return bdir

def build(board: str, defaults: str, signing_key: pathlib.Path,
          version: str | None = None) -> pathlib.Path:
    bdir = ROOT / f"build_pub_{board}"
    (bdir / "sdkconfig").unlink(missing_ok=True)
    cmd = [
        "idf.py", "-B", str(bdir),
        f"-DSDKCONFIG={bdir}/sdkconfig",
        f"-DSDKCONFIG_DEFAULTS={_chain_with_key(defaults, signing_key)}",
    ]
    if version:
        cmd.append(f"-DPROJECT_VER={version}")
    cmd.append("build")
    print(f"== building {board} ==", flush=True)
    try:
        subprocess.run(cmd, cwd=ROOT, check=True)
    finally:
        SIGNKEY_OVERLAY.unlink(missing_ok=True)
    return bdir / BIN_NAME

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--boards", nargs="+", choices=sorted(BOARDS),
                    help="default: all boards")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "dist",
                    help="staging root; assets land in <out>/<channel>/ (default: dist/)")
    ap.add_argument("--factory", action="store_true", help="build merged single-file images (flashable at 0x0) to dist_factory/")
    ap.add_argument("--channel", choices=["stable", "beta"], default="stable",
                    help="beta: stage for the rolling `beta` pre-release (the dial's opt-in "
                         "channel, CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL); manifest gains channel=beta")
    ap.add_argument("--version", dest="version_override",
                    help="manifest + app-descriptor version, e.g. 0.1.7-beta.1 (must be "
                         "maj.min.patch[-pre.N]; required with --channel beta)")
    ap.add_argument("--signing-key", type=pathlib.Path,
                    help="production signing key; overrides the repo-root dev key "
                         "and forbids dev-key signatures in the staged bins")
    args = ap.parse_args()

    signing_key = args.signing_key.expanduser().resolve() if args.signing_key else DEV_SIGNING_KEY
    if args.signing_key and not signing_key.exists():
        sys.exit(f"--signing-key {signing_key}: no such file")
    if not args.signing_key:
        print("!! DEV-KEY BUILD — signed with the repo dev key; NOT for release "
              "(pass --signing-key <production key>)", flush=True)

    version = (ROOT / "version.txt").read_text().strip()
    if args.version_override:
        if not re.fullmatch(r"\d+\.\d+\.\d+(-[A-Za-z]+\.\d+)?", args.version_override):
            sys.exit(f"--version {args.version_override}: want maj.min.patch or maj.min.patch-pre.N "
                     "(what link_ota's ota_ver_cmp can order)")
        version = args.version_override
    if args.channel == "beta" and "-" not in version:
        sys.exit("--channel beta needs a pre-release --version (e.g. 0.1.7-beta.1): a plain "
                 "triple on the beta channel would outrank the stable release it shadows")
    if args.channel == "stable" and "-" in version:
        sys.exit(f"{version} is a pre-release; publish it with --channel beta")
    if args.factory and args.channel != "stable":
        sys.exit("--factory has no channel")

    boards = args.boards or sorted(BOARDS)

    if args.factory:
        out_dir = ROOT / "dist_factory"
        out_dir.mkdir(exist_ok=True)
        builds = []
        for board in boards:
            bdir = build_factory(board, BOARDS[board], signing_key)
            merged = out_dir / f"serin_dial_{board}_factory.bin"
            merge_factory(bdir, merged)
            data = merged.read_bytes()
            builds.append({
                "board": board,
                "path": f"serin_dial_{board}_factory.bin",
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            })
            print(f"{board}: factory {len(data)} B  {builds[-1]['sha256'][:12]}…")
        (out_dir / "factory-manifest.json").write_text(
            json.dumps({"name": "Serin Dial (factory)", "version": version,
                        "dirty": _tree_dirty(), "builds": builds}, indent=2) + "\n")
        print(f"staged {out_dir} (version {version}) — merged factory images, "
              f"flashable at 0x0 with esptool")
        return 0

    # One dir per channel: both channels use the same flat asset names, and they
    # go to two different releases.
    stage = args.out / args.channel
    stage.mkdir(parents=True, exist_ok=True)

    builds, assets = [], []
    for board in boards:
        pid = PUBLIC_IDS[board]
        binary = (ROOT / f"build_pub_{board}" / BIN_NAME) if args.no_build \
                 else build(board, BOARDS[board], signing_key, args.version_override)
        if not binary.exists():
            sys.exit(f"missing {binary}; build it first or drop --no-build")
        assert_factory_chain(ROOT / f"build_pub_{board}")
        data = binary.read_bytes()
        asset = f"{pid}-app.bin"
        shutil.copyfile(binary, stage / asset)
        assets.append(asset)
        builds.append({
            "board": pid,
            "path": asset,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
        print(f"{board}: {len(data)} B  {builds[-1]['sha256'][:12]}…")

    # A bin left by an earlier run with a different --boards is not in this
    # manifest; drop it so everything in the dir is uploadable as-is.
    for old in sorted(stage.glob("*-app.bin")):
        if old.name not in assets:
            old.unlink()
            print(f"removed stale {old.name}")

    tag = "beta" if args.channel == "beta" else f"v{version}"
    manifest = {
        "name": "Serin Link",
        "version": version,
        "release_url": f"{REPO_URL}/releases/tag/{tag}",
        "builds": builds,
    }
    if args.channel == "beta":
        manifest["channel"] = "beta"
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"staged {stage} (version {version}, channel {args.channel})")
    for name in ["manifest.json", *assets]:
        print(f"  {name}")
    if args.channel == "beta":
        print(f"next: upload those as assets on the rolling `beta` release "
              f"({REPO_URL}/releases/tag/beta), replacing the assets of the same name "
              f"already on it, and keep it flagged as a pre-release so it never becomes "
              f"`latest`. That serves {REPO_URL}/releases/download/beta/manifest.json, "
              f"which is what a dial set to `ota-channel beta` fetches.")
    else:
        print(f"next: tag {tag}, then upload those as assets on the {tag} release "
              f"({REPO_URL}/releases/tag/{tag}) and publish it. That serves "
              f"{REPO_URL}/releases/latest/download/manifest.json, which is what a dial "
              f"fetches by default.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
