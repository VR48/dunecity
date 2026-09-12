#!/usr/bin/env bash
# Administrator-only provisioning on the existing Ubuntu/Apache website host.
# Usage: sudo bash bootstrap.sh ROOT_OWNED_RELAY_DIR COMMIT_SHA MANIFEST_SHA256
set -euo pipefail

# --------------------------------------------------------------------------
# Helpers. Everything above the "sourced" guard below is free of side effects;
# deploy/test-helpers.sh sources this file to exercise it against fixtures.
# --------------------------------------------------------------------------

fail() { echo "$*" >&2; exit 2; }

# Refuses a destination root cannot safely overwrite: a symlink (which would
# redirect the write out of the directory), a non-regular file, or a file some
# other account owns. The owner argument exists so the fixture tests can run
# unprivileged; the script itself always requires root ownership.
require_safe_file() {
  local path=$1 owner=${2:-0}
  [[ ! -L $path ]] || fail "Refusing symlinked path: $path"
  if [[ -e $path ]]; then
    [[ -f $path ]] || fail "Refusing non-regular file: $path"
    [[ $(stat -c %u "$path") == "$owner" ]] || fail "Refusing file owned by another account: $path"
  fi
}

require_safe_dir() {
  local path=$1 owner=${2:-0} mode
  [[ ! -L $path ]] || fail "Refusing symlinked directory: $path"
  if [[ -e $path ]]; then
    [[ -d $path ]] || fail "Refusing non-directory: $path"
    [[ $(stat -c %u "$path") == "$owner" ]] || fail "Refusing directory owned by another account: $path"
    mode=$(stat -c %a "$path")
    (( 8#$mode & 8#022 )) && fail "Refusing group/other writable directory: $path" || true
  fi
}

# Installs stdin at DEST by writing beside it and renaming, so a reader never
# sees a partial file and an existing file's mode cannot survive the update.
# mktemp creates the staged file 0600, so secrets are never briefly readable.
install_stream() {
  local mode=$1 dest=$2 owner=${3:-0} tmp
  require_safe_file "$dest" "$owner"
  tmp=$(mktemp "$(dirname "$dest")/.dune-relay-install.XXXXXX") || fail "Could not stage $dest"
  if ! { cat > "$tmp" && chmod "$mode" "$tmp" && mv -Tf "$tmp" "$dest"; }; then
    rm -f "$tmp"
    fail "Could not install $dest"
  fi
}

# Exact tree inventory: every entry's type, path and, for symlinks, its target.
# A missing entry, an extra entry or a retargeted symlink all show up here;
# regular file contents are covered separately by .installed-sha256.
inventory_of() {
  (cd "$1" && find . -mindepth 1 \
      \( -name .installed-sha256 -o -name .installed-inventory \) -prune -o \
      -printf '%y %p %l\n' | LC_ALL=C sort)
}

record_tree() {
  local dir=$1
  (cd "$dir" && find . -type f ! -name .installed-sha256 ! -name .installed-inventory -print0 \
      | LC_ALL=C sort -z | xargs -0 sha256sum > .installed-sha256)
  inventory_of "$dir" > "$dir/.installed-inventory"
  # Not umask-dependent: verify_tree rejects any group/other writable entry.
  chmod 0644 "$dir/.installed-sha256" "$dir/.installed-inventory"
}

# The official Node archive ships npm/corepack as symlinks whose mode is 0777,
# so ownership is checked for every entry and write permission only on the
# entries that actually have a mode of their own.
verify_tree() {
  local dir=$1 label=$2 owner=${3:-0}
  [[ -z $(find "$dir" \( ! -uid "$owner" -o \( ! -type l -a -perm /022 \) \) -print -quit) ]] \
    || fail "Unsafe $label ownership/mode"
  [[ -f $dir/.installed-inventory && -f $dir/.installed-sha256 ]] \
    || fail "$label has no recorded inventory"
  inventory_of "$dir" | diff -q - "$dir/.installed-inventory" >/dev/null \
    || fail "$label has missing, extra or retargeted entries"
  (cd "$dir" && sha256sum --check --status .installed-sha256) \
    || fail "$label file contents do not match the recorded inventory"
}

# sha256sum --check only proves the entries a manifest happens to list, so the
# files that matter must be shown to be listed before that check means anything.
manifest_covers() {
  local manifest=$1 name
  shift
  for name in "$@"; do
    LC_ALL=C grep -qE "^[0-9a-f]{64} [ *](\./)?${name//./\\.}\$" "$manifest" \
      || fail "SHA256SUMS does not cover $name"
  done
}

# The relay answers health as text/plain key=value lines, so a 2xx from any
# other handler on the same path cannot be mistaken for a healthy relay.
assert_health() {
  local body=$1 label=$2 protocol=$3
  grep -qx 'status=ok' <<<"$body" || fail "$label health did not report status=ok"
  grep -qx "protocol=$protocol" <<<"$body" || fail "$label health did not report protocol=$protocol"
  grep -qE '^rooms=[0-9]+$' <<<"$body" || fail "$label health did not report a room count"
  grep -qE '^connections=[0-9]+$' <<<"$body" || fail "$label health did not report a connection count"
}

# Reads Apache's own parsed vhost map on stdin and proves that SERVER_NAME on
# port 443 is served by one of the given configuration files. A pathname in a
# config file and a closing tag prove nothing about what Apache actually maps.
# Apache prints "*:443 ... is a NameVirtualHost" followed by indented
# "port 443 namevhost NAME (file:line)" entries, or, when an address has a
# single vhost, one "*:443   NAME (file:line)" line.
vhost_serves_443() {
  python3 -c '
import re,sys
name,want=sys.argv[1],set(sys.argv[2:])
port=None
for line in sys.stdin:
    header=re.match(r"^(\S+):(\d+)\s",line)
    if header: port=header.group(2)
    entry=re.search(r"(?<![\w.-])"+re.escape(name)+r" \(([^:]+):\d+\)",line)
    if entry and port=="443" and entry.group(1) in want: sys.exit(0)
sys.exit(1)
' "$@"
}

# deploy/test-helpers.sh stops here: nothing below runs when this file is sourced.
(return 0 2>/dev/null) && return 0

[[ $(id -u) == 0 ]] || { echo 'Run this script as an administrator.' >&2; exit 1; }
SOURCE=$(realpath "${1:?reviewed room-relay directory required}")
REVISION=${2:?full game commit SHA required}
MANIFEST_SHA=${3:?independently verified manifest SHA256 required}
[[ $REVISION =~ ^[0-9a-f]{40}$ ]] || fail 'Commit SHA must be 40 lowercase hex characters'
[[ $MANIFEST_SHA =~ ^[0-9a-f]{64}$ ]] || fail 'Manifest SHA256 must be 64 lowercase hex characters'
ROOT=/opt/dunecity-relay
# Root must execute this script from the same verified root-owned bundle. Do not
# execute a script in a deploy-user-owned upload directory, even through sudo.
python3 - "$SOURCE" <<'CHECK'
import os,stat,sys
from pathlib import Path
root=Path(sys.argv[1])
for p in [root,*root.parents,*root.rglob('*')]:
    st=p.lstat()
    if stat.S_ISLNK(st.st_mode) or st.st_uid != 0 or st.st_mode & 0o022:
        raise SystemExit('Refusing mutable or symlinked source: '+str(p))
CHECK
printf '%s  %s\n' "$MANIFEST_SHA" "$SOURCE/SHA256SUMS" | sha256sum --check --status
for input in REVISION src/index.js package.json package-lock.json \
             deploy/bootstrap.sh deploy/apache.conf deploy/dunecity-relay.service; do
  [[ -f "$SOURCE/$input" ]] || fail "Missing $input"
done
# Every shipped source file, not just the entry point, must be named in the
# manifest; an omitted file would otherwise be copied into the release unchecked.
mapfile -t COVERED < <(cd "$SOURCE" && find src -type f | LC_ALL=C sort)
COVERED+=(REVISION package.json package-lock.json
          deploy/bootstrap.sh deploy/apache.conf deploy/dunecity-relay.service)
manifest_covers "$SOURCE/SHA256SUMS" "${COVERED[@]}"
(cd "$SOURCE" && sha256sum --check --status SHA256SUMS)
# The manifest now authenticates REVISION, so the release label is bound to the
# bundle rather than being whatever the operator typed on the command line.
BUNDLE_REVISION=$(<"$SOURCE/REVISION")
[[ $BUNDLE_REVISION == "$REVISION" ]] \
  || fail "Bundle REVISION ($BUNDLE_REVISION) does not match the requested commit ($REVISION)"
VHOST=$(readlink -f /etc/apache2/sites-enabled/dunelegacy-le-ssl.conf)
[[ -f $VHOST ]] || fail 'Expected existing HTTPS vhost missing'
require_safe_file "$VHOST"

# A second concurrent bootstrap would interleave backups, renames and service
# restarts, so take an exclusive root-owned lock before anything is captured.
require_safe_dir /root
LOCK=/root/.dune-relay-bootstrap.lock
require_safe_file "$LOCK"
(umask 077; : >>"$LOCK")
chmod 0600 "$LOCK"
exec 9>>"$LOCK"
flock -n 9 || fail 'Another bootstrap is already running.'

# Capture recoverable activation state before touching any service configuration.
BACKUP=$(mktemp -d /root/dune-relay-rollback.XXXXXX)
PATHS=(/etc/dunecity-relay/environment /etc/apache2/conf-available/dunecity-relay.conf /etc/apache2/conf-available/dunecity-relay-analytics.conf /etc/systemd/system/dunecity-relay.service "$VHOST")
for i in "${!PATHS[@]}"; do [[ ! -e ${PATHS[$i]} ]] || cp -a "${PATHS[$i]}" "$BACKUP/$i"; done
PREVIOUS=$(readlink "$ROOT/current" || true)
WAS_ACTIVE=$(systemctl is-active dunecity-relay || true)
WAS_ENABLED=$(systemctl is-enabled dunecity-relay 2>/dev/null || true)
ACTIVATING=0
STAGE=''
NODE_STAGE=''
# Recorded so that a later manual rollback does not depend on reading this script.
{
  printf 'revision=%s\n' "$REVISION"
  printf 'previous_release=%s\n' "$PREVIOUS"
  printf 'was_active=%s\n' "$WAS_ACTIVE"
  printf 'was_enabled=%s\n' "$WAS_ENABLED"
  for i in "${!PATHS[@]}"; do
    if [[ -e $BACKUP/$i ]]; then printf 'file\t%s\trestore\t%s\n' "$i" "${PATHS[$i]}"
    else printf 'file\t%s\tremove\t%s\n' "$i" "${PATHS[$i]}"; fi
  done
} >"$BACKUP/rollback-manifest.txt"
chmod 0600 "$BACKUP/rollback-manifest.txt"

# Rollback runs with errexit disabled: one failed step must not skip the rest.
# Every restoration is attempted, read back, and any failure is reported rather
# than suppressed, because a half-rolled-back host needs a person, not a claim.
cleanup() {
  local status=$?
  trap - EXIT
  set +e
  if [[ $status != 0 && $ACTIVATING == 1 ]]; then
    local -a failures=()
    local i target tmp now_active now_enabled
    for i in "${!PATHS[@]}"; do
      target=${PATHS[$i]}
      if [[ -e $BACKUP/$i ]]; then
        tmp=$(mktemp "$(dirname "$target")/.dune-relay-rollback.XXXXXX" 2>/dev/null)
        if [[ -n $tmp ]] && cp -a "$BACKUP/$i" "$tmp" && mv -Tf "$tmp" "$target"; then
          cmp -s "$BACKUP/$i" "$target" || failures+=("$target differs from its backup after restore")
          [[ $(stat -c %a "$BACKUP/$i") == "$(stat -c %a "$target")" ]] \
            || failures+=("$target was restored with a different mode")
        else
          [[ -z $tmp ]] || rm -f "$tmp"
          failures+=("could not restore $target")
        fi
      else
        rm -f "$target"
        [[ ! -e $target ]] || failures+=("could not remove $target")
      fi
    done
    if [[ -n $PREVIOUS ]]; then
      if ln -sfn "$PREVIOUS" "$ROOT/current.rollback.$$" \
         && mv -Tf "$ROOT/current.rollback.$$" "$ROOT/current"; then
        [[ $(readlink "$ROOT/current") == "$PREVIOUS" ]] \
          || failures+=("current release symlink does not point at $PREVIOUS")
      else
        rm -f "$ROOT/current.rollback.$$"
        failures+=("could not restore the current release symlink")
      fi
    else
      rm -f "$ROOT/current"
      [[ ! -L $ROOT/current && ! -e $ROOT/current ]] \
        || failures+=("could not remove the current release symlink")
    fi
    systemctl daemon-reload || failures+=("systemctl daemon-reload failed")
    if [[ $WAS_ENABLED == enabled ]]; then
      systemctl enable dunecity-relay >/dev/null 2>&1 || failures+=("could not re-enable the unit")
    else
      systemctl disable dunecity-relay >/dev/null 2>&1
    fi
    if [[ $WAS_ACTIVE == active ]]; then
      systemctl restart dunecity-relay || failures+=("could not restart the previous relay")
    else
      systemctl stop dunecity-relay || failures+=("could not stop the relay")
    fi
    now_active=$(systemctl is-active dunecity-relay 2>/dev/null)
    now_enabled=$(systemctl is-enabled dunecity-relay 2>/dev/null)
    [[ -z $WAS_ACTIVE || $now_active == "$WAS_ACTIVE" ]] \
      || failures+=("unit is '$now_active' but was '$WAS_ACTIVE'")
    [[ -z $WAS_ENABLED || $now_enabled == "$WAS_ENABLED" ]] \
      || failures+=("unit is '$now_enabled' but was '$WAS_ENABLED'")
    if apache2ctl configtest >/dev/null 2>&1; then
      systemctl reload apache2 || failures+=("could not reload Apache")
    else
      failures+=("Apache configuration is invalid after rollback; it was NOT reloaded")
    fi
    [[ $(systemctl is-active apache2 2>/dev/null) == active ]] || failures+=("Apache is not active")
    if (( ${#failures[@]} == 0 )); then
      echo "Activation failed; prior service/configuration restored and verified. Backup: $BACKUP" >&2
    else
      echo 'Activation failed AND rollback did not complete. MANUAL RECOVERY REQUIRED:' >&2
      printf '  - %s\n' "${failures[@]}" >&2
      echo "Recorded state and backups: $BACKUP/rollback-manifest.txt" >&2
      status=3
    fi
  fi
  [[ -z $STAGE ]] || rm -rf "$STAGE"
  [[ -z $NODE_STAGE ]] || rm -rf "$NODE_STAGE"
  exit "$status"
}
trap cleanup EXIT

RELEASE="$ROOT/releases/$REVISION"

NODE_VERSION=22.23.2
case $(uname -m) in
  x86_64) ARCH=x64; HASH=d60acfe00a2932254bb0ad20e01b0d74397a0875595de719654b214f4b03f307 ;;
  aarch64) ARCH=arm64; HASH=fff4078c5def658577f92c88db7db3bc0072924bfb93fe52c1e744a54e94abb8 ;;
  *) fail 'Unsupported architecture' ;;
esac
apt-get update
apt-get install -y ca-certificates curl xz-utils
# The unit runs as dune-relay:dune-relay and that identity holds the analytics
# key, so an account that already exists is only reused when it is provably the
# same unprivileged system identity; otherwise it is created explicitly.
if id dune-relay >/dev/null 2>&1; then
  IFS=: read -r _ _ RELAY_UID RELAY_GID _ RELAY_HOME RELAY_SHELL < <(getent passwd dune-relay)
  (( RELAY_UID > 0 && RELAY_UID < 1000 )) || fail "dune-relay is not a system account (uid $RELAY_UID)"
  (( RELAY_GID > 0 && RELAY_GID < 1000 )) || fail "dune-relay's primary group is not a system group (gid $RELAY_GID)"
  case $RELAY_SHELL in
    /usr/sbin/nologin|/sbin/nologin|/bin/false|/usr/bin/false) ;;
    *) fail "dune-relay has a login shell: $RELAY_SHELL" ;;
  esac
  [[ $RELAY_HOME == /nonexistent ]] || fail "dune-relay has an unexpected home directory: $RELAY_HOME"
  getent group dune-relay >/dev/null || fail 'Group dune-relay does not exist'
  [[ $(id -gn dune-relay) == dune-relay ]] || fail "dune-relay's primary group is $(id -gn dune-relay), not dune-relay"
  [[ $(id -nG dune-relay) == dune-relay ]] || fail "dune-relay has extra groups: $(id -nG dune-relay)"
