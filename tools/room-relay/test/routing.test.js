'use strict';

const assert = require('node:assert/strict');
const { describe, it } = require('node:test');

const protocol = require('../src/protocol');
const {
  CLOSE, GAME, GAME_POLICY, PHASE, RELAY_FLAGS, S2C,
} = require('../src/constants');
const { startRelay, joinAsHost, joinAsClient, delay } = require('./helpers');

// The relay's matrix has to agree with the client's, because the client applies the same one to
// what it receives. Anything the relay carries but the client refuses is traffic that is
// guaranteed to be thrown away, and anything the client sends but the relay refuses is a broken
// lobby. This table is transcribed from the two C++ sources of truth in the crossplay checkout:
//
//   gameMessageRule()                       include/Network/RoomRelayProtocol.h
//   NetworkPacketPolicy::classifyPacket()   include/Network/NetworkPacketPolicy.h
//
// sender/phase come from the first; "the host is the only peer that acts on this" comes from the
// second, and is why a client may only address those two types to the host.

const EXPECTED = [
  // type,                    carried, sender,   phase,   clientDestination
  [GAME.CONNECT, false],
  [GAME.DISCONNECT, false],
  [GAME.PEER_CONNECTED, false],
  [GAME.SENDGAMEINFO, true, 'host', 'lobby', 'any'],
  [GAME.SENDNAME, true, 'any', 'lobby', 'any'],
  [GAME.CHATMESSAGE, true, 'any', 'any', 'any'],
  [GAME.CHANGEEVENTLIST, true, 'any', 'lobby', 'host'],
  [GAME.STARTGAME, true, 'host', 'lobby', 'any'],
  [GAME.COMMANDLIST, true, 'any', 'match', 'any'],
  [GAME.SELECTIONLIST, true, 'any', 'match', 'any'],
  [GAME.CONFIG_HASH, true, 'any', 'lobby', 'any'],
  [GAME.SETPATHBUDGET, true, 'host', 'match', 'any'],
  [GAME.CLIENTSTATS, true, 'client', 'match', 'host'],
  [GAME.MOD_INFO, false],
  [GAME.MOD_REQUEST, false],
  [GAME.MOD_CHUNK, false],
  [GAME.MOD_COMPLETE, false],
  [GAME.MOD_ACK, false],
  [GAME.KEEPALIVE, true, 'any', 'any', 'any'],
  [GAME.COOP_MISSION, true, 'host', 'any', 'any'],
];

function allowed(sender, phase, entry) {
  const [, carried, senderRule, phaseRule] = entry;
  if (!carried) return false;
  if (senderRule === 'host' && sender !== 'host') return false;
  if (senderRule === 'client' && sender === 'host') return false;
  if (phaseRule === 'lobby' && phase !== 'lobby') return false;
  if (phaseRule === 'match' && phase !== 'match') return false;
  return true;
}

function payloadFor(type) {
  return protocol.gamePayload(type, Buffer.alloc(8));
}

describe('the relay matrix agrees with the client matrix', () => {
  it('has the same sender, phase and destination rules as the C++ table', () => {
    for (const entry of EXPECTED) {
      const [type, carried, sender, phase, destination] = entry;
      const policy = GAME_POLICY.get(type);
      if (!carried) {
        assert.equal(policy, undefined, `game message ${type} must not be carried`);
        continue;
      }
      assert.notEqual(policy, undefined, `game message ${type} must be carried`);
      assert.equal(policy.sender, sender, `sender rule for ${type}`);
      assert.equal(policy.phase, phase, `phase rule for ${type}`);
      assert.equal(policy.destination, destination, `destination rule for ${type}`);
    }
    assert.equal(GAME_POLICY.size, EXPECTED.filter((e) => e[1]).length,
      'the relay carries nothing the table does not list');
  });

  it('accepts and refuses exactly what the table says, over real sockets', async () => {
    // One refusal per row is expected, so the soft-error budget must not end the test for us.
    const relay = await startRelay({ maxSoftErrors: 1000 });
    let host;
    let guest;
    try {
      host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);
      const hostPeerId = host.welcome.peerId;
      const guestPeerId = guest.welcome.peerId;

      for (const phase of ['lobby', 'match']) {
        if (phase === 'match') {
          host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
          assert.equal((await guest.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.MATCH);
        }

        for (const entry of EXPECTED) {
          const type = entry[0];
          for (const sender of ['host', 'client']) {
            const from = sender === 'host' ? host.client : guest.client;
            const to = sender === 'host' ? guest.client : host.client;
            // A client addresses the host; the host broadcasts. Both satisfy every
            // destination rule, so this loop measures the sender and phase rules alone.
            from.send(protocol.encodeClientRelay({
              recipient: sender === 'host' ? 0 : hostPeerId,
              gameMessageType: type,
              payload: payloadFor(type),
            }));

            const label = `${type} from ${sender} in ${phase}`;
            if (allowed(sender, phase, entry)) {
              const received = await to.expect(S2C.RELAY);
              assert.equal(received.gameMessageType, type, label);
              assert.equal(received.senderPeerId,
                sender === 'host' ? hostPeerId : guestPeerId, label);
            } else {
              const error = await from.expect(S2C.ERROR);
              assert.equal(error.code, CLOSE.FORBIDDEN, label);
            }
          }
        }
      }

      assert.equal(host.client.closeInfo, null);
      assert.equal(guest.client.closeInfo, null);
    } finally {
      if (host) host.client.close();
      if (guest) guest.client.close();
      await relay.stop();
    }
  });
});

