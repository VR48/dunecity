#!/usr/bin/env python3
"""Regression tests for deploy/relay-supervisor.py against temporary fixtures.

No production path, service, port or key is touched: each scenario builds a
private temporary base directory containing a fake release (a copy of the real
artifact-manifest.py plus a REVISION file), a fake gateway key, a fake relay
that serves `status=ok` only when the request carries that key, and a fixture
wrapper that reproduces exactly what run-user-relay.sh does around the
supervisor - one `flock -n` on fd 9, handed over with `exec`.

    python3 deploy/test-relay-supervisor.py

Timings are compressed by the RELAY_WATCHDOG_* variables the supervisor reads;
the whole suite takes well under a minute.
"""
import os
import pathlib
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

if not sys.platform.startswith('linux'):
    print('SKIP: supervisor fixtures require Linux /proc, prctl and flock; run on deployment host')
    raise SystemExit(0)

HERE = pathlib.Path(__file__).parent
SUPERVISOR = str(HERE / 'relay-supervisor.py')
MANIFEST_TOOL = HERE / 'artifact-manifest.py'
KEY = 'ab' * 32
passed = failed = 0

FAKE_RELAY = r'''#!/usr/bin/env python3
"""Stand-in for the Node relay: loopback health that requires the gateway key."""
import http.server
import os
import pathlib
import signal
import socket
import sys
import threading
import time

mode = os.environ.get('FAKE_MODE', 'healthy')
port = int(os.environ['FAKE_PORT'])
state = pathlib.Path(os.environ['FAKE_STATE'])
lifetime = float(os.environ.get('FAKE_LIFETIME', '0'))
with (state / 'starts').open('a') as handle:
    handle.write('%d %s\n' % (os.getpid(), mode))

if mode == 'bad-start':
    sys.stderr.write('fake relay: refusing to start\n')
    raise SystemExit(1)
if mode == 'idle':                      # a decoy with the same argv as the child
    while True:
        time.sleep(1)

if mode == 'hang-once' and not (state / 'hung-once').exists():
    (state / 'hung-once').write_text('1\n')
    # Ignoring SIGTERM forces the watchdog to escalate within its bounded grace.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', port))
    listener.listen(16)
    accepted = []
    while True:
        connection, _ = listener.accept()   # accepted and never answered
        accepted.append(connection)

key = pathlib.Path(os.environ['FAKE_KEY_FILE']).read_text().strip()

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def reply(self, status, body=b''):
        self.send_response(status)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        if self.headers.get('x-dune-gateway') != key:
            self.reply(403)
        elif self.path != '/v1/health':
            self.reply(404)
        else:
            self.reply(200, b'status=ok\nprotocol=5\nrooms=0\nconnections=0\n')

    def log_message(self, *args):
        pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler)
if lifetime > 0:
    def die():
        time.sleep(lifetime)
        os._exit(0)
    threading.Thread(target=die, daemon=True).start()
server.serve_forever()
'''

# Exactly what run-user-relay.sh does around the supervisor: trim, one flock on
# fd 9, redirect to the service log, exec. The supervisor inherits the held lock.
WRAPPER = r'''#!/bin/bash
set -euo pipefail
base=$RELAY_BASE
exec 9>"$base/run.lock"
flock -n 9 || exit 0
exec >>"$base/service.log" 2>&1
exec /usr/bin/python3 "$1"
'''


