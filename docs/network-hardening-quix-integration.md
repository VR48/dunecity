# Network hardening and Quix transport integration

Reviewed 2026-09-12 while implementing `fix/network-hardening`. Stefan asked to
consider Quix's concurrent work and reuse useful parts without adopting a
matchmaking redesign merely for decentralization. No pull request was merged.

## Source snapshots

- [VR48/dunecity PR 24](https://github.com/VR48/dunecity/pull/24), head
  `35eb6ff47df349c5643949231361aa693cff761b`: reproducible Emscripten build tooling,
  exception catching, native-service guards and build-output checks. It does not
  implement multiplayer. Our current tree already builds Emscripten, enables
  exceptions and packages the production web shell; copying its older build
  configuration wholesale would duplicate or replace that work.
- [QuixThe2nd/dunecity PR 3](https://github.com/QuixThe2nd/dunecity/pull/3), head
  `db4c81ec5bf132d4bd18f873af08bec04bd87b6e`: the stacked WebRTC implementation.
  It introduces packet buffer/view abstractions, ordered control and unreliable
  command data channels, room codes, a Node signaling service, browser event-loop
  yielding and tests. The native implementation remains ENet; the browser
  implementation supports exactly two players. This is not native/browser
  crossplay and does not itself establish deterministic simulation equivalence.

The implementation inspected still uses one in-memory signaling server with no
replication or federation. It can be operated centrally. The quoted discussion's
future decentralization claims are not features demonstrated by this snapshot.

## Useful pieces and integration order

1. Keep packet admission, identity/command ownership and bounded decoding in shared
   production policy, independent of UDP versus WebRTC. Apply those checks to
   both transport event sources before exposing browser multiplayer.
2. Reuse the packet-buffer abstraction and byte-for-byte compatibility tests when
   integrating Quix's transport. Use one bounded decoder for both backends, or
   require both readers to pass the same adversarial fixtures and expose remaining
   length to nested collection readers. Merely aliasing a second InputStream
   implementation does not carry the ENet reader's protections across.
3. Keep central discovery/analytics as an independent adapter. Room-code signaling
   can complement it without replacing SQLite game logging or server governance.
4. Reuse emitted-JavaScript bridge checks and real-browser acceptance tests, but
   extend acceptance to client-originated commands, reconnect, mission transition,
   bounded queues and long deterministic games. Packet traffic counts and a short
   live match do not prove the two simulations stayed equal.
5. Port the complete current wire format, including protocol 5 co-op mission
   support and the simulation seed on game packets. Quix's snapshot is based on
   an older network tree; a mechanical merge is not a compatibility review.

## Issues to resolve before taking the transport

- **The new browser reader repeats the wasm32 length overflow.**
  `include/Network/NetworkPacketView.h:46` tests `currentPos + length`; casting
  currentPos to size_t does not widen wasm32 arithmetic. A four-byte packet
  declaring length `0xfffffffc` bypasses the packet bounds check and reaches
  `std::length_error` during allocation. Codex reproduced this with Emscripten
  4.0.14/Node, while the hardened ENet reader rejects the same input with
  `InputStream::eof` at the packet boundary. The view also needs the strict bool
  and nested-count/remaining-length rules.
- **The browser client's host is omitted from the connected peer list.**
  In `src/Network/NetworkManager.cpp:728`, browser Connect assigns `connectPeer`
  but never adds it to `awaitingConnectionList`. SENDGAMEINFO at line 1119 then
  assigns `peerList = awaitingConnectionList`, which is empty for this path.
  Broadcasts (including chat and command lists) iterate peerList, so this source
  path sends no client broadcasts and reports no connected host to lockstep code.
  This is a source-traced finding, not a reproduced browser-session claim.
  Fix list membership together with cleanup: `clearAllPeers()` separately deletes
  list entries and connectPeer, so simply adding the host to a list creates an
  alias/double-delete risk unless ownership is corrected too.
- **Queue limits must bound bytes and preserve control events.** The C++ event
  queue is capped by entry count after copying incoming payloads and drops the
  oldest event. The JS reliable control outbox has no aggregate byte cap.
  Enforce packet/queue byte budgets before copies, define disconnect handling
  under overflow, and do not silently discard ordered control or disconnect
  events. Retain unreliable rolling-history command semantics under congestion.

## Checks performed

- Read actual PR diffs and the referenced files at the pinned hashes above.
- Ran seven emitted-library/JavaScript verifier tests from PR 3: all passed.
  These tests do not cover the C++ client membership or packet-length issues.
- Reproduced the NetworkPacketView malformed-length result in wasm32.
- Did not execute the PR's full browser game or signaling deployment, and did not
  treat its self-reported smoke-test results as independent verification.

The existing-network hardening can ship independently of this transport work.
Before integrating the transport, rebase onto that hardening and rerun the same
boundary tests for each transport rather than replacing its checks.

## Subsequent PR update and coordination

At PR24 head `0b172c2222e96167cba3ed6c72c15d89b63d9ad1`, commit `69655ca`
changes the root CMake file, build workflow and gitignore; the WebRTC PR3 head is
still `db4c81e`. The updated root CMake version is 1.0.655 but config.h and
vcpkg.json remain 1.0.531. Running the PR's version checker against its own files
fails with the expected mismatch. Do not substitute that head for the current
working tree merely because its root project version matches.

Stefan authorized posting these findings. The source-pinned review and update are
[in PR24's comment](https://github.com/VR48/dunecity/pull/24#issuecomment-5644077955).
No code has been copied from Quix's branches at this checkpoint; the seven bridge
tests were executed on an isolated source snapshot.

A Codex thread heartbeat named **Hermes review of Quix networking changes** checks
both PRs every five minutes. It asks the real Hermes CLI to review newly observed
changes, retains commit checkpoints in the task's output directory, and brings
only actionable findings or failures back to the coordinating task. It does not
merge, push, deploy or post comments automatically.

Scope now includes implementing updated-desktop/browser and browser/browser
multiplayer with Opus, real browser game tests, and an independent Hermes security
review. Existing-network hardening remains the first acceptance checkpoint.
