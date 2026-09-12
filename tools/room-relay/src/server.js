'use strict';

const http = require('node:http');
const { WebSocket, WebSocketServer } = require('ws');

const {
  C2S,
  CLOSE,
  LEAVE_REASON,
  LIMITS,
  PHASE,
  ROLE,
  GAME_POLICY,
  RELAY_PROTOCOL_VERSION,
} = require('./constants');
const protocol = require('./protocol');
const { ProtocolError } = protocol;
const { RoomStore } = require('./rooms');
const { WindowCounter, BoundedRateTable } = require('./limits');
const { LifecycleLog } = require('./logging');
const { NULL_LIFECYCLE } = require('./analytics');
const { createAdmissionHandler, clientAddress } = require('./admission');

const DEFAULT_CONFIG = {
  host: '127.0.0.1',
  port: 8787,
  app: 'dunecity',
  socketPath: '/v1/socket',
  publicSocketUrl: 'ws://127.0.0.1:8787/v1/socket',
  observedTransport: 'ws',
  allowedOrigins: [],
  trustForwardedFor: false,
  requiredGameProtocol: 0,
  maxConnections: LIMITS.MAX_CONNECTIONS,
  maxRooms: LIMITS.MAX_ROOMS,
  grantTtlMs: LIMITS.GRANT_TTL_MS,
  handshakeTimeoutMs: LIMITS.HANDSHAKE_TIMEOUT_MS,
  livenessTimeoutMs: LIMITS.LIVENESS_TIMEOUT_MS,
  messagesPerSecond: LIMITS.MESSAGES_PER_SECOND,
  bytesPerSecond: LIMITS.BYTES_PER_SECOND,
  backpressureBytes: LIMITS.BACKPRESSURE_BYTES,
  maxSoftErrors: LIMITS.MAX_SOFT_ERRORS,
  maxHttpSockets: LIMITS.HTTP_MAX_SOCKETS,
  httpBodyTimeoutMs: LIMITS.HTTP_BODY_TIMEOUT_MS,
  httpHeadersTimeoutMs: LIMITS.HTTP_HEADERS_TIMEOUT_MS,
  httpRequestTimeoutMs: LIMITS.HTTP_REQUEST_TIMEOUT_MS,
  httpKeepAliveTimeoutMs: LIMITS.HTTP_KEEPALIVE_TIMEOUT_MS,
  httpIdleSocketTimeoutMs: LIMITS.HTTP_IDLE_SOCKET_TIMEOUT_MS,
  log: undefined,
  /** Optional analytics sink; NULL_LIFECYCLE when the operator has not configured one. */
  lifecycle: undefined,
};

/** One WebSocket connection. Everything the relay trusts about a peer lives here. */
class Connection {
  constructor(ws, address, now, budgets) {
    this.ws = ws;
    this.address = address;
    this.peerId = 0;
    this.role = 0;
    this.room = null;
    this.displayName = '';
    this.runtime = '';
    this.appVersion = '';
    this.authenticated = false;
    this.removed = false;
    this.joinedAt = 0;
    this.connectedAt = now;
    this.lastSeen = now;
    this.softErrors = 0;
    /** Peers this connection has been told about, so PEER_LEFT is emitted exactly once. */
    this.announced = new Set();
    this.messageBudget = new WindowCounter(budgets.messagesPerSecond, 1000);
    this.byteBudget = new WindowCounter(budgets.bytesPerSecond, 1000);
  }

  get isHost() {
    return this.role === ROLE.HOST;
  }
}

