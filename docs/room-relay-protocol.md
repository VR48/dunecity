# DuneCity room relay protocol v1

Status: implemented. This document is the **shared wire contract**. The Node relay in
`tools/room-relay` and the C++ client in `include/Network/RoomRelayProtocol.h` are both
written against this file; if they disagree with it, the file wins and one of them is a bug.

The relay carries *crossplay* games: browser↔browser and updated-desktop↔browser. It does not
replace the legacy ENet transport. Old desktop clients keep using ENet LAN/direct Internet play
and the existing metaserver; nothing in this document changes the ENet wire format or
`NETWORK_PROTOCOL_VERSION`.

## 1. Roles and vocabulary

| Term | Meaning |
| --- | --- |
| Relay | The Node service in `tools/room-relay`. Terminates WebSocket connections, routes game payloads inside a room. |
| Room | A set of at most `maxPeers` connections that may exchange payloads. Created by a host. |
| Room code | The human-typeable invitation, e.g. `H4PQ-7T2M-9XKB`. High entropy, see §3.3. |
| Grant | A short-lived, single-use admission token issued over HTTPS and presented in the first WebSocket frame. |
| Peer id | A `uint32` **assigned by the relay**. Clients never choose or send their own id. |
| Game payload | An existing DuneCity packet, byte-for-byte as `ENetPacketOStream` produces it. |

The relay is a routing and admission component. It is **not** an authoritative simulation and
not an anti-cheat system: a peer admitted to a room can still lie about anything inside its own
authority, exactly as on ENet.

## 2. Transport

- WebSocket over TLS (`wss://`). Plain `ws://` is accepted by clients **only** for explicit
  loopback development endpoints (see §7).
- Or HTTPS polling, when the admission answer's `url` is an `https://` endpoint instead: the same
  frames, batched inside ordinary HTTPS requests. See
  [room-relay-http-polling.md](room-relay-http-polling.md). Everything in §4, §5 and §6 below
  applies unchanged to those frames; only the pipe differs.
- Binary frames only. A text frame is a protocol error (close `4400`).
- `permessage-deflate` is disabled on the server. A client must not negotiate it.
- Server `maxPayload` is `262144` bytes and is enforced by the WebSocket layer *before* the
  frame body is buffered.
- The relay sends WebSocket pings every 15 s. Browsers cannot originate WebSocket pings from
  JavaScript, so liveness in the browser direction uses the application-level `HEARTBEAT`
  message (§4.2.3).

## 3. Admission over HTTPS

Admission is a separate, bounded HTTP operation. It exists so that room policy, invitation
checks and rate limiting happen before a socket is opened, and so the gameplay socket never
carries a credential in its URL.

### 3.1 Requests

Admission and lobby endpoints take `application/x-www-form-urlencoded` bodies. Unknown keys are ignored.
Every value is length-capped (see §3.4).

```
POST /v1/admission/host
  app=dunecity
  appVersion=1.0.655
  gameProtocol=5
  contentHash=<= 64 chars, [0-9a-f]      ; bundled-content fingerprint, see §6
  runtime=native|browser
  maxPeers=2..8
  mode=coop|custom

POST /v1/admission/join
  app=dunecity
  appVersion=1.0.655
  gameProtocol=5
  contentHash=...
  runtime=native|browser
  room=H4PQ-7T2M-9XKB
```

### 3.2 Responses

`text/plain; charset=utf-8`, LF-separated `key=value` lines. At most 16 lines, at most 512
bytes per line, at most 8192 bytes total. This format exists so the C++ client can parse it
with a strict, bounded parser instead of adding a JSON parser to the game.

Success:

```
status=ok
protocol=1
room=H4PQ-7T2M-9XKB
grant=<64 lowercase hex characters>
grantExpiresMs=30000
maxPeers=2
url=wss://relay.example.net/v1/socket
```

Failure (HTTP 4xx/5xx, same body shape):

```
status=error
code=room_not_found
message=That room code is not open.
```

Defined `code` values: `bad_request`, `unsupported_version`, `content_mismatch`,
`room_not_found`, `room_full`, `room_closed`, `match_in_progress`, `rate_limited`, `capacity`,
`forbidden_origin`.
Clients must treat an unknown code as a generic failure and must show `message` only after
length- and charset-checking it (§3.4).

