#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
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
# Only entry from Home resets the menu selection; submenu returns retain it.
# Settle the screen fade before querying visible labels; info is synchronous.
for reopen in ('press', 'tap 240 240'):
    check('Menu selects Home after Back via '+reopen,
          lambda action=reopen: expect('press; turn -1; press; wait 400; '+action+'; wait 400; labels; press; info',
                                      'text=Home', 'screen=thermostat'))
check('Menu selects Home after applying a mode',
      lambda: expect('press; turn 1; press; press; press; wait 300; labels', 'text=Home'))
check('Menu selects Home with optional items hidden',
      lambda: expect('caps fan_steps=0 fan_auto=0 vane_v=0 vane_h=0; press; turn -1; press; press; wait 300; labels',
                     'text=Home'))
check('Settings return retains menu selection',
      lambda: expect('press; turn 3; press; turn -1; press; wait 300; labels', 'text=Settings'))
check('Mode return retains menu selection',
      lambda: expect('press; turn 1; press; turn 6; press; wait 300; labels', 'text=Mode'))
# Back retraces the way in.  `screen <name>` jumps straight from the thermostat,
# which is what the home dial's own shortcuts do, so each pair below opens the
# same screen twice and expects a different landing.
check('Settings Back returns to Menu',
      lambda: expect('press; turn 3; press; turn -1; press; info','screen=menu'))
check('Settings Back returns home when home opened it',
      lambda: expect('screen settings; turn -1; press; info','screen=thermostat'))
check('Mode Back returns to Menu',
      lambda: expect('press; turn 1; press; turn 6; press; info','screen=menu'))
check('Mode Back returns home when home opened it',
      lambda: expect('screen mode; turn 6; press; info','screen=thermostat'))
check('Airflow Back returns to Menu',
      lambda: expect('bridge 1 1 -50; wait 60; press; turn 2; press; wait 100; press; turn 3; press; info','screen=menu'))
check('Airflow Back returns home when home opened it',
      lambda: expect('bridge 1 1 -50; wait 60; screen fanvane; wait 100; press; turn 3; press; info','screen=thermostat'))
check('Zones Back returns to Menu',
      lambda: expect('bridge 1 1 -50; zones 3 0; wait 100; press; turn 3; press; wait 60; turn 3; press; info','screen=menu'))
check('Zones Back returns home when home opened it',
      lambda: expect('bridge 1 1 -50; zones 3 0; wait 100; screen zones; wait 60; turn 3; press; info','screen=thermostat'))
# Three levels deep, unwound one step at a time.
check('Back unwinds About to Settings to Menu',
      lambda: expect('bridge 1 1 -50; wait 60; press; turn 3; press; wait 60; turn 5; press; wait 100;'
                     ' turn 2; press; wait 100; info; turn 2; press; wait 60; info',
                     'screen=settings','screen=menu'))
check('Back unwinds Advanced to Settings to Menu',
      lambda: expect('bridge 1 1 -50; wait 60; press; turn 3; press; wait 60; turn 6; press; wait 100;'
                     ' turn 7; press; wait 100; info; turn 1; press; wait 60; info',
                     'screen=settings','screen=menu'))
check('Zones opened from Advanced returns to Advanced',
      lambda: expect('bridge 1 1 -50; zones 3 0; wait 100; screen advanced; wait 100; turn 5; press;'
                     ' wait 100; turn 3; press; wait 100; info','screen=advanced'))
# The update readout is a finished run, never somewhere Back should land.
check('Back skips the update readout',
      lambda: expect('bridge 1 1 -50; ota uptodate; wait 100; press; wait 100; turn 2; press; wait 100; info',
                     'screen=thermostat'))
check('Heat/Cool identity', lambda: expect('caps modes=0x19; state mode=heat_cool; screen mode; wait 300; labels; press; info','text=Heat/Cool','text=Sets a temperature range','mode=heat_cool'))
check('Auto identity', lambda: expect('caps modes=0x19; state mode=auto; screen mode; wait 300; labels; press; info','text=Auto','text=Automatic mode','mode=auto'))
base='bridge 1 1 -50; caps features={features}; state wifi_setup_avail=1 wifi_provisioned=0 wifi_connected=0 wifi_setup_ap=1 hk_paired={paired}; screen settings; turn 1; press; wait 100; state wifi_connected=1 wifi_setup_ap=0; wait 100; '
for features,paired in [(512,0),(514,1)]:
    prefix=base.format(features=features,paired=paired)
    check(f'Wi-Fi finishes without pairing {features}/{paired}',lambda p=prefix: expect(p+'wait 3200; info; labels; press; info','wz=done','text=Press to finish','screen=settings','set_sel=wifi'))
for next_action in ('press','wait 3200'):
    check('Wi-Fi offers pairing '+next_action,lambda a=next_action: expect(base.format(features=514,paired=0)+a+'; info','wz=hk'))
check('Removed pairing capability leaves QR',lambda: expect(base.format(features=514,paired=0)+'press; caps features=512; info','screen=settings','set_sel=wifi'))
check('Setup pairing completion restores Wi-Fi',lambda: expect(base.format(features=514,paired=0)+'press; state hk_paired=1; press; info','screen=settings','set_sel=wifi'))
check('Skipping setup pairing restores Wi-Fi',lambda: expect(base.format(features=514,paired=0)+'press; press; info','screen=settings','set_sel=wifi'))
check('Direct home pairing restores HomeKit',lambda: expect('bridge 1 1 -50; caps features=514; state hk_paired=0; screen settings; turn 3; press; state hk_paired=1; press; info','screen=settings','set_sel=homekit'))
# While syncing, the controller has not confirmed the zone, so a change would
# be dropped. Mode and Airflow refuse it and return home to the Syncing chip.
syncing='bridge 1 1 -50; wait 60; state alert=syncing; wait 60; '
def refused(commands, *texts):
    output=run(syncing+commands+'; wait 200; info; labels')
    for text in ('screen=thermostat','text=Syncing')+texts:
        assert text in output, 'missing '+repr(text)+'\n'+output
    assert 'CMD: mode=' not in output, 'unexpected command\n'+output
check('Mode commit while syncing keeps the mode', lambda: refused('screen mode; wait 100; turn 1; press','mode=heat'))
check('Menu Mode commit while syncing keeps the mode', lambda: refused('press; wait 100; turn 1; press; wait 100; turn 1; press','mode=heat'))
check('Menu Airflow while syncing stays home', lambda: refused('press; wait 100; turn 2; press'))
check('Mode Back while syncing still retraces', lambda: expect(syncing+'press; wait 100; turn 1; press; wait 100; turn 6; press; info','screen=menu'))
check('Mode commit after syncing applies', lambda: expect(syncing+'state alert=none; wait 60; screen mode; wait 100; turn 1; press; info','mode=cool','CMD: mode=COOL'))
if checks: sys.exit(1)
PY
python3 "$(dirname "$0")/selector_layout.py"
