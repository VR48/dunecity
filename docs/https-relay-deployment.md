# HTTPS relay on the existing Apache host

The website deployment account has no sudo access, and the current Apache has no
proxy modules. HTTPS polling keeps the relay on the same machine: Apache's existing
TLS listener executes a fixed-route PHP gateway, which forwards to Node on
127.0.0.1:18787. It does not require another host, another public port, or an Apache
restart. Existing WebSocket clients/servers remain supported where available.

## Message delivery

Admission and public lobby endpoints retain their existing request formats. The
admission answer advertises `https://dunelegacy.com/relay/v1/poll`. Browser and
native adapters select HTTP for this URL and preserve existing binary game frames.

- POST `/open` with empty octet-stream body returns a fresh 64-hex session token
  followed by LF. The game sends its existing authenticated HELLO afterwards.
- POST `/exchange`, `X-Dune-Session` header: `DHP1`, little-endian uint32 sequence
  starting at one, uint32 frame count, then uint32 length and bytes per frame.
- Reply: `DHR1`, matching uint32 sequence, uint16 close code (zero while open),
  uint16 frame count, then the same length-prefixed frames.
- Each batch is at most 1MiB/64 frames; an individual frame is at most 262144 bytes.
  Validate the entire envelope before applying any frame. No trailing bytes.
- Only one exchange may be in flight. Identical last-sequence retries return the
  exact cached response, without applying incoming commands twice. Changed replay,
  skipped sequence and concurrent requests are refused. The next sequence
  acknowledges the previous answer. Overflow ends the connection.
- Empty polls wait at most 100ms and wake when a message arrives. Client request
  starts are at least 25ms apart. Retries use identical bytes and bounded backoff.
- POST `/close` ends membership. Idle and closed-session expiry also reclaim clients
  that vanish. Closed tokens cannot occupy capacity forever by repeated polling.

The server uses the existing grant, room, role, routing, heartbeat and command
validation handlers for both transports. Queue overflow ends the connection; it
never silently drops a continuing lockstep stream.

## Restricted deployment

The public gateway only accepts its exact route table, HTTPS, valid methods,
allowlisted browser origins, bounded bodies, and expected content types. No query
parameters or caller-chosen destinations are forwarded. It creates the upstream
client IP from Apache's REMOTE_ADDR, strips ambient cookies/authorization and
spoofable proxy headers, and supplies a private gateway key. Node accepts that key
only over loopback. Session tokens stay in headers, never access-log URLs.

Twelve precreated flock slots serve exchanges and four separate slots serve
admission, chat and close requests. They cap PHP requests waiting on the relay. Additional
requests receive 503 promptly. This deployment caps polling sessions at 12 (four per address),
with independent request, frame, byte and queue limits. Polling ingress charges declared bytes before buffering or hashing, including retries: 2MiB/s per session, 4MiB/s per address, 8MiB/s globally. Failed handshakes release their session immediately when closed. This is an initial capacity
limit for the existing one-core, 1GB website server, not a benchmark guarantee.

Node22.23.2 is installed in the deployment account's private home from the pinned,
SHA256-verified official archive. The relay source and its locked `ws` dependency
are outside the web root. Private gateway and analytics keys are outside the web
root at `/var/www/data/dunecity-relay`, readable by the gateway's www-data group.
Never publish those keys or include them in source bundles.

`deploy/run-user-relay.sh` uses an exclusive flock, a clean environment, a 192MiB
V8 heap limit, bounded append logs, and a fail-closed Landlock launcher. Landlock
ABI4 is verified available on this host; it denies filesystem writes, reading
unlisted private files, and executing unlisted programs. TCP binding is limited
to18787, and outbound TCP to443/53. The relay itself binds loopback only. ABI4 does
not restrict UDP or Unix-domain sockets; Node permissions are additional defense
in depth, not a substitute for the kernel restriction. No SSH agent or deployment
environment is inherited. A minute cron watchdog and @reboot entry supervise the
user-owned process; failed sandbox setup must never fall back to unrestricted Node.

