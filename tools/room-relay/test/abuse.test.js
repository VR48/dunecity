'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const protocol = require('../src/protocol');
const { S2C, GAME, CLOSE, PHASE, LIMITS, LEAVE_REASON } = require('../src/constants');
const {
  startRelay, joinAsHost, joinAsClient, admitHost, admitJoin, TestClient, delay, GAME_PROTOCOL,
} = require('./helpers');

test('a replayed grant is refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  const first = await TestClient.connect(relay.socketUrl);
  first.send(protocol.encodeHello({ grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL }));
  await first.expect(S2C.WELCOME);

  const replay = await TestClient.connect(relay.socketUrl);
  replay.send(protocol.encodeHello({ grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL }));
  const err = await replay.expect(S2C.ERROR);
  assert.equal(err.code, CLOSE.UNAUTHORIZED);
  assert.equal((await replay.waitForClose()).code, CLOSE.UNAUTHORIZED);

  first.close();
});

test('two connections racing with the same grant admit at most one', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  const a = await TestClient.connect(relay.socketUrl);
  const b = await TestClient.connect(relay.socketUrl);
  const hello = protocol.encodeHello({ grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL });
  a.send(hello);
  b.send(hello);

  const outcomes = await Promise.all([a.next(), b.next()]);
  const welcomes = outcomes.filter((m) => m.type === S2C.WELCOME);
  const errors = outcomes.filter((m) => m.type === S2C.ERROR);
  assert.equal(welcomes.length, 1);
  assert.equal(errors.length, 1);
  assert.equal(errors[0].code, CLOSE.UNAUTHORIZED);

  a.close();
  b.close();
});

test('an expired grant is refused', async (t) => {
  const relay = await startRelay({ grantTtlMs: 60 });
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  await delay(150);

  const client = await TestClient.connect(relay.socketUrl);
  client.send(protocol.encodeHello({ grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL }));
  assert.equal((await client.expect(S2C.ERROR)).code, CLOSE.UNAUTHORIZED);
  client.close();
});

test('an invented grant is refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const client = await TestClient.connect(relay.socketUrl);
  client.send(protocol.encodeHello({ grant: 'f'.repeat(64), gameProtocol: GAME_PROTOCOL }));
  assert.equal((await client.expect(S2C.ERROR)).code, CLOSE.UNAUTHORIZED);
  client.close();
});

test('a foreign browser origin cannot even open the socket', async (t) => {
  const relay = await startRelay({ allowedOrigins: ['https://dunecity.example'] });
  t.after(() => relay.stop());

  const admission = await admitHost(relay, { __headers: { origin: 'https://dunecity.example' } });

  await assert.rejects(
    () => TestClient.connect(relay.socketUrl, { headers: { origin: 'https://evil.example' } }),
    (err) => err.statusCode === 403,
  );
  await assert.rejects(
    () => TestClient.connect(relay.socketUrl, { headers: { origin: 'null' } }),
    (err) => err.statusCode === 403,
  );

  const allowed = await TestClient.connect(relay.socketUrl, {
    headers: { origin: 'https://dunecity.example' },
  });
  allowed.send(protocol.encodeHello({ grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL }));
  await allowed.expect(S2C.WELCOME);
  allowed.close();
});

test('the first frame must be the handshake', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const client = await TestClient.connect(relay.socketUrl);
  client.send(protocol.encodeClientRelay({
    gameMessageType: GAME.CHATMESSAGE, payload: protocol.gamePayload(GAME.CHATMESSAGE),
  }));
  assert.equal((await client.expect(S2C.ERROR)).code, CLOSE.UNAUTHORIZED);
  assert.equal((await client.waitForClose()).code, CLOSE.UNAUTHORIZED);
});

test('a second handshake on the same socket is refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const second = await admitJoin(relay, host.room);
  host.client.send(protocol.encodeHello({
    grant: second.fields.grant, gameProtocol: GAME_PROTOCOL,
  }));
  assert.equal((await host.client.waitForClose()).code, CLOSE.PROTOCOL_ERROR);
});

test('a text frame closes the connection', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  host.client.sendText('{"type":"hello"}');
  assert.equal((await host.client.waitForClose()).code, CLOSE.PROTOCOL_ERROR);
});

test('an oversized frame closes the connection instead of being buffered', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const payload = protocol.gamePayload(GAME.CHATMESSAGE, Buffer.alloc(LIMITS.MAX_FRAME_BYTES));
  host.client.send(protocol.encodeClientRelay({
    gameMessageType: GAME.CHATMESSAGE, payload,
  }));
  const info = await host.client.waitForClose();
  assert.ok(info.code === 1009 || info.code === CLOSE.TOO_LARGE,
    `expected a size-related close, got ${info.code}`);
});

