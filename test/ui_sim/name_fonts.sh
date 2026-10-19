#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Real labels must retain complete UTF-8 names and render supported Latin glyphs.
set -euo pipefail
: "${SERIN_SIM_BIN:?set SERIN_SIM_BIN to the serin-sim binary}"
python3 - <<'PY'
import os
from pathlib import Path
import re
import subprocess
import tempfile

sim = str(Path(os.environ['SERIN_SIM_BIN']).resolve())
errors = []

def check(commands, expected, message):
    with tempfile.TemporaryDirectory() as cwd:
        result = subprocess.run([sim, '--headless', '--run', 'wait 1800; ' + commands + '; wait 500; labels; fontcheck'],
                                cwd=cwd, capture_output=True, timeout=30)
    try:
        log = result.stdout.decode('utf-8')
    except UnicodeDecodeError:
        errors.append(message + ': label contains incomplete UTF-8')
        return
    if result.returncode or not re.search(r'^fontcheck missing=0 checked=[1-9][0-9]*$', log, re.M):
        errors.append(message + ': glyph coverage failed\n' + log + result.stderr.decode('utf-8', errors='replace'))
    if not re.search(r'text=' + re.escape(expected) + r'$', log, re.M):
        errors.append(message + ': expected complete name ' + repr(expected) + '\n' + log)

# Latin-1 plus Extended-A: German, Spanish, French, Polish, Turkish and Czech.
for name in ['Küche', 'Niñez', 'Séjour', 'Łódź', 'İğdır', 'Český', 'Ærø', 'Œuvre']:
    check(f'zones 2; zone 0 name={name}; screen zones', name, 'zone picker ' + name)
    check(f'roomsrc_ids 10 10:0:Sala,20:1:{name}; screen room_source; turn 1', name,
          'room-source picker ' + name)
# Exercise every claimed non-ASCII code point through a real picker label.
# Ten two-byte characters fit inside the smaller controller name field.
repertoire = ''.join(chr(cp) for cp in range(0xA0, 0x17F) if cp not in (0xAD, 0x149))
for offset in range(0, len(repertoire), 10):
    name = repertoire[offset:offset + 10]
    check(f'roomsrc_ids 10 10:1:{name}; screen room_source', name,
          'Latin repertoire ' + hex(ord(name[0])))
# The home chip and Settings details use smaller faces than the picker titles.
check('zones 2; zone 0 name=Küche; bridge 1 1 -42; screen thermostat', 'Küche', 'home zone chip')
check('zones 2; zone 0 name=Łódź; screen settings; turn 2', 'Łódź', 'Controller settings')
check('caps features=65535; roomsrc 0 Niñez; screen settings; turn 4', 'Niñez', 'Room Sensor settings')

# Existing 32/24-byte buffers reserve a NUL. A two-byte accent either fits in
# full, or its cut-off leading byte must be discarded, preserving the prefix.
for field, limit in [('zone', 31), ('room', 23)]:
    for name, expected in [('A' * limit, 'A' * limit),
                           ('A' * (limit - 2) + 'ü', 'A' * (limit - 2) + 'ü'),
                           ('A' * (limit - 1) + 'ü', 'A' * (limit - 1))]:
        commands = (f'zones 2; zone 0 name={name}; screen zones' if field == 'zone'
                    else f'roomsrc_ids 10 10:1:{name}; screen room_source')
        check(commands, expected, field + ' byte boundary ' + repr(name))
        if field == 'room':
            check(f'caps features=65535; roomsrc 0 {name}; screen settings; turn 4',
                  expected, 'applied source byte boundary ' + repr(name))

# Reusing a label after accented text must restore its ASCII layout metrics.
for commands in [
    'zones 2; zone 0 name=Living; screen zones; wait 500; labelbox Living; '
    'zone 0 name=ŰĞČą; wait 500; zone 0 name=Living; wait 500; labelbox Living; fontcheck',
    'roomsrc_ids 10 10:1:Living; screen room_source; wait 500; labelbox Living; '
    'roomsrc_ids 10 10:1:ŰĞČą; wait 500; roomsrc_ids 10 10:1:Living; wait 500; labelbox Living; fontcheck',
]:
    with tempfile.TemporaryDirectory() as cwd:
        result = subprocess.run([sim, '--headless', '--run', commands], cwd=cwd,
                                capture_output=True, text=True, timeout=30)
    boxes = re.findall(r'^labelbox text=Living box=(.*)$', result.stdout, re.M)
    if result.returncode or len(boxes) != 2 or boxes[0] != boxes[1]:
        errors.append('ASCII name geometry changed after an accented name\n' + result.stdout)

# Unsupported characters remain explicit failures. Both command entry points
# must return a failing process status and stop before the next command.
commands = 'zones 2; zone 0 name=Room中; screen zones; wait 500; fontcheck; info'
for mode in ['--run', '--script']:
    with tempfile.TemporaryDirectory() as cwd:
        argument = commands
        if mode == '--script':
            script = Path(cwd) / 'missing-glyph.sim'
            script.write_text(commands.replace('; ', '\n') + '\n')
            argument = str(script)
        result = subprocess.run([sim, '--headless', mode, argument], cwd=cwd,
                                capture_output=True, text=True, timeout=30)
    if not result.returncode or 'cp=U+4E2D' not in result.stdout or re.search(r'^info ', result.stdout, re.M):
        errors.append(mode + ': missing glyph did not fail and stop the command runner\n' + result.stdout)

if errors:
    raise SystemExit('\n'.join('FAIL: ' + error for error in errors))
print('PASS name-fonts')
PY