/** Waits for every membership announcement in a three-peer room, so nothing is left queued. */
async function settleThree(host, a, b) {
  await host.client.expect(S2C.PEER_JOINED);
  await host.client.expect(S2C.PEER_JOINED);
  await a.client.expect(S2C.PEER_JOINED);
  await a.client.expect(S2C.PEER_JOINED);
  await b.client.expect(S2C.PEER_JOINED);
  await b.client.expect(S2C.PEER_JOINED);
}

describe('host-destined messages', () => {
  it('carries a lobby slot change from a client only to the host', async () => {
    const relay = await startRelay({ maxSoftErrors: 1000 });
    let host;
    let a;
    let b;
    try {
      host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      a = await joinAsClient(relay, host.room, { name: 'a' });
      b = await joinAsClient(relay, host.room, { name: 'b' });
      await settleThree(host, a, b);

      const change = {
        gameMessageType: GAME.CHANGEEVENTLIST,
        payload: payloadFor(GAME.CHANGEEVENTLIST),
      };

      // To the host: carried, and only the host sees it.
      a.client.send(protocol.encodeClientRelay({ ...change, recipient: host.welcome.peerId }));
      assert.equal((await host.client.expect(S2C.RELAY)).gameMessageType, GAME.CHANGEEVENTLIST);

      // Broadcast from a client: refused, because every other client would refuse it anyway.
      a.client.send(protocol.encodeClientRelay({ ...change, recipient: 0 }));
      assert.equal((await a.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

      // Straight at another client: refused for the same reason.
      a.client.send(protocol.encodeClientRelay({ ...change, recipient: b.welcome.peerId }));
      assert.equal((await a.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

      await delay(80);
      assert.equal(b.client.drain().length, 0, 'no client sees another client seat itself');

      // The host broadcasts the authoritative list, which is the other half of the flow.
      host.client.send(protocol.encodeClientRelay({ ...change, recipient: 0 }));
      assert.equal((await a.client.expect(S2C.RELAY)).gameMessageType, GAME.CHANGEEVENTLIST);
      assert.equal((await b.client.expect(S2C.RELAY)).gameMessageType, GAME.CHANGEEVENTLIST);
    } finally {
      for (const peer of [host, a, b]) if (peer) peer.client.close();
      await relay.stop();
    }
  });

  it('carries client statistics only to the host', async () => {
    const relay = await startRelay({ maxSoftErrors: 1000 });
    let host;
    let a;
    let b;
    try {
      host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      a = await joinAsClient(relay, host.room, { name: 'a' });
      b = await joinAsClient(relay, host.room, { name: 'b' });
      await settleThree(host, a, b);
      host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
      assert.equal((await a.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.MATCH);
      assert.equal((await b.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.MATCH);

      const stats = {
        gameMessageType: GAME.CLIENTSTATS,
        payload: payloadFor(GAME.CLIENTSTATS),
      };
      a.client.send(protocol.encodeClientRelay({ ...stats, recipient: host.welcome.peerId }));
      assert.equal((await host.client.expect(S2C.RELAY)).gameMessageType, GAME.CLIENTSTATS);

      a.client.send(protocol.encodeClientRelay({ ...stats, recipient: 0 }));
      assert.equal((await a.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
      await delay(80);
      assert.equal(b.client.drain().length, 0);
    } finally {
      for (const peer of [host, a, b]) if (peer) peer.client.close();
      await relay.stop();
    }
  });

  it('still carries a config hash in both directions', async () => {
    // The real flow: a client sends its hashes to the host, the host broadcasts its own.
    const relay = await startRelay();
    let host;
    let guest;
    try {
      host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      const hash = {
        gameMessageType: GAME.CONFIG_HASH,
        payload: payloadFor(GAME.CONFIG_HASH),
      };
      guest.client.send(protocol.encodeClientRelay({ ...hash, recipient: host.welcome.peerId }));
      assert.equal((await host.client.expect(S2C.RELAY)).gameMessageType, GAME.CONFIG_HASH);

      host.client.send(protocol.encodeClientRelay({ ...hash, recipient: 0 }));
      assert.equal((await guest.client.expect(S2C.RELAY)).gameMessageType, GAME.CONFIG_HASH);

      // A client broadcast is not restricted here: classifyPacket accepts a config hash from
      // any peer while the room is a lobby, so nothing would refuse it.
      guest.client.send(protocol.encodeClientRelay({ ...hash, recipient: 0 }));
      assert.equal((await host.client.expect(S2C.RELAY)).gameMessageType, GAME.CONFIG_HASH);
    } finally {
      for (const peer of [host, guest]) if (peer) peer.client.close();
      await relay.stop();
    }
  });
});

describe('envelope channels and flags', () => {
  async function sendRaw(relay, overrides) {
    const host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
    const guest = await joinAsClient(relay, host.room);
    await host.client.expect(S2C.PEER_JOINED);
    await guest.client.expect(S2C.PEER_JOINED);
    guest.client.send(protocol.encodeClientRelay({
      recipient: host.welcome.peerId,
      gameMessageType: GAME.CHATMESSAGE,
      payload: payloadFor(GAME.CHATMESSAGE),
      ...overrides,
    }));
    return { host, guest };
  }

  it('carries the two channels the client knows about', async () => {
    const relay = await startRelay();
    let peers;
    try {
      for (const channel of [0, 1]) {
        peers = await sendRaw(relay, { channel });
        const received = await peers.host.client.expect(S2C.RELAY);
        assert.equal(received.channel, channel);
        peers.host.client.close();
        peers.guest.client.close();
        await delay(30);
      }
    } finally {
      await relay.stop();
    }
  });

  it('refuses a channel that does not exist', async () => {
    const relay = await startRelay();
    try {
      const peers = await sendRaw(relay, { channel: 2 });
      assert.equal((await peers.guest.client.waitForClose()).code, CLOSE.PROTOCOL_ERROR);
      assert.equal(peers.host.client.closeInfo, null, 'the room survives one bad sender');
      peers.host.client.close();
    } finally {
      await relay.stop();
    }
  });

  it('accepts the defined flag bit and refuses every other one', async () => {
    const relay = await startRelay();
    try {
      const good = await sendRaw(relay, { flags: RELAY_FLAGS.RELIABLE });
      assert.equal((await good.host.client.expect(S2C.RELAY)).flags, RELAY_FLAGS.RELIABLE);
      good.host.client.close();
      good.guest.client.close();
      await delay(30);

      const none = await sendRaw(relay, { flags: 0 });
      assert.equal((await none.host.client.expect(S2C.RELAY)).flags, 0);
      none.host.client.close();
      none.guest.client.close();
      await delay(30);

      for (const flags of [0x02, 0x80, 0xff]) {
        const bad = await sendRaw(relay, { flags });
        assert.equal((await bad.guest.client.waitForClose()).code, CLOSE.PROTOCOL_ERROR,
          `flags 0x${flags.toString(16)} must not be forwarded`);
        bad.host.client.close();
        await delay(30);
      }
    } finally {
      await relay.stop();
    }
  });

  it('does not let a refused envelope reach anybody', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      const guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      const raw = protocol.encodeClientRelay({
        recipient: 0,
        flags: 0x04,
        gameMessageType: GAME.CHATMESSAGE,
        payload: payloadFor(GAME.CHATMESSAGE),
      });
      guest.client.send(raw);
      await guest.client.waitForClose();
      await delay(80);
      const seen = host.client.drain().filter((m) => m.type === S2C.RELAY);
      assert.equal(seen.length, 0, 'the host sees the peer leave, never the frame');
      host.client.close();
    } finally {
      await relay.stop();
    }
  });
});

describe('unknown clients and unknown ids', () => {
  it('refuses a game message id that is not in the table', async () => {
    const relay = await startRelay({ maxSoftErrors: 1000 });
    let host;
    let guest;
    try {
      host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      for (const type of [0, 21, 255, 65535]) {
        guest.client.send(protocol.encodeClientRelay({
          recipient: host.welcome.peerId,
          gameMessageType: type,
          payload: payloadFor(type),
        }));
        assert.equal((await guest.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN, `type ${type}`);
      }
      await delay(80);
      assert.equal(host.client.drain().length, 0);
    } finally {
      for (const peer of [host, guest]) if (peer) peer.client.close();
      await relay.stop();
    }
  });

  it('refuses a frame whose envelope and payload disagree about the type', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
      const guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      guest.client.send(protocol.encodeClientRelay({
        recipient: host.welcome.peerId,
        gameMessageType: GAME.CHATMESSAGE,
        payload: payloadFor(GAME.COMMANDLIST),
      }));
      assert.equal((await guest.client.waitForClose()).code, CLOSE.PROTOCOL_ERROR);
      host.client.close();
    } finally {
      await relay.stop();
    }
  });
});
