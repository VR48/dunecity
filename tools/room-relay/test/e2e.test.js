'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const protocol = require('../src/protocol');
const { S2C, ROLE, PHASE, GAME, CLOSE, DIAGNOSTIC_KIND } = require('../src/constants');
const {
  startRelay, joinAsHost, joinAsClient, admitHost, admitJoin, TestClient, delay, GAME_PROTOCOL,
} = require('./helpers');

test('a host and a browser client meet in a room and exchange game payloads', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay, { name: 'desktop', runtime: 'native' });
  assert.equal(host.welcome.role, ROLE.HOST);
  assert.equal(host.welcome.gameProtocol, GAME_PROTOCOL);
  assert.equal(host.welcome.maxPeers, 2);
  assert.equal(host.welcome.phase, PHASE.LOBBY);
  assert.ok(host.welcome.peerId > 0);

  const guest = await joinAsClient(relay, host.room, { name: 'browser', runtime: 'browser' });
  assert.equal(guest.welcome.role, ROLE.CLIENT);
  assert.equal(guest.welcome.roomCode, host.room);
  assert.notEqual(guest.welcome.peerId, host.welcome.peerId);

  // Both sides learn about each other exactly once, with the right role and runtime.
  const hostSawGuest = await host.client.expect(S2C.PEER_JOINED);
  assert.equal(hostSawGuest.peerId, guest.welcome.peerId);
  assert.equal(hostSawGuest.role, ROLE.CLIENT);
  assert.equal(hostSawGuest.displayName, 'browser');
  assert.equal(hostSawGuest.runtime, 'browser');

  const guestSawHost = await guest.client.expect(S2C.PEER_JOINED);
  assert.equal(guestSawHost.peerId, host.welcome.peerId);
  assert.equal(guestSawHost.role, ROLE.HOST);
  assert.equal(guestSawHost.displayName, 'desktop');
  assert.equal(guestSawHost.runtime, 'native');

  // A host-only lobby message reaches the client with a relay-assigned sender id.
  const settings = protocol.gamePayload(GAME.SENDGAMEINFO, Buffer.from('map-bytes'));
  host.client.send(protocol.encodeClientRelay({
    gameMessageType: GAME.SENDGAMEINFO, payload: settings,
  }));
  const received = await guest.client.expect(S2C.RELAY);
  assert.equal(received.senderPeerId, host.welcome.peerId);
  assert.equal(received.gameMessageType, GAME.SENDGAMEINFO);
  assert.deepEqual(received.payload, settings);

  // And the client can answer with its own config hash.
  const hash = protocol.gamePayload(GAME.CONFIG_HASH, Buffer.from([5, 0, 0, 0]));
  guest.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.CONFIG_HASH, payload: hash }));
  const answer = await host.client.expect(S2C.RELAY);
  assert.equal(answer.senderPeerId, guest.welcome.peerId);
  assert.deepEqual(answer.payload, hash);

  host.client.close();
  guest.client.close();
});

test('a directed message goes only to its recipient', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
  const a = await joinAsClient(relay, host.room, { name: 'alpha' });
  const b = await joinAsClient(relay, host.room, { name: 'bravo' });
  await host.client.expect(S2C.PEER_JOINED);
  await host.client.expect(S2C.PEER_JOINED);
  await a.client.expect(S2C.PEER_JOINED);   // host
  await a.client.expect(S2C.PEER_JOINED);   // bravo
  await b.client.expect(S2C.PEER_JOINED);   // host
  await b.client.expect(S2C.PEER_JOINED);   // alpha

  const chat = protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('hi'));
  a.client.send(protocol.encodeClientRelay({
    recipient: b.welcome.peerId, gameMessageType: GAME.CHATMESSAGE, payload: chat,
  }));

  const toB = await b.client.expect(S2C.RELAY);
  assert.equal(toB.senderPeerId, a.welcome.peerId);

  // Give the host a chance to receive something it should not have received.
  await delay(120);
  assert.equal(host.client.drain().length, 0, 'a directed message must not be broadcast');

  host.client.close();
  a.client.close();
  b.client.close();
});

