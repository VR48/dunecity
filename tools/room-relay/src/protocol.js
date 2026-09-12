'use strict';

// Bounded encoder/decoder for the relay envelope described in docs/room-relay-protocol.md.
//
// Rules that the whole file obeys:
//   * every length is checked against the bytes actually remaining, by subtraction;
//   * nothing is allocated before its length has been checked;
//   * a frame with trailing bytes after its declared fields is rejected rather than ignored;
//   * strings are validated for length and charset before they become JavaScript strings that
//     end up in logs or in another peer's UI.

const {
  RELAY_PROTOCOL_VERSION,
  C2S,
  S2C,
  ROLE,
  PHASE,
  CLOSE,
  LIMITS,
  DIAGNOSTIC_KIND,
  RELAY_FLAGS,
  MAX_CHANNEL,
} = require('./constants');

class ProtocolError extends Error {
  constructor(closeCode, message) {
    super(message);
    this.name = 'ProtocolError';
    this.closeCode = closeCode;
  }
}

function protocolError(message) {
  return new ProtocolError(CLOSE.PROTOCOL_ERROR, message);
}

class Reader {
  constructor(buffer) {
    this.buf = buffer;
    this.pos = 0;
  }

  get remaining() {
    return this.pos >= this.buf.length ? 0 : this.buf.length - this.pos;
  }

  need(count) {
    // Subtraction form: this.pos never passes this.buf.length, so it cannot wrap.
    if (count > this.remaining) {
      throw protocolError(`truncated frame: need ${count}, have ${this.remaining}`);
    }
  }

  u8() {
    this.need(1);
    return this.buf[this.pos++];
  }

  u16() {
    this.need(2);
    const value = this.buf.readUInt16BE(this.pos);
    this.pos += 2;
    return value;
  }

  u32() {
    this.need(4);
    const value = this.buf.readUInt32BE(this.pos);
    this.pos += 4;
    return value;
  }

  bytes(count) {
    this.need(count);
    const slice = Buffer.allocUnsafe(count);
    this.buf.copy(slice, 0, this.pos, this.pos + count);
    this.pos += count;
    return slice;
  }

  /** Reads a u8-prefixed string and validates its length and charset. */
  shortString(field, maxLen, validator) {
    const len = this.u8();
    if (len > maxLen) {
      throw protocolError(`${field} is ${len} bytes, limit ${maxLen}`);
    }
    this.need(len);
    const value = this.buf.toString('latin1', this.pos, this.pos + len);
    this.pos += len;
    if (validator && !validator(value)) {
      throw protocolError(`${field} has an unusable value`);
    }
    return value;
  }

  expectEnd() {
    if (this.remaining !== 0) {
      throw protocolError(`${this.remaining} trailing bytes after the declared fields`);
    }
  }
}

class Writer {
  constructor(size) {
    this.buf = Buffer.allocUnsafe(size);
    this.pos = 0;
  }

  u8(value) {
    this.buf.writeUInt8(value & 0xff, this.pos);
    this.pos += 1;
    return this;
  }

  u16(value) {
    this.buf.writeUInt16BE(value & 0xffff, this.pos);
    this.pos += 2;
    return this;
  }

  u32(value) {
    this.buf.writeUInt32BE(value >>> 0, this.pos);
    this.pos += 4;
    return this;
  }

  raw(bytes) {
    bytes.copy(this.buf, this.pos);
    this.pos += bytes.length;
    return this;
  }

  shortString(text) {
    const bytes = Buffer.from(text, 'latin1');
    if (bytes.length > 255) {
      throw new Error('shortString longer than 255 bytes');
    }
    this.u8(bytes.length);
    return this.raw(bytes);
  }

  done() {
    return this.buf.subarray(0, this.pos);
  }
}

// --- field validators -------------------------------------------------------------------

/**
 * Same rule as NetworkPacketPolicy::isAcceptablePlayerName: non-empty, bounded and free of
 * control characters, because the name reaches other players' UI and chat.
 */
