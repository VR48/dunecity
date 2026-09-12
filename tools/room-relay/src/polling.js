'use strict';

// HTTPS transport for hosts where Apache can run PHP but cannot upgrade WebSockets.
// This is a message transport only: peers enter the same authenticated relay handlers as WS.
const { EventEmitter } = require('node:events');
const { randomBytes, createHash } = require('node:crypto');
const { LIMITS, CLOSE } = require('./constants');
const { readBoundedBody, clientAddress, corsHeaders } = require('./admission');
const { WindowCounter, BoundedRateTable } = require('./limits');

const MAX_BATCH = 1048576;
const MAX_FRAMES = 64;
const MAX_QUEUE_FRAMES = 4096;
const SESSION_TTL = 25000;

function decodeBatch(body) {
  if (body.length < 12 || body.length > MAX_BATCH || !body.subarray(0, 4).equals(Buffer.from('DHP1'))) {
    throw new Error('Invalid batch');
  }
  const sequence = body.readUInt32LE(4), count = body.readUInt32LE(8);
  if (sequence === 0 || count > MAX_FRAMES) throw new Error('Invalid sequence or count');
  const frames = [];
  let offset = 12;
  for (let i = 0; i < count; i += 1) {
    if (offset + 4 > body.length) throw new Error('Truncated frame');
    const size = body.readUInt32LE(offset); offset += 4;
    if (size === 0 || size > LIMITS.MAX_FRAME_BYTES || size > body.length - offset) {
      throw new Error('Invalid frame size');
    }
    frames.push(body.subarray(offset, offset + size)); offset += size;
  }
  if (offset !== body.length) throw new Error('Trailing data');
  return { sequence, frames };
}

class PollPeer extends EventEmitter {
  constructor() {
    super();
    this.readyState = 1;
    this.bufferedAmount = 0;
    this.frames = [];
    this.closeCode = 0;
  }
  send(data) {
    if (this.readyState !== 1) return;
    if (this.frames.length >= MAX_QUEUE_FRAMES || this.bufferedAmount + data.length + 4 > MAX_BATCH) {
      // Never continue a partially dropped lockstep stream.
      this.frames = []; this.bufferedAmount = 0;
      this.close(CLOSE.SLOW_CONSUMER, 'Slow consumer');
      return;
    }
    this.frames.push(Buffer.from(data)); this.bufferedAmount += data.length + 4;
    this.emit('readable');
  }
  close(code = 1000) {
    if (this.readyState === 3) return;
    this.readyState = 3; this.closeCode = code;
    this.emit('close'); this.emit('readable');
  }
  terminate() { this.close(CLOSE.TIMEOUT); }
  ping() { /* Application heartbeat frames drive liveness, not HTTP requests. */ }
  response(sequence) {
    const frames = [];
    let bytes = 12;
    while (this.frames.length && frames.length < MAX_FRAMES
           && bytes + 4 + this.frames[0].length <= MAX_BATCH) {
      const frame = this.frames.shift();
      frames.push(frame); bytes += 4 + frame.length;
      this.bufferedAmount -= frame.length + 4;
    }
    const out = Buffer.allocUnsafe(bytes);
    out.write('DHR1', 0, 'ascii'); out.writeUInt32LE(sequence, 4);
    out.writeUInt16LE(this.frames.length ? 0 : this.closeCode, 8);
    out.writeUInt16LE(frames.length, 10);
    let offset = 12;
    for (const frame of frames) {
      out.writeUInt32LE(frame.length, offset); offset += 4;
      frame.copy(out, offset); offset += frame.length;
    }
    return out;
  }
}

