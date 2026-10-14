#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Child Lock gates every home face except the setup faces: pairing and the
# controller Wi-Fi offer. The offline faces, their dialogs, and an offline
# face dismissed to the dimmed home face must not reach Menu or Settings.
set -euo pipefail
: "${SERIN_SIM_BIN:?Set SERIN_SIM_BIN to the simulator executable}"
python3 - "$SERIN_SIM_BIN" <<'PY'
import os, subprocess, sys, tempfile
binary=os.path.abspath(sys.argv[1])
def run(commands):
    with tempfile.TemporaryDirectory() as work:
        p=subprocess.run([binary,'--headless','--run','wait 1800; '+commands],cwd=work,text=True,capture_output=True,timeout=60)
        assert p.returncode==0, p.stdout+p.stderr
        return p.stdout
checks=[]
def check(name, fn):
    try: fn(); print('PASS '+name)
    except AssertionError as e: checks.append(name); print('FAIL '+name+': '+str(e))
def expect(commands,*texts):
    output=run(commands)
    for text in texts: assert text in output, 'missing '+repr(text)+'\n'+output
    return output
def refused(commands):
    output=expect(commands+'; wait 300; info; labels','screen=thermostat','dlg=none','lock=1','text=Hold to unlock')
    assert 'CMD: begin pairing' not in output, output
live='bridge 1 1 -50; wait 60; state set=720 cn105_connected=1 alert=none; wait 60; '
lock='screen advanced; turn 1; press; turn 1; press; wait 100; screen thermostat; wait 300; '
link_off=live+'state alert=nolink; wait 100; '
cn105_off=live+'state cn105_connected=0; wait 100; '
# Dismissing the offline dialog leaves the dimmed home face with Offline.
remind=link_off+'press; wait 300; turn 1; press; wait 300; '
unlock='down; wait 1800; up; wait 100; '
check('Locked live face refuses Menu', lambda: refused(live+lock+'press'))
check('Locked controller-offline face refuses its dialog',
      lambda: refused(link_off+lock+'press'))
check('Locked heat-pump-offline face refuses its dialog',
      lambda: refused(cn105_off+lock+'press'))
check('Locked offline label tap refuses its dialog',
      lambda: refused(link_off+lock+'tap 233 247'))
check('Locked remind face refuses Menu',
      lambda: (expect(remind+'info; labels','face=link_off','text=Offline'), refused(remind+lock+'press')))
# A short knob hold on a locked face starts the unlock arc, not Zones.
check('Locked remind face refuses Zones',
      lambda: expect('zones 3 0; wait 100; '+remind+lock+'long; wait 300; info',
                     'screen=thermostat','lock=1','lock_win=0'))
check('Hold unlocks an offline face and opens its dialog',
      lambda: expect(link_off+lock+unlock+'press; wait 300; info','lock_win=1','dlg=other'))
# The offline dialog's Settings action is touch-only. Open the dialog while
# unlocked, let the unlock window close, then tap Settings.
def dialog_action_refused():
    opened=link_off+lock+unlock+'press; wait 300; wait 31000; '
    output=expect(opened+'info; hitmap','dlg=other','lock_win=0')
    hit=[l for l in output.splitlines() if l.startswith('hit ') and l.endswith(' text=Settings')]
    assert hit, 'missing Settings target\n'+output
    x,y=(int(f.split('=')[1]) for f in hit[-1].split()[1:3])
    output=expect(opened+f'tap {x} {y}; wait 300; info; labels','screen=thermostat','lock=1','text=Hold to unlock')
check('Offline dialog action refused after the lock closes', dialog_action_refused)
# Child Lock protects Home, not a Reset confirmation reached in Advanced.
# Close the temporary unlock window with real idle time before opening Reset.
def advanced_reset_after_idle(control):
    expect(live+lock+unlock+'info','screen=thermostat','lock=1','lock_win=1')
    idle=live+lock+unlock+'screen advanced; turn 5; wait 31000; '
    output=expect(idle+'info','screen=advanced','adv_sel=reset','lock=1','lock_win=0')
    opened=idle+'down; wait 1800; up; wait 300; '
    output=expect(opened+'info; hitmap','screen=advanced','dlg=reset','lock_win=0')
    hit=[l for l in output.splitlines() if l.startswith('hit ') and l.endswith(' text=Reset')]
    assert hit, 'missing Reset target\n'+output
    x,y=(int(f.split('=')[1]) for f in hit[-1].split()[1:3])
    action=f'tap {x} {y}' if control == 'touch' else 'turn 1; press'
    expect(opened+action+'; wait 1100; info','reset_fired=1')
check('Advanced touch Reset works after the lock closes', lambda: advanced_reset_after_idle('touch'))
check('Advanced knob Reset works after the lock closes', lambda: advanced_reset_after_idle('knob'))
check('Setup exemption: Wi-Fi offer stays reachable while locked',
      lambda: expect(live+'state wifi_setup_avail=1 wifi_provisioned=0; wait 100; '+lock+'info; press; wait 300; info',
                     'face=wifi_off','lock=1','screen=wifi'))
check('Setup exemption: pairing stays reachable while locked',
      lambda: expect('bridge 0 0 0; wait 100; '+lock+'info; press; wait 300','face=unpaired','lock=1','CMD: begin pairing'))
if checks: sys.exit('FAILED: '+', '.join(checks))
PY
