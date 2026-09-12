'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { describe, it } = require('node:test');

const {
  startRelay,
  admitHost,
  joinAsHost,
  joinAsClient,
  delay,
} = require('./helpers');

// The 4096-byte body cap bounds how much an admission request may say, not how long it may take
// to say it or how many clients may be part-way through saying it. These tests use tiny injected
// deadlines and a tiny socket cap so that the real behaviour is observable in milliseconds.

const TIGHT = {
  httpBodyTimeoutMs: 150,
  httpHeadersTimeoutMs: 200,
  httpRequestTimeoutMs: 400,
  httpKeepAliveTimeoutMs: 200,
  httpIdleSocketTimeoutMs: 400,
  maxHttpSockets: 4,
};

/** A raw client, so a test can send half a request and then stop. */
function rawConnect(relay) {
  const socket = net.connect(relay.port, '127.0.0.1');
  const state = { text: '', closed: false, socket };
  socket.setEncoding('utf8');
  socket.on('data', (chunk) => { state.text += chunk; });
  socket.on('close', () => { state.closed = true; });
  socket.on('error', () => { state.closed = true; });
  state.ready = new Promise((resolve, reject) => {
    socket.once('connect', resolve);
    socket.once('error', reject);
  });
  return state;
}

async function waitFor(predicate, timeoutMs = 4000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await delay(5);
  }
  throw new Error('timed out waiting for a condition');
}

describe('HTTP ingress bounds', () => {
  it('refuses a body that arrives too slowly, and recovers', async () => {
    const relay = await startRelay(TIGHT);
    const client = rawConnect(relay);
    try {
      await client.ready;
      // Announces 400 bytes of form, sends 12, then goes quiet.
      client.socket.write('POST /v1/admission/host HTTP/1.1\r\n'
        + 'host: 127.0.0.1\r\n'
        + 'content-type: application/x-www-form-urlencoded\r\n'
        + 'content-length: 400\r\n\r\n'
        + 'app=dunecity');

      const started = Date.now();
      await waitFor(() => client.text.includes('code=timeout'), 3000);
      assert.match(client.text, /^HTTP\/1\.1 408 /);
      assert.ok(Date.now() - started < 2000, 'the body deadline is enforced promptly');
      // The socket goes away rather than being left to finish an abandoned upload.
      await waitFor(() => client.closed, 2000);
      await waitFor(() => relay.httpSocketCount === 0, 2000);

      // A well-behaved client is unaffected.
      const ok = await admitHost(relay);
      assert.equal(ok.fields.status, 'ok');
    } finally {
      client.socket.destroy();
      await relay.stop();
    }
  });

  it('refuses headers that never finish', async () => {
    const relay = await startRelay(TIGHT);
    const client = rawConnect(relay);
    try {
      await client.ready;
      client.socket.write('POST /v1/admission/host HTTP/1.1\r\nhost: 127.0.0.1\r\n');
      await waitFor(() => client.closed, 3000);
      assert.ok(!client.text.includes('status=ok'));
      await waitFor(() => relay.httpSocketCount === 0, 2000);
      const ok = await admitHost(relay);
      assert.equal(ok.fields.status, 'ok');
    } finally {
      client.socket.destroy();
      await relay.stop();
    }
  });

  it('caps how many admission sockets can be open at once, and recovers', async () => {
    // A longer headers deadline than the other cases: the four sockets have to still be open
    // together when the fifth arrives, even on a loaded machine running every suite at once.
    const relay = await startRelay({
      ...TIGHT,
      httpHeadersTimeoutMs: 1000,
      httpRequestTimeoutMs: 1500,
      httpIdleSocketTimeoutMs: 1500,
    });
    const idle = [];
    try {
      for (let i = 0; i < TIGHT.maxHttpSockets; i += 1) {
        const client = rawConnect(relay);
        await client.ready;
        idle.push(client);
      }
      await waitFor(() => relay.httpSocketCount === TIGHT.maxHttpSockets, 2000);

      // One more is dropped immediately: it never gets to occupy a slot or a parser.
      const extra = rawConnect(relay);
      await extra.ready;
      await waitFor(() => extra.closed, 2000);
      assert.equal(extra.text, '');
      assert.equal(relay.httpSocketCount, TIGHT.maxHttpSockets);

      // The idle sockets are reaped by the headers deadline, and capacity comes back.
      await waitFor(() => relay.httpSocketCount === 0, 6000);
      // Server-side removal can precede delivery of FIN to the client event loop.
      await waitFor(() => idle.every(client => client.closed), 2000);
      for (const client of idle) assert.equal(client.closed, true);
      const ok = await admitHost(relay);
      assert.equal(ok.fields.status, 'ok');
    } finally {
      for (const client of idle) client.socket.destroy();
      await relay.stop();
    }
  });

  it('does not charge game sockets to the admission budget', async () => {
    const relay = await startRelay(TIGHT);
    let host;
    let guest;
    try {
      host = await joinAsHost(relay);
      guest = await joinAsClient(relay, host.room);
      // Both peers are upgraded WebSockets, so the admission budget is free again.
      await waitFor(() => relay.httpSocketCount === 0, 2000);
      assert.equal(relay.connections.size, 2);

      // And they survive well past the ingress deadlines, which no longer apply to them.
      await delay(TIGHT.httpIdleSocketTimeoutMs + 300);
      assert.equal(host.client.closeInfo, null);
      assert.equal(guest.client.closeInfo, null);
      assert.equal(relay.connections.size, 2);
    } finally {
      if (host) host.client.close();
      if (guest) guest.client.close();
      await relay.stop();
    }
  });

  it('still answers a complete request that is merely split across packets', async () => {
    const relay = await startRelay(TIGHT);
    const client = rawConnect(relay);
    try {
      await client.ready;
      const body = 'app=dunecity&appVersion=1.0.655&gameProtocol=5&runtime=native'
        + '&maxPeers=2&mode=coop';
      client.socket.write('POST /v1/admission/host HTTP/1.1\r\n'
        + 'host: 127.0.0.1\r\n'
        + 'content-type: application/x-www-form-urlencoded\r\n'
        + `content-length: ${body.length}\r\n\r\n`
        + body.slice(0, 10));
      await delay(40);
      client.socket.write(body.slice(10));
      await waitFor(() => client.text.includes('status=ok'), 3000);
      assert.match(client.text, /^HTTP\/1\.1 200 /);
    } finally {
      client.socket.destroy();
      await relay.stop();
    }
  });
});
