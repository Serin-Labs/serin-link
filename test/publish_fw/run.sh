#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -euo pipefail
cd "$(dirname "$0")"
# The only host test that needs more than gcc. Outside CI, a machine without
# the release tooling skips it (exit 77, reported by run-all.sh); CI installs
# the tooling, so a miss there is a failure.
if ! python3 -c 'import cryptography, espsecure, esptool' 2>/dev/null; then
    msg='needs tools/requirements-release.txt (or an exported ESP-IDF) in python3'
    if [ -n "${CI:-}" ]; then echo "$msg" >&2; exit 1; fi
    echo "$msg"
    exit 77
fi
python3 -B test_publish_fw.py
