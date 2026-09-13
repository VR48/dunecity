# Real direct-session checks

These tests use the production room state machine, PHP admission/signaling, and native RTC
adapter with P2PKit framing. They do not simulate a whole game or prove WAN/NAT reachability.

Build with SDL2, libcurl and an installed libdatachannel v0.24.5 at commit
`443f6934d9007eb7076ab7825ba330f355fcbead` (media and WebSockets disabled):

```sh
cmake -S tools/p2p-session-smoke -B /tmp/dune-direct-smoke \
  -DCMAKE_PREFIX_PATH=/path/to/dependencies
cmake --build /tmp/dune-direct-smoke
ctest --test-dir /tmp/dune-direct-smoke --output-on-failure
python3 tools/p2p-session-smoke/run.py /tmp/dune-direct-smoke/session-peer --host-last --outage
```

Set `PHP_BIN` if PHP is not on PATH. The Python runner creates private temporary state and
loopback-only services. It redeems three grants into real native clients, checks full-mesh
readiness, exchanges ordered maximum-size game payloads, and, with `--outage`, terminates all
PHP workers and verifies the listener closed before checking 75 seconds of continued traffic.
`--host-last` ensures the host role works when its numeric peer ID is not the lowest.

The channel-policy test negotiates real SCTP channels. Reliable ordered `p2pkit` channels must
connect; unordered, partial-reliability and wrong-label channels must fail. It also checks the
native answer advertises the bounded 131072-byte SCTP message size.

A failure retains private fixture evidence in the temporary directory printed by the runner.
It can include ephemeral grants/SDP; do not publish it. Normal completion removes fixture state.
