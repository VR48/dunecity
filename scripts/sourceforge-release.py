#!/usr/bin/env python3
"""Mirror an existing stable GitHub release; never rebuild or delete releases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.parse
import urllib.request

REPO = 'VR48/dunecity'
PROJECT = 'dunelegacy'


def run(*args, **kwargs):
    return subprocess.check_output(args, **kwargs).decode().strip()


def version(tag):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', tag):
        raise ValueError('Expected stable tag vX.Y.Z')
    return tag[1:]


def expected_files(tag):
    v = version(tag)
    return [f'DuneCity-{v}-Windows-x64.zip', f'DuneCity-{v}-macOS.dmg',
            f'DuneCity-{v}-Linux-x86_64.AppImage', f'dunecity_{v}_amd64.deb',
            f'DuneCity-{v}-Linux-x64.rpm', f'DuneCity-{v}-Linux-x64.tar.gz']


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def validate_release(release, tag):
    if release['tag_name'] != tag or release['draft'] or release['prerelease']:
        raise ValueError('Only published stable releases can be mirrored')
    assets = {a['name']: a for a in release['assets']}
    for name in expected_files(tag):
        if name not in assets or assets[name]['size'] <= 0:
            raise ValueError(f'Missing desktop package: {name}')
    return assets


def verify_files(folder, manifest):
    for name, checksum in manifest.items():
        if Path(name).name != name or name in ('.', '..'):
            raise ValueError('Unsafe manifest filename')
        if digest(folder / name) != checksum:
            raise ValueError(f'Checksum mismatch: {name}')


def prepare(tag, folder):
    version(tag)
    release = json.loads(run('gh', 'api', f'repos/{REPO}/releases/tags/{tag}'))
    assets = validate_release(release, tag)
    folder.mkdir(parents=True, exist_ok=False)
    run('git', 'fetch', '--no-tags', 'origin', f'refs/tags/{tag}:refs/tags/{tag}')
    commit = run('git', 'rev-parse', f'{tag}^{{commit}}')
    for name in expected_files(tag):
        run('gh', 'release', 'download', tag, '--repo', REPO, '--pattern', name,
            '--dir', str(folder))
        if (folder/name).stat().st_size != assets[name]['size']:
            raise ValueError(f'Incorrect download size: {name}')
        published = assets[name].get('digest')
        if published and published != 'sha256:' + digest(folder/name):
            raise ValueError(f'GitHub digest mismatch: {name}')
    # Source belongs in Git; do not package the tracked historical build trees.
    (folder/'README.md').write_text((release.get('body') or '') +
        f'\n\nSource code: https://github.com/{REPO}/tree/{tag}\n'
        f'Source commit: `{commit}`\n')
    manifest = {p.name: digest(p) for p in sorted(folder.iterdir())}
    (folder/'SHA256SUMS').write_text(''.join(f'{sha}  {name}\n' for name, sha in manifest.items()))
    print(f'Prepared {tag}: six packages, notes and checksums ({commit})')
    return commit


def publish(tag, folder, commit):
    user = os.environ.get('SOURCEFORGE_USER', '')
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]*', user):
        raise ValueError('Set SOURCEFORGE_USER to the release account username')
    api_key = os.environ.get('SOURCEFORGE_API_KEY', '')
    if not api_key or not os.environ.get('GIT_SSH_COMMAND'):
        raise ValueError('SourceForge API key and pinned SSH configuration are required')
    remote = f'{user}@frs.sourceforge.net:/home/frs/project/{PROJECT}/dunecity/{version(tag)}/'
    ssh = os.environ['GIT_SSH_COMMAND']
    # rsync creates the version folder; parent dunecity must be created at setup.
    subprocess.run(['rsync', '-a', '--checksum', '-e', ssh, str(folder)+'/', remote], check=True)
    # Read back through the authenticated file service, avoiding mirror lag.
    manifest = {p.name: digest(p) for p in folder.iterdir()}
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['rsync', '-a', '-e', ssh, remote, tmp+'/'], check=True)
        verify_files(Path(tmp), manifest)
    print('All uploaded file checksums verified')
    git_remote = f'ssh://{user}@git.code.sf.net/p/{PROJECT}/code'
    subprocess.run(['git', 'push', git_remote, f'refs/tags/{tag}:refs/tags/dunecity-{tag}'], check=True)
    latest = json.loads(run('gh', 'api', f'repos/{REPO}/releases/latest'))['tag_name']
    if latest != tag:
        print('Historical backfill: source tag uploaded; current branch/defaults unchanged')
        return
    # Fast-forward only. Never force-push or mirror-delete the Legacy repository.
    subprocess.run(['git', 'push', git_remote, f'{commit}:refs/heads/dunecity'], check=True)
    defaults = dict(zip(['windows', 'mac', 'linux'], expected_files(tag)[:3]))
    for platform, name in defaults.items():
        url = f'https://sourceforge.net/projects/{PROJECT}/files/dunecity/{version(tag)}/{name}'
        data = urllib.parse.urlencode({'api_key': api_key, 'default': platform}).encode()
        req = urllib.request.Request(url, data=data, method='PUT', headers={'Accept': 'application/json'})
        with urllib.request.urlopen(req, timeout=60) as response:
            result = json.load(response)
        assigned = result.get('result', {}).get('x_sf', {}).get('default', '').split(',')
        if result.get('error') or platform not in assigned:
            raise RuntimeError(f'SourceForge did not confirm {platform} default')
        print(f'Confirmed {platform} default: {name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    commit = prepare(args.tag, args.directory)
    if args.publish:
        publish(args.tag, args.directory, commit)
