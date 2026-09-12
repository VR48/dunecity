#!/bin/bash
# Invoked by cron and @reboot; an exclusive flock allows one process only.
set -euo pipefail
base="/home/dunelegacy-deploy/dunecity-relay"
private="/var/www/data/dunecity-relay"
/usr/bin/python3 "$base/current/deploy/trim-user-log.py"
exec 9>"$base/run.lock"
flock -n 9 || exit 0
exec >>"$base/service.log" 2>&1
release=$(readlink -f "$base/current")
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
