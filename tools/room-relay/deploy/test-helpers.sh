#!/usr/bin/env bash
# Fixture tests for the pure helpers in bootstrap.sh.
#
# Sourcing bootstrap.sh stops at its "sourced" guard, so no provisioning runs.
# Every fixture lives in a private temporary directory owned by the invoking
# user: nothing here reads or writes /etc, /root, /opt, or any service, and the
# tests are expected to run unprivileged.
#
#   bash deploy/test-helpers.sh
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=bootstrap.sh
source "$HERE/bootstrap.sh"
set +e   # a failing expectation must not end the run

ME=$(id -u)
PASSED=0
FAILED=0
TMP=$(mktemp -d "${TMPDIR:-/tmp}/dune-relay-helper-tests.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

pass() { PASSED=$((PASSED + 1)); printf 'ok   %s\n' "$1"; }
bad()  { FAILED=$((FAILED + 1)); printf 'FAIL %s\n' "$1"; }

# Helpers exit on refusal, so each expectation runs in a subshell.
accepts() { local desc=$1; shift; if ( "$@" ) >/dev/null 2>&1; then pass "$desc"; else bad "$desc"; fi; }
refuses() { local desc=$1; shift; if ( "$@" ) >/dev/null 2>&1; then bad "$desc (was accepted)"; else pass "$desc"; fi; }
same()    { if [[ $2 == "$3" ]]; then pass "$1"; else bad "$1 (got '$2', wanted '$3')"; fi; }

# Wrappers, so that every expectation runs the helper in this shell rather than
# in a `bash -c` that would fail merely because the function is not defined there.
install_text()      { local text=$1; shift; printf '%s\n' "$text" | install_stream "$@"; }
feed_vhost_map()    { local map=$1; shift; vhost_serves_443 "$@" <<<"$map"; }
checks_manifest()   { ( cd "$1" && sha256sum --check --status SHA256SUMS ); }

# --- require_safe_file / require_safe_dir ---------------------------------
D=$TMP/safe; mkdir -m 0755 -p "$D"
: > "$D/plain"
ln -s "$D/plain" "$D/link"
mkfifo "$D/fifo"
mkdir -m 0700 "$D/dir"
accepts 'require_safe_file accepts a missing path'      require_safe_file "$D/absent" "$ME"
accepts 'require_safe_file accepts an owned regular file' require_safe_file "$D/plain" "$ME"
refuses 'require_safe_file refuses a symlink'           require_safe_file "$D/link" "$ME"
refuses 'require_safe_file refuses a fifo'              require_safe_file "$D/fifo" "$ME"
refuses 'require_safe_file refuses a directory'         require_safe_file "$D/dir" "$ME"
refuses 'require_safe_file refuses a foreign owner'     require_safe_file "$D/plain" 0
accepts 'require_safe_dir accepts an owned 0700 dir'    require_safe_dir "$D/dir" "$ME"
chmod 0777 "$D/dir"
refuses 'require_safe_dir refuses a world-writable dir' require_safe_dir "$D/dir" "$ME"
chmod 0755 "$D/dir"
accepts 'require_safe_dir accepts an owned 0755 dir'    require_safe_dir "$D/dir" "$ME"
refuses 'require_safe_dir refuses a symlinked dir'      require_safe_dir "$D/link" "$ME"
refuses 'require_safe_dir refuses a regular file'       require_safe_dir "$D/plain" "$ME"

# --- install_stream --------------------------------------------------------
D=$TMP/install; mkdir -p "$D"
printf 'secret=1\n' | install_stream 0600 "$D/env" "$ME"
same 'install_stream writes the streamed content' "$(<"$D/env")" 'secret=1'
same 'install_stream applies the requested mode'  "$(stat -c %a "$D/env")" '600'

# A pre-existing world-readable secret file must not keep its mode or its inode.
printf 'old\n' > "$D/leaky"; chmod 0666 "$D/leaky"
OLD_INODE=$(stat -c %i "$D/leaky")
printf 'new\n' | install_stream 0600 "$D/leaky" "$ME"
same 'install_stream replaces a world-readable mode' "$(stat -c %a "$D/leaky")" '600'
same 'install_stream replaces the content'           "$(<"$D/leaky")" 'new'
if [[ $(stat -c %i "$D/leaky") != "$OLD_INODE" ]]; then
  pass 'install_stream installs by rename, not in-place rewrite'
else
  bad 'install_stream installs by rename, not in-place rewrite'
fi

printf 'outside\n' > "$D/outside"
ln -s "$D/outside" "$D/redirect"
refuses 'install_stream refuses a symlinked destination' \
  install_text attack 0600 "$D/redirect" "$ME"
same 'install_stream leaves the symlink target untouched' "$(<"$D/outside")" 'outside'
same 'install_stream leaves no staged temporary files' \
  "$(find "$D" -name '.dune-relay-install.*' | wc -l)" '0'

# --- record_tree / verify_tree --------------------------------------------
make_tree() {
  rm -rf "$1"
  mkdir -m 0755 -p "$1"
  mkdir -m 0755 "$1/bin" "$1/lib"
  printf '#!/bin/sh\n' > "$1/bin/node"; chmod 0755 "$1/bin/node"
  printf 'cli\n' > "$1/lib/npm-cli.js"; chmod 0644 "$1/lib/npm-cli.js"
  ln -s ../lib/npm-cli.js "$1/bin/npm"
  record_tree "$1"
}
T=$TMP/tree
make_tree "$T"
accepts 'verify_tree accepts the recorded tree'          verify_tree "$T" fixture "$ME"
same 'the inventory records the symlink target' \
  "$(grep ' ./bin/npm ' "$T/.installed-inventory")" 'l ./bin/npm ../lib/npm-cli.js'

ln -sfn ../lib/other.js "$T/bin/npm"
refuses 'verify_tree refuses a retargeted symlink'       verify_tree "$T" fixture "$ME"
make_tree "$T"; rm "$T/lib/npm-cli.js"
refuses 'verify_tree refuses a missing entry'            verify_tree "$T" fixture "$ME"
make_tree "$T"; printf 'x\n' > "$T/lib/extra.js"
refuses 'verify_tree refuses an extra entry'             verify_tree "$T" fixture "$ME"
make_tree "$T"; mkdir -m 0755 "$T/lib/extra"
refuses 'verify_tree refuses an extra directory'         verify_tree "$T" fixture "$ME"
make_tree "$T"; printf 'tampered\n' > "$T/lib/npm-cli.js"
refuses 'verify_tree refuses modified content'           verify_tree "$T" fixture "$ME"
make_tree "$T"; chmod 0666 "$T/lib/npm-cli.js"
refuses 'verify_tree refuses a world-writable file'      verify_tree "$T" fixture "$ME"
make_tree "$T"
refuses 'verify_tree refuses a foreign owner'            verify_tree "$T" fixture 0
rm -f "$T/.installed-inventory"
refuses 'verify_tree refuses a tree with no inventory'   verify_tree "$T" fixture "$ME"

# --- manifest_covers -------------------------------------------------------
M=$TMP/SHA256SUMS
H=0000000000000000000000000000000000000000000000000000000000000000
{ printf '%s  src/index.js\n' "$H"
  printf '%s  ./REVISION\n' "$H"
  printf '%s *package.json\n' "$H"
  printf '%s  src/index-js\n' "$H"; } > "$M"
accepts 'manifest_covers accepts a plain entry'          manifest_covers "$M" src/index.js
accepts 'manifest_covers accepts a ./ prefixed entry'    manifest_covers "$M" REVISION
accepts 'manifest_covers accepts a binary-mode entry'    manifest_covers "$M" package.json
accepts 'manifest_covers accepts several names at once'  manifest_covers "$M" REVISION package.json
refuses 'manifest_covers refuses an unlisted name'       manifest_covers "$M" package-lock.json
refuses 'manifest_covers refuses when one of several is unlisted' \
  manifest_covers "$M" REVISION deploy/apache.conf
refuses 'manifest_covers does not let . match any character' manifest_covers "$M" src/index-js.

# --- assert_health ---------------------------------------------------------
GOOD=$'status=ok\nprotocol=1\nrooms=0\nconnections=0'
accepts 'assert_health accepts a relay health body'      assert_health "$GOOD" test 1
refuses 'assert_health refuses another protocol'         assert_health "$GOOD" test 2
refuses 'assert_health refuses a non-ok status'          assert_health "${GOOD/status=ok/status=degraded}" test 1
refuses 'assert_health refuses a foreign 2xx body'       assert_health '<html>OK</html>' test 1
refuses 'assert_health refuses an empty body'            assert_health '' test 1
refuses 'assert_health refuses a substring match on status' \
  assert_health $'xstatus=okay\nprotocol=1\nrooms=0\nconnections=0' test 1

# --- vhost_serves_443 ------------------------------------------------------
VH=/etc/apache2/sites-enabled/dunelegacy-le-ssl.conf
NAMED=$(cat <<'MAP'
VirtualHost configuration:
*:80                   is a NameVirtualHost
         default server dunelegacy.com (/etc/apache2/sites-enabled/000-default.conf:1)
         port 80 namevhost dunelegacy.com (/etc/apache2/sites-enabled/000-default.conf:1)
*:443                  is a NameVirtualHost
         default server other.example (/etc/apache2/sites-enabled/other-le-ssl.conf:2)
         port 443 namevhost other.example (/etc/apache2/sites-enabled/other-le-ssl.conf:2)
         port 443 namevhost dunelegacy.com (/etc/apache2/sites-enabled/dunelegacy-le-ssl.conf:2)
                 alias www.dunelegacy.com
MAP
)
SINGLE=$(printf 'VirtualHost configuration:\n*:443                  dunelegacy.com (%s:2)\n' "$VH")
PORT80=$(printf 'VirtualHost configuration:\n*:80                   dunelegacy.com (%s:2)\n' "$VH")
ELSEWHERE=$(printf 'VirtualHost configuration:\n*:443                  dunelegacy.com (%s:2)\n' \
  /etc/apache2/sites-enabled/999-catchall.conf)
ALIASONLY=$(printf 'VirtualHost configuration:\n*:443                  www.dunelegacy.com (%s:2)\n' "$VH")

accepts 'vhost_serves_443 accepts a namevhost entry' \
  feed_vhost_map "$NAMED" dunelegacy.com "$VH"
accepts 'vhost_serves_443 accepts the single-vhost line form' \
  feed_vhost_map "$SINGLE" dunelegacy.com "$VH"
accepts 'vhost_serves_443 accepts any of several candidate files' \
  feed_vhost_map "$SINGLE" dunelegacy.com /etc/apache2/sites-available/dunelegacy-le-ssl.conf "$VH"
refuses 'vhost_serves_443 refuses a match on port 80 only' \
  feed_vhost_map "$PORT80" dunelegacy.com "$VH"
refuses 'vhost_serves_443 refuses another file serving the name' \
  feed_vhost_map "$ELSEWHERE" dunelegacy.com "$VH"
refuses 'vhost_serves_443 does not accept a longer hostname' \
  feed_vhost_map "$ALIASONLY" dunelegacy.com "$VH"
refuses 'vhost_serves_443 refuses an empty map' \
  feed_vhost_map '' dunelegacy.com "$VH"

# --- the documented bundle manifest recipe --------------------------------
# The manifest the README tells the operator to build must satisfy exactly the
# gates bootstrap.sh applies to it, REVISION included.
B=$TMP/bundle
mkdir -m 0755 -p "$B/src" "$B/deploy"
printf 'x\n' | tee "$B/src/index.js" "$B/src/constants.js" "$B/package.json" \
  "$B/package-lock.json" "$B/deploy/apache.conf" "$B/deploy/dunecity-relay.service" \
  "$B/deploy/bootstrap.sh" >/dev/null
REV=4940aef0000000000000000000000000000000ab
printf '%s\n' "$REV" > "$B/REVISION"
( cd "$B" && find . -type f ! -name SHA256SUMS -printf '%P\n' | LC_ALL=C sort \
    | xargs sha256sum > SHA256SUMS )
mapfile -t COVERED < <(cd "$B" && find src -type f | LC_ALL=C sort)
COVERED+=(REVISION package.json package-lock.json
          deploy/bootstrap.sh deploy/apache.conf deploy/dunecity-relay.service)
accepts 'the documented manifest covers every gated bundle path' \
  manifest_covers "$B/SHA256SUMS" "${COVERED[@]}"
accepts 'the documented manifest verifies with sha256sum --check' checks_manifest "$B"
same 'REVISION reads back exactly as the expected argument' "$(<"$B/REVISION")" "$REV"
printf '%s\nextra\n' "$REV" > "$B/REVISION"
if [[ $(<"$B/REVISION") == "$REV" ]]; then
  bad 'a REVISION file with trailing junk is rejected'
else
  pass 'a REVISION file with trailing junk is rejected'
fi

printf '\n%d passed, %d failed\n' "$PASSED" "$FAILED"
[[ $FAILED == 0 ]]
