'use strict';

const crypto = require('node:crypto');
const { AdmissionError } = require('./rooms');
const { WindowCounter } = require('./limits');

// Ephemeral, bounded chat. Names are display names, not verified accounts. Tokens bind
// messages to the confirmed name; neither tokens nor message contents go into analytics.
const SESSION_TTL = 90000;
const SESSION_LIFETIME = 30 * 60000;
const MAX_SESSIONS_PER_ADDRESS = 4;
const MAX_CHANNELS = 32;
const HISTORY_TTL = 15 * 60000;
const MAX_SESSIONS = 128;
const MAX_HISTORY = 100;
function decodeText(hex, maxBytes) {
  if (typeof hex !== 'string' || !hex.length || hex.length > maxBytes * 2
      || !/^(?:[0-9a-f]{2})+$/.test(hex)) {
    throw new AdmissionError(400, 'bad_request', 'Text is missing or too long.');
  }
  let text;
  try { text = new TextDecoder('utf-8', { fatal: true }).decode(Buffer.from(hex, 'hex')); }
  catch { throw new AdmissionError(400, 'bad_request', 'Text must be valid UTF-8.'); }
  // Also reject Unicode formatting controls used to impersonate names or reorder text.
  if (!text.trim() || /[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/u.test(text)) {
    throw new AdmissionError(400, 'bad_request', 'Text contains unsupported characters.');
  }
  return text.trim();
}

class LobbyChat {
  constructor(now = Date.now) {
    this.now = now;
    this.sessions = new Map();
    this.channels = new Map();
  }
  sweep() {
    const now = this.now();
    for (const [token, session] of this.sessions) {
      if (now >= session.expiresAt || now >= session.deadline) this.sessions.delete(token);
    }
    for (const [key, channel] of this.channels) {
      channel.history = channel.history.filter(message => now - message.time < HISTORY_TTL);
      if (!channel.history.length && ![...this.sessions.values()].some(session => session.channel === key)) {
        this.channels.delete(key);
      }
    }
  }
  handle(action, form, spec) {
    this.sweep();
    const channel = `${spec.gameProtocol}:${spec.contentHash}`;
    if (action === 'enter') {
      const name = decodeText(form.name, 64);
      const key = name.normalize('NFKC').toLowerCase();
      if ([...this.sessions.values()].some(session => session.channel === channel && session.key === key)) {
        throw new AdmissionError(409, 'name_taken', 'That name is in use. Choose another name.');
      }
      if (this.sessions.size >= MAX_SESSIONS) {
        throw new AdmissionError(503, 'capacity', 'Lobby chat is full. Try again shortly.');
      }
      const address = spec.address || 'unknown';
      if ([...this.sessions.values()].filter(session => session.address === address).length >= MAX_SESSIONS_PER_ADDRESS) {
        throw new AdmissionError(429, 'rate_limited', 'Too many chat sessions from this connection. Try again shortly.');
      }
      if (!this.channels.has(channel)) {
        if (this.channels.size >= MAX_CHANNELS) {
          throw new AdmissionError(503, 'capacity', 'Lobby chat is full. Try again shortly.');
        }
        this.channels.set(channel, { history: [], sequence: 0 });
      }
      const token = crypto.randomBytes(32).toString('hex');
      this.sessions.set(token, { name, key, channel, address, deadline: this.now() + SESSION_LIFETIME, expiresAt: this.now() + SESSION_TTL,
        sends: new WindowCounter(4, 10000) });
      return [['session', token], ['cursor', String(this.channels.get(channel).sequence)]];
    }
    const session = typeof form.session === 'string' && /^[0-9a-f]{64}$/.test(form.session)
      ? this.sessions.get(form.session) : undefined;
    if (!session || session.channel !== channel) {
      throw new AdmissionError(403, 'session_expired', 'Confirm your name again to use lobby chat.');
    }
    session.expiresAt = this.now() + SESSION_TTL;
    const state = this.channels.get(channel);
    if (action === 'say') {
      const text = decodeText(form.text, 120);
      if (!session.sends.allow(this.now(), 1)) {
        throw new AdmissionError(429, 'rate_limited', 'Please wait a moment before sending more messages.');
      }
      state.history.push({ id: ++state.sequence, time: this.now(), name: session.name, text });
      if (state.history.length > MAX_HISTORY) state.history.shift();
      return [['cursor', String(state.sequence)]];
    }
    if (action !== 'poll' || typeof form.cursor !== 'string' || !/^[0-9]{1,15}$/.test(form.cursor)
        || Number(form.cursor) > state.sequence) {
      throw new AdmissionError(400, 'bad_request', 'The chat request is not valid.');
    }
    const messages = state.history.filter(message => message.id > Number(form.cursor)).slice(0, 12);
    const cursor = messages.length ? messages[messages.length - 1].id : state.sequence;
    const gap = (state.history.length ? state.history[0].id : state.sequence + 1) > Number(form.cursor) + 1;
    return [['cursor', String(cursor)], ['gap', gap ? '1' : '0'], ...messages.map(message => ['chat',
      `${message.id}|${Buffer.from(message.name).toString('hex')}|${Buffer.from(message.text).toString('hex')}`])];
  }
}

module.exports = { LobbyChat, decodeText, SESSION_TTL, SESSION_LIFETIME, MAX_HISTORY, MAX_SESSIONS, MAX_CHANNELS };
