#!/usr/bin/env bash
# Administrator-only provisioning on the existing Ubuntu/Apache website host.
# Usage: sudo bash bootstrap.sh ROOT_OWNED_RELAY_DIR COMMIT_SHA MANIFEST_SHA256
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run this script as an administrator.' >&2; exit 1; }
SOURCE=$(realpath "${1:?reviewed room-relay directory required}")
REVISION=${2:?full game commit SHA required}
MANIFEST_SHA=${3:?independently verified manifest SHA256 required}
[[ $MANIFEST_SHA =~ ^[0-9a-f]{64}$ ]] || exit 2
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
(cd "$SOURCE"; sha256sum --check --status SHA256SUMS)
for input in src/index.js package.json package-lock.json deploy/bootstrap.sh deploy/apache.conf deploy/dunecity-relay.service; do
  [[ -f "$SOURCE/$input" ]] || { echo "Missing $input" >&2; exit 2; }
done
VHOST=$(readlink -f /etc/apache2/sites-enabled/dunelegacy-le-ssl.conf)
[[ -f $VHOST ]] || { echo 'Expected existing HTTPS vhost missing' >&2; exit 2; }
# Capture recoverable activation state before touching any service configuration.
BACKUP=$(mktemp -d /root/dune-relay-rollback.XXXXXX)
PATHS=(/etc/dunecity-relay/environment /etc/apache2/conf-available/dunecity-relay.conf /etc/apache2/conf-available/dunecity-relay-analytics.conf /etc/systemd/system/dunecity-relay.service "$VHOST")
for i in "${!PATHS[@]}"; do [[ ! -e ${PATHS[$i]} ]] || cp -a "${PATHS[$i]}" "$BACKUP/$i"; done
PREVIOUS=$(readlink /opt/dunecity-relay/current || true)
WAS_ACTIVE=$(systemctl is-active dunecity-relay || true)
WAS_ENABLED=$(systemctl is-enabled dunecity-relay 2>/dev/null || true)
ACTIVATING=0
STAGE=''
NODE_STAGE=''
cleanup() {
  status=$?
  trap - EXIT
  if [[ $status != 0 && $ACTIVATING == 1 ]]; then
    for i in "${!PATHS[@]}"; do
      if [[ -e $BACKUP/$i ]]; then cp -a "$BACKUP/$i" "${PATHS[$i]}"; else rm -f "${PATHS[$i]}"; fi
    done
    if [[ -n $PREVIOUS ]]; then ln -sfn "$PREVIOUS" /opt/dunecity-relay/current; else rm -f /opt/dunecity-relay/current; fi
    systemctl daemon-reload || true
    if [[ $WAS_ACTIVE == active ]]; then systemctl restart dunecity-relay || true; else systemctl stop dunecity-relay || true; fi
    [[ $WAS_ENABLED == enabled ]] || systemctl disable dunecity-relay >/dev/null 2>&1 || true
    apache2ctl configtest && systemctl reload apache2 || true
    echo "Activation failed; prior service/configuration restored. Backup: $BACKUP" >&2
  fi
  [[ -z $STAGE ]] || rm -rf "$STAGE"
  [[ -z $NODE_STAGE ]] || rm -rf "$NODE_STAGE"
  exit "$status"
}
trap cleanup EXIT
[[ $REVISION =~ ^[0-9a-f]{40}$ ]] || exit 2
[[ -f "$SOURCE/package-lock.json" && -f "$SOURCE/src/index.js" ]] || exit 2
ROOT=/opt/dunecity-relay
RELEASE="$ROOT/releases/$REVISION"

NODE_VERSION=22.23.2
case $(uname -m) in
  x86_64) ARCH=x64; HASH=d60acfe00a2932254bb0ad20e01b0d74397a0875595de719654b214f4b03f307 ;;
  aarch64) ARCH=arm64; HASH=fff4078c5def658577f92c88db7db3bc0072924bfb93fe52c1e744a54e94abb8 ;;
  *) echo 'Unsupported architecture' >&2; exit 2 ;;
esac
apt-get update
apt-get install -y ca-certificates curl xz-utils
id dune-relay >/dev/null 2>&1 || useradd --system --home-dir /nonexistent --no-create-home --shell /usr/sbin/nologin dune-relay
install -d -m 0755 "$ROOT/releases" "$ROOT/runtime"
NODE="$ROOT/runtime/node-v$NODE_VERSION-linux-$ARCH"
if [[ ! -x "$NODE/bin/node" ]]; then
  NODE_STAGE=$(mktemp -d "$ROOT/runtime/.stage.XXXXXX")
  ARCHIVE="$NODE_STAGE/node.tar.xz"
  curl --fail --silent --show-error --proto '=https' --tlsv1.2 \
    "https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-linux-$ARCH.tar.xz" -o "$ARCHIVE"
  printf '%s  %s\n' "$HASH" "$ARCHIVE" | sha256sum --check --status
  tar -xJf "$ARCHIVE" -C "$NODE_STAGE"
  (cd "$NODE_STAGE/node-v$NODE_VERSION-linux-$ARCH"; find . -type f ! -name .installed-sha256 -print0 | sort -z | xargs -0 sha256sum > .installed-sha256)
  mv "$NODE_STAGE/node-v$NODE_VERSION-linux-$ARCH" "$NODE"
