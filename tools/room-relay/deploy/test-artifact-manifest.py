#!/usr/bin/env python3
"""Regression tests for deploy/artifact-manifest.py, against temporary fixtures.

Nothing here reads or writes the real release, runtime, private key directory or
any service: every tree is built under a private temporary directory owned by the
invoking user, and the tool is run as a subprocess exactly as the launcher runs it.

    python3 deploy/test-artifact-manifest.py
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

TOOL = str(pathlib.Path(__file__).with_name('artifact-manifest.py'))
passed = failed = 0


def report(ok, description, detail=''):
    global passed, failed
    if ok:
        passed += 1
        print('ok   ' + description)
    else:
        failed += 1
        print('FAIL ' + description + (('  [' + detail.strip()[:300] + ']') if detail else ''))


def run(action, root, manifest):
    return subprocess.run([sys.executable, TOOL, action, '--root', str(root),
                           '--manifest', str(manifest)],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def accepts(description, root, manifest):
    done = run('verify', root, manifest)
    report(done.returncode == 0, description, done.stdout)


def refuses(description, root, manifest, expect=''):
    done = run('verify', root, manifest)
    ok = done.returncode == 1 and expect in done.stdout
    report(ok, description, 'rc=%d %s' % (done.returncode, done.stdout))


def build_tree(root):
    """A stand-in for a frozen release: sources, a dependency tree and a symlink."""
    shutil.rmtree(root, ignore_errors=True)
    (root / 'src').mkdir(parents=True)
    (root / 'node_modules' / 'dep' / 'lib').mkdir(parents=True)
    (root / 'deploy').mkdir()
    (root / 'src' / 'index.js').write_text("'use strict';\n")
    (root / 'src' / 'constants.js').write_text('const RELAY_PROTOCOL_VERSION = 5;\n')
    (root / 'node_modules' / 'dep' / 'lib' / 'dep.js').write_text('module.exports = 1;\n')
    (root / 'node_modules' / 'dep' / 'package.json').write_text('{"name":"dep"}\n')
    (root / 'deploy' / 'run-user-relay.sh').write_text('#!/bin/bash\n')
    (root / 'package.json').write_text('{"name":"room-relay"}\n')
    (root / 'REVISION').write_text('4940aef0000000000000000000000000000000ab\n')
    # The official Node runtime ships bin/npm as a relative symlink; a release
    # can carry one too, so the manifest has to cover links as links.
    (root / 'node_modules' / '.bin').mkdir()
    os.symlink('../dep/lib/dep.js', root / 'node_modules' / '.bin' / 'dep')
    # Frozen artifacts are never group/other writable, matching bootstrap.sh.
    subprocess.run(['chmod', '-R', 'go-w', str(root)], check=True)


with tempfile.TemporaryDirectory(prefix='dune-manifest-tests-') as tmp:
    home = pathlib.Path(tmp)
    tree = home / 'releases' / 'abc'
    state = home / 'state'
    manifest = state / 'release.manifest'
    build_tree(tree)

    # --- writing --------------------------------------------------------
    first = run('write', tree, manifest)
    report(first.returncode == 0, 'write records the tree', first.stdout)
    recorded = manifest.read_text()
    report(oct(manifest.stat().st_mode & 0o777) == '0o600',
           'the manifest is written 0600', oct(manifest.stat().st_mode))
    report(not manifest.is_relative_to(tree),
           'the manifest lives outside the tree it covers')
    run('write', tree, manifest)
    report(manifest.read_text() == recorded, 'writing twice is byte-for-byte deterministic')
    report('node_modules/dep/lib/dep.js' in recorded and 'src/index.js' in recorded
           and 'REVISION' in recorded and 'deploy/run-user-relay.sh' in recorded,
           'coverage includes release code, deployment files and dependencies')
    report('l - - ../dep/lib/dep.js node_modules/.bin/dep' in recorded,
           'a symlink is recorded with its exact target')
    inside = run('write', tree, tree / 'self.manifest')
    report(inside.returncode == 1 and 'outside the tree' in inside.stdout,
           'refuses to write the manifest inside the covered tree', inside.stdout)
    accepts('verify accepts the recorded tree', tree, manifest)

    # --- content, missing, extra, symlink, mode -------------------------
    (tree / 'src' / 'index.js').write_text("'use strict'; // edited\n")
    refuses('refuses changed file contents', tree, manifest, 'changed: src/index.js')
    build_tree(tree)
    accepts('verify accepts the rebuilt identical tree', tree, manifest)

    (tree / 'node_modules' / 'dep' / 'lib' / 'dep.js').unlink()
    refuses('refuses a missing file', tree, manifest, 'missing: node_modules/dep/lib/dep.js')
    build_tree(tree)
    shutil.rmtree(tree / 'node_modules' / 'dep' / 'lib')
    refuses('refuses a missing directory', tree, manifest,
            'missing: node_modules/dep/lib')
    build_tree(tree)

    (tree / 'src' / 'extra.js').write_text('leftover\n')
    os.chmod(tree / 'src' / 'extra.js', 0o644)
    refuses('refuses an extra file', tree, manifest, 'unexpected: src/extra.js')
    build_tree(tree)
    (tree / 'src' / 'extra').mkdir(mode=0o755)
    refuses('refuses an extra directory', tree, manifest, 'unexpected: src/extra')
    build_tree(tree)

    os.symlink('../dep/package.json', tree / 'node_modules' / '.bin' / 'other')
    refuses('refuses an added symlink', tree, manifest, 'unexpected: node_modules/.bin/other')
    build_tree(tree)
    (tree / 'node_modules' / '.bin' / 'dep').unlink()
    os.symlink('/etc/passwd', tree / 'node_modules' / '.bin' / 'dep')
    refuses('refuses a retargeted symlink', tree, manifest, 'changed: node_modules/.bin/dep')
    build_tree(tree)
    (tree / 'src' / 'index.js').unlink()
    os.symlink('/etc/passwd', tree / 'src' / 'index.js')
    refuses('refuses a file replaced by a symlink', tree, manifest, 'changed: src/index.js')
    build_tree(tree)
    (tree / 'node_modules' / '.bin' / 'dep').unlink()
    (tree / 'node_modules' / '.bin' / 'dep').write_text('module.exports = 1;\n')
    os.chmod(tree / 'node_modules' / '.bin' / 'dep', 0o644)
    refuses('refuses a symlink replaced by a regular file', tree, manifest,
            'changed: node_modules/.bin/dep')
    build_tree(tree)

    os.chmod(tree / 'deploy' / 'run-user-relay.sh', 0o755)
    refuses('refuses a changed mode', tree, manifest, 'changed: deploy/run-user-relay.sh')
    build_tree(tree)
    os.chmod(tree / 'src' / 'index.js', 0o666)
    refuses('refuses a group/other writable entry', tree, manifest, 'group/other writable')
    build_tree(tree)
    os.mkfifo(tree / 'src' / 'pipe')
    refuses('refuses an unexpected file type', tree, manifest, 'unexpected file type')
    build_tree(tree)
    accepts('verify accepts the tree once more after every refusal', tree, manifest)

    # Root metadata is part of the artifact, not only its descendants.
    root_mode = tree.stat().st_mode & 0o7777
    os.chmod(tree, 0o777)
    refuses('refuses a writable artifact root', tree, manifest, 'root is group/other writable')
    os.chmod(tree, 0o700 if root_mode != 0o700 else 0o755)
    refuses('refuses safe but changed root permissions', tree, manifest, 'root mode changed')
    os.chmod(tree, root_mode)
    accepts('restored root permissions verify', tree, manifest)
    import importlib.util
    from unittest import mock
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('manifest_under_test', TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with mock.patch.object(module.os, 'getuid', return_value=os.getuid()+1):
        try:
            module.resolve_root(tree)
            wrong_owner_refused = False
        except module.Refused as error:
            wrong_owner_refused = 'owned by another account' in str(error)
    report(wrong_owner_refused, 'refuses root ownership belonging to another account')

    # --- the covered root is pinned by absolute path --------------------
    moved = home / 'releases' / 'def'
    build_tree(moved)
    refuses('refuses a different release directory with the same contents', moved, manifest,
            'was presented')
    current = home / 'current'
    os.symlink(tree, current)
    accepts('accepts the release reached through the current symlink', current, manifest)
    current.unlink()
    os.symlink(moved, current)
    refuses('refuses a current symlink re-pointed at another release', current, manifest,
            'was presented')
    current.unlink()
    shutil.rmtree(moved)

    # --- a symlinked directory is never traversed -----------------------
    outside = home / 'outside'
    outside.mkdir()
    (outside / 'planted.js').write_text('planted\n')
    os.symlink(outside, tree / 'src' / 'elsewhere')
    linked = run('write', tree, manifest)
    text = manifest.read_text()
    report(linked.returncode == 0 and 'src/elsewhere/planted.js' not in text
           and ('l - - %s src/elsewhere' % outside) in text,
           'a directory symlink is recorded, not traversed into', text)
    (outside / 'planted.js').write_text('changed outside the root\n')
    accepts('content behind a directory symlink is outside the coverage', tree, manifest)
    (tree / 'src' / 'elsewhere').unlink()
    refuses('removing that directory symlink is still refused', tree, manifest,
            'missing: src/elsewhere')
    build_tree(tree)
    run('write', tree, manifest)
    accepts('re-frozen tree verifies', tree, manifest)

    # --- the manifest itself --------------------------------------------
    good = manifest.read_text()
    manifest.write_text(good.replace('src/index.js', 'src/index2.js'))
    refuses('refuses a manifest whose digest no longer covers it', tree, manifest,
            'digest does not cover')
    lines = good.splitlines()
    body = [line for line in lines[:-1] if not line.endswith(' src/index.js')]
    import hashlib
    rebuilt = ''.join(line + '\n' for line in body)
    manifest.write_text(rebuilt + 'digest '
                        + hashlib.sha256(rebuilt.encode()).hexdigest() + '\n')
    refuses('refuses a resealed manifest that dropped a covered file', tree, manifest,
            'declares')
    escape = [line for line in lines[:-1]]
    escape[3] = 'count %d' % (int(lines[3].split()[1]) + 1)
    escape.append('f 0644 1 %s ../escape' % ('0' * 64))
    rebuilt = ''.join(line + '\n' for line in escape)
    manifest.write_text(rebuilt + 'digest '
                        + hashlib.sha256(rebuilt.encode()).hexdigest() + '\n')
    refuses('refuses a manifest entry that escapes the root', tree, manifest, 'escapes')
    manifest.write_text(good)
    accepts('the untouched manifest still verifies', tree, manifest)
    manifest.unlink()
    refuses('refuses a missing manifest', tree, manifest, 'Cannot read the manifest')

print('\n%d passed, %d failed' % (passed, failed))
raise SystemExit(1 if failed else 0)
