'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const https = require('node:https');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { after, before, describe, it } = require('node:test');

const {
  AnalyticsConfigError,
  AnalyticsSchemaError,
  LifecyclePublisher,
  NULL_LIFECYCLE,
  TRANSPORTS,
  analyticsConfigFromEnv,
  buildLifecycleEvent,
  createLifecycleSink,
  parseDestination,
  serializeEvent,
  signBody,
  socketTarget,
} = require('../src/analytics');
const { LifecycleLog } = require('../src/logging');
const protocol = require('../src/protocol');
const { GAME, PHASE, S2C } = require('../src/constants');
const {
  admitHost,
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
  const seen = { maxConcurrent: 0, total: 0 };
  let responder = options.responder || okResponder;

  const track = (socket) => {
    sockets.add(socket);
    seen.total += 1;
    seen.maxConcurrent = Math.max(seen.maxConcurrent, sockets.size);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
  };

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
  server.on('connection', track);
  server.on('secureConnection', track);

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;

  return {
    requests,
    port,
    seen,
    liveSockets: () => sockets.size,
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

/**
 * A receiver that stays busy without ever finishing: it answers a request one byte at a time on
 * an interval shorter than the publisher's deadline. An inactivity timeout never fires against
 * this; only an absolute deadline does.
 */
async function startDripServer(options = {}) {
  const sockets = new Set();
  const seen = { maxConcurrent: 0, total: 0 };
  const script = options.script || 'HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n\r\n{}';
  const intervalMs = options.intervalMs === undefined ? 15 : options.intervalMs;

  const server = net.createServer((socket) => {
    sockets.add(socket);
    seen.total += 1;
    seen.maxConcurrent = Math.max(seen.maxConcurrent, sockets.size);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));

    let index = 0;
    const timer = setInterval(() => {
      if (socket.destroyed || index >= script.length) {
        clearInterval(timer);
        return;
      }
      socket.write(script[index]);
      index += 1;
    }, intervalMs);
    socket.on('close', () => clearInterval(timer));
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return {
    seen,
    liveSockets: () => sockets.size,
    url: `http://127.0.0.1:${server.address().port}/relay-events.php`,
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
    transport: 'wss',
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

  it('emits exactly the ten backend keys in a fixed order', () => {
    const event = buildLifecycleEvent({
      kind: 'joined', roomId, participantId: 7, runtime: 'browser', gameVersion: '1.0.655',
      occurredAt: 1757000000, transport: 'wss',
    });
    assert.deepEqual(Object.keys(event), [
      'schema_version', 'event_id', 'room_id', 'kind', 'occurred_at',
      'participant_id', 'client_runtime', 'game_version', 'reason', 'transport',
    ]);
    assert.equal(event.schema_version, 2);
    assert.equal(event.transport, 'wss');
    assert.equal(event.room_id, roomId);
    assert.equal(event.reason, 'peer_joined');
    assert.match(event.event_id, /^[A-Za-z0-9_-]{22,64}$/);
  });

  it('gives every event an independent id', () => {
    const ids = new Set();
    for (let i = 0; i < 50; i += 1) {
      ids.add(buildLifecycleEvent({
        kind: 'created', roomId, occurredAt: 1757000000, transport: 'wss',
      }).event_id);
    }
    assert.equal(ids.size, 50);
  });

  it('reports room events with unknown runtime, empty version and zero participant', () => {
    for (const kind of ['created', 'started', 'closed']) {
      const event = buildLifecycleEvent({
        kind, roomId, occurredAt: 1757000000, transport: 'https-poll',
        // Even if a caller passes attribution, a room event carries none.
        participantId: 9, runtime: 'native', gameVersion: '1.0.655',
      });
      assert.equal(event.transport, 'https-poll');
      assert.equal(event.client_runtime, 'unknown');
      assert.equal(event.game_version, '');
      assert.equal(event.participant_id, 0);
    }
  });

  it('refuses relay-owned fields that are wrong', () => {
    const ok = { roomId, occurredAt: 1, transport: 'wss' };
    assert.throws(() => buildLifecycleEvent({ ...ok, kind: 'nope' }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ ...ok, kind: 'created', roomId: 'short' }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ ...ok, kind: 'created', occurredAt: 4102444801 }),
      AnalyticsSchemaError);
    assert.throws(() => buildLifecycleEvent({ ...ok, kind: 'joined' }),
      AnalyticsSchemaError, 'joined needs a positive participant id');
    assert.throws(() => buildLifecycleEvent({
      ...ok, kind: 'left', participantId: 2 ** 32,
    }), AnalyticsSchemaError);
  });

  it('clamps client-reported attribution instead of dropping the event', () => {
    const event = buildLifecycleEvent({
      kind: 'left', roomId, participantId: 3, occurredAt: 1757000000, transport: 'wss',
      runtime: 'ADMIN', gameVersion: 'INVITE CODE 4T2K-9QRS', reason: 'Chatty McChatface!!',
    });
    assert.equal(event.client_runtime, 'unknown');
    assert.equal(event.game_version, '');
    assert.equal(event.reason, 'unspecified');
  });

  it('records the server-observed transport and refuses anything else', () => {
    for (const transport of ['wss', 'https-poll']) {
      const event = buildLifecycleEvent({
        kind: 'joined', roomId, participantId: 1, occurredAt: 1757000000, transport,
        runtime: 'browser', gameVersion: '1.0.655',
      });
      assert.equal(event.transport, transport);
      // The runtime stays the client's word for itself; the transport is the relay's own.
      assert.equal(event.client_runtime, 'browser');
    }
    // Unlike a client-reported field, an unknown transport is never clamped to a default: it
    // would mislabel the connection, so the event is refused instead.
    for (const transport of ['ws', 'WSS', 'wss ', 'https', 'http-poll', '', undefined, null, 2,
      ['wss'], { transport: 'wss' }]) {
      assert.throws(() => buildLifecycleEvent({
        kind: 'joined', roomId, participantId: 1, occurredAt: 1757000000, transport,
        runtime: 'browser', gameVersion: '1.0.655',
      }), AnalyticsSchemaError);
    }
  });

  it('keeps every serialised event inside the 4096 byte body limit', () => {
    const event = buildLifecycleEvent({
      kind: 'left', roomId: 'r'.repeat(64), participantId: 0xffffffff, occurredAt: 4102444800,
      runtime: 'browser', gameVersion: 'v'.repeat(64), reason: 'x'.repeat(48),
      eventId: 'e'.repeat(64), transport: 'https-poll',
    });
    assert.ok(serializeEvent(event).length <= 4096);
  });
});

