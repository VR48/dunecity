'use strict';

const assert = require('node:assert/strict');
const { describe, it } = require('node:test');

const protocol = require('../src/protocol');
const { CLOSE, GAME, PHASE, S2C } = require('../src/constants');
const { RoomStore } = require('../src/rooms');
const {
  GAME_PROTOCOL,
  CONTENT_HASH,
  startRelay,
  admitJoin,
  joinAsHost,
  joinAsClient,
  TestClient,
} = require('./helpers');

// A room that has started a match cannot take on a new participant: there is no snapshot and no
// reconnect protocol, so a late arrival has nothing to join and the lockstep peers have nothing
// to wait for. Two doors have to be shut, and both of them are races: the HTTP admission that
// issues a grant, and the redemption of a grant that was issued while the room was still a lobby.

const CUSTOM_ROOM = { admission: { mode: 'custom', maxPeers: 4 } };

async function startMatch(relay, host) {
  host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    const room = [...relay.store.rooms.values()][0];
    if (room && room.phase === PHASE.MATCH) return room;
    await new Promise((resolve) => { setTimeout(resolve, 5); });
  }
  throw new Error('the room never entered the match phase');
}

describe('room phase and admission', () => {
  it('refuses a new admission once the match has started', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, CUSTOM_ROOM);
      const guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      const room = await startMatch(relay, host);

      // Two of four seats are still free: it is the phase that refuses, not the capacity.
      assert.equal(room.maxPeers, 4);
      assert.equal(room.peers.size, 2);

      const refused = await admitJoin(relay, host.room);
      assert.equal(refused.status, 409);
      assert.equal(refused.fields.code, 'match_in_progress');
      assert.equal(refused.fields.status, 'error');
      assert.ok(!refused.text.includes(host.admission.fields.grant));

      // Nothing was handed out, so nothing new can be redeemed either.
      assert.equal(relay.store.grants.size, 0);
      assert.equal(room.peers.size, 2);

      host.client.close();
      guest.client.close();
    } finally {
      await relay.stop();
    }
  });

  it('refuses a lobby grant that the match start overtook', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, CUSTOM_ROOM);
      // A grant obtained while the room was still a lobby, held back until after the start.
      const admission = await admitJoin(relay, host.room);
      assert.equal(admission.fields.status, 'ok');
      const room = await startMatch(relay, host);
      assert.equal(room.outstandingGrants, 1);

      const late = await TestClient.connect(relay.socketUrl);
      late.send(protocol.encodeHello({
        grant: admission.fields.grant,
        gameProtocol: GAME_PROTOCOL,
        runtime: 'browser',
        displayName: 'late',
        contentHash: CONTENT_HASH,
      }));
      const closed = await late.waitForClose();
      assert.equal(closed.code, CLOSE.UNAUTHORIZED);
      assert.equal(room.peers.size, 1, 'the late peer never entered the room');

      // The refused grant was consumed, not left lying around for a second attempt.
      assert.equal(relay.store.grants.size, 0);
      assert.equal(room.outstandingGrants, 0);
      const retry = await TestClient.connect(relay.socketUrl);
      retry.send(protocol.encodeHello({
        grant: admission.fields.grant,
        gameProtocol: GAME_PROTOCOL,
        runtime: 'browser',
        displayName: 'late',
        contentHash: CONTENT_HASH,
      }));
      assert.equal((await retry.waitForClose()).code, CLOSE.UNAUTHORIZED);

      host.client.close();
    } finally {
      await relay.stop();
    }
  });

  it('lets an ordinary lobby guest leave and rejoin', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, CUSTOM_ROOM);
      const first = await joinAsClient(relay, host.room, { name: 'guest' });
      await host.client.expect(S2C.PEER_JOINED);

      first.client.send(protocol.encodeClientLeave());
      await first.client.waitForClose();
      await host.client.expect(S2C.PEER_LEFT);

      const second = await joinAsClient(relay, host.room, { name: 'guest' });
      assert.equal(second.welcome.phase, PHASE.LOBBY);
      const rejoined = await host.client.expect(S2C.PEER_JOINED);
      assert.notEqual(rejoined.peerId, first.welcome.peerId);

      host.client.close();
      second.client.close();
    } finally {
      await relay.stop();
    }
  });

  it('leaves the co-op intermission alone but still admits nobody new', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, CUSTOM_ROOM);
      const guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      // A grant issued in the lobby, before anything started.
      const stale = await admitJoin(relay, host.room);
      assert.equal(stale.fields.status, 'ok');

      const room = await startMatch(relay, host);
      assert.equal((await guest.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.MATCH);

      // The next mission arrives while the session is still in-game, then the host returns the
      // room to the lobby for the intermission.
      const mission = protocol.gamePayload(GAME.COOP_MISSION, Buffer.alloc(24));
      host.client.send(protocol.encodeClientRelay({
        gameMessageType: GAME.COOP_MISSION, payload: mission,
      }));
      assert.equal((await guest.client.expect(S2C.RELAY)).gameMessageType, GAME.COOP_MISSION);

      host.client.send(protocol.encodeClientRoomPhase(PHASE.LOBBY));
      assert.equal((await guest.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.LOBBY);

      // Both members are still there and still talking.
      assert.equal(room.peers.size, 2);
      assert.equal(host.client.closeInfo, null);
      assert.equal(guest.client.closeInfo, null);
      const chat = protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('still here'));
      host.client.send(protocol.encodeClientRelay({
        gameMessageType: GAME.CHATMESSAGE, payload: chat,
      }));
      assert.equal((await guest.client.expect(S2C.RELAY)).gameMessageType, GAME.CHATMESSAGE);

      // A room back in the lobby is not a fresh lobby: the campaign is in the peers' memory.
      const refused = await admitJoin(relay, host.room);
      assert.equal(refused.status, 409);
      assert.equal(refused.fields.code, 'match_in_progress');

      // And the grant from before the match is not redeemable now that the phase moved twice.
      const late = await TestClient.connect(relay.socketUrl);
      late.send(protocol.encodeHello({
        grant: stale.fields.grant,
        gameProtocol: GAME_PROTOCOL,
        runtime: 'browser',
        displayName: 'late',
        contentHash: CONTENT_HASH,
      }));
      assert.equal((await late.waitForClose()).code, CLOSE.UNAUTHORIZED);
      assert.equal(room.peers.size, 2);

      host.client.close();
      guest.client.close();
    } finally {
      await relay.stop();
    }
  });
});

