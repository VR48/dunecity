'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { decodeBatch, PollPeer, MAX_BATCH } = require('../src/polling');
const { startRelay, admitHost, admitJoin, GAME_PROTOCOL, CONTENT_HASH, joinAsClient } = require('./helpers');
const protocol = require('../src/protocol');
const { S2C, GAME, CLOSE } = require('../src/constants');
function batch(sequence, frames = []) {
  const out = Buffer.alloc(12 + frames.reduce((n, f) => n + 4 + f.length, 0));
  out.write('DHP1'); out.writeUInt32LE(sequence, 4); out.writeUInt32LE(frames.length, 8);
  let p = 12;
  for (const frame of frames) { out.writeUInt32LE(frame.length, p); p += 4; frame.copy(out, p); p += frame.length; }
  return out;
}
function response(body) {
  assert.equal(body.toString('ascii', 0, 4), 'DHR1');
  const out = []; let p = 12;
  for (let n = body.readUInt16LE(10); n; --n) {
    const len = body.readUInt32LE(p); p += 4;
    out.push(protocol.decodeServerMessage(body.subarray(p, p + len))); p += len;
  }
  assert.equal(p, body.length); return out;
}
async function request(relay, path, body = Buffer.alloc(0), token, extra = {}) {
  const res = await fetch(relay.baseUrl + '/v1/poll/' + path, { method: 'POST',
    headers: { 'content-type': 'application/octet-stream', ...(token ? { 'x-dune-session': token } : {}), ...extra }, body });
  return { status: res.status, body: Buffer.from(await res.arrayBuffer()) };
}
async function open(relay, extra = {}) {
  const res = await request(relay, 'open', undefined, undefined, extra);
  assert.equal(res.status, 200); assert.match(res.body.toString(), /^[0-9a-f]{64}\n$/);
  return res.body.toString().trim();
}
function hello(grant, runtime, name) {
  return protocol.encodeHello({ grant, gameProtocol: GAME_PROTOCOL, contentHash: CONTENT_HASH, runtime, displayName: name });
}
test('batch parser rejects incomplete, trailing, oversized and noncanonical data', () => {
  const valid = batch(1, [Buffer.from([1, 2])]); assert.equal(decodeBatch(valid).frames.length, 1);
  const cases = [Buffer.alloc(0), batch(0), Buffer.concat([valid, Buffer.from([0])]),
    valid.subarray(0, valid.length - 1), batch(1, [Buffer.alloc(262145)]), batch(1, Array(65).fill(Buffer.from([1])))];
  const magic = Buffer.from(valid); magic[0] |= 0x80; cases.push(magic);
  for (const item of cases) assert.throws(() => decodeBatch(item));
});
test('poll peer closes instead of dropping a continuing stream at queue cap', () => {
  const peer = new PollPeer(); for (let i = 0; i < 5; ++i) peer.send(Buffer.alloc(262144));
  assert.equal(peer.readyState, 3); assert.equal(peer.closeCode, CLOSE.SLOW_CONSUMER);
  assert.ok(peer.bufferedAmount <= MAX_BATCH);
});
test('HTTP peers join public room, exchange ordered messages and retry exactly once', async (t) => {
  const relay = await startRelay({ pollingEnabled: true }); t.after(() => relay.stop());
  const admission = await admitHost(relay, { visibility: 'public' }); const host = await open(relay);
  const h1 = await request(relay, 'exchange', batch(1, [hello(admission.fields.grant, 'native', 'Host')]), host);
  assert.equal(h1.status, 200); assert.equal(response(h1.body)[0].type, S2C.WELCOME);
  const ga = await admitJoin(relay, admission.fields.room, { publicOnly: '1' }); const guest = await open(relay);
  const g1 = await request(relay, 'exchange', batch(1, [hello(ga.fields.grant, 'browser', 'Guest')]), guest);
  assert.equal(g1.status, 200); assert.equal(response(g1.body)[0].type, S2C.WELCOME);
  const messages = [0, 1, 2].map(i => protocol.encodeClientRelay({ gameMessageType: GAME.CHATMESSAGE,
    payload: protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('chat' + i)) }));
  const bytes = batch(2, messages); const h2 = await request(relay, 'exchange', bytes, host);
  const retry = await request(relay, 'exchange', bytes, host);
  assert.equal(retry.status, 200); assert.deepEqual(retry.body, h2.body);
  assert.equal((await request(relay, 'exchange', batch(2), host)).status, 409);
  const g2 = await request(relay, 'exchange', batch(2), guest);
  const received = response(g2.body).filter(m => m.type === S2C.RELAY); assert.equal(received.length, 3);
  received.forEach((m, i) => assert.equal(m.payload.subarray(4).toString(), 'chat' + i));
  assert.deepEqual((await request(relay, 'exchange', batch(2), guest)).body, g2.body);
  assert.equal(response((await request(relay, 'exchange', batch(3), guest)).body).length, 0);
  assert.equal((await request(relay, 'exchange', batch(4), host)).status, 409);
});
test('polling and WebSocket clients share membership and routing', async (t) => {
  const relay = await startRelay({ pollingEnabled: true }); t.after(() => relay.stop());
  const admission = await admitHost(relay); const host = await open(relay);
  await request(relay, 'exchange', batch(1, [hello(admission.fields.grant, 'native', 'Host')]), host);
  const guest = await joinAsClient(relay, admission.fields.room); t.after(() => guest.client.close());
  await guest.client.expect(S2C.PEER_JOINED);
  const bytes = protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('mixed'));
  await request(relay, 'exchange', batch(2, [protocol.encodeClientRelay({ gameMessageType: GAME.CHATMESSAGE, payload: bytes })]), host);
  assert.deepEqual((await guest.client.expect(S2C.RELAY)).payload, bytes);
  await request(relay, 'close', undefined, host);
  assert.equal((await guest.client.waitForClose()).code, CLOSE.HOST_LEFT);
});
test('sessions reject foreign origins, missing tokens, concurrent requests and cap', async (t) => {
  const relay = await startRelay({ pollingEnabled: true, allowedOrigins: ['https://dunelegacy.com'], maxPollingSessions: 1 });
  t.after(() => relay.stop());
  assert.equal((await request(relay, 'open', undefined, undefined, { origin: 'https://evil.example' })).status, 403);
  const token = await open(relay, { origin: 'https://dunelegacy.com' });
  assert.equal((await request(relay, 'open')).status, 503);
  assert.equal((await request(relay, 'exchange', batch(1), token)).status, 403);
  assert.equal((await request(relay, 'exchange', batch(1), 'a'.repeat(64))).status, 403);
  const first = request(relay, 'exchange', batch(1), token, { origin: 'https://dunelegacy.com' });
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.equal((await request(relay, 'exchange', batch(1), token, { origin: 'https://dunelegacy.com' })).status, 409);
  assert.equal((await first).status, 200);
});
test('gateway authentication refuses malformed header without crashing', async (t) => {
  const key = 'a'.repeat(64); const relay = await startRelay({ pollingEnabled: true, gatewayKey: key }); t.after(() => relay.stop());
  assert.equal((await request(relay, 'open')).status, 403);
  assert.equal((await request(relay, 'open', undefined, undefined, { 'x-dune-gateway': '\u00e9'.repeat(64) })).status, 403);
  assert.equal((await request(relay, 'open', undefined, undefined, { 'x-dune-gateway': key })).status, 200);
});

