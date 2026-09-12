#!/usr/bin/env python3
"""Deterministic artifact manifest for the frozen release and the pinned runtime.

Writes or verifies an exact description of one directory tree: every entry's
type, mode, owner, symlink target and — for regular files — size and SHA256.
Verification is set equality, so a missing entry, an extra entry, a retargeted
symlink, a type change, a mode change or edited content all fail closed.

The manifest is written outside the tree it covers (`$base/state/`), so nothing
has to be excluded from coverage: there are no transient files inside the
release or the runtime, and this tool supports no exclusions at all.

Scope of the guarantee: this detects a release or runtime that is no longer the
frozen artifact - a bad rsync, a partial upgrade, a half-removed directory, an
edited file, a re-pointed symlink. It is NOT tamper resistance against the
account that owns the files: that account can rewrite the manifest as easily as
the tree. Root-owned integrity is bootstrap.sh's job, not this tool's.

    artifact-manifest.py write  --root DIR --manifest FILE
    artifact-manifest.py verify --root DIR --manifest FILE

Exit status: 0 accepted, 1 refused (with the reason on stderr), 2 usage.
"""
import argparse
import hashlib
import os
import pathlib
import stat
import sys
import tempfile
import urllib.parse

FORMAT = 'dune-artifact-manifest v2'
# Paths and symlink targets are percent-encoded, so a name containing a space or
# a newline cannot forge extra fields or extra lines.
SAFE = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/._-+@'
CHUNK = 1 << 20


class Refused(Exception):
    """Any reason the tree or the manifest is not acceptable."""


def quote(text):
    return urllib.parse.quote(text, safe=SAFE)


def unquote(text):
    return urllib.parse.unquote(text, errors='strict')


def check_relative(rel):
    """A manifest entry may only name a path inside the covered root."""
    if rel in ('', '.') or rel.startswith('/') or rel.startswith('./'):
        raise Refused('Manifest entry is not a relative path inside the root: ' + rel)
    parts = rel.split('/')
    if any(part in ('', '.', '..') for part in parts):
        raise Refused('Manifest entry escapes or does not normalise inside the root: ' + rel)
    return rel


def digest_file(path):
    sha = hashlib.sha256()
    # O_NOFOLLOW: the entry was lstat'd as a regular file; do not let a symlink
    # swapped in between the walk and the read redirect this read out of the tree.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise Refused('Not a regular file when read: ' + str(path))
        while True:
            block = os.read(fd, CHUNK)
            if not block:
                break
            sha.update(block)
    finally:
        os.close(fd)
    return sha.hexdigest(), info.st_size


def scan(root):
    """Every entry under root, keyed by relative path. Symlinks are never followed."""
    root = pathlib.Path(root)
    entries = {}
    pending = ['']
    while pending:
        rel_dir = pending.pop()
        base = root / rel_dir if rel_dir else root
        for name in sorted(os.listdir(base)):
            rel = (rel_dir + '/' + name) if rel_dir else name
            check_relative(rel)
            path = base / name
            info = path.lstat()
            mode = stat.S_IMODE(info.st_mode)
            if stat.S_ISLNK(info.st_mode):
                # Recorded, never traversed: a directory symlink cannot pull
                # content from outside the root into this manifest's coverage.
                entries[rel] = ('l', 0, 0, quote(os.readlink(path)), info.st_uid)
            elif stat.S_ISDIR(info.st_mode):
                entries[rel] = ('d', mode, 0, '-', info.st_uid)
                pending.append(rel)
            elif stat.S_ISREG(info.st_mode):
                sha, size = digest_file(path)
                entries[rel] = ('f', mode, size, sha, info.st_uid)
            else:
                raise Refused('Refusing unexpected file type in the covered tree: ' + str(path))
    return entries


def resolve_root(root):
    """The root is pinned by absolute path, so a re-pointed release symlink fails."""
    real = pathlib.Path(os.path.realpath(root))
    if not real.is_absolute():
        raise Refused('Root must resolve to an absolute path: ' + str(root))
    try:
        info = real.lstat()
    except OSError as error:
        raise Refused('Cannot read the covered root: ' + str(error)) from None
    if not stat.S_ISDIR(info.st_mode):
        raise Refused('The covered root is not a directory: ' + str(real))
    if info.st_uid != os.getuid():
        raise Refused('Covered root is owned by another account: ' + str(real))
    if stat.S_IMODE(info.st_mode) & 0o022:
        raise Refused('Covered root is group/other writable: ' + str(real))
    return real


def render(root, uid, entries):
    lines = [FORMAT, 'root ' + quote(str(root)), 'uid ' + str(uid), 'count ' + str(len(entries)),
             'root_mode %04o' % stat.S_IMODE(root.stat().st_mode)]
    for rel in sorted(entries, key=lambda name: name.encode()):
        kind, mode, size, extra, _ = entries[rel]
        lines.append('%s %s %s %s %s' % (
            kind, ('%04o' % mode) if kind != 'l' else '-',
            str(size) if kind == 'f' else '-', extra, quote(rel)))
    body = ''.join(line + '\n' for line in lines)
    return body + 'digest ' + hashlib.sha256(body.encode()).hexdigest() + '\n'