`GET /v1/health` returns `status=ok` plus `rooms=`/`connections=` counters.

### 3.3 Grants and room codes

- A grant is 32 bytes from a CSPRNG, hex encoded. It is stored server-side with the room id,
  the role (`host`/`client`), an expiry (`grantExpiresMs`, default 30 s) and a consumed flag.
- A grant is **single use**. Consumption is atomic: the entry is deleted from the store before
  the connection is admitted, so a replayed grant fails even if two frames arrive together.
- A room code is 12 characters from the 32-character alphabet `0123456789ABCDEFGHJKMNPQRSTVWXYZ`
  (Crockford base32, no I/L/O/U), drawn from a CSPRNG and formatted `XXXX-XXXX-XXXX`. That is
  60 bits of entropy; guessing is not a practical attack, and room-code lookups are rate limited
  anyway. Codes are matched case-insensitively and dashes are optional on input.
- The *only* thing a room code grants is the right to ask for a client grant. It is never sent
  over the WebSocket.
- A grant records the answers admission was given: `gameProtocol`, `contentHash`, `appVersion`
  and `runtime`. The `HELLO` that redeems it must repeat them exactly, or the socket is closed
  (`4450` for protocol or content, `4401` for version or runtime). Admission decided real things
  from those answers — which room the code matched, whether the content agreed — so a handshake
  that says something else is either a grant taken by a different client or one client telling
  two stories. This is **consistency, not authentication**: `runtime` in particular stays a
  client claim, and a peer that lies consistently is still believed.
- A grant is bound to the room phase it was issued in. A phase change invalidates every
  outstanding grant, and redeeming one afterwards consumes it and fails with `4401`, whichever
  order the start and the handshake arrive in.
- Once a room has entered `MATCH`, `/v1/admission/join` answers `409 match_in_progress` for the
  rest of that room's life, including after the host returns the room to the lobby for a co-op
  intermission. There is no snapshot or reconnect protocol, so a late arrival has nothing to
  join; existing members are unaffected. This also means a host cannot fill an empty seat during
  a running match.

### 3.4 Field limits

| Field | Limit |
| --- | --- |
| `app` | 32 bytes, `[A-Za-z0-9_-]` |
| `appVersion` | 32 bytes, `[A-Za-z0-9._-]` |
| `gameProtocol` | integer 0..65535 |
| `contentHash` | 64 bytes, `[0-9a-f]` |
| `runtime` | exactly `native` or `browser` |
| `mode` | exactly `coop` or `custom` |
| `maxPeers` | integer 2..8 |
| `room` | 16 bytes after dash removal, alphabet above |
| `message` (response) | 200 bytes, printable ASCII, no control characters |

Values that exceed a limit are a `bad_request`; they are never truncated and used.

### 3.4.1 Ingress deadlines and connection ceiling

A size cap bounds what a request may say, not how long it may take to say it. The relay also
applies, with defaults in `LIMITS`:

| Bound | Default | Effect |
| --- | --- | --- |
| body read deadline | 3 s | `408 timeout`, then the socket is dropped mid-upload |
| headers deadline | 5 s | Node answers `408` and closes |
| whole-request deadline | 10 s | request aborted |
| keep-alive idle | 5 s | connection closed |
| idle socket | 15 s | socket destroyed |
| header bytes / count | 8192 / 64 | request refused |
| concurrent admission sockets | 128 | further connections are dropped without a response |

The socket ceiling covers sockets that have **not** upgraded. A WebSocket leaves that budget at
upgrade, has its idle timeout cleared, and is governed by `maxConnections` and the liveness
deadline instead. These bounds are defence in depth behind the reverse proxy, not a replacement
for it: the proxy remains responsible for TLS, its own body and connection limits, and for being
the only thing that can reach the relay's loopback port.

### 3.5 Origin

If an `Origin` header is present it must match the configured exact allowlist. `null` and any
origin not on the list are rejected (`forbidden_origin`, and close `4403` on the socket).
Requests with no `Origin` header are accepted because native clients do not send one — this is
**not** treated as proof of a native client. Authentication is the grant; Origin is defence in
depth for browsers only.

