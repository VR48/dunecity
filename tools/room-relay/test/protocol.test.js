'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const protocol = require('../src/protocol');
const {
  C2S, S2C, CLOSE, LIMITS, GAME, PHASE, ROLE, DIAGNOSTIC_KIND, RELAY_PROTOCOL_VERSION,
} = require('../src/constants');

function expectProtocolError(fn, closeCode) {
  try {
    fn();
  } catch (err) {
    assert.equal(err.name, 'ProtocolError', `expected a ProtocolError, got ${err}`);
    if (closeCode !== undefined) assert.equal(err.closeCode, closeCode);
    return err;
  }
  assert.fail('expected the frame to be refused');
}

test('HELLO round trips and keeps every field', () => {
  const frame = protocol.encodeHello({
    grant: 'ab12'.repeat(16),
    gameProtocol: 5,
    runtime: 'browser',
    appVersion: '1.0.655',
    contentHash: 'deadbeef',
    displayName: 'Stefan',
  });
  const msg = protocol.decodeClientMessage(frame);
  assert.equal(msg.type, C2S.HELLO);
  assert.equal(msg.relayVersion, RELAY_PROTOCOL_VERSION);
  assert.equal(msg.gameProtocol, 5);
  assert.equal(msg.grant.length, 64);
  assert.equal(msg.runtime, 'browser');
  assert.equal(msg.contentHash, 'deadbeef');
  assert.equal(msg.displayName, 'Stefan');
});