def require_hygiene(entries, uid):
    """Freeze-time hygiene, also re-checked on every verification."""
    for rel in sorted(entries):
        kind, mode, _, _, owner = entries[rel]
        if owner != uid:
            raise Refused('Entry is owned by another account: ' + rel)
        if kind != 'l' and mode & 0o022:
            raise Refused('Entry is group/other writable: ' + rel)


def parse(text):
    lines = text.splitlines()
    if len(lines) < 6 or lines[0] != FORMAT:
        raise Refused('Not a ' + FORMAT + ' manifest')
    if not lines[-1].startswith('digest '):
        raise Refused('Manifest has no trailing digest')
    body = ''.join(line + '\n' for line in lines[:-1])
    want = lines[-1].split(' ', 1)[1].strip()
    if hashlib.sha256(body.encode()).hexdigest() != want:
        raise Refused('Manifest digest does not cover its own contents')
    header = {}
    for line in lines[1:5]:
        key, _, value = line.partition(' ')
        header[key] = value
    for key in ('root', 'uid', 'count', 'root_mode'):
        if key not in header:
            raise Refused('Manifest header is missing ' + key)
    entries = {}
    for line in lines[5:-1]:
        fields = line.split(' ')
        if len(fields) != 5:
            raise Refused('Malformed manifest line: ' + line)
        kind, mode, size, extra, path = fields
        rel = check_relative(unquote(path))
        if rel in entries:
            raise Refused('Duplicate manifest entry: ' + rel)
        if kind not in ('f', 'd', 'l'):
            raise Refused('Unknown manifest entry type: ' + kind)
        entries[rel] = (kind, 0 if kind == 'l' else int(mode, 8),
                        int(size) if kind == 'f' else 0, extra, int(header['uid']))
    if len(entries) != int(header['count']):
        raise Refused('Manifest lists %d entries but declares %s' % (len(entries), header['count']))
    return pathlib.Path(unquote(header['root'])), int(header['uid']), int(header['root_mode'], 8), entries


def describe(kind, mode, size, extra):
    if kind == 'l':
        return 'symlink -> ' + unquote(extra)
    if kind == 'd':
        return 'directory mode %04o' % mode
    return 'file mode %04o size %d sha256 %s' % (mode, size, extra)


def compare(recorded, observed):
    problems = []
    for rel in sorted(set(recorded) - set(observed), key=lambda name: name.encode()):
        problems.append('missing: ' + rel)
    for rel in sorted(set(observed) - set(recorded), key=lambda name: name.encode()):
        problems.append('unexpected: ' + rel)
    for rel in sorted(set(recorded) & set(observed), key=lambda name: name.encode()):
        want, got = recorded[rel][:4], observed[rel][:4]
        if want != got:
            problems.append('changed: %s (recorded %s, found %s)'
                            % (rel, describe(*want), describe(*got)))
    return problems


def write_manifest(root, manifest):
    real = resolve_root(root)
    uid = os.getuid()
    entries = scan(real)
    require_hygiene(entries, uid)
    text = render(real, uid, entries)
    target = pathlib.Path(manifest).resolve()
    if target.is_relative_to(real):
        raise Refused('The manifest must live outside the tree it covers: ' + str(target))
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, staged = tempfile.mkstemp(prefix='.manifest-', dir=str(target.parent))
    try:
        with os.fdopen(fd, 'w') as out:
            out.write(text)
        os.chmod(staged, 0o600)
        os.replace(staged, target)
    except BaseException:
        os.path.exists(staged) and os.unlink(staged)
        raise
    print('%s: recorded %d entries' % (target, len(entries)))


def verify_manifest(root, manifest):
    path = pathlib.Path(manifest)
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    except OSError as error:
        raise Refused('Cannot read the manifest: ' + str(error)) from None
    with os.fdopen(fd, 'r') as handle:
        recorded_root, recorded_uid, recorded_mode, recorded = parse(handle.read())
    real = resolve_root(root)
    if recorded_root != real:
        raise Refused('Manifest covers %s but %s was presented (re-pointed release?)'
                      % (recorded_root, real))
    if recorded_uid != os.getuid():
        raise Refused('Manifest was recorded by uid %d but verified as uid %d'
                      % (recorded_uid, os.getuid()))
    if stat.S_IMODE(real.stat().st_mode) != recorded_mode:
        raise Refused('Covered root mode changed: ' + str(real))
    observed = scan(real)
    require_hygiene(observed, recorded_uid)
    problems = compare(recorded, observed)
    if problems:
        raise Refused('%s does not match %s:\n  %s' % (real, path, '\n  '.join(problems[:50])))
    print('%s: %d entries match %s' % (real, len(recorded), path))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('action', choices=('write', 'verify'))
    parser.add_argument('--root', required=True, help='directory tree to cover')
    parser.add_argument('--manifest', required=True, help='manifest file, outside --root')
    args = parser.parse_args(argv)
    if args.action == 'write':
        write_manifest(args.root, args.manifest)
    else:
        verify_manifest(args.root, args.manifest)


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except Refused as refusal:
        print('artifact-manifest: ' + str(refusal), file=sys.stderr)
        raise SystemExit(1)
    except OSError as error:
        print('artifact-manifest: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
