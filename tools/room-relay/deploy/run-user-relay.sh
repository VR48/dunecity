#!/bin/bash
# Two roles, one file.
#
#   run-user-relay.sh               cron/@reboot entry point: trims the log, takes
#                                   the single exclusive flock, verifies the frozen
#                                   release and pinned runtime, and hands the held
#                                   lock to the persistent supervisor via exec.
#   run-user-relay.sh --exec-child  the launcher the supervisor spawns. It takes no
#                                   lock (the supervisor above it holds the only
#                                   one, so there is no nested lock to deadlock on)
#                                   and every step is an exec, so the PID the
#                                   supervisor holds is the Node process itself.
#
# Crontab for the deployment account:
#   @reboot     /bin/bash /home/dunelegacy-deploy/dunecity-relay/current/deploy/run-user-relay.sh
#   * * * * *   /bin/bash /home/dunelegacy-deploy/dunecity-relay/current/deploy/run-user-relay.sh
# The minute line is a no-op while the supervisor holds the lock, so it only ever
# restores a supervisor that is gone. Neither line needs a login shell.
set -euo pipefail
base="/home/dunelegacy-deploy/dunecity-relay"
private="/var/www/data/dunecity-relay"
state="$base/state"

if [[ ${1:-} == --exec-child ]]; then
  # The supervisor passes the same absolute release it verified. Never follow
  # current here: it may have changed during an operator's rollout.
  release=${2:-}
  own_release=$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")
  [[ "$release" == /* && "$release" == "$own_release" && $# == 2 ]] || exit 2
  cd "$release"
  ulimit -c 0
  ulimit -n 256
  umask 077
  # No inherited SSH agent, deployment environment or shell subprocess permissions.
  # Node's permission model is defense in depth, not an OS security boundary.
  exec env -i PATH="$base/runtime/bin:/usr/bin:/bin" LANG=C.UTF-8 \
    HOME="$base" NODE_ENV=production \
    RELAY_HOST=127.0.0.1 RELAY_PORT=18787 RELAY_HTTP_POLLING=1 \
    RELAY_PUBLIC_URL=https://dunelegacy.com/relay/v1/poll \
    RELAY_ALLOWED_ORIGINS=https://dunelegacy.com,https://www.dunelegacy.com \
    RELAY_TRUST_FORWARDED_FOR=1 RELAY_GAME_PROTOCOL=5 RELAY_MAX_CONNECTIONS=12 \
    RELAY_MAX_POLLING_SESSIONS=12 \
    RELAY_GATEWAY_KEY_FILE="$private/gateway.key" \
    DUNE_RELAY_ANALYTICS_URL=https://dunelegacy.com/metaserver/relay-events.php \
    DUNE_RELAY_ANALYTICS_KEY_FILE="$private/analytics.key" \
    /usr/bin/python3 "$release/deploy/sandbox.py" \
    "$base/runtime/bin/node" --permission --no-addons \
    --allow-fs-read="$release" --allow-fs-read="$private/gateway.key" \
    --allow-fs-read="$private/analytics.key" --allow-fs-read=/etc/ssl/certs \
    --max-old-space-size=192 --disable-proto=throw src/index.js
fi
[[ -z ${1:-} ]] || { echo "usage: run-user-relay.sh [--exec-child VERIFIED_RELEASE]" >&2; exit 2; }

# Trimming happens here, before the lock: the supervisor holds the lock for its
# whole life, so this minute-by-minute run is the only thing that bounds the log.
/usr/bin/python3 "$base/current/deploy/trim-user-log.py"
exec 9>"$base/run.lock"
flock -n 9 || exit 0
exec >>"$base/service.log" 2>&1
release=$(readlink -f "$base/current")

# Fail closed before any relay code runs: the pinned runtime and the frozen
# release must still be byte-for-byte the artifacts that were recorded, with no
# missing entry, no extra entry and no re-pointed symlink. The recorded root is
# an absolute path, so a `current` symlink moved to a different release is
# refused too. (This gate detects drift and damage, not same-UID tampering: the
# account that owns these files also owns the manifests.)
/usr/bin/python3 "$release/deploy/artifact-manifest.py" verify \
  --root "$base/runtime" --manifest "$state/runtime.manifest"
/usr/bin/python3 "$release/deploy/artifact-manifest.py" verify \
  --root "$release" --manifest "$state/release.manifest"

# fd 9 survives the exec, so the supervisor inherits the one held lock and takes
# no lock of its own; it closes fd 9 out of the children it spawns.
exec env -i PATH="$base/runtime/bin:/usr/bin:/bin" LANG=C.UTF-8 HOME="$base" \
  RELAY_BASE="$base" RELAY_PRIVATE="$private" \
  RELAY_GATEWAY_KEY_FILE="$private/gateway.key" \
  /usr/bin/python3 "$release/deploy/relay-supervisor.py"