## Acceptance and rollback

Before switching the public browser build, verify the final committed source,
Node/PHP/client tests, live TLS gateway headers and authentication, runtime sandbox,
actual browser/native joining and game digests, signed SQLite delivery, and an
Apache load check. Local transport tests are not proof of a public match.

Back up SQLite using its backup API before deploying receiver/schema changes.
Preserve existing cron entries when adding the managed relay block. Record prior
website files and the current release symlink. Rollback removes only the managed
cron block, terminates the recorded relay PID, restores the prior symlink and
website files, and verifies the ordinary website still responds. Do not delete the
legacy metaserver data or restore SQLite over newer game records casually.

## Verified Apache ingress configuration

Read from the active host on12September2026: prefork MaxRequestWorkers150,
KeepAliveTimeout5s, RequestReadTimeout headers20–40s at500bytes/s and body10s
with500bytes/s minimum rate; PHP max_input_time60s/max_execution_time30s.
The new directory limits bodies to1MiB. Slow clients can still occupy Apache
workers before PHP admission; the gateway lock pool bounds upstream waits only,
not all Apache ingress. Root access is required to tighten the vhost's body
maximum deadline or worker policy. Do not describe the PHP locks as a whole-site
DoS defense. Live acceptance must confirm slow-body rejection and website
responsiveness under bounded test load.

## Candidate verification — 13 September 2026

Candidate 1.0.658 remains on the hardening branch; the public website and cron service
have not been switched. Native and browser builds compile. Six CTest targets,
200 Node tests, the wasm32 wire harness (221 checks), PHP gateway contract tests,
and signed Node-to-PHP SQLite delivery pass. Browser testing reproduced and fixed
an EM_JS regex escape failure during open. The generated-JavaScript regression
now checks successful open plus declared and streamed response size limits.

Two native clients exchanged normal traffic and 48 large messages intact over
local HTTP polling. Two browser game clients joined a public room and entered a
match. A duplicate default player name previously hid the actionable refusal and
disabled the menu; handshake refusals now preserve their reason, the lobby allows
retry, and confirmed chat names can be changed. The public Join Game action is a
separate, spaced button. Production Apache load, final immutable-artifact security
review, native/browser gameplay over public TLS and watchdog acceptance remain
release gates. The current launcher only restarts an exited process; it does not
yet detect and recover a live but wedged process. Do not call this a deployed release.

## Production acceptance in progress — 13 September 2026

The public gateway and schema-2 receiver are installed for acceptance, while the
ordinary Play client is unchanged. The receiver is restricted to the server's
own addresses in Apache and still requires its independent HMAC. The gateway
is now also tracked in the website repository, so ordinary rsync deployments
will preserve it. SQLite's online backup is at
`~/dunecity-relay/backups/pre-http-abe9901/games.sqlite`; migration and relay-only
rollback were verified on that copy with every legacy row preserved. The live
database passed integrity_check after adding the relay objects.

Two native transport clients passed through public TLS with 17 matching digest
comparisons each. Their created/joined/started/left/closed records reached actual
SQLite as https-poll/native. A four-worker, 24-request bounded test kept the
website responsive, both from the Mac and on the server. One incomplete upload
was rejected with HTTP400 after about10 seconds while the ordinary page remained
responsive. This is not a capacity or DDoS benchmark. Browser/native gameplay,
actual watchdog recovery and release publication are still being verified.

The supervisor and manifest gates are now implemented. Linux fixture checks
cover healthy/exited/hung children, abrupt supervisor death, duplicate cron
launches and modified artifacts: 34 manifest and42 supervisor checks pass.
The supervisor installs a Linux parent-death signal so SIGKILL/OOM cannot leave
an orphan child occupying the port. Neither manifest drift checks nor Landlock
protect against compromise of the deployment account itself.
