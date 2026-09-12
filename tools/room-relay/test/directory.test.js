'use strict';

const assert = require('node:assert/strict');
const { describe, it } = require('node:test');
const { startRelay, joinAsHost, admitHost, admitJoin, postForm,
  GAME_PROTOCOL, CONTENT_HASH } = require('./helpers');
const { RoomStore, PHASE } = require('../src/rooms');

const fields = { app: 'dunecity', appVersion: '1.0.655', runtime: 'browser',
  gameProtocol: GAME_PROTOCOL, contentHash: CONTENT_HASH };
const list = (relay, extra = {}, headers = {}) =>
  postForm(relay, '/v1/admission/list', { ...fields, ...extra }, headers);

describe('public room directory', () => {
  it('lists only opted-in connected hosts, without leaking private codes or grants', async () => {
    const relay = await startRelay();
    try {
      const privateHost = await joinAsHost(relay);
      const publicHost = await joinAsHost(relay, { name: 'Alice|=Host',
        admission: { visibility: 'public', mode: 'custom', maxPeers: 4 } });
      const absentHost = await admitHost(relay, { visibility: 'public' });
      const response = await list(relay);
      assert.equal(response.status, 200);
      assert.match(response.text, new RegExp(`game=${publicHost.room}\\|1\\|4\\|custom\\|`));
      assert.ok(response.text.includes(Buffer.from('Alice|=Host').toString('hex')));
      for (const secret of [privateHost.room, privateHost.admission.fields.grant,
        publicHost.admission.fields.grant, absentHost.fields.room, absentHost.fields.grant]) {
        assert.ok(!response.text.includes(secret));
      }
      assert.equal(response.headers['cache-control'], 'no-store');
      assert.equal((await list(relay, { contentHash: 'b'.repeat(16) })).text.includes('game='), false);
      assert.equal((await list(relay, { gameProtocol: GAME_PROTOCOL + 1 })).text.includes('game='), false);
      privateHost.client.close(); publicHost.client.close();
    } finally { await relay.stop(); }
  });

  it('hides reserved/full, started and closed games and does not issue grants for listing', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, { admission: { visibility: 'public' } });
      const room = relay.store.rooms.get(host.room);
      const initialGrants = relay.store.grants.size;
      assert.ok((await list(relay)).text.includes(host.room));
      assert.equal(relay.store.grants.size, initialGrants);
      const invitation = await admitJoin(relay, host.room);
      assert.equal(invitation.status, 200);
      assert.equal((await list(relay)).text.includes(host.room), false);
      relay.store.consumeGrant(invitation.fields.grant);
      assert.ok((await list(relay)).text.includes(host.room));
      relay.store.setRoomPhase(room, PHASE.MATCH);
      assert.equal((await list(relay)).text.includes(host.room), false);
      relay.store.setRoomPhase(room, PHASE.LOBBY);
      assert.equal((await list(relay)).text.includes(host.room), false);
      relay.store.closeRoom(room, 'host_left');
      assert.equal((await list(relay)).text.includes(host.room), false);
      host.client.close();
    } finally { await relay.stop(); }
  });

  it('keeps pagination bounded and private rooms outside the page offsets', () => {
    const store = new RoomStore();
    for (let i = 0; i < 15; i++) {
      const { room, grant } = store.createRoom({ ...fields, mode: 'custom', maxPeers: 4,
        visibility: i === 0 ? 'private' : 'public' });
      store.consumeGrant(grant);
      room.hostPeerId = i + 1;
      room.peers.set(i + 1, { displayName: `Host ${i}` });
    }
    const first = store.listPublicRooms(fields);
    const second = store.listPublicRooms(fields, first.next);
    assert.equal(first.games.length, 12); assert.equal(first.next, 12);
    assert.equal(second.games.length, 2); assert.equal(second.next, 0);
    assert.equal(new Set([...first.games, ...second.games].map(game => game.code)).size, 14);
  });

  it('enforces input, CORS and rate bounds on listing', async () => {
    const relay = await startRelay({ allowedOrigins: ['http://localhost:8766'] });
    try {
      assert.equal((await list(relay, {}, { Origin: 'https://foreign.example' })).status, 403);
      const allowed = await list(relay, {}, { Origin: 'http://localhost:8766' });
      assert.equal(allowed.headers['access-control-allow-origin'], 'http://localhost:8766');
      assert.equal((await list(relay, { offset: '-1' })).status, 400);
      assert.equal((await list(relay, { offset: '99999999' })).status, 400);
      assert.equal((await admitHost(relay, { visibility: 'friends' })).status, 400);
      let limited = false;
      for (let i = 0; i < 100; i++) {
        if ((await list(relay)).status === 429) { limited = true; break; }
      }
      assert.ok(limited);
    } finally { await relay.stop(); }
  });
});
