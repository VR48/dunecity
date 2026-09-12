/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Network/RoomRelayClient.h>

#include <algorithm>

namespace {
/// How long past the relay's own liveness deadline we wait before declaring the session dead.
/// A hidden browser tab that stops being scheduled must surface as a visible timeout rather
/// than as a match that quietly stops progressing.
constexpr Uint32 kLivenessGraceMs = 5000;
} // namespace

RoomRelayClient::RoomRelayClient() = default;

RoomRelayClient::~RoomRelayClient() {
    if(socket_ && status_ != Status::Closed) {
        socket_->close(RoomRelay::Close::Normal, "leaving");
    }
}

bool RoomRelayClient::start(const Config& config, std::string& error) {
    config_ = config;
    peers_.clear();
    events_.clear();
    helloSent_ = false;
    localPeerId_ = 0;
    localRole_ = RoomRelay::Role::Unknown;
    phase_ = RoomRelay::Phase::Lobby;
    closeCode_ = 0;
    roundTripMs_ = 0;
    refusalsFromRelay_ = 0;
    roomCode_.clear();

    if(!isAcceptableRelayUrl(config.socketUrl, config.allowLoopbackPlaintext, error)) {
        status_ = Status::Closed;
        statusMessage_ = error;
        return false;
    }
    if(!RoomRelay::isAcceptableGrant(config.grant)) {
        error = "That invitation could not be used.";
        status_ = Status::Closed;
        statusMessage_ = error;
        return false;
    }
    if(!RoomRelay::isAcceptableDisplayName(config.displayName)) {
        error = "That player name cannot be used online.";
        status_ = Status::Closed;
        statusMessage_ = error;
        return false;
    }

    const RelayWebSocketSupport support = relayWebSocketSupport();
    if(!support.available) {
        error = support.reason.empty()
              ? std::string("Online play is not available on this computer.") : support.reason;
        status_ = Status::Closed;
        statusMessage_ = error;
        return false;
    }

    socket_ = createRelayWebSocket(config.socketUrl, config.origin);
    if(!socket_) {
        error = "The game could not open a connection to the game service.";
        status_ = Status::Closed;
        statusMessage_ = error;
        return false;
    }

    const Uint32 now = SDL_GetTicks();
    lastFrameReceived_ = now;
    lastHeartbeatSent_ = now;
    status_ = Status::Connecting;
    statusMessage_ = "Connecting to the game service...";
    return true;
}

void RoomRelayClient::stop(std::uint8_t reason) {
    if(!socket_) {
        status_ = Status::Closed;
        return;
    }
    if(status_ == Status::Joined || status_ == Status::Handshaking) {
        socket_->send(RoomRelay::encodeLeave(reason));
        socket_->pump();
    }
    socket_->close(RoomRelay::Close::Normal, "leaving");
    socket_.reset();
    status_ = Status::Closed;
}

bool RoomRelayClient::sendFrame(const std::vector<std::uint8_t>& frame) {
    if(!socket_ || status_ == Status::Closed) {
        return false;
    }
    if(!socket_->send(frame)) {
        finish(RoomRelay::Close::Normal, socket_->lastError().empty()
            ? std::string("The connection to the game was lost.") : socket_->lastError());
        return false;
    }
    return true;
}

void RoomRelayClient::pushEvent(Event&& event) {
    if(events_.size() >= kMaxQueuedEvents) {
        // The game loop is not draining. Ending the session is the honest outcome: dropping
        // queued gameplay events would desynchronise the match instead.
        finish(RoomRelay::Close::SlowConsumer, "This computer fell too far behind the game.");
        return;
    }
    events_.push_back(std::move(event));
}

void RoomRelayClient::finish(std::uint16_t code, const std::string& message) {
    if(status_ == Status::Closed) {
        return;
    }
    status_ = Status::Closed;
    closeCode_ = (code != 0) ? code : closeCode_;
    statusMessage_ = message;

    Event event;
    event.type = Event::Type::Closed;
    event.code = closeCode_;
    event.message = message;
    if(events_.size() < kMaxQueuedEvents) {
        events_.push_back(std::move(event));
    }

    if(socket_) {
        socket_->close(code, message);
    }
}