else
  useradd --system --user-group --home-dir /nonexistent --no-create-home \
    --shell /usr/sbin/nologin dune-relay
fi
require_safe_dir "$ROOT"
require_safe_dir "$ROOT/releases"
require_safe_dir "$ROOT/runtime"
install -d -m 0755 -o root -g root "$ROOT" "$ROOT/releases" "$ROOT/runtime"
NODE="$ROOT/runtime/node-v$NODE_VERSION-linux-$ARCH"
# An interrupted earlier run can leave a partial runtime here. Renaming onto it
# would nest the new tree inside the old one, so refuse it instead of guessing.
[[ ! -e $NODE || -x $NODE/bin/node ]] \
  || fail "Incomplete cached Node runtime at $NODE; remove it and retry."
if [[ ! -x "$NODE/bin/node" ]]; then
  NODE_STAGE=$(mktemp -d "$ROOT/runtime/.stage.XXXXXX")
  ARCHIVE="$NODE_STAGE/node.tar.xz"
  curl --fail --silent --show-error --proto '=https' --tlsv1.2 \
    "https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-linux-$ARCH.tar.xz" -o "$ARCHIVE"
  printf '%s  %s\n' "$HASH" "$ARCHIVE" | sha256sum --check --status
  tar -xJf "$ARCHIVE" -C "$NODE_STAGE" --no-same-owner
  rm -f "$ARCHIVE"
  STAGED_NODE="$NODE_STAGE/node-v$NODE_VERSION-linux-$ARCH"
  [[ -x $STAGED_NODE/bin/node && -x $STAGED_NODE/bin/npm ]] \
    || fail 'The extracted Node runtime has no usable node/npm'
  record_tree "$STAGED_NODE"
  # Reject a bad runtime while it is still staged, never after it is in place.
  verify_tree "$STAGED_NODE" 'staged Node runtime'
  [[ $("$STAGED_NODE/bin/node" --version) == v$NODE_VERSION ]] \
    || fail 'The staged Node runtime does not report the pinned version'
  mv -T "$STAGED_NODE" "$NODE"
