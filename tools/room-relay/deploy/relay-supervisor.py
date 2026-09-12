#!/usr/bin/env python3
"""Persistent user-owned watchdog for the loopback relay.

Started by deploy/run-user-relay.sh, which already holds the single exclusive
flock on $base/run.lock and passes the held descriptor across its exec. This
process therefore never locks anything itself - there is no nested lock and no
deadlock - and the once-a-minute cron invocation of that wrapper is a no-op
(`flock -n` fails, the wrapper exits 0) for as long as this supervisor lives.
If this supervisor dies, the lock is released with its last descriptor and the
next minute's cron run takes over. That, plus the @reboot line, is the whole
liveness story; nothing depends on a login shell, so an SSH disconnect is
irrelevant (this process detaches from any controlling terminal at startup).

What it does, in a loop:

  * verifies the frozen release and the pinned runtime against their manifests
    and refuses to start anything when they do not match (fail closed);
  * spawns exactly one child: the Landlock launcher chain in
    run-user-relay.sh --exec-child, whose every step is an exec, so the child
    PID this process holds *is* the Node process;
  * probes http://127.0.0.1:18787/v1/health with the gateway key read from its
    private file and sent as a header - never as an argument, never logged -
    and requires a `status=ok` line, with a bounded per-operation timeout;
  * after a launch grace period, treats N consecutive probe failures as a hung
    relay and terminates *that* child only, by pidfd when the kernel and Python
    support it and otherwise by signalling its own unreaped child PID (a PID
    that cannot have been reused while this process has not waited on it).
    No pattern matching, no pkill, no PID file read back from disk;
  * restarts with bounded exponential backoff, gives up after a bounded number
    of starts that never reached health (cron retries a minute later), and
    terminates the child if it is itself asked to exit.

Every environment variable below has a production default; they exist so the
regression tests in deploy/test-relay-supervisor.py can drive this against
fixtures with short timings instead of against the real service.
"""
import ctypes
import http.client
import os
import pathlib
import re
import shlex
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
import urllib.parse

BASE = pathlib.Path(os.environ.get('RELAY_BASE', '/home/dunelegacy-deploy/dunecity-relay'))
PRIVATE = pathlib.Path(os.environ.get('RELAY_PRIVATE', '/var/www/data/dunecity-relay'))
KEY_FILE = pathlib.Path(os.environ.get('RELAY_GATEWAY_KEY_FILE', str(PRIVATE / 'gateway.key')))
HEALTH_URL = os.environ.get('RELAY_HEALTH_URL', 'http://127.0.0.1:18787/v1/health')
STATE = pathlib.Path(os.environ.get('RELAY_STATE_DIR', str(BASE / 'state')))
KEY_PATTERN = re.compile(r'^[0-9a-f]{64}$')
# Pin the release for this supervisor's lifetime. A rollout stops this supervisor
# and starts the wrapper from the new release; current is never followed again.
PINNED_RELEASE = pathlib.Path(os.path.realpath(BASE / 'current'))


def number(name, default, low, high):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = float(raw)
    except ValueError:
        return default
    return min(max(value, low), high)


GRACE = number('RELAY_WATCHDOG_GRACE_SECONDS', 25.0, 0.2, 600.0)
INTERVAL = number('RELAY_WATCHDOG_INTERVAL_SECONDS', 15.0, 0.05, 600.0)
TIMEOUT = number('RELAY_WATCHDOG_TIMEOUT_SECONDS', 5.0, 0.05, 120.0)
FAILURES = int(number('RELAY_WATCHDOG_FAILURES', 3, 1, 100))
TERM_GRACE = number('RELAY_WATCHDOG_TERM_GRACE_SECONDS', 10.0, 0.1, 120.0)
BACKOFF_MAX = number('RELAY_WATCHDOG_BACKOFF_MAX_SECONDS', 60.0, 0.1, 3600.0)
BACKOFF_BASE = number('RELAY_WATCHDOG_BACKOFF_BASE_SECONDS', 2.0, 0.05, 600.0)
MAX_BAD_STARTS = int(number('RELAY_WATCHDOG_MAX_BAD_STARTS', 5, 1, 100))