Allowlist entries must be canonical origins: `scheme://host[:port]`, `http` or `https`, with a
non-default port only, and no credentials, path, query, fragment or trailing slash. Anything
else could never equal a real `Origin` header, so it is refused at startup rather than becoming
a silently dead entry. The literal `null` is refused by name as well.

### 3.6 CORS

A browser may *send* an admission POST cross-origin without any CORS involvement — `POST` with
`application/x-www-form-urlencoded` and no custom headers is a simple request — but it cannot
**read** the response, which is where the grant is. So the relay answers with:

| Header | Value |
| --- | --- |
| `Access-Control-Allow-Origin` | the request's exact `Origin`, and only when that origin is on the allowlist |
| `Vary` | `Origin`, on every response |

- Never `*`, and a foreign or `null` origin is never reflected: it gets `403 forbidden_origin`
  with no allow-origin header at all.
- `Access-Control-Allow-Credentials` is never sent. A grant must never be issued on the strength
  of an ambient cookie, so the client uses the default `credentials: 'omit'`.
- Error responses carry the same headers. Without them a browser cannot read the status or the
  `code`, and every refusal is indistinguishable from the relay being down.
- `OPTIONS` on the two admission endpoints answers a preflight for an allowlisted origin with
  `POST`, `content-type` and a 600-second `Access-Control-Max-Age`, i.e. nothing beyond what a
  simple request already allows. It is not needed by a client that sends simple requests, and it
  is not a second way in: it creates nothing, and it is refused for a foreign origin, for a
  method other than `POST`, and for any other path.

## 4. WebSocket protocol

### 4.1 Framing

Every WebSocket binary frame is exactly one relay message:

```
byte 0        : uint8  messageType
byte 1..      : type-specific body
```

All multi-byte integers in the relay envelope are **big-endian**. (Game payloads keep their own
little-endian encoding; the relay never reinterprets them beyond §4.2.2.)

Message ids are split by direction so a confused implementation fails loudly:

| Range | Direction |
| --- | --- |
| `0x01`–`0x3F` | client → relay |
| `0x81`–`0xBF` | relay → client |

A message id outside the range for its direction, a body that is shorter than its fixed header,
or a body with trailing bytes after the declared fields is a protocol error (close `4400`).
Lengths are validated with subtraction against the remaining byte count, never by addition,
because the browser client is wasm32 and `size_t` addition wraps there.

### 4.2 Client → relay

#### 4.2.1 `0x01 HELLO`

Must be the first frame. Must arrive within 5000 ms of the socket opening.

```
u16  relayProtocolVersion      ; must equal 1
u16  gameProtocolVersion       ; NETWORK_PROTOCOL_VERSION, echoed in WELCOME
u8   grantLen                  ; 1..64
...  grant bytes               ; ASCII hex
u8   runtimeLen                ; 1..16
...  runtime                   ; "native" | "browser"  (client claim, logged as such)
u8   appVersionLen             ; 1..32
...  appVersion
u8   contentHashLen            ; 0..64
...  contentHash
u8   nameLen                   ; 1..64
...  displayName               ; same rules as NetworkPacketPolicy::isAcceptablePlayerName
```

The room and the role come from the grant, not from this frame. A client cannot ask to be the
host, cannot pick its room and cannot pick its peer id.

Failure modes: `4450` on relay version mismatch, on a game protocol that differs from the room's,
or on a `contentHash` that differs from the one the grant was issued for; `4401` on an
unknown/expired/consumed grant, on a grant invalidated by a phase change, or on an `appVersion`
or `runtime` that differs from the one the grant was issued for; `4409` if the room filled while
the grant was outstanding; `4403` if the room already has a host or if `displayName` is already
used in that room; `4400` on a malformed frame.

The display-name check matters because the game resolves a command list to a player by name;
two players in one room sharing a name would let one take over the other's commands. The relay
is the only component that sees both names before the match starts.

#### 4.2.2 `0x02 RELAY`

Carries one game payload.

```
u32  recipient      ; 0 = every other peer in the room, else a peer id in this room
u8   channel        ; 0 or 1 (mirrors the two ENet channels)
u8   flags          ; bit0 = sender asked for reliable delivery (informational; WS is always reliable+ordered)
                    ; every other bit is undefined and must be zero
u16  gameMessageType; the payload's own packet id, declared for relay-side authorisation
u32  payloadLen     ; 4 .. 262128
...  payload        ; the serialized DuneCity packet, unchanged
```