function isAcceptableDisplayName(name) {
  if (name.length === 0 || name.length > LIMITS.MAX_NAME_CHARS) return false;
  for (let i = 0; i < name.length; i += 1) {
    const c = name.charCodeAt(i);
    if (c < 32 || c === 127) return false;
  }
  return true;
}

function isRuntimeName(value) {
  return value === 'native' || value === 'browser';
}

function isHexToken(value) {
  return value.length > 0 && /^[0-9a-f]+$/.test(value);
}

function isContentHash(value) {
  return value.length === 0 || /^[0-9a-f]{1,64}$/.test(value);
}

function isAppVersion(value) {
  return value.length > 0 && /^[A-Za-z0-9._-]{1,32}$/.test(value);
}

function isPrintableMessage(value) {
  if (value.length > LIMITS.MAX_ERROR_MESSAGE_CHARS) return false;
  for (let i = 0; i < value.length; i += 1) {
    const c = value.charCodeAt(i);
    if (c < 32 || c > 126) return false;
  }
  return true;
}

// --- decoding ---------------------------------------------------------------------------

/**
 * Decodes one client -> relay frame.
 * @param {Buffer} frame raw binary WebSocket payload
 * @returns {object} a typed message
 * @throws {ProtocolError}
 */
function decodeClientMessage(frame) {
  if (!Buffer.isBuffer(frame)) {
    throw protocolError('binary frames only');
  }
  if (frame.length === 0) {
    throw protocolError('empty frame');
  }
  if (frame.length > LIMITS.MAX_FRAME_BYTES) {
    throw new ProtocolError(CLOSE.TOO_LARGE, `frame of ${frame.length} bytes exceeds the limit`);
  }

  const reader = new Reader(frame);
  const type = reader.u8();

  switch (type) {
    case C2S.HELLO: {
      const relayVersion = reader.u16();
      const gameProtocol = reader.u16();
      const grant = reader.shortString('grant', LIMITS.MAX_GRANT_CHARS, isHexToken);
      const runtime = reader.shortString('runtime', LIMITS.MAX_RUNTIME_CHARS, isRuntimeName);
      const appVersion = reader.shortString('appVersion', LIMITS.MAX_APP_VERSION_CHARS, isAppVersion);
      const contentHash = reader.shortString('contentHash', LIMITS.MAX_CONTENT_HASH_CHARS, isContentHash);
      const displayName = reader.shortString('displayName', LIMITS.MAX_NAME_CHARS, isAcceptableDisplayName);
      reader.expectEnd();
      if (relayVersion !== RELAY_PROTOCOL_VERSION) {
        throw new ProtocolError(
          CLOSE.VERSION_MISMATCH,
          `relay protocol ${relayVersion} is not supported`,
        );
      }
      return { type, relayVersion, gameProtocol, grant, runtime, appVersion, contentHash, displayName };
    }

    case C2S.RELAY: {
      const recipient = reader.u32();
      const channel = reader.u8();
      const flags = reader.u8();
      const gameMessageType = reader.u16();
      const payloadLen = reader.u32();
      if (payloadLen < 4 || payloadLen > LIMITS.MAX_GAME_PAYLOAD_BYTES) {
        throw protocolError(`payloadLen ${payloadLen} out of range`);
      }
      const payload = reader.bytes(payloadLen);
      reader.expectEnd();
      if (channel > MAX_CHANNEL) {
        throw protocolError(`channel ${channel} does not exist`);
      }
      // An undefined flag bit means the sender and the relay disagree about what this frame is.
      // Forwarding it would hand the receiver a meaning nobody has agreed on.
      if ((flags & ~RELAY_FLAGS.DEFINED_MASK) !== 0) {
        throw protocolError(`flags 0x${flags.toString(16)} set an undefined bit`);
      }
      // The payload declares its own type in a little-endian uint32 header. Requiring the two
      // to agree removes any gap between what the relay authorises and what the receiver acts on.
      const declared = payload.readUInt32LE(0);
      if (declared !== gameMessageType) {
        throw protocolError(`declared type ${gameMessageType} but payload says ${declared}`);
      }
      return { type, recipient, channel, flags, gameMessageType, payload };
    }

    case C2S.HEARTBEAT: {
      const clientTimeMs = reader.u32();
      reader.expectEnd();
      return { type, clientTimeMs };
    }

    case C2S.LEAVE: {
      const reason = reader.u8();
      reader.expectEnd();
      return { type, reason };
    }

    case C2S.ROOM_PHASE: {
      const phase = reader.u8();
      reader.expectEnd();
      if (phase !== PHASE.LOBBY && phase !== PHASE.MATCH) {
        throw protocolError(`phase ${phase} is not defined`);
      }
      return { type, phase };
    }

    case C2S.DIAGNOSTIC: {
      const kind = reader.u8();
      const payloadLen = reader.u32();
      if (payloadLen > LIMITS.MAX_DIAGNOSTIC_BYTES) {
        throw protocolError(`diagnostic payload ${payloadLen} exceeds the limit`);
      }
      const payload = reader.bytes(payloadLen);
      reader.expectEnd();
      if (kind !== DIAGNOSTIC_KIND.STATE_DIGEST && kind !== DIAGNOSTIC_KIND.LOCKSTEP_STALL) {
        throw protocolError(`diagnostic kind ${kind} is not defined`);
      }
      return { type, kind, payload };
    }

    default:
      throw protocolError(`message id 0x${type.toString(16)} is not a client message`);
  }
}