function createRelay(userConfig = {}) {
  const config = { ...DEFAULT_CONFIG, ...userConfig };
  if (config.allowedOrigins.includes('null')) {
    throw new Error("'null' is not an acceptable Origin; remove it from allowedOrigins");
  }

  const now = config.now || (() => Date.now());
  const log = config.log || new LifecycleLog({ enabled: config.logEnabled !== false });
  // Lifecycle delivery is driven by explicit calls, not by the diagnostic log, so the log's
  // rate limiting can never suppress an API event.
  const lifecycle = config.lifecycle || NULL_LIFECYCLE;
  const store = new RoomStore({
    now,
    maxRooms: config.maxRooms,
    grantTtlMs: config.grantTtlMs,
    // Fires once per room whether the host left, the relay stopped, or the reaper collected it.
    onRoomClosed: (room, reason) => lifecycle.roomClosed({ roomLogId: room.logId, reason }),
    // The reaper defers to the full close below, so an expired room disconnects the peers that
    // are still in it instead of silently disappearing from the room table underneath them.
    onRoomExpired: (room, reason) => {
      if (reason === 'lifetime') {
        closeRoom(room, CLOSE.TIMEOUT, 'This room reached its time limit.', 'lifetime');
      } else {
        closeRoom(room, CLOSE.NORMAL, 'This room expired.', 'empty');
      }
    },
  });

  /** @type {Set<Connection>} */
  const connections = new Set();

  const addressLimiter = new BoundedRateTable({
    limit: LIMITS.HTTP_PER_ADDRESS_PER_MINUTE,
    windowMs: 60000,
    maxEntries: LIMITS.HTTP_ADDRESS_TABLE_ENTRIES,
    ttlMs: LIMITS.HTTP_ADDRESS_TABLE_TTL_MS,
  });
  const globalLimiter = new WindowCounter(LIMITS.HTTP_GLOBAL_PER_MINUTE, 60000);

  const ctx = {
    store,
    log,
    lifecycle,
    config,
    now,
    addressLimiter,
    globalLimiter,
    connectionCount: () => connections.size,
  };

  // Ingress deadlines. Node only enforces headersTimeout/requestTimeout when its connection
  // check ticks, so the interval has to be short enough for the deadlines to mean anything.
  const headersTimeout = Math.min(config.httpHeadersTimeoutMs, config.httpRequestTimeoutMs);
  const httpServer = http.createServer({
    headersTimeout,
    requestTimeout: config.httpRequestTimeoutMs,
    keepAliveTimeout: config.httpKeepAliveTimeoutMs,
    connectionsCheckingInterval: Math.max(50, Math.floor(headersTimeout / 2)),
    maxHeaderSize: LIMITS.HTTP_MAX_HEADER_BYTES,
  }, createAdmissionHandler(ctx));
  httpServer.maxHeadersCount = LIMITS.HTTP_MAX_HEADER_COUNT;
  httpServer.setTimeout(config.httpIdleSocketTimeoutMs);
  httpServer.on('timeout', (socket) => socket.destroy());

  /**
   * Sockets that have not become WebSocket connections. Admission requests are short, so a
   * client occupying one of these for long is either broken or trying to hold the door open;
   * either way there is a hard ceiling on how many can do it at once. Upgraded sockets leave
   * this set and are governed by maxConnections instead.
   */
  const httpSockets = new Set();
  httpServer.on('connection', (socket) => {
    if (httpSockets.size >= config.maxHttpSockets) {
      log.emit('connection_denied', {
        code: CLOSE.RATE_LIMITED,
        reason: 'http_socket_cap',
        addressTag: log.addressTag(socket.remoteAddress),
      });
      socket.destroy();
      return;
    }
    httpSockets.add(socket);
    socket.on('close', () => httpSockets.delete(socket));
  });

  const wss = new WebSocketServer({
    noServer: true,
    maxPayload: LIMITS.MAX_FRAME_BYTES,
    perMessageDeflate: false,
  });

  // --- sending -----------------------------------------------------------------------------

  function sendFrame(conn, frame) {
    if (conn.ws.readyState !== WebSocket.OPEN) return false;
    conn.ws.send(frame, { binary: true });
    return true;
  }

  function closeConnection(conn, code, message, reason) {
    if (conn.ws.readyState === WebSocket.OPEN) {
      if (code !== CLOSE.NORMAL) {
        sendFrame(conn, protocol.encodeError(code, message));
      }
      conn.ws.close(code, String(message).slice(0, 100));
    } else {
      conn.ws.terminate();
    }
    detach(conn, reason === undefined ? LEAVE_REASON.UNSPECIFIED : reason, message);
  }

  function softError(conn, code, message) {
    conn.softErrors += 1;
    sendFrame(conn, protocol.encodeError(code, message));
    log.emit('message_refused', {
      room: conn.room ? conn.room.logId : undefined,
      peerId: conn.peerId,
      code,
      detail: message,
    });
    if (conn.softErrors > config.maxSoftErrors) {
      closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Too many refused messages.',
        LEAVE_REASON.PROTOCOL_ERROR);
    }
  }

  /**
   * Routes one frame to a recipient, honouring backpressure. A recipient whose socket buffer has
   * run away is disconnected rather than being quietly skipped: dropping a lockstep command
   * desynchronises the match, which is worse than an honest disconnect.
   */
  function deliver(recipient, frame) {
    if (recipient.ws.readyState !== WebSocket.OPEN) return;
    if (recipient.ws.bufferedAmount > config.backpressureBytes) {
      closeConnection(recipient, CLOSE.SLOW_CONSUMER,
        'This player fell too far behind the match.', LEAVE_REASON.SLOW_CONSUMER);
      return;
    }
    sendFrame(recipient, frame);
  }

  // --- membership --------------------------------------------------------------------------

  function announceJoin(room, joiner) {
    const joinerFrame = protocol.encodePeerJoined({
      peerId: joiner.peerId,
      role: joiner.role,
      displayName: joiner.displayName,
      runtime: joiner.runtime,
    });

    for (const existing of room.peers.values()) {
      if (existing === joiner) continue;

      if (!joiner.announced.has(existing.peerId)) {
        joiner.announced.add(existing.peerId);
        sendFrame(joiner, protocol.encodePeerJoined({
          peerId: existing.peerId,
          role: existing.role,
          displayName: existing.displayName,
          runtime: existing.runtime,
        }));
      }
      if (!existing.announced.has(joiner.peerId)) {
        existing.announced.add(joiner.peerId);
        sendFrame(existing, joinerFrame);
      }
    }
  }

  function announceLeave(room, leaver, reason) {
    const frame = protocol.encodePeerLeft(leaver.peerId, reason);
    for (const other of room.peers.values()) {
      if (other === leaver) continue;
      if (other.announced.delete(leaver.peerId)) {
        sendFrame(other, frame);
      }
    }
  }

  function closeRoom(room, code, message, reason) {
    if (room.closed) return;
    const members = [...room.peers.values()];
    // The store owns the single 'closed' lifecycle event, so the reaper path reports it too.
    store.closeRoom(room, reason);
    log.emit('room_closed', { room: room.logId, code, reasonCode: reason, peers: members.length });

    for (const member of members) {
      room.peers.delete(member.peerId);
      member.room = null;
      member.removed = true;
      if (member.ws.readyState === WebSocket.OPEN) {
        sendFrame(member, protocol.encodeRoomClosed(code, message));
        member.ws.close(code, String(message).slice(0, 100));
      } else {
        member.ws.terminate();
      }
      log.emit('participant_left', {
        room: room.logId,
        peerId: member.peerId,
        role: member.isHost ? 'host' : 'client',
        runtime: member.runtime,
        appVersion: member.appVersion,
        reasonCode: reason,
        transport: config.observedTransport,
        runtimeMs: member.joinedAt ? now() - member.joinedAt : 0,
      });
      lifecycle.participantLeft({
        roomLogId: room.logId,
        participantId: member.peerId,
        runtime: member.runtime,
        appVersion: member.appVersion,
        reason,
      });
    }
  }

  function detach(conn, reason, detail) {
    if (conn.removed) return;
    conn.removed = true;

    const room = conn.room;
    conn.room = null;
    if (room === null) return;

    room.peers.delete(conn.peerId);
    if (room.peers.size === 0) room.emptySince = now();
    // Remember a bounded number of departed ids so that a peer which addresses somebody who
    // just left gets a dropped message rather than being disconnected for forgery.
    if (room.formerPeerIds.size < 64) room.formerPeerIds.add(conn.peerId);

    log.emit('participant_left', {
      room: room.logId,
      peerId: conn.peerId,
      role: conn.isHost ? 'host' : 'client',
      runtime: conn.runtime,
      appVersion: conn.appVersion,
      reasonCode: reason,
      transport: config.observedTransport,
      runtimeMs: conn.joinedAt ? now() - conn.joinedAt : 0,
    });
    lifecycle.participantLeft({
      roomLogId: room.logId,
      participantId: conn.peerId,
      runtime: conn.runtime,
      appVersion: conn.appVersion,
      reason,
    });

    announceLeave(room, conn, reason);

    if (conn.isHost && !room.closed) {
      // The host owns the room: without it there is nobody to run the lobby or the match.
      closeRoom(room, CLOSE.HOST_LEFT, 'The host left the game.', 'host_left');
    }
  }

  // --- message handling ----------------------------------------------------------------------

  function handleHello(conn, msg) {
    if (conn.authenticated) {
      closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Duplicate handshake.',
        LEAVE_REASON.PROTOCOL_ERROR);
      return;
    }

    const admitted = store.consumeGrant(msg.grant);
    if (admitted === null) {
      log.emit('connection_denied', {
        code: CLOSE.UNAUTHORIZED,
        reason: 'grant',
        addressTag: log.addressTag(conn.address),
      });
      closeConnection(conn, CLOSE.UNAUTHORIZED, 'That invitation is no longer valid.');
      return;
    }

    const room = admitted.room;
    if (room.peers.size >= room.maxPeers) {
      closeConnection(conn, CLOSE.ROOM_FULL, 'That room is full.');
      return;
    }
    if (admitted.role === ROLE.HOST && room.hostPeerId !== 0) {
      closeConnection(conn, CLOSE.FORBIDDEN, 'That room already has a host.');
      return;
    }
    if (msg.gameProtocol !== room.gameProtocol) {
      closeConnection(conn, CLOSE.VERSION_MISMATCH, 'This room expects another game version.');
      return;
    }
    for (const existing of room.peers.values()) {
      if (existing.displayName === msg.displayName) {
        // The game resolves a command list to a player by name, so two players in one room can
        // never share one. This is the only place that sees both names before the game starts.
        closeConnection(conn, CLOSE.FORBIDDEN, 'That player name is already used in this game.');
        return;
      }
    }

    conn.authenticated = true;
    conn.role = admitted.role;
    conn.room = room;
    conn.peerId = store.allocatePeerId();
    conn.displayName = msg.displayName;
    conn.runtime = msg.runtime;
    conn.appVersion = msg.appVersion;
    conn.joinedAt = now();

    room.peers.set(conn.peerId, conn);
    if (conn.isHost) room.hostPeerId = conn.peerId;

    sendFrame(conn, protocol.encodeWelcome({
      gameProtocol: room.gameProtocol,
      peerId: conn.peerId,
      role: conn.role,
      roomCode: room.code,
      maxPeers: room.maxPeers,
      phase: room.phase,
    }));

    announceJoin(room, conn);

    log.emit('participant_joined', {
      room: room.logId,
      peerId: conn.peerId,
      role: conn.isHost ? 'host' : 'client',
      runtime: conn.runtime,
      appVersion: conn.appVersion,
      transport: config.observedTransport,
      addressTag: log.addressTag(conn.address),
    });
    lifecycle.participantJoined({
      roomLogId: room.logId,
      participantId: conn.peerId,
      runtime: conn.runtime,
      appVersion: conn.appVersion,
    });
  }

  function handleRelay(conn, msg) {
    const room = conn.room;
    const policy = GAME_POLICY.get(msg.gameMessageType);
    if (policy === undefined) {
      softError(conn, CLOSE.FORBIDDEN, `Message type ${msg.gameMessageType} is not carried by the relay.`);
      return;
    }
    if (policy.sender === 'host' && !conn.isHost) {
      softError(conn, CLOSE.FORBIDDEN, 'Only the host may send that.');
      return;
    }
    if (policy.sender === 'client' && conn.isHost) {
      softError(conn, CLOSE.FORBIDDEN, 'The host may not send that.');
      return;
    }
    if (policy.phase === 'lobby' && room.phase !== PHASE.LOBBY) {
      softError(conn, CLOSE.FORBIDDEN, 'That message belongs to the lobby.');
      return;
    }
    if (policy.phase === 'match' && room.phase !== PHASE.MATCH) {
      softError(conn, CLOSE.FORBIDDEN, 'That message belongs to a running match.');
      return;
    }

    const frame = protocol.encodeRelay(conn.peerId, msg);

    if (msg.recipient === 0) {
      for (const other of room.peers.values()) {
        if (other !== conn) deliver(other, frame);
      }
      return;
    }

    const recipient = room.peers.get(msg.recipient);
    if (recipient === undefined) {
      // A recipient that was in this room and has since left is an ordinary race; anything
      // else is a client naming a peer it has no business knowing about.
      if (room.formerPeerIds.has(msg.recipient)) {
        softError(conn, CLOSE.FORBIDDEN, 'That player already left.');
      } else {
        closeConnection(conn, CLOSE.FORBIDDEN, 'That recipient is not in this room.',
          LEAVE_REASON.PROTOCOL_ERROR);
      }
      return;
    }
    if (recipient === conn) {
      softError(conn, CLOSE.FORBIDDEN, 'A message cannot be addressed to its own sender.');
      return;
    }
    deliver(recipient, frame);
  }

  function handleRoomPhase(conn, msg) {
    if (!conn.isHost) {
      softError(conn, CLOSE.FORBIDDEN, 'Only the host may change the room phase.');
      return;
    }
    const room = conn.room;
    // The store owns the phase, the epoch that invalidates lobby grants, and the flag that
    // stops a started room from ever admitting somebody new.
    if (!store.setRoomPhase(room, msg.phase)) return;
    const frame = protocol.encodeRoomPhaseChanged(room.phase, conn.peerId);
    for (const other of room.peers.values()) {
      if (other !== conn) deliver(other, frame);
    }
    log.emit('room_phase', { room: room.logId, phase: room.phase, byPeerId: conn.peerId });
    // 'started' is the lobby -> match transition only; a return to the lobby is not an event.
    if (room.phase === PHASE.MATCH) lifecycle.matchStarted({ roomLogId: room.logId });
  }

  function handleDiagnostic(conn, msg) {
    const frame = protocol.encodeDiagnostic(conn.peerId, msg.kind, msg.payload);
    for (const other of conn.room.peers.values()) {
      if (other !== conn) deliver(other, frame);
    }
  }

  function handleMessage(conn, data, isBinary) {
    const at = now();
    conn.lastSeen = at;

    if (!isBinary) {
      closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Binary frames only.',
        LEAVE_REASON.PROTOCOL_ERROR);
      return;
    }

    const frame = Buffer.isBuffer(data) ? data : Buffer.from(data);

    if (!conn.messageBudget.allow(at, 1) || !conn.byteBudget.allow(at, frame.length)) {
      closeConnection(conn, CLOSE.RATE_LIMITED, 'Too many messages.', LEAVE_REASON.RATE_LIMITED);
      return;
    }

    let msg;
    try {
      msg = protocol.decodeClientMessage(frame);
    } catch (err) {
      const code = err instanceof ProtocolError ? err.closeCode : CLOSE.PROTOCOL_ERROR;
      closeConnection(conn, code, err.message, LEAVE_REASON.PROTOCOL_ERROR);
      return;
    }

    if (!conn.authenticated) {
      if (msg.type !== C2S.HELLO) {
        closeConnection(conn, CLOSE.UNAUTHORIZED, 'The first message must be the handshake.');
        return;
      }
      handleHello(conn, msg);
      return;
    }

    if (conn.room === null || conn.room.closed) {
      closeConnection(conn, CLOSE.ROOM_NOT_FOUND, 'That room is no longer open.');
      return;
    }

    switch (msg.type) {
      case C2S.HELLO:
        closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Duplicate handshake.',
          LEAVE_REASON.PROTOCOL_ERROR);
        break;
      case C2S.RELAY:
        handleRelay(conn, msg);
        break;
      case C2S.HEARTBEAT:
        sendFrame(conn, protocol.encodeHeartbeatAck(msg.clientTimeMs, at >>> 0));
        break;
      case C2S.LEAVE:
        closeConnection(conn, CLOSE.NORMAL, 'left', LEAVE_REASON.NORMAL);
        break;
      case C2S.ROOM_PHASE:
        handleRoomPhase(conn, msg);
        break;
      case C2S.DIAGNOSTIC:
        handleDiagnostic(conn, msg);
        break;
      default:
        closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Unhandled message.',
          LEAVE_REASON.PROTOCOL_ERROR);
        break;
    }
  }

  // --- connection lifecycle -------------------------------------------------------------------

  function rejectUpgrade(socket, status, text) {
    socket.write(`HTTP/1.1 ${status} ${text}\r\nconnection: close\r\ncontent-length: 0\r\n\r\n`);
    socket.destroy();
  }

  httpServer.on('upgrade', (req, socket, head) => {
    const url = (req.url || '').split('?')[0];
    const address = clientAddress(req, config.trustForwardedFor);

    // A game socket is long-lived and idle between heartbeats: it belongs to the WebSocket
    // budget from here on, not to the admission one.
    httpSockets.delete(socket);
    socket.setTimeout(0);

    if (url !== config.socketPath) {
      rejectUpgrade(socket, 404, 'Not Found');
      return;
    }

    const origin = req.headers.origin;
    if (origin !== undefined
        && (typeof origin !== 'string' || !config.allowedOrigins.includes(origin))) {
      log.emit('connection_denied', {
        code: CLOSE.FORBIDDEN,
        reason: 'origin',
        addressTag: log.addressTag(address),
      });
      rejectUpgrade(socket, 403, 'Forbidden');
      return;
    }

    if (connections.size >= config.maxConnections) {
      log.emit('connection_denied', {
        code: CLOSE.ROOM_FULL,
        reason: 'connection_cap',
        addressTag: log.addressTag(address),
      });
      rejectUpgrade(socket, 503, 'Service Unavailable');
      return;
    }

    let pending = 0;
    for (const conn of connections) {
      if (!conn.authenticated) pending += 1;
    }
    if (pending >= LIMITS.MAX_UNAUTHENTICATED_CONNECTIONS) {
      log.emit('connection_denied', {
        code: CLOSE.RATE_LIMITED,
        reason: 'handshake_backlog',
        addressTag: log.addressTag(address),
      });
      rejectUpgrade(socket, 503, 'Service Unavailable');
      return;
    }

    wss.handleUpgrade(req, socket, head, (ws) => {
      wss.emit('connection', ws, req);
    });
  });

  wss.on('connection', (ws, req) => {
    const conn = new Connection(ws, clientAddress(req, config.trustForwardedFor), now(), config);
    connections.add(conn);

    ws.on('message', (data, isBinary) => {
      try {
        handleMessage(conn, data, isBinary);
      } catch (err) {
        closeConnection(conn, CLOSE.PROTOCOL_ERROR, 'Internal routing failure.',
          LEAVE_REASON.PROTOCOL_ERROR);
      }
    });

    ws.on('pong', () => { conn.lastSeen = now(); });

    ws.on('close', () => {
      detach(conn, LEAVE_REASON.NORMAL, 'socket closed');
      connections.delete(conn);
    });

    ws.on('error', () => {
      detach(conn, LEAVE_REASON.PROTOCOL_ERROR, 'socket error');
      connections.delete(conn);
      try { ws.terminate(); } catch { /* already gone */ }
    });
  });

  const sweepTimer = setInterval(() => {
    const at = now();
    for (const conn of connections) {
      if (!conn.authenticated) {
        if (at - conn.connectedAt > config.handshakeTimeoutMs) {
          closeConnection(conn, CLOSE.TIMEOUT, 'The handshake took too long.',
            LEAVE_REASON.TIMEOUT);
        }
        continue;
      }
      if (at - conn.lastSeen > config.livenessTimeoutMs) {
        closeConnection(conn, CLOSE.TIMEOUT, 'This player stopped responding.',
          LEAVE_REASON.TIMEOUT);
      } else if (conn.ws.readyState === WebSocket.OPEN) {
        conn.ws.ping();
      }
    }
    store.sweep();
  }, Math.max(250, Math.min(config.handshakeTimeoutMs, LIMITS.SERVER_PING_INTERVAL_MS) / 4));
  if (sweepTimer.unref) sweepTimer.unref();

  async function start() {
    await new Promise((resolve, reject) => {
      httpServer.once('error', reject);
      httpServer.listen(config.port, config.host, () => {
        httpServer.removeListener('error', reject);
        resolve();
      });
    });
    const bound = httpServer.address();
    log.emit('relay_started', {
      host: config.host,
      port: bound && bound.port ? bound.port : config.port,
      protocol: RELAY_PROTOCOL_VERSION,
      transport: config.observedTransport,
    });
    return bound;
  }

  async function stop() {
    clearInterval(sweepTimer);
    for (const room of [...store.rooms.values()]) {
      closeRoom(room, CLOSE.SERVER_SHUTDOWN, 'The relay is restarting.', 'shutdown');
    }
    for (const conn of [...connections]) {
      try {
        if (conn.ws.readyState === WebSocket.OPEN) {
          conn.ws.send(protocol.encodeRoomClosed(CLOSE.SERVER_SHUTDOWN, 'The relay is restarting.'),
            { binary: true });
          conn.ws.close(CLOSE.SERVER_SHUTDOWN, 'shutdown');
        } else {
          conn.ws.terminate();
        }
      } catch { /* already gone */ }
    }
    connections.clear();
    // Admission sockets have no protocol-level goodbye, and an idle one would otherwise keep
    // httpServer.close() waiting for as long as the client felt like holding it.
    for (const socket of httpSockets) {
      try { socket.destroy(); } catch { /* already gone */ }
    }
    httpSockets.clear();
    await new Promise((resolve) => wss.close(resolve));
    await new Promise((resolve) => httpServer.close(resolve));
    // Last, so that the shutdown 'left'/'closed' events above get their bounded chance to
    // leave. A publisher that cannot reach the receiver must not delay the exit.
    if (typeof lifecycle.stop === 'function') {
      try {
        await lifecycle.stop();
      } catch { /* delivery never blocks shutdown */ }
    }
    log.emit('relay_stopped', {});
  }

  return {
    config,
    store,
    log,
    lifecycle,
    httpServer,
    wss,
    connections,
    start,
    stop,
    get httpSocketCount() {
      return httpSockets.size;
    },
    get port() {
      const bound = httpServer.address();
      return bound && bound.port ? bound.port : config.port;
    },
  };
}

module.exports = { createRelay, Connection, DEFAULT_CONFIG };