def report(ok, description, detail=''):
    global passed, failed
    if ok:
        passed += 1
        print('ok   ' + description)
    else:
        failed += 1
        print('FAIL ' + description + (('\n     ' + detail.strip()[-1200:]) if detail else ''))


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def alive(pid):
    try:
        state = pathlib.Path('/proc/%d/stat' % pid).read_text().rsplit(')', 1)[1].split()[0]
        if state == 'Z':
            return False
    except FileNotFoundError:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_for(predicate, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return False


class Scenario:
    """One temporary base directory, fake release, key, relay and wrapper."""

    def __init__(self, mode, lifetime=0.0, max_bad_starts=3):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix='dune-watchdog-'))
        self.base = self.dir / 'base'
        self.state = self.base / 'state'
        self.release = self.base / 'releases' / 'r1'
        (self.release / 'deploy').mkdir(parents=True)
        shutil.copy(MANIFEST_TOOL, self.release / 'deploy' / 'artifact-manifest.py')
        (self.release / 'REVISION').write_text('4940aef0000000000000000000000000000000ab\n')
        subprocess.run(['chmod', '-R', 'go-w', str(self.release)], check=True)
        os.symlink(self.release, self.base / 'current')
        self.state.mkdir()
        self.manifest = self.state / 'release.manifest'
        self.freeze()
        self.key_file = self.dir / 'gateway.key'
        self.key_file.write_text(KEY + '\n')
        os.chmod(self.key_file, 0o400)
        self.relay = self.dir / 'fake-relay.py'
        self.relay.write_text(FAKE_RELAY)
        self.wrapper = self.dir / 'wrapper.sh'
        self.wrapper.write_text(WRAPPER)
        self.marker = self.dir / 'markers'
        self.marker.mkdir()
        self.port = free_port()
        self.log = self.base / 'service.log'
        self.log.write_text('')
        self.env = {
            'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'HOME': str(self.dir),
            'RELAY_BASE': str(self.base),
            'RELAY_STATE_DIR': str(self.state),
            'RELAY_GATEWAY_KEY_FILE': str(self.key_file),
            'RELAY_HEALTH_URL': 'http://127.0.0.1:%d/v1/health' % self.port,
            'RELAY_SUPERVISOR_CHILD': '%s %s' % (sys.executable, self.relay),
            'RELAY_SUPERVISOR_MANIFESTS': '%s=%s' % (self.release, self.manifest),
            'RELAY_WATCHDOG_GRACE_SECONDS': '0.6',
            'RELAY_WATCHDOG_INTERVAL_SECONDS': '0.25',
            'RELAY_WATCHDOG_TIMEOUT_SECONDS': '0.5',
            'RELAY_WATCHDOG_FAILURES': '2',
            'RELAY_WATCHDOG_TERM_GRACE_SECONDS': '0.6',
            'RELAY_WATCHDOG_BACKOFF_BASE_SECONDS': '0.2',
            'RELAY_WATCHDOG_BACKOFF_MAX_SECONDS': '0.5',
            'RELAY_WATCHDOG_MAX_BAD_STARTS': str(max_bad_starts),
            'FAKE_MODE': mode, 'FAKE_PORT': str(self.port),
            'FAKE_STATE': str(self.marker), 'FAKE_KEY_FILE': str(self.key_file),
            'FAKE_LIFETIME': str(lifetime),
        }
        self.process = None
        self.decoy = None

    def freeze(self):
        subprocess.run([sys.executable, str(MANIFEST_TOOL), 'write', '--root',
                        str(self.release), '--manifest', str(self.manifest)],
                       check=True, stdout=subprocess.DEVNULL)

    def start(self):
        self.process = subprocess.Popen(['/bin/bash', str(self.wrapper), SUPERVISOR],
                                        env=self.env, stdin=subprocess.DEVNULL)
        return self.process

    def cron_run(self):
        """A second wrapper invocation, exactly as the minute cron line does."""
        return subprocess.run(['/bin/bash', str(self.wrapper), SUPERVISOR],
                              env=self.env, stdin=subprocess.DEVNULL, timeout=20)

    def start_decoy(self):
        """A process with the same argv as the child, which must never be killed."""
        env = dict(self.env, FAKE_MODE='idle', FAKE_STATE=str(self.dir / 'decoy'))
        (self.dir / 'decoy').mkdir(exist_ok=True)
        self.decoy = subprocess.Popen([sys.executable, str(self.relay)], env=env,
                                      stdin=subprocess.DEVNULL)
        return self.decoy

    def text(self):
        return self.log.read_text()

    def starts(self):
        path = self.marker / 'starts'
        return path.read_text().splitlines() if path.exists() else []

    def status(self):
        path = self.state / 'supervisor-status'
        if not path.exists():
            return {}
        return dict(line.split('=', 1) for line in path.read_text().splitlines() if '=' in line)

    def child_pids(self):
        return [int(line.split()[0]) for line in self.starts()]

    def stop(self):
        for process in (self.process, self.decoy):
            if process is None or process.poll() is not None:
                continue
            process.send_signal(signal.SIGKILL)
            process.wait()
        for pid in self.child_pids():
            if alive(pid):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
        shutil.rmtree(self.dir, ignore_errors=True)


