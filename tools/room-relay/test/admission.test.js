'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const { startRelay, postForm, admitHost, admitJoin, GAME_PROTOCOL, CONTENT_HASH } = require('./helpers');
const { LIMITS, RELAY_PROTOCOL_VERSION } = require('../src/constants');

test('hosting returns a room code, a single-use grant and the socket URL', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const res = await admitHost(relay);
  assert.equal(res.status, 200);
  assert.equal(res.fields.status, 'ok');
  assert.equal(res.fields.protocol, String(RELAY_PROTOCOL_VERSION));
  assert.match(res.fields.room, /^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$/);
  assert.match(res.fields.grant, /^[0-9a-f]{64}$/);
  assert.equal(res.fields.maxPeers, '2');
  assert.ok(res.fields.url.length > 0);
  // The response must stay inside the bounds the C++ parser relies on.
  assert.ok(res.text.length <= 8192);
  assert.ok(res.text.split('\n').length <= 17);
});

test('joining an open room issues a distinct grant', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await admitHost(relay);
  const guest = await admitJoin(relay, host.fields.room);
  assert.equal(guest.fields.status, 'ok');
  assert.equal(guest.fields.room, host.fields.room);
  assert.notEqual(guest.fields.grant, host.fields.grant);
});

test('a room code is accepted without dashes and in lower case', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await admitHost(relay);
  const compact = host.fields.room.replace(/-/g, '').toLowerCase();
  const guest = await admitJoin(relay, compact);
  assert.equal(guest.fields.status, 'ok');
  assert.equal(guest.fields.room, host.fields.room);
});

test('an unknown room code is refused without revealing anything else', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const res = await admitJoin(relay, 'ZZZZ-ZZZZ-ZZZZ');
  assert.equal(res.status, 404);
  assert.equal(res.fields.status, 'error');
  assert.equal(res.fields.code, 'room_not_found');
  assert.ok(res.fields.message.length <= 200);
});

test('a mismatched content fingerprint is refused before any socket is opened', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await admitHost(relay);
  const res = await admitJoin(relay, host.fields.room, { contentHash: 'b'.repeat(16) });
  assert.equal(res.status, 409);
  assert.equal(res.fields.code, 'content_mismatch');
});

test('required fields are validated strictly', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const cases = [
    { app: 'not-dunecity' },
    { appVersion: 'x'.repeat(64) },
    { appVersion: 'has space' },
    { gameProtocol: 'abc' },
    { gameProtocol: '99999999' },
    { contentHash: 'NOTHEX' },
    { runtime: 'server' },
    { maxPeers: '1' },
    { maxPeers: '99' },
    { mode: 'ranked' },
  ];
  for (const override of cases) {
    const res = await admitHost(relay, override);
    assert.notEqual(res.fields.status, 'ok', `${JSON.stringify(override)} should be refused`);
  }
});

test('a body above the size limit is refused rather than truncated', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const res = await fetch(`${relay.baseUrl}/v1/admission/host`, {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: `app=dunecity&pad=${'x'.repeat(LIMITS.HTTP_MAX_BODY_BYTES + 100)}`,
  });
  assert.equal(res.status, 413);
});

test('prototype-polluting form keys are ignored', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const res = await admitHost(relay, { __proto__: 'polluted' });
  assert.equal(res.fields.status, 'ok');
  assert.equal({}.polluted, undefined);
});

test('an unknown endpoint and an unknown method are refused', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const unknown = await postForm(relay, '/v1/admin/shutdown', {});
  assert.equal(unknown.status, 404);

  const wrongMethod = await fetch(`${relay.baseUrl}/v1/admission/host`, { method: 'GET' });
  assert.equal(wrongMethod.status, 404);
});

test('health reports status without leaking room codes', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  const host = await admitHost(relay);
  const res = await fetch(`${relay.baseUrl}/v1/health`);
  const text = await res.text();
  assert.equal(res.status, 200);
  assert.match(text, /status=ok/);
  assert.match(text, /rooms=1/);
  assert.ok(!text.includes(host.fields.room));
});

test('an origin outside the allowlist is refused, and an allowed one is not', async (t) => {
  const relay = await startRelay({ allowedOrigins: ['https://dunecity.example'] });
  t.after(() => relay.stop());

  const foreign = await admitHost(relay, { __headers: { origin: 'https://evil.example' } });
  assert.equal(foreign.status, 403);
  assert.equal(foreign.fields.code, 'forbidden_origin');

  const nullOrigin = await admitHost(relay, { __headers: { origin: 'null' } });
  assert.equal(nullOrigin.status, 403);

  const allowed = await admitHost(relay, { __headers: { origin: 'https://dunecity.example' } });
  assert.equal(allowed.fields.status, 'ok');

  // No Origin header at all is how native clients look; that is accepted, but it is the grant
  // that authenticates, not the absence of a header.
  const native = await admitHost(relay);
  assert.equal(native.fields.status, 'ok');
});

test("a relay cannot be configured to allow the 'null' origin", () => {
  const { createRelay } = require('../src/server');
  assert.throws(() => createRelay({ allowedOrigins: ['null'] }), /not an acceptable Origin/);
});

test('per-address issuance is rate limited', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  let refused = 0;
  for (let i = 0; i < LIMITS.HTTP_PER_ADDRESS_PER_MINUTE + 4; i += 1) {
    const res = await admitHost(relay);
    if (res.status === 429) refused += 1;
  }
  assert.ok(refused >= 4, `expected the address limiter to bite, refused ${refused}`);
});

test('a rejected admission is logged without the grant or the room code', async (t) => {
  const relay = await startRelay();
  t.after(() => relay.stop());

  await admitJoin(relay, 'ZZZZ-ZZZZ-ZZZZ');
  const denied = relay.log.events('admission_denied');
  assert.equal(denied.length, 1);
  assert.equal(denied[0].code, 'room_not_found');
  assert.ok(denied[0].addressTag.length > 0);

  const created = relay.log.events('room_created');
  assert.equal(created.length, 0);

  const serialized = JSON.stringify(relay.log.events());
  assert.ok(!/grant/i.test(serialized), 'no lifecycle event may carry a grant');
});

test('the game protocol can be pinned by configuration', async (t) => {
  const relay = await startRelay({ requiredGameProtocol: GAME_PROTOCOL + 1 });
  t.after(() => relay.stop());

  const res = await admitHost(relay);
  assert.equal(res.status, 409);
  assert.equal(res.fields.code, 'unsupported_version');
  assert.equal(CONTENT_HASH.length, 16);
});
