# Relay lifecycle delivery to the metaserver analytics API

The relay can report room and participant lifecycle to the additive metaserver endpoint
described in `metaserver/ANALYTICS.md` (website repository). This is **optional, off by default,
and service-only**: no player, browser or game client can enable it, target it, or influence
what it sends.

Producer: `tools/room-relay/src/analytics.js`. Diagnostic stdout logging is a separate mechanism
with a separate schema — see [`room-relay-logging.md`](room-relay-logging.md).

## 1. Configuration

| Variable | Meaning |
| --- | --- |
| `DUNE_RELAY_ANALYTICS_URL` | Destination. `https://` required, or loopback `http://` for development. No userinfo, no fragment. |
| `DUNE_RELAY_ANALYTICS_KEY` | Shared HMAC secret, 32..512 printable ASCII characters. Relay and receiver only; never in a client bundle, never a deployment credential. |
| `DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP` | `1` to permit plain `http://` to `127.0.0.1`, `::1` or `localhost`. Development only. |
| `DUNE_RELAY_ANALYTICS_CA_FILE` | Optional PEM whose CAs are the *only* ones trusted for the destination. |
| `DUNE_RELAY_ANALYTICS_TIMEOUT_MS` | Per-request timeout, default 5000. |
| `DUNE_RELAY_ANALYTICS_ATTEMPTS` | Attempts per event including the first, default 3, maximum 10. |
| `DUNE_RELAY_ANALYTICS_QUEUE_EVENTS` | Queue bound in events, default 256. |
| `DUNE_RELAY_ANALYTICS_QUEUE_BYTES` | Queue bound in bytes, default 262144. |

Rules the loader enforces at startup, before the socket is bound:

- **Neither URL nor key set: disabled.** This is the default and is not an error.
- **One of the two set: startup fails** with a message that names the variable and never quotes
  its value, so a mistyped key cannot land in a service log.
- **Delivery also requires `RELAY_OBSERVED_TRANSPORT=wss`.** A development relay serving plain
  `ws` reports nothing rather than labelling itself as something it is not. The status token
  (`enabled`, `disabled_not_configured`, `disabled_transport_not_wss`) is logged once at startup.

## 2. The event

One event per request. A fresh DTO is constructed from primitives for every event; no room,
connection, or log record is ever forwarded, and the object has exactly these keys in this order:

```json
{
  "schema_version": 1,
  "event_id": "22..64 opaque base64url, unique per event, stable across retries",
  "room_id": "the relay's internal room log id, never the invitation code",
  "kind": "created | joined | started | left | closed",
  "occurred_at": 1757000000,
  "participant_id": 0,
  "client_runtime": "browser | native | unknown",
  "game_version": "",
  "reason": "fixed lowercase code"
}
```

| Kind | Emitted when | `participant_id` | Attribution |
| --- | --- | --- | --- |
| `created` | host admission succeeds | 0 | `unknown` / empty version |
| `joined` | a peer completes the handshake | peer id | client reported |
| `started` | the host moves the room from lobby to match | 0 | `unknown` / empty version |
| `left` | a peer leaves for any reason | peer id | client reported |
| `closed` | the room ends: host left, relay shutdown, or the reaper | 0 | `unknown` / empty version |

An expired room goes through the same close path as any other: the reaper hands it to the server
(`RoomStore.onRoomExpired`), which disconnects the peers still in it, emits their `left` events
with reason `lifetime`, and then closes the room exactly once. Before this, `sweep()` dropped the
room out of the table with no `room_closed`, no participant teardown and live sockets attached to
a room that no longer existed.

`reason` comes from a fixed table, never from free text: `room_created`, `peer_joined`,
`match_started`, `normal`, `timeout`, `protocol_error`, `rate_limited`, `slow_consumer`,
`host_left`, `shutdown`, `lifetime`, `empty`, `unspecified`.

Trust labels are unchanged from the logging contract: `transport=wss` is assigned by the
receiver because only a wss relay reports at all, while `client_runtime` and `game_version` are
client claims. A claim that does not match the schema is clamped to `unknown` / `""` rather than
being allowed to suppress the event.

`room_id` is `Room.logId`, 16 random bytes as base64url, generated per room and independent of
the invitation code and of every grant. A test asserts that the room code, the compact form of
the room code, the issued grants and the player display names appear in no signed body and in no
log record.

## 3. Transport

- `POST` with `content-type: application/json`. Body is at most 4096 bytes; oversize is a
  producer bug and is dropped rather than sent.
- `X-Dune-Relay-Timestamp`: unix seconds, exactly 10 digits.
- `X-Dune-Relay-Signature`: lowercase hex HMAC-SHA256 over the exact bytes
  `timestamp + "\n" + body`, using the shared key.
- Redirects are never followed. A 3xx from the receiver is a permanent failure, and the signed
  body is not re-sent anywhere.
- HTTPS verifies the certificate chain and hostname (`rejectUnauthorized`, TLS 1.2 minimum). A
  verification failure is permanent and is not retried.
- Retries reuse the immutable `event_id` and the byte-identical body, with a fresh timestamp and
  a fresh signature. Retry on timeout, connection failure, 408, 429 and 5xx; never on other 4xx
  or on a redirect.
- Backoff doubles from 250 ms, capped at 4 s, bounded by the attempt count.

## 4. Behaviour under load and failure

- The queue is bounded in **both** events and bytes. When it is full the newest event is dropped
  and counted; a dropped event is never sent later.
- Exactly one request is in flight. Nothing in the relay's message path awaits delivery.
- A failing, hanging, refusing or redirecting receiver cannot stall, disconnect or close a game.
  A test drives a full lobby exchange while every attempt fails.
- Shutdown gives the in-flight drain a bounded window (2 s by default), then hard-stops: the
  socket is destroyed, the queue is discarded, and the process exits.
- Failures are reported to stdout in aggregate only (`analytics_delivery`: accepted, delivered,
  failed, dropped, retried) and at most once per minute. Never logged: the key, the destination,
  request bodies, or per-event failures.

## 5. Deployment notes

- The key is a dedicated random secret shared only by the relay service and the receiver. It is
  not the SSH or deployment credential, and it is not in any client bundle.
- Ensure the reverse proxy in front of the receiver does not log the signature header or request
  bodies.
- Clocks must be synchronised: the receiver's acceptance window is ±300 s.
- The relay still holds no database credential and opens no SQLite file. Delivery is one
  outbound HTTPS destination, configured by the operator.

## 6. What this is not

These are relay lifecycle records. They are not completed-match records and must not be turned
into `gamestats`-style match rows. They also say nothing about who won, what was played, or
whether a browser and a native client actually finished a game together.