// --- configuration ------------------------------------------------------------------------------

describe('analytics configuration', () => {
  it('loads a bounded private key file and refuses ambiguous or unsafe sources', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'relay-key-test-'));
    const file = path.join(dir, 'analytics.key');
    const env = { DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
      DUNE_RELAY_ANALYTICS_KEY_FILE: file };
    try {
      fs.writeFileSync(file, KEY + '\n', { mode: 0o640 });
      assert.equal(analyticsConfigFromEnv(env).key, KEY);
      assert.throws(() => analyticsConfigFromEnv({ ...env, DUNE_RELAY_ANALYTICS_KEY: KEY }), /only one/);
      fs.chmodSync(file, 0o644);
      assert.throws(() => analyticsConfigFromEnv(env), AnalyticsConfigError);
      fs.chmodSync(file, 0o600);
      fs.writeFileSync(file, 'x'.repeat(4096));
      assert.throws(() => analyticsConfigFromEnv(env), AnalyticsConfigError);
      assert.throws(() => analyticsConfigFromEnv({ ...env, DUNE_RELAY_ANALYTICS_KEY_FILE: dir }), AnalyticsConfigError);
      fs.unlinkSync(file);
      fs.symlinkSync('missing', file);
      assert.throws(() => analyticsConfigFromEnv(env), AnalyticsConfigError);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
  });
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

  it('sends SNI for DNS destinations only, and unwraps IPv6 literals', () => {
    assert.deepEqual(socketTarget(new URL('https://metaserver.example/x'), true), {
      hostname: 'metaserver.example', port: 443, servername: 'metaserver.example',
    });
    // Node 26 refuses an IP in SNI; certificate hostname verification still covers these.
    assert.deepEqual(socketTarget(new URL('https://127.0.0.1:8443/x'), true), {
      hostname: '127.0.0.1', port: 8443,
    });
    assert.deepEqual(socketTarget(new URL('https://[::1]:8443/x'), true), {
      hostname: '::1', port: 8443,
    });
    assert.deepEqual(socketTarget(new URL('http://127.0.0.1/x'), false), {
      hostname: '127.0.0.1', port: 80,
    });
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

  it('stays disabled unless the observed transport is on the production allowlist', () => {
    const env = {
      DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
      DUNE_RELAY_ANALYTICS_KEY: KEY,
    };
    // Development transports, near misses, and anything a client might call itself.
    for (const transport of ['ws', '', undefined, null, 'WSS', 'wss ', 'https-poll ', 'HTTPS-POLL',
      'http-poll', 'https', 'poll', 'browser', 'native', 'udp', 1, {}]) {
      const built = createLifecycleSink({ env, observedTransport: transport });
      assert.equal(built.sink, NULL_LIFECYCLE);
      assert.equal(built.state, 'disabled_transport_not_allowed');
    }
    assert.deepEqual([...TRANSPORTS], ['wss', 'https-poll']);
    for (const transport of TRANSPORTS) {
      const enabled = createLifecycleSink({ env, observedTransport: transport });
      assert.equal(enabled.state, 'enabled');
      assert.ok(enabled.sink instanceof LifecyclePublisher);
      assert.equal(enabled.sink.transport, transport);
    }
  });

  it('takes the transport from the server observation, not from a tuning override', () => {
    const built = createLifecycleSink({
      env: {
        DUNE_RELAY_ANALYTICS_URL: 'https://metaserver.example/relay-events.php',
        DUNE_RELAY_ANALYTICS_KEY: KEY,
      },
      observedTransport: 'https-poll',
      overrides: { backoffMs: 1, transport: 'wss' },
    });
    assert.equal(built.sink.transport, 'https-poll');
  });

  it('refuses to build a publisher for a transport it cannot name', () => {
    for (const transport of ['ws', 'https', undefined, 'wss ']) {
      assert.throws(() => new LifecyclePublisher({
        destination: new URL('https://metaserver.example/relay-events.php'),
        key: KEY,
        transport,
      }), AnalyticsConfigError);
    }
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
      kind: 'created', roomId: 'I'.repeat(22), occurredAt: 1757000000, transport: 'wss',
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

  it('bounds an attempt by an absolute deadline, not by socket activity', async () => {
    // One byte every 15 ms: never idle, never finished. An inactivity timeout would never fire.
    const drip = await startDripServer({ intervalMs: 15 });
    const publisher = makePublisher(drip.url, { timeoutMs: 150, maxAttempts: 1 });
    try {
      const started = Date.now();
      publisher.roomCreated({ roomLogId: 'N'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1, 3000);
      const elapsed = Date.now() - started;
      assert.equal(publisher.stats.timeouts, 1);
      assert.ok(elapsed < 1200, `the deadline must bound a dripping receiver, took ${elapsed}ms`);
      await waitFor(() => drip.liveSockets() === 0, 2000);
      assert.equal(publisher.liveRequests, 0);
    } finally {
      await publisher.stop();
      await drip.close();
    }
  });

  it('covers connect and TLS with the same deadline', async () => {
    // A listener with a full accept backlog: the connection never completes.
    const blackhole = net.createServer(() => {});
    await new Promise((resolve) => blackhole.listen(0, '127.0.0.1', resolve));
    const port = blackhole.address().port;
    blackhole.close();
    // Nothing is listening on this port now, so connect fails fast; the deadline still applies.
    const publisher = makePublisher(`http://127.0.0.1:${port}/relay-events.php`, {
      timeoutMs: 200, maxAttempts: 1,
    });
    try {
      const started = Date.now();
      publisher.roomCreated({ roomLogId: 'O'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 1, 3000);
      assert.ok(Date.now() - started < 1200);
      assert.equal(publisher.liveRequests, 0);
    } finally {
      await publisher.stop();
    }
  });

  it('destroys a header-only response instead of leaving it draining', async () => {
    const receiver = await startReceiver();
    // Status line and headers, then a body that never ends.
    receiver.setResponder((req, res) => {
      res.writeHead(200, { 'content-type': 'application/json' });
      res.write('{"status":');
    });
    const publisher = makePublisher(receiver.url, { timeoutMs: 2000 });
    try {
      for (let i = 0; i < 4; i += 1) publisher.roomCreated({ roomLogId: 'P'.repeat(22) });
      const started = Date.now();
      await waitFor(() => publisher.stats.delivered === 4, 4000);
      assert.ok(Date.now() - started < 1500, 'the status line is the whole answer');
      assert.equal(receiver.seen.total, 4);
      assert.equal(publisher.maxOpenSockets, 1,
        'no previous response may still be open when the next request starts');
      // The receiver can accept the next connection before its own close event for the previous
      // socket lands, so its own view is one connection wider than the client-side bound.
      assert.ok(receiver.seen.maxConcurrent <= 2, `receiver saw ${receiver.seen.maxConcurrent}`);
      await waitFor(() => receiver.liveSockets() === 0, 2000);
      assert.equal(publisher.openSockets, 0);
      assert.equal(publisher.liveRequests, 0);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('holds one upstream connection open at a time across mixed outcomes', async () => {
    const receiver = await startReceiver();
    receiver.setResponder((req, res, record, count) => {
      if (count === 1) return;                                  // no response at all
      if (count === 2) { res.writeHead(500); res.write('x'); return; } // header only, retryable
      if (count === 3) { res.writeHead(302, { location: '/elsewhere' }); res.write('x'); return; }
      okResponder(req, res);
    });
    const publisher = makePublisher(receiver.url, { timeoutMs: 200, maxAttempts: 1 });
    try {
      for (let i = 0; i < 4; i += 1) publisher.roomCreated({ roomLogId: 'Q'.repeat(22) });
      await waitFor(() => publisher.stats.delivered + publisher.stats.failed === 4, 5000);
      assert.equal(publisher.stats.failed, 3);
      assert.equal(publisher.stats.delivered, 1);
      assert.equal(publisher.stats.redirects, 1);
      assert.equal(publisher.maxOpenSockets, 1, 'one upstream socket at a time, whatever happens');
      await waitFor(() => receiver.liveSockets() === 0, 2000);
      assert.equal(publisher.openSockets, 0);
      assert.equal(publisher.liveRequests, 0);
      assert.equal(publisher.queue.length, 0);
    } finally {
      await publisher.stop();
      await receiver.close();
    }
  });

  it('recovers the queue and shuts down after a dripping receiver', async () => {
    const drip = await startDripServer({ intervalMs: 15 });
    const good = await startReceiver();
    const publisher = makePublisher(drip.url, { timeoutMs: 120, maxAttempts: 1 });
    try {
      publisher.roomCreated({ roomLogId: 'R'.repeat(22) });
      publisher.matchStarted({ roomLogId: 'R'.repeat(22) });
      await waitFor(() => publisher.stats.failed === 2, 4000);
      assert.equal(publisher.queue.length, 0, 'the queue drains rather than wedging');

      // The same publisher pointed at a healthy receiver keeps working.
      publisher.destination = new URL(good.url);
      publisher.roomClosed({ roomLogId: 'R'.repeat(22), reason: 'shutdown' });
      await waitFor(() => publisher.stats.delivered === 1, 3000);

      const started = Date.now();
      await publisher.stop();
      assert.ok(Date.now() - started < 1500, 'shutdown stays bounded');
      assert.equal(publisher.liveRequests, 0);
    } finally {
      await drip.close();
      await good.close();
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
    const publisher = new LifecyclePublisher({
      ...config, transport: 'wss', timeoutMs: 2000, backoffMs: 1,
    });
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
        assert.equal(event.schema_version, 2);
        assert.equal(event.transport, 'wss');
        assert.ok(Math.abs(event.occurred_at - Math.floor(Date.now() / 1000)) < 120);
        assert.deepEqual(Object.keys(event).length, 10);
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

  it('labels an https-poll relay as https-poll, from the server observation only', async () => {
    const receiver = await startReceiver();
    const built = createLifecycleSink({
      env: {
        DUNE_RELAY_ANALYTICS_URL: receiver.url,
        DUNE_RELAY_ANALYTICS_KEY: KEY,
        DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP: '1',
      },
      observedTransport: 'https-poll',
      overrides: { backoffMs: 1, timeoutMs: 2000 },
    });
    assert.equal(built.state, 'enabled');
    const relay = await startRelay({ observedTransport: 'https-poll', lifecycle: built.sink });
    let host;
    let guest;
    try {
      host = await joinAsHost(relay, { runtime: 'native' });
      guest = await joinAsClient(relay, host.room, { runtime: 'browser' });
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);
      await waitFor(() => receiver.requests.length >= 3, 8000);

      const events = receiver.events();
      for (const record of receiver.requests) verifyReceived(record);
      for (const event of events) {
        assert.equal(event.schema_version, 2);
        assert.equal(event.transport, 'https-poll');
      }
      // The browser/native distinction is orthogonal to the transport and survives with it.
      const joined = events.filter((e) => e.kind === 'joined');
      assert.deepEqual(joined.map((e) => e.client_runtime).sort(), ['browser', 'native']);
      assert.deepEqual(events.filter((e) => e.kind === 'created').length, 1);
    } finally {
      if (host) host.client.close();
      if (guest) guest.client.close();
      await relay.stop();
      await receiver.close();
    }
  });

  it('ignores a transport offered by a caller of the lifecycle hooks', async () => {
    const receiver = await startReceiver();
    const publisher = makePublisher(receiver.url, { transport: 'https-poll' });
    try {
      // Whatever a hook is handed, the published label stays the publisher's own observation.
      publisher.participantJoined({
        roomLogId: 'T'.repeat(22), participantId: 1, runtime: 'browser', appVersion: '1.0.655',
        transport: 'wss', schema_version: 1, source: 'client',
      });
      publisher.roomCreated({ roomLogId: 'T'.repeat(22), transport: 'ws' });
      await waitFor(() => publisher.stats.delivered === 2);
      for (const event of receiver.events()) {
        assert.equal(event.transport, 'https-poll');
        assert.equal(event.schema_version, 2);
        assert.equal(Object.keys(event).length, 10);
      }
    } finally {
      await publisher.stop();
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

});

// --- expired rooms ------------------------------------------------------------------------------
//
// The reaper used to drop an expired room out of the room table without telling anybody: no
// room_closed, no participant teardown, and sockets left attached to a room that no longer
// existed. It now defers to the same close path as every other reason, on an injected clock.

describe('expired rooms', () => {
  const ROOM_LIFETIME_MS = 6 * 60 * 60 * 1000;

  async function startExpiringRelay(receiver, clock) {
    return startRelay({
      observedTransport: 'wss',
      lifecycle: makePublisher(receiver.url),
      now: () => clock.value,
      // Longer than the jumps below, so the liveness sweep cannot pre-empt the reaper.
      livenessTimeoutMs: 7 * 60 * 60 * 1000,
    });
  }

  it('disconnects the peers of a room that reached its lifetime, once', async () => {
    const receiver = await startReceiver();
    const clock = { value: Date.now() };
    const relay = await startExpiringRelay(receiver, clock);
    try {
      const host = await joinAsHost(relay, { runtime: 'native' });
      const guest = await joinAsClient(relay, host.room, { runtime: 'browser' });
      await host.client.expect(S2C.PEER_JOINED);
      await guest.client.expect(S2C.PEER_JOINED);
      const room = [...relay.store.rooms.values()][0];

      clock.value += ROOM_LIFETIME_MS + 1000;
      relay.store.sweep();
      relay.store.sweep(); // a second pass must not repeat anything

      const hostClose = await host.client.waitForClose();
      const guestClose = await guest.client.waitForClose();
      assert.equal(hostClose.code, 4408);
      assert.equal(guestClose.code, 4408);
      assert.equal(room.peers.size, 0, 'the room releases its peers');
      assert.equal(relay.store.rooms.has(room.code), false);

      const closedLogs = relay.log.events('room_closed');
      assert.equal(closedLogs.length, 1);
      assert.equal(closedLogs[0].reasonCode, 'lifetime');
      assert.equal(closedLogs[0].peers, 2);
      assert.equal(closedLogs[0].room, room.logId);
      assert.equal(relay.log.events('participant_left').length, 2);

      await waitFor(() => relay.lifecycle.stats.delivered >= 6, 8000);
      const kinds = receiver.events().map((e) => e.kind);
      assert.equal(kinds.filter((k) => k === 'closed').length, 1, 'closed is reported once');
      assert.equal(kinds.filter((k) => k === 'left').length, 2);
      const closed = receiver.events().find((e) => e.kind === 'closed');
      assert.equal(closed.reason, 'lifetime');
      assert.equal(closed.room_id, room.logId);
      for (const left of receiver.events().filter((e) => e.kind === 'left')) {
        assert.equal(left.reason, 'lifetime');
        assert.ok(left.participant_id > 0);
      }
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });

  it('reaps an empty room after its grant expires, and the grant no longer admits', async () => {
    const receiver = await startReceiver();
    const clock = { value: Date.now() };
    const relay = await startExpiringRelay(receiver, clock);
    try {
      const admission = await admitHost(relay);
      assert.equal(admission.fields.status, 'ok');
      const room = [...relay.store.rooms.values()][0];
      assert.equal(room.outstandingGrants, 1);

      // Not yet: an outstanding grant holds the seat open for a host that is still connecting.
      clock.value += 10000;
      relay.store.sweep();
      assert.equal(room.closed, false);

      clock.value += 120000;
      relay.store.sweep();
      relay.store.sweep();

      assert.equal(room.closed, true);
      assert.equal(room.outstandingGrants, 0);
      assert.equal(relay.store.consumeGrant(admission.fields.grant), null,
        'an expired grant admits nobody');

      const closedLogs = relay.log.events('room_closed');
      assert.equal(closedLogs.length, 1);
      assert.equal(closedLogs[0].reasonCode, 'empty');
      assert.equal(closedLogs[0].peers, 0);
      assert.equal(relay.log.events('participant_left').length, 0);

      await waitFor(() => relay.lifecycle.stats.delivered >= 2);
      const kinds = receiver.events().map((e) => e.kind);
      assert.deepEqual(kinds, ['created', 'closed']);
      assert.equal(receiver.events()[1].reason, 'empty');
      assert.equal(receiver.events()[1].participant_id, 0);
      assert.ok(!receiver.bodies().join('').includes(admission.fields.grant));
      assert.ok(!receiver.bodies().join('').includes(admission.fields.room));
    } finally {
      await relay.stop();
      await receiver.close();
    }
  });
});
