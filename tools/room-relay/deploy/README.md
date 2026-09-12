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
