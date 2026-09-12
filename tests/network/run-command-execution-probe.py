#!/usr/bin/env python3
"""Exercise command authorization and batch recovery against real initialized game objects.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-command-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runOwnershipProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/network/command-execution-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'command-probe-main.cpp'
source.write_text(main)
cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i+2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'command-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
app = out / 'command-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/command-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
env = dict(os.environ, DUNECITY_USERDIR=str(out / 'profile'), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
with (out / 'run.log').open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
text = (out / 'run.log').read_text()
for marker in ('OWNERSHIP_PROBE_PASS:', 'COMMAND_BATCH_PROBE_PASS:'):
    if marker not in text:
        raise RuntimeError('Missing completion marker: ' + marker)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Real game command authorization and batch recovery passed. Logs: ' + str(out))
