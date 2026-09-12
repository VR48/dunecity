'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const https = require('node:https');
const { isIP } = require('node:net');

const { LEAVE_REASON } = require('./constants');

// Optional, service-only lifecycle delivery to the metaserver relay analytics API.
//
// Shape of the integration:
//   * the relay calls explicit lifecycle hooks (roomCreated/participantJoined/matchStarted/
//     participantLeft/roomClosed) with primitives only. No room, connection or log record ever
//     reaches this file, so nothing can be forwarded by accident;
//   * each hook builds a fresh exact-schema DTO with its own event id and serialises it once.
//     The bytes that are signed are the bytes that are sent, on the first attempt and on every
//     retry;
//   * the queue is bounded in both events and bytes, exactly one request is in flight, and no
//     relay code path ever awaits delivery. Delivery outcomes cannot stall or close a game;
//   * delivery is disabled unless an operator configured both a destination and a key, and it
//     stays disabled unless the server-observed transport is actually wss.
//
// Never sent and never logged here: the analytics key, destination userinfo (refused at
// startup), request bodies, invitation codes, grants, display names, chat, or raw addresses.

const SCHEMA_VERSION = 1;
const MAX_BODY_BYTES = 4096;
const MAX_OCCURRED_AT = 4102444800;
const MIN_KEY_CHARS = 32;
const MAX_KEY_CHARS = 512;

const KINDS = new Set(['created', 'joined', 'started', 'left', 'closed']);
const PARTICIPANT_KINDS = new Set(['joined', 'left']);
const RUNTIMES = new Set(['browser', 'native']);

const ID_PATTERN = /^[A-Za-z0-9_-]{22,64}$/;
const GAME_VERSION_PATTERN = /^[A-Za-z0-9._-]{1,64}$/;
const REASON_PATTERN = /^[a-z0-9_-]{1,48}$/;
const TIMESTAMP_PATTERN = /^[0-9]{10}$/;
const LOOPBACK_HOSTS = new Set(['127.0.0.1', '::1', '[::1]', 'localhost']);

const DEFAULTS = Object.freeze({
  timeoutMs: 5000,
  maxAttempts: 3,
  backoffMs: 250,
  maxBackoffMs: 4000,
  maxQueueEvents: 256,
  maxQueueBytes: 256 * 1024,
  shutdownTimeoutMs: 2000,
  aggregateIntervalMs: 60000,
});

/** Fixed reason codes. Anything not in this table becomes 'unspecified'; nothing is free text. */
const REASON_CODES = new Map([
  [LEAVE_REASON.UNSPECIFIED, 'unspecified'],
  [LEAVE_REASON.NORMAL, 'normal'],
  [LEAVE_REASON.TIMEOUT, 'timeout'],
  [LEAVE_REASON.PROTOCOL_ERROR, 'protocol_error'],
  [LEAVE_REASON.RATE_LIMITED, 'rate_limited'],
  [LEAVE_REASON.SLOW_CONSUMER, 'slow_consumer'],
  [LEAVE_REASON.HOST_LEFT, 'host_left'],
  ['host_left', 'host_left'],
  ['shutdown', 'shutdown'],
  ['lifetime', 'lifetime'],
  ['empty', 'empty'],
]);

const KIND_REASONS = Object.freeze({
  created: 'room_created',
  started: 'match_started',
  joined: 'peer_joined',
});

class AnalyticsConfigError extends Error {
  constructor(message) {
    super(message);
    this.name = 'AnalyticsConfigError';
  }
}

class AnalyticsSchemaError extends Error {
  constructor(message) {
    super(message);
    this.name = 'AnalyticsSchemaError';
  }
}

/** No-op lifecycle sink. The relay always has one of these, so hooks are never conditional. */
const NULL_LIFECYCLE = Object.freeze({
  enabled: false,
  roomCreated() {},
  participantJoined() {},
  matchStarted() {},
  participantLeft() {},
  roomClosed() {},
});

function reasonCode(value) {
  const mapped = REASON_CODES.get(value);
  return mapped === undefined ? 'unspecified' : mapped;
}

/** 22 characters of base64url: inside the backend's 22..64 opaque-id window. */
function newEventId() {
  return crypto.randomBytes(16).toString('base64url');
}

