# Network hardening — the ENet trust boundary

This document describes what the multiplayer receive path does and does not guarantee after
the hardening pass, and how to exercise it. It covers the existing ENet mesh transport only;
it says nothing about the proposed WSS relay, which is separate work.

## What this is not

The legacy ENet transport has **no encryption and no authenticated users**. Nothing here adds
a session key, a signature or a verified identity. What is enforced is the boundary that
already exists in the protocol:

- *which connection* a packet arrived on (the host connection, an established mesh peer, or a
  connection that has not been admitted),
- *which role* the local process has (host or client),
- *which phase* the session is in (lobby or match),
- *which player* a command or a lobby slot claim belongs to.

A peer that has been admitted to a game can still lie about anything inside its own authority
(that is what a modified client does), and a hostile host is still a hostile host. Treat the
guarantees below as "the protocol's own rules are now enforced", not as anti-cheat.

## Admission matrix

`NetworkPacketPolicy::classifyPacket()` (`include/Network/NetworkPacketPolicy.h`) is called by
`NetworkManager::admitPacket()` before any packet payload is interpreted. Summarised:

| Packet | Accepted from | Phase |
| --- | --- | --- |
| `KEEPALIVE` | any identified connection | any |
| `SENDNAME`, `CONFIG_HASH` | any identified connection, including during admission | lobby |
| `SENDGAMEINFO`, `CONNECT`, `DISCONNECT`, `STARTGAME`, `COOP_MISSION`, `MOD_INFO`, `MOD_CHUNK`, `MOD_COMPLETE` | client, host connection only | lobby (`DISCONNECT`: any) |
| `CHANGEEVENTLIST` | host: established client; client: host connection | lobby |
| `PEER_CONNECTED` | host: established client; client: host connection | lobby |
| `CHATMESSAGE` | established peer | any |
| `COMMANDLIST`, `SELECTIONLIST` | established peer | match |
| `CLIENTSTATS`, `MOD_REQUEST`, `MOD_ACK` | host, established client | `CLIENTSTATS`: match, mod packets: lobby |
| `SETPATHBUDGET` | client, host connection only | match |

`SENDNAME` and `CONFIG_HASH` stay available while a peer is still handshaking because the mesh
handshake needs them there: the host greets a new connection with its name, mesh peers exchange
names as they connect to each other, and the client answers every `CONFIG_HASH` with its own.

The phase flips to "match" in `NetworkManager::beginSimulation()`, which every peer calls when
the game starts - previously only the host had an in-progress flag.

## Identity

A peer's name is bound once per connection and cannot be changed afterwards
(`PeerData::bNameAssigned`). This matters because `CommandManager::addCommandList()` resolves a
command list to a player *by name*: a rename mid-match was a way to take over another player's
commands. Names must be non-empty, at most 64 bytes and free of control characters.

The path-budget client id is now a stable per-connection id rather than
`peer->address.host ^ peer->address.port`, which collides behind NAT and is trivially spoofable.

## Commands

`CommandManager::addCommandList()` drops (not just logs) anything that is not the sending
peer's own command, anything whose command id or parameter count would make
`Command::executeCommand()` throw out of the simulation loop, and any cycle outside the
acceptable window - checked *before* `addCommand()` resizes its timeslot vector.

Cycles in the past stay acceptable: that is how the rolling 2.5 s history and its
retransmissions work, and `nextExpectedCommandsCycle` still filters what was already applied.
Players who share a house each have their own player id, so co-op control is unaffected.
Replays and savegames load through `CommandManager::load()`, which does not apply the network
window.

## Wire decoding

`ENetPacketIStream` uses subtraction-form bounds (`length > dataLength - currentPos`), copies
through `memcpy` instead of dereferencing unaligned typed pointers, caps a single string field,
and rejects boolean encodings other than 0/1. `InputStream::getRemainingLength()` lets element
counts be checked against the bytes that are actually present before anything is allocated;
file-backed streams keep the default "unknown" length, so savegame and map parsing is
unchanged.

The additive bounds checks matter specifically on wasm32, where `size_t` is 32 bits and
`currentPos + length` wraps. The same applies to the mod unpacker, which now goes through
`ModTransferValidation::fitsWithinPayload()`.

## Received content

- **Maps**: the filename from `SENDGAMEINFO` must be a single portable path component; the
  `.ini` extension is added if missing, the payload is size capped, and the resolved parent
  directory is verified to be `maps/multiplayer` before anything is written. Legitimate custom
  maps are unaffected.
- **Mods**: chunks are only accepted for a transfer this client requested, the announced size
  is pinned for the whole transfer, and a "successful" completion that did not deliver every
  announced byte is refused. The payload is unpacked into a staging directory and its combined
  checksum is compared with the checksum the host announced *before* anything is installed or
  activated.

  Be clear about what that checksum is worth: it is FNV-1a over canonicalised INI files, and it
  comes from the same peer as the payload. It is an integrity and ordering guarantee - nothing
  lands in the mod directory or becomes active unless it matches what the lobby verified
  against - not authentication. A malicious host can still send a mod whose checksum matches
  the mod it announced.

## Abuse accounting

Refused packets are counted per peer, the log line is throttled, and a peer that keeps sending
garbage is disconnected. A separate per-peer packet rate limit (4096/s, far above a mod
transfer burst) drops peers that only want to burn CPU.

## Running the tests

```bash
ctest --test-dir build --output-on-failure -R 'dunelegacy_tests|network_wire_harness'
```

- `tests/NetworkHardeningTestCase/NetworkHardeningTestCase.cpp` drives the admission policy,
  the real `ENetPacketIStream` over crafted packets, the real `ChangeEventList` parser, the
  command table and cycle window, path-budget orders and the mod payload bounds.
- `tests/wasm/NetworkWireHarness.cpp` is the same wire boundary without Catch2 or game data, so
  it can run where `size_t` is 32 bits:

```bash
tests/wasm/run-network-wire-harness.sh wasm     # emcc + node
tests/wasm/run-network-wire-harness.sh native   # host compiler, ASan/UBSan
```

## Known gaps

- There is no desync detection or state hash, so a divergence between peers still surfaces as
  unexplained disagreement rather than an error.
- Lockstep still has no timeout: a peer that stops sending commands stalls the match
  indefinitely.
- `CONNECT` still points a client at an address chosen by the host; only obviously implausible
  destinations are refused. A relay transport removes the packet entirely.
- The metaserver list parser still stops at the first row it does not understand.