# --- a healthy relay is left alone ------------------------------------------
scenario = Scenario('healthy')
try:
    scenario.start()
    report(wait_for(lambda: 'healthy' in scenario.text()),
           'a healthy relay reaches health through the authenticated probe', scenario.text())
    time.sleep(2.0)
    report(len(scenario.starts()) == 1, 'a healthy relay is started exactly once',
           repr(scenario.starts()))
    report('terminating' not in scenario.text() and 'killing' not in scenario.text(),
           'a healthy relay is never terminated', scenario.text())
    pid = scenario.child_pids()[0]
    report(alive(pid), 'the healthy child is still running')
    status = scenario.status()
    report(status.get('child_pid') == str(pid) and status.get('last_health') == 'ok'
           and status.get('revision', '').startswith('4940aef')
           and status.get('release') == str(scenario.release),
           'pid, release and revision are observable in the status file', repr(status))
    report(KEY not in scenario.text() and KEY not in repr(status),
           'the gateway key never reaches the log or the status file')
    cmdlines = ''
    for check in (scenario.process.pid, pid):
        cmdlines += pathlib.Path('/proc/%d/cmdline' % check).read_bytes().decode('utf-8', 'replace')
    report(KEY not in cmdlines, 'the gateway key is on no command line', cmdlines)
    report('artifact_verified' in scenario.text(),
           'artifacts are verified before the relay is started')

    # The minute cron line must not start a second supervisor or a second relay.
    done = scenario.cron_run()
    time.sleep(0.5)
    report(done.returncode == 0 and len(scenario.starts()) == 1,
           'a concurrent cron run is a no-op while the supervisor holds the flock',
           'rc=%d starts=%r' % (done.returncode, scenario.starts()))

    # --- the supervisor never leaves its child behind -----------------------
    scenario.process.send_signal(signal.SIGTERM)
    report(scenario.process.wait(timeout=15) == 0, 'the supervisor exits 0 on SIGTERM')
    report(wait_for(lambda: not alive(pid), 5.0),
           'the supervisor terminates its child when it is asked to exit')
    report('child_terminated_on_exit' in scenario.text(), 'that termination is logged',
           scenario.text())
    report(scenario.status().get('last_health') == 'stopped',
           'the status file records the stop', repr(scenario.status()))
finally:
    scenario.stop()

# Supervisor SIGKILL/OOM cannot run finally: the child must die in the kernel.
scenario = Scenario('healthy')
try:
    scenario.start()
    report(wait_for(lambda: scenario.status().get('last_health') == 'ok'),
           'parent-death fixture reaches health', scenario.text())
    pid = scenario.child_pids()[0]
    scenario.process.kill()
    scenario.process.wait(timeout=5)
    report(wait_for(lambda: not alive(pid), 5),
           'kernel terminates the child after supervisor SIGKILL')
    scenario.start()
    report(wait_for(lambda: len(scenario.starts()) >= 2 and scenario.status().get('last_health') == 'ok'),
           'a new supervisor can recover after abrupt supervisor death', scenario.text())
finally:
    scenario.stop()

# --- an exited relay is restarted -------------------------------------------
scenario = Scenario('healthy', lifetime=1.2)
try:
    scenario.start()
    report(wait_for(lambda: len(scenario.starts()) >= 2, 20.0),
           'a relay that exits on its own is restarted', scenario.text())
    report(wait_for(lambda: scenario.text().count('healthy') >= 2, 20.0),
           'the restarted relay reaches health again', scenario.text())
    report('child_exited status=0' in scenario.text(), 'the exit is logged with its status',
           scenario.text())
    report('backoff' in scenario.text(), 'the restart goes through bounded backoff')
finally:
    scenario.stop()

