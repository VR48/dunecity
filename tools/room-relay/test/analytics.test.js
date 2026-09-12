'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const https = require('node:https');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { after, before, describe, it } = require('node:test');

const {
  AnalyticsConfigError,
  AnalyticsSchemaError,
  LifecyclePublisher,
  NULL_LIFECYCLE,
  analyticsConfigFromEnv,
  buildLifecycleEvent,
  createLifecycleSink,
  parseDestination,
  serializeEvent,
  signBody,
} = require('../src/analytics');
const { LifecycleLog } = require('../src/logging');
const protocol = require('../src/protocol');
const { GAME, PHASE, S2C } = require('../src/constants');
const {
  GAME_PROTOCOL,
  startRelay,
  joinAsHost,
  joinAsClient,
  delay,
} = require('./helpers');

const KEY = 'k'.repeat(48);

// --- a receiver that records exactly what arrived on the wire ---------------------------------

function okResponder(req, res) {
  res.writeHead(200, { 'content-type': 'application/json' });
  res.end('{"status":"ok"}');
}

/**
 * Local stand-in for the PHP endpoint. It records raw bytes and headers only; it does not
 * pretend to validate like the real receiver does.
 */
async function startReceiver(options = {}) {
  const requests = [];
  const sockets = new Set();
  let responder = options.responder || okResponder;

  const handler = (req, res) => {
    const chunks = [];
    let total = 0;
    req.on('data', (chunk) => { total += chunk.length; chunks.push(chunk); });
    req.on('end', () => {
      const record = {
        method: req.method,
        url: req.url,
        headers: req.headers,
        body: Buffer.concat(chunks, total),
      };
      requests.push(record);
      responder(req, res, record, requests.length);
    });
  };

  const server = options.tls
    ? https.createServer(options.tls, handler)
    : http.createServer(handler);
  server.on('connection', (socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
  });
  server.on('secureConnection', (socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;

  return {
    requests,
    port,
    scheme: options.tls ? 'https' : 'http',
    url: `${options.tls ? 'https' : 'http'}://127.0.0.1:${port}/relay-events.php`,
    setResponder(fn) { responder = fn; },
    bodies() { return requests.map((r) => r.body.toString('utf8')); },
    events() { return requests.map((r) => JSON.parse(r.body.toString('utf8'))); },
    async close() {
      for (const socket of sockets) socket.destroy();
      await new Promise((resolve) => server.close(resolve));
    },
  };
}

function makePublisher(url, overrides = {}) {
  return new LifecyclePublisher({
    destination: new URL(url),
    key: KEY,
    timeoutMs: 500,
    maxAttempts: 3,
    backoffMs: 1,
    maxBackoffMs: 2,
    shutdownTimeoutMs: 1000,
    ...overrides,
  });
}

async function waitFor(predicate, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await delay(5);
  }
  throw new Error('timed out waiting for a condition');
}

/** Recomputes the signature over the bytes the receiver actually read. */
function verifyReceived(record, key = KEY) {
  const timestamp = record.headers['x-dune-relay-timestamp'];
  const signature = record.headers['x-dune-relay-signature'];
  assert.match(timestamp, /^[0-9]{10}$/, 'timestamp header must be 10 unix-second digits');
  assert.match(signature, /^[0-9a-f]{64}$/, 'signature header must be lowercase hex');
  const expected = crypto.createHmac('sha256', key)
    .update(Buffer.concat([Buffer.from(`${timestamp}\n`, 'ascii'), record.body]))
    .digest('hex');
  assert.equal(signature, expected, 'signature must cover timestamp + "\\n" + exact body bytes');
  return { timestamp, signature };
}

// --- schema -----------------------------------------------------------------------------------

describe('lifecycle DTO', () => {
  const roomId = crypto.randomBytes(16).toString('base64url');

  it('emits exactly the nine backend keys in a fixed order', () => {
    const event = buildLifecycleEvent({
      kind: 'joined', roomId, participantId: 7, runtime: 'browser', gameVersion: '1.0.655',
      occurredAt: 1757000000,
    });
    assert.deepEqual(Object.keys(event), [
      'schema_version', 'event_id', 'room_id', 'kind', 'occurred_at',
      'participant_id', 'client_runtime', 'game_version', 'reason',
    ]);
    assert.equal(event.schema_version, 1);
    assert.equal(event.room_id, roomId);
    assert.equal(event.reason, 'peer_joined');
    assert.match(event.event_id, /^[A-Za-z0-9_-]{22,64}$/);
  });

  it('gives every event an independent id', () => {
    const ids = new Set();
    for (let i = 0; i < 50; i += 1) {
      ids.add(buildLifecycleEvent({ kind: 'created', roomId, occurredAt: 1757000000 }).event_id);
    }
    assert.equal(ids.size, 50);
  });

  it('reports room events with unknown runtime, empty version and zero participant', () => {
    for (const kind of ['created', 'started', 'closed']) {
      const event = buildLifecycleEvent({
        kind, roomId, occurredAt: 1757000000,
        // Even if a caller passes attribution, a room event carries none.
        participantId: 9, runtime: 'native', gameVersion: '1.0.655',
      });
      assert.equal(event.client_runtime, 'unknown');
      assert.equal(event.game_version, '');
      assert.equal(event.participant_id, 0);
    }
  });

  it('refuses relay-owned fields that are wrong', () => {
    assert.throws(() => buildLifecycleEvent({ kind: 'nope', roomId, occurredAt: 1 }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ kind: 'created', roomId: 'short', occurredAt: 1 }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ kind: 'created', roomId, occurredAt: 4102444801 }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ kind: 'joined', roomId, occurredAt: 1 }),
      AnalyticsSchemaError, 'joined needs a positive participant id');
    assert.throws(() => buildLifecycleEvent({
      kind: 'left', roomId, occurredAt: 1, participantId: 2 ** 32,
    }), AnalyticsSchemaError);
  });

  it('clamps client-reported attribution instead of dropping the event', () => {
    const event = buildLifecycleEvent({
      kind: 'left', roomId, participantId: 3, occurredAt: 1757000000,
      runtime: 'ADMIN', gameVersion: 'INVITE CODE 4T2K-9QRS', reason: 'Chatty McChatface!!',
    });
    assert.equal(event.client_runtime, 'unknown');
    assert.equal(event.game_version, '');
    assert.equal(event.reason, 'unspecified');
  });

  it('keeps every serialised event inside the 4096 byte body limit', () => {
    const event = buildLifecycleEvent({
      kind: 'left', roomId: 'r'.repeat(64), participantId: 0xffffffff, occurredAt: 4102444800,
      runtime: 'browser', gameVersion: 'v'.repeat(64), reason: 'x'.repeat(48),
      eventId: 'e'.repeat(64),
    });
    assert.ok(serializeEvent(event).length <= 4096);
  });
});