// --- encoding ---------------------------------------------------------------------------

function encodeWelcome(opts) {
  const room = Buffer.from(opts.roomCode, 'latin1');
  const w = new Writer(1 + 2 + 2 + 4 + 1 + 1 + room.length + 1 + 1 + 4 + 2 + 2);
  w.u8(S2C.WELCOME)
    .u16(RELAY_PROTOCOL_VERSION)
    .u16(opts.gameProtocol)
    .u32(opts.peerId)
    .u8(opts.role)
    .shortString(opts.roomCode)
    .u8(opts.maxPeers)
    .u8(opts.phase)
    .u32(LIMITS.MAX_GAME_PAYLOAD_BYTES)
    .u16(LIMITS.HEARTBEAT_INTERVAL_MS)
    .u16(LIMITS.LIVENESS_TIMEOUT_MS);
  return w.done();
}

function encodePeerJoined(peer) {
  const name = Buffer.from(peer.displayName, 'latin1');
  const runtime = Buffer.from(peer.runtime, 'latin1');
  const w = new Writer(1 + 4 + 1 + 1 + name.length + 1 + runtime.length);
  w.u8(S2C.PEER_JOINED)
    .u32(peer.peerId)
    .u8(peer.role)
    .shortString(peer.displayName)
    .shortString(peer.runtime);
  return w.done();
}

function encodePeerLeft(peerId, reason) {
  return new Writer(6).u8(S2C.PEER_LEFT).u32(peerId).u8(reason).done();
}

function encodeRoomPhaseChanged(phase, byPeerId) {
  return new Writer(6).u8(S2C.ROOM_PHASE_CHANGED).u8(phase).u32(byPeerId).done();
}

function encodeRelay(senderPeerId, msg) {
  const w = new Writer(1 + 4 + 1 + 1 + 2 + 4 + msg.payload.length);
  w.u8(S2C.RELAY)
    .u32(senderPeerId)
    .u8(msg.channel)
    .u8(msg.flags)
    .u16(msg.gameMessageType)
    .u32(msg.payload.length)
    .raw(msg.payload);
  return w.done();
}