# --- a live but hung relay is detected and replaced -------------------------
scenario = Scenario('hang-once')
try:
    decoy = scenario.start_decoy()
    scenario.start()
    report(wait_for(lambda: 'health_failed' in scenario.text(), 20.0),
           'a hung relay fails the bounded health probe', scenario.text())
    report(wait_for(lambda: 'healthy' in scenario.text(), 25.0),
           'the watchdog recovers a hung relay by restarting it', scenario.text())
    hung = scenario.child_pids()[0]
    report(len(scenario.starts()) == 2 and not alive(hung),
           'the hung process is gone and exactly one replacement was started',
           repr(scenario.starts()))
    text = scenario.text()
    report('consecutive=2 of=2' in text and 'hung after 2 consecutive' in text,
           'termination needs repeated failures, not one', text)
    report('killing' in text and 'term_grace_expired' in text,
           'a child that ignores SIGTERM is killed after the bounded grace', text)
    report(re.search(r'terminating pid=%d .*method=(pidfd|unreaped_child_pid)' % hung, text)
           is not None, 'only the supervisor\'s own child handle is signalled', text)
    report(decoy.poll() is None and alive(decoy.pid),
           'a foreign process with the same command line is left untouched')
finally:
    scenario.stop()

# --- a relay that never starts is retried a bounded number of times ---------
scenario = Scenario('bad-start', max_bad_starts=3)
try:
    scenario.start()
    report(scenario.process.wait(timeout=30) == 1,
           'the supervisor gives up with a non-zero status after bad starts', scenario.text())
    report(len(scenario.starts()) == 3, 'the bad-start budget is bounded and respected',
           repr(scenario.starts()))
    report('bad_start_budget_exhausted' in scenario.text(), 'giving up is logged',
           scenario.text())
    source = pathlib.Path(SUPERVISOR).read_text()
    report('fcntl' not in source and 'LOCK_EX' not in source and 'flock(' not in source,
           'the supervisor takes no lock of its own, so the released lock lets cron retry')
finally:
    scenario.stop()

# --- artifacts that change under a running relay stop the restart -----------
scenario = Scenario('healthy', lifetime=1.2)
try:
    scenario.start()
    report(wait_for(lambda: 'spawned' in scenario.text()), 'the first relay was spawned')
    (scenario.release / 'REVISION').write_text('0000000000000000000000000000000000000000\n')
    report(scenario.process.wait(timeout=30) == 1,
           'a changed release refuses the restart and exits non-zero', scenario.text())
    report('artifact_refused' in scenario.text() and 'refusing_start' in scenario.text(),
           'the refusal names artifact verification', scenario.text())
    report(len(scenario.starts()) == 1, 'no relay is started from changed artifacts',
           repr(scenario.starts()))
finally:
    scenario.stop()

# --- the cron/child wrapper keeps its two roles apart -----------------------
wrapper = (HERE / 'run-user-relay.sh').read_text()
report(subprocess.run(['bash', '-n', str(HERE / 'run-user-relay.sh')]).returncode == 0,
       'run-user-relay.sh parses')
code = '\n'.join(line for line in wrapper.splitlines() if not line.lstrip().startswith('#'))
report(code.count('flock') == 1 and 'flock -n 9' in code,
       'the wrapper takes exactly one lock')
child_branch = code.split('--exec-child ]]; then', 1)[1].split('\nfi\n', 1)[0]
report('flock' not in child_branch and 'run.lock' not in child_branch,
       'the spawned child takes no lock, so there is no nested lock to deadlock on')
report('exec env -i' in child_branch and 'sandbox.py' in child_branch
       and 'src/index.js' in child_branch,
       'the child is still the clean-env Landlock launcher chain, reached by exec')
supervise_branch = code.split('\nfi\n', 1)[1]
report(supervise_branch.count('artifact-manifest.py') == 2
       and supervise_branch.index('artifact-manifest.py')
       < supervise_branch.index('relay-supervisor.py'),
       'the runtime and the release are both verified before the supervisor is exec\'d')
report(supervise_branch.index('trim-user-log.py') < supervise_branch.index('flock'),
       'the log is trimmed before the lock, so it stays bounded while the supervisor holds it')
report('exec env -i' in supervise_branch and 'relay-supervisor.py' in supervise_branch,
       'the supervisor is exec\'d, so it inherits the held lock descriptor')

print('\n%d passed, %d failed' % (passed, failed))
raise SystemExit(1 if failed else 0)
