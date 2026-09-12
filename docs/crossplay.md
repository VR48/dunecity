# Crossplay: desktop and browser in one game

Status: integration and browser testing in progress; not deployed. A successful build or
transport harness is not evidence of a synchronized browser match. The relay endpoint is
configuration, and the game offers online play only when one is configured.

Three documents describe this feature:

- **this one** — how it fits together, how to build it, how to run it, what is not done;
- [docs/room-relay-protocol.md](room-relay-protocol.md) — the wire contract both sides implement;
- [docs/room-relay-logging.md](room-relay-logging.md) — the lifecycle events and what the website
  metaserver has to do to ingest them.

The existing ENet transport remains available for LAN and direct-Internet games. Its packet
validation and authorization have been hardened; see [network-hardening.md](network-hardening.md).

## 1. Why a relay at all

A browser cannot open a UDP socket, so ENet cannot work there. It can open one outbound
WebSocket. So can a desktop client, through a firewall, without port forwarding. A room-scoped
relay is the smallest thing that lets both kinds of player share a match.

What the relay is not: an authoritative simulation. It routes and it authorises; the simulation
is still lockstep between peers, and a player running a modified client can still lie about
anything inside their own authority, exactly as on ENet.

## 2. The pieces

| Piece | Where | What it does |
| --- | --- | --- |
| Relay service | `tools/room-relay/` | Node + `ws`. Admission, rooms, routing, limits, logging. |
| Wire contract | `docs/room-relay-protocol.md` | The shared definition both implementations follow. |
| Protocol codec | `include/Network/RoomRelayProtocol.h` | Bounded encode/decode, and the authorisation matrix. |
| Admission client | `include/Network/RoomAdmissionClient.h`, `src/Network/RoomAdmissionClient.cpp` | The bounded HTTPS request that yields a room code and a single-use grant. |
| Socket | `include/Network/RelayWebSocket.h` | One interface, two implementations. |
| ⤷ native | `src/Network/RelayWebSocketCurl.cpp` | libcurl WebSocket on a CONNECT_ONLY multi handle. |
| ⤷ browser | `src/Network/RelayWebSocketEmscripten.cpp` | Emscripten WebSocket, queued into the game loop. |
| Session | `include/Network/RoomRelayClient.h`, `src/Network/RoomRelayClient.cpp` | Handshake, logical peers, membership, heartbeats, deadlines. |
| Shared receive path | `include/Network/GamePayloadRouter.h`, `src/Network/GamePayloadRouter.cpp` | The payload handling both transports use. |
| Content rule | `include/Network/ContentCompatibility.h` | Whether two installs may be in the same match, in one place. |
| Transport switch | `src/Network/NetworkManager.cpp` | `NetworkManager::Transport::RoomRelay`. |
| Menu | `src/Menu/CrossplayMenu.cpp` | Host a room, join by code, carry it into the lobby. |
| Digest | `include/Network/GameStateDigest.h` | The periodic deterministic fingerprint. |

## 3. Building

Nothing extra is needed for a normal desktop build beyond a libcurl that has the WebSocket
protocol handlers.

```bash
cmake --build build --target dunecity
```

libcurl gained `curl_ws_send`/`curl_ws_recv` in **7.86**. Older libcurl still builds: the
transport compiles out and the game says online play is unavailable. Configure prints which case
you are in.

On macOS the *system* libcurl is new enough by version but is built **without** the `ws`/`wss`
handlers, so the game checks at runtime as well and says so in plain words. Point the build at a
libcurl that has them:

```bash
cmake -S . -B build -DCURL_ROOT=/opt/homebrew/opt/curl
```

Browser build: the link options `-lwebsocket.js` and `-sFETCH=1` are already in
`src/CMakeLists.txt`. The browser needs its Content-Security-Policy to allow the relay origin in
`connect-src`. Package with `scripts/package-web.py --relay-origin https://relay.example`
(plus the required `--build-root` and `--play-root` arguments) to add the exact HTTPS and WSS
origins to both the HTML meta policy and the packaged `web/.htaccess` policy. Without that
argument the existing policy stays in effect. Cross-origin admission also requires the relay
to allow the browser page's exact Origin and return matching CORS headers.

For a loopback test package, use `--relay-origin http://127.0.0.1:8787
--allow-loopback-relay`. This explicit development package permits HTTP/WS loopback and removes
the HTTPS upgrade directive from its Apache policy. Do not publish a development package.

## 4. Running a relay for testing

```bash
cd tools/room-relay
npm ci
npm test
RELAY_ALLOWED_ORIGINS=http://127.0.0.1:8766 npm run dev
```

Production shape (TLS at a reverse proxy, relay on loopback) is in
[`tools/room-relay/README.md`](../tools/room-relay/README.md).

The real producer/PHP/SQLite contract can be checked independently, with disposable data:

```bash
python3 tools/room-relay/test/verify-php-delivery.py --metaserver-dir /path/to/website/metaserver
```

This sends seven synthetic lifecycle events through HMAC validation and checks stored runtime
markers. It does not attest an actual WSS match.

## 5. Pointing the game at it

Plain `ws://`/`http://` is only ever accepted for **loopback**, and only when the development
endpoint is chosen explicitly. Everything else must be `wss://`, with the certificate chain and
hostname verified and redirects not followed.

Desktop:

```bash
./dunecity --RelayEndpoint=https://relay.example.net          # production shape
./dunecity --RelayDevEndpoint=http://127.0.0.1:8787           # loopback, development
./dunecity --RelayDev                                         # use the configured dev endpoint
```

Or in the configuration file:

```ini
[Network]
Relay Endpoint=https://relay.example.net
Relay Development Endpoint=http://127.0.0.1:8787
Use Relay Development Endpoint=false
```

Browser — there is no command line, so the page URL carries it:

```
dunecity.html?relay=http://127.0.0.1:8787&relaydev=1
```

The value goes through exactly the same validation.

## 6. Playing

Desktop: **MODES → MULTIPLAYER → Play Online (Crossplay)**. The legacy LAN, direct-Internet and
campaign co-op buttons are unchanged and still there.

Browser: **MODES → PLAY ONLINE**. The browser is not offered LAN or direct-Internet play, because
it has no UDP socket to do it with.

Then:

1. One player chooses **Host a Game** or **Host Campaign Co-op** and reads out the game code.
2. The other types the code and chooses **Join Game**. Codes look like `H4PQ-7T2M-9XKB`; dashes
   and capitalisation do not matter.
3. The host picks a map (or a campaign mission) and the normal lobby opens for both players. The
   game code stays on screen in the lobby caption and in the chat area.
4. **Start Game** works as it always has. Campaign co-op continues into the next mission the same
   way, and leaving ends the room.

## 7. What relay v1 deliberately does not do

- **No custom content transfer.** `MOD_INFO`, `MOD_REQUEST`, `MOD_CHUNK`, `MOD_COMPLETE` and
  `MOD_ACK` are refused by the relay and have no client code path. Both players need the same
  bundled content; the relay refuses a join whose content fingerprint differs, so the mismatch is
  reported before a socket opens, and the lobby's own config-hash exchange still checks it
  independently.
- **No address-bearing packets.** `CONNECT`, `DISCONNECT` and `PEER_CONNECTED` name an IP and a
  port. They are refused at the relay and have no code path in relay mode. Membership is typed
  relay events instead. This is the point of the relay: nothing in this path can be told to open
  a socket to an address of somebody else's choosing.
- **No discovery service.** Room codes are how a game is found. The legacy `list`/`list2`
  metaserver endpoints describe UDP hosts with an address and a port; a relay room has neither,
  and an old client handed one would try to open a UDP socket to nonsense. Advertising relay
  rooms needs a separate, capability-aware endpoint that old clients never request.
- **Two players for co-op**, up to four for a custom relay room. The lobby's own limits still
  apply on top.

## 8. Failure behaviour

Every one of these is visible to the player rather than silent:

| Situation | What happens |
| --- | --- |
| No relay configured | "Online play has not been set up in this copy of the game." |
| libcurl without WebSocket support | The reason is shown, and the online buttons stay disabled. |
| Wrong or expired code | The admission request fails with the relay's reason. |
| Content or version mismatch | Refused at admission, reported in the lobby, and the match refuses to start. |
| This install cannot hash its own content | It refuses to go online at all rather than sending an empty fingerprint. |
| The host changes mod after the room opened | The host re-checks before starting and refuses if anyone now differs. |
| The host leaves | The room ends for everybody with "The host left the game." |
| A player stops responding | The relay drops them after 20 s; the match ends after 45 s of waiting. |
| The connection falls behind | The session ends rather than dropping queued gameplay messages. |
| The game loop stops draining | The session ends, the backlog is discarded, and the close is still delivered. |
| Simulations disagree | The state digest reports it once, in the news ticker. |

Two of those need spelling out, because the obvious implementation of each is wrong.

**Content agreement is never assumed.** A peer that has not reported its content hashes yet has
not shown that it matches, and an install that could not hash its own content has not shown
anything at all — two such installs would otherwise compare equal and neither would have verified
anything. The first case is recoverable and says so ("waiting for …"); the second and a real
disagreement both stop the match. The room's fingerprint is checked again at start, because the
lobby lets the host pick a different mod after the room was admitted against the old one. The
rule itself lives in `include/Network/ContentCompatibility.h` so the three places that apply it
cannot drift apart.

**A session that ends discards what it could not keep up with.** The event queue is bounded by
count *and* by aggregate size — four thousand events each carrying a 256 KiB payload is a
gigabyte, which a count alone would never notice. When either bound is reached the session ends,
the backlog goes with it, and the close is still delivered: applying a prefix of what the game
could not keep up with is exactly how a lockstep match desynchronises quietly. An *orderly* close
is the opposite case and keeps its pending events, because a co-op continuation is sent
immediately before the host disconnects and dropping it would strand the other player.