fi
verify_tree "$NODE" 'cached Node runtime'
# npm is a symlink into lib/node_modules; the inventory above authenticates both
# the link and its target, and it is invoked by path so PATH cannot supply another.
[[ -x "$NODE/bin/npm" ]] || fail 'The cached Node runtime has no usable npm'
[[ $("$NODE/bin/node" --version) == v$NODE_VERSION ]] \
  || fail 'The cached Node runtime does not report the pinned version'

if [[ -d "$RELEASE" ]]; then
  [[ $(<"$RELEASE/.source-manifest-sha256") == "$MANIFEST_SHA" ]] \
    || fail "$RELEASE was built from a different source manifest"
  [[ $(<"$RELEASE/REVISION") == "$REVISION" ]] \
    || fail "$RELEASE records a different revision"
  verify_tree "$RELEASE" 'cached release'
elif [[ -e "$RELEASE" ]]; then
  fail "$RELEASE exists and is not a release directory"
else
  STAGE=$(mktemp -d "$ROOT/releases/.stage.XXXXXX")
  cp -a "$SOURCE/src" "$SOURCE/package.json" "$SOURCE/package-lock.json" "$SOURCE/REVISION" "$STAGE/"
  # Dependency install cannot execute lifecycle scripts or modify system packages.
  (cd "$STAGE"; PATH="$NODE/bin:$PATH" "$NODE/bin/node" "$NODE/bin/npm" ci \
    --omit=dev --ignore-scripts --no-audit --no-fund)
  chown -R root:root "$STAGE"
  chmod -R go-w "$STAGE"
  chmod 0755 "$STAGE"
  printf '%s\n' "$MANIFEST_SHA" > "$STAGE/.source-manifest-sha256"
  record_tree "$STAGE"
  verify_tree "$STAGE" 'staged release'
  mv -T "$STAGE" "$RELEASE"
  STAGE=''
