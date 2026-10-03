#!/usr/bin/env bash
set -euo pipefail
: "${SERIN_SIM_BIN:?Set SERIN_SIM_BIN}"
python3 - "$SERIN_SIM_BIN" <<'PY'
import os, subprocess, sys, tempfile
binary=os.path.abspath(sys.argv[1])
def run(commands):
    with tempfile.TemporaryDirectory() as work:
        p=subprocess.run([binary,'--headless','--run',commands],cwd=work,text=True,capture_output=True,timeout=30)
    assert p.returncode==0,p.stdout+p.stderr
    return p.stdout
base='wait 1800; bridge 1 1 -50; caps features=8704; state wifi_setup_avail=1 wifi_provisioned=1 wifi_connected=1 wifi_setup_ap=0; screen settings; turn 1; down; wait 1700; up; wait 50; '
def expect(name, commands, *texts):
    output=run(commands)
    for text in texts: assert text in output,name+': missing '+repr(text)+'\n'+output
    print('PASS: '+name)
    return output
qr=base+'state wifi_setup_ap=1; wait 100; '
expect('QR cancellation waits',qr+'press; info; labels','wz=cancelling','CMD: wifi cancel','Waiting for the controller.')
expect('starting cancellation waits',base+'long; info','wz=cancelling','CMD: wifi cancel')
expect('confirmed cancel returns to Wi-Fi',qr+'press; wifi_ack 0; info','screen=settings','set_sel=wifi')
expect('stale reply ignored',qr+'press; wifi_ack 0 999; info','wz=cancelling')
expect('unsolicited closed state is not confirmation',qr+'press; state wifi_setup_ap=0; wait 100; info','wz=cancelling')
expect('offline recovery explained',qr+'press; wifi_ack 2; info; labels; press; info','wz=recovery','Home Wi-Fi is unavailable.','screen=settings')
expect('missing reply supports retry',qr+'press; wait 16000; info; labels; press; info','wz=cancel_failed','No confirmation','wz=cancelling')
expect('missing reply permits leaving',qr+'press; wait 16000; long; info','screen=settings')
expect('trial wait stays pending',qr+'press; wait 10000; wifi_ack 1; wait 10000; info; wifi_ack 0; info','wz=cancelling','screen=settings')
expect('stale session never claims success',qr+'press; wifi_ack 3; info; labels','wz=cancel_failed','No confirmation')
expect('legacy Back explains expiry',qr.replace('features=8704','features=512')+'press; info; labels; press; info','wz=leave','Closes after about 10 minutes','screen=settings')
expect('initial setup only leaves','wait 1800; bridge 1 1 -50; caps features=8704; state wifi_setup_avail=1 wifi_provisioned=0 wifi_connected=0 wifi_setup_ap=1; screen settings; turn 1; press; wait 100; press; info','screen=settings')
expect('QR timeout requests cancellation',qr+'wait 541000; info; wifi_ack 0; info','CMD: wifi cancel','screen=thermostat')
output=expect('cancel retries do not restart setup',qr+'press; wait 5000; info','wz=cancelling')
assert output.count('CMD: wifi setup')==1, output
PY