function encodeDiagnostic(senderPeerId, kind, payload) {
  const w = new Writer(1 + 4 + 1 + 4 + payload.length);
  w.u8(S2C.DIAGNOSTIC).u32(senderPeerId).u8(kind).u32(payload.length).raw(payload);
  return w.done();
}

function encodeHeartbeatAck(clientEchoMs, serverTimeMs) {
  return new Writer(9).u8(S2C.HEARTBEAT_ACK).u32(clientEchoMs).u32(serverTimeMs).done();
}

function clampMessage(message) {
  const text = typeof message === 'string' ? message : '';
  let out = '';
  for (let i = 0; i < text.length && out.length < LIMITS.MAX_ERROR_MESSAGE_CHARS; i += 1) {
    const c = text.charCodeAt(i);
    out += c >= 32 && c <= 126 ? text[i] : ' ';
  }
  return out;
}

function encodeError(code, message) {
  const text = clampMessage(message);
  const w = new Writer(1 + 2 + 2 + text.length);
  w.u8(S2C.ERROR).u16(code).u16(text.length).raw(Buffer.from(text, 'latin1'));
  return w.done();
}

function encodeRoomClosed(code, message) {
  const text = clampMessage(message);
  const w = new Writer(1 + 2 + 2 + text.length);
  w.u8(S2C.ROOM_CLOSED).u16(code).u16(text.length).raw(Buffer.from(text, 'latin1'));
  return w.done();
}

// --- decoding relay -> client (used by tests and by the transport harness) ----------------

function decodeServerMessage(frame) {
  const reader = new Reader(frame);
  const type = reader.u8();
  switch (type) {
    case S2C.WELCOME: {
      const relayVersion = reader.u16();
      const gameProtocol = reader.u16();
      const peerId = reader.u32();
      const role = reader.u8();
      const roomCode = reader.shortString('roomCode', LIMITS.MAX_ROOM_CODE_CHARS, null);
      const maxPeers = reader.u8();
      const phase = reader.u8();
      const maxPayload = reader.u32();
      const heartbeatIntervalMs = reader.u16();
      const livenessTimeoutMs = reader.u16();
      reader.expectEnd();
      return {
        type, relayVersion, gameProtocol, peerId, role, roomCode, maxPeers, phase,
        maxPayload, heartbeatIntervalMs, livenessTimeoutMs,
      };
    }
    case S2C.PEER_JOINED: {
      const peerId = reader.u32();
      const role = reader.u8();
      const displayName = reader.shortString('displayName', LIMITS.MAX_NAME_CHARS, null);
      const runtime = reader.shortString('runtime', LIMITS.MAX_RUNTIME_CHARS, null);
      reader.expectEnd();
      return { type, peerId, role, displayName, runtime };
    }
    case S2C.PEER_LEFT: {
      const peerId = reader.u32();
      const reason = reader.u8();
      reader.expectEnd();
      return { type, peerId, reason };
    }
    case S2C.ROOM_PHASE_CHANGED: {
      const phase = reader.u8();
      const byPeerId = reader.u32();
      reader.expectEnd();
      return { type, phase, byPeerId };
    }
    case S2C.RELAY: {
      const senderPeerId = reader.u32();
      const channel = reader.u8();
      const flags = reader.u8();
      const gameMessageType = reader.u16();
      const payloadLen = reader.u32();
      if (payloadLen > LIMITS.MAX_GAME_PAYLOAD_BYTES) throw protocolError('payload too large');
      const payload = reader.bytes(payloadLen);
      reader.expectEnd();
      return { type, senderPeerId, channel, flags, gameMessageType, payload };
    }
    case S2C.DIAGNOSTIC: {
      const senderPeerId = reader.u32();
      const kind = reader.u8();
      const payloadLen = reader.u32();
      if (payloadLen > LIMITS.MAX_DIAGNOSTIC_BYTES) throw protocolError('diagnostic too large');
      const payload = reader.bytes(payloadLen);
      reader.expectEnd();
      return { type, senderPeerId, kind, payload };
    }
    case S2C.HEARTBEAT_ACK: {
      const clientEchoMs = reader.u32();
      const serverTimeMs = reader.u32();
      reader.expectEnd();
      return { type, clientEchoMs, serverTimeMs };
    }
    case S2C.ERROR:
    case S2C.ROOM_CLOSED: {
      const code = reader.u16();
      const messageLen = reader.u16();
      if (messageLen > LIMITS.MAX_ERROR_MESSAGE_CHARS) throw protocolError('message too long');
      const message = reader.bytes(messageLen).toString('latin1');
      reader.expectEnd();
      if (!isPrintableMessage(message)) throw protocolError('message is not printable');
      return { type, code, message };
    }
    default:
      throw protocolError(`message id 0x${type.toString(16)} is not a server message`);
  }
}