The relay checks `payloadLen >= 4` and that the payload's own little-endian `uint32` header
equals `gameMessageType`. A mismatch is a protocol error (`4400`). The relay does not otherwise
parse the payload.

A `channel` above 1 and any undefined `flags` bit are also protocol errors (`4400`), and the
frame is not forwarded to anybody. Neither is a field the relay can interpret on the sender's
behalf: a receiver that reads a bit the relay ignored would be acting on a meaning the two ends
never agreed. `RoomRelayProtocol.h` refuses both on the way in as well.

Authorisation is applied to `gameMessageType` before routing — see §5.

#### 4.2.3 `0x03 HEARTBEAT`

```
u32  clientTimeMs   ; opaque echo value
```

Sent every 5000 ms. Any frame refreshes liveness; `HEARTBEAT` exists so an idle or backgrounded
client still proves it is alive, and so the client can measure round-trip time.

#### 4.2.4 `0x04 LEAVE`

```
u8   reason         ; 0 unspecified, 1 user left, 2 returning to menu, 3 error
```

Polite shutdown. The relay replies by closing with `1000`.

#### 4.2.5 `0x05 ROOM_PHASE` (host only)

```
u8   phase          ; 1 = lobby, 2 = match
```

The host declares the room phase. Phase drives the relay's authorisation matrix (§5.2). A
non-host sender is `4403`. Phase may go lobby → match, and match → lobby only for co-op
campaign continuation (the host sends `COOP_MISSION` and then reopens the lobby).

#### 4.2.6 `0x06 DIAGNOSTIC`

```
u8   kind           ; 1 = deterministic state digest, 2 = lockstep stall report
u32  payloadLen     ; 0 .. 4096
...  payload
```

Broadcast to the room. Diagnostics live in the relay envelope rather than in a new game packet
id so the legacy ENet wire format and `NETWORK_PROTOCOL_VERSION` stay untouched. The relay
never interprets the body; it is bounded and rate limited like everything else.

### 4.3 Relay → client

#### 4.3.1 `0x81 WELCOME`

```
u16  relayProtocolVersion
u16  gameProtocolVersion    ; echo of HELLO
u32  yourPeerId
u8   yourRole               ; 1 = host, 2 = client
u8   roomCodeLen
...  roomCode               ; formatted XXXX-XXXX-XXXX
u8   maxPeers
u8   phase                  ; current room phase
u32  maxPayloadBytes
u16  heartbeatIntervalMs
u16  livenessTimeoutMs
```

`WELCOME` is followed immediately by one `PEER_JOINED` for every peer already in the room
(including, for a joining client, the host), in ascending peer-id order. The joining peer does
**not** receive a `PEER_JOINED` for itself; its own identity is in `WELCOME`.

#### 4.3.2 `0x82 PEER_JOINED`

```
u32  peerId
u8   role                   ; 1 = host, 2 = client
u8   nameLen
...  displayName
u8   runtimeLen
...  runtime                ; client-reported
```

Sent to every other peer in the room when a peer completes `HELLO`, and replayed to a new peer
for existing members as described above. A given peer id is announced to a given recipient
**exactly once**.

#### 4.3.3 `0x83 PEER_LEFT`

```
u32  peerId
u8   reason                 ; see §4.4
```

Sent exactly once per peer id per recipient, and only if that recipient previously received a
`PEER_JOINED` for it. Membership bookkeeping on the relay is explicit about this: a peer that
is dropped during its own `HELLO` was never announced, so no `PEER_LEFT` is emitted for it.

#### 4.3.4 `0x84 ROOM_PHASE_CHANGED`

```
u8   phase
u32  byPeerId
```

#### 4.3.5 `0x85 RELAY`

```
u32  senderPeerId       ; assigned by the relay from the connection; never client-supplied
u8   channel
u8   flags
u16  gameMessageType
u32  payloadLen
...  payload
```

#### 4.3.6 `0x86 HEARTBEAT_ACK`

```
u32  clientEchoMs
u32  serverTimeMs
```

#### 4.3.7 `0x87 ERROR`

```
u16  code               ; same numeric space as the close codes in §4.4
u16  messageLen         ; 0..200
...  message            ; printable ASCII
```

