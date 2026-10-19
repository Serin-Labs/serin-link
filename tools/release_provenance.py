#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Local release receipts: source/config/output consistency, not an attestation.

Records contain identifiers and hashes only. Never serialize command lines,
configuration contents, key names, environment values, or absolute paths.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

RECEIPT = "release-receipt.json"
SCHEMA = 1


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _git(root: Path, *args: str) -> bytes:
    try:
        result = subprocess.run(["git", *args], cwd=root, capture_output=True, check=False)
    except OSError:
        raise SystemExit("Cannot establish release provenance: Git unavailable") from None
    if result.returncode:
        # Git stderr can contain local paths. Keep it out of public records/logs.
        raise SystemExit("Cannot establish release provenance: Git command failed")
    return result.stdout


def source_state(root: Path, version: str, development: bool) -> dict:
    """Hash tracked contents/modes and nonignored untracked files; fail closed.

    Ignored generated files outside main/components/cmake are excluded.
    Submodules and source symlinks need separate target/dependency capture,
    so this bounded contract refuses them.
    """
    # Git discovers ancestor repositories from source exports/subdirectories.
    # An empty prefix identifies the checkout root, including linked worktrees.
    if _git(root, "rev-parse", "--show-prefix") != b"\n":
        raise SystemExit("Cannot establish release provenance: source directory must be the Git worktree root")
    revision = _git(root, "rev-parse", "--verify", "HEAD").decode().strip()
    tags = _git(root, "tag", "--points-at", "HEAD").decode().splitlines()
    tag = f"v{version}" if f"v{version}" in tags else None
    dirty = bool(_git(root, "status", "--porcelain=v1", "--untracked-files=all"))
    if not development and dirty:
        raise SystemExit("Production release requires a clean source tree, including nonignored untracked files")
    if not development and tag is None:
        raise SystemExit(f"Production release requires tag v{version} at HEAD (selected version must match the release tag)")
    ignored_inputs = _git(root, "ls-files", "--others", "--ignored", "--exclude-standard", "-z",
                          "--", "main", "components", "cmake").split(b"\0")
    if not development and any(ignored_inputs):
        raise SystemExit("Production release refuses ignored untracked source in main/, components/ or cmake/")
    # Read HEAD objects directly: status and the index can hide working edits
    # through assume-unchanged/skip-worktree flags or core.fileMode settings.
    files = _git(root, "ls-tree", "-r", "-z", "HEAD").split(b"\0")
    if development:
        # A staged addition is neither in HEAD nor in ls-files --others.
        names = {entry.split(b"\t", 1)[1] for entry in files if entry}
        for entry in _git(root, "ls-files", "--stage", "-z").split(b"\0"):
            if not entry:
                continue
            metadata, name = entry.split(b"\t", 1)
            mode, object_id, stage = metadata.split()
            if stage != b"0":
                raise SystemExit("Cannot establish release provenance for unmerged source")
            if name not in names:
                files.append(mode + b" blob " + object_id + b"\t" + name)
        dirty = dirty or any(ignored_inputs)
    entries = []
    try:
        for entry in files:
            if not entry:
                continue
            metadata, name = entry.split(b"\t", 1)
            mode, object_type, object_id = metadata.split()
            if object_type != b"blob":
                raise SystemExit("Cannot establish release provenance for submodules")
            path = root / os.fsdecode(name)
            if path.is_symlink():
                value = os.fsencode(os.readlink(path))
                kind = "symlink"
            elif path.is_file():
                value = path.read_bytes()
                kind = "executable" if path.stat().st_mode & 0o111 else "file"
            else:
                value, kind = b"", "missing"
            if not development:
                algorithm = "sha1" if len(object_id) == 40 else "sha256"
                actual_id = hashlib.new(algorithm, b"blob " + str(len(value)).encode() + b"\0" + value).hexdigest()
                actual_mode = {"symlink": b"120000", "executable": b"100755", "file": b"100644"}.get(kind)
                if actual_id.encode() != object_id or actual_mode != mode:
                    raise SystemExit("Production source differs from tagged HEAD contents/modes; clean source required")
            if kind == "symlink":
                raise SystemExit("Source symlinks need a separately captured target; unsupported")
            entries.append([os.fsdecode(name), kind, digest(value)])
        untracked = _git(root, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0")
        for name in [*untracked, *ignored_inputs]:
            if name:
                path = root / os.fsdecode(name)
                if path.is_symlink():
                    raise SystemExit("Source symlinks need a separately captured target; unsupported")
                value = path.read_bytes()
                entries.append([os.fsdecode(name), "untracked", digest(value)])
    except OSError:
        raise SystemExit("Cannot read source to establish release provenance") from None
    return {"revision": revision, "tag": tag, "dirty": dirty,
            "snapshot_sha256": digest(json.dumps(sorted(entries), ensure_ascii=True).encode())}


def assert_source_unchanged(root: Path, version: str, development: bool, source: dict) -> None:
    if source_state(root, version, development) != source:
        raise SystemExit("Source changed during release staging; rebuild from the intended source")


def factory_inputs(bdir: Path) -> str:
    """Bind merge options and every flash input, including bootloader/table/data.

    The digest includes the flash_args contents but records no paths. IDF's
    response file consists of option/value pairs followed by offset/file pairs.
    """
    try:
        arguments = (bdir / "flash_args").read_bytes()
        tokens = shlex.split(arguments.decode())
        inputs = []
        index = 0
        while index < len(tokens):
            token = tokens[index]
            if token.startswith("--"):
                # IDF merge options all take a value, except these switches.
                index += 1 if token in ("--compress", "--no-compress") or "=" in token else 2
                continue
            offset = int(token, 0)
            path = (bdir / tokens[index + 1]).resolve()
            if not path.is_relative_to(bdir.resolve()):
                raise ValueError("flash input outside build directory")
            inputs.append([offset, digest(path.read_bytes())])
            index += 2
        if not inputs:
            raise ValueError("no flash inputs")
    except (OSError, ValueError, IndexError, UnicodeError):
        raise SystemExit("Cannot establish factory build provenance from flash_args and its inputs") from None
    return digest(json.dumps([digest(arguments), inputs]).encode())


def build_record(source: dict, board: str, kind: str, version: str, development: bool,
                 defaults: str, config: bytes, artifact: bytes, inputs_sha256: str) -> dict:
    body = {"schema": SCHEMA, "source": source, "board": board, "kind": kind,
            "version": version, "development": development,
            "defaults": defaults.split(";"), "sdkconfig_sha256": digest(config),
            "artifact_sha256": digest(artifact), "build_inputs_sha256": inputs_sha256}
    return {**body, "build_sha256": digest(json.dumps(body, sort_keys=True).encode())}


def validate_record(record: object, board: str, kind: str, version: str,
                    development: bool, artifact: bytes) -> dict:
    """Strict schema: never copy arbitrary archive metadata into public output."""
    fail = "Missing or invalid build provenance; rebuild (legacy artifacts have no release receipt)"
    if not isinstance(record, dict):
        raise SystemExit(fail)
    fields = {"schema", "source", "board", "kind", "version", "development", "defaults",
              "sdkconfig_sha256", "artifact_sha256", "build_inputs_sha256", "build_sha256"}
    source = record.get("source")
    if set(record) != fields or not isinstance(source, dict) or set(source) != {
            "revision", "tag", "dirty", "snapshot_sha256"}:
        raise SystemExit(fail)
    def sha(value):
        return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value)
    if (type(record["schema"]) is not int or record["schema"] != SCHEMA
            or record["board"] != board or record["kind"] != kind
            or record["version"] != version or record["development"] is not development
            or type(source["dirty"]) is not bool
            or not isinstance(source["revision"], str)
            or not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", source["revision"])
            or source["tag"] not in (None, f"v{version}")
            or (not development and (source["dirty"] or source["tag"] != f"v{version}"))
            or not sha(source["snapshot_sha256"])
            or not sha(record["sdkconfig_sha256"])
            or not sha(record["build_inputs_sha256"])
            or not isinstance(record["defaults"], list)
            or not record["defaults"]
            or any(not isinstance(name, str) or not re.fullmatch(r"sdkconfig\.defaults(?:\.[a-zA-Z0-9_-]+)?", name)
                   for name in record["defaults"])):
        raise SystemExit(fail)
    body = {key: value for key, value in record.items() if key != "build_sha256"}
    if (record["artifact_sha256"] != digest(artifact)
            or record["build_sha256"] != digest(json.dumps(body, sort_keys=True).encode())):
        raise SystemExit("Build provenance artifact/hash mismatch; rebuild instead of relabeling cached output")
    return record


def verify_receipt(bdir: Path, expected: dict, artifact: bytes) -> dict:
    try:
        record = json.loads((bdir / RECEIPT).read_text())
    except (OSError, ValueError):
        raise SystemExit("Missing or unreadable build provenance receipt; rerun without --no-build") from None
    validate_record(record, expected["board"], expected["kind"], expected["version"],
                    expected["development"], artifact)
    if record != expected:
        raise SystemExit("Build provenance source/config mismatch; rerun without --no-build")
    return record