// --- configuration ------------------------------------------------------------------------------

describe('analytics configuration', () => {
  it('is disabled when neither variable is set', () => {
    const config = analyticsConfigFromEnv({});
    assert.equal(config.enabled, false);
    assert.equal(config.state, 'disabled_not_configured');
    assert.equal(createLifecycleSink({ env: {}, observedTransport: 'wss' }).sink, NULL_LIFECYCLE);
  });

  it('fails startup when only one half is configured, without echoing values', () => {
    const secret = 's3cret-key-value-that-is-long-enough-x';
    const onlyKey = () => analyticsConfigFromEnv({ DUNE_RELAY_ANALYTICS_KEY: secret });
    assert.throws(onlyKey, AnalyticsConfigError);
    try {
      onlyKey();
    } catch (err) {
      assert.ok(!err.message.includes(secret), 'the key must not appear in a startup error');
    }
    assert.throws(() => analyticsConfigFromEnv({
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
    }), AnalyticsConfigError);
  });

  it('requires a long key', () => {
    assert.throws(() => analyticsConfigFromEnv({
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
      DUNE_RELAY_ANALYTICS_KEY: 'short',
    }), /must be 32\.\.512 characters/);
  });

  it('refuses userinfo in the destination and never repeats it', () => {
    try {
      parseDestination('https://relay:hunter2@metaserver.example/x', { allowLoopbackHttp: false });
      assert.fail('expected userinfo to be refused');
    } catch (err) {
      assert.ok(err instanceof AnalyticsConfigError);
      assert.ok(!err.message.includes('hunter2'));
      assert.ok(!err.message.includes('relay:'));
    }
  });

  it('allows plain http only for an explicitly enabled loopback host', () => {
    assert.throws(() => parseDestination('http://metaserver.example/x',
      { allowLoopbackHttp: false }), AnalyticsConfigError);
    assert.throws(() => parseDestination('http://127.0.0.1:9/x',
      { allowLoopbackHttp: false }), AnalyticsConfigError);
    assert.throws(() => parseDestination('http://metaserver.example/x',
      { allowLoopbackHttp: true }), /loopback host/);
    assert.equal(parseDestination('http://127.0.0.1:9/x', { allowLoopbackHttp: true }).port, '9');
    assert.equal(parseDestination('https://metaserver.example/x',
      { allowLoopbackHttp: false }).protocol, 'https:');
    assert.throws(() => parseDestination('ftp://metaserver.example/x',
      { allowLoopbackHttp: true }), AnalyticsConfigError);
    assert.throws(() => parseDestination('not-a-url', { allowLoopbackHttp: true }),
      AnalyticsConfigError);
  });

  it('takes the destination from the operator environment only', () => {
    const config = analyticsConfigFromEnv({
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
      DUNE_RELAY_ANALYTICS_KEY: KEY,
      DUNE_RELAY_ANALYTICS_TIMEOUT_MS: '1500',
      DUNE_RELAY_ANALYTICS_ATTEMPTS: '2',
    });
    assert.equal(config.enabled, true);
    assert.equal(config.destination.href, 'https://metaserver.example/relay-events.php');
    assert.equal(config.timeoutMs, 1500);
    assert.equal(config.maxAttempts, 2);
    assert.throws(() => analyticsConfigFromEnv({
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/x',
      DUNE_RELAY_ANALYTICS_KEY: KEY,
      DUNE_RELAY_ANALYTICS_ATTEMPTS: '99',
    }), /DUNE_RELAY_ANALYTICS_ATTEMPTS/);
  });

  it('stays disabled unless the observed transport is exactly wss', () => {
    const env = {
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
      DUNE_RELAY_ANALYTICS_KEY: KEY,
    };
    for (const transport of ['ws', '', undefined, 'WSS', 'wss ']) {
      const built = createLifecycleSink({ env, observedTransport: transport });
      assert.equal(built.sink, NULL_LIFECYCLE);
      assert.equal(built.state, 'disabled_transport_not_wss');
    }
    const enabled = createLifecycleSink({ env, observedTransport: 'wss' });
    assert.equal(enabled.state, 'enabled');
    assert.ok(enabled.sink instanceof LifecyclePublisher);
  });
});