function createPolling({ config, accept, canAccept, now = Date.now }) {
  const sessions = new Map();
  const globalRate = new WindowCounter(800, 1000);
  const globalBytes = new WindowCounter(8 * MAX_BATCH, 1000);
  const addressBytes = new BoundedRateTable({ limit: 4 * MAX_BATCH, windowMs: 1000, maxEntries: 2048, ttlMs: 60000 });
  const addressRate = new BoundedRateTable({ limit: 240, windowMs: 1000, maxEntries: 2048, ttlMs: 60000 });
  const sweep = setInterval(() => {
    const at = now();
    for (const [token, session] of sessions) {
      if (at - session.touched > SESSION_TTL || at - session.created > LIMITS.ROOM_LIFETIME_MS
          || (session.closedAt !== null && at - session.closedAt > SESSION_TTL)) {
        session.peer.terminate(); sessions.delete(token);
      }
    }
  }, 1000);
  sweep.unref();

  async function handle(req, res) {
    const path = req.url;
    if (!['/v1/poll/open', '/v1/poll/exchange', '/v1/poll/close'].includes(path)) {
      res.writeHead(404).end(); return;
    }
    const cors = corsHeaders(req.headers, config.allowedOrigins);
    const respond = (status, body = Buffer.alloc(0), type = 'application/octet-stream') => {
      if (!res.destroyed) res.writeHead(status, { ...cors, 'cache-control': 'no-store',
        'content-type': type, 'content-length': body.length, 'x-content-type-options': 'nosniff' }).end(body);
    };
    const origin = req.headers.origin;
    if (origin !== undefined && !config.allowedOrigins.includes(origin)) { respond(403); return; }
    if (req.method === 'OPTIONS') {
      res.writeHead(204, { ...cors, 'access-control-allow-methods': 'POST, OPTIONS',
        'access-control-allow-headers': 'Content-Type, X-Dune-Session', 'cache-control': 'no-store',
        'access-control-max-age': '600' }).end(); return;
    }
    if (req.method !== 'POST') { respond(405); return; }
    const address = clientAddress(req, config.trustForwardedFor);
    if (!globalRate.allow(now(), 1) || !addressRate.allow(address, now())) { respond(429); return; }
    if (req.headers['content-type'] !== 'application/octet-stream') { respond(415); return; }
    // Charge even exact replays before reading, decoding or hashing their bodies.
    const token = req.headers['x-dune-session'];
    const session = typeof token === 'string' && /^[0-9a-f]{64}$/.test(token) ? sessions.get(token) : null;
    if (!path.endsWith('/open')) {
      if (!session || session.address !== address || session.origin !== origin) { respond(403); return; }
      if (!session.rate.allow(now(), 1)) { respond(429); return; }
    }
    const rawLength = req.headers['content-length'];
    if (req.headers['transfer-encoding'] !== undefined || typeof rawLength !== 'string'
        || !/^[0-9]{1,7}$/.test(rawLength)) { respond(400); return; }
    const length = Number(rawLength);
    if (length > (path.endsWith('/exchange') ? MAX_BATCH : 0)) { respond(413); return; }
    const withinGlobal = globalBytes.allow(now(), length);
    const withinAddress = addressBytes.allow(address, now(), length);
    const withinSession = !session || session.bytes.allow(now(), length);
    if (!withinGlobal || !withinAddress || !withinSession) { respond(429); return; }
    let body;
    try { body = await readBoundedBody(req, path.endsWith('/exchange') ? MAX_BATCH : 0); }
    catch (err) { respond(err.httpStatus || 400); return; }
    if (path.endsWith('/open')) {
      const status = canAccept(req);
      if (status !== 200 || sessions.size >= config.maxPollingSessions) { respond(status !== 200 ? status : 503); return; }
      let perAddress = 0;
      for (const session of sessions.values()) if (session.address === address) perAddress += 1;
      if (perAddress >= 4) { respond(429); return; }
      const token = randomBytes(32).toString('hex'), peer = new PollPeer();
      const session = { peer, address, origin, touched: now(), created: now(), closedAt: null,
        sequence: 0, hash: '', response: null, busy: false, rate: new WindowCounter(48, 1000),
        bytes: new WindowCounter(2 * MAX_BATCH, 1000) };
      sessions.set(token, session);
      const connection = accept(peer, req);
      peer.once('close', () => {
        session.closedAt = now();
        // Failed handshakes have no continuing stream whose final response needs replaying.
        if (!connection.authenticated) sessions.delete(token);
      });
      respond(200, Buffer.from(token + '\n'), 'text/plain'); return;
    }
    session.touched = now();
    if (path.endsWith('/close')) {
      session.peer.close(); sessions.delete(token); respond(200); return;
    }
    if (session.busy) { respond(409); return; }
    let batch;
    try { batch = decodeBatch(body); } catch { respond(400); return; }
    const hash = createHash('sha256').update(body).digest('hex');
    if (batch.sequence === session.sequence && session.response !== null) {
      if (hash !== session.hash) { respond(409); return; }
      respond(200, session.response); return;
    }
    if (session.sequence === 0xffffffff || batch.sequence !== session.sequence + 1) { respond(409); return; }
    session.busy = true;
    try {
      for (const frame of batch.frames) {
        if (session.peer.readyState !== 1) break;
        session.peer.emit('message', frame, true);
      }
      if (!session.peer.frames.length && session.peer.readyState === 1) {
        await new Promise((resolve) => {
          const done = () => { clearTimeout(timer); session.peer.off('readable', done); resolve(); };
          const timer = setTimeout(done, 100);
          session.peer.once('readable', done);
        });
      }
      session.sequence = batch.sequence; session.hash = hash;
      session.response = session.peer.response(batch.sequence);
      respond(200, session.response);
    } finally { session.busy = false; }
  }
  return { handle, sessions, stop() {
    clearInterval(sweep);
    for (const session of sessions.values()) session.peer.terminate();
    sessions.clear();
  } };
}
module.exports = { createPolling, decodeBatch, PollPeer, MAX_BATCH, MAX_FRAMES };