void RoomRelayClient::update() {
    if(!socket_ || status_ == Status::Closed) {
        return;
    }

    socket_->pump();

    if(socket_->state() == RelayWebSocket::State::Open && !helloSent_) {
        RoomRelay::HelloFields fields;
        fields.gameProtocolVersion = config_.gameProtocolVersion;
        fields.grant       = config_.grant;
        fields.runtime     = config_.runtime;
        fields.appVersion  = config_.appVersion;
        fields.contentHash = config_.contentHash;
        fields.displayName = config_.displayName;

        std::vector<std::uint8_t> hello;
        if(!RoomRelay::encodeHello(fields, hello)) {
            finish(RoomRelay::Close::ProtocolError,
                   "The game could not introduce itself to the game service.");
            return;
        }
        helloSent_ = true;
        status_ = Status::Handshaking;
        statusMessage_ = "Joining the game...";
        if(!sendFrame(hello)) {
            return;
        }
        // The grant is single use and now spent; keeping a copy has no value and some risk.
        config_.grant.clear();
    }

    std::vector<std::uint8_t> frame;
    while(socket_ && status_ != Status::Closed && socket_->receive(frame)) {
        lastFrameReceived_ = SDL_GetTicks();
        handleFrame(frame);
    }

    if(!socket_ || status_ == Status::Closed) {
        return;
    }

    if(socket_->state() == RelayWebSocket::State::Closed) {
        const std::uint16_t code = socket_->closeCode();
        std::string message = socket_->lastError();
        if(message.empty()) {
            message = RoomRelay::describeCloseCode(code);
        }
        finish(code != 0 ? code : RoomRelay::Close::Normal, message);
        return;
    }

    const Uint32 now = SDL_GetTicks();

    if(status_ == Status::Joined && (now - lastHeartbeatSent_) >= heartbeatIntervalMs_) {
        lastHeartbeatSent_ = now;
        sendFrame(RoomRelay::encodeHeartbeat(now));
    }

    // A relay that has stopped answering has to become visible, not silent. The deadline is the
    // relay's own liveness window plus a grace period, so a normally scheduled client never
    // trips it.
    if(status_ != Status::Connecting
       && (now - lastFrameReceived_) > (livenessTimeoutMs_ + kLivenessGraceMs)) {
        finish(RoomRelay::Close::Timeout, "The game service stopped responding.");
    }
}

