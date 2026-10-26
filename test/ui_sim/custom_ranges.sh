#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
# Real UI detents, narrow shared labels, CAPS refresh and canonical band rails.
set -euo pipefail
SIM=${SERIN_SIM_BIN:?Set SERIN_SIM_BIN using test/ui_sim/build.sh}
python3 - "$SIM" <<'PY'
import re, subprocess, sys, tempfile
sim = sys.argv[1]
def run(commands):
    with tempfile.TemporaryDirectory() as tmp:
        return subprocess.check_output([sim, '--headless', '--run',
            'bridge 1 1 -50; wait 300; ' + commands], cwd=tmp, text=True)
def values(out, field='set'):
    return [int(re.search(r'\b' + field + r'=(-?\d+)', line)[1])
            for line in out.splitlines() if line.startswith('info ')]
def check(name, got, want):
    global fails
    ok = got == want
    print(f'{"PASS" if ok else "FAIL"} {name}: {got} expected {want}')
    fails += not ok
fails = 0
disp = int(re.search(r'disp=(\d+)', run('hitmap'))[1])
out = run('caps ftab=0 min_dc=100 max_dc=310 step_dc=5; '
          'state use_f=1 set_dd=869; turn 1; info; turn 100; info; turn -100; info')
check('linear half-degree and exact extended stops', values(out), [878,878,500])
out = run('caps ftab=0 min_dc=100 max_dc=305 step_dc=5; '
          'state use_f=1 set_dd=860; turn 1; info; labels')
check('30.5C endpoint', values(out), [869])
check('F hero rounds 86.9F to 87', 'label text=87\n' in out, True)
out = run('caps ftab=0 min_dc=225 max_dc=230 step_dc=5; '
          'state use_f=1 set_dd=725; labels; turn 1; info; labels; turn -1; info')
check('narrow 22.5–23C preserves both positions', values(out), [734,725])
check('both fractional F positions show 73', out.count('label text=73\n') >= 2, True)
out = run('caps ftab=0 min_dc=100 max_dc=310 step_dc=10; '
          'state use_f=1 set_dd=716; turn 1; info; turn 100; info; turn -100; info')
check('linear whole-degree legacy grid', values(out), [734,878,500])
out = run('caps ftab=1 min_dc=160 max_dc=305 step_dc=5; '
          'state use_f=1 set_dd=720; turn 1; info; turn 100; info; turn -100; info')
check('native table detents and stops unchanged', values(out), [730,880,610])
out = run('caps ftab=1 min_dc=160 max_dc=300 step_dc=10; '
          'state use_f=0 set_dd=220; turn 1; info; turn 100; info')
check('C whole-degree grid', values(out), [230,300])
out = run('caps ftab=0 min_dc=100 max_dc=310 step_dc=5; '
          'state use_f=1 set_dd=869; turn 1; '
          'caps max_dc=305; info; labels; turn -1; info')
check('CAPS refresh during active edit reclamps and steps', values(out), [869,860])
check('CAPS refresh repaints current rounded hero', 'label text=87\n' in out, True)
out = run('caps modes=0x7f ftab=0 min_dc=100 max_dc=310 step_dc=5 band_gap=10; '
          'state use_f=1 mode=heat_cool band_valid=1 set_lo=716 set_hi=752; '
          'turn 100; info')
check('linear band gap 1C remains exact', values(out, 'set_lo'), [734])
out = run('caps modes=0x7f ftab=1 min_dc=160 max_dc=305 step_dc=5 band_gap=10; '
          'state use_f=1 mode=heat_cool band_valid=1 set_lo=710 set_hi=750; '
          'turn 100; info')
check('native band gap 1C unchanged', values(out, 'set_lo'), [730])
out = run('caps ftab=1 min_dc=190 max_dc=195 step_dc=5; '
          'state use_f=0 set_dd=190; turn 1; info; turn -1; info; '
          'state set_dd=195; screen advanced; press; turn -1; '
          'screen thermostat; wait 300; state use_f=1 set_dd=670; '
          'turn 1; info; labels; turn -1; info')