A lockstep command is **never** skipped to keep a match moving. Skipping one desynchronises the
simulation silently, which is worse than an honest disconnect and is exactly what the digest
exists to catch.

## 9. Diagnostics

During a relay match each peer produces a deterministic fingerprint every 200 game cycles and
sends it in the relay's diagnostic envelope — not as a game packet, so the ENet wire format and
`NETWORK_PROTOCOL_VERSION` are untouched. It covers the cycle, the shared random seed, per-house
credits and counts, and every object in ascending id order with its item, houses, raw fixed-point
health and tile. Rendering, wall-clock time, the local player and derived caches are excluded,
because those legitimately differ between two peers of one match.

Packet counters are not a substitute: they say messages arrived, not that the simulations agree.

Verification tools:

```bash
# Parsers, under wasm32 where size_t is 32 bits, and natively with ASan/UBSan:
tests/wasm/run-relay-wire-harness.sh wasm
tests/wasm/run-relay-wire-harness.sh native

# Cross-table agreement, the content rule, and the session's queue behaviour:
ctest --test-dir build --output-on-failure \
      -R 'dunelegacy_tests|relay_wire_harness|relay_session_tests'

# Two real peers against a real relay on loopback:
cmake --build build --target relay_transport_harness
tests/relay/run-relay-transport-harness.sh
tests/relay/run-relay-transport-harness.sh diverge   # injected divergence must be detected
tests/relay/run-relay-transport-harness.sh bulk      # large frames, every byte verified
```

`relay_session_tests` is a separate executable rather than another file in the main test target,
because it compiles the production `RoomRelayClient.cpp` against a scripted socket. The session
reaches its transport through two free functions, so the test target supplies its own definitions
of them instead of linking `RelayWebSocketCurl.cpp`: the code under test is exactly what ships and
the production build carries no hook for it. That is what makes the queue bounds testable at all —
overflowing them against a real relay would mean pushing a gigabyte through a socket.

`bulk` mode verifies every byte of large frames at the receiver. It paces traffic below the
production relay's bandwidth limit; an unpaced burst exercises rate-limit disconnection instead.
A successful transfer does not prove partial socket writes occurred. The script reports outgoing
backlog as a diagnostic, but a backlog alone does not establish which libcurl write path ran.

The browser side cannot be driven from a shell. Build the web target, open it with
`?relay=http://127.0.0.1:8787&relaydev=1`, and host or join against the same relay the native
harness is using. The relay's lifecycle log shows `runtime=browser` for that participant.

## 10. Known limitations

- WebSocket is TCP: a lost packet stalls everything behind it. That is measurable and should be
  measured under latency and loss before this is called good enough for competitive play. It is
  not a reason to prefer the current ENet path for browsers, which cannot use it at all.
- The relay authorises and routes. It does not validate game state, and it is not anti-cheat.
- The state digest detects divergence; it does not repair it. There is no resynchronisation.
- There is no reconnect. A dropped player is out of that match.
- Relay rooms are not advertised anywhere. Room codes only.

## 11. Local browser verification — 12 September 2026

Two in-app browser clients played a real Dune City match on Habbanya-Penny over the loopback
relay. The initial run accepted movement commands and matched sampled 28-byte state digests
through cycle 16,800. Opening the host menu exposed a 45-second lockstep timeout: local pause
froze the cycle that would transmit the pause command itself.

Relay menus now leave simulation running, closing menus emits no pause/resume command, and
Space explains that online games cannot pause. Local single-player and legacy ENet pause paths
are preserved; the new lockstep deadline applies only to relay sessions. On browser build
8a793d6, the host menu stayed open for over 70 seconds while both games continued and sampled
digests matched at cycles 3,600, 3,800, 5,600 and 5,800. Closing the menu returned to gameplay.

The retest also found that a guest joining before map selection received an empty seat snapshot.
Commit c5db43d registers the seat-assignment callback before starting the lobby. This ordering fix
is built but still needs a fresh early-join UI regression run.

Native CTest's four targets pass, the standalone wasm32 wire harness passes 140 checks, and the
real native transport harness passes agreement, injected-divergence detection and all 48 large
messages with no corrupt bytes. That bulk run did not exercise partial socket writes. The real
Game command probe passes authorization, atomic batch recovery and relay/local pause behavior.
The Node lifecycle publisher also delivered seven signed fixture events through the PHP receiver
to isolated SQLite successfully.

These are local development results, not public WSS deployment or security approval. Actual
browser-to-native gameplay, final review reconciliation, public TLS/proxy verification and tests
under real latency/loss remain outstanding. The running local test page has explicit loopback
CSP permissions and must not be used as the production package.