test('a client cannot send host-only control messages', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  for (const type of [GAME.STARTGAME, GAME.SENDGAMEINFO, GAME.COOP_MISSION]) {
    guest.client.send(protocol.encodeClientRelay({
      gameMessageType: type, payload: protocol.gamePayload(type, Buffer.alloc(4)),
    }));
    const err = await guest.client.expect(S2C.ERROR);
    assert.equal(err.code, CLOSE.FORBIDDEN, `type ${type} should be host-only`);
  }

  await delay(120);
  assert.equal(host.client.drain().length, 0, 'nothing forged may reach the host');

  host.client.close();
  guest.client.close();
});

test('a client cannot change the room phase', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  guest.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
  assert.equal((await guest.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

  const room = relay.store.rooms.get(host.room);
  assert.equal(room.phase, PHASE.LOBBY);

  host.client.close();
  guest.client.close();
});

test('a host cannot send a client-only message', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);
  host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
  await guest.client.expect(S2C.ROOM_PHASE_CHANGED);

  host.client.send(protocol.encodeClientRelay({
    gameMessageType: GAME.CLIENTSTATS,
    payload: protocol.gamePayload(GAME.CLIENTSTATS, Buffer.alloc(20)),
  }));
  assert.equal((await host.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);

  host.client.close();
  guest.client.close();
});

test('address-bearing and mod-transfer packets are never carried', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  const banned = [
    GAME.CONNECT, GAME.DISCONNECT, GAME.PEER_CONNECTED,
    GAME.MOD_INFO, GAME.MOD_REQUEST, GAME.MOD_CHUNK, GAME.MOD_COMPLETE, GAME.MOD_ACK,
  ];
  for (const type of banned) {
    host.client.send(protocol.encodeClientRelay({
      gameMessageType: type, payload: protocol.gamePayload(type, Buffer.alloc(16)),
    }));
    const err = await host.client.expect(S2C.ERROR);
    assert.equal(err.code, CLOSE.FORBIDDEN, `type ${type} must not be relayed`);
  }

  await delay(120);
  assert.equal(guest.client.drain().length, 0, 'no banned packet may reach another peer');

  host.client.close();
  guest.client.close();
});

test('an unknown game message type is refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  host.client.send(protocol.encodeClientRelay({
    gameMessageType: 4242, payload: protocol.gamePayload(4242),
  }));
  assert.equal((await host.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
  host.client.close();
});

test('a recipient in another room is treated as forgery', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const roomA = await joinAsHost(relay, { name: 'a-host' });
  const roomB = await joinAsHost(relay, { name: 'b-host' });
  const guestB = await joinAsClient(relay, roomB.room, { name: 'b-guest' });
  await roomB.client.expect(S2C.PEER_JOINED);
  await guestB.client.expect(S2C.PEER_JOINED);

  roomA.client.send(protocol.encodeClientRelay({
    recipient: guestB.welcome.peerId,
    gameMessageType: GAME.CHATMESSAGE,
    payload: protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('leak')),
  }));

  assert.equal((await roomA.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
  assert.equal((await roomA.client.waitForClose()).code, CLOSE.FORBIDDEN);

  await delay(150);
  assert.equal(guestB.client.drain().length, 0, 'no cross-room message may be delivered');
  assert.equal(roomB.client.drain().length, 0);

  roomB.client.close();
  guestB.client.close();
});

test('a message addressed to its own sender is refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  host.client.send(protocol.encodeClientRelay({
    recipient: host.welcome.peerId,
    gameMessageType: GAME.CHATMESSAGE,
    payload: protocol.gamePayload(GAME.CHATMESSAGE),
  }));
  assert.equal((await host.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
  host.client.close();
});

test('addressing a peer that just left is a dropped message, not a disconnect', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  guest.client.send(protocol.encodeClientLeave(1));
  await guest.client.waitForClose();
  await host.client.expect(S2C.PEER_LEFT);

  host.client.send(protocol.encodeClientRelay({
    recipient: guest.welcome.peerId,
    gameMessageType: GAME.CHATMESSAGE,
    payload: protocol.gamePayload(GAME.CHATMESSAGE),
  }));
  assert.equal((await host.client.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
  assert.equal(host.client.closeInfo, null, 'the host must stay connected');

  host.client.close();
});

test('two players cannot share one name in a room', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay, { name: 'stefan' });
  const admission = await admitJoin(relay, host.room);
  const twin = await TestClient.connect(relay.socketUrl);
  twin.send(protocol.encodeHello({
    grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL, displayName: 'stefan',
  }));

  assert.equal((await twin.expect(S2C.ERROR)).code, CLOSE.FORBIDDEN);
  assert.equal((await twin.waitForClose()).code, CLOSE.FORBIDDEN);

  await delay(120);
  assert.equal(host.client.drain().length, 0, 'the host must not see a phantom member');

  host.client.close();
});