/**
 * Builds one fresh lifecycle DTO with exactly the keys the backend accepts, in a fixed order.
 *
 * Relay-owned fields (kind, room id, event id, participant id, timestamp) are invariants and
 * throw when they are wrong. Client-reported fields (runtime, version) and reasons are clamped
 * to the schema instead, because a client must not be able to suppress a lifecycle event by
 * reporting something unusual about itself.
 *
 * @throws {AnalyticsSchemaError}
 */
function buildLifecycleEvent(input) {
  const kind = input.kind;
  if (!KINDS.has(kind)) throw new AnalyticsSchemaError('unknown lifecycle kind');

  const eventId = input.eventId === undefined ? newEventId() : input.eventId;
  if (typeof eventId !== 'string' || !ID_PATTERN.test(eventId)) {
    throw new AnalyticsSchemaError('event_id is not an acceptable opaque id');
  }
  const roomId = input.roomId;
  if (typeof roomId !== 'string' || !ID_PATTERN.test(roomId)) {
    throw new AnalyticsSchemaError('room_id is not an acceptable opaque id');
  }

  const occurredAt = input.occurredAt;
  if (!Number.isInteger(occurredAt) || occurredAt < 0 || occurredAt > MAX_OCCURRED_AT) {
    throw new AnalyticsSchemaError('occurred_at is out of range');
  }

  const isParticipant = PARTICIPANT_KINDS.has(kind);
  let participantId = input.participantId === undefined ? 0 : input.participantId;
  if (!Number.isInteger(participantId) || participantId < 0 || participantId > 0xffffffff) {
    throw new AnalyticsSchemaError('participant_id is not a uint32');
  }
  if (isParticipant && participantId === 0) {
    throw new AnalyticsSchemaError('participant events need a positive participant_id');
  }
  if (!isParticipant) participantId = 0;

  // Room-scoped events carry no client attribution: there is no single client to attribute to.
  const runtime = isParticipant && RUNTIMES.has(input.runtime) ? input.runtime : 'unknown';
  const gameVersion = isParticipant && typeof input.gameVersion === 'string'
    && GAME_VERSION_PATTERN.test(input.gameVersion) ? input.gameVersion : '';

  let reason = input.reason;
  if (typeof reason !== 'string' || !REASON_PATTERN.test(reason)) {
    reason = KIND_REASONS[kind] || 'unspecified';
  }

  return {
    schema_version: SCHEMA_VERSION,
    event_id: eventId,
    room_id: roomId,
    kind,
    occurred_at: occurredAt,
    participant_id: participantId,
    client_runtime: runtime,
    game_version: gameVersion,
    reason,
  };
}

function serializeEvent(event) {
  const body = Buffer.from(JSON.stringify(event), 'utf8');
  if (body.length > MAX_BODY_BYTES) {
    throw new AnalyticsSchemaError('serialised event exceeds the 4096 byte body limit');
  }
  return body;
}

/** HMAC-SHA256 over the exact bytes `timestamp + "\n" + body`, lowercase hex. */
function signBody(key, timestamp, body) {
  const mac = crypto.createHmac('sha256', key);
  mac.update(timestamp, 'ascii');
  mac.update('\n', 'ascii');
  mac.update(body);
  return mac.digest('hex');
}

function intFromEnv(env, name, fallback, min, max) {
  const raw = env[name];
  if (raw === undefined || raw === '') return fallback;
  if (!/^[0-9]{1,9}$/.test(raw)) {
    throw new AnalyticsConfigError(`${name} must be a whole number of ${min}..${max}`);
  }
  const value = Number.parseInt(raw, 10);
  if (value < min || value > max) {
    throw new AnalyticsConfigError(`${name} must be a whole number of ${min}..${max}`);
  }
  return value;
}

function boolFromEnv(env, name) {
  const raw = env[name];
  if (raw === undefined || raw === '') return false;
  if (raw === '1' || raw === 'true') return true;
  if (raw === '0' || raw === 'false') return false;
  throw new AnalyticsConfigError(`${name} must be 1 or 0`);
}