test('HELLO refuses a control character in the display name', () => {
  const frame = protocol.encodeHello({ grant: 'ab'.repeat(32), displayName: 'ev\x07il' });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('HELLO refuses a runtime that is not native or browser', () => {
  const frame = protocol.encodeHello({ grant: 'ab'.repeat(32), runtime: 'server' });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('HELLO refuses a non-hex grant', () => {
  const frame = protocol.encodeHello({ grant: '../../etc/passwd' });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('HELLO with another relay protocol version is a version mismatch, not a parse error', () => {
  const frame = protocol.encodeHello({ grant: 'ab'.repeat(32), relayVersion: 99 });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.VERSION_MISMATCH);
});

test('RELAY round trips and preserves the game payload byte for byte', () => {
  const payload = protocol.gamePayload(GAME.COMMANDLIST, Buffer.from([1, 2, 3, 250, 255]));
  const frame = protocol.encodeClientRelay({
    recipient: 7, channel: 1, flags: 0, gameMessageType: GAME.COMMANDLIST, payload,
  });
  const msg = protocol.decodeClientMessage(frame);
  assert.equal(msg.recipient, 7);
  assert.equal(msg.channel, 1);
  assert.equal(msg.gameMessageType, GAME.COMMANDLIST);
  assert.deepEqual(msg.payload, payload);

  const routed = protocol.decodeServerMessage(protocol.encodeRelay(42, msg));
  assert.equal(routed.type, S2C.RELAY);
  assert.equal(routed.senderPeerId, 42);
  assert.deepEqual(routed.payload, payload);
});

test('RELAY refuses a declared type that disagrees with the payload header', () => {
  const payload = protocol.gamePayload(GAME.COMMANDLIST);
  const frame = protocol.encodeClientRelay({ gameMessageType: GAME.STARTGAME, payload });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('RELAY refuses a channel the transport does not have', () => {
  const payload = protocol.gamePayload(GAME.CHATMESSAGE);
  const frame = protocol.encodeClientRelay({ channel: 9, gameMessageType: GAME.CHATMESSAGE, payload });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('a payload length larger than the frame is refused before anything is allocated', () => {
  // 0xffffffff would wrap an additive bounds check on wasm32 and would be a 4 GiB allocation
  // if it were believed. The decoder must reject it from the declared length alone.
  const frame = Buffer.alloc(1 + 4 + 1 + 1 + 2 + 4 + 4);
  let pos = 0;
  frame.writeUInt8(C2S.RELAY, pos); pos += 1;
  frame.writeUInt32BE(0, pos); pos += 4;
  frame.writeUInt8(0, pos); pos += 1;
  frame.writeUInt8(0, pos); pos += 1;
  frame.writeUInt16BE(GAME.CHATMESSAGE, pos); pos += 2;
  frame.writeUInt32BE(0xffffffff, pos); pos += 4;
  frame.writeUInt32LE(GAME.CHATMESSAGE, pos);
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('a payload shorter than its own four-byte game header is refused', () => {
  const frame = protocol.encodeClientRelay({
    gameMessageType: GAME.CHATMESSAGE, payload: Buffer.from([1, 2, 3]),
  });
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('trailing bytes after the declared fields are refused rather than ignored', () => {
  const good = protocol.encodeClientHeartbeat(1234);
  const padded = Buffer.concat([good, Buffer.from([0, 0])]);
  expectProtocolError(() => protocol.decodeClientMessage(padded), CLOSE.PROTOCOL_ERROR);
});

test('a truncated frame is refused', () => {
  const good = protocol.encodeHello({ grant: 'ab'.repeat(32) });
  for (let cut = 1; cut < good.length; cut += 1) {
    expectProtocolError(() => protocol.decodeClientMessage(good.subarray(0, cut)));
  }
});

test('an empty frame is refused', () => {
  expectProtocolError(() => protocol.decodeClientMessage(Buffer.alloc(0)), CLOSE.PROTOCOL_ERROR);
});

test('a frame above the transport limit is reported as too large', () => {
  const huge = Buffer.alloc(LIMITS.MAX_FRAME_BYTES + 1);
  huge.writeUInt8(C2S.HEARTBEAT, 0);
  expectProtocolError(() => protocol.decodeClientMessage(huge), CLOSE.TOO_LARGE);
});

test('a server message id sent by a client is refused', () => {
  const frame = Buffer.from([S2C.WELCOME, 0, 0]);
  expectProtocolError(() => protocol.decodeClientMessage(frame), CLOSE.PROTOCOL_ERROR);
});

test('ROOM_PHASE only accepts the two defined phases', () => {
  assert.equal(protocol.decodeClientMessage(protocol.encodeClientRoomPhase(PHASE.MATCH)).phase,
    PHASE.MATCH);
  expectProtocolError(() => protocol.decodeClientMessage(protocol.encodeClientRoomPhase(7)));
});

test('DIAGNOSTIC is bounded and only accepts defined kinds', () => {
  const ok = protocol.encodeClientDiagnostic(DIAGNOSTIC_KIND.STATE_DIGEST, Buffer.alloc(32, 9));
  assert.equal(protocol.decodeClientMessage(ok).payload.length, 32);

  const badKind = protocol.encodeClientDiagnostic(77, Buffer.alloc(4));
  expectProtocolError(() => protocol.decodeClientMessage(badKind));

  const tooBig = protocol.encodeClientDiagnostic(
    DIAGNOSTIC_KIND.STATE_DIGEST, Buffer.alloc(LIMITS.MAX_DIAGNOSTIC_BYTES + 1));
  expectProtocolError(() => protocol.decodeClientMessage(tooBig));
});

test('server-side encoders produce frames the decoder accepts', () => {
  const welcome = protocol.decodeServerMessage(protocol.encodeWelcome({
    gameProtocol: 5, peerId: 3, role: ROLE.HOST, roomCode: 'ABCD-EFGH-JKMN',
    maxPeers: 2, phase: PHASE.LOBBY,
  }));
  assert.equal(welcome.peerId, 3);
  assert.equal(welcome.roomCode, 'ABCD-EFGH-JKMN');
  assert.equal(welcome.maxPeers, 2);

  const joined = protocol.decodeServerMessage(protocol.encodePeerJoined({
    peerId: 9, role: ROLE.CLIENT, displayName: 'guest', runtime: 'browser',
  }));
  assert.equal(joined.peerId, 9);
  assert.equal(joined.runtime, 'browser');

  const left = protocol.decodeServerMessage(protocol.encodePeerLeft(9, 1));
  assert.equal(left.peerId, 9);

  const err = protocol.decodeServerMessage(protocol.encodeError(CLOSE.FORBIDDEN, 'no'));
  assert.equal(err.code, CLOSE.FORBIDDEN);
  assert.equal(err.message, 'no');
});

test('error text is clamped to printable ASCII and to its length limit', () => {
  const nasty = `bad\x00\x1b[31m${'x'.repeat(500)}`;
  const decoded = protocol.decodeServerMessage(protocol.encodeError(CLOSE.PROTOCOL_ERROR, nasty));
  assert.ok(decoded.message.length <= LIMITS.MAX_ERROR_MESSAGE_CHARS);
  assert.ok(protocol.isPrintableMessage(decoded.message));
  assert.ok(!decoded.message.includes('\x1b'));
});
