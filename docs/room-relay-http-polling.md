# Room relay over HTTPS polling

The gameplay protocol in [room-relay-protocol.md](room-relay-protocol.md) assumes a WebSocket.
This document describes the second way the same frames can travel: batched inside ordinary HTTPS
requests, so the relay can be reached through an existing Apache/PHP front end when no WebSocket
endpoint is available.

Nothing about the relay frames changes. §4 of the protocol document is the payload here, byte for
byte, in both directions. This is an envelope, not a second gameplay protocol, and the client
above the transport (`RoomRelayClient`) cannot tell the two apart.

Client implementation:

| file | what it is |
| --- | --- |
| `include/Network/RelayPollProtocol.h` | the batch format and its bounded parser; no platform dependencies |
| `include/Network/RelayHttpTransport.h`, `src/Network/RelayHttpTransport.cpp` | the state machine: sequencing, retries, pacing, queues |
| `src/Network/RelayHttpTransportCurl.cpp` | native backend (libcurl multi handle, non-blocking) |
| `src/Network/RelayHttpTransportEmscripten.cpp` | browser backend (Fetch API through `EM_JS`) |

## 1. Choosing the transport

The admission answer's `url` field decides. `wss://` and `ws://` mean a WebSocket; `https://` and
`http://` mean polling. `relayTransportKindForUrl()` makes that choice and both platform
implementations of `createRelayWebSocket()` dispatch on it.

The admission endpoint itself is unchanged and is still reached over HTTPS as described in §3 of
the protocol document.

`isAcceptableRelayUrl()` validates all four schemes with one rule: the encrypted form is always
acceptable, the plaintext form only for `127.0.0.1`, `::1` or `localhost` *and* only when the
development endpoint was explicitly selected. Adding the HTTP transport does not create a way to
reach a remote host in the clear. An HTTP endpoint additionally may not carry a query string or a
fragment, because the transport appends a path to it.

A machine that cannot do WebSockets at all — the macOS system libcurl carries no `ws`/`wss`
handlers — can use the polling transport. `RoomRelayClient::start()` therefore only applies the
WebSocket capability probe to a WebSocket endpoint.

## 2. Endpoints

Given a poll URL such as `https://dunelegacy.com/relay/v1/poll`, with any trailing slashes
removed:

| request | body | answer |
| --- | --- | --- |
| `POST <url>/open` | empty | 64 lowercase hex characters and one LF, exactly 65 bytes |
| `POST <url>/exchange` | a `DHP1` batch | a `DHR1` batch |
| `POST <url>/close` | empty | ignored |

Every request carries `Content-Type: application/octet-stream`. Every request except `/open`
carries the session token in `X-Dune-Session`.

The session token appears in that header and nowhere else — never in a URL, never in a log line.
No redirect is followed on any of them, and the certificate chain and hostname are verified, the
same as for admission and the WebSocket transport.

`/open` is attempted **once**. There is no automatic retry: a session that cannot be opened is a
visible failure rather than a menu that quietly hangs. No `HELLO` is sent until `/open` has
returned a well-formed token.

## 3. The batch format

Little endian throughout, unlike the big-endian relay envelope inside the frames.

Request (`DHP1`):

```
"DHP1"                      4 bytes ASCII
sequence                    uint32   starts at 1
frameCount                  uint32
  frameLength               uint32   repeated frameCount times
  frame                     frameLength bytes
```

Response (`DHR1`):

```
"DHR1"                      4 bytes ASCII
sequence                    uint32   echoes the request
closeCode                   uint16   0 while the session is open
frameCount                  uint16
  frameLength               uint32   repeated frameCount times
  frame                     frameLength bytes
```

Limits, enforced on both sides:

| limit | value |
| --- | --- |
| frames per batch | 64 |
| bytes per frame | 262144 (the relay's own frame ceiling) |
| bytes per request | 1048576 |
| bytes per response | 1048576 |

A response is refused, and the session ends with close code `4400`, if the magic is wrong, the
sequence does not match the request, the close code is neither 0 nor 1000 nor in 3000..4999, the
frame count is above 64, any frame length is zero or above the ceiling, any length runs past the
end of the body, or a well-formed batch is followed by trailing bytes.

The whole response is validated before a single frame is delivered. A batch is admitted in full
or not at all; a prefix of a batch is never handed to the game, because applying part of what
arrived is how a lockstep match desynchronises quietly.

## 4. Sequencing and retries

- One request is in flight at a time, for the whole transport.
- A new exchange starts no sooner than 25 ms after the previous one *started*: at most 40
  requests per second per peer. Once that interval has passed it starts immediately.
- The server holds an otherwise empty exchange for up to 100 ms and answers early when a frame
  arrives for this peer, so an idle session costs far less than the ceiling.
- Request timeout is 5000 ms.
- On a network failure, a timeout, or 429, 502, 503 or 504, the **same bytes with the same
  sequence number** are sent again, after 100 ms and then 250 ms. Two retries, then the session
  is lost. Data queued while a retry is pending waits for the next fresh exchange: the server
  answers a repeated request from its stored copy and would never see the addition.
- Every other 4xx ends the session. Repeating a request the server has rejected only asks the
  same question again.
- The sequence advances only after a response was accepted. A session that would wrap it past
  `0xFFFFFFFF` ends instead — at 40 exchanges a second that is over three years, and a wrapped
  sequence would make a stale answer indistinguishable from a current one.
- A new request implicitly acknowledges the previous response; the server may drop its stored
  copy then.

## 5. Closing

A non-zero `closeCode` in a response ends the session. Any frames in that same response are
delivered to the game first: the relay drains what it still had for the peer into the final
batch, and the game reads all of it before it acts on the close.

When the client ends the session itself it posts `/close` with an empty body and the session
header. This is best effort in the strict sense — it is dispatched, its answer is never read,
and if the client exits before it completes it goes with it. Abandoned sessions expire on the
server; `/close` only makes the common case immediate.

## 6. Browser specifics

The browser backend uses `fetch()` through `EM_JS` rather than `emscripten_fetch()`, because the
request must be made with `credentials: 'omit'`, `cache: 'no-store'` and `redirect: 'error'`, and
`emscripten_fetch()` is XMLHttpRequest underneath and can express none of the three.

There is no C callback in that path at all. JavaScript owns the request and its result; the C++
side holds an integer handle and asks about it from `pump()`. A request that settles after the
transport is gone finds its entry marked abandoned and deletes it, so there is no pointer to get
wrong.

`Origin` is whatever the browser attaches; a page cannot choose one. Native clients send none,
which is why `Origin` is worth checking on the relay for browser clients and worth nothing for
native ones (§3.5 of the protocol document).

## 7. Tests

| target | what it covers |
| --- | --- |
| `relay_http_transport_tests` | the state machine against a scripted backend and a virtual clock: pacing, retry byte-identity, the retry ceiling, which statuses are retried, sequence advance and wrap, bounded memory, delivery exactly once, close and shutdown |
| `relay_wire_harness` | the batch codec and a short transport run, built for wasm32 as well as the host, because `size_t` is 32 bits in the browser |
| `dunelegacy_tests` (`[relay][security]`) | endpoint validation for all four schemes |
| `relay_session_tests` | that an HTTPS endpoint does not need WebSocket support, and that plain HTTP to a remote host is still refused |

The native backend retains its libcurl multi handle across polls to reuse connections.
The browser reads a bounded stream before handing a response to C++; it rejects
oversized declared lengths and stops reading at the batch limit. Run
`node tests/wasm/test-poll-fetch-backend.cjs BUILD/bin/dunecity.js` against the
compiled browser artifact to test the actual generated EM_JS functions.
