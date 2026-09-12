'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { LifecycleLog } = require('../src/logging');

test('logging emits only schema fields and never caller-provided free text', () => {
  const records = [];
  const log = new LifecycleLog({ sink: e => records.push(e) });
  const secret = 'ABCD-EFGH-JKLM';
  log.emit('participant_left', { room: secret, peerId: 2, reason: secret,
    grant: secret, payload: secret, runtime: 'browser' });
  assert.equal(records.length, 1);
  assert.equal(records[0].runtime, 'browser');
  assert.ok(!JSON.stringify(records).includes(secret));
  assert.ok(!Object.hasOwn(records[0], 'grant'));
});

test('diagnostic floods have a global bounded emission rate and recover next second', () => {
  let now = 1000;
  let delivered = 0;
  const log = new LifecycleLog({ now: () => now, sink: () => { delivered++; } });
  for (let i = 0; i < 10000; i++) log.emit('connection_denied', { code: 4003 });
  assert.equal(delivered, 100);
  assert.equal(log.events().length, 100);
  assert.equal(log.dropped, 9900);
  now += 1000;
  log.emit('connection_denied', { code: 4003 });
  assert.equal(delivered, 101);
});
