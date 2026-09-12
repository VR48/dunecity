# Deployment paths

For the current restricted Apache account, use the HTTPS polling deployment in
[https-relay-deployment.md](../../../docs/https-relay-deployment.md). Its public
file is `relay/http-gateway.php` alongside `http-gateway.htaccess` installed as
`relay/.htaccess`; it uses private loopback18787 and requires no Apache restart.
The administrator bootstrap below is a separate, optional WebSocket deployment.
Do not run it or mix its8787/WSS settings into the polling deployment.

# Startup gates and the watchdog for the user-owned polling relay

These apply to the unprivileged HTTPS-polling deployment under
`/home/dunelegacy-deploy/dunecity-relay` (`$base` below), which is started by
`deploy/run-user-relay.sh` from the deployment account's own crontab. They are
independent of the administrator bootstrap in the next section.

## Frozen release and pinned runtime

`deploy/artifact-manifest.py` records and re-checks one directory tree exactly:
every entry's type, mode and owner, each symlink's target, and each regular
file's size and SHA256. Verification is set equality, so a missing entry, an
extra entry, a retargeted symlink, a type change, a mode change and edited
content are all refused; unexpected file types (sockets, fifos, devices) and
group/other-writable entries are refused too. Symlinked directories are
recorded as links and never traversed, so nothing outside the tree can be
pulled into - or quietly out of - coverage, and a manifest entry naming `..` or
an absolute path is rejected on sight. Two trees are covered, with nothing
excluded from either: the pinned runtime (`$base/runtime`, i.e. the Node
binary, its libraries and npm) and the frozen release (`$base/releases/<rev>`,
i.e. `src/`, `deploy/`, `package.json`, `package-lock.json`, `REVISION` and the
installed `node_modules`). There are no transient files inside either tree -
the service log, the lock and the manifests all live in `$base` itself - and
the tool deliberately supports no exclusions.

Freeze both after installing or updating a release, from the deployment account:

```sh
base=/home/dunelegacy-deploy/dunecity-relay
release=$(readlink -f "$base/current")
chmod -R go-w "$base/runtime" "$release"
python3 "$release/deploy/artifact-manifest.py" write \
  --root "$base/runtime"  --manifest "$base/state/runtime.manifest"
python3 "$release/deploy/artifact-manifest.py" write \
  --root "$release" --manifest "$base/state/release.manifest"
```

The manifests live in `$base/state/`, outside both covered trees. Each records
the covered root as an absolute path, so `$base/current` re-pointed at a
different release is refused even if that release's contents are identical: an
upgrade means installing the new release, re-freezing, and letting the next
minute's cron run pick it up.

`run-user-relay.sh` verifies both manifests before it starts anything and the
supervisor verifies them again before every (re)start, so a release that drifts
while running is not restarted from.

**What this is not.** It is drift and damage detection, not tamper resistance
against the account that owns the files: that account can rewrite a manifest as
easily as the tree it covers, and the wrapper that performs the check is itself
part of the release. Root-owned integrity is `bootstrap.sh`'s job. Hard links
are not distinguished from ordinary files (content is hashed either way).

## Watchdog

`deploy/run-user-relay.sh` has two roles:

* with no argument it is the cron entry point. It trims the service log, takes
  the single exclusive `flock` on `$base/run.lock` (fd 9), redirects to
  `$base/service.log`, verifies the two manifests and `exec`s the supervisor,
  which inherits the held descriptor. Trimming happens before the lock because
  the supervisor holds that lock for its whole life.
* with `--exec-child VERIFIED_RELEASE` it is the launcher the supervisor spawns: no lock at all
  (so there is no nested lock to deadlock on), and the unchanged clean-env
  `sandbox.py` Landlock chain. Every step is an `exec`, so the PID the
  supervisor holds is the Node process itself.

```
@reboot     /bin/bash /home/dunelegacy-deploy/dunecity-relay/current/deploy/run-user-relay.sh
* * * * *   /bin/bash /home/dunelegacy-deploy/dunecity-relay/current/deploy/run-user-relay.sh
```

The minute line only ever restores a supervisor that is gone: while one is
alive, `flock -n` fails and the wrapper exits 0. Neither line needs a login
shell, and the supervisor detaches from any controlling terminal, so closing an
SSH session does not take the relay down.

