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
const ALLOWED_EVENTS = new Set([
  'relay_started',
  'relay_stopped',
  'room_created',
  'room_phase',
  'room_closed',
  'participant_joined',
  'participant_left',
  'admission_denied',
  'connection_denied',
  'message_refused',
]);

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
    this.sink = options.sink || ((event) => process.stdout.write(`${JSON.stringify(event)}\n`));
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

    const record = { ts: new Date().toISOString(), event };
    for (const [key, value] of Object.entries(fields)) {
      if (value === undefined || value === null) continue;
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
