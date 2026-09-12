'use strict';

const crypto = require('node:crypto');

// Bounded lifecycle logging.
//
// What is logged: room and participant lifecycle, server-observed transport, client-reported
// runtime and version, and the reason a connection ended.
// What is never logged: grants, room codes, chat text, command streams, game payload bytes, or
// raw client addresses. Addresses appear only as a per-process salted prefix so that repeated
// abuse from one source is still correlatable within a run without retaining the address.

const MAX_STRING = 64;
const FIELDS = {
  relay_started: ['port', 'protocol', 'transport'],
  relay_stopped: [],
  room_created: ['room', 'mode', 'maxPeers', 'gameProtocol', 'appVersion', 'hostRuntime', 'transport', 'addressTag'],
  room_phase: ['room', 'phase', 'byPeerId'],
  room_closed: ['room', 'code', 'peers', 'reasonCode'],
  participant_joined: ['room', 'peerId', 'role', 'runtime', 'appVersion', 'transport', 'addressTag'],
  participant_left: ['room', 'peerId', 'role', 'runtime', 'appVersion', 'transport', 'runtimeMs', 'reasonCode'],
  admission_denied: ['endpoint', 'code', 'addressTag'],
  connection_denied: ['code', 'addressTag'],
  message_refused: ['room', 'peerId', 'code'],
  // Analytics delivery is reported in aggregate only: never a destination, a body, or a
  // per-event failure. 'state' is one of the fixed disabled_*/enabled tokens.
  analytics_status: ['state'],
  analytics_delivery: ['accepted', 'delivered', 'failed', 'dropped', 'retried'],
};
const ALLOWED_EVENTS = new Set(Object.keys(FIELDS));

function clampToken(value, maxLength = MAX_STRING) {
  if (typeof value !== 'string') return '';
  let out = '';
  for (let i = 0; i < value.length && out.length < maxLength; i += 1) {
    const c = value.charCodeAt(i);
    out += c >= 32 && c <= 126 ? value[i] : '_';
  }
  return out;
}

class LifecycleLog {
  /**
   * @param {object} options
   * @param {(event: object) => void} [options.sink] receives each finished event
   * @param {boolean} [options.enabled]
   */
  constructor(options = {}) {
    this.sink = options.sink || ((event) => {
      // Drop diagnostics while stdout is congested; never queue more data behind it.
      if (!process.stdout.writableNeedDrain) process.stdout.write(`${JSON.stringify(event)}\n`);
    });
    this.now = options.now || Date.now;
    this.windowStart = this.now();
    this.windowCount = 0;
    this.dropped = 0;
    this.enabled = options.enabled !== false;
    this.addressSalt = crypto.randomBytes(16);
    this.recent = [];
    this.maxRecent = options.maxRecent || 256;
  }

  /** Stable pseudonym for an address; the address itself is never stored or emitted. */
  addressTag(address) {
    const hash = crypto.createHash('sha256');
    hash.update(this.addressSalt);
    hash.update(String(address || ''));
    return hash.digest('hex').slice(0, 12);
  }

  emit(event, fields = {}) {
    if (!this.enabled) return;
    if (!ALLOWED_EVENTS.has(event)) {
      throw new Error(`lifecycle event '${event}' is not in the schema`);
    }

    const now = this.now();
    if (now - this.windowStart >= 1000) { this.windowStart = now; this.windowCount = 0; }
    if (this.windowCount++ >= 100) { this.dropped += 1; return; }
    const record = { ts: new Date(now).toISOString(), event };
    for (const key of FIELDS[event]) {
      const value = fields[key];
      if (value === undefined || value === null) continue;
      // Numeric LEAVE_REASON values, or one of the fixed room-close codes. Never free text.
      if (key === 'reasonCode'
          && ![0, 1, 2, 3, 4, 5, 'host_left', 'shutdown', 'lifetime', 'empty'].includes(value)) {
        continue;
      }
      if (key === 'room' && (typeof value !== 'string' || !/^[A-Za-z0-9_-]{22}$/.test(value))) continue;
      if (typeof value === 'number') {
        record[key] = Number.isFinite(value) ? value : 0;
      } else if (typeof value === 'boolean') {
        record[key] = value;
      } else {
        record[key] = clampToken(String(value));
      }
    }

    this.recent.push(record);
    if (this.recent.length > this.maxRecent) this.recent.shift();
    this.sink(record);
  }

  /** Test/inspection helper; the ring buffer is bounded by maxRecent. */
  events(eventName) {
    return eventName ? this.recent.filter((r) => r.event === eventName) : this.recent.slice();
  }
}

module.exports = { LifecycleLog, ALLOWED_EVENTS, clampToken };
