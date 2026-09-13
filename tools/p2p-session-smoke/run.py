#!/usr/bin/env python3
"""Real native full-mesh test. Uses the actual PHP service and C++ session transports.
Usage: PHP_BIN=php python3 tools/p2p-session-smoke/run.py /path/to/session-peer [--outage]
"""
import importlib.util
import os
from pathlib import Path
import selectors
import socket
import shutil
import tempfile
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('signaling_fixture',root/'tools/p2p-signaling/test/test_signaling.py')
fixture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
service=fixture.ServiceFixture()
peers=[]
selector=selectors.DefaultSelector()
ready=set()
passed=set()
outage='--outage' in sys.argv
hold=75000 if outage else 6000
try:
    base=f'http://127.0.0.1:{service.port}/p2p'
    # The fixture normally mounts at /. Use /p2p here, matching production clients.
    service.write_config(base_path='/p2p',public_base_url=base,ice_servers=[])
    host=service.request('POST','/p2p/v1/admission/host',dict(fixture.claims(),maxPeers=3,mode='custom',visibility='public'))
    assert host.status==200,host.fields.get('message')
    admissions=[host]
    for _ in range(2):
        response=service.request('POST','/p2p/v1/admission/join',dict(fixture.claims(),room=host.fields['room'],publicOnly='1'))
        assert response.status==200,response.fields.get('message')
        admissions.append(response)
    order=[1,2,0] if "--host-last" in sys.argv else [0,1,2]
    for slot,index in enumerate(order):
        admission=admissions[index]
        peer=subprocess.Popen([sys.argv[1],'3',str(hold)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
        peer.stdin.write('\n'.join([base,admission.fields['grant'],host.fields['room'],f'Peer{index}'])+'\n')
        peer.stdin.close()
        peers.append(peer);selector.register(peer.stdout,selectors.EVENT_READ,slot)
        if "--host-last" in sys.argv:time.sleep(0.3)
    deadline=time.monotonic()+hold/1000+50
    stopped=False
    while selector.get_map() and time.monotonic()<deadline:
        for key,_ in selector.select(1):
            line=key.fileobj.readline()
            if not line:
                selector.unregister(key.fileobj)
                code=peers[key.data].poll()
                if code is not None and code!=0:raise RuntimeError(f'peer{key.data} exited {code}')
                continue
            print(f'peer{key.data}: {line.rstrip()}',flush=True)
            if line.strip()=='READY':ready.add(key.data)
            if line.startswith('PASS:'):passed.add(key.data)
        if outage and len(ready)==3 and not stopped:
            # Give the host's phase update time to be delivered; no payload uses this service.
            time.sleep(1)
            service.stop_server();stopped=True
            gone=False
            for _ in range(60):
                try:
                    with socket.create_connection(('127.0.0.1',service.port),timeout=0.1):pass
                except OSError:
                    gone=True;break
                time.sleep(0.05)
            if not gone:raise RuntimeError('signaling listener survived the outage trigger')
            print('SIGNALING STOPPED; listener confirmed closed',flush=True)
    codes=[peer.wait(timeout=5) for peer in peers]
    if len(passed)!=3 or any(codes):raise RuntimeError(f'full mesh failed: ready={len(ready)}, passed={len(passed)}, exits={codes}')
    print('PASS: all three real session transports exchanged ordered maximum-size payloads'+(' for 75 seconds across signaling loss' if outage else ''))
finally:
    for peer in peers:
        if peer.poll() is None:peer.terminate()
    for peer in peers:
        try:peer.wait(timeout=5)
        except subprocess.TimeoutExpired:peer.kill();peer.wait()
    if len(passed)!=3:
        evidence=tempfile.mkdtemp(prefix="dunecity-p2p-failed-session-")
        shutil.copytree(service.tmp,Path(evidence)/"fixture")
        print(f"Private test evidence retained: {evidence}",flush=True)
    service.stop()
