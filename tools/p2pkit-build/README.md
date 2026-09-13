# Browser direct-transport tooling

Install the pinned build-only dependency with `npm ci --ignore-scripts` here.
No Node process is used to carry gameplay or for production signaling.

* `node test.mjs`: bounded framing, no-TURN policy, malformed signaling, ordered
  sends, and queue overflow checks against the real vendored implementation.
* `node build.mjs`: bundle the game's browser bridge and pinned P2PKit transport.
* `node serve-smoke.mjs [port]`: serve two local browser test URLs. Open both.
  The pages stop their introduction channel after connecting, exchange the
  maximum game payload and ordered messages, and display the actual ICE route.
  This tests RTC transport, not game simulation or public-NAT reachability.

Source provenance and patches: `platform/web/p2pkit/UPSTREAM.md`.