A non-fatal refusal: the offending message was dropped, the connection stays open. Repeated
errors count toward the abuse budget and eventually close the socket.

#### 4.3.8 `0x88 ROOM_CLOSED`

```
u16  code
u16  messageLen
...  message
```

Sent before the relay closes every socket in a room (host left, shutdown, lifetime cap).

When the host disconnects, the remaining peers receive `PEER_LEFT` for the host first and
`ROOM_CLOSED` immediately afterwards. Membership bookkeeping therefore stays uniform: every peer
a client was told about gets exactly one `PEER_LEFT`, host included.

### 4.4 Close codes

Private-range WebSocket close codes:

| Code | Name | Meaning |
| --- | --- | --- |
| 4400 | `PROTOCOL_ERROR` | malformed frame, text frame, bad lengths, declared/actual type mismatch |
| 4401 | `UNAUTHORIZED` | missing, expired, unknown or already-consumed grant; frame before `HELLO` |
| 4403 | `FORBIDDEN` | disallowed Origin, host-only message from a client, cross-room recipient, wrong phase |
| 4404 | `ROOM_NOT_FOUND` | the room disappeared between admission and connect |
| 4408 | `TIMEOUT` | handshake deadline or liveness deadline expired |
| 4409 | `ROOM_FULL` | peer cap reached |
| 4413 | `TOO_LARGE` | frame above `maxPayload` |
| 4429 | `RATE_LIMITED` | message-rate or byte-rate budget exhausted |
| 4431 | `SLOW_CONSUMER` | recipient's socket buffer exceeded the backpressure limit |
| 4440 | `HOST_LEFT` | the designated host disconnected; the room is terminated |
| 4441 | `SERVER_SHUTDOWN` | the relay is going away |
| 4450 | `VERSION_MISMATCH` | unsupported relay protocol version |

`PEER_LEFT.reason` uses the low byte of the same space: 0 unspecified, 1 normal leave,
2 timeout, 3 protocol error, 4 rate limited, 5 slow consumer, 6 host left.

## 5. Relay-side authorisation

Every `RELAY` message is checked in this order, and the first failure wins. All checks use only
relay-held state (which connection sent the frame, which room it is in, what role it was granted,
what phase the host declared).

1. **Source binding.** The sender id is taken from the connection. A client has no field in
   which to put a sender id.
2. **Membership.** The recipient must be `0` or a peer id currently in *this* room. A peer id
   belonging to another room is `4403` — peer ids are globally unique, so this is checked by
   room membership, not by id range.
3. **Type allowlist.** `gameMessageType` must be in the table below. Anything else is refused.
4. **Role.** Host-only types are refused from clients; client-only types are refused from the host.
5. **Phase.** Lobby-only types are refused during a match and vice versa.
6. **Budgets.** Message rate, byte rate and recipient backpressure (§6.3).

### 5.1 Type allowlist

| Game message | Id | Relay v1 |
| --- | --- | --- |
| `CONNECT` | 1 | **refused** — carries an IP/port; must never cause a network action on the relay |
| `DISCONNECT` | 2 | **refused** — address-directed |
| `PEER_CONNECTED` | 3 | **refused** — address-directed; replaced by `PEER_JOINED`/`PEER_LEFT` |
| `SENDGAMEINFO` | 4 | host only, lobby |
| `SENDNAME` | 5 | any, lobby |
| `CHATMESSAGE` | 6 | any, any phase |
| `CHANGEEVENTLIST` | 7 | any, lobby; a client may only address it **to the host** |
| `STARTGAME` | 8 | **host only**, lobby |
| `COMMANDLIST` | 9 | any, match |
| `SELECTIONLIST` | 10 | any, match |
| `CONFIG_HASH` | 11 | any, lobby |
| `SETPATHBUDGET` | 12 | **host only**, match |
| `CLIENTSTATS` | 13 | client only, match; **to the host** only |
| `MOD_INFO` … `MOD_ACK` | 14–18 | **refused** — no custom content transfer on the relay in v1 |
| `KEEPALIVE` | 19 | any, any phase |
| `COOP_MISSION` | 20 | **host only**, any phase (campaign continuation arrives after a match) |

The address-bearing packets are refused at the relay *and* have no code path on the client side
in relay mode. Their removal is the point of the relay: a relay peer can never be told to open a
socket to an address of somebody else's choosing.

