'use strict';

const { LIMITS, RELAY_PROTOCOL_VERSION, ROLE } = require('./constants');
const { AdmissionError } = require('./rooms');

// HTTPS admission. This is deliberately a separate, bounded operation from the gameplay socket:
// room policy, invitation checks and rate limits are applied here, and the credential never
// appears in a WebSocket URL (where it would land in proxy logs and browser history).

const FIELD_RULES = {
  app: { max: 32, pattern: /^[A-Za-z0-9_-]{1,32}$/ },
  appVersion: { max: 32, pattern: /^[A-Za-z0-9._-]{1,32}$/ },
  contentHash: { max: 64, pattern: /^[0-9a-f]{0,64}$/ },
  runtime: { max: 16, pattern: /^(native|browser)$/ },
  mode: { max: 16, pattern: /^(coop|custom)$/ },
  room: { max: 16, pattern: /^[0-9A-Za-z-]{1,16}$/ },
};

/**
 * Reads at most HTTP_MAX_BODY_BYTES of a request body. A larger body is refused instead of
 * being truncated, so a half-parsed form can never be acted on.
 */
function readBoundedBody(req, maxBytes) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    let settled = false;

    const finish = (fn, value) => {
      if (settled) return;
      settled = true;
      req.removeAllListeners('data');
      req.removeAllListeners('end');
      req.removeAllListeners('error');
      fn(value);
    };

    req.on('data', (chunk) => {
      total += chunk.length;
      if (total > maxBytes) {
        // Stop consuming, but let the response be written before the socket goes away: a client
        // that gets a reset instead of a status code cannot tell "too large" from "relay down".
        req.pause();
        finish(reject, new AdmissionError(413, 'bad_request', 'Request body is too large.'));
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => finish(resolve, Buffer.concat(chunks, total)));
    req.on('error', () => finish(reject, new AdmissionError(400, 'bad_request', 'Request failed.')));
  });
}

/** Strict urlencoded parse: bounded pair count, bounded key/value length, no prototype keys. */
function parseForm(body) {
  const out = Object.create(null);
  const text = body.toString('latin1');
  if (text.length === 0) return out;

  const pairs = text.split('&');
  if (pairs.length > 24) {
    throw new AdmissionError(400, 'bad_request', 'Too many form fields.');
  }
  for (const pair of pairs) {
    if (pair.length === 0) continue;
    if (pair.length > 256) {
      throw new AdmissionError(400, 'bad_request', 'A form field is too long.');
    }
    const eq = pair.indexOf('=');
    if (eq <= 0) continue;
    let key;
    let value;
    try {
      key = decodeURIComponent(pair.slice(0, eq).replace(/\+/g, ' '));
      value = decodeURIComponent(pair.slice(eq + 1).replace(/\+/g, ' '));
    } catch {
      throw new AdmissionError(400, 'bad_request', 'A form field is not valid.');
    }
    if (key.length > 32) continue;
    if (key === '__proto__' || key === 'constructor' || key === 'prototype') continue;
    out[key] = value;
  }
  return out;
}

function requireField(form, name) {
  const rule = FIELD_RULES[name];
  const value = form[name];
  if (typeof value !== 'string' || value.length > rule.max || !rule.pattern.test(value)) {
    throw new AdmissionError(400, 'bad_request', `The '${name}' field is missing or not valid.`);
  }
  return value;
}

function optionalField(form, name, fallback) {
  if (form[name] === undefined) return fallback;
  return requireField(form, name);
}

function requireInteger(form, name, min, max) {
  const raw = form[name];
  if (typeof raw !== 'string' || raw.length > 8 || !/^[0-9]{1,8}$/.test(raw)) {
    throw new AdmissionError(400, 'bad_request', `The '${name}' field is missing or not valid.`);
  }
  const value = Number.parseInt(raw, 10);
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new AdmissionError(400, 'bad_request', `The '${name}' field is out of range.`);
  }
  return value;
}

function renderResponse(lines) {
  const text = lines.map(([k, v]) => `${k}=${v}`).join('\n');
  if (lines.length > 16 || text.length > 8192) {
    throw new Error('admission response exceeds its own documented bounds');
  }
  return `${text}\n`;
}

function sendText(res, status, lines, closeConnection = false) {
  const body = renderResponse(lines);
  const headers = {
    'content-type': 'text/plain; charset=utf-8',
    'content-length': Buffer.byteLength(body),
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
  };
  if (closeConnection) headers.connection = 'close';
  res.writeHead(status, headers);
  res.end(body);
}

function sendError(res, err) {
  const isAdmission = err instanceof AdmissionError;
  const status = isAdmission ? err.httpStatus : 500;
  const code = isAdmission ? err.code : 'bad_request';
  const message = isAdmission ? err.message : 'The request could not be handled.';
  // A refused request never continues on the same connection: the body may be half-read.
  sendText(res, status, [
    ['status', 'error'],
    ['code', code],
    ['message', message.slice(0, 200)],
  ], true);
}

/**
 * Checks the browser Origin against the exact allowlist.
 *
 * A request with no Origin header is accepted because native clients do not send one. That is
 * not evidence that the caller is a native client: authentication is the grant. Origin is
 * defence in depth against a hostile page driving a logged-in browser.
 */