describe('RoomStore phase bookkeeping', () => {
  function makeRoom() {
    const store = new RoomStore({});
    const { room, grant } = store.createRoom({
      maxPeers: 4, mode: 'custom', gameProtocol: 5, contentHash: '', appVersion: '1.0.655',
    });
    return { store, room, grant };
  }

  it('bumps the epoch on a real change only', () => {
    const { store, room } = makeRoom();
    assert.equal(room.phaseEpoch, 0);
    assert.equal(store.setRoomPhase(room, PHASE.LOBBY), false);
    assert.equal(room.phaseEpoch, 0);
    assert.equal(store.setRoomPhase(room, PHASE.MATCH), true);
    assert.equal(room.phaseEpoch, 1);
    assert.equal(store.setRoomPhase(room, PHASE.MATCH), false);
    assert.equal(room.phaseEpoch, 1);
    assert.equal(store.setRoomPhase(room, PHASE.LOBBY), true);
    assert.equal(room.phaseEpoch, 2);
  });

  it('remembers that a match started, even back in the lobby', () => {
    const { store, room } = makeRoom();
    assert.equal(room.everStarted, false);
    store.setRoomPhase(room, PHASE.MATCH);
    store.setRoomPhase(room, PHASE.LOBBY);
    assert.equal(room.everStarted, true);
    assert.throws(() => store.joinRoom(room.code, {
      gameProtocol: 5, contentHash: '',
    }), /already started/);
  });

  it('keeps the host grant redeemable while the room is untouched', () => {
    const { store, room, grant } = makeRoom();
    const admitted = store.consumeGrant(grant);
    assert.notEqual(admitted, null);
    assert.equal(admitted.room, room);
  });
});