fi
[[ $("$NODE/bin/node" --version) == v$NODE_VERSION ]] || exit 2
[[ -z $(find "$NODE" \( ! -user root -o -perm /022 \) -print -quit) ]] || { echo 'Unsafe cached Node ownership/mode' >&2; exit 2; }
(cd "$NODE"; sha256sum --check --status .installed-sha256)
if [[ -d "$RELEASE" ]]; then
  [[ -z $(find "$RELEASE" \( ! -user root -o -perm /022 \) -print -quit) ]] || exit 2
  [[ $(cat "$RELEASE/.source-manifest-sha256") == "$MANIFEST_SHA" ]] || exit 2
  (cd "$RELEASE"; sha256sum --check --status .installed-sha256)
else
STAGE=$(mktemp -d "$ROOT/releases/.stage.XXXXXX")
cp -a "$SOURCE/src" "$SOURCE/package.json" "$SOURCE/package-lock.json" "$STAGE/"
# Dependency install cannot execute lifecycle scripts or modify system packages.
(cd "$STAGE"; PATH="$NODE/bin:$PATH" npm ci --omit=dev --ignore-scripts --no-audit --no-fund)
chown -R root:root "$STAGE"
chmod -R go-w "$STAGE"
chmod 0755 "$STAGE"
printf '%s\n' "$MANIFEST_SHA" > "$STAGE/.source-manifest-sha256"
(cd "$STAGE"; find . -type f ! -name .installed-sha256 -print0 | sort -z | xargs -0 sha256sum > .installed-sha256)
mv "$STAGE" "$RELEASE"
STAGE=''
fi
ACTIVATING=1
install -d -m 0700 /etc/dunecity-relay
if [[ ! -f /etc/dunecity-relay/analytics.key ]]; then
  (umask 077; openssl rand -hex 32 > /etc/dunecity-relay/analytics.key)
fi
KEY=$(cat /etc/dunecity-relay/analytics.key)
[[ $KEY =~ ^[0-9a-f]{64}$ ]] || { echo 'Unexpected analytics key format' >&2; exit 2; }
(umask 077; cat > /etc/dunecity-relay/environment <<ENV
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
cat > /etc/apache2/conf-available/dunecity-relay-analytics.conf <<CONF
<Location "/metaserver/relay-events.php">
    SetEnv DUNE_RELAY_ANALYTICS_KEY $KEY
</Location>
CONF
)
unset KEY
install -m 0644 "$SOURCE/deploy/apache.conf" /etc/apache2/conf-available/dunecity-relay.conf
sed "s|@NODE@|$NODE/bin/node|g" "$SOURCE/deploy/dunecity-relay.service" > /etc/systemd/system/dunecity-relay.service
chmod 0644 /etc/systemd/system/dunecity-relay.service
printf '%s\n' "$PREVIOUS" > /etc/dunecity-relay/previous-release
# Include routes and secret only in the existing canonical TLS virtual host.
python3 - "$VHOST" <<'VHOSTEDIT'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
for name in ('dunecity-relay','dunecity-relay-analytics'):
    line='    Include /etc/apache2/conf-available/'+name+'.conf'
    if line not in s:
        if s.count('</VirtualHost>') != 1: raise SystemExit('Expected exactly one TLS vhost')
        s=s.replace('</VirtualHost>',line+'\n</VirtualHost>')
p.write_text(s)
VHOSTEDIT
a2enmod proxy proxy_http proxy_wstunnel headers ssl
apache2ctl configtest
ln -s "$RELEASE" "$ROOT/current.$$.next"
mv -Tf "$ROOT/current.$$.next" "$ROOT/current"
systemctl daemon-reload
systemctl enable --now dunecity-relay
systemctl restart dunecity-relay
for attempt in $(seq 1 20); do
  if curl -fsS http://127.0.0.1:8787/v1/health >/dev/null; then break; fi
  sleep 1
done
curl -fsS http://127.0.0.1:8787/v1/health >/dev/null
systemctl reload apache2
curl --fail --silent --show-error --max-time 15 https://dunelegacy.com/relay/v1/health >/dev/null
ALLOWED=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -H 'Origin: https://dunelegacy.com' --data 'app=dunecity' https://dunelegacy.com/relay/v1/admission/host)
DENIED=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -H 'Origin: https://untrusted.invalid' --data 'app=dunecity' https://dunelegacy.com/relay/v1/admission/host)
[[ $ALLOWED == 400 && $DENIED == 403 ]]
ACTIVATING=0
echo "Relay $REVISION is installed. Verify public HTTPS/WSS and signed SQLite delivery before publishing the game."
