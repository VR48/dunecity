'use strict';

const { WebSocket } = require('ws');

const { createRelay } = require('../src/server');
const { LifecycleLog } = require('../src/logging');
const protocol = require('../src/protocol');
const { S2C, RELAY_PROTOCOL_VERSION } = require('../src/constants');

const GAME_PROTOCOL = 5;
const CONTENT_HASH = 'a'.repeat(16);

/** Starts a relay on an ephemeral loopback port with logging captured instead of printed. */
async function startRelay(overrides = {}) {
  const log = new LifecycleLog({ sink: () => {} });
  const relay = createRelay({
    host: '127.0.0.1',
    port: 0,
    log,
    ...overrides,
  });
  await relay.start();
  relay.socketUrl = `ws://127.0.0.1:${relay.port}${relay.config.socketPath}`;
  relay.baseUrl = `http://127.0.0.1:${relay.port}`;
  return relay;
}

async function postForm(relay, path, fields, headers = {}) {
  const body = Object.entries(fields)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join('&');
  const res = await fetch(`${relay.baseUrl}${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded', ...headers },
    body,
  });
  const text = await res.text();
  return { status: res.status, text, fields: parseKeyValue(text) };
}

function parseKeyValue(text) {
  const out = {};
  for (const line of text.split('\n')) {
    if (line.length === 0) continue;
    const eq = line.indexOf('=');
    if (eq <= 0) continue;
    out[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return out;
}

function admitHost(relay, extra = {}) {
  return postForm(relay, '/v1/admission/host', {
    app: 'dunecity',
    appVersion: '1.0.655',
    gameProtocol: GAME_PROTOCOL,
    contentHash: CONTENT_HASH,
    runtime: 'native',
    maxPeers: 2,
    mode: 'coop',
    ...extra,
  }, extra.__headers);
}

function admitJoin(relay, room, extra = {}) {
  return postForm(relay, '/v1/admission/join', {
    app: 'dunecity',
    appVersion: '1.0.655',
    gameProtocol: GAME_PROTOCOL,
    contentHash: CONTENT_HASH,
    runtime: 'browser',
    room,
    ...extra,
  }, extra.__headers);
}

/** A real WebSocket client with a queue of decoded relay messages. */
class TestClient {
  constructor(ws) {
    this.ws = ws;
    this.queue = [];
    this.waiters = [];
    this.closeInfo = null;
    this.closeWaiters = [];

    ws.on('message', (data, isBinary) => {
      if (!isBinary) return;
      let msg;
      try {
        msg = protocol.decodeServerMessage(Buffer.isBuffer(data) ? data : Buffer.from(data));
      } catch (err) {
        msg = { type: 0, decodeError: err.message };
      }
      const waiter = this.waiters.shift();
      if (waiter) waiter.resolve(msg);
      else this.queue.push(msg);
    });

    ws.on('close', (code, reason) => {
      this.closeInfo = { code, reason: reason.toString() };
      for (const w of this.closeWaiters) w(this.closeInfo);
      this.closeWaiters = [];
      for (const w of this.waiters) w.reject(new Error(`socket closed with ${code}`));
      this.waiters = [];
    });

    ws.on('error', () => { /* close handler reports the outcome */ });
  }

  static connect(url, options = {}) {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url, options);
      const client = new TestClient(ws);
      ws.on('open', () => resolve(client));
      ws.on('error', (err) => reject(err));
      ws.on('unexpected-response', (_req, res) => {
        reject(Object.assign(new Error(`upgrade rejected with ${res.statusCode}`), {
          statusCode: res.statusCode,
        }));
      });
    });
  }

  send(frame) {
    this.ws.send(frame, { binary: true });
  }

  sendText(text) {
    this.ws.send(text);
  }

  next(timeoutMs = 3000) {
    if (this.queue.length > 0) return Promise.resolve(this.queue.shift());
    if (this.closeInfo !== null) {
      return Promise.reject(new Error(`socket already closed with ${this.closeInfo.code}`));
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const index = this.waiters.findIndex((w) => w.timer === timer);
        if (index >= 0) this.waiters.splice(index, 1);
        reject(new Error('timed out waiting for a relay message'));
      }, timeoutMs);
      this.waiters.push({
        timer,
        resolve: (msg) => { clearTimeout(timer); resolve(msg); },
        reject: (err) => { clearTimeout(timer); reject(err); },
      });
    });
  }

  async expect(type, timeoutMs = 3000) {
    const msg = await this.next(timeoutMs);
    if (msg.type !== type) {
      throw new Error(`expected message 0x${type.toString(16)}, got 0x${msg.type.toString(16)}`
        + (msg.decodeError ? ` (${msg.decodeError})` : ''));
    }
    return msg;
  }

  waitForClose(timeoutMs = 3000) {
    if (this.closeInfo !== null) return Promise.resolve(this.closeInfo);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('timed out waiting for close')), timeoutMs);
      this.closeWaiters.push((info) => { clearTimeout(timer); resolve(info); });
    });
  }

  /** Drains anything already queued; used when a test does not care about the exact order. */
  drain() {
    const out = this.queue;
    this.queue = [];
    return out;
  }

  close() {
    try { this.ws.terminate(); } catch { /* already gone */ }
  }
}

/** Admission + connect + HELLO + WELCOME, i.e. a peer that is ready to play. */
async function joinAsHost(relay, opts = {}) {
  const admission = await admitHost(relay, opts.admission || {});
  if (admission.fields.status !== 'ok') {
    throw new Error(`host admission failed: ${admission.text}`);
  }
  const client = await TestClient.connect(relay.socketUrl, opts.wsOptions);
  client.send(protocol.encodeHello({
    grant: admission.fields.grant,
    gameProtocol: GAME_PROTOCOL,
    runtime: opts.runtime || 'native',
    displayName: opts.name || 'host',
    contentHash: CONTENT_HASH,
  }));
  const welcome = await client.expect(S2C.WELCOME);
  return { client, welcome, room: admission.fields.room, admission };
}

async function joinAsClient(relay, room, opts = {}) {
  const admission = await admitJoin(relay, room, opts.admission || {});
  if (admission.fields.status !== 'ok') {
    throw new Error(`join admission failed: ${admission.text}`);
  }
  const client = await TestClient.connect(relay.socketUrl, opts.wsOptions);
  client.send(protocol.encodeHello({
    grant: admission.fields.grant,
    gameProtocol: GAME_PROTOCOL,
    runtime: opts.runtime || 'browser',
    displayName: opts.name || 'guest',
    contentHash: CONTENT_HASH,
  }));
  const welcome = await client.expect(S2C.WELCOME);
  return { client, welcome, admission };
}

function delay(ms) {
  return new Promise((resolve) => { setTimeout(resolve, ms); });
}

module.exports = {
  GAME_PROTOCOL,
  CONTENT_HASH,
  RELAY_PROTOCOL_VERSION,
  startRelay,
  postForm,
  parseKeyValue,
  admitHost,
  admitJoin,
  TestClient,
  joinAsHost,
  joinAsClient,
  delay,
};
