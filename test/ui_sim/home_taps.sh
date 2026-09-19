#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Home screen side taps: left 30% steps the setpoint down, right 30% steps it
# up, the middle keeps opening the Menu.
set -euo pipefail
: "${SERIN_SIM_BIN:?Set SERIN_SIM_BIN to the simulator executable}"
python3 - "$SERIN_SIM_BIN" <<'PY'
import os, subprocess, sys, tempfile
binary=os.path.abspath(sys.argv[1])
def run(commands):
    with tempfile.TemporaryDirectory() as work:
        p=subprocess.run([binary,'--headless','--run','wait 1800; '+commands],cwd=work,text=True,capture_output=True,timeout=30)
        assert p.returncode==0, p.stdout+p.stderr
        return p.stdout
checks=[]
def check(name, fn):
    try: fn(); print('PASS '+name)
    except AssertionError as e: checks.append(name); print('FAIL '+name+': '+str(e))
def expect(commands,*texts):
    output=run(commands)
    for text in texts: assert text in output, 'missing '+repr(text)+'\n'+output
live='bridge 1 1 -50; wait 60; state set=720; wait 60; '
# The zones are the outer 30% of the width: x<edge and x>=disp-edge.
import re
disp=int(re.search(r'disp=(\d+)', run('hitmap')).group(1))
edge=disp*30//100
R=disp-24   # clear of the band number targets, which reach to about disp-45
check('Left tap steps setpoint down',
      lambda: expect(live+'tap 60 240; wait 600; info', 'set=710', 'screen=thermostat'))
check('Right tap steps setpoint up',
      lambda: expect(live+f'tap {R} 240; wait 600; info', 'set=730', 'screen=thermostat'))
check('Two right taps step twice',
      lambda: expect(live+f'tap {R} 240; wait 600; tap {R} 240; wait 600; info', 'set=740'))
check('Tap just inside the zone boundary counts',
      lambda: expect(live+f'tap {edge-3} 300; wait 600; info', 'set=710'))
check('Tap just outside the zone boundary opens Menu',
      lambda: expect(live+f'tap {edge+3} 300; wait 600; info', 'set=720', 'screen=menu'))
check('Middle tap opens Menu unchanged',
      lambda: expect(live+f'tap {disp//2} {disp//2}; wait 600; info', 'set=720', 'screen=menu'))
check('Fan button in bottom middle still opens Airflow',
      lambda: expect(live+f'tap {disp//2} {disp-16}; wait 600; info', 'set=720', 'screen=fanvane'))
check('Band mode: side tap adjusts selected edge',
      lambda: expect(live+'state mode=heat_cool set_lo=680 set_hi=740 band_valid=1; wait 200;'
                     f' tap {R} 240; wait 600; info', 'edge=lo', 'set_lo=690', 'set_hi=740'))
check('Band mode: number tap selects, side tap then adjusts hi',
      lambda: expect(live+'state mode=heat_cool set_lo=680 set_hi=740 band_valid=1; wait 200;'
                     f' tap {disp//2+100} 240; wait 300; tap 30 240; wait 600; info', 'edge=hi', 'set_lo=680', 'set_hi=730'))
check('Child Lock refuses side taps',
      lambda: expect(live+'screen advanced; turn 1; press; turn 1; press; wait 100; screen thermostat; wait 500;'
                     ' tap 60 240; wait 600; info', 'lock=1', 'set=720'))
if checks: sys.exit('FAILED: '+', '.join(checks))
PY
