# DuneCity room relay

A room-scoped WebSocket relay for DuneCity crossplay: browser↔browser and updated
desktop↔browser games. Legacy desktop clients are untouched and keep using ENet LAN/direct
Internet play and the existing metaserver.

The wire contract is [`docs/room-relay-protocol.md`](../../docs/room-relay-protocol.md). The C++
client implements the same document in `include/Network/RoomRelayProtocol.h`.

## What it is and is not

It routes game payloads inside a room, assigns peer identity from the connection, and applies
admission, role, phase and rate policy. It is not an authoritative simulation and not
anti-cheat: a player admitted to a room can still run a modified client, exactly as on ENet.

## Install and test

```bash
cd tools/room-relay
npm ci
npm test
```

`npm test` runs six suites with Node's built-in test runner:

| Suite | Covers |
| --- | --- |
| `test/protocol.test.js` | envelope encode/decode, truncation, trailing bytes, oversized and wrapping lengths, charset limits |
| `test/rooms.test.js` | room codes, grant single use and expiry, seat accounting, reaping, caps |
| `test/admission.test.js` | HTTP admission, field validation, body limits, Origin allowlist, rate limits, logging hygiene |
| `test/e2e.test.js` | real `ws` clients: membership, routing, phases, co-op continuation, diagnostics, large payloads, shutdown |
| `test/abuse.test.js` | grant replay/expiry, foreign origins, wrong roles, banned packet types, cross-room routing, full rooms, deadlines, floods, backpressure |
| `test/analytics.test.js` | lifecycle DTO schema, configuration and startup failures, HMAC over exact bytes, idempotent retries, queue drops, timeouts, refused redirects, TLS verification, full relay lifecycle into a captured receiver |

The TLS tests generate a throwaway certificate with the `openssl` binary and skip themselves if
it is unavailable.

## Run it

Development, loopback, plain `ws://` (this is what the game's development endpoint option
expects):

```bash
npm run dev            # listens on 127.0.0.1:8787
curl -s http://127.0.0.1:8787/v1/health
```

Production shape — the relay itself never terminates TLS:

```bash
RELAY_HOST=127.0.0.1 \
RELAY_PORT=8787 \
RELAY_PUBLIC_URL=wss://relay.example.net/v1/socket \
RELAY_OBSERVED_TRANSPORT=wss \
RELAY_ALLOWED_ORIGINS=https://dunecity.example \
RELAY_TRUST_FORWARDED_FOR=1 \
RELAY_GAME_PROTOCOL=5 \
npm start
```

| Variable | Meaning |
| --- | --- |
| `RELAY_HOST` / `RELAY_PORT` | Bind address. Keep it on loopback behind the reverse proxy. |
| `RELAY_PUBLIC_URL` | The `wss://` URL clients are told to connect to. Required, and must be `wss://`, unless `--dev`. |
| `RELAY_OBSERVED_TRANSPORT` | What the proxy actually serves; recorded in lifecycle logs as server-observed. |
| `RELAY_ALLOWED_ORIGINS` | Comma-separated exact browser origins. Empty means "no browser origin is accepted". `null` is refused outright. |
| `RELAY_TRUST_FORWARDED_FOR` | Only enable when exactly one trusted proxy sits in front; the last `X-Forwarded-For` hop is then used for rate limiting. |
| `RELAY_GAME_PROTOCOL` | Pin `NETWORK_PROTOCOL_VERSION`; `0` accepts any. |
| `RELAY_MAX_ROOMS`, `RELAY_MAX_CONNECTIONS` | Capacity caps. |

## Optional lifecycle delivery to the metaserver

Off unless an operator sets both variables, and off unless `RELAY_OBSERVED_TRANSPORT=wss`:

```bash
DUNE_RELAY_ANALYTICS_URL=https://metaserver.example/relay-events.php \
DUNE_RELAY_ANALYTICS_KEY=<dedicated random secret, >=32 chars> \
npm start
```

It POSTs one signed, fixed-schema lifecycle event per room creation, join, match start, leave
and close. Invitation codes, grants, names, chat and payloads are not part of that schema and
cannot reach it. Setting one variable without the other fails startup. The full contract,
including the remaining variables, is [`docs/room-relay-analytics.md`](../../docs/room-relay-analytics.md).

## Deployment expectations

- Dedicated unprivileged service account, no deploy or SSH keys, no outbound network access.
- TLS terminates at the reverse proxy; the relay listens on loopback.
- Keep its files outside any `rsync --delete` website deploy tree.
- It never opens the production SQLite database. Lifecycle events go to stdout in the schema
  described in [`docs/room-relay-logging.md`](../../docs/room-relay-logging.md). Shipping a
  separate, signed lifecycle summary to the metaserver is the optional integration above: one
  operator-configured outbound HTTPS destination, no database credential, no SQLite access.

## Layout

```
src/constants.js   wire ids, limits, the game-packet authorisation matrix
src/protocol.js    bounded encoder/decoder for the relay envelope
src/limits.js      fixed-window counters and a bounded-cardinality rate table
src/rooms.js       rooms, room codes, single-use grants, capacity and reaping
src/admission.js   HTTPS admission endpoints and their strict parsers
src/logging.js     bounded lifecycle events, no credentials or game content
src/analytics.js   optional signed lifecycle delivery to the metaserver analytics API
src/server.js      HTTP + WebSocket wiring, routing, authorisation, deadlines
src/index.js       entry point and configuration from the environment
```
