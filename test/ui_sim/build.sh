#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build the simulator against THIS checkout in a separate directory.
set -euo pipefail
: "${SERIN_SIM_SOURCE:?Set SERIN_SIM_SOURCE to the simulator sim directory}"
board=${1:-viewe15}
case "$board" in viewe15|viewe21) ;; *) echo 'Board must be viewe15 or viewe21' >&2; exit 2;; esac
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${SERIN_SIM_BUILD_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/serin-ui-sim.XXXXXX")}
mkdir -p "$build"
build=$(cd "$build" && pwd)
source_dir=$(cd "$SERIN_SIM_SOURCE" && pwd)
mkdir -p "$build/source"
cp "$source_dir"/*.c "$source_dir"/*.h "$source_dir/Makefile" "$build/source/"
cp -R "$source_dir/stubs" "$build/source/"
python3 - "$build/source/sim_cmd.c" "$root/test/ui_sim/catalog_command.inc" <<'PY'
from pathlib import Path
import sys
path=Path(sys.argv[1]); text=path.read_text()
marker='    if (!strcmp(cmd, "roomsrc")) {'
if marker not in text:
    raise SystemExit('Unsupported simulator: roomsrc command insertion point missing')
support=Path(sys.argv[2]).parent
text=text.replace(marker, (support/'wifi_command.inc').read_text()+'\n'+Path(sys.argv[2]).read_text()+'\n'+marker, 1)
text=text.replace('static int s_wifi_setup_reqs = 0;', (support/'wifi_callbacks.inc').read_text()+'\nstatic int s_wifi_setup_reqs = 0;', 1)
path.write_text(text)
main=path.parent/'sim_main.c'
text=main.read_text()
text=text.replace('    ui_set_on_wifi_setup(sim_cmd_on_wifi_setup);', '''    ui_set_on_wifi_setup(sim_cmd_on_wifi_setup);
    extern void sim_wifi_session_start(uint32_t), sim_wifi_cancel(uint32_t);
    ui_set_on_wifi_setup_session(sim_wifi_session_start);
    ui_set_on_wifi_cancel(sim_wifi_cancel);''')
main.write_text(text)
PY
make -C "$build/source" -j"${SERIN_SIM_JOBS:-8}" ROOT="$root" BOARD="$board" BUILD="$build/$board" >&2
printf '%s\n' "$build/$board/serin-sim"
