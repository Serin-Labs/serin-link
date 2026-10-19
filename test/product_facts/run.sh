#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)

grep -Fq 'project(serin_link)' "$root/CMakeLists.txt"
grep -Fq 'BIN_NAME = "serin_link.bin"' "$root/tools/publish_fw.py"
grep -Fq '"name": "Serin Link (factory)"' "$root/tools/publish_fw.py"
grep -Fq 'serin_link_{board}_factory.bin' "$root/tools/publish_fw.py"
grep -Fq 'SHT41 sensor' "$root/README.md"
grep -Fq 'SHT41 sensor' "$root/docs/HARDWARE.md"

if grep -Fq 'Serin Dial' \
    "$root/README.md" \
    "$root/docs/HARDWARE.md" \
    "$root/docs/FLASHING.md" \
    "$root/tools/publish_fw.py"; then
    echo 'retired Serin Dial name remains in public source' >&2
    exit 1
fi

if grep -Fq 'serin_dial' "$root/tools/publish_fw.py"; then
    echo 'retired serin_dial identity remains in release tooling' >&2
    exit 1
fi