test('closed session tombstone expires despite repeated authenticated retries', async (t) => {
  let clock = Date.now();
  const relay = await startRelay({ pollingEnabled: true, maxPollingSessions: 1, now: () => clock });
  t.after(() => relay.stop());
  const admission = await admitHost(relay);
  const token = await open(relay);
  await request(relay, 'exchange', batch(1, [hello(admission.fields.grant, 'native', 'Host')]), token);
  const session = relay.polling.sessions.get(token);
  session.peer.close(CLOSE.TIMEOUT);
  const bytes = batch(2);
  assert.equal((await request(relay, 'exchange', bytes, token)).status, 200);
  clock += 20000;
  assert.equal((await request(relay, 'exchange', bytes, token)).status, 200);
  clock += 6000;
  await new Promise(resolve => setTimeout(resolve, 1050));
  assert.equal(relay.polling.sessions.size, 0);
  assert.equal((await request(relay, 'open')).status, 200);
});

test('failed handshakes release polling capacity immediately on timeout', async (t) => {
  const relay = await startRelay({ pollingEnabled: true, maxPollingSessions: 1, handshakeTimeoutMs: 50 });
  t.after(() => relay.stop());
  await open(relay);
  await new Promise(resolve => setTimeout(resolve, 350));
  assert.equal(relay.polling.sessions.size, 0);
  assert.equal((await request(relay, 'open')).status, 200);
});
test('oversized replay traffic is charged before decoding and stops at the byte budget', async (t) => {
  const relay = await startRelay({ pollingEnabled: true, now: () => 100000 });
  t.after(() => relay.stop());
  const token = await open(relay);
  const malformed = Buffer.alloc(MAX_BATCH);
  assert.equal((await request(relay, 'exchange', malformed, token)).status, 400);
  assert.equal((await request(relay, 'exchange', malformed, token)).status, 400);
  assert.equal((await request(relay, 'exchange', malformed, token)).status, 429);
});
