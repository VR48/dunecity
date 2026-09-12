'use strict';

// Wire constants for the DuneCity room relay.
//
// Everything here is defined by docs/room-relay-protocol.md and mirrored by the C++ client in
// include/Network/RoomRelayProtocol.h. Changing a value here without changing both of the
// others is a bug.

const RELAY_PROTOCOL_VERSION = 1;

// Client -> relay message ids (0x01..0x3F).
const C2S = Object.freeze({
  HELLO: 0x01,
  RELAY: 0x02,
  HEARTBEAT: 0x03,
  LEAVE: 0x04,
  ROOM_PHASE: 0x05,
  DIAGNOSTIC: 0x06,
});

// Relay -> client message ids (0x81..0xBF).
const S2C = Object.freeze({
  WELCOME: 0x81,
  PEER_JOINED: 0x82,
  PEER_LEFT: 0x83,
  ROOM_PHASE_CHANGED: 0x84,
  RELAY: 0x85,
  HEARTBEAT_ACK: 0x86,
  ERROR: 0x87,
  ROOM_CLOSED: 0x88,
  DIAGNOSTIC: 0x89,
});

const ROLE = Object.freeze({ HOST: 1, CLIENT: 2 });
const PHASE = Object.freeze({ LOBBY: 1, MATCH: 2 });

const CLOSE = Object.freeze({
  NORMAL: 1000,
  PROTOCOL_ERROR: 4400,
  UNAUTHORIZED: 4401,
  FORBIDDEN: 4403,
  ROOM_NOT_FOUND: 4404,
  TIMEOUT: 4408,
  ROOM_FULL: 4409,
  TOO_LARGE: 4413,
  RATE_LIMITED: 4429,
  SLOW_CONSUMER: 4431,
  HOST_LEFT: 4440,
  SERVER_SHUTDOWN: 4441,
  VERSION_MISMATCH: 4450,
});

const LEAVE_REASON = Object.freeze({
  UNSPECIFIED: 0,
  NORMAL: 1,
  TIMEOUT: 2,
  PROTOCOL_ERROR: 3,
  RATE_LIMITED: 4,
  SLOW_CONSUMER: 5,
  HOST_LEFT: 6,
});

const LIMITS = Object.freeze({
  MAX_FRAME_BYTES: 262144,
  MAX_GAME_PAYLOAD_BYTES: 262128,
  MAX_DIAGNOSTIC_BYTES: 4096,
  MAX_GRANT_CHARS: 64,
  MAX_RUNTIME_CHARS: 16,
  MAX_APP_VERSION_CHARS: 32,
  MAX_CONTENT_HASH_CHARS: 64,
  MAX_NAME_CHARS: 64,
  MAX_ERROR_MESSAGE_CHARS: 200,
  MAX_ROOM_CODE_CHARS: 16,

  MAX_PEERS_PER_ROOM: 8,
  MAX_ROOMS: 64,
  MAX_CONNECTIONS: 256,
  MAX_UNAUTHENTICATED_CONNECTIONS: 32,
  MAX_OUTSTANDING_GRANTS: 512,

  HANDSHAKE_TIMEOUT_MS: 5000,
  LIVENESS_TIMEOUT_MS: 20000,
  HEARTBEAT_INTERVAL_MS: 5000,
  SERVER_PING_INTERVAL_MS: 15000,
  GRANT_TTL_MS: 30000,
  EMPTY_ROOM_TTL_MS: 30000,
  ROOM_LIFETIME_MS: 6 * 60 * 60 * 1000,

  MESSAGES_PER_SECOND: 512,
  BYTES_PER_SECOND: 1536 * 1024,
  BACKPRESSURE_BYTES: 1024 * 1024,
  MAX_SOFT_ERRORS: 32,

  HTTP_MAX_BODY_BYTES: 4096,
  HTTP_PER_ADDRESS_PER_MINUTE: 10,
  // A socket needs a grant, and grants are already rate limited per address at issuance. This
  // is the second line: it bounds the cost of opening sockets at all, valid grant or not.
  SOCKET_PER_ADDRESS_PER_MINUTE: 30,
  HTTP_GLOBAL_PER_MINUTE: 120,
  HTTP_ADDRESS_TABLE_ENTRIES: 4096,
  HTTP_ADDRESS_TABLE_TTL_MS: 10 * 60 * 1000,

  // Ingress bounds. A body cap alone bounds neither how long a client may take to send that
  // body nor how many clients may be part-way through one at the same time.
  HTTP_MAX_SOCKETS: 128,
  HTTP_MAX_HEADER_BYTES: 8192,
  HTTP_MAX_HEADER_COUNT: 64,
  HTTP_BODY_TIMEOUT_MS: 3000,
  HTTP_HEADERS_TIMEOUT_MS: 5000,
  HTTP_REQUEST_TIMEOUT_MS: 10000,
  HTTP_KEEPALIVE_TIMEOUT_MS: 5000,
  HTTP_IDLE_SOCKET_TIMEOUT_MS: 15000,
});

