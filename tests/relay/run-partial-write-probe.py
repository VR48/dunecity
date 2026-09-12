#!/usr/bin/env python3
"""Compile a test-only curl adapter and exercise the unchanged production send queue.

Forces short accepts and CURLE_AGAIN without relying on OS socket buffer sizes.
A real libcurl and relay verify exact bulk bytes/order. This is injected API
backpressure, not evidence of a naturally saturated production socket.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
build = root / 'build'
out = Path(tempfile.mkdtemp(prefix='dunecity-partial-write-'))
target = 'bin/relay_transport_harness'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
cc = shlex.split(next(x for x in commands if ' -c ' in x and '/RelayWebSocketCurl.cpp' in x and 'relay_transport_harness' in x))
for flag in ('-include','-MT','-MF'):
    if flag in cc:
        i=cc.index(flag);del cc[i:i+2]
for flag in ('-MD','-MMD'):
    if flag in cc: cc.remove(flag)
obj=out/'partial.o';cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(root/'tests/relay/ForcedPartialCurl.cpp')
link=shlex.split(next(x for x in commands if ' -o '+target+' ' in x))
link=link[link.index('&&')+1:];link=link[:link.index('&&')]
exe=out/'partial-harness';link[link.index('-o')+1]=str(exe)
link=[str(obj) if x.endswith('/RelayWebSocketCurl.cpp.o') else x for x in link]
for command in (cc,link): subprocess.run(command,cwd=build,check=True)
env=dict(os.environ,RELAY_TRANSPORT_HARNESS=str(exe),RELAY_PORT='8791',RELAY_BULK_COUNT='8')
result=subprocess.run(['bash',str(root/'tests/relay/run-relay-transport-harness.sh'),'bulk'],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'result.log').write_text(result.stdout)
if result.returncode: print(result.stdout);raise SystemExit(result.returncode)
counts=[tuple(map(int,m)) for m in re.findall(r'PARTIAL_PROBE short=(\d+) again=(\d+) complete=(\d+)',result.stdout)]
if len(counts)!=2 or counts[0][0]==0 or not all(again>0 and complete>0 for short,again,complete in counts):
    raise RuntimeError('Bulk sender must exercise short writes; both peers must retry and complete: '+str(counts))
if 'bulkrecv=8 bulkcorrupt=0' not in result.stdout: raise RuntimeError('Bulk integrity verification missing')
print('PASS: unchanged production queue, real curl/relay, 8 x 200000 byte bodies intact and ordered; short/again/completed:',counts)
print('Evidence:',out/'result.log')