fi
ACTIVATING=1
require_safe_dir /etc/dunecity-relay
install -d -m 0700 -o root -g root /etc/dunecity-relay
# The key file and both generated configurations carry the HMAC secret, so an
# existing one is only reused when it is a root-owned regular file, and each is
# (re)installed by rename at 0600 rather than rewritten through a redirection.
require_safe_file /etc/dunecity-relay/analytics.key
if [[ ! -e /etc/dunecity-relay/analytics.key ]]; then
  KEY=$(openssl rand -hex 32)
else
  KEY=$(</etc/dunecity-relay/analytics.key)
fi
[[ $KEY =~ ^[0-9a-f]{64}$ ]] || fail 'Unexpected analytics key format'
printf '%s\n' "$KEY" | install_stream 0600 /etc/dunecity-relay/analytics.key
install_stream 0600 /etc/dunecity-relay/environment <<ENV
RELAY_HOST=127.0.0.1
RELAY_PORT=8787
RELAY_PUBLIC_URL=wss://dunelegacy.com/relay/v1/socket
RELAY_OBSERVED_TRANSPORT=wss
RELAY_ALLOWED_ORIGINS=https://dunelegacy.com,https://www.dunelegacy.com
RELAY_TRUST_FORWARDED_FOR=1
RELAY_GAME_PROTOCOL=5
DUNE_RELAY_ANALYTICS_URL=https://dunelegacy.com/metaserver/relay-events.php
DUNE_RELAY_ANALYTICS_KEY=$KEY
ENV
install_stream 0600 /etc/apache2/conf-available/dunecity-relay-analytics.conf <<CONF
<Location "/metaserver/relay-events.php">
    SetEnv DUNE_RELAY_ANALYTICS_KEY $KEY
