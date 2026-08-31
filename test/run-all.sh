#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -uo pipefail
cd "$(dirname "$0")"
fail=0 n=0 ok=0
for d in */; do
    [ -x "${d}run.sh" ] || continue
    n=$((n + 1))
    if out=$("${d}run.sh" 2>&1); then
        printf 'PASS %s\n' "${d%/}"
        ok=$((ok + 1))
    else
        printf 'FAIL %s\n%s\n' "${d%/}" "$out"
        fail=1
    fi
done
echo "----"
echo "$ok/$n passed"
exit $fail
