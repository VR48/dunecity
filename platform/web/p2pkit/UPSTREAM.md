# P2PKit source used by DuneCity

Source: https://github.com/QuixThe2nd/p2pkit
Revision: 94ae7eb8818a629478e0a6ba0aa3232c5fc0b1ab
Upstream package version: 0.1.0; declared license: MIT (upstream-package.json).

DuneCity uses RTCTransport directly; mesh forwarding, RPC, DHT, application
cryptography and runtime backend auto-discovery are not part of the game bundle.
The desktop transport implements the same JSON chunk framing using libdatachannel.

Local changes harden chunk allocation/reassembly, signaling queues and errors,
connection deadlines, data-channel backpressure, and the direct-only ICE policy.
These changes are maintained here rather than installing mutable upstream HEAD.
Room membership and SDP fingerprints are bound through authenticated HTTPS
signaling; the application still validates every game command and its owner.