</Location>
CONF
unset KEY
install_stream 0644 /etc/apache2/conf-available/dunecity-relay.conf < "$SOURCE/deploy/apache.conf"
sed "s|@NODE@|$NODE/bin/node|g" "$SOURCE/deploy/dunecity-relay.service" \
  | install_stream 0644 /etc/systemd/system/dunecity-relay.service
printf '%s\n' "$PREVIOUS" | install_stream 0600 /etc/dunecity-relay/previous-release
# Include routes and secret only in the existing canonical TLS virtual host.
python3 - "$VHOST" <<'VHOSTEDIT'
import os,sys,tempfile
from pathlib import Path
p=Path(sys.argv[1]);s=p.read_text();out=s
for name in ('dunecity-relay','dunecity-relay-analytics'):
    line='    Include /etc/apache2/conf-available/'+name+'.conf'
    if line not in out:
        if out.count('</VirtualHost>') != 1: raise SystemExit('Expected exactly one TLS vhost')
        out=out.replace('</VirtualHost>',line+'\n</VirtualHost>')
if out != s:
    mode=p.stat().st_mode & 0o7777
    fd,tmp=tempfile.mkstemp(dir=str(p.parent),prefix='.dune-relay-vhost.')
    try:
        with os.fdopen(fd,'w') as f: f.write(out)
        os.chmod(tmp,mode)
        os.replace(tmp,str(p))
    except BaseException:
        os.path.exists(tmp) and os.unlink(tmp)
        raise