check('native collapsed F retains CAPS and C endpoints', values(out), [195,190,670,670])
check('native collapsed F preserves native67 label', 'label text=67\n' in out, True)
check('collapsed F emits no invented temperature edit', out.count('CMD: mode='), 2)
check('units alone emits only units change', 'CMD: units=F' in out, True)
out = run('caps ftab=1 min_dc=190 max_dc=195 step_dc=5; '
          f'state use_f=1 set_dd=670; tap {disp//2} {disp-16}; wait 300; info')
check('collapsed F retains access to fan controls', 'screen=fanvane' in out, True)
# Every detent must reach the next native label on the advertised whole-C grid.
whole = [610,630,650,670,680,690,710,730,750,770,790,810,830,850,870]
for step, positions in [(10, whole), (5, list(range(610,881,10)))]:
    max_dc = 300 if step == 10 else 305
    prefix = f'caps ftab=1 min_dc=160 max_dc={max_dc} step_dc={step}; state use_f=1 set_dd='
    up = run(prefix + str(positions[0]) + '; ' + 'turn 1; info; ' * (len(positions)-1))
    down = run(prefix + str(positions[-1]) + '; ' + 'turn -1; info; ' * (len(positions)-1))
    check(f'native step{step} full upward traversal', values(up), positions[1:])
    check(f'native step{step} full downward traversal', values(down), positions[-2::-1])
out = run('caps modes=0x7f ftab=1 min_dc=160 max_dc=300 step_dc=10 band_gap=10; '
          'state use_f=1 mode=heat_cool band_valid=1 set_lo=730 set_hi=770; '
          'turn -1; info; turn 1; info; turn 100; info; '
          'tap 310 233; turn -100; info; turn 100; info')
check('native whole-grid band low detents/gap', values(out, 'set_lo'), [710,730,750,750,750])
check('native whole-grid band high detents/gap', values(out, 'set_hi'), [770,770,770,770,870])
out = run('caps ftab=1 min_dc=160 max_dc=300 step_dc=10; state use_f=1 set_dd=730; '
          'swipe 440 350 350 233 100; wait 300; info; '
          'swipe 350 233 440 350 100; wait 300; info')
touch = values(out)
check('native whole-grid touch moves down through tied native label', touch[0] < 730, True)
check('native whole-grid touch reverses upward', touch[1] > touch[0], True)
check('native whole-grid touch stays reachable', all(v in whole for v in touch), True)
out = run('caps modes=0x7f ftab=1 min_dc=160 max_dc=300 step_dc=10 band_gap=10; '
          'state use_f=1 mode=heat_cool band_valid=1 set_lo=730 set_hi=770; '
          'swipe 440 350 350 233 100; wait 300; info; '
          'swipe 350 233 440 350 100; wait 300; info')
band_touch = values(out, 'set_lo')
check('native whole-grid band touch moves down', band_touch[0] < 730, True)
check('native whole-grid band touch reverses and respects gap',
      band_touch[0] < band_touch[1] <= 750, True)
check('native whole-grid band touch preserves inactive edge', values(out, 'set_hi'), [770,770])
# CAPS switches mapping after knob/touch edits. The displayed coordinates change,
# but the Celsius intent and inactive band edge must remain the same.
for table, other, old, new in [(1, 0, 690, 698), (0, 1, 698, 690)]:
    out = run(f'caps ftab={table} min_dc=160 max_dc=300 step_dc=5; '
              f'state use_f=1 set_dd={old}; turn 1; turn -1; '
              f'caps ftab={other}; info')
    check(f'table{table} to {other} pending hero', values(out), [new])
    out = run(f'caps modes=0x7f ftab={table} min_dc=160 max_dc=300 step_dc=5 band_gap=20; '
              f'state use_f=1 mode=heat_cool band_valid=1 set_lo={old} '
              f'set_hi={750 if table else 752}; turn 1; turn -1; '
              f'caps ftab={other}; info')
    check(f'table{table} to {other} pending band low', values(out, 'set_lo'), [new])
    check(f'table{table} to {other} pending band high', values(out, 'set_hi'), [752 if table else 750])
    out = run(f'caps ftab={table} min_dc=160 max_dc=300 step_dc=5; '
              f'state use_f=1 set_dd={old}; '
              'swipe 440 350 350 233 100; wait 300; info; '
              f'caps ftab={other}; info; caps ftab={table}; info')
    v = values(out)
    check(f'table{table} touch edit survives mapping round trip', v[2], v[0])
sys.exit(bool(fails))
PY