/**
 * Validates the operator's destination. The URL comes from the environment only; no client
 * value ever reaches it. Errors name the variable and never quote its value, so a mistyped
 * destination or key cannot be echoed into a startup log.
 *
 * @throws {AnalyticsConfigError}
 */
function parseDestination(raw, { allowLoopbackHttp }) {
  let url;
  try {
    url = new URL(raw);
  } catch {
    throw new AnalyticsConfigError('DUNE_RELAY_ANALYTICS_URL is not a valid absolute URL');
  }
  if (url.username !== '' || url.password !== '') {
    throw new AnalyticsConfigError('DUNE_RELAY_ANALYTICS_URL must not contain userinfo');
  }
  if (url.hash !== '') {
    throw new AnalyticsConfigError('DUNE_RELAY_ANALYTICS_URL must not contain a fragment');
  }
  if (url.protocol === 'http:') {
    if (!allowLoopbackHttp) {
      throw new AnalyticsConfigError(
        'DUNE_RELAY_ANALYTICS_URL must be https:// unless '
        + 'DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP=1 and the host is loopback');
    }
    if (!LOOPBACK_HOSTS.has(url.hostname)) {
      throw new AnalyticsConfigError(
        'DUNE_RELAY_ANALYTICS_URL may only use http:// for a loopback host');
    }
  } else if (url.protocol !== 'https:') {
    throw new AnalyticsConfigError('DUNE_RELAY_ANALYTICS_URL must use https:// or loopback http://');
  }
  return url;
}

/**
 * Reads the optional analytics configuration.
 *
 * Neither variable set is the default and means "disabled". Exactly one set is a deployment
 * mistake and fails startup. Both set is enabled, subject to the transport check below.
 *
 * @throws {AnalyticsConfigError}
 */
function analyticsConfigFromEnv(env = process.env) {
  const rawUrl = env.DUNE_RELAY_ANALYTICS_URL;
  const rawKey = env.DUNE_RELAY_ANALYTICS_KEY;
  const hasUrl = typeof rawUrl === 'string' && rawUrl !== '';
  const hasKey = typeof rawKey === 'string' && rawKey !== '';

  if (!hasUrl && !hasKey) return { enabled: false, state: 'disabled_not_configured' };
  if (!hasUrl) {
    throw new AnalyticsConfigError(
      'DUNE_RELAY_ANALYTICS_KEY is set but DUNE_RELAY_ANALYTICS_URL is not');
  }
  if (!hasKey) {
    throw new AnalyticsConfigError(
      'DUNE_RELAY_ANALYTICS_URL is set but DUNE_RELAY_ANALYTICS_KEY is not');
  }
  if (rawKey.length < MIN_KEY_CHARS || rawKey.length > MAX_KEY_CHARS) {
    throw new AnalyticsConfigError(
      `DUNE_RELAY_ANALYTICS_KEY must be ${MIN_KEY_CHARS}..${MAX_KEY_CHARS} characters`);
  }
  if (!/^[\x21-\x7e]+$/.test(rawKey)) {
    throw new AnalyticsConfigError(
      'DUNE_RELAY_ANALYTICS_KEY must be printable ASCII without spaces');
  }

  const allowLoopbackHttp = boolFromEnv(env, 'DUNE_RELAY_ANALYTICS_ALLOW_LOOPBACK_HTTP');
  const destination = parseDestination(rawUrl, { allowLoopbackHttp });

  let ca;
  const caFile = env.DUNE_RELAY_ANALYTICS_CA_FILE;
  if (typeof caFile === 'string' && caFile !== '') {
    try {
      ca = fs.readFileSync(caFile);
    } catch {
      throw new AnalyticsConfigError('DUNE_RELAY_ANALYTICS_CA_FILE could not be read');
    }
  }

  return {
    enabled: true,
    state: 'enabled',
    destination,
    key: rawKey,
    ca,
    timeoutMs: intFromEnv(env, 'DUNE_RELAY_ANALYTICS_TIMEOUT_MS', DEFAULTS.timeoutMs, 100, 60000),
    maxAttempts: intFromEnv(env, 'DUNE_RELAY_ANALYTICS_ATTEMPTS', DEFAULTS.maxAttempts, 1, 10),
    maxQueueEvents: intFromEnv(env, 'DUNE_RELAY_ANALYTICS_QUEUE_EVENTS',
      DEFAULTS.maxQueueEvents, 1, 100000),
    maxQueueBytes: intFromEnv(env, 'DUNE_RELAY_ANALYTICS_QUEUE_BYTES',
      DEFAULTS.maxQueueBytes, 4096, 64 * 1024 * 1024),
  };
}