VHOSTEDIT
a2enmod proxy proxy_http proxy_wstunnel headers ssl
apache2ctl configtest
ln -s "$RELEASE" "$ROOT/current.$$.next"
mv -Tf "$ROOT/current.$$.next" "$ROOT/current"
systemctl daemon-reload
systemctl enable --now dunecity-relay
systemctl restart dunecity-relay

# The protocol version the installed release actually serves, read (not executed)
# from the verified release, so health checks assert the relay's own answer.
PROTOCOL=$(sed -n 's/^const RELAY_PROTOCOL_VERSION = \([0-9]\{1,\}\);.*$/\1/p' "$RELEASE/src/constants.js")
[[ $PROTOCOL =~ ^[0-9]+$ ]] || fail 'Could not read the protocol version from the installed release'
HEALTH=''
for attempt in $(seq 1 20); do
  HEALTH=$(curl -fsS --max-time 5 http://127.0.0.1:8787/v1/health 2>/dev/null) && break
  HEALTH=''
  sleep 1
done
assert_health "$HEALTH" 'loopback' "$PROTOCOL"

# The running process, not just the unit file, must be this release on the pinned runtime.
[[ $(systemctl is-active dunecity-relay) == active ]] || fail 'The relay unit is not active'
MAIN_PID=$(systemctl show -p MainPID --value dunecity-relay)
[[ $MAIN_PID =~ ^[0-9]+$ ]] && (( MAIN_PID > 0 )) || fail 'The relay unit has no running main process'
[[ $(readlink -f "/proc/$MAIN_PID/cwd") == "$(readlink -f "$RELEASE")" ]] \
  || fail "The running relay is not executing from $RELEASE"
[[ $(readlink -f "/proc/$MAIN_PID/exe") == "$(readlink -f "$NODE/bin/node")" ]] \
  || fail 'The running relay is not using the pinned Node runtime'

systemctl reload apache2
# Prove Apache really maps dunelegacy.com:443 to the vhost file that was edited,
# rather than trusting a pathname and a closing tag.
VHOST_LINK=/etc/apache2/sites-enabled/dunelegacy-le-ssl.conf
apache2ctl -t -D DUMP_VHOSTS 2>&1 | vhost_serves_443 dunelegacy.com "$VHOST" "$VHOST_LINK" \
  || fail 'Apache does not serve dunelegacy.com:443 from the edited TLS vhost'

# Local TLS through the canonical vhost (certificate verification stays on; only
# the address resolution is pinned), then the same check over public DNS.
LOCAL_HEALTH=$(curl --fail --silent --show-error --max-time 15 \
  --resolve dunelegacy.com:443:127.0.0.1 https://dunelegacy.com/relay/v1/health) \
  || fail 'The local canonical vhost did not serve relay health over TLS'
assert_health "$LOCAL_HEALTH" 'local vhost' "$PROTOCOL"
PUBLIC_HEALTH=$(curl --fail --silent --show-error --max-time 15 \
  https://dunelegacy.com/relay/v1/health) || fail 'Public relay health is not reachable'
assert_health "$PUBLIC_HEALTH" 'public' "$PROTOCOL"

# Browser origin handling, headers included: an allowed origin must get its exact
# echo back, and a foreign origin must be refused without any CORS grant.
probe_admission() {
  curl -sS --max-time 10 -D - -o /dev/null -w '\ncode=%{http_code}\n' \
    -H "Origin: $1" --data 'app=dunecity' https://dunelegacy.com/relay/v1/admission/host
}
ALLOWED=$(probe_admission https://dunelegacy.com)
grep -qx 'code=400' <<<"$ALLOWED" || fail 'Allowed origin did not reach relay admission'
grep -qiE '^access-control-allow-origin:[[:space:]]*https://dunelegacy[.]com[[:space:]]*$' <<<"$ALLOWED" \
  || fail 'Allowed origin did not receive its exact CORS grant'
grep -qi '^vary:.*origin' <<<"$ALLOWED" || fail 'Allowed origin response is missing Vary: Origin'
DENIED=$(probe_admission https://untrusted.invalid)
grep -qx 'code=403' <<<"$DENIED" || fail 'A foreign origin was not refused'
! grep -qi '^access-control-allow-origin' <<<"$DENIED" || fail 'A foreign origin received a CORS grant'
grep -qi '^vary:.*origin' <<<"$DENIED" || fail 'Refusal is missing Vary: Origin'

ACTIVATING=0
echo "Relay $REVISION is installed. Verify public HTTPS/WSS and signed SQLite delivery before publishing the game."
