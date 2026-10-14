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
# A units flip converts the setpoints before any STATE echo arrives, so the
# next step moves from the converted value instead of clamping to a rail.
# The default Fahrenheit table maps 72 F to 22.5 C.
flip_knob=live+'screen advanced; press; turn 1; press; long; wait 300; '
adv=run(live+'screen advanced; wait 200; hitmap')
cx,cy=map(int,re.search(r'hit x=(\d+) y=(\d+) [^\n]*text=\u00b0C\n',adv).groups())
flip_tap=live+f'screen advanced; wait 200; tap {cx} {cy}; wait 300; long; wait 300; '
check('Knob units flip converts the setpoint',
      lambda: expect(flip_knob+'info', 'CMD: units=C', 'set=225', 'units=C', 'screen=thermostat'))
check('Step after a knob units flip sends the converted value',
      lambda: expect(flip_knob+f'tap {R} 240; wait 600; info', 'CMD: mode=HEAT set=230 ', 'set=230'))
check('Step after a touch units flip sends the converted value',
      lambda: expect(flip_tap+'turn 1; wait 600; info', 'CMD: units=C', 'CMD: mode=HEAT set=230 ', 'set=230'))
check('Units flip and back restores the setpoint',
      lambda: expect(live+'screen advanced; press; turn 1; turn -1; press; long; wait 300; info',
                     'CMD: units=F', 'set=720', 'units=F'))
check('Units flip converts both band edges',
      lambda: expect(live+'state mode=heat_cool set_lo=680 set_hi=740 band_valid=1; wait 200; '
                     'screen advanced; press; turn 1; press; long; wait 300; info',
                     'set_lo=200', 'set_hi=235', 'units=C'))
# Wiggling the Units option from Celsius flips to Fahrenheit and straight
# back. Converting twice would round: with table 1, 19.5 and 20.5 C have no
# exact Fahrenheit twin; with table 0, 22.5 C becomes 73 F, then 22.8 C.
celsius=('bridge 1 1 -50; wait 60; state use_f=0 set={set} set_lo=195 set_hi=205'
         ' band_valid=1 mode={mode}; wait 60; ')
wiggle='screen advanced; press; turn -1; turn 1; press; long; wait 300; '
for ftab,value in ((1,195),(1,205),(0,225)):
    check(f'Celsius flip and back keeps {value} with table {ftab}',
          lambda f=ftab,v=value: expect(f'caps ftab={f}; '+celsius.format(set=v,mode='heat')+wiggle+'info',
                                        'CMD: units=F','CMD: units=C',f'set={v} ','units=C'))
check('Step after a Celsius flip and back sends the unrounded value',
      lambda: expect(celsius.format(set=195,mode='heat')+wiggle+f'tap {R} 240; wait 600; info',
                     'CMD: mode=HEAT set=200 ','set=200 '))
check('Celsius flip and back keeps both band edges',
      lambda: expect(celsius.format(set=200,mode='heat_cool')+wiggle+'info',
                     'set_lo=195 ','set_hi=205 ','units=C'))
check('Repeated Celsius flips stay stable in both units',
      lambda: expect(celsius.format(set=195,mode='heat')+'screen advanced; press; turn -1; turn 1; turn -1; wait 100; info; turn 1; press; long; wait 300; info',
                     'set=670 ','set=195 '))
# A flip in one zone must not restore that zone's value in another zone.
# Zone 2 happens to hold 67 F, the value 19.5 C became in zone 1; flipping
# back converts it to 19.0 C instead of restoring zone 1's 19.5 C.
check('Units flip back in another zone converts its own value',
      lambda: expect('zones 3 0; '+celsius.format(set=195,mode='heat')+'screen advanced; press; turn -1; press; long; wait 300; '
                     'zones 3 1; wait 100; state set=670 use_f=1; wait 100; '
                     'screen advanced; press; turn 1; press; long; wait 300; info',
                     'set=190 ','units=C','zone=1/3'))
if checks: sys.exit('FAILED: '+', '.join(checks))
PY
