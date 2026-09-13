# Direct-play interoperability harness

Checks that the native WebRTC backend and the browser's agree about the wire, with no signaling
service, no relay and no internet connection: signals travel through a directory on loopback and
the peers use host candidates.

There are two halves, plus a pure-framing cross-check that needs neither.

## 1. Framing cross-check (fast, no browser)

```bash
node --experimental-strip-types tools/p2p-interop/framing-vectors.mjs
```

Feeds the exact strings `tests/P2PWireFramingTestCase` asserts the native encoder produces to the
vendored browser chunker, in both directions. If either side's framing changes, one of the two
suites fails rather than a browser and a native client quietly failing to play each other.

## 2. Live native-to-browser session

```bash
# 1. build the browser bundle (Codex's tooling)
node tools/p2pkit-build/build.mjs

# 2. build the harness
cmake -S . -B build -DDUNECITY_BUILD_TESTS=ON -DDUNECITY_DIRECT_P2P=ON
cmake --build build --target dunecity_p2p_interop

# 3. start the loopback mailbox (development tool; binds 127.0.0.1 only)
node tools/p2p-interop/serve-mailbox.mjs --dir /tmp/dune-interop

# 4. open http://127.0.0.1:8799/ in a browser, then run the native peer
./build/bin/dunecity_p2p_interop --dir /tmp/dune-interop --initiator
```

The native peer sends one full-size game packet (262128 bytes, which fragments) and 100 ordered
probes; the page echoes both back through P2PKit's RTCTransport. The native peer exits 0 only if
the large payload returns byte-identical and all 100 probes return in order.

## What this is not

The mailbox server is a test fixture. It is not the signaling service, it is never deployed, it
binds loopback only, and it carries offers, answers and candidates exclusively - there is no
endpoint on it that would take a game packet.