function isPermanentTransportError(err) {
  const code = err && err.code;
  if (typeof code !== 'string') return false;
  // Certificate and TLS negotiation failures do not improve on a retry, and retrying them just
  // repeats a failing handshake against the receiver.
  return code.startsWith('ERR_TLS')
    || code.startsWith('DEPTH_')
    || code.startsWith('SELF_SIGNED')
    || code.startsWith('UNABLE_TO_')
    || code.startsWith('CERT_')
    || code === 'HOSTNAME_MISMATCH'
    || code === 'ERR_SSL_WRONG_VERSION_NUMBER';
}

class LifecyclePublisher {
  constructor(options) {
    this.destination = options.destination;
    this.key = Buffer.from(options.key, 'utf8');
    this.ca = options.ca;
    this.timeoutMs = options.timeoutMs || DEFAULTS.timeoutMs;
    this.maxAttempts = options.maxAttempts || DEFAULTS.maxAttempts;
    this.backoffMs = options.backoffMs === undefined ? DEFAULTS.backoffMs : options.backoffMs;
    this.maxBackoffMs = options.maxBackoffMs === undefined
      ? DEFAULTS.maxBackoffMs : options.maxBackoffMs;
    this.maxQueueEvents = options.maxQueueEvents || DEFAULTS.maxQueueEvents;
    this.maxQueueBytes = options.maxQueueBytes || DEFAULTS.maxQueueBytes;
    this.shutdownTimeoutMs = options.shutdownTimeoutMs === undefined
      ? DEFAULTS.shutdownTimeoutMs : options.shutdownTimeoutMs;
    this.aggregateIntervalMs = options.aggregateIntervalMs === undefined
      ? DEFAULTS.aggregateIntervalMs : options.aggregateIntervalMs;
    this.now = options.now || (() => Date.now());
    this.log = options.log;

    this.enabled = true;
    this.queue = [];
    this.queuedBytes = 0;
    this.running = false;
    this.stopped = false;
    this.request = null;
    this.sleepTimer = null;
    this.wakeSleep = null;
    this.idleWaiters = [];
    this.lastAggregateAt = this.now();
    this.stats = {
      accepted: 0,
      delivered: 0,
      failed: 0,
      retried: 0,
      droppedQueue: 0,
      droppedInvalid: 0,
      redirects: 0,
      rejected: 0,
      timeouts: 0,
      tlsFailures: 0,
    };
  }

  // --- lifecycle hooks ------------------------------------------------------------------------
  //
  // Each hook takes primitives, returns immediately and never throws into the caller. A relay
  // handler that calls one of these must be able to continue as if it had not.

  roomCreated(info) {
    this.publish('created', { roomId: info.roomLogId });
  }

  matchStarted(info) {
    this.publish('started', { roomId: info.roomLogId });
  }

  roomClosed(info) {
    this.publish('closed', { roomId: info.roomLogId, reason: reasonCode(info.reason) });
  }

  participantJoined(info) {
    this.publish('joined', {
      roomId: info.roomLogId,
      participantId: info.participantId,
      runtime: info.runtime,
      gameVersion: info.appVersion,
    });
  }

  participantLeft(info) {
    this.publish('left', {
      roomId: info.roomLogId,
      participantId: info.participantId,
      runtime: info.runtime,
      gameVersion: info.appVersion,
      reason: reasonCode(info.reason),
    });
  }

  /** Builds, validates, serialises and enqueues one event. Synchronous and bounded. */
  publish(kind, fields) {
    if (!this.enabled || this.stopped) return;
    let body;
    let event;
    try {
      event = buildLifecycleEvent({
        ...fields,
        kind,
        occurredAt: Math.floor(this.now() / 1000),
      });
      body = serializeEvent(event);
    } catch {
      // A malformed event is a relay bug, not a reason to interrupt a game.
      this.stats.droppedInvalid += 1;
      return;
    }

    if (this.queue.length >= this.maxQueueEvents
        || this.queuedBytes + body.length > this.maxQueueBytes) {
      this.stats.droppedQueue += 1;
      return;
    }

    this.queue.push({ eventId: event.event_id, body });
    this.queuedBytes += body.length;
    this.stats.accepted += 1;
    this.pump();
  }