def log(event, **fields):
    stamp = time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime())
    parts = ' '.join('%s=%s' % (key, value) for key, value in fields.items())
    sys.stdout.write('%s relay-supervisor %s %s\n' % (stamp, event, parts))
    sys.stdout.flush()


def release_path():
    return PINNED_RELEASE


def manifest_pairs():
    """(root, manifest) pairs to verify before every start. Empty only for tests."""
    raw = os.environ.get('RELAY_SUPERVISOR_MANIFESTS')
    if raw is None:
        return [(BASE / 'runtime', STATE / 'runtime.manifest'),
                (release_path(), STATE / 'release.manifest')]
    pairs = []
    for item in [part for part in raw.split(',') if part.strip()]:
        root, _, manifest = item.partition('=')
        pairs.append((pathlib.Path(root), pathlib.Path(manifest)))
    return pairs


def child_command():
    raw = os.environ.get('RELAY_SUPERVISOR_CHILD')
    if raw:
        return shlex.split(raw)
    return ['/bin/bash', str(release_path() / 'deploy' / 'run-user-relay.sh'), '--exec-child', str(release_path())]


def verify_artifacts():
    """Fail closed: the release and runtime must still be the frozen artifacts."""
    pairs = manifest_pairs()
    if not pairs:
        log('artifact_verification_disabled', note='fixture_mode')
        return True
    tool = release_path() / 'deploy' / 'artifact-manifest.py'
    for root, manifest in pairs:
        done = subprocess.run(['/usr/bin/python3', str(tool), 'verify',
                               '--root', str(root), '--manifest', str(manifest)],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        first = (done.stdout or '').strip().splitlines()
        if done.returncode != 0:
            log('artifact_refused', root=root, manifest=manifest,
                detail=shlex.quote(' | '.join(first)[:400]))
            return False
        log('artifact_verified', root=root)
    return True


def gateway_key():
    """The key is read from its private file on every probe and only ever sent
    as a request header; it is never an argument and never written to the log."""
    try:
        fd = os.open(KEY_FILE, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    except OSError as error:
        return None, 'unreadable(%s)' % error.errno
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            return None, 'not_a_regular_file'
        raw = os.read(fd, 4096).decode('ascii', 'replace').strip()
    finally:
        os.close(fd)
    if not KEY_PATTERN.match(raw):
        return None, 'malformed'
    return raw, 'ok'


def probe():
    """(state, detail) where state is 'ok', 'fail' or 'skip'."""
    key, status = gateway_key()
    if key is None:
        # A missing key is a configuration fault, not a hung relay: restarting
        # cannot fix it (the relay refuses to start without one), so this does
        # not count towards the failure budget.
        return 'skip', 'key_' + status
    parts = urllib.parse.urlsplit(HEALTH_URL)
    connection = http.client.HTTPConnection(parts.hostname or '127.0.0.1',
                                            parts.port or 80, timeout=TIMEOUT)
    try:
        connection.request('GET', parts.path or '/',
                           headers={'Host': '127.0.0.1', 'Connection': 'close',
                                    'x-dune-gateway': key})
        response = connection.getresponse()
        body = response.read(4096).decode('ascii', 'replace')
        if response.status != 200:
            return 'fail', 'http_%d' % response.status
        if any(line.strip() == 'status=ok' for line in body.splitlines()):
            return 'ok', 'status_ok'
        return 'fail', 'no_status_ok'
    except (OSError, socket.timeout, http.client.HTTPException) as error:
        # Exception text only; a request header is never echoed into the log.
        return 'fail', type(error).__name__
    finally:
        try:
            connection.close()
        except OSError:
            pass


class Child:
    """One spawned relay process, terminated only through its own handle.

    pidfd_open pins the process identity, so a signal cannot land on a reused
    PID. Where pidfd is unavailable the fallback is still reuse-safe, because
    this process is the direct parent and has not reaped the child: the PID is
    held by the (possibly zombie) child until poll() collects it.
    """

    def __init__(self, command, log_fd):
        self.command = command
        parent_pid = os.getpid()
        def die_with_parent():
            # The supervisor is single-threaded. Set this between fork and exec,
            # then check for a parent that died before prctl installed the signal.
            # SIGKILL also covers a stopped/hung child and supervisor SIGKILL/OOM.
            libc = ctypes.CDLL(None, use_errno=True)
            if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0:
                raise OSError(ctypes.get_errno(), 'Cannot set parent-death signal')
            if os.getppid() != parent_pid:
                os._exit(1)
        self.process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=log_fd, stderr=log_fd,
            close_fds=True, start_new_session=True,
            preexec_fn=die_with_parent if sys.platform.startswith('linux') else None)
        self.pidfd = None
        if hasattr(os, 'pidfd_open') and hasattr(signal, 'pidfd_send_signal'):
            try:
                self.pidfd = os.pidfd_open(self.process.pid)
            except OSError:
                self.pidfd = None
        self.started = time.monotonic()
        self.reached_health = False

    @property
    def pid(self):
        return self.process.pid

    def exit_status(self):
        return self.process.poll()

    def _signal(self, number):
        if self.process.poll() is not None:
            return
        if self.pidfd is not None:
            try:
                signal.pidfd_send_signal(self.pidfd, number)
                return
            except ProcessLookupError:
                return
            except OSError:
                pass
        try:
            self.process.send_signal(number)
        except (ProcessLookupError, OSError):
            pass

    def terminate(self, reason):
        """SIGTERM, then SIGKILL after a bounded grace. Never a pattern kill."""
        if self.process.poll() is None:
            log('terminating', pid=self.pid, reason=reason, method='pidfd'
                if self.pidfd is not None else 'unreaped_child_pid')
            self._signal(signal.SIGTERM)
            deadline = time.monotonic() + TERM_GRACE
            while time.monotonic() < deadline and self.process.poll() is None:
                time.sleep(0.05)
            if self.process.poll() is None:
                log('killing', pid=self.pid, reason='term_grace_expired')
                self._signal(signal.SIGKILL)
        status = self.process.wait()
        self.close()
        return status

    def close(self):
        if self.pidfd is not None:
            try:
                os.close(self.pidfd)
            except OSError:
                pass
            self.pidfd = None


def revision():
    try:
        text = (release_path() / 'REVISION').read_text().strip()
    except OSError:
        return 'unknown'
    return text if re.match(r'^[0-9a-zA-Z._-]{1,64}$', text) else 'unreadable'


def write_status(fields):
    """Observable release/pid state for an operator. Never any secret."""
    try:
        STATE.mkdir(parents=True, exist_ok=True)
        fd, staged = tempfile.mkstemp(prefix='.status-', dir=str(STATE))
        with os.fdopen(fd, 'w') as out:
            for key, value in fields.items():
                out.write('%s=%s\n' % (key, value))
        os.chmod(staged, 0o600)
        os.replace(staged, STATE / 'supervisor-status')
    except OSError as error:
        log('status_write_failed', errno=getattr(error, 'errno', '?'))


def main():
    # Detach from any controlling terminal so a closed SSH session cannot take
    # the relay down with it; under cron there is none to begin with.
    try:
        os.setsid()
    except OSError:
        pass
    command = child_command()
    log('starting', supervisor_pid=os.getpid(), release=release_path(), revision=revision(),
        health_url=HEALTH_URL, grace=GRACE, interval=INTERVAL, timeout=TIMEOUT,
        failures=FAILURES, max_bad_starts=MAX_BAD_STARTS)
    child = None
    state = {'stop': False}

    def request_stop(number, _frame):
        state['stop'] = True
        state['signal'] = number

    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, request_stop)

    starts = 0
    bad_starts = 0
    restarts = 0
    status_fields = {}

    def publish(**extra):
        status_fields.update(extra)
        write_status({'supervisor_pid': os.getpid(), 'release': release_path(),
                      'revision': revision(), 'runtime': BASE / 'runtime',
                      'starts': starts, 'restarts': restarts, 'bad_starts': bad_starts,
                      **status_fields})

    exit_code = 0
    try:
        while not state['stop']:
            if not verify_artifacts():
                log('refusing_start', reason='artifact_verification_failed')
                publish(child_pid='none', last_health='refused',
                        last_event='artifact_verification_failed')
                exit_code = 1
                break
            if bad_starts >= MAX_BAD_STARTS:
                log('giving_up', reason='bad_start_budget_exhausted', bad_starts=bad_starts)
                publish(child_pid='none', last_health='unknown',
                        last_event='bad_start_budget_exhausted')
                exit_code = 1
                break
            starts += 1
            child = Child(command, sys.stdout.fileno())
            log('spawned', pid=child.pid, start=starts, command=shlex.join(command))
            publish(child_pid=child.pid, last_health='pending', last_event='spawned',
                    child_started_unix=int(time.time()))
            consecutive = 0
            reason = None
            tick = min(INTERVAL, 0.25)
            # Probing only begins after the launch grace period, so a relay that
            # is still binding its socket is never mistaken for a hung one.
            next_probe_at = child.started + GRACE
            while not state['stop']:
                time.sleep(tick)
                exited = child.exit_status()
                if exited is not None:
                    reason = 'child_exited status=%s' % exited
                    break
                now = time.monotonic()
                if now < next_probe_at:
                    continue
                next_probe_at = now + INTERVAL
                result, detail = probe()
                if result == 'ok':
                    consecutive = 0
                    if not child.reached_health:
                        child.reached_health = True
                        bad_starts = 0
                        log('healthy', pid=child.pid)
                    publish(child_pid=child.pid, last_health='ok', last_event='healthy',
                            last_health_unix=int(time.time()))
                    continue
                if result == 'skip':
                    log('health_skipped', pid=child.pid, detail=detail)
                    publish(child_pid=child.pid, last_health='skipped', last_event=detail)
                    continue
                consecutive += 1
                log('health_failed', pid=child.pid, detail=detail,
                    consecutive=consecutive, of=FAILURES)
                publish(child_pid=child.pid, last_health='fail', last_event=detail)
                if consecutive >= FAILURES:
                    reason = 'hung after %d consecutive failed health probes' % consecutive
                    break
            if state['stop']:
                break
            if not child.reached_health:
                bad_starts += 1
            status = child.terminate(reason or 'restart')
            log('child_gone', pid=child.pid, reason=reason, status=status,
                reached_health=child.reached_health, bad_starts=bad_starts)
            child.close()
            child = None
            restarts += 1
            delay = min(BACKOFF_MAX, BACKOFF_BASE * (2 ** min(bad_starts, 10))) if bad_starts \
                else BACKOFF_BASE
            publish(child_pid='none', last_health='restarting',
                    last_event='backoff_%.2fs' % delay)
            log('backoff', seconds='%.2f' % delay, bad_starts=bad_starts)
            deadline = time.monotonic() + delay
            while not state['stop'] and time.monotonic() < deadline:
                time.sleep(min(0.1, delay))
    finally:
        if child is not None:
            # The supervisor never outlives its child, and never leaves one behind.
            status = child.terminate('supervisor_exiting')
            log('child_terminated_on_exit', pid=child.pid, status=status)
        last = ('signal_%s' % state.get('signal', 'none')) if state['stop'] \
            else ('exit_%d' % exit_code)
        publish(child_pid='none', last_health='stopped', last_event=last)
        log('stopped', supervisor_pid=os.getpid(), signal=state.get('signal', 'none'),
            exit_code=exit_code, starts=starts, restarts=restarts)
    raise SystemExit(exit_code)


if __name__ == '__main__':
    main()
