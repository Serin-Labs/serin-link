#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
set -uo pipefail
cd "$(dirname "$0")"
fail=0 n=0 ok=0 skip=0
for d in */; do
    [ -x "${d}run.sh" ] || continue
    n=$((n + 1))
    out=$("${d}run.sh" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        printf 'PASS %s\n' "${d%/}"
        ok=$((ok + 1))
    elif [ $rc -eq 77 ]; then
        # 77 = prerequisites missing on this machine (the automake convention).
        printf 'SKIP %s: %s\n' "${d%/}" "$out"
        skip=$((skip + 1))
    else
        printf 'FAIL %s\n%s\n' "${d%/}" "$out"
        fail=1
    fi
done
echo "----"
echo "$ok/$n passed, $skip skipped"
exit $fail
