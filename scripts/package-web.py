#!/usr/bin/env python3
"""Package an already-built Emscripten game for the Play Online website."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def package(build_root, play_root):
    repo = Path(__file__).resolve().parents[1]
    version = re.search(r'project\(DuneCity VERSION ([0-9.]+)', (repo / 'CMakeLists.txt').read_text())[1]
    output = build_root / 'bin'
    files = {'index.html': output / 'dunecity.html',
             **{name: output / name for name in ('dunecity.js', 'dunecity.wasm', 'dunecity.data')},
             **{name: repo / 'web' / name for name in ('shell.js', 'shell.css', '.htaccess')}}
    for source in files.values():
        if not source.is_file():
            raise RuntimeError(f'Missing browser artifact: {source}')
    # Hash the compiled game, so rebuilding a version cannot reuse stale URLs.
    token = version + '-' + hashlib.sha256((output / 'dunecity.wasm').read_bytes()).hexdigest()[:12]
    play_root.mkdir(parents=True, exist_ok=True)
    for name, source in files.items():
        shutil.copyfile(source, play_root / name)
    index = play_root / 'index.html'
    html = index.read_text()
    for name in ('shell.css', 'shell.js', 'dunecity.js'):
        html = html.replace('"' + name + '"', '"' + name + '?v=' + token + '"')
    index.write_text(html)
    names = [name for name in files if name != '.htaccess']
    manifest = {
        'version': version,
        'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        'builtAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'artifacts': names,
        'sha256': {name: hashlib.sha256((play_root / name).read_bytes()).hexdigest() for name in names},
    }
    (play_root / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packaged DuneCity {version} at {play_root}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--play-root', type=Path, required=True)
    args = parser.parse_args()
    package(args.build_root, args.play_root)