test('a full room refuses a third peer', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  const third = await admitJoin(relay, host.room);
  assert.equal(third.status, 409);
  assert.equal(third.fields.code, 'room_full');

  host.client.close();
  guest.client.close();
});

test('a grant held while the room fills up cannot oversubscribe it', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  // Take the only client seat as an outstanding grant, then try to get a second.
  const reserved = await admitJoin(relay, host.room);
  assert.equal(reserved.fields.status, 'ok');
  const extra = await admitJoin(relay, host.room);
  assert.equal(extra.fields.code, 'room_full');

  host.client.close();
});

test('the handshake deadline closes a silent connection', async (t) => {
  const relay = await startRelay({ handshakeTimeoutMs: 150 });
  t.after(() => relay.stop());

  const client = await TestClient.connect(relay.socketUrl);
  const info = await client.waitForClose(4000);
  assert.equal(info.code, CLOSE.TIMEOUT);
});

test('the liveness deadline closes a stalled player instead of stalling the match', async (t) => {
  const relay = await startRelay({ handshakeTimeoutMs: 900, livenessTimeoutMs: 800 });
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  // The host keeps heartbeating, the way a running game does. The guest goes silent, which is
  // what a suspended browser tab looks like: the ws client answers pings by itself, so that has
  // to be suppressed too for it to be genuinely wedged.
  guest.client.ws.pong = () => {};
  guest.client.ws._receiver.removeAllListeners('ping');

  const heartbeat = setInterval(() => {
    if (host.client.closeInfo === null) {
      try { host.client.send(protocol.encodeClientHeartbeat(Date.now() >>> 0)); } catch { /* gone */ }
    }
  }, 150);
  t.after(() => clearInterval(heartbeat));

  let closed = null;
  while (closed === null) {
    const msg = await host.client.next(6000);
    if (msg.type !== S2C.HEARTBEAT_ACK) {
      assert.equal(msg.type, S2C.PEER_LEFT);
      closed = msg;
    }
  }
  assert.equal(closed.peerId, guest.welcome.peerId);
  assert.equal(closed.reason, LEAVE_REASON.TIMEOUT);
  assert.equal((await guest.client.waitForClose(5000)).code, CLOSE.TIMEOUT);

  host.client.close();
  guest.client.close();
});

test('a message flood is disconnected rather than amplified', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const frame = protocol.encodeClientHeartbeat(1);
  for (let i = 0; i < LIMITS.MESSAGES_PER_SECOND + 64; i += 1) {
    if (host.client.closeInfo !== null) break;
    try { host.client.send(frame); } catch { break; }
  }
  await host.client.waitForClose(5000);

  // The close code reaches a well-behaved client, but a client that is still writing when the
  // relay hangs up can see a transport-level abort instead. The relay's own record is the
  // deterministic assertion.
  const left = relay.log.events('participant_left');
  assert.equal(left.length, 1);
  assert.equal(left[0].reasonCode, LEAVE_REASON.RATE_LIMITED);
  assert.ok(host.client.closeInfo.code === CLOSE.RATE_LIMITED || host.client.closeInfo.code === 1006);
});

test('repeated refused messages eventually close the connection', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const banned = protocol.encodeClientRelay({
    gameMessageType: GAME.CONNECT,
    payload: protocol.gamePayload(GAME.CONNECT, Buffer.alloc(8)),
  });
  for (let i = 0; i < LIMITS.MAX_SOFT_ERRORS + 4; i += 1) {
    if (host.client.closeInfo !== null) break;
    try { host.client.send(banned); } catch { break; }
  }
  await host.client.waitForClose(5000);

  const refusals = relay.log.events('message_refused');
  assert.ok(refusals.length >= LIMITS.MAX_SOFT_ERRORS);
  const left = relay.log.events('participant_left');
  assert.equal(left[0].reasonCode, LEAVE_REASON.PROTOCOL_ERROR);
  assert.ok(host.client.closeInfo.code === CLOSE.PROTOCOL_ERROR || host.client.closeInfo.code === 1006);
});