// --- delivery -----------------------------------------------------------------------------------

describe('lifecycle delivery', () => {
  it('signs the exact body bytes it sends', async () => {
    const receiver = await startReceiver();
    const publisher = makePublisher(receiver.url);
    try {
      publisher.participantJoined({
        roomLogId: 'A'.repeat(22), participantId: 4, runtime: 'native', appVersion: '1.0.655',
      });
      await waitFor(() => publisher.stats.delivered === 1);
      assert.equal(receiver.requests.length, 1);

      const [record] = receiver.requests;
      verifyReceived(record);
      assert.equal(record.method, 'POST');
      assert.equal(record.headers['content-type'], 'application/json');
      assert.equal(Number(record.headers['content-length']), record.body.length);
      const event = JSON.parse(record.body.toString('utf8'));
      assert.equal(event.kind, 'joined');
      assert.equal(event.client_runtime, 'native');
      assert.equal(event.game_version, '1.0.655');
      assert.equal(event.participant_id, 4);
      assert.equal(publisher.stats.delivered, 1);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('retries with the same id and body but a fresh timestamp and signature', async () => {
    const receiver = await startReceiver();
    // A controlled clock, advanced by the receiver itself, so the two attempts land in
    // different seconds without depending on wall-clock timing.
    let clock = 1757000000_000;
    receiver.setResponder((req, res, record, count) => {
      if (count === 1) {
        clock += 3000;
        res.writeHead(503);
        res.end();
        return;
      }
      okResponder(req, res);
    });
    const publisher = makePublisher(receiver.url, { now: () => clock, backoffMs: 1 });
    try {
      publisher.roomCreated({ roomLogId: 'B'.repeat(22) });
      await waitFor(() => publisher.stats.delivered === 1);
      assert.equal(receiver.requests.length, 2);

      const [first, second] = receiver.requests;
      assert.deepEqual(first.body, second.body, 'the retried body must be byte-identical');
      assert.equal(JSON.parse(first.body).event_id, JSON.parse(second.body).event_id);
      const a = verifyReceived(first);
      const b = verifyReceived(second);
      assert.notEqual(a.timestamp, b.timestamp, 'each attempt must be freshly timestamped');
      assert.notEqual(a.signature, b.signature);
      assert.equal(publisher.stats.retried, 1);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('stops after the configured number of attempts', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res) => { res.writeHead(500); res.end(); });
    const publisher = makePublisher(receiver.url, { maxAttempts: 3 });
    try {
      publisher.roomCreated({ roomLogId: 'C'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1);
      assert.equal(receiver.requests.length, 3);
      assert.equal(publisher.stats.delivered, 0);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('does not retry a request the receiver refused', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res) => { res.writeHead(401); res.end(); });
    const publisher = makePublisher(receiver.url);
    try {
      publisher.roomCreated({ roomLogId: 'D'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1);
      await delay(50);
      assert.equal(receiver.requests.length, 1);
      assert.equal(publisher.stats.rejected, 1);
      assert.equal(publisher.stats.retried, 0);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('never follows a redirect and never re-sends the body elsewhere', async () => {
    const target = await startReceiver();
    const receiver = await startReceiver();
    receiver.setResponder((req, res) => {
      res.writeHead(302, { location: target.url });
      res.end();
    });
    const publisher = makePublisher(receiver.url);
    try {
      publisher.roomCreated({ roomLogId: 'E'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1);
      await delay(50);
      assert.equal(publisher.stats.redirects, 1);
      assert.equal(publisher.stats.retried, 0, 'a redirect is permanent, not retryable');
      assert.equal(receiver.requests.length, 1);
      assert.equal(target.requests.length, 0, 'the redirect target must never be contacted');
    } finally {
      await publisher.stop();
      await receiver.close();
      await target.close();
    }
  });

  it('times out a receiver that never answers and carries on', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res, record, count) => {
      if (count === 1) return; // hang for the first event only
      okResponder(req, res);
    });
    const publisher = makePublisher(receiver.url, { timeoutMs: 120, maxAttempts: 1 });
    try {
      const started = Date.now();
      publisher.roomCreated({ roomLogId: 'F'.repeat(22) });
      publisher.matchStarted({ roomLogId: 'F'.repeat(22) });
      await waitFor(() => publisher.stats.delivered === 1, 4000);
      assert.equal(publisher.stats.timeouts, 1);
      assert.equal(publisher.stats.failed, 1);
      assert.ok(Date.now() - started < 3000, 'the timeout must be bounded');
      assert.equal(JSON.parse(receiver.requests[1].body).kind, 'started');
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('keeps exactly one request in flight', async () => {
    const receiver = await startReceiver();
    let release;
    const held = new Promise((resolve) => { release = resolve; });
    receiver.setResponder(async (req, res) => {
      await held;
      okResponder(req, res);
    });
    const publisher = makePublisher(receiver.url, { maxQueueEvents: 16 });
    try {
      for (let i = 0; i < 4; i += 1) publisher.roomCreated({ roomLogId: 'G'.repeat(22) });
      await waitFor(() => receiver.requests.length === 1);
      await delay(60);
      assert.equal(receiver.requests.length, 1, 'only one request may be open at a time');
      release();
      await waitFor(() => publisher.stats.delivered === 4);
      assert.equal(receiver.requests.length, 4);
    } finally {
      release();
      await publisher.stop();
      await receiver.close();
    }
  });

  it('drops events when the queue is full by count', async () => {
    const receiver = await startReceiver();
    let release;
    const held = new Promise((resolve) => { release = resolve; });
    receiver.setResponder(async (req, res) => { await held; okResponder(req, res); });
    const publisher = makePublisher(receiver.url, { maxQueueEvents: 2 });
    try {
      for (let i = 0; i < 5; i += 1) publisher.roomCreated({ roomLogId: 'H'.repeat(22) });
      assert.equal(publisher.stats.droppedQueue, 2);
      assert.equal(publisher.stats.accepted, 3);
      release();
      await waitFor(() => publisher.stats.delivered === 3);
      assert.equal(receiver.requests.length, 3, 'a dropped event is never sent later');
    } finally {
      release();
      await publisher.stop();
      await receiver.close();
    }
  });

  it('drops events when the queue is full by bytes', async () => {
    const receiver = await startReceiver();
    let release;
    const held = new Promise((resolve) => { release = resolve; });
    receiver.setResponder(async (req, res) => { await held; okResponder(req, res); });
    const sample = serializeEvent(buildLifecycleEvent({
      kind: 'created', roomId: 'I'.repeat(22), occurredAt: 1757000000,
    })).length;
    const publisher = makePublisher(receiver.url, {
      maxQueueEvents: 1000,
      maxQueueBytes: sample * 2 + 1,
    });
    try {
      for (let i = 0; i < 6; i += 1) publisher.roomCreated({ roomLogId: 'I'.repeat(22) });
      assert.equal(publisher.stats.accepted, 3, 'one in flight plus two queued bodies fit');
      assert.equal(publisher.stats.droppedQueue, 3);
      assert.ok(publisher.queuedBytes <= sample * 2 + 1);
      release();
      await waitFor(() => publisher.stats.delivered === 3);
    } finally {
      release();
      await publisher.stop();
      await receiver.close();
    }
  });

  it('shuts down within its bound while the receiver hangs', async () => {
    const receiver = await startReceiver();
    receiver.setResponder(() => {});
    const publisher = makePublisher(receiver.url, {
      timeoutMs: 30000, maxAttempts: 5, shutdownTimeoutMs: 200,
    });
    try {
      publisher.roomCreated({ roomLogId: 'J'.repeat(22) });
      publisher.roomClosed({ roomLogId: 'J'.repeat(22), reason: 'shutdown' });
      await waitFor(() => receiver.requests.length === 1);
      const started = Date.now();
      await publisher.stop();
      const elapsed = Date.now() - started;
      assert.ok(elapsed < 1500, `shutdown took ${elapsed}ms`);
      assert.equal(publisher.stopped, true);
      assert.equal(publisher.queue.length, 0);
      publisher.roomCreated({ roomLogId: 'J'.repeat(22) });
      assert.equal(publisher.queue.length, 0, 'a stopped publisher accepts nothing');
    } finally {
      await receiver.close();
    }
  });

  it('reports failures only in aggregate', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res) => { res.writeHead(500); res.end(); });
    const log = new LifecycleLog({ sink: () => {} });
    const publisher = makePublisher(receiver.url, {
      maxAttempts: 1, log, aggregateIntervalMs: 0,
    });
    try {
      publisher.participantLeft({
        roomLogId: 'K'.repeat(22), participantId: 2, runtime: 'browser', appVersion: '1.0.655',
      });
      await waitFor(() => publisher.stats.failed === 1);
      await publisher.stop();
      const reported = log.events('analytics_delivery');
      assert.ok(reported.length >= 1);
      const text = JSON.stringify(log.events());
      assert.ok(!text.includes('127.0.0.1'), 'the destination is not logged');
      assert.ok(!text.includes(KEY), 'the key is not logged');
      assert.equal(reported[reported.length - 1].failed, 1);
    } finally {
      await receiver.close();
    }
  });
});

// --- TLS ------------------------------------------------------------------------------------------

describe('TLS endpoint validation', () => {
  let material = null;
  let dir = null;

  before(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), 'relay-tls-'));
    const keyPath = path.join(dir, 'key.pem');
    const certPath = path.join(dir, 'cert.pem');
    const result = spawnSync('openssl', [
      'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
      '-keyout', keyPath, '-out', certPath, '-days', '2',
      '-subj', '/CN=localhost',
      '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1',
    ], { encoding: 'utf8' });
    if (result.status === 0) {
      material = {
        key: fs.readFileSync(keyPath),
        cert: fs.readFileSync(certPath),
        certPath,
      };
    }
  });

  after(() => {
    if (dir) fs.rmSync(dir, { recursive: true, force: true });
  });

  it('refuses an https endpoint whose certificate does not verify', async (t) => {
    if (material === null) {
      t.skip('openssl is unavailable, so no test certificate could be generated');
      return;
    }
    const receiver = await startReceiver({ tls: { key: material.key, cert: material.cert } });
    const publisher = makePublisher(receiver.url);
    try {
      publisher.roomCreated({ roomLogId: 'L'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1);
      assert.equal(receiver.requests.length, 0, 'no body may cross an unverified connection');
      assert.equal(publisher.stats.tlsFailures, 1);
      assert.equal(publisher.stats.retried, 0, 'a verification failure is permanent');
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('delivers over https when the certificate verifies against the pinned CA', async (t) => {
    if (material === null) {
      t.skip('openssl is unavailable, so no test certificate could be generated');
      return;
    }
    const receiver = await startReceiver({ tls: { key: material.key, cert: material.cert } });
    const config = analyticsConfigFromEnv({
      DUNE_RELAY_ANALYTICS_URL: receiver.url,
      DUNE_RELAY_ANALYTICS_KEY: KEY,
      DUNE_RELAY_ANALYTICS_CA_FILE: material.certPath,
    });
    const publisher = new LifecyclePublisher({ ...config, timeoutMs: 2000, backoffMs: 1 });
    try {
      publisher.participantJoined({
        roomLogId: 'M'.repeat(22), participantId: 1, runtime: 'browser', appVersion: '1.0.655',
      });
      await waitFor(() => publisher.stats.delivered === 1, 8000);
      assert.equal(receiver.requests.length, 1);
      verifyReceived(receiver.requests[0]);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });
});

// --- the relay end to end ----------------------------------------------------------------------

describe('relay lifecycle delivery', () => {
  it('delivers a whole room lifecycle without leaking the invitation or the grant', async () => {
    const receiver = await startReceiver();
    const built = createLifecycleSink({
      env: {
        DUNE_RELAY_ANALYTICS_URL: receiver.url,
        DUNE_RELAY_ANALYTICS_KEY: KEY,
        DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP: '1',
      },
      observedTransport: 'wss',
      overrides: { backoffMs: 1, timeoutMs: 2000 },
    });
    assert.equal(built.state, 'enabled');

    const log = new LifecycleLog({ sink: () => {} });
    const relay = await startRelay({ log, observedTransport: 'wss', lifecycle: built.sink });
    let host;
    let guest;
    try {
      host = await joinAsHost(relay, { runtime: 'native', name: 'Stefan' });
      guest = await joinAsClient(relay, host.room, { runtime: 'browser', name: 'Guest' });
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);
      const room = [...relay.store.rooms.values()][0];

      host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
      await guest.client.expect(S2C.ROOM_PHASE_CHANGED);

      // The host leaving closes the room, which ends the guest too.
      host.client.close();
      await guest.client.waitForClose();

      await waitFor(() => receiver.requests.length >= 7, 8000);
      await delay(100);

      const events = receiver.events();
      for (const record of receiver.requests) verifyReceived(record);

      const kinds = events.map((e) => e.kind);
      assert.deepEqual(kinds.filter((k) => k === 'created').length, 1);
      assert.deepEqual(kinds.filter((k) => k === 'joined').length, 2);
      assert.deepEqual(kinds.filter((k) => k === 'started').length, 1);
      assert.deepEqual(kinds.filter((k) => k === 'left').length, 2);
      assert.deepEqual(kinds.filter((k) => k === 'closed').length, 1);
      assert.equal(kinds[0], 'created');

      // One room, one opaque id, and it is the relay's log id rather than the invitation.
      const roomIds = new Set(events.map((e) => e.room_id));
      assert.equal(roomIds.size, 1);
      const [onlyRoomId] = [...roomIds];
      assert.match(onlyRoomId, /^[A-Za-z0-9_-]{22,64}$/);
      assert.equal(onlyRoomId, room.logId);
      assert.notEqual(onlyRoomId, room.code);

      const eventIds = new Set(events.map((e) => e.event_id));
      assert.equal(eventIds.size, events.length, 'every event carries an independent id');

      // Runtime attribution is client-reported and survives to the API; room events carry none.
      const joined = events.filter((e) => e.kind === 'joined');
      assert.deepEqual(joined.map((e) => e.client_runtime).sort(), ['browser', 'native']);
      for (const event of joined) {
        assert.equal(event.game_version, '1.0.655');
        assert.ok(event.participant_id > 0);
        assert.equal(event.reason, 'peer_joined');
      }
      for (const event of events.filter((e) => ['created', 'started', 'closed'].includes(e.kind))) {
        assert.equal(event.client_runtime, 'unknown');
        assert.equal(event.game_version, '');
        assert.equal(event.participant_id, 0);
      }
      const closed = events.find((e) => e.kind === 'closed');
      assert.equal(closed.reason, 'host_left');
      for (const event of events.filter((e) => e.kind === 'left')) {
        assert.ok(['normal', 'host_left', 'unspecified'].includes(event.reason));
        assert.ok(event.participant_id > 0);
      }
      for (const event of events) {
        assert.equal(event.schema_version, 1);
        assert.ok(Math.abs(event.occurred_at - Math.floor(Date.now() / 1000)) < 120);
        assert.deepEqual(Object.keys(event).length, 9);
      }

      // Nothing secret anywhere: not in the signed bodies, not in the diagnostic log.
      const secrets = [
        host.admission.fields.grant,
        guest.admission.fields.grant,
        host.room,
        host.room.replace(/-/g, ''),
        'Stefan',
        'Guest',
      ];
      const wire = receiver.bodies().join('\n');
      const logged = JSON.stringify(log.events());
      for (const secret of secrets) {
        assert.ok(secret && secret.length > 0);
        assert.ok(!wire.includes(secret), `signed bodies must not contain ${secret.slice(0, 4)}...`);
        assert.ok(!logged.includes(secret), `logs must not contain ${secret.slice(0, 4)}...`);
      }
      assert.ok(!wire.includes(KEY));
      assert.ok(!logged.includes(KEY));
    } finally {
      if (host) host.client.close();
      if (guest) guest.client.close();
      await relay.stop();
      await receiver.close();
    }
  });

  it('uses the relay log id, not the invitation code, as room_id', async () => {
    const receiver = await startReceiver();
    const publisher = makePublisher(receiver.url);
    const relay = await startRelay({ observedTransport: 'wss', lifecycle: publisher });
    try {
      const host = await joinAsHost(relay);
      const room = [...relay.store.rooms.values()][0];
      await waitFor(() => receiver.requests.length >= 2);
      const events = receiver.events();
      assert.equal(events[0].room_id, room.logId);
      assert.notEqual(room.logId, room.code);
      assert.ok(!receiver.bodies().join('').includes(room.code));
      host.client.close();
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });

  it('reports nothing when the relay serves plain ws', async () => {
    const receiver = await startReceiver();
    const built = createLifecycleSink({
      env: {
        DUNE_RELAY_ANALYTICS_URL: receiver.url,
        DUNE_RELAY_ANALYTICS_KEY: KEY,
        DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP: '1',
      },
      observedTransport: 'ws',
    });
    const relay = await startRelay({ observedTransport: 'ws', lifecycle: built.sink });
    try {
      const host = await joinAsHost(relay);
      const guest = await joinAsClient(relay, host.room);
      await delay(150);
      assert.equal(receiver.requests.length, 0, 'a ws relay must not report itself as wss');
      host.client.close();
      guest.client.close();
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });

  it('delivers lifecycle events even when diagnostic logging is off', async () => {
    const receiver = await startReceiver();
    const publisher = makePublisher(receiver.url);
    const log = new LifecycleLog({ sink: () => {}, enabled: false });
    const relay = await startRelay({ log, observedTransport: 'wss', lifecycle: publisher });
    try {
      const host = await joinAsHost(relay);
      await waitFor(() => receiver.requests.length >= 2);
      assert.equal(log.events().length, 0, 'the diagnostic log really is off');
      assert.deepEqual(receiver.events().map((e) => e.kind), ['created', 'joined']);
      host.client.close();
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });

  it('keeps the game running when delivery fails', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res) => { res.writeHead(500); res.end(); });
    const publisher = makePublisher(receiver.url, { timeoutMs: 200, maxAttempts: 2 });
    const relay = await startRelay({ observedTransport: 'wss', lifecycle: publisher });
    try {
      const host = await joinAsHost(relay);
      const guest = await joinAsClient(relay, host.room);
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);

      // A full lobby exchange while every delivery attempt is failing.
      const chat = protocol.gamePayload(GAME.CHATMESSAGE, Buffer.from('hi'));
      host.client.send(protocol.encodeClientRelay({
        gameMessageType: GAME.CHATMESSAGE, payload: chat,
      }));
      const relayed = await guest.client.expect(S2C.RELAY);
      assert.equal(relayed.gameMessageType, GAME.CHATMESSAGE);

      host.client.send(protocol.encodeClientRoomPhase(PHASE.MATCH));
      const phase = await guest.client.expect(S2C.ROOM_PHASE_CHANGED);
      assert.equal(phase.phase, PHASE.MATCH);
      assert.equal(guest.client.closeInfo, null, 'a failing analytics receiver must not close a game');
      assert.ok(publisher.stats.failed >= 1);

      host.client.close();
      guest.client.close();
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });

  it('reports a room the reaper collected', async () => {
    const receiver = await startReceiver();
    const publisher = makePublisher(receiver.url);
    const relay = await startRelay({
      observedTransport: 'wss', lifecycle: publisher, grantTtlMs: 1,
    });
    try {
      const room = relay.store.createRoom({
        maxPeers: 2, mode: 'coop', gameProtocol: GAME_PROTOCOL, contentHash: '', appVersion: '1.0.655',
      }).room;
      room.createdAt = 0;
      room.emptySince = 0;
      relay.store.sweep();
      await waitFor(() => receiver.requests.length >= 1);
      const [event] = receiver.events();
      assert.equal(event.kind, 'closed');
      assert.equal(event.room_id, room.logId);
      assert.ok(['lifetime', 'empty'].includes(event.reason));
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });
});
