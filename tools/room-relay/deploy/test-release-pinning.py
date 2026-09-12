#!/usr/bin/env python3
"""Switch current at the verification/exec seam; only verified A may execute."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest import mock

if not sys.platform.startswith('linux'):
    print('SKIP: release launch fixture requires Linux readlink/flock')
    raise SystemExit(0)
sys.dont_write_bytecode = True
os.umask(0o077)
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='dune-pin-') as tmp:
    base = Path(tmp)
    a, b = base/'releases/a', base/'releases/b'
    for release, message in [(a,'VERIFIED_A'),(b,'UNVERIFIED_B')]:
        (release/'deploy').mkdir(parents=True)
        (release/'REVISION').write_text(release.name)
        (release/'deploy/sandbox.py').write_text('print('+repr(message)+')\n')
        script=(here/'run-user-relay.sh').read_text()
        script=script.replace('base="/home/dunelegacy-deploy/dunecity-relay"','base="'+str(base)+'"')
        (release/'deploy/run-user-relay.sh').write_text(script)
        shutil.copyfile(here/'artifact-manifest.py', release/'deploy/artifact-manifest.py')
    (base/'runtime').mkdir()
    (base/'state').mkdir()
    (base/'current').symlink_to(a)
    for root, name in [(a,'release'),(base/'runtime','runtime')]:
        subprocess.run([sys.executable,str(here/'artifact-manifest.py'),'write','--root',str(root),
                        '--manifest',str(base/'state'/f'{name}.manifest')],check=True,stdout=subprocess.DEVNULL)
    with mock.patch.dict(os.environ,{'RELAY_BASE':str(base)},clear=True):
        spec=importlib.util.spec_from_file_location('pinned_supervisor',here/'relay-supervisor.py')
        sup=importlib.util.module_from_spec(spec);spec.loader.exec_module(sup)
        assert sup.verify_artifacts()
        # The update happens after verification, immediately before command selection/exec.
        (base/'current').unlink();(base/'current').symlink_to(b)
        assert sup.release_path()==a
        assert sup.manifest_pairs()[1][0]==a
        result=subprocess.run(sup.child_command(),capture_output=True,text=True,timeout=5)
        assert result.returncode==0 and result.stdout.strip()=='VERIFIED_A',result
        # Passing an unrelated release to A's launcher must be refused too.
        bad=subprocess.run(['/bin/bash',str(a/'deploy/run-user-relay.sh'),'--exec-child',str(b)],
                           capture_output=True,text=True,timeout=5)
        assert bad.returncode!=0 and 'UNVERIFIED_B' not in bad.stdout
        assert "base / 'current'" not in (here/'sandbox.py').read_text()
print('PASS: swapped current cannot change verified release at launch, and sandbox follows its own release')
