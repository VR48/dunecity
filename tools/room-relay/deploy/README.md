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
source, package lock, and deployment template. Do not run an uploaded mutable
script directly with sudo. Never paste the analytics key into a task or log.

```
bash /root/relay-bundle/deploy/bootstrap.sh /root/relay-bundle FULL_GAME_COMMIT MANIFEST_SHA256
```

The script pins Node22.23.2 and verifies the official archive hash, stages runtime
and dependencies atomically, verifies cached manifests on reuse, and backs up
Apache configuration, service configuration and the previous release symlink.
Activation failure restores these and the previous running/enabled service state.
Packages, the service account, an unused staged release and the private backup
remain after a failed activation for inspection/retry; existing game data is never
removed. Only the canonical existing HTTPS vhost receives the two Include lines.

It verifies loopback/public HTTPS health and allowed/denied browser origins.
**Successful installation is not release acceptance:** before publishing clients,
verify public WebSocket admission/upgrade, a real browser/native match, forwarding
header replacement and signed lifecycle delivery plus production SQLite readback.
The PHP receiver must be deployed from the reviewed website branch and its schema
backed up before enabling analytics. Keep the game release blocked if these checks
cannot be run. TLS verification must remain enabled throughout.

For rollback after successful installation, use the private directory printed in
`/root/dune-relay-rollback.*`: restore the numbered files in the order recorded by
bootstrap's PATHS array and restore `/etc/dunecity-relay/previous-release` as the
current symlink. Validate Apache before reload; reload systemd and restart the
previous relay. Prefer the automatic rollback during failed activation.