// Game packet ids from include/Network/NetworkPacketTypes.h. Listed here so the relay can
// authorise by declared type without parsing game payloads.
const GAME = Object.freeze({
  CONNECT: 1,
  DISCONNECT: 2,
  PEER_CONNECTED: 3,
  SENDGAMEINFO: 4,
  SENDNAME: 5,
  CHATMESSAGE: 6,
  CHANGEEVENTLIST: 7,
  STARTGAME: 8,
  COMMANDLIST: 9,
  SELECTIONLIST: 10,
  CONFIG_HASH: 11,
  SETPATHBUDGET: 12,
  CLIENTSTATS: 13,
  MOD_INFO: 14,
  MOD_REQUEST: 15,
  MOD_CHUNK: 16,
  MOD_COMPLETE: 17,
  MOD_ACK: 18,
  KEEPALIVE: 19,
  COOP_MISSION: 20,
});

// Relay authorisation matrix, keyed by game packet id.
//   sender:      'any' | 'host' | 'client'
//   phase:       'any' | 'lobby' | 'match'
//   destination: 'any' | 'host'   - where a *client* may address it; a host is unrestricted
// A type that is absent from this table is refused.
//
// sender and phase mirror gameMessageRule() in include/Network/RoomRelayProtocol.h exactly.
// destination comes from the receiving end of the same policy,
// NetworkPacketPolicy::classifyPacket(): a client refuses CHANGEEVENTLIST that did not come
// from the host, and only a host accepts CLIENTSTATS. Carrying those to anybody else would
// produce traffic that every recipient is obliged to refuse. NetworkManager already sends both
// to the host alone, so this refuses nothing the game does.
const GAME_POLICY = new Map([
  [GAME.SENDGAMEINFO, { sender: 'host', phase: 'lobby', destination: 'any' }],
  [GAME.SENDNAME, { sender: 'any', phase: 'lobby', destination: 'any' }],
  [GAME.CHATMESSAGE, { sender: 'any', phase: 'any', destination: 'any' }],
  [GAME.CHANGEEVENTLIST, { sender: 'any', phase: 'lobby', destination: 'host' }],
  [GAME.STARTGAME, { sender: 'host', phase: 'lobby', destination: 'any' }],
  [GAME.COMMANDLIST, { sender: 'any', phase: 'match', destination: 'any' }],
  [GAME.SELECTIONLIST, { sender: 'any', phase: 'match', destination: 'any' }],
  // Both directions are ordinary here: the host broadcasts its hashes and a client sends its
  // own, and classifyPacket accepts a config hash from any peer while the room is a lobby.
  [GAME.CONFIG_HASH, { sender: 'any', phase: 'lobby', destination: 'any' }],
  [GAME.SETPATHBUDGET, { sender: 'host', phase: 'match', destination: 'any' }],
  [GAME.CLIENTSTATS, { sender: 'client', phase: 'match', destination: 'host' }],
  [GAME.KEEPALIVE, { sender: 'any', phase: 'any', destination: 'any' }],
  // Campaign continuation, including the empty settings that mean "exit", arrives after the
  // previous match while the session is still in-game, so it is allowed in both phases.
  [GAME.COOP_MISSION, { sender: 'host', phase: 'any', destination: 'any' }],
]);

// RELAY envelope flags. Bit 0 records that the sender asked for reliable delivery; the
// WebSocket transport is always reliable and ordered, so it is informational. Every other bit
// is undefined, and a frame that sets one is refused rather than forwarded with a meaning the
// relay and the receiver might read differently.
const RELAY_FLAGS = Object.freeze({
  RELIABLE: 0x01,
  DEFINED_MASK: 0x01,
});
const MAX_CHANNEL = 1;

// Room codes: Crockford base32 without I, L, O and U.
const ROOM_CODE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const ROOM_CODE_LENGTH = 12;

const DIAGNOSTIC_KIND = Object.freeze({
  STATE_DIGEST: 1,
  LOCKSTEP_STALL: 2,
});

module.exports = {
  RELAY_PROTOCOL_VERSION,
  C2S,
  S2C,
  ROLE,
  PHASE,
  CLOSE,
  LEAVE_REASON,
  LIMITS,
  GAME,
  GAME_POLICY,
  RELAY_FLAGS,
  MAX_CHANNEL,
  ROOM_CODE_ALPHABET,
  ROOM_CODE_LENGTH,
  DIAGNOSTIC_KIND,
};