// --- helpers used by tests and harness ---------------------------------------------------

function encodeHello(opts) {
  const w = new Writer(LIMITS.MAX_FRAME_BYTES > 1024 ? 1024 : LIMITS.MAX_FRAME_BYTES);
  w.u8(C2S.HELLO)
    .u16(opts.relayVersion === undefined ? RELAY_PROTOCOL_VERSION : opts.relayVersion)
    .u16(opts.gameProtocol === undefined ? 5 : opts.gameProtocol)
    .shortString(opts.grant)
    .shortString(opts.runtime || 'native')
    .shortString(opts.appVersion || '1.0.655')
    .shortString(opts.contentHash === undefined ? '' : opts.contentHash)
    .shortString(opts.displayName || 'player');
  return Buffer.from(w.done());
}

function encodeClientRelay(opts) {
  const payload = opts.payload;
  const w = new Writer(1 + 4 + 1 + 1 + 2 + 4 + payload.length);
  w.u8(C2S.RELAY)
    .u32(opts.recipient === undefined ? 0 : opts.recipient)
    .u8(opts.channel === undefined ? 0 : opts.channel)
    .u8(opts.flags === undefined ? 1 : opts.flags)
    .u16(opts.gameMessageType)
    .u32(payload.length)
    .raw(payload);
  return Buffer.from(w.done());
}

function encodeClientHeartbeat(clientTimeMs) {
  return Buffer.from(new Writer(5).u8(C2S.HEARTBEAT).u32(clientTimeMs).done());
}

function encodeClientLeave(reason) {
  return Buffer.from(new Writer(2).u8(C2S.LEAVE).u8(reason).done());
}

function encodeClientRoomPhase(phase) {
  return Buffer.from(new Writer(2).u8(C2S.ROOM_PHASE).u8(phase).done());
}

function encodeClientDiagnostic(kind, payload) {
  const w = new Writer(1 + 1 + 4 + payload.length);
  w.u8(C2S.DIAGNOSTIC).u8(kind).u32(payload.length).raw(payload);
  return Buffer.from(w.done());
}

/** Builds a minimal game payload: a little-endian uint32 packet id plus optional extra bytes. */
function gamePayload(packetType, extra) {
  const tail = extra || Buffer.alloc(0);
  const buf = Buffer.allocUnsafe(4 + tail.length);
  buf.writeUInt32LE(packetType >>> 0, 0);
  tail.copy(buf, 4);
  return buf;
}

module.exports = {
  ProtocolError,
  Reader,
  Writer,
  decodeClientMessage,
  decodeServerMessage,
  encodeWelcome,
  encodePeerJoined,
  encodePeerLeft,
  encodeRoomPhaseChanged,
  encodeRelay,
  encodeDiagnostic,
  encodeHeartbeatAck,
  encodeError,
  encodeRoomClosed,
  encodeHello,
  encodeClientRelay,
  encodeClientHeartbeat,
  encodeClientLeave,
  encodeClientRoomPhase,
  encodeClientDiagnostic,
  gamePayload,
  isAcceptableDisplayName,
  isPrintableMessage,
  ROLE,
  PHASE,
};
