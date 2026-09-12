'use strict';

const crypto = require('node:crypto');

const {
  LIMITS,
  ROLE,
  PHASE,
  ROOM_CODE_ALPHABET,
  ROOM_CODE_LENGTH,
} = require('./constants');

class AdmissionError extends Error {
  constructor(httpStatus, code, message) {
    super(message);
    this.name = 'AdmissionError';
    this.httpStatus = httpStatus;
    this.code = code;
  }
}

/** Unbiased draw from ROOM_CODE_ALPHABET (32 symbols divides 256 evenly, so no rejection needed). */
function generateRoomCode() {
  const bytes = crypto.randomBytes(ROOM_CODE_LENGTH);
  let out = '';
  for (let i = 0; i < ROOM_CODE_LENGTH; i += 1) {
    out += ROOM_CODE_ALPHABET[bytes[i] % ROOM_CODE_ALPHABET.length];
  }
  return `${out.slice(0, 4)}-${out.slice(4, 8)}-${out.slice(8, 12)}`;
}

function normalizeRoomCode(raw) {
  if (typeof raw !== 'string' || raw.length > LIMITS.MAX_ROOM_CODE_CHARS) return null;
  const compact = raw.replace(/-/g, '').toUpperCase();
  if (compact.length !== ROOM_CODE_LENGTH) return null;
  for (const ch of compact) {
    if (!ROOM_CODE_ALPHABET.includes(ch)) return null;
  }
  return `${compact.slice(0, 4)}-${compact.slice(4, 8)}-${compact.slice(8, 12)}`;
}

class Room {
  constructor(id, code, spec) {
    this.id = id;
    // Independent of the invitation credential; only this ID belongs in telemetry.
    this.logId = crypto.randomBytes(16).toString("base64url");
    this.code = code;
    this.maxPeers = spec.maxPeers;
    this.mode = spec.mode;
    this.gameProtocol = spec.gameProtocol;
    this.contentHash = spec.contentHash;
    this.appVersion = spec.appVersion;
    this.phase = PHASE.LOBBY;
    this.createdAt = spec.now;
    this.emptySince = spec.now;
    this.hostPeerId = 0;
    this.closed = false;
    /** @type {Map<number, object>} peerId -> connection */
    this.peers = new Map();
    /** Bounded memory of peer ids that were in this room and have left. */
    this.formerPeerIds = new Set();
    /** Grants issued for this room that have not been consumed or expired yet. */
    this.outstandingGrants = 0;
  }

  /** Seats taken plus grants that can still be redeemed, so a room cannot be oversubscribed. */
  get reservedSeats() {
    return this.peers.size + this.outstandingGrants;
  }

  get host() {
    return this.hostPeerId ? this.peers.get(this.hostPeerId) : undefined;
  }
}

class RoomStore {
  /**
   * @param {object} options
   * @param {() => number} [options.now] clock injection for tests
   */
  constructor(options = {}) {
    this.now = options.now || (() => Date.now());
    /** Called once per room, whichever path closed it, including the reaper. */
    this.onRoomClosed = options.onRoomClosed || (() => {});
    this.maxRooms = options.maxRooms || LIMITS.MAX_ROOMS;
    this.grantTtlMs = options.grantTtlMs === undefined ? LIMITS.GRANT_TTL_MS : options.grantTtlMs;
    /** @type {Map<string, Room>} room code -> room */
    this.rooms = new Map();
    /** @type {Map<string, object>} grant token -> {roomCode, role, expiresAt} */
    this.grants = new Map();
    this.nextRoomId = 1;
    this.nextPeerId = 1;
  }

  sweep() {
    const now = this.now();

    for (const [token, grant] of this.grants) {
      if (now >= grant.expiresAt) {
        this.grants.delete(token);
        const room = this.rooms.get(grant.roomCode);
        if (room && room.outstandingGrants > 0) room.outstandingGrants -= 1;
      }
    }

    for (const room of [...this.rooms.values()]) {
      if (room.closed) continue;
      if (now - room.createdAt > LIMITS.ROOM_LIFETIME_MS) {
        this.closeRoom(room, 'lifetime');
      } else if (room.peers.size === 0 && room.outstandingGrants === 0
                 && now - room.emptySince > LIMITS.EMPTY_ROOM_TTL_MS) {
        this.closeRoom(room, 'empty');
      }
    }
  }