test('membership is announced once per peer and departure exactly once', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay, { admission: { mode: 'custom', maxPeers: 4 } });
  const a = await joinAsClient(relay, host.room, { name: 'alpha' });
  const b = await joinAsClient(relay, host.room, { name: 'bravo' });

  const hostJoins = [await host.client.expect(S2C.PEER_JOINED), await host.client.expect(S2C.PEER_JOINED)];
  assert.deepEqual(
    hostJoins.map((m) => m.peerId).sort(),
    [a.welcome.peerId, b.welcome.peerId].sort(),
  );

  const aJoins = [await a.client.expect(S2C.PEER_JOINED), await a.client.expect(S2C.PEER_JOINED)];
  assert.deepEqual(
    aJoins.map((m) => m.peerId).sort(),
    [host.welcome.peerId, b.welcome.peerId].sort(),
  );
  // The host is identified as the host, not just as another peer.
  assert.equal(aJoins.find((m) => m.peerId === host.welcome.peerId).role, ROLE.HOST);

  await b.client.expect(S2C.PEER_JOINED);
  await b.client.expect(S2C.PEER_JOINED);

  b.client.send(protocol.encodeClientLeave(1));
  await b.client.waitForClose();

  const hostLeft = await host.client.expect(S2C.PEER_LEFT);
  assert.equal(hostLeft.peerId, b.welcome.peerId);
  const aLeft = await a.client.expect(S2C.PEER_LEFT);
  assert.equal(aLeft.peerId, b.welcome.peerId);

  await delay(150);
  assert.equal(host.client.drain().length, 0, 'PEER_LEFT must not be repeated');
  assert.equal(a.client.drain().length, 0, 'PEER_LEFT must not be repeated');

  host.client.close();
  a.client.close();
});

test('the host drives the room phase and match traffic follows it', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  // Commands are refused while the room is still a lobby.
  const command = protocol.gamePayload(GAME.COMMANDLIST, Buffer.alloc(8));
  guest.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.COMMANDLIST, payload: command }));
  const refused = await guest.client.expect(S2C.ERROR);
  assert.equal(refused.code, CLOSE.FORBIDDEN);

  // The host starts the match.
  const start = protocol.gamePayload(GAME.STARTGAME, Buffer.from([0xb8, 0x0b, 0, 0]));
  host.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.STARTGAME, payload: start }));
  assert.equal((await guest.client.expect(S2C.RELAY)).gameMessageType, GAME.STARTGAME);

  host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
  const phase = await guest.client.expect(S2C.ROOM_PHASE_CHANGED);
  assert.equal(phase.phase, PHASE.MATCH);
  assert.equal(phase.byPeerId, host.welcome.peerId);

  // Now commands flow and lobby messages do not.
  guest.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.COMMANDLIST, payload: command }));
  assert.equal((await host.client.expect(S2C.RELAY)).gameMessageType, GAME.COMMANDLIST);

  const rename = protocol.gamePayload(GAME.SENDNAME, Buffer.from([0, 0, 0, 0]));
  guest.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.SENDNAME, payload: rename }));
  assert.equal((await guest.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

  host.client.close();
  guest.client.close();
});

test('co-op continuation is carried after the match, then the lobby reopens', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
  await guest.client.expect(S2C.ROOM_PHASE_CHANGED);

  // The next mission arrives while the session is still marked as in-game.
  const mission = protocol.gamePayload(GAME.COOP_MISSION, Buffer.alloc(24));
  host.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.COOP_MISSION, payload: mission }));
  assert.equal((await guest.client.expect(S2C.RELAY)).gameMessageType, GAME.COOP_MISSION);

  // A client may not fabricate a mission.
  guest.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.COOP_MISSION, payload: mission }));
  assert.equal((await guest.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

  host.client.send(protocol.encodeClientRoomPhase(PHASE.LOBBY));
  assert.equal((await guest.client.expect(S2C.ROOM_PHASE_CHANGED)).phase, PHASE.LOBBY);

  host.client.close();
  guest.client.close();
});

test('heartbeats are acknowledged with the value the client chose', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  host.client.send(protocol.encodeClientHeartbeat(0xdeadbeef));
  const ack = await host.client.expect(S2C.HEARTBEAT_ACK);
  assert.equal(ack.clientEchoMs, 0xdeadbeef);

  host.client.close();
});

test('diagnostics are broadcast without touching the game protocol', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  const digest = Buffer.alloc(24, 7);
  guest.client.send(protocol.encodeClientDiagnostic(DIAGNOSTIC_KIND.STATE_DIGEST, digest));
  const seen = await host.client.expect(S2C.DIAGNOSTIC);
  assert.equal(seen.senderPeerId, guest.welcome.peerId);
  assert.equal(seen.kind, DIAGNOSTIC_KIND.STATE_DIGEST);
  assert.deepEqual(seen.payload, digest);

  host.client.close();
  guest.client.close();
});