### 5.1.1 Destination

`sender` and `phase` above mirror `gameMessageRule()` in `RoomRelayProtocol.h`. The destination
rule comes from the other end of the same policy, `NetworkPacketPolicy::classifyPacket()`: a
client only acts on a `CHANGEEVENTLIST` that came from the host, and only a host acts on
`CLIENTSTATS`. Carrying either to anybody else would be traffic the recipient is obliged to
refuse, so a client that addresses one of them to a broadcast or to another client gets a
`4403` refusal on that message. A host is unrestricted: broadcasting the authoritative lobby
list is how the other half of that exchange works, and `NetworkManager` already sends both
messages exactly this way.

`CONFIG_HASH` is deliberately *not* restricted: `classifyPacket` accepts a config hash from any
peer while the room is a lobby, so both the host's broadcast and a client's own hashes are
ordinary traffic.

### 5.2 Phase notes

`COOP_MISSION` is deliberately allowed in both phases: campaign continuation (and the empty
settings that signal "exit") is sent after the previous match has ended, while the session is
still marked as in-game. This matches `NetworkPacketPolicy::classifyPacket`, which the receiving
client applies independently.

## 6. Limits

### 6.1 Sizes

| Limit | Value |
| --- | --- |
| WebSocket `maxPayload` | 262144 bytes |
| Max game payload | 262128 bytes |
| Max diagnostic payload | 4096 bytes |
| Max peers per room | 8 (2 for `mode=coop`) |
| Max rooms | 64 (configurable) |
| Max concurrent connections | 256 (configurable) |
| Max unauthenticated connections | 32 |
| Max outstanding grants | 512 |

### 6.2 Deadlines

| Deadline | Value |
| --- | --- |
| Handshake (socket open → `HELLO`) | 5000 ms |
| Liveness (no frame from client) | 20000 ms |
| Client heartbeat interval | 5000 ms |
| Server WebSocket ping interval | 15000 ms |
| Grant lifetime | 30000 ms |
| Empty-room reaping | 30000 ms |
| Room lifetime cap | 6 h |

### 6.3 Rates and backpressure

- Per connection: 512 messages/s and 1.5 MiB/s, measured in one-second buckets. Exceeding
  either closes the connection with `4429`.
- Per address (HTTP): 10 admission requests per minute, sliding, with bounded cardinality — the
  table holds at most 4096 addresses and entries expire after 10 minutes, so the table itself
  cannot be used to exhaust memory.
- Per address (WebSocket upgrade): 30 per minute, same bounded table. A socket needs a grant and
  grants are already limited at issuance; this bounds the cost of opening sockets at all.
- Globally (HTTP): 120 admission requests per minute.
- Recipient backpressure: before routing, the relay checks the recipient socket's
  `bufferedAmount`. Above 1 MiB the *recipient* is closed with `4431`. The relay never silently
  drops a gameplay message — a dropped lockstep command desynchronises the match, which is worse
  than an honest disconnect.

## 7. Endpoints and configuration

- The production endpoint is configuration only; nothing in this repository deploys it.
- `wss://` is the default and the only scheme accepted for non-loopback hosts.
- `ws://` is accepted by the client **only** when the host is `127.0.0.1`, `::1` or `localhost`
  *and* the development endpoint was explicitly selected. This is what Codex uses for loopback
  testing. Everything else must be `wss://` with full certificate chain and hostname
  verification, and redirects are not followed.
- In production the relay listens on loopback behind a TLS-terminating reverse proxy. It needs
  no deploy keys, no SSH access and no outbound network access.

## 8. Content compatibility

Relay v1 requires matching bundled content. `contentHash` in the admission request is the same
fingerprint the game already exchanges in `CONFIG_HASH` (QuantBot config + ObjectData). The
relay refuses a join whose `contentHash` or `gameProtocol` differs from the room's with
`content_mismatch`, so the mismatch is reported before a socket opens. The clients still run
their own `CONFIG_HASH` exchange after joining; the relay check is an early, friendly failure,
not the authority.

Custom asset transfer and mod synchronisation are refused outright (§5.1). Large downloads must
not share the gameplay socket.

## 9. Keeping the two implementations honest

