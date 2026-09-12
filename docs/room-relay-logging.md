# Relay lifecycle logging and metaserver integration

This document is the contract between the relay in `tools/room-relay` and the website
metaserver repository. The relay produces events; the metaserver decides what to persist. Codex
owns the metaserver side. Nothing here connects to the production SQLite database, and nothing
here changes the existing metaserver text endpoints.

## 1. What the relay emits

One JSON object per line on stdout. Field values are clamped to 64 printable ASCII characters;
numbers are finite or replaced by 0. Unknown event names are rejected by the emitter itself, so
the schema cannot drift silently (`ALLOWED_EVENTS` in `src/logging.js`).

| Event | When | Fields |
| --- | --- | --- |
| `relay_started` | process start | `host`, `port`, `protocol`, `transport` |
| `relay_stopped` | graceful shutdown | – |
| `room_created` | host admission succeeds | `room`, `mode`, `maxPeers`, `gameProtocol`, `appVersion`, `hostRuntime`, `transport`, `addressTag` |
| `participant_joined` | a peer completes the handshake | `room`, `peerId`, `role`, `runtime`, `appVersion`, `transport`, `addressTag` |
| `room_phase` | the host declares lobby/match | `room`, `phase`, `byPeerId` |
| `participant_left` | a peer leaves for any reason | `room`, `peerId`, `role`, `runtime`, `appVersion`, `reason`, `transport`, `runtimeMs` |
| `room_closed` | room terminated | `room`, `code`, `reason`, `peers` |
| `admission_denied` | an HTTPS admission request is refused | `endpoint`, `code`, `addressTag` |
| `connection_denied` | a socket is refused before or during the handshake | `code`, `reason`, `addressTag` |
| `message_refused` | a message fails authorisation | `room`, `peerId`, `code`, `detail` |

Every event carries `ts` (ISO 8601) and `event`.

## 2. What is never logged

- Grants, in any form. A test asserts that no lifecycle event contains an issued grant.
- Chat text, command streams, selection lists, map bytes or any game payload content.
- Raw client addresses. `addressTag` is a per-process, salted SHA-256 prefix: it correlates
  repeated behaviour within one relay run and is meaningless afterwards and across runs.

The room code *is* logged, because it is the only handle an operator has on a session and it
stops working the moment the room closes.

## 3. Trust labels

Two fields look similar and must not be merged:

- `transport` is **server observed**. It is what the relay's own configuration says the reverse
  proxy served (`wss` in production, `ws` for loopback development). It is not a client claim.
- `runtime` (`native` / `browser`) and `appVersion` are **client reported**. A modified client
  can put anything acceptable there. Store them as claims.

The existing metaserver `client_runtime` column records *the reporter* of a match, not every
participant. Do not overload it. A mixed desktop/browser match needs per-participant rows.

## 4. Required metaserver integration

Codex owns this work in the website repository. The requirements the relay side depends on:

1. **Additive schema.** Add tables/columns; do not change the meaning of existing rows. Existing
   `list`/`list2` responses must keep their current field counts and ordering, because the old
   native client's parser stops at the first row it does not understand
   (`src/Network/MetaServerClient.cpp`).
2. **A new authenticated internal endpoint**, e.g. `POST /internal/relay/events`, that accepts a
   batch of the events above. Authentication is a server-to-server credential held in the
   metaserver's existing secret storage. The relay must not receive database credentials, a
   database socket, or SSH access.
3. **Suggested shape** (names are a proposal; the metaserver owns them):

   ```sql
   CREATE TABLE relay_session (
       id             INTEGER PRIMARY KEY,
       room_code      TEXT    NOT NULL,
       mode           TEXT    NOT NULL,      -- coop | custom
       max_peers      INTEGER NOT NULL,
       game_protocol  INTEGER NOT NULL,
       transport      TEXT    NOT NULL,      -- server observed
       created_at     TEXT    NOT NULL,
       closed_at      TEXT,
       close_reason   TEXT
   );

   CREATE TABLE relay_participant (
       id                  INTEGER PRIMARY KEY,
       session_id          INTEGER NOT NULL REFERENCES relay_session(id),
       peer_id             INTEGER NOT NULL,
       role                TEXT    NOT NULL, -- host | client
       reported_runtime    TEXT    NOT NULL, -- client claim
       reported_version    TEXT    NOT NULL, -- client claim
       joined_at           TEXT    NOT NULL,
       left_at             TEXT,
       leave_reason        TEXT,
       runtime_ms          INTEGER
   );
   ```

   All writes parameterised; every text column length-capped on insert.
4. **Retention.** Decide and document a retention window for `relay_participant`. `addressTag`
   should not be persisted at all unless there is a stated abuse-handling reason and a retention
   period to go with it.
5. **Rate and size limits** on the ingestion endpoint, since it is another network surface.

## 5. What relay logs cannot do

- They cannot advertise relay rooms to old native clients. The legacy `list`/`list2` endpoints
  describe UDP hosts with an address and a port; a relay room has neither, and an old client that
  received one would try to open a UDP socket to nonsense. Relay discovery needs a separate,
  capability-aware endpoint that old clients never request. **Room codes are the discovery
  mechanism in v1** and need no new endpoint at all.
- They are not an audit trail. Like the existing `gamestats` submissions, they are reports from a
  component, useful for operations and debugging, and not evidence of what a player did.
