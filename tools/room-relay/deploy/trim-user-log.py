#!/usr/bin/env python3
"""Bound the append-only user service log; called by the minute watchdog."""
import os
import pathlib
import stat
import tempfile
base = pathlib.Path('/home/dunelegacy-deploy/dunecity-relay')
path = base / 'service.log'
fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
try:
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
        raise RuntimeError('Refusing non-owned service log')
    if info.st_size > 8 * 1024 * 1024:
        os.lseek(fd, -1024 * 1024, os.SEEK_END)
        tail = os.read(fd, 1024 * 1024)
        staged_fd, staged_name = tempfile.mkstemp(prefix='.log-tail-', dir=base)
        with os.fdopen(staged_fd, 'wb') as output:
            output.write(tail)
        os.replace(staged_name, base / 'service.previous.log')
        # Node opened this inode with O_APPEND, so subsequent writes restart at the new end.
        os.ftruncate(fd, 0)
finally:
    os.close(fd)