Both sides agreeing with this document is not the same as both sides agreeing with each other: a
field written in the wrong order, or a length prefix of the wrong width, reads back perfectly to
whichever side wrote it. So the same byte-for-byte fixtures appear in both test suites:

- `tools/room-relay/test/interop.test.js`
- `tests/wasm/RelayWireHarness.cpp` (`testWireFixtures`)

They cover `HELLO`, `WELCOME`, `PEER_JOINED`, a routed game payload in both directions, and a
diagnostic. If one of them has to change, this document, that test and that harness change in the
same commit, and `RELAY_PROTOCOL_VERSION` changes with them.

## 10. Logging contract

See [docs/room-relay-logging.md](room-relay-logging.md) for the lifecycle event schema the relay
emits and the integration requirements for the website metaserver repository.

## 8. Additive public directory, host visibility and lobby chat (September 2026)

These HTTP extensions retain relay wire protocol 1. Old host requests default to **private**;
old clients ignore the additional admission response fields. All routes use the same HTTPS,
exact-Origin CORS allowlist, non-cacheable responses, request deadlines, 4 KiB body cap,
256-byte encoded-field cap and 8 KiB / 16-line response cap as admission.

- `POST /v1/admission/list` accepts common client fields plus `offset` (0..64). It returns
  `status`, `protocol`, `next` (0 means end), and up to 12 repeated
  `game=code|players|maxPeers|mode|nameHex` lines. The name hex preserves the original HELLO
  byte string (Latin-1 storage of UTF-8 bytes). Only opted-in public rooms with a connected
  host, matching game protocol/content, an available reserved seat, and no previous match
  appear. No grants or control tokens appear in discovery. The code is an internal public
  room locator; public UI joins from the listing and never asks the player to handle it.
- Host admission accepts `visibility=public|private` and returns `visibility` plus a separate
  256-bit random `control` token. Client admission returns visibility but never host control.
  `POST /v1/admission/visibility` takes common fields, `room`, `control`, `visibility`.
  Only the live host credential can update an unstarted lobby; the result acknowledges the
  authoritative visibility. Ordinary invitation holders cannot change visibility.
  A public-list join sends `publicOnly=1`; stale listings fail once a room becomes private.
  Visibility does not revoke earlier invitations, issued grants or existing peers.
- `POST /v1/lobby/enter` takes common fields and `name` (UTF-8 hex, 1..64 bytes after decoding).
  The name is explicitly confirmed by the player, unique by NFKC/case normalization within
  the protocol/content channel. Returns `session` (256-bit random token) and `cursor`.
  These are temporary display-name reservations, not verified identities or accounts.
- `POST /v1/lobby/poll` takes `session` and decimal `cursor` (up to 15 digits). It returns a
  cursor and up to 12 `chat=id|nameHex|textHex` lines from that compatible-content channel.
  IDs increase monotonically; clients reject malformed, duplicate or out-of-order messages.
  Confirmation begins at the current cursor; later polling can catch up from bounded history.
- `POST /v1/lobby/say` takes `session` and `text` (UTF-8 hex, maximum 120 bytes). The server
  supplies the name from the session and ignores any claimed author. UTF-8 must be valid;
  Unicode control/formatting/line-separator characters and blank messages are refused.
  The response acknowledges a cursor; clients poll to retrieve messages, including their own.

Chat is capped at 128 sessions, 4 per originating address, and 32 protocol/content channels.
Each channel has its own 100-message history and sequence, so another channel cannot evict it.
Sessions expire after 90 seconds idle or 30 minutes total even if polled continuously; history
expires after 15 minutes. Poll responses include `gap=0|1` so an evicted/expired interval is
reported explicitly. The extra line still fits the 16-line response cap with 12 messages. A session may send 4 messages per 10-second fixed window.
Directory, chat poll and chat send share separate bounded limits of 90 requests/address/minute
and 4096 globally/minute. Host/join, visibility and chat confirmation retain the stricter
10/address/minute and 120/global/minute admission budget, so normal polling cannot starve
room admission. A separate all-route ingress budget of 360/address/minute and 8192 globally
applies before routing, including health, preflight and unknown endpoints. All limiter address
tables remain bounded. New error codes include
`name_taken`, `session_expired`, `forbidden`; unknown error codes remain generic failures.
No chat payload/name/session token or host control token is recorded in lifecycle analytics.
