'use strict';
const assert = require('node:assert/strict');
const { describe, it } = require('node:test');
const { LobbyChat, SESSION_TTL, MAX_HISTORY, MAX_SESSIONS } = require('../src/lobby');
const { startRelay, joinAsHost, admitJoin, postForm, GAME_PROTOCOL, CONTENT_HASH } = require('./helpers');
const { PHASE } = require('../src/rooms');
const fields = { app: 'dunecity', appVersion: '1.0.655', runtime: 'browser',
  gameProtocol: GAME_PROTOCOL, contentHash: CONTENT_HASH };
const hex = text => Buffer.from(text).toString('hex');
const request = (relay, action, extra = {}, headers = {}) => postForm(relay,
  `/v1/lobby/${action}`, { ...fields, ...extra }, headers);

describe('confirmed public lobby chat', () => {
  it('binds messages to the confirmed name across clients and separates incompatible builds', async () => {
    const relay = await startRelay();
    try {
      const alice = await request(relay, 'enter', { name: hex('Álice') });
      const bob = await request(relay, 'enter', { name: hex('Bob') });
      assert.equal(alice.status, 200); assert.equal(bob.status, 200);
      assert.equal((await request(relay, 'say', { text: hex('unconfirmed') })).status, 403);
      const sent = await request(relay, 'say', { session: alice.fields.session,
        name: hex('Bob'), text: hex('Hello | = <b>Arrakis</b>') });
      assert.equal(sent.status, 200);
      const received = await request(relay, 'poll', { session: bob.fields.session, cursor: '0' });
      assert.equal(received.fields.chat, `1|${hex('Álice')}|${hex('Hello | = <b>Arrakis</b>')}`);
      assert.equal(received.headers['cache-control'], 'no-store');
      assert.ok(!received.text.includes(alice.fields.session));
      assert.equal((await request(relay, 'enter', { name: hex('áLICE') })).status, 409);
      assert.equal((await request(relay, 'poll', { session: bob.fields.session, cursor: '0',
        contentHash: 'b'.repeat(16) })).status, 403);
      assert.equal((await request(relay, 'poll', { session: bob.fields.session, cursor: '999' })).status, 400);
    } finally { await relay.stop(); }
  });

  it('bounds messages, Unicode, history, batches, sessions, expiry and sending frequency', () => {
    let now = 100000;
    const lobby = new LobbyChat(() => now);
    const enter = name => Object.fromEntries(lobby.handle('enter', { name: hex(name) }, fields)).session;
    const alice = enter('Alice');
    const say = text => lobby.handle('say', { session: alice, text: hex(text) }, fields);
    for (const text of ['', ' ', 'bad\nname', 'a'.repeat(121), '\u202eevil', '\u0000']) {
      assert.throws(() => say(text));
    }
    assert.throws(() => lobby.handle('say', { session: alice, text: 'c0af' }, fields));
    for (let i = 0; i < 4; i++) say(`message ${i}`);
    assert.throws(() => say('flood'), error => error.code === 'rate_limited');
    for (let i = 0; i < 110; i++) { now += 10001; say(`next ${i}`); }
    assert.ok(lobby.history.length <= MAX_HISTORY);
    const page = lobby.handle('poll', { session: alice, cursor: '0' }, fields);
    assert.equal(page.length, 13);
    assert.equal(page.filter(([key]) => key === 'chat').length, 12);
    now += SESSION_TTL;
    assert.throws(() => say('expired'), error => error.code === 'session_expired');
    enter('Alice');
    for (let i = 1; i < MAX_SESSIONS; i++) enter(`Person ${i}`);
    assert.throws(() => enter('overflow'), error => error.code === 'capacity');
  });

  it('checks origins and polling cannot consume host/join admission budgets', async () => {
    const relay = await startRelay({ allowedOrigins: ['http://localhost:8766'] });
    try {
      assert.equal((await request(relay, 'enter', { name: hex('Alice') },
        { Origin: 'https://foreign.example' })).status, 403);
      const session = (await request(relay, 'enter', { name: hex('Alice') })).fields.session;
      for (let i = 0; i < 90; i++) {
        assert.equal((await request(relay, 'poll', { session, cursor: '0' })).status, 200);
      }
      assert.equal((await request(relay, 'poll', { session, cursor: '0' })).status, 429);
      const host = await joinAsHost(relay);
      assert.equal((await admitJoin(relay, host.room)).status, 200);
      host.client.close();
    } finally { await relay.stop(); }
  });
});

describe('host visibility control', () => {
  it('lets only a live host change visibility before play; stale public listings cannot join private rooms', async () => {
    const relay = await startRelay();
    try {
      const host = await joinAsHost(relay, { admission: { visibility: 'public', mode: 'custom', maxPeers: 4 } });
      const control = host.admission.fields.control;
      const change = (token, visibility) => postForm(relay, '/v1/admission/visibility',
        { ...fields, room: host.room, control: token, visibility });
      assert.equal((await change('0'.repeat(64), 'private')).status, 403);
      assert.equal((await change(control, 'private')).fields.visibility, 'private');
      assert.equal(relay.store.listPublicRooms(fields).games.length, 0);
      assert.equal((await admitJoin(relay, host.room, { publicOnly: '1' })).status, 404);
      const invitation = await admitJoin(relay, host.room);
      assert.equal(invitation.status, 200);
      assert.equal(invitation.fields.control, undefined);
      assert.equal((await change(control, 'public')).fields.visibility, 'public');
      assert.equal(relay.store.listPublicRooms(fields).games.length, 1);
      relay.store.setRoomPhase(relay.store.rooms.get(host.room), PHASE.MATCH);
      assert.equal((await change(control, 'private')).status, 409);
      host.client.close();
    } finally { await relay.stop(); }
  });
});