test('a slow consumer is disconnected instead of having its commands dropped', async (t) => {
  // A smaller backpressure budget keeps the test honest without needing to push megabytes
  // through the loopback socket buffers; the production value is in LIMITS.
  const relay = await startRelay({ backpressureBytes: 32 * 1024 });
  t.after(() => relay.stop());

  const host = await joinAsHost(relay);
  const guest = await joinAsClient(relay, host.room);
  await host.client.expect(S2C.PEER_JOINED);
  await guest.client.expect(S2C.PEER_JOINED);

  // Stop reading on the guest side, then push data at it until the relay notices.
  guest.client.ws._socket.pause();

  const body = Buffer.alloc(120000, 3);
  const payload = protocol.gamePayload(GAME.SENDGAMEINFO, body);
  const frame = protocol.encodeClientRelay({ gameMessageType: GAME.SENDGAMEINFO, payload });

  for (let i = 0; i < 40; i += 1) {
    if (host.client.closeInfo !== null) break;
    host.client.send(frame);
    // Stay inside the sender's own byte budget while filling the recipient's buffer.
    await delay(120);
    const room = relay.store.rooms.get(host.room);
    if (room === undefined || room.peers.size < 2) break;
  }

  await delay(200);
  const room = relay.store.rooms.get(host.room);
  assert.ok(room === undefined || room.peers.size < 2,
    'a recipient that stopped reading must be disconnected');

  // And it must be disconnected for that reason, not for some unrelated failure.
  const left = relay.log.events('participant_left');
  assert.equal(left.length, 1);
  assert.equal(left[0].peerId, guest.welcome.peerId);
  assert.equal(left[0].reasonCode, LEAVE_REASON.SLOW_CONSUMER);
  assert.equal(host.client.closeInfo, null, 'the sender must stay connected');

  host.client.close();
  guest.client.close();
});

test('hosting and joining races do not leave a room without a host record', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  const join = await admitJoin(relay, admission.fields.room);

  // The client redeems its grant before the host ever connects.
  const guest = await TestClient.connect(relay.socketUrl);
  guest.send(protocol.encodeHello({
    grant: join.fields.grant, gameProtocol: GAME_PROTOCOL, displayName: 'early',
  }));
  const guestWelcome = await guest.expect(S2C.WELCOME);
  assert.equal(guestWelcome.role, 2);

  const host = await TestClient.connect(relay.socketUrl);
  host.send(protocol.encodeHello({
    grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL, displayName: 'late-host',
  }));
  const hostWelcome = await host.expect(S2C.WELCOME);
  assert.equal(hostWelcome.role, 1);

  // Each side still sees the other exactly once, with the right role.
  assert.equal((await guest.expect(S2C.PEER_JOINED)).role, 1);
  assert.equal((await host.expect(S2C.PEER_JOINED)).role, 2);

  const room = relay.store.rooms.get(admission.fields.room);
  assert.equal(room.hostPeerId, hostWelcome.peerId);

  host.close();
  guest.close();
});

test('a mismatched game protocol in the handshake is refused at the socket too', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const admission = await admitHost(relay);
  const client = await TestClient.connect(relay.socketUrl);
  client.send(protocol.encodeHello({
    grant: admission.fields.grant, gameProtocol: GAME_PROTOCOL + 1,
  }));
  assert.equal((await client.expect(S2C.ERROR)).code, CLOSE.VERSION_MISMATCH);
  client.close();
});

test('an unknown socket path is not upgraded', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  await assert.rejects(
    () => TestClient.connect(`ws://127.0.0.1:${relay.port}/v1/admin`),
    (err) => err.statusCode === 404,
  );
});

test('opening sockets from one address is rate limited', async (t) => {
  const relay = await startRelay({ socketsPerAddressPerMinute: 3 });
  t.after(() => relay.stop());

  const opened = [];
  t.after(() => { for (const client of opened) client.close(); });

  for (let i = 0; i < 3; i += 1) {
    opened.push(await TestClient.connect(relay.socketUrl));
  }
  await assert.rejects(
    () => TestClient.connect(relay.socketUrl),
    (err) => err.statusCode === 429,
  );

  const denied = relay.log.events('connection_denied');
  assert.equal(denied.length, 1);
  assert.equal(denied[0].code, CLOSE.RATE_LIMITED);
});

test('the connection cap refuses new sockets', async (t) => {
  const relay = await startRelay({ maxConnections: 2 });
  t.after(() => relay.stop());

  const a = await TestClient.connect(relay.socketUrl);
  const b = await TestClient.connect(relay.socketUrl);
  await assert.rejects(
    () => TestClient.connect(relay.socketUrl),
    (err) => err.statusCode === 503,
  );
  a.close();
  b.close();
});