  allocatePeerId() {
    const id = this.nextPeerId;
    // Peer ids are u32 and 0 means "broadcast", so wrap to 1 rather than to 0.
    this.nextPeerId = this.nextPeerId >= 0xffffffff ? 1 : this.nextPeerId + 1;
    return id;
  }

  /**
   * Creates a room and the single-use grant that admits its host.
   * @throws {AdmissionError}
   */
  createRoom(spec) {
    this.sweep();

    if (this.rooms.size >= this.maxRooms) {
      throw new AdmissionError(503, 'capacity', 'The relay is at its room limit. Try again shortly.');
    }
    if (this.grants.size >= LIMITS.MAX_OUTSTANDING_GRANTS) {
      throw new AdmissionError(503, 'capacity', 'The relay is busy. Try again shortly.');
    }

    let code = generateRoomCode();
    for (let attempt = 0; this.rooms.has(code) && attempt < 8; attempt += 1) {
      code = generateRoomCode();
    }
    if (this.rooms.has(code)) {
      throw new AdmissionError(503, 'capacity', 'Could not allocate a room code.');
    }

    const room = new Room(this.nextRoomId++, code, { ...spec, now: this.now() });
    this.rooms.set(code, room);
    const grant = this.issueGrant(room, ROLE.HOST);
    return { room, grant };
  }

  issueGrant(room, role) {
    const token = crypto.randomBytes(32).toString('hex');
    this.grants.set(token, {
      roomCode: room.code,
      role,
      expiresAt: this.now() + this.grantTtlMs,
    });
    room.outstandingGrants += 1;
    return token;
  }

  /**
   * Issues a client grant for an existing room after checking the room's policy.
   * @throws {AdmissionError}
   */
  joinRoom(rawCode, spec) {
    this.sweep();

    const code = normalizeRoomCode(rawCode);
    if (code === null) {
      throw new AdmissionError(400, 'bad_request', 'That room code is not valid.');
    }
    const room = this.rooms.get(code);
    if (room === undefined || room.closed) {
      throw new AdmissionError(404, 'room_not_found', 'That room code is not open.');
    }
    if (room.gameProtocol !== spec.gameProtocol || room.contentHash !== spec.contentHash) {
      throw new AdmissionError(409, 'content_mismatch',
        'This room needs the same game version and content as the host.');
    }
    if (room.reservedSeats >= room.maxPeers) {
      throw new AdmissionError(409, 'room_full', 'That room is full.');
    }
    if (this.grants.size >= LIMITS.MAX_OUTSTANDING_GRANTS) {
      throw new AdmissionError(503, 'capacity', 'The relay is busy. Try again shortly.');
    }

    const grant = this.issueGrant(room, ROLE.CLIENT);
    return { room, grant };
  }

  /**
   * Atomically consumes a grant. The entry is removed before anything else happens, so a
   * replay - even one that arrives in the same tick - finds nothing.
   * @returns {{room: Room, role: number}|null}
   */
  consumeGrant(token) {
    if (typeof token !== 'string') return null;
    const grant = this.grants.get(token);
    if (grant === undefined) return null;

    this.grants.delete(token);

    const room = this.rooms.get(grant.roomCode);
    if (room !== undefined && room.outstandingGrants > 0) room.outstandingGrants -= 1;

    if (this.now() >= grant.expiresAt) return null;
    if (room === undefined || room.closed) return null;

    return { room, role: grant.role };
  }

  /** @param {string} reason fixed code: 'host_left' | 'shutdown' | 'lifetime' | 'empty' */
  closeRoom(room, reason) {
    if (room.closed) return;
    room.closed = true;
    this.rooms.delete(room.code);
    room.closeReason = reason;
    this.onRoomClosed(room, reason);
  }

  get roomCount() {
    return this.rooms.size;
  }
}

module.exports = {
  AdmissionError,
  Room,
  RoomStore,
  generateRoomCode,
  normalizeRoomCode,
  PHASE,
  ROLE,
};
