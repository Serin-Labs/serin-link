#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch checksum-pinned LVGL sources without installing ESP-IDF."""
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request

VERSION = "9.5.0"
REVISION = "85aa60d18b3d5e5588d7b247abf90198f07c8a63"
SHA256 = "b32747cf6568d86a5cb9d30f53970bec1c7a322cb624c4158d4b0e348324146b"
URL = f"https://codeload.github.com/lvgl/lvgl/tar.gz/{REVISION}"
NOTICES = ("LICENCE.txt", "COPYRIGHTS.md")


def verify_lock(lock):
    match = re.search(r"^  lvgl/lvgl:\n(?:(?!^  \S).|\n)*?^    version: ['\"]?([^'\"\s]+)",
                      lock.read_text(), re.MULTILINE)
    if not match or match[1] != VERSION:
        raise ValueError(f"simulator requires dependencies.lock lvgl/lvgl {VERSION}; update its source pin and checksum together")


def unpack(archive, destination):
    with archive.open("rb") as content:
        checksum = hashlib.file_digest(content, "sha256").hexdigest()
    if checksum != SHA256:
        raise ValueError(f"LVGL archive checksum mismatch: remove {archive} and retry")
    # Keep only the library, top-level headers and notices, not upstream media,
    # examples, tools or tests. Never extract links or archive-supplied paths.
    with tarfile.open(archive) as source:
        for member in source:
            parts = PurePosixPath(member.name).parts
            if not parts or parts[0] != f"lvgl-{REVISION}" or ".." in parts:
                raise ValueError("unexpected LVGL archive path")
            relative = Path(*parts[1:])
            wanted = len(parts) > 1 and (parts[1] == "src" or
                       len(parts) == 2 and (parts[1].endswith(".h") or parts[1] in NOTICES))
            if wanted and member.isfile():
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as content, target.open("wb") as out:
                    shutil.copyfileobj(content, out)


def prepare(root, cache):
    verify_lock(root / "dependencies.lock")
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / f"lvgl-{VERSION}-{SHA256[:12]}"
    marker = target / ".source-sha256"
    if (marker.is_file() and marker.read_text().strip() == SHA256
            and all((target / name).is_file() for name in NOTICES)):
        return target.resolve()
    archive = cache / f"lvgl-{VERSION}.tar.gz"
    if not archive.is_file():
        print(f"Downloading LVGL {VERSION} (checksum pinned)", file=sys.stderr)
        with tempfile.NamedTemporaryFile(dir=cache, delete=False) as download:
            temporary = Path(download.name)
            try:
                with urllib.request.urlopen(URL, timeout=120) as response:
                    shutil.copyfileobj(response, download)
            except Exception:
                temporary.unlink(missing_ok=True)
                raise
        temporary.replace(archive)
    with tempfile.TemporaryDirectory(dir=cache) as work:
        unpack(archive, Path(work))
        (Path(work) / ".source-sha256").write_text(SHA256 + "\n")
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(work, target)
    return target.resolve()


if __name__ == "__main__":
    try:
        print(prepare(Path(__file__).resolve().parent.parent, Path(sys.argv[1])))
    except (OSError, ValueError, tarfile.TarError) as error:
        sys.exit(f"LVGL prerequisite: {error}")