bool RoomRelayClient::pollEvent(Event& event) {
    if(events_.empty()) {
        return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

RoomRelayClient::Peer* RoomRelayClient::findPeer(std::uint32_t peerId) {
    for(Peer& peer : peers_) {
        if(peer.id == peerId) {
            return &peer;
        }
    }
    return nullptr;
}

const RoomRelayClient::Peer* RoomRelayClient::findPeer(std::uint32_t peerId) const {
    for(const Peer& peer : peers_) {
        if(peer.id == peerId) {
            return &peer;
        }
    }
    return nullptr;
}

void RoomRelayClient::handleFrame(const std::vector<std::uint8_t>& frame) {
    RoomRelay::ServerFrame decoded;
    std::string error;
    if(!RoomRelay::decodeServerFrame(frame.data(), frame.size(), decoded, error)) {
        finish(RoomRelay::Close::ProtocolError, "The game service sent something unexpected.");
        return;
    }

    switch(decoded.type) {
        case RoomRelay::ServerMessage::Welcome:
            handleWelcome(decoded);
            break;

        case RoomRelay::ServerMessage::PeerJoined:
            handlePeerJoined(decoded);
            break;

        case RoomRelay::ServerMessage::PeerLeft:
            handlePeerLeft(decoded);
            break;

        case RoomRelay::ServerMessage::RoomPhaseChanged: {
            phase_ = decoded.phase;
            Event event;
            event.type   = Event::Type::PhaseChanged;
            event.phase  = decoded.phase;
            event.peerId = decoded.byPeerId;
            pushEvent(std::move(event));
        } break;

        case RoomRelay::ServerMessage::Relay:
            handleRelayPayload(decoded);
            break;

        case RoomRelay::ServerMessage::Diagnostic: {
            if(findPeer(decoded.senderPeerId) == nullptr) {
                return;     // a diagnostic from somebody who is not in the room is not usable
            }
            Event event;
            event.type           = Event::Type::Diagnostic;
            event.peerId         = decoded.senderPeerId;
            event.diagnosticKind = decoded.diagnosticKind;
            event.payload        = std::move(decoded.payload);
            pushEvent(std::move(event));
        } break;

        case RoomRelay::ServerMessage::HeartbeatAck: {
            const Uint32 now = SDL_GetTicks();
            if(decoded.clientEchoMs != 0 && now >= decoded.clientEchoMs) {
                roundTripMs_ = now - decoded.clientEchoMs;
            }
        } break;

        case RoomRelay::ServerMessage::Error: {
            refusalsFromRelay_++;
            Event event;
            event.type    = Event::Type::Refused;
            event.code    = decoded.code;
            event.message = decoded.message;
            pushEvent(std::move(event));
            if(refusalsFromRelay_ >= kMaxRelayRefusals) {
                finish(RoomRelay::Close::ProtocolError,
                       "The game and the game service disagreed too many times.");
            }
        } break;

        case RoomRelay::ServerMessage::RoomClosed: {
            const std::string message = decoded.message.empty()
                ? std::string(RoomRelay::describeCloseCode(decoded.code)) : decoded.message;
            finish(decoded.code, message);
        } break;

        default:
            finish(RoomRelay::Close::ProtocolError, "The game service sent something unexpected.");
            break;
    }
}

void RoomRelayClient::handleWelcome(const RoomRelay::ServerFrame& frame) {
    if(status_ != Status::Handshaking) {
        finish(RoomRelay::Close::ProtocolError, "The game service sent something unexpected.");
        return;
    }
    if(frame.relayProtocolVersion != RoomRelay::kProtocolVersion) {
        finish(RoomRelay::Close::VersionMismatch,
               "This version of the game cannot use that game service.");
        return;
    }
    if(frame.gameProtocolVersion != config_.gameProtocolVersion) {
        finish(RoomRelay::Close::VersionMismatch,
               "That game was created by a different version of Dune City.");
        return;
    }

    localPeerId_ = frame.peerId;
    localRole_   = frame.role;
    roomCode_    = frame.roomCode;
    maxPeers_    = frame.maxPeers;
    phase_       = frame.phase;

    // The relay tells us its own deadlines; clamp them so a hostile answer cannot disable the
    // client-side liveness check or make it fire constantly.
    heartbeatIntervalMs_ = std::min<Uint32>(std::max<Uint32>(frame.heartbeatIntervalMs, 1000),
                                            30000);
    livenessTimeoutMs_   = std::min<Uint32>(std::max<Uint32>(frame.livenessTimeoutMs, 5000),
                                            120000);

    status_ = Status::Joined;
    statusMessage_ = isHost() ? "Waiting for another player to join."
                              : "Joined. Waiting for the host.";
}

void RoomRelayClient::handlePeerJoined(const RoomRelay::ServerFrame& frame) {
    if(status_ != Status::Joined) {
        finish(RoomRelay::Close::ProtocolError, "The game service sent something unexpected.");
        return;
    }
    if(frame.peerId == localPeerId_) {
        return;     // our own identity came in WELCOME
    }
    if(findPeer(frame.peerId) != nullptr) {
        return;     // announced twice; membership is idempotent
    }
    if(peers_.size() >= RoomRelay::Limits::kMaxPeersPerRoom) {
        finish(RoomRelay::Close::ProtocolError, "That game has too many players.");
        return;
    }
    if(frame.role == RoomRelay::Role::Host) {
        for(const Peer& peer : peers_) {
            if(peer.isHost()) {
                finish(RoomRelay::Close::ProtocolError,
                       "The game service named two hosts for one game.");
                return;
            }
        }
        if(isHost()) {
            finish(RoomRelay::Close::ProtocolError,
                   "The game service named two hosts for one game.");
            return;
        }
    }

    Peer peer;
    peer.id      = frame.peerId;
    peer.role    = frame.role;
    peer.name    = frame.displayName;
    peer.runtime = frame.runtime;
    peers_.push_back(peer);

    Event event;
    event.type    = Event::Type::PeerJoined;
    event.peerId  = peer.id;
    event.role    = peer.role;
    event.name    = peer.name;
    event.runtime = peer.runtime;
    pushEvent(std::move(event));
}

void RoomRelayClient::handlePeerLeft(const RoomRelay::ServerFrame& frame) {
    const auto it = std::find_if(peers_.begin(), peers_.end(),
                                 [&](const Peer& peer) { return peer.id == frame.peerId; });
    if(it == peers_.end()) {
        return;     // never announced to us, or already removed: leaving happens exactly once
    }

    Event event;
    event.type    = Event::Type::PeerLeft;
    event.peerId  = it->id;
    event.role    = it->role;
    event.name    = it->name;
    event.runtime = it->runtime;
    event.reason  = frame.reason;
    peers_.erase(it);
    pushEvent(std::move(event));
}

void RoomRelayClient::handleRelayPayload(RoomRelay::ServerFrame& frame) {
    if(status_ != Status::Joined) {
        finish(RoomRelay::Close::ProtocolError, "The game service sent something unexpected.");
        return;
    }

    Peer* sender = findPeer(frame.senderPeerId);
    if(sender == nullptr) {
        // The relay should never route from somebody we were not told about. Refusing is the
        // conservative reading: the peer table is what binds a payload to a player.
        return;
    }

    // The same matrix the relay applies, applied again here. Even a relay that has been
    // tampered with cannot make this client act on a host-only message from a client.
    if(!RoomRelay::isRelayableGameMessage(frame.gameMessageType, sender->isHost(), phase_)) {
        const Uint32 now = SDL_GetTicks();
        if(sender->lastRefuseTime != 0 && (now - sender->lastRefuseTime) > 10000) {
            sender->refusedMessages = 0;
        }
        sender->lastRefuseTime = now;
        sender->refusedMessages++;
        if(sender->refusedMessages <= 3 || (now - sender->lastRefuseLog) >= 5000) {
            sender->lastRefuseLog = now;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "RoomRelayClient: refused message %u from '%s' (%u refused so far)",
                        static_cast<unsigned>(frame.gameMessageType), sender->name.c_str(),
                        static_cast<unsigned>(sender->refusedMessages));
        }
        if(sender->refusedMessages >= 64) {
            finish(RoomRelay::Close::ProtocolError, "Another player sent unusable game data.");
        }
        return;
    }

    Event event;
    event.type            = Event::Type::GamePayload;
    event.peerId          = frame.senderPeerId;
    event.channel         = frame.channel;
    event.gameMessageType = frame.gameMessageType;
    event.payload         = std::move(frame.payload);
    pushEvent(std::move(event));
}

bool RoomRelayClient::sendGamePayload(const std::uint8_t* payload, std::size_t length,
                                      int channel, std::uint32_t recipient) {
    if(status_ != Status::Joined) {
        return false;
    }
    if(channel < 0 || channel > 1) {
        return false;
    }
    if(payload == nullptr || length < 4) {
        return false;
    }

    const std::uint32_t declared =
          static_cast<std::uint32_t>(payload[0])
        | (static_cast<std::uint32_t>(payload[1]) << 8)
        | (static_cast<std::uint32_t>(payload[2]) << 16)
        | (static_cast<std::uint32_t>(payload[3]) << 24);

    // Refusing here means a message the relay would reject never leaves, which keeps the local
    // refusal budget on the relay clean and makes mistakes obvious during development.
    if(declared > 0xFFFFu
       || !RoomRelay::isRelayableGameMessage(static_cast<std::uint16_t>(declared), isHost(),
                                             phase_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RoomRelayClient: not sending message %u in this role or phase",
                    static_cast<unsigned>(declared));
        return false;
    }
    if(recipient != 0 && findPeer(recipient) == nullptr) {
        return false;
    }

    std::vector<std::uint8_t> frame;
    if(!RoomRelay::encodeRelay(recipient, static_cast<std::uint8_t>(channel), true, payload,
                               length, frame)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RoomRelayClient: refusing to send a %zu byte game message", length);
        return false;
    }
    return sendFrame(frame);
}

bool RoomRelayClient::setRoomPhase(RoomRelay::Phase phase) {
    if(status_ != Status::Joined || !isHost()) {
        return false;
    }
    if(phase_ == phase) {
        return true;
    }
    phase_ = phase;
    return sendFrame(RoomRelay::encodeRoomPhase(phase));
}

bool RoomRelayClient::sendDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                                     std::size_t length) {
    if(status_ != Status::Joined) {
        return false;
    }
    std::vector<std::uint8_t> frame;
    if(!RoomRelay::encodeDiagnostic(kind, payload, length, frame)) {
        return false;
    }
    return sendFrame(frame);
}
