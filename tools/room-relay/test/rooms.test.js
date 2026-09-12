'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const { RoomStore, AdmissionError, normalizeRoomCode, generateRoomCode } = require('../src/rooms');
const { ROLE, PHASE, LIMITS, ROOM_CODE_ALPHABET } = require('../src/constants');

function makeStore(clock) {
  return new RoomStore({ now: () => clock.t });
}

const ROOM_SPEC = {
  maxPeers: 2, mode: 'coop', gameProtocol: 5, contentHash: 'abc123', appVersion: '1.0.655',
};

test('room codes use the documented alphabet and shape', () => {
  for (let i = 0; i < 50; i += 1) {
    const code = generateRoomCode();
    assert.match(code, /^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$/);
    for (const ch of code.replace(/-/g, '')) {
      assert.ok(ROOM_CODE_ALPHABET.includes(ch), `${ch} is not in the alphabet`);
    }
  }
});

test('room codes are drawn from a wide space', () => {
  const seen = new Set();
  for (let i = 0; i < 500; i += 1) seen.add(generateRoomCode());
  assert.equal(seen.size, 500, 'generated codes collided, which 60 bits should not do');
});

test('room code input is normalised but not loosened', () => {
  const code = generateRoomCode();
  const compact = code.replace(/-/g, '');
  assert.equal(normalizeRoomCode(code), code);
  assert.equal(normalizeRoomCode(compact), code);
  assert.equal(normalizeRoomCode(compact.toLowerCase()), code);
  assert.equal(normalizeRoomCode(''), null);
  assert.equal(normalizeRoomCode(`${compact}X`), null);
  assert.equal(normalizeRoomCode('IIII-IIII-IIII'), null, 'I is not in the alphabet');
  assert.equal(normalizeRoomCode('x'.repeat(64)), null);
  assert.equal(normalizeRoomCode(null), null);
});

test('a grant admits exactly once', () => {
  const clock = { t: 1000 };
  const store = makeStore(clock);
  const { room, grant } = store.createRoom(ROOM_SPEC);

  const first = store.consumeGrant(grant);
  assert.ok(first);
  assert.equal(first.role, ROLE.HOST);
  assert.equal(first.room.code, room.code);

  assert.equal(store.consumeGrant(grant), null, 'a replayed grant must not admit');
});

test('an expired grant does not admit and stops holding a seat', () => {
  const clock = { t: 1000 };
  const store = makeStore(clock);
  const { room, grant } = store.createRoom(ROOM_SPEC);
  assert.equal(room.reservedSeats, 1);

  clock.t += LIMITS.GRANT_TTL_MS + 1;
  assert.equal(store.consumeGrant(grant), null);

  store.sweep();
  assert.equal(room.outstandingGrants, 0);
});

test('outstanding grants count against the room capacity', () => {
  const clock = { t: 1000 };
  const store = makeStore(clock);
  const { room } = store.createRoom(ROOM_SPEC);
  store.joinRoom(room.code, { gameProtocol: 5, contentHash: 'abc123' });

  assert.equal(room.reservedSeats, 2);
  assert.throws(
    () => store.joinRoom(room.code, { gameProtocol: 5, contentHash: 'abc123' }),
    (err) => err instanceof AdmissionError && err.code === 'room_full',
  );
});

test('joining refuses a different game protocol or content fingerprint', () => {
  const clock = { t: 1000 };
  const store = makeStore(clock);
  const { room } = store.createRoom(ROOM_SPEC);

  assert.throws(
    () => store.joinRoom(room.code, { gameProtocol: 4, contentHash: 'abc123' }),
    (err) => err.code === 'content_mismatch',
  );
  assert.throws(
    () => store.joinRoom(room.code, { gameProtocol: 5, contentHash: 'deadbeef' }),
    (err) => err.code === 'content_mismatch',
  );
});

test('an unknown room code is not found, and a malformed one is a bad request', () => {
  const store = makeStore({ t: 0 });
  assert.throws(
    () => store.joinRoom('ZZZZ-ZZZZ-ZZZZ', { gameProtocol: 5, contentHash: '' }),
    (err) => err.code === 'room_not_found',
  );
  assert.throws(
    () => store.joinRoom('nope', { gameProtocol: 5, contentHash: '' }),
    (err) => err.code === 'bad_request',
  );
});

test('an empty room is reaped, and its code stops working', () => {
  const clock = { t: 1000 };
  const store = makeStore(clock);
  const { room, grant } = store.createRoom(ROOM_SPEC);
  store.consumeGrant(grant);

  assert.equal(store.roomCount, 1);
  clock.t += LIMITS.EMPTY_ROOM_TTL_MS + 1;
  store.sweep();
  assert.equal(store.roomCount, 0);
  assert.equal(room.closed, true);
  assert.throws(() => store.joinRoom(room.code, { gameProtocol: 5, contentHash: 'abc123' }),
    (err) => err.code === 'room_not_found');
});

test('the room limit is enforced', () => {
  const clock = { t: 1000 };
  const store = new RoomStore({ now: () => clock.t, maxRooms: 3 });
  for (let i = 0; i < 3; i += 1) store.createRoom(ROOM_SPEC);
  assert.throws(() => store.createRoom(ROOM_SPEC), (err) => err.code === 'capacity');
});

test('peer ids never repeat and never take the broadcast value', () => {
  const store = makeStore({ t: 0 });
  const seen = new Set();
  for (let i = 0; i < 1000; i += 1) {
    const id = store.allocatePeerId();
    assert.notEqual(id, 0);
    assert.ok(!seen.has(id));
    seen.add(id);
  }
});

test('a new room starts in the lobby phase', () => {
  const store = makeStore({ t: 0 });
  const { room } = store.createRoom(ROOM_SPEC);
  assert.equal(room.phase, PHASE.LOBBY);
});
