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
| `SENDGAMEINFO`, `CONNECT`, `DISCONNECT`, `STARTGAME`, `COOP_MISSION`, `MOD_INFO`, `MOD_CHUNK`, `MOD_COMPLETE` | client, host connection only | lobby (`DISCONNECT`, `COOP_MISSION`: any; campaign continuation follows the previous match) |
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

## Command authorization

`Command::executeCommand()` resolves the issuing player once per object action and refuses the
command unless the acting object - parameter 0, after the command's own `dynamic_cast` - belongs
to the issuer's house. The decision is `CommandAuthorization::authorizeActor()`, which sees only
simulation state, so every peer reaches the same verdict and a refusal is a deterministic no-op.

Preserved on purpose: co-op partners share a house and therefore both keep control; deviated
units follow their temporary owner because `UnitBase::deviate()` reassigns the owner; targets are
not constrained, because attacking, capturing and healing another house's object is the game.

Enum and boolean parameters are validated exactly (attack mode, the move/attack/produce/cancel/
hold booleans). City commands require a real issuing player with a house, validate the zone and
tool enums, re-apply the tile preconditions the local UI applies, and bound the tax rate and
police funding percentage. `CMD_CITY_PLACE_ZONE` is *not* dormant - the zone placement click
sends it - so it is constrained rather than refused.

Note on the city model: police funding is per house, but the city tax is a single shared value.
With more than one human house any of them can change it, inside the valid range. That is a
game-design question about shared city state, not something the network boundary can decide.

## Lobby authorization

The host owns the lobby. A client sends *requests*, and `LobbyAuthorization` accepts exactly what
the client's own widgets can produce: a seat claim carrying its own name for a seat that exists
and is not closed, and house/team/colour/partner-slot changes for a house where it already holds
a seat. One seat claim per transaction; the whole transaction is judged before anything is
applied, so a list mixing a legal and an illegal event changes nothing and is not rebroadcast.
The sender is the connection's bound peer name, which the ENet transport does not prove
cryptographically - this is authorization, not authentication.

## Command batches

`CommandManager::addCommandList()` runs in two passes. The first validates everything the batch
would add - cycle window, commands per cycle, commands per packet, ownership, well-formedness -
and drops the whole batch on any content fault without queueing anything or moving the
watermark. The second applies only the contiguous run starting at the cycle the receiver is
waiting for and stops at the first gap, so an unsorted, gapped or duplicated list can never
advance `nextExpectedCommandsCycle` past a cycle that was never received.

Cycles in the past stay acceptable: that is how the rolling 2.5 s history and its
retransmissions work. Ordinary packet loss on the unsequenced command channel recovers from the
overlapping history in the next packet, which is why a gap truncates the batch instead of
rejecting it. Players who share a house each have their own player id, so co-op control is
unaffected.

A packet may carry at most 512 cycle entries, 512 commands per entry and 4096 commands in
total; the first two bounds alone multiplied out to roughly a quarter of a million commands.
Selection lists are bounded at 2048 ids.

Replays and savegames load through `CommandManager::load()`, which applies its own file bounds
(cycle and total command count) and the same command well-formedness rule, but not the network
cycle window.

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

## Phase races are not abuse

Peers change phase at slightly different times: clients start their countdown half a round trip
before the host, and campaign co-op moves between missions. Command, selection, stats and late
lobby packets can therefore be legitimately in flight across a phase boundary. They are still
dropped - the phase gate is what protects the receiver, and lockstep retransmits the rolling
history once both sides are in the match - but `NetworkPacketPolicy::isExpectedOrderingRefusal()`
keeps them out of the abuse budget so an honest session cannot disconnect itself.

`COOP_MISSION` is the one host control packet that is valid in either phase: `sand.cpp` sends the
next campaign mission after `runMainLoop()` returns, while the session is still marked in-game
and the client waits in `CoopMissionWait`. It still has to come from the established host
connection, and the receiving handler still requires exactly one remote peer.

## Abuse accounting and traffic budgets

All of this lives in `NetworkPacketPolicy` (`RateWindow`, `RefusalCounter`) so it stays
transport independent and the tests use the same values production does:

- Refused packets are counted per peer with a 10 s decay, so isolated refusals never
  accumulate. A burst of 64 disconnects the peer **once**: the connection is marked, and after
  that nothing from it is parsed, counted or logged again.
- Per peer, per second: at most 4096 packets and 8 MiB. While this client is receiving a mod
  transfer it asked for, the byte budget on the host connection rises to 24 MiB/s, which passes
  a complete 10 MiB transfer arriving in one burst on loopback or LAN.
- ENet itself is capped before it allocates: `maximumPacketSize` 4 MiB (checked against the
  announced fragment total *before* the reassembly buffer is allocated) and
  `maximumWaitingData` 16 MiB per peer, down from 32 MiB each. The largest legitimate packet is
  a map inside `SENDGAMEINFO`; a mod chunk is 64 KiB.
- Only the host emits `DISCONNECT`. Every client is connected to every other client and sees
  its own ENet disconnect event, so a client-sent `DISCONNECT` was both redundant and refused by
  the receivers' host-only rule.

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

## Received game info

`SENDGAMEINFO` is decoded into temporaries and validated as a whole before membership changes or
the callback runs, so a malformed packet leaves the client exactly as it was.
`GameInitSettingsPolicy` bounds the game type and house enums, the house count, players per
house, team numbers, the filename, the map payload, the mod name and the player strings, and the
game speed. A map that cannot be stored safely rejects the whole packet rather than being played
from memory. The same validation runs on `COOP_MISSION`. None of it applies to local savegame or
map loading.

## Known gaps

- There is no desync detection or state hash, so a divergence between peers still surfaces as
  unexplained disagreement rather than an error.
- Lockstep still has no timeout: a peer that stops sending commands stalls the match
  indefinitely.
- **The native mesh is not authenticated.** Peer identity is "the name this connection bound
  first". Every peer now refuses a duplicate name, and a joining client only accepts inbound mesh
  connections during its join window and only in the lobby, but a peer that reaches a joining
  client inside that window is still admitted on the strength of its address and a free name.
  Closing this needs a host-issued, single-use introduction token per peer pair, carried in
  `CONNECT` and presented by the connecting peer, plus an expected-roster check before any
  gameplay state is allocated. That is a protocol change (new fields, new rejection cause) and is
  deliberately *not* improvised here: a 32-bit nonce in the existing packet would look like
  authentication without being any. Note that the protocol already requires an exact game-version
  match, so a protocol change does not break a compatibility that exists today.
- `CONNECT` still points a client at an address chosen by the host; ports below 1024 and
  non-unicast destinations are refused, but any other host:port is still reachable. A relay
  transport removes the packet entirely, and that is the secure path for Internet play.
- The metaserver list parser still stops at the first row it does not understand.
- Mod delivery still installs host-announced content after a checksum the same host supplied.
  The pre-install verification is an integrity and ordering guarantee only; for browser and
  crossplay rooms the intended answer is bundled content, not peer mod delivery.
- Lockstep still has no stall timeout, and client performance reports remain advisory input to
  the host's path-budget decision.