  /** Starts the single-flight drain loop if it is not already running. */
  pump() {
    if (this.running || this.stopped) return;
    this.running = true;
    // Detached on purpose: no relay code path awaits delivery.
    this.drain().then(() => this.finishDrain(), () => this.finishDrain());
  }

  finishDrain() {
    this.running = false;
    if (this.queue.length > 0 && !this.stopped) {
      this.pump();
      return;
    }
    const waiters = this.idleWaiters;
    this.idleWaiters = [];
    for (const resolve of waiters) resolve();
  }

  async drain() {
    while (this.queue.length > 0 && !this.stopped) {
      const item = this.queue.shift();
      this.queuedBytes -= item.body.length;
      await this.deliver(item);
      this.maybeReportAggregate(false);
    }
  }

  /** Sends one immutable body, retrying a bounded number of times with a fresh signature. */
  async deliver(item) {
    for (let attempt = 1; attempt <= this.maxAttempts && !this.stopped; attempt += 1) {
      const timestamp = String(Math.floor(this.now() / 1000));
      if (!TIMESTAMP_PATTERN.test(timestamp)) {
        this.stats.droppedInvalid += 1;
        return;
      }
      const signature = signBody(this.key, timestamp, item.body);
      const outcome = await this.send(item.body, timestamp, signature);

      if (outcome.ok) {
        this.stats.delivered += 1;
        return;
      }
      if (outcome.code === 'redirect') this.stats.redirects += 1;
      if (outcome.code === 'timeout') this.stats.timeouts += 1;
      if (outcome.code === 'tls') this.stats.tlsFailures += 1;
      if (outcome.code === 'rejected') this.stats.rejected += 1;

      if (!outcome.retryable || attempt === this.maxAttempts) {
        this.stats.failed += 1;
        return;
      }
      this.stats.retried += 1;
      await this.backoff(attempt);
    }
  }

  backoff(attempt) {
    const delay = Math.min(this.maxBackoffMs, this.backoffMs * (2 ** (attempt - 1)));
    if (delay <= 0) return Promise.resolve();
    return new Promise((resolve) => {
      this.wakeSleep = () => {
        this.wakeSleep = null;
        if (this.sleepTimer) clearTimeout(this.sleepTimer);
        this.sleepTimer = null;
        resolve();
      };
      this.sleepTimer = setTimeout(() => {
        if (this.wakeSleep) this.wakeSleep();
      }, delay);
      if (this.sleepTimer.unref) this.sleepTimer.unref();
    });
  }