test('a large but legal map payload survives the round trip unchanged', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  const body = Buffer.alloc(200000);
  for (let i = 0; i < body.length; i += 1) body[i] = (i * 31) & 0xff;
  const payload = protocol.gamePayload(GAME.SENDGAMEINFO, body);

  host.client.send(protocol.encodeClientRelay({ gameMessageType: GAME.SENDGAMEINFO, payload }));
  const received = await guest.client.expect(S2C.RELAY, 5000);
  assert.equal(received.payload.length, payload.length);
  assert.ok(received.payload.equals(payload));

  host.client.close();
  guest.client.close();
});

test('the host leaving terminates the room for everybody', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  host.client.close();

  // Membership bookkeeping stays uniform: the host is announced as leaving like any other peer,
  // and the room termination follows immediately.
  const left = await guest.client.expect(S2C.PEER_LEFT);
  assert.equal(left.peerId, host.welcome.peerId);

  const closed = await guest.client.expect(S2C.ROOM_CLOSED);
  assert.equal(closed.code, CLOSE.HOST_LEFT);
  const info = await guest.client.waitForClose();
  assert.equal(info.code, CLOSE.HOST_LEFT);

  // The room code stops working immediately.
  const rejoin = await admitJoin(relay, host.room);
  assert.equal(rejoin.fields.code, 'room_not_found');
});

test('a client leaving does not end the room, and the seat is reusable', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const first = await joinAsClient(relay, host.room, { name: 'first' });
  await host.client.expect(S2C.PEER_JOINED);
  await first.client.expect(S2C.PEER_JOINED);

  first.client.send(protocol.encodeClientLeave(1));
  await first.client.waitForClose();
  await host.client.expect(S2C.PEER_LEFT);

  const second = await joinAsClient(relay, host.room, { name: 'second' });
  const rejoined = await host.client.expect(S2C.PEER_JOINED);
  assert.equal(rejoined.displayName, 'second');
  assert.notEqual(rejoined.peerId, first.welcome.peerId);

  host.client.close();
  second.client.close();
});

test('shutting the relay down tells every room why', async (t) => {
  const relay = await startRelay();
  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  await relay.stop();

  for (const peer of [host.client, guest.client]) {
    const closed = await peer.expect(S2C.ROOM_CLOSED);
    assert.equal(closed.code, CLOSE.SERVER_SHUTDOWN);
  }
});

test('the lifecycle log records the room, both participants and their runtimes', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay, { runtime: 'native', name: 'desktop' });
  const guest = await joinAsClient(relay, host.room, { runtime: 'browser', name: 'browser' });
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);
  host.client.send(protocol.encodeClientRoomPhase(2));
  await guest.client.expect(S2C.ROOM_PHASE_CHANGED);
  guest.client.send(protocol.encodeClientLeave(1));
  await guest.client.waitForClose();
  await host.client.expect(S2C.PEER_LEFT);

  const created = relay.log.events('room_created');
  assert.equal(created.length, 1);
  assert.match(created[0].room, /^[A-Za-z0-9_-]{22}$/);
  assert.notEqual(created[0].room, host.room);
  assert.equal(created[0].hostRuntime, 'native');
  assert.equal(created[0].transport, relay.config.observedTransport);

  const joined = relay.log.events('participant_joined');
  assert.equal(joined.length, 2);
  assert.deepEqual(joined.map((e) => e.role).sort(), ['client', 'host']);
  assert.deepEqual(joined.map((e) => e.runtime).sort(), ['browser', 'native']);
  for (const event of joined) {
    assert.equal(event.room, created[0].room);
    assert.equal(event.appVersion, '1.0.655');
    assert.ok(event.addressTag.length > 0);
  }

  const phase = relay.log.events('room_phase');
  assert.equal(phase.length, 1);
  assert.equal(phase[0].phase, 2);

  const left = relay.log.events('participant_left');
  assert.equal(left.length, 1);
  assert.equal(left[0].role, 'client');
  assert.ok(typeof left[0].runtimeMs === 'number');

  // Nothing in the log is a credential or game content.
  const serialized = JSON.stringify(relay.log.events());
  assert.ok(!serialized.includes(host.room));
  assert.ok(!serialized.includes(host.admission.fields.grant));
  assert.ok(!serialized.includes(guest.admission.fields.grant));

  host.client.close();
});

test('a room reports the correct code back to the host so it can be shown to a friend', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  const client = await TestClient.connect(relay.socketUrl);
  client.send(protocol.encodeHello({
    grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL, displayName: 'host',
  }));
  const welcome = await client.expect(S2C.WELCOME);
  assert.equal(welcome.roomCode, admission.fields.room);
  client.close();
});