function checkOrigin(headers, allowedOrigins) {
  const origin = headers.origin;
  if (origin === undefined) return;
  if (typeof origin !== 'string' || origin.length > 256) {
    throw new AdmissionError(403, 'forbidden_origin', 'That origin is not allowed.');
  }
  if (!allowedOrigins.includes(origin)) {
    // Covers the literal string "null" (sandboxed iframes, file:// pages) unless an operator
    // explicitly put it on the list, which the config loader refuses to do.
    throw new AdmissionError(403, 'forbidden_origin', 'That origin is not allowed.');
  }
}

function clientAddress(req, trustForwardedFor) {
  if (trustForwardedFor) {
    const forwarded = req.headers['x-forwarded-for'];
    if (typeof forwarded === 'string' && forwarded.length <= 512) {
      // With exactly one trusted reverse proxy in front, the hop it appended is the last one.
      const parts = forwarded.split(',');
      const last = parts[parts.length - 1].trim();
      if (last.length > 0) return last;
    }
  }
  return req.socket ? req.socket.remoteAddress || 'unknown' : 'unknown';
}

/**
 * Builds the admission HTTP handler.
 * @param {object} ctx relay context: {store, log, config, addressLimiter, globalLimiter, now}
 */
function createAdmissionHandler(ctx) {
  return async function handleRequest(req, res) {
    const address = clientAddress(req, ctx.config.trustForwardedFor);
    const url = (req.url || '').split('?')[0];

    try {
      if (req.method === 'GET' && url === '/v1/health') {
        sendText(res, 200, [
          ['status', 'ok'],
          ['protocol', String(RELAY_PROTOCOL_VERSION)],
          ['rooms', String(ctx.store.roomCount)],
          ['connections', String(ctx.connectionCount())],
        ]);
        return;
      }

      if (req.method !== 'POST' || (url !== '/v1/admission/host' && url !== '/v1/admission/join')) {
        throw new AdmissionError(404, 'bad_request', 'Unknown endpoint.');
      }

      checkOrigin(req.headers, ctx.config.allowedOrigins);

      const now = ctx.now();
      if (!ctx.globalLimiter.allow(now, 1)) {
        throw new AdmissionError(429, 'rate_limited', 'The relay is busy. Try again in a moment.');
      }
      if (!ctx.addressLimiter.allow(address, now)) {
        throw new AdmissionError(429, 'rate_limited', 'Too many attempts. Try again in a minute.');
      }

      const body = await readBoundedBody(req, LIMITS.HTTP_MAX_BODY_BYTES);
      const form = parseForm(body);

      const app = requireField(form, 'app');
      if (app !== ctx.config.app) {
        throw new AdmissionError(400, 'bad_request', 'Unknown application.');
      }
      const appVersion = requireField(form, 'appVersion');
      const gameProtocol = requireInteger(form, 'gameProtocol', 0, 65535);
      const contentHash = optionalField(form, 'contentHash', '');
      const runtime = requireField(form, 'runtime');

      if (ctx.config.requiredGameProtocol !== 0 && gameProtocol !== ctx.config.requiredGameProtocol) {
        throw new AdmissionError(409, 'unsupported_version',
          'This relay expects a different game version.');
      }

      let result;
      let role;
      if (url === '/v1/admission/host') {
        const mode = optionalField(form, 'mode', 'custom');
        const maxPeersRequested = requireInteger(form, 'maxPeers', 2, LIMITS.MAX_PEERS_PER_ROOM);
        const maxPeers = mode === 'coop' ? 2 : maxPeersRequested;
        result = ctx.store.createRoom({ maxPeers, mode, gameProtocol, contentHash, appVersion });
        role = ROLE.HOST;
        ctx.log.emit('room_created', {
          room: result.room.code,
          mode,
          maxPeers,
          gameProtocol,
          appVersion,
          hostRuntime: runtime,
          transport: ctx.config.observedTransport,
          addressTag: ctx.log.addressTag(address),
        });
      } else {
        const roomCode = requireField(form, 'room');
        result = ctx.store.joinRoom(roomCode, { gameProtocol, contentHash });
        role = ROLE.CLIENT;
      }

      sendText(res, 200, [
        ['status', 'ok'],
        ['protocol', String(RELAY_PROTOCOL_VERSION)],
        ['room', result.room.code],
        ['grant', result.grant],
        ['grantExpiresMs', String(ctx.store.grantTtlMs)],
        ['maxPeers', String(result.room.maxPeers)],
        ['url', ctx.config.publicSocketUrl],
      ]);
      void role;
    } catch (err) {
      if (err instanceof AdmissionError) {
        ctx.log.emit('admission_denied', {
          endpoint: url,
          code: err.code,
          addressTag: ctx.log.addressTag(address),
        });
      } else {
        ctx.log.emit('admission_denied', {
          endpoint: url,
          code: 'internal',
          addressTag: ctx.log.addressTag(address),
        });
      }
      sendError(res, err);
    }
  };
}

module.exports = {
  createAdmissionHandler,
  parseForm,
  readBoundedBody,
  checkOrigin,
  clientAddress,
  renderResponse,
};