  /**
   * One HTTP request. Redirects are never followed, TLS is always verified, the response body
   * is discarded unread, and the socket is not reused.
   */
  send(body, timestamp, signature) {
    return new Promise((resolve) => {
      const url = this.destination;
      const secure = url.protocol === 'https:';
      const requestOptions = {
        protocol: url.protocol,
        hostname: url.hostname.replace(/^\[|\]$/g, ''),
        port: url.port === '' ? (secure ? 443 : 80) : Number(url.port),
        path: `${url.pathname}${url.search}`,
        method: 'POST',
        headers: {
          'content-type': 'application/json',
          'content-length': body.length,
          'user-agent': 'dunecity-room-relay/1',
          'x-dune-relay-timestamp': timestamp,
          'x-dune-relay-signature': signature,
          connection: 'close',
        },
        agent: false,
        timeout: this.timeoutMs,
      };
      if (secure) {
        requestOptions.rejectUnauthorized = true;
        requestOptions.minVersion = 'TLSv1.2';
        // SNI carries DNS names only; IP certificates are still verified against hostname.
        if (!isIP(requestOptions.hostname)) requestOptions.servername = requestOptions.hostname;
        if (this.ca !== undefined) requestOptions.ca = this.ca;
      }

      let settled = false;
      const finish = (outcome) => {
        if (settled) return;
        settled = true;
        this.request = null;
        resolve(outcome);
      };

      let req;
      try {
        req = (secure ? https : http).request(requestOptions);
      } catch {
        finish({ ok: false, retryable: false, code: 'transport' });
        return;
      }
      this.request = req;

      req.on('timeout', () => {
        req.destroy();
        finish({ ok: false, retryable: true, code: 'timeout' });
      });
      req.on('error', (err) => {
        finish({
          ok: false,
          retryable: !isPermanentTransportError(err),
          code: isPermanentTransportError(err) ? 'tls' : 'transport',
        });
      });
      req.on('response', (res) => {
        const status = res.statusCode || 0;
        res.resume();
        res.on('error', () => {});
        if (status >= 200 && status < 300) {
          finish({ ok: true, code: 'ok' });
        } else if (status >= 300 && status < 400) {
          // A redirect from an analytics receiver is a misconfiguration or an interception
          // attempt. The signed body is not replayed anywhere else.
          req.destroy();
          finish({ ok: false, retryable: false, code: 'redirect' });
        } else if (status === 408 || status === 429 || status >= 500) {
          finish({ ok: false, retryable: true, code: 'server' });
        } else {
          finish({ ok: false, retryable: false, code: 'rejected' });
        }
      });
      req.end(body);
    });
  }

  /** Aggregate counters only: no destination, no event contents, no per-event failures. */
  maybeReportAggregate(force) {
    if (this.log === undefined) return;
    const at = this.now();
    if (!force && at - this.lastAggregateAt < this.aggregateIntervalMs) return;
    if (!force && this.stats.failed === 0 && this.stats.droppedQueue === 0
        && this.stats.droppedInvalid === 0) {
      return;
    }
    this.lastAggregateAt = at;
    this.log.emit('analytics_delivery', {
      accepted: this.stats.accepted,
      delivered: this.stats.delivered,
      failed: this.stats.failed,
      dropped: this.stats.droppedQueue + this.stats.droppedInvalid,
      retried: this.stats.retried,
    });
  }

  /** Waits for the drain loop, then hard-stops. Bounded by shutdownTimeoutMs. */
  async stop() {
    if (this.stopped) return;
    if (this.running && this.shutdownTimeoutMs > 0) {
      await new Promise((resolve) => {
        const timer = setTimeout(resolve, this.shutdownTimeoutMs);
        if (timer.unref) timer.unref();
        this.idleWaiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
    this.stopped = true;
    this.enabled = false;
    if (this.wakeSleep) this.wakeSleep();
    if (this.request) {
      try { this.request.destroy(); } catch { /* already gone */ }
      this.request = null;
    }
    this.stats.droppedQueue += this.queue.length;
    this.queue = [];
    this.queuedBytes = 0;
    this.maybeReportAggregate(true);
  }
}

/**
 * Builds the lifecycle sink for a relay.
 *
 * Delivery requires an operator configuration *and* a server-observed transport of exactly
 * 'wss'. A development relay serving plain ws reports nothing rather than labelling itself as
 * something it is not.
 *
 * @throws {AnalyticsConfigError} when the configuration is present but wrong or incomplete
 */
function createLifecycleSink(options = {}) {
  const env = options.env || process.env;
  const config = options.config || analyticsConfigFromEnv(env);
  if (!config.enabled) {
    return { sink: NULL_LIFECYCLE, state: config.state || 'disabled_not_configured' };
  }
  if (options.observedTransport !== 'wss') {
    return { sink: NULL_LIFECYCLE, state: 'disabled_transport_not_wss' };
  }
  return {
    sink: new LifecyclePublisher({ ...config, ...options.overrides, log: options.log }),
    state: 'enabled',
  };
}

module.exports = {
  AnalyticsConfigError,
  AnalyticsSchemaError,
  DEFAULTS,
  LifecyclePublisher,
  MAX_BODY_BYTES,
  NULL_LIFECYCLE,
  SCHEMA_VERSION,
  analyticsConfigFromEnv,
  buildLifecycleEvent,
  createLifecycleSink,
  newEventId,
  parseDestination,
  reasonCode,
  serializeEvent,
  signBody,
};