`deploy/relay-supervisor.py` then, in a loop: probes
`http://127.0.0.1:18787/v1/health` with the gateway key read from
`/var/www/data/dunecity-relay/gateway.key` and sent as the `x-dune-gateway`
header the relay already requires - never an argument, never logged - and
requires a `status=ok` line in the `text/plain` body (a 2xx alone is not
health). Probing starts only after a launch grace period, and it takes
`RELAY_WATCHDOG_FAILURES` consecutive failures, each with a bounded
per-operation timeout, to declare the relay hung. Termination is always of its
own child handle: a pidfd where the kernel and Python provide one, otherwise a
signal to its own unreaped child PID, which cannot have been reused while this
process has not waited on it. `SIGTERM`, then `SIGKILL` after a bounded grace.
No pattern matching, no `pkill`, no PID file read back from disk. Restarts use
bounded exponential backoff; after `RELAY_WATCHDOG_MAX_BAD_STARTS` starts that
never reached health it gives up and exits non-zero, releasing the lock so the
next minute's cron run retries - a bounded, once-a-minute retry rate instead of
a hot loop. If it is signalled, it terminates its child before exiting. Linux also installs a
parent-death SIGKILL before exec, so abrupt supervisor death or OOM cannot leave
an orphan relay holding the port while the next supervisor starts.

Defaults (grace 25s, interval 15s, timeout 5s, 3 failures, 10s termination
grace, backoff 2s doubling to 60s, 5 bad starts) are overridable through the
`RELAY_WATCHDOG_*` variables, which exist so the tests can compress timings.

`$base/state/supervisor-status` (0600) carries the supervisor PID, the child
PID, the release path, the revision, start/restart counters and the last health
outcome. It contains no secret.

A gateway key file that is missing or malformed is reported and does **not**
count as a failed probe: the relay cannot start without one, so restarting
could not fix it. That means a relay whose key file disappeared after startup
is not watchdogged until the key is restored.

## Tests

Both suites use fixtures in a private temporary directory, need no privileges,
and touch no production path, port, key or service:

```sh
python3 deploy/test-artifact-manifest.py
python3 deploy/test-relay-supervisor.py
```

The second builds a fake release, a fake gateway key and a fake relay that
answers health only when the request carries that key, then drives the real
supervisor through healthy, exited, hung, bad-start and changed-artifact
scenarios (including a decoy process with the same command line, which must
survive).

# Production relay on the existing website host

The application uses `https://dunelegacy.com/relay`; Apache terminates the existing
certificate and proxies to the dedicated unprivileged service on loopback8787.
The service holds no SSH/deployment keys or database credentials. The root-readable
environment file supplies a dedicated HMAC key shared with the PHP receiver.

This is an administrator-only bootstrap, not a privilege grant to the restricted
website deployment account. The operator must first copy the release bundle into
a root-owned directory under `/root`, verify its independently supplied archive
SHA256, extract it as root, and ensure neither files nor parent directories are
group/other writable. Only then execute its reviewed script. The third argument
is the independently supplied SHA256 of `SHA256SUMS`, which covers every supplied
source, package lock, deployment template **and the `REVISION` file**. Do not run
an uploaded mutable script directly with sudo. Never paste the analytics key into
a task or log.

## Building the bundle

The bundle must carry its own revision, so that the release label is authenticated
by the manifest instead of being whatever was typed on the command line. From a
clean checkout of the commit being released:

Run these commands from the game checkout on Linux (GNU tools):

```sh
revision=$(git rev-parse HEAD)
staging=$(mktemp -d)
git archive "$revision" tools/room-relay | tar -xf - -C "$staging"
bundle="$staging/tools/room-relay"
printf '%s\n' "$revision" > "$bundle/REVISION"
(cd "$bundle"; find . -type f ! -name SHA256SUMS -print0 \
  | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS)
tar -czf "relay-$revision.tar.gz" -C "$bundle" .
sha256sum "relay-$revision.tar.gz" "$bundle/SHA256SUMS"
```

Supply both hashes to the operator separately from the archive. `git archive`
includes only committed files, so local dependencies and untracked files cannot
silently enter the bundle. Keep the staging directory until the hashes and bundle
have been handed off.

The bundle is the reviewed `tools/room-relay` directory; it carries no VCS metadata
and needs no `node_modules`, since dependencies are installed from the locked
manifest into the staged release.

`bootstrap.sh` refuses to continue unless `SHA256SUMS` lists `REVISION`, every file
under `src/`, the package lock and each deployment template, all of those verify,
and `REVISION` contains exactly the commit passed as the second argument.

```
bash /root/relay-bundle/deploy/bootstrap.sh /root/relay-bundle FULL_GAME_COMMIT MANIFEST_SHA256
```

