#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# First-run coach: the side-tap step follows the turn step, absorbs side taps
# into the demo ring, and advances once both sides have been tapped.
set -euo pipefail
: "${SERIN_SIM_BIN:?Set SERIN_SIM_BIN to the simulator executable}"
python3 - "$SERIN_SIM_BIN" <<'PY'
import os, re, subprocess, sys, tempfile
binary=os.path.abspath(sys.argv[1])
def run(commands):
    with tempfile.TemporaryDirectory() as work:
        p=subprocess.run([binary,'--headless','--run','wait 1800; '+commands],cwd=work,text=True,capture_output=True,timeout=40)
        assert p.returncode==0, p.stdout+p.stderr
        return p.stdout
checks=[]
def check(name, fn):
    try: fn(); print('PASS '+name)
    except AssertionError as e: checks.append(name); print('FAIL '+name+': '+str(e))
def expect(commands,*texts):
    output=run(commands)
    for text in texts: assert text in output, 'missing '+repr(text)+'\n'+output
disp=int(re.search(r'disp=(\d+)', run('hitmap')).group(1))
R=disp-24
# Live face, then re-arm the coach; it presents about 1 s after input goes quiet.
live='bridge 1 1 -50; wait 60; state set=720; wait 60; coach arm; wait 2500; '
# Turn step completes on one cw + one ccw click, then 1.5 s settle + crossfade.
turned=live+'turn 1; wait 100; turn -1; wait 2200; '
check('Coach presents on the live face',
      lambda: expect(live+'info', 'coach=1', 'screen=thermostat'))
check('Side tap during Turn step leaves the real setpoint alone',
      lambda: expect(live+'tap 60 240; wait 600; info', 'coach=1', 'set=720', 'screen=thermostat'))
check('Turn step advances into the Tap step and side taps stay sandboxed',
      lambda: expect(turned+f'tap {R} 240; wait 600; info', 'coach=1', 'set=720', 'screen=thermostat'))
check('One side only does not finish the Tap step',
      lambda: expect(turned+f'tap {R} 240; wait 2200; tap {R} 240; wait 600; info', 'coach=1', 'set=720'))
check('Left and right taps finish the Tap step (Mode step follows, still on home)',
      lambda: expect(turned+f'tap {R} 240; wait 300; tap 60 240; wait 2200; info', 'coach=1', 'set=720', 'screen=thermostat'))
check('Middle tap during the Tap step still opens the Menu',
      lambda: expect(turned+f'tap {disp//2} {disp//2}; wait 600; info', 'screen=menu', 'set=720'))
if checks: sys.exit('FAILED: '+', '.join(checks))
PY
