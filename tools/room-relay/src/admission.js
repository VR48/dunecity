'use strict';

const { LIMITS, RELAY_PROTOCOL_VERSION, ROLE } = require('./constants');
const { AdmissionError } = require('./rooms');
const { NULL_LIFECYCLE } = require('./analytics');

// HTTPS admission. This is deliberately a separate, bounded operation from the gameplay socket:
// room policy, invitation checks and rate limits are applied here, and the credential never
// appears in a WebSocket URL (where it would land in proxy logs and browser history).

const MAX_ORIGIN_CHARS = 256;
/** Preflights are only useful for a few minutes; a long cache would outlive a config change. */
const PREFLIGHT_MAX_AGE_SECONDS = 600;

/**
 * A canonical HTTP(S) origin is exactly what a browser puts in the `Origin` header:
 * `scheme://host[:port]` with a non-default port only, and nothing else. Anything with
 * credentials, a path, a query, a fragment, a trailing slash or a default port would never
 * match a real header, so accepting it in the allowlist would silently do nothing.
 * The literal `null` is not a URL and is refused here as well as by name.
 */
function isCanonicalOrigin(value) {
  if (typeof value !== 'string' || value.length === 0 || value.length > MAX_ORIGIN_CHARS) {
    return false;
  }
  let url;
  try {
    url = new URL(value);
  } catch {
    return false;
  }
  if (url.protocol !== 'http:' && url.protocol !== 'https:') return false;
  if (url.username !== '' || url.password !== '') return false;
  if (url.hostname === '') return false;
  if (url.search !== '' || url.hash !== '') return false;
  // `origin` drops a default port and everything after the authority, so this equality is the
  // whole check: the operator wrote an origin and only an origin.
  return url.origin === value;
}

/**
 * @throws {Error} with a message an operator can act on. An origin is not a secret, so the
 *   offending value is quoted; nothing else about the configuration is.
 */
function assertAllowedOrigins(origins) {
  for (const origin of origins) {
    if (origin === 'null') {
      throw new Error("'null' is not an acceptable Origin; remove it from allowedOrigins");
    }
    if (!isCanonicalOrigin(origin)) {
      throw new Error(`allowedOrigins entry ${JSON.stringify(origin)} is not a canonical `
        + 'http(s) origin: expected scheme://host[:port] with no credentials, path, query, '
        + 'fragment, trailing slash or default port');
    }
  }
  return origins;
}

/**
 * CORS headers for one request.
 *
 * Only an exact allowlisted origin is ever echoed: no wildcard, and a foreign or `null` origin
 * gets no header at all rather than a reflected one. `Access-Control-Allow-Credentials` is
 * never sent, because a grant is carried in the response body and must never be handed out on
 * the strength of an ambient cookie. `Vary: Origin` is always present so that no cache can
 * serve one origin's response to another.
 */
function corsHeaders(headers, allowedOrigins) {
  const out = { vary: 'Origin' };
  const origin = headers.origin;
  if (typeof origin === 'string' && origin.length <= MAX_ORIGIN_CHARS
      && allowedOrigins.includes(origin)) {
    out['access-control-allow-origin'] = origin;
  }
  return out;
}

const FIELD_RULES = {
  app: { max: 32, pattern: /^[A-Za-z0-9_-]{1,32}$/ },
  appVersion: { max: 32, pattern: /^[A-Za-z0-9._-]{1,32}$/ },
  contentHash: { max: 64, pattern: /^[0-9a-f]{0,64}$/ },
  runtime: { max: 16, pattern: /^(native|browser)$/ },
  mode: { max: 16, pattern: /^(coop|custom)$/ },
  visibility: { max: 7, pattern: /^(public|private)$/ },
  room: { max: 16, pattern: /^[0-9A-Za-z-]{1,16}$/ },
};

/**
 * Reads at most HTTP_MAX_BODY_BYTES of a request body, within at most `timeoutMs`. A larger
 * body is refused instead of being truncated, so a half-parsed form can never be acted on, and
 * a body that arrives slowly - or a byte at a time, forever - is refused rather than held open.
 */
function readBoundedBody(req, maxBytes, timeoutMs = LIMITS.HTTP_BODY_TIMEOUT_MS) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    let settled = false;

    let deadline = null;
    const finish = (fn, value) => {
      if (settled) return;
      settled = true;
      if (deadline !== null) clearTimeout(deadline);
      req.removeAllListeners('data');
      req.removeAllListeners('end');
      req.removeAllListeners('error');
      fn(value);
    };

    if (timeoutMs > 0) {
      deadline = setTimeout(() => {
        req.pause();
        finish(reject, new AdmissionError(408, 'timeout', 'The request body arrived too slowly.'));
      }, timeoutMs);
      if (deadline.unref) deadline.unref();
    }

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

function sendText(res, status, lines, closeConnection = false, extraHeaders = {}) {
  const body = renderResponse(lines);
  const headers = {
    'content-type': 'text/plain; charset=utf-8',
    'content-length': Buffer.byteLength(body),
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
    ...extraHeaders,
  };
  if (closeConnection) headers.connection = 'close';
  res.writeHead(status, headers);
  res.end(body);
}

