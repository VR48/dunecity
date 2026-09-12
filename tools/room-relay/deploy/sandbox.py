#!/usr/bin/env python3
"""Fail-closed Landlock ABI4+ launcher; no root or user namespace is needed."""
import ctypes
import os
import pathlib
import platform
import sys

READ_FILE, READ_DIR, EXECUTE = 4, 8, 1
ALL_FS_V4 = (1 << 15) - 1

class Ruleset(ctypes.Structure):
    _fields_ = [('fs', ctypes.c_uint64), ('net', ctypes.c_uint64)]
class PathRule(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('access', ctypes.c_uint64), ('fd', ctypes.c_int32)]
class NetRule(ctypes.Structure):
    _fields_ = [('access', ctypes.c_uint64), ('port', ctypes.c_uint64)]

def restrict(read_paths, executable, bind_port=18787):
    if platform.machine() not in ('x86_64', 'aarch64'):
        raise RuntimeError('Unsupported Landlock syscall architecture')
    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall.restype = ctypes.c_long
    def call(number, *args):
        result = libc.syscall(number, *args)
        if result < 0:
            raise OSError(ctypes.get_errno(), 'Landlock restriction failed')
        return result
    abi = call(444, 0, 0, 1)
    if abi < 4:
        raise RuntimeError('Landlock ABI4 or later is required; refusing unsandboxed startup')
    rules = Ruleset(ALL_FS_V4, 3)
    fd = call(444, ctypes.byref(rules), ctypes.sizeof(rules), 0)
    try:
        for path in read_paths + [executable]:
            real = pathlib.Path(path).resolve(strict=True)
            access = READ_FILE | (READ_DIR if real.is_dir() else 0)
            if real == pathlib.Path(executable).resolve(strict=True):
                access |= EXECUTE
            # ELF interpreter must be executable too, not the whole library hierarchy.
            if real.name.startswith('ld-linux'):
                access |= EXECUTE
            pathfd = os.open(real, os.O_PATH | os.O_CLOEXEC)
            try:
                rule = PathRule(access, pathfd)
                call(445, fd, 1, ctypes.byref(rule), 0)
            finally:
                os.close(pathfd)
        for access, port in [(1, bind_port), (2, 443), (2, 53)]:
            rule = NetRule(access, port)
            call(445, fd, 2, ctypes.byref(rule), 0)
        if libc.prctl(38, 1, 0, 0, 0):  # PR_SET_NO_NEW_PRIVS
            raise OSError(ctypes.get_errno(), 'Cannot set no_new_privs')
        call(446, fd, 0)
    finally:
        os.close(fd)

def main():
    if len(sys.argv) < 2:
        raise RuntimeError('Expected the Node executable and arguments')
    executable = pathlib.Path(sys.argv[1]).resolve(strict=True)
    base = pathlib.Path('/home/dunelegacy-deploy/dunecity-relay')
    private = pathlib.Path('/var/www/data/dunecity-relay')
    required = [base / 'current', private / 'gateway.key', private / 'analytics.key',
                '/usr/lib/x86_64-linux-gnu', '/etc/ssl/certs', '/etc/ssl/openssl.cnf', '/etc/ld.so.cache',
                '/etc/nsswitch.conf', '/etc/resolv.conf', '/etc/hosts',
                '/lib64/ld-linux-x86-64.so.2', '/dev/urandom', '/dev/null']
    optional = ['/proc/self/maps', '/proc/meminfo', '/sys/devices/system/cpu']
    read_paths = required + [p for p in optional if pathlib.Path(p).exists()]
    restrict(read_paths, executable)
    os.execv(str(executable), [str(executable), *sys.argv[2:]])

if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Relay sandbox refused startup: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