## What the script guarantees

It takes an exclusive root-owned lock (`/root/.dune-relay-bootstrap.lock`) before
anything is captured or replaced, so two runs cannot interleave backups, renames
and restarts.

It pins Node22.23.2 and verifies the official archive hash. The runtime and the
release are staged, recorded (`.installed-sha256` for contents, `.installed-inventory`
for the exact set of entries with each symlink's target) and fully re-verified
while still staged; only then are they renamed into place. On reuse the same
inventory must match exactly, so a missing entry, an extra entry or a retargeted
`npm` symlink fails closed. `npm` is invoked through the pinned runtime by absolute
path, never as a bare command off `PATH`. An incomplete cached runtime directory is
rejected for manual removal rather than being renamed into.

The `dune-relay` account is created with `--user-group`. An account that already
exists is reused only if it is a system UID/GID, has a `nologin`/`false` shell, the
expected `/nonexistent` home, `dune-relay` as its primary group and no supplementary
groups at all; otherwise the run stops.

The analytics key, the environment file and the analytics Apache configuration all
carry the HMAC secret. Each destination must be a root-owned regular file (symlinks
and other file types are refused), and each is installed by writing beside the
destination and renaming at mode 0600 — so a pre-existing world-readable file cannot
keep its mode or its inode. The canonical vhost is edited the same way.

Apache configuration, service configuration and the previous release symlink are
backed up before activation, and the recovery state (revision, previous release,
prior active/enabled state, and the numbered backup for each path) is written to
`rollback-manifest.txt` in the backup directory.

## Activation checks

* the unit is active, its `MainPID` exists, and that process's cwd and executable
  resolve to this release and to the pinned Node runtime;
* loopback health returns the relay's own body (`status=ok`, the protocol version
  the installed release declares, room and connection counts) — not merely a 2xx;
* Apache's parsed vhost map serves `dunelegacy.com:443` from the file that was
  edited;
* the same health body over local TLS with `--resolve dunelegacy.com:443:127.0.0.1`
  and again over public DNS, with normal certificate verification in both cases;
* an allowed browser origin reaches admission (400) and receives its exact
  `Access-Control-Allow-Origin` echo plus `Vary: Origin`, and a foreign origin is
  refused (403) with no CORS grant.

If any of these fail, rollback runs with errexit disabled: every restoration is
attempted, each file is read back (content and mode), the release symlink, the
unit's active/enabled state and Apache's state are re-checked, and every failure is
listed. A rollback that did not fully complete says so and exits **3**; only a
verified restoration reports success. Packages, the service account, an unused
staged release and the private backup remain after a failed activation for
inspection/retry; existing game data is never removed.

**Successful installation is not release acceptance:** before publishing clients,
verify public WebSocket admission/upgrade, a real browser/native match, forwarding
header replacement and signed lifecycle delivery plus production SQLite readback.
The PHP receiver must be deployed from the reviewed website branch and its schema
backed up before enabling analytics. Keep the game release blocked if these checks
cannot be run. TLS verification must remain enabled throughout.

For a manual rollback after a successful installation, use the private directory
printed in `/root/dune-relay-rollback.*`: `rollback-manifest.txt` names each
numbered backup, its destination and whether that destination should be restored or
removed, along with the previous release and the prior active/enabled state. Restore
the current symlink from `/etc/dunecity-relay/previous-release`. Validate Apache
before reload; reload systemd and restart the previous relay. Prefer the automatic
rollback during failed activation.

## Tests

`bash -n deploy/bootstrap.sh` and `bash deploy/test-helpers.sh`. The second sources
`bootstrap.sh`, which stops at its "sourced" guard so no provisioning runs, and
exercises the destination, atomic-install, inventory, manifest-coverage, health-body
and vhost-map helpers against fixtures in a private temporary directory. It needs no
privileges and touches nothing under `/etc`, `/root`, `/opt` or any service.

The covered root directory's owner and exact mode are checked as well as all
children (manifest format v2). Ancestors must remain inside the deployment
account's private home; same-account changes remain outside the tamper guarantee.
The supervisor pins one release for its lifetime and passes that exact path to
the child launcher. The launcher checks that it is its own script directory;
the sandbox also uses its own release rather than following `current`. Stop the
old supervisor before activating a new release and regenerating manifests.
`test-release-pinning.py` switches `current` after verification and verifies that
the old, verified release executes while a mismatched launch path is refused.