function sendError(res, err, extraHeaders = {}) {
  const isAdmission = err instanceof AdmissionError;
  const status = isAdmission ? err.httpStatus : 500;
  const code = isAdmission ? err.code : 'bad_request';
  const message = isAdmission ? err.message : 'The request could not be handled.';
  // A refused request never continues on the same connection: the body may be half-read.
  // The CORS headers go on errors too: without them a browser cannot read the status or the
  // code, and every refusal looks like "the relay is down".
  sendText(res, status, [
    ['status', 'error'],
    ['code', code],
    ['message', message.slice(0, 200)],
  ], true, extraHeaders);
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
  if (ctx.lifecycle === undefined) ctx.lifecycle = NULL_LIFECYCLE;
  return async function handleRequest(req, res) {
    const address = clientAddress(req, ctx.config.trustForwardedFor);
    const url = (req.url || '').split('?')[0];
    const cors = corsHeaders(req.headers, ctx.config.allowedOrigins);
    const isAdmissionPath = url === '/v1/admission/host' || url === '/v1/admission/join'
      || url === '/v1/admission/list';

    try {
      if (req.method === 'GET' && url === '/v1/health') {
        sendText(res, 200, [
          ['status', 'ok'],
          ['protocol', String(RELAY_PROTOCOL_VERSION)],
          ['rooms', String(ctx.store.roomCount)],
          ['connections', String(ctx.connectionCount())],
        ], false, cors);
        return;
      }

      if (req.method === 'OPTIONS' && isAdmissionPath) {
        // Admission is a simple CORS request (POST + urlencoded), so a browser normally never
        // gets here. This exists so that a client which does preflight is not left guessing,
        // and it allows nothing beyond what a simple request already allows.
        checkOrigin(req.headers, ctx.config.allowedOrigins);
        if (cors['access-control-allow-origin'] === undefined) {
          throw new AdmissionError(403, 'forbidden_origin', 'That origin is not allowed.');
        }
        const requested = req.headers['access-control-request-method'];
        if (requested !== undefined && requested !== 'POST') {
          throw new AdmissionError(403, 'forbidden_origin', 'Only POST is allowed here.');
        }
        res.writeHead(204, {
          ...cors,
          'access-control-allow-methods': 'POST',
          'access-control-allow-headers': 'content-type',
          'access-control-max-age': String(PREFLIGHT_MAX_AGE_SECONDS),
          'cache-control': 'no-store',
          'content-length': 0,
        });
        res.end();
        return;
      }

      if (req.method !== 'POST' || !isAdmissionPath) {
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

      const body = await readBoundedBody(req, LIMITS.HTTP_MAX_BODY_BYTES,
        ctx.config.httpBodyTimeoutMs);
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
      if (url === '/v1/admission/list') {
        const offset = form.offset === undefined ? 0 : requireInteger(form, 'offset', 0, LIMITS.MAX_ROOMS);
        const page = ctx.store.listPublicRooms({ gameProtocol, contentHash }, offset);
        // Hex names cannot inject delimiters or lines; private invitations and grants never
        // appear in discovery. Same CORS, rate, request/response bounds as admission.
        sendText(res, 200, [
          ['status', 'ok'], ['protocol', String(RELAY_PROTOCOL_VERSION)], ['next', String(page.next)],
          ...page.games.map(game => ['game', [game.code, game.players, game.maxPeers,
            game.mode, Buffer.from(game.hostName, 'utf8').toString('hex')].join('|')]),
        ], false, cors);
        return;
      }
      if (url === '/v1/admission/host') {
        const mode = optionalField(form, 'mode', 'custom');
        const maxPeersRequested = requireInteger(form, 'maxPeers', 2, LIMITS.MAX_PEERS_PER_ROOM);
        const maxPeers = mode === 'coop' ? 2 : maxPeersRequested;
        result = ctx.store.createRoom({
          maxPeers, mode, gameProtocol, contentHash, appVersion, runtime,
          visibility: optionalField(form, 'visibility', 'private'),
        });
        role = ROLE.HOST;
        ctx.log.emit('room_created', {
          room: result.room.logId,
          mode,
          maxPeers,
          gameProtocol,
          appVersion,
          hostRuntime: runtime,
          transport: ctx.config.observedTransport,
          addressTag: ctx.log.addressTag(address),
        });
        // Only the relay-generated log id leaves the process; the invitation code does not.
        ctx.lifecycle.roomCreated({ roomLogId: result.room.logId });
      } else {
        const roomCode = requireField(form, 'room');
        result = ctx.store.joinRoom(roomCode, {
          gameProtocol, contentHash, appVersion, runtime,
        });
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
      ], false, cors);
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
      // A client that ran out of time part-way through a body is still sending it. Answer
      // first, then take the socket away rather than reading the rest of it.
      if (err instanceof AdmissionError && err.code === 'timeout') {
        const socket = res.socket;
        res.on('finish', () => {
          if (socket) {
            try { socket.destroy(); } catch { /* already gone */ }
          }
        });
      }
      sendError(res, err, cors);
    }
  };
}

module.exports = {
  assertAllowedOrigins,
  corsHeaders,
  createAdmissionHandler,
  isCanonicalOrigin,
  parseForm,
  readBoundedBody,
  checkOrigin,
  clientAddress,
  renderResponse,
};
