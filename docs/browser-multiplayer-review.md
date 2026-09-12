# Independent browser multiplayer reviews

Reviewed 2026-09-12 at Stefan's request. Target: proposal commit `68b8c71`,
`docs/browser-multiplayer-design.md`, with the corresponding game source and
the website deployment/metaserver source. No implementation or deployment was
performed. The FPS task remains deferred.

## Provenance

Hermes and Claude Code Opus reviewed independently in separate CLI sessions on
the configured Hermes host. Both received the same isolated source snapshot;
its source directories were read-only. Local CLI attempts failed authentication
or provider setup; the successful remote runs used existing configuration with
no credential changes. Claude's result identifies `claude-opus-5` and session
`2cc5eaa7-c9e1-4e47-9df3-1ffd22e202e1`. Hermes used its configured provider.

Original responses and request are retained locally in:
`/Users/stefan/Documents/projects/outputs/browser-multiplayer-review/`
(`opus.md`, `opus.json`, `hermes.txt`, `review-request.txt`,
`remote-request.txt`, `source-snapshot.tar.gz`). They are reviewer opinions and
source observations, not penetration-test results or deployment certification.

## Shared verdict

Use a room-scoped WSS transport over outbound 443, preserve legacy native ENet,
and keep SSH administration separate. Both reviewers require explicit protocol
authorization, robust parsing and bounded failure behavior before public use.
An opaque tunnel for the existing packets is insufficient. Neither reviewer
considers encryption or a relay equivalent to authoritative simulation/anti-cheat.

## Highest-priority findings checked against source by Codex

| Finding | Evidence at reviewed source | Required correction |
| --- | --- | --- |
| Commands with another player's ID are logged and still queued | `src/CommandManager.cpp:88-107` | Reject mismatches; bind authenticated peer IDs to allowed player IDs, never display names |
| STARTGAME and SETPATHBUDGET lack designated-host sender checks | `src/Network/NetworkManager.cpp:1144-1150`, `1204-1218` | Enforce sender, receiver, phase and role at both relay and client |
| A remote cycle controls timeslot allocation | `include/Network/CommandList.h:39`, `src/CommandManager.cpp:111-118` | Bound tick windows and arithmetic before allocating or queueing |
| Network command deserialization lacks command-ID validation | `src/Command.cpp:106-110` | Validate command ID and exact parameter count before execution; reject malformed senders |
| Additive string bounds can overflow on wasm32 | `include/Network/ENetPacketIStream.h:65-75` | Use subtraction-based bounds with explicit size/count limits; test actual wasm32 |
| Lobby slot values reach array indexing without a local bounds check | `src/Menu/CustomGamePlayers.cpp:669-747` | Validate all lobby event indices/types before applying any event |
| Received map filename is concatenated into a writable path | `src/Network/NetworkManager.cpp:850-880` | Reject traversal and use generated safe storage names; prohibit remote map payloads in initial relay protocol |
| Legacy discovery stops on unexpected field counts | `src/Network/MetaServerClient.cpp:668-675` | Add a new room-discovery endpoint; preserve old list/list2 formats |
| Browser networking policy is enforced twice | `web/.htaccess:7`, `web/shell.html:8` | Update/test both header and meta CSP for the exact WSS destination |

The map path forces an `.ini` suffix and avoids overwriting existing files;
that limits the impact but does not make concatenating a remote traversal path
safe. The cited string check is an arithmetic defect; whether a particular
malicious value traps, throws or exhausts memory depends on runtime behavior.
No exploit was executed against users or production during these reviews.

Further reviewer findings to include in implementation validation: pre-handshake
packet admission, mutable peer names, unbounded nested collections, typed
unaligned byte reads, mod unpacking bounds, mod activation before verification,
and lack of a bounded lockstep timeout. See original reviews for full citations.

## Changes recommended to the proposal

1. Make parser and authorization hardening the first milestone, shared by native
   and browser clients. Add malformed-input and forged-command regression tests.
2. Define a typed room protocol and a phase/role matrix. Replace address-bearing
   CONNECT/DISCONNECT/PEER_CONNECTED packets with relay membership events.
   Derive room/sender authority from connection state; never accept arbitrary
   network destinations. Select browser services before native constructors
   attempt ENet, LAN, UPnP or threaded discovery.
3. Specify HTTPS guest/account session issuance, first-frame WSS authentication,
   a short pre-auth timeout and atomic consumption of expiring single-use
   tickets. Define public/invite/host-approval admission. Origin is additional
   browser protection, not proof of native-client identity.
4. Require bundled, manifest-matched maps and mods for relay v1. Explicitly
   reject both map data and MOD_* transfers. Later content service work needs
   safe staging and cryptographic integrity; host-provided hashes alone do not
   establish content trust.
5. Define queue limits, heartbeat deadlines, tab suspension, host loss and
   reconnect policy. A bounded command replay buffer alone does not prove
   state recovery. Never omit gameplay commands to keep a match moving.
6. Prefer a same-origin WSS route behind the HTTPS proxy where practical, and a
   separate restricted service identity. Keep its files outside the website
   deploy's rsync --delete tree. The public origin and the relay's machine are
   separate decisions: same-origin proxying can reach an isolated backend over
   authenticated encrypted transport. Test backend/key/SQLite isolation.
7. Add a dedicated state-hash verification mechanism and an injected-divergence
   test before claiming browser/native lockstep parity. Exercise long games,
   co-op and save/load on the actual runtime combinations.
8. Use an authenticated internal lifecycle endpoint and additive participant
   records. Preserve match-level client_runtime as reporter metadata and keep
   client claims distinct from server-observed transport. Legacy gamestats is
   not an authenticated audit trail.

## Points requiring judgment rather than automatic acceptance

- Opus proposes a sequencing relay. This merits a separate design decision:
  enforcing sender identity, tick windows and duplicate detection is useful;
  rewriting tick numbers or command order casually can change deterministic
  behavior. Define the contract against existing CommandManager scheduling and
  prove equivalence. It is not automatically a safe transport-only change.
- Opus describes WebRTC as the only unreliable/unordered browser option. That
  is too broad: WebTransport also offers datagrams (as the original proposal's
  Emscripten reference notes). This does not invalidate the WSS-first choice.
- Opus uses strong exploitability/severity language for several source findings.
  Codex verified the code defects listed above, not every claimed unauthenticated
  exploit path, exact failure outcome or impact on every peer. Establish those
  through isolated tests before publishing categorical security claims.
- Both treat SSH as administration. If Stefan means a separate machine/trust
  domain or actual player SSH transport, resolve that before deployment design.
- TCP behavior under loss remains a measured acceptance gate, not an assurance
  that WSS will meet the latency target simply because commands are small.

These reviews support proceeding to a hardened private prototype after the
identified client boundaries are fixed. They do not approve a public deployment.
