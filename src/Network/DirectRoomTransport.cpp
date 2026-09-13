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

#include <Network/DirectRoomTransport.h>

#include <misc/SDL2pp.h>

#include <algorithm>

namespace {

/// The only endpoints this transport knows. There is no gameplay endpoint to add to this list.
constexpr const char* kSessionPath = "/v1/p2p/session";
constexpr const char* kPollPath    = "/v1/p2p/poll";
constexpr const char* kSignalPath  = "/v1/p2p/signal";
constexpr const char* kPhasePath   = "/v1/p2p/phase";

/**
    Accepts an HTTPS signaling base url, or a plaintext loopback one when the player explicitly
    asked for the development endpoint on this computer.

    A ws:// or wss:// url is refused outright: the direct transport has no socket to open with it,
    and accepting one would be the first step of quietly going back to relayed gameplay.
*/
bool isAcceptableSignalingBaseUrl(const std::string& url, bool allowLoopbackPlaintext,
                                  std::string& error) {
    // A relay endpoint is refused by name as well as by scheme. The whole point of this transport
    // is that an admission answer cannot move gameplay back onto somebody's server, so the client
    // decides what it will talk to rather than taking the service's word for it.
    if(url.find("/relay") != std::string::npos) {
        error = "That game service address is a relay, and this game plays directly between players.";
        return false;
    }
    const bool https = url.compare(0, 8, "https://") == 0;
    const bool http  = url.compare(0, 7, "http://") == 0;
    if(!https && !http) {
        error = url.compare(0, 5, "ws://") == 0 || url.compare(0, 6, "wss://") == 0
                    ? "This game service is a relay, and this game plays directly between players."
                    : "The game service address is not usable.";
        return false;
    }
    if(url.size() > 200 || url.find('?') != std::string::npos
       || url.find('#') != std::string::npos || url.back() == '/') {
        error = "The game service address is not usable.";
        return false;
    }
    for(const char c : url) {
        const bool acceptable = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                             || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'
                             || c == ':' || c == '/' || c == '[' || c == ']';
        if(!acceptable) {
            error = "The game service address is not usable.";
            return false;
        }
    }
    const bool endsWithP2p = url.size() >= 4 && url.compare(url.size() - 4, 4, "/p2p") == 0;
    if(http) {
        const std::string host = url.substr(7);
        const bool loopback = host.compare(0, 10, "127.0.0.1:") == 0 || host == "127.0.0.1"
                           || host.compare(0, 6, "[::1]:") == 0 || host == "[::1]"
                           || host.compare(0, 10, "localhost:") == 0 || host == "localhost";
        if(!allowLoopbackPlaintext || !loopback) {
            error = "The game service must be reached over HTTPS.";
            return false;
        }
        // A development endpoint on this computer may live anywhere under the document root.
        return true;
    }
    if(!endsWithP2p) {
        error = "That game service address is not a direct-play service.";
        return false;
    }
    return true;
}

std::string fieldsFor(const DirectRoomTransport::Config& config) {
    std::string body = "app=dunecity";
    body += "&appVersion=" + P2PSignal::encodeFormValue(config.appVersion);
    body += "&gameProtocol=" + std::to_string(config.gameProtocolVersion);
    body += "&contentHash=" + P2PSignal::encodeFormValue(config.contentHash);
    body += "&runtime=" + P2PSignal::encodeFormValue(config.runtime);
    return body;
}

} // namespace

DirectRoomTransport::DirectRoomTransport() : DirectRoomTransport(Dependencies()) { }

DirectRoomTransport::DirectRoomTransport(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {
    if(!dependencies_.httpFactory) {
        dependencies_.httpFactory = []() { return createBoundedHttpClient(); };
    }
    if(!dependencies_.peerFactory) {
        dependencies_.peerFactory = [](const DirectPeerConnectionOptions& options,
                                       std::string& error) {
            return createDirectPeerConnection(options, error);
        };
    }
    if(!dependencies_.clock) {
        dependencies_.clock = []() { return static_cast<std::uint32_t>(SDL_GetTicks()); };
    }
}

DirectRoomTransport::~DirectRoomTransport() {
    for(auto& link : links_) {
        if(link->connection) {
            link->connection->close();
        }
    }
}

std::uint32_t DirectRoomTransport::now() const {
    return dependencies_.clock();
}

std::string DirectRoomTransport::endpoint(const char* path) const {
    return config_.signalingBaseUrl + path;
}

/**
    The shape every signalling request shares.

    The response bound is the one the parser in P2PSignalingProtocol.h enforces, so the client
    never reads a byte it would refuse anyway - and a service cannot make it allocate more by
    claiming to have more to say.
*/
BoundedHttpClient::Request DirectRoomTransport::signalingRequest() const {
    BoundedHttpClient::Request request;
    request.maxResponseBytes = P2PSignal::Limits::kMaxResponseBytes;
    return request;
}

bool DirectRoomTransport::start(const Config& config, std::string& error) {
    if(status_ != Status::Idle) {
        error = "A direct session is already running.";
        return false;
    }
    if(!isAcceptableSignalingBaseUrl(config.signalingBaseUrl, config.allowLoopbackPlaintext, error)) {
        return false;
    }
    if(!RoomRelay::isAcceptableGrant(config.grant) || config.grant.empty()) {
        error = "The invitation from the game service is not usable.";
        return false;
    }
    if(!RoomRelay::isAcceptableDisplayName(config.displayName)) {
        error = "That player name cannot be used online.";
        return false;
    }
    if(config.runtime != "native" && config.runtime != "browser") {
        error = "This build cannot describe itself to the game service.";
        return false;
    }
    if(!isDirectPeerConnectionAvailable()) {
        error = "This build cannot open direct connections to other players.";
        return false;
    }

    config_   = config;
    roomCode_ = config.roomCode;
    http_     = dependencies_.httpFactory();
    if(!http_) {
        error = "This build cannot reach the game service.";
        return false;
    }

    status_           = Status::Connecting;
    sessionStartedMs_ = now();
    lastAnswerMs_     = sessionStartedMs_;
    statusMessage_    = "Setting up a direct connection.";
    beginSession();
    return true;
}

void DirectRoomTransport::beginSession() {
    BoundedHttpClient::Request request = signalingRequest();
    request.url  = endpoint(kSessionPath);
    request.body = fieldsFor(config_);
    request.body += "&grant=" + P2PSignal::encodeFormValue(config_.grant);
    request.body += "&name=" + P2PSignal::encodeFormValue(P2PSignal::encodeHexText(config_.displayName));
    if(!config_.roomCode.empty()) {
        request.body += "&room=" + P2PSignal::encodeFormValue(config_.roomCode);
    }
    http_->begin(request);
    stage_            = SignalingStage::OpeningSession;
    inFlight_         = InFlight::Session;
    requestStartedMs_ = now();
}

void DirectRoomTransport::update() {
    if(status_ == Status::Idle || status_ == Status::Closed) {
        return;
    }
    const std::uint32_t nowMs = now();
    pumpSignaling(nowMs);
    pumpConnections(nowMs);
}

void DirectRoomTransport::pumpSignaling(std::uint32_t nowMs) {
    if(!http_ || stage_ == SignalingStage::Stopped) {
        return;
    }
    http_->update();

    BoundedHttpClient::Result result;
    if(http_->poll(result)) {
        const std::uint32_t elapsed = nowMs - requestStartedMs_;
        roundTripMs_  = elapsed > 60000 ? roundTripMs_ : elapsed;
        const InFlight what = inFlight_;
        inFlight_ = InFlight::None;
        switch(what) {
            case InFlight::Session: handleSessionResult(result); break;
            case InFlight::Poll:    handlePollResult(result);    break;
            case InFlight::Post:
            case InFlight::Phase:   handlePostResult(result);    break;
            case InFlight::None:    break;
        }
        if(result.transportError.empty() && result.httpStatus >= 200 && result.httpStatus < 300) {
            lastAnswerMs_ = nowMs;
            if(signalingLost_) {
                signalingLost_ = false;
                Event event;
                event.type    = Event::Type::Diagnostic;
                event.message = "The game service is reachable again.";
                pushEvent(std::move(event));
            }
        }
    }

    if(stage_ != SignalingStage::Running || http_->busy() || inFlight_ != InFlight::None) {
        // A request that never answers must not wedge the session; the deadline recycles it.
        if(inFlight_ != InFlight::None && nowMs - requestStartedMs_ > kRequestTimeoutMs) {
            http_->cancel();
            inFlight_ = InFlight::None;
            if(haveInFlightSignal_) {
                // Whatever was in flight was never delivered, and the peer it was for is still
                // waiting for it. It goes back at the front rather than being forgotten.
                haveInFlightSignal_ = false;
                if(inFlightSignal_.attempts < kMaxSignalAttempts) {
                    outgoing_.push_front(inFlightSignal_);
                } else {
                    dropLink(inFlightSignal_.to,
                             "Could not exchange connection details through the game service.");
                }
            }
            if(stage_ == SignalingStage::OpeningSession) {
                finish(RoomRelay::Close::Timeout,
                       "The game service did not answer, so the match could not be set up.");
            }
        }
        // Losing the service is survivable only once the match has started and the roster is
        // fixed. Before that this session still depends on the admission authority - it is how a
        // player is admitted at all - so losing it has to fail closed rather than leave a lobby
        // running on nobody's authority.
        if(!signalingLost_ && stage_ == SignalingStage::Running
           && nowMs - lastAnswerMs_ > kSignalingLostMs) {
            signalingLost_ = true;
            if(!matchStarted_) {
                finish(RoomRelay::Close::Timeout,
                       "The game service stopped answering before the match started.");
                return;
            }
            Event event;
            event.type    = Event::Type::Diagnostic;
            event.message = "The game service is unreachable. The match continues between the players.";
            pushEvent(std::move(event));
        }
        return;
    }

    beginNextSignalingRequest(nowMs);
}

/**
    Starts whichever signaling request matters most right now.

    The order is deliberate. A phase change stops the service admitting somebody into a match
    that has already started, so it goes first. A handshake message is what a waiting player is
    blocked on, so it goes before a poll. A poll is the only one that can wait.
*/
void DirectRoomTransport::beginNextSignalingRequest(std::uint32_t nowMs) {
    if(phaseUpdatePending_) {
        BoundedHttpClient::Request request = signalingRequest();
        request.url          = endpoint(kPhasePath);
        request.sessionToken = sessionToken_;
        request.body         = std::string("phase=")
                             + (pendingPhase_ == RoomRelay::Phase::Match ? "match" : "lobby");
        http_->begin(request);
        inFlight_         = InFlight::Phase;
        requestStartedMs_ = nowMs;
        return;
    }

    if(!outgoing_.empty()) {
        inFlightSignal_ = outgoing_.front();
        outgoing_.pop_front();
        ++inFlightSignal_.attempts;
        haveInFlightSignal_ = true;

        BoundedHttpClient::Request request = signalingRequest();
        request.url          = endpoint(kSignalPath);
        request.sessionToken = sessionToken_;
        request.body  = "to=" + std::to_string(inFlightSignal_.to);
        request.body += "&kind=";
        request.body += P2PSignal::signalKindName(inFlightSignal_.kind);
        request.body += "&data=" + P2PSignal::encodeHexText(inFlightSignal_.payload);
        if(request.body.size() > P2PSignal::Limits::kMaxSignalRequestBytes) {
            // Refusing to send it is not a silent loss: the peer it was for cannot be reached
            // without it, so the connection is ended and the player is told.
            haveInFlightSignal_ = false;
            dropLink(inFlightSignal_.to,
                     "This computer produced connection details the game service cannot carry.");
            return;
        }
        http_->begin(request);
        inFlight_         = InFlight::Post;
        requestStartedMs_ = nowMs;
        return;
    }

    // Polling slows right down once every known peer is connected: the channels carry the game,
    // and the service is only needed to notice somebody arriving or leaving.
    bool negotiating = false;
    for(const auto& link : links_) {
        if(!link->connected) {
            negotiating = true;
        }
    }
    // Once the match is running and every known player is connected, the service has almost
    // nothing left to tell us - it cannot admit a late joiner into a running match - so polling
    // drops to a trickle rather than keeping a conversation going for its own sake.
    const bool settled = !negotiating && matchStarted_ && !links_.empty();
    const std::uint32_t interval = negotiating ? kNegotiatingPollMs
                                 : settled ? kMatchPollMs
                                 : (links_.empty() ? kIdlePollMs : kConnectedPollMs);
    if(nowMs - lastPollMs_ < interval) {
        return;
    }
    lastPollMs_ = nowMs;
    BoundedHttpClient::Request request = signalingRequest();
    request.url          = endpoint(kPollPath);
    request.sessionToken = sessionToken_;
    request.body         = "cursor=" + std::to_string(cursor_);
    http_->begin(request);
    inFlight_         = InFlight::Poll;
    requestStartedMs_ = nowMs;
}

void DirectRoomTransport::handleSessionResult(const BoundedHttpClient::Result& result) {
    if(!result.transportError.empty()) {
        finish(RoomRelay::Close::Timeout,
               "The game service could not be reached, so the match could not be set up.");
        return;
    }
    P2PSignal::SessionResponse response;
    std::string error;
    if(!P2PSignal::parseSessionResponse(result.body, response, error)) {
        finish(RoomRelay::Close::ProtocolError, error);
        return;
    }
    if(!response.ok) {
        finish(RoomRelay::Close::Unauthorized, response.errorMessage);
        return;
    }

    localPeerId_ = response.peerId;
    sessionToken_ = response.session;
    localRole_   = response.role;
    maxPeers_    = response.maxPeers;
    phase_       = response.phase;
    iceServers_  = response.iceServers;
    if(!response.roomCode.empty()) {
        roomCode_ = response.roomCode;
    }
    status_        = Status::Joined;
    stage_         = SignalingStage::Running;
    statusMessage_ = "Connecting directly to the other players.";
    lastPollMs_    = 0;   // poll immediately
}

void DirectRoomTransport::handlePollResult(const BoundedHttpClient::Result& result) {
    if(!result.transportError.empty()) {
        return;     // the deadline above decides when this becomes player-visible
    }
    if(result.httpStatus == 401 || result.httpStatus == 403 || result.httpStatus == 404) {
        // Our membership has been revoked or has expired. Until the match starts, this session
        // exists on the strength of that membership, so losing it is fatal however many channels
        // happen to be open. Afterwards the roster is frozen and the players carry the match
        // themselves, so it is only worth saying out loud.
        if(!matchStarted_) {
            finish(RoomRelay::Close::Unauthorized,
                   "The game service ended this room before the match started.");
            return;
        }
        if(!signalingLost_) {
            signalingLost_ = true;
            Event event;
            event.type    = Event::Type::Diagnostic;
            event.message = "The game service ended this room. The match continues between the players.";
            pushEvent(std::move(event));
        }
        stage_ = SignalingStage::Stopped;
        return;
    }

    P2PSignal::PollResponse response;
    std::string error;
    if(!P2PSignal::parsePollResponse(result.body, localPeerId_, response, error)) {
        Event event;
        event.type    = Event::Type::Refused;
        event.message = error;
        pushEvent(std::move(event));
        return;
    }
    if(!response.ok) {
        return;
    }
    // A stale response must not roll the cursor back or replay an SDP offer.
    if(response.cursor < cursor_) {
        return;
    }
    const std::uint64_t previousCursor = cursor_;
    cursor_ = response.cursor;

    if(response.phase != phase_) {
        phase_ = response.phase;
        Event event;
        event.type  = Event::Type::PhaseChanged;
        event.phase = phase_;
        pushEvent(std::move(event));
    }

    const std::uint32_t nowMs = now();
    for(const P2PSignal::RoomMember& member : response.members) {
        // Applying one record can end the session - the host being renamed, for instance - and
        // nothing after that belongs to a session that no longer exists.
        if(status_ != Status::Joined) {
            return;
        }
        if(member.id == localPeerId_) {
            continue;
        }
        Peer* existing = mutablePeer(member.id);
        if(existing == nullptr) {
            if(peers_.size() >= RoomRelay::Limits::kMaxPeersPerRoom) {
                continue;   // the service is claiming a bigger room than the protocol allows
            }
            if(matchStarted_) {
                // Lockstep has no way to bring somebody up to date, so a member that appears
                // after the roster was frozen is not joinable - it is ignored, loudly.
                if(!isFrozenMember(member.id)) {
                    Event event;
                    event.type    = Event::Type::Refused;
                    event.peerId  = member.id;
                    event.message = "A player tried to join a match that had already started.";
                    pushEvent(std::move(event));
                }
                continue;
            }
            Peer peer;
            peer.id      = member.id;
            peer.role    = member.role;
            peer.name    = member.name;
            peer.runtime = member.runtime;
            peers_.push_back(peer);
            readinessDirty_ = true;
            for(auto& existingLink : links_) {
                // A membership change invalidates everybody's readiness report, including ours.
                existingLink->reportedReadiness = false;
            }
            openLink(member.id, nowMs);
        } else if(existing->role != member.role || existing->name != member.name
                  || existing->runtime != member.runtime) {
            // A player's id, role and name are fixed for the life of the room. Letting the
            // service change them afterwards would let it rename one player into another, and
            // names are identity everywhere the lobby and the command path look at them.
            dropLink(member.id, "The game service changed who a player is.");
        }
    }

    for(const std::uint32_t departed : response.departed) {
        if(status_ != Status::Joined) {
            return;
        }
        if(departed == localPeerId_) {
            continue;
        }
        const Link* link = findLink(departed);
        if(matchStarted_ && isFrozenMember(departed) && link != nullptr && link->connected) {
            // The service says this player is gone, but we are talking to them directly and the
            // channel is open. The channel is the truth: the service only knows who stopped
            // polling it, and a session that expired mid-match is exactly the case this transport
            // is built to survive.
            continue;
        }
        dropLink(departed, std::string());
    }

    // Attestations, once the room's membership is up to date: an attestation names a peer, and
    // a peer that has only just appeared has no connection to attach it to yet. A description is
    // never applied before the attestation for its pair has arrived.
    for(const P2PSignal::PeerFingerprint& fingerprint : response.fingerprints) {
        if(status_ != Status::Joined) {
            return;
        }
        // The parser has already refused anything not bound to (that peer -> us), so what is
        // left is an attestation for exactly one of our own connections.
        if(Link* link = findLink(fingerprint.from)) {
            if(!link->fingerprintValue.empty()
               && (link->fingerprintValue != fingerprint.value
                   || link->fingerprintAlgorithm != fingerprint.algorithm)) {
                // First one wins, for the life of this pair. A service that changes its mind is
                // either confused or being used to put somebody else on this channel.
                dropLink(fingerprint.from,
                         "The game service changed a player's identity during the handshake.");
                continue;
            }
            link->fingerprintAlgorithm = fingerprint.algorithm;
            link->fingerprintValue     = fingerprint.value;
        }
    }

    for(const P2PSignal::SignalRecord& record : response.signals) {
        if(status_ != Status::Joined) {
            return;
        }
        if(record.sequence > previousCursor) {
            applySignal(record);
        }
    }

    if(response.closed && !matchStarted_) {
        // An explicit closure from the authority that admitted us, before the match exists.
        finish(response.closeCode != 0 ? response.closeCode : RoomRelay::Close::Normal,
               "The game service closed this room.");
    }
}

void DirectRoomTransport::handlePostResult(const BoundedHttpClient::Result& result) {
    const bool succeeded = result.transportError.empty() && result.httpStatus >= 200
                        && result.httpStatus < 300;
    const bool refused   = result.httpStatus >= 400 && result.httpStatus < 500;

    if(!haveInFlightSignal_) {
        // A phase update. It is retried until the service takes it, because the whole point of
        // it is to stop a late grant being redeemed; a transient failure must not lose it.
        if(succeeded || refused) {
            phaseUpdatePending_ = false;
        }
        if(!succeeded && refused) {
            Event event;
            event.type    = Event::Type::Refused;
            event.message = "The game service did not accept the start of the match.";
            pushEvent(std::move(event));
        }
        return;
    }

    haveInFlightSignal_ = false;
    if(succeeded) {
        return;
    }
    if(!refused && inFlightSignal_.attempts < kMaxSignalAttempts) {
        // Transient. The same message goes back to the front of the queue: dropping a handshake
        // message leaves the other player waiting for something that will never arrive.
        outgoing_.push_front(inFlightSignal_);
        return;
    }
    const Peer* peer = findPeer(inFlightSignal_.to);
    const std::string name = peer != nullptr && !peer->name.empty() ? peer->name
                                                                   : std::string("another player");
    dropLink(inFlightSignal_.to,
             "Could not exchange connection details with " + name
                 + " through the game service.");
}

void DirectRoomTransport::openLink(std::uint32_t peerId, std::uint32_t nowMs) {
    if(findLink(peerId) != nullptr || peerId == localPeerId_ || peerId == 0) {
        return;
    }
    // A failed pair cannot negotiate a new certificate under the same admitted peer ids.
    // The player must rejoin with a fresh admission; retrying each poll creates a signal storm.
    if(std::find(retiredPeerIds_.begin(), retiredPeerIds_.end(), peerId) != retiredPeerIds_.end()) return;
    auto link      = std::make_unique<Link>();
    link->peerId   = peerId;
    link->startedMs = nowMs;
    // The lower peer id offers. Both sides compute the same answer without another round trip,
    // and neither can end up waiting for the other to start.
    link->initiator = localPeerId_ < peerId;

    DirectPeerConnectionOptions options;
    options.iceServers = iceServers_;
    options.initiator  = link->initiator;

    std::string error;
    link->connection = dependencies_.peerFactory(options, error);
    if(!link->connection) {
        Event event;
        event.type    = Event::Type::Diagnostic;
        event.message = "A direct connection could not be created on this computer.";
        pushEvent(std::move(event));
        return;
    }
    links_.push_back(std::move(link));
}

void DirectRoomTransport::applySignal(const P2PSignal::SignalRecord& record) {
    Link* link = findLink(record.from);
    if(link == nullptr) {
        // A signal from somebody who is not a member of our room, as far as we know. The service
        // should not have sent it; it certainly is not applied.
        return;
    }
    if(record.kind == P2PSignal::SignalKind::Candidate) {
        if(P2PSignal::mentionsRelayCandidate(record.payload)) {
            refuse(*link, "relayed candidate refused");
            return;
        }
        if(link->connection && !link->connection->addRemoteCandidate(record.payload)) {
            refuse(*link, "candidate refused");
        }
        return;
    }
    applyDescription(*link, record);
}

void DirectRoomTransport::applyDescription(Link& link, const P2PSignal::SignalRecord& record) {
    if(link.fingerprintValue.empty()) {
        // The attestation for this pair has not arrived yet. Hold the description rather than
        // applying one that cannot be bound to an admitted player - and if they keep coming,
        // say so rather than quietly discarding the one that mattered.
        if(link.deferred.size() >= 4) {
            dropLink(link.peerId,
                     "A player sent connection details the game service never vouched for.");
            return;
        }
        link.deferred.push_back(record);
        return;
    }
    std::string algorithm;
    std::string value;
    if(!P2PSignal::extractSdpFingerprint(record.payload, algorithm, value)) {
        refuse(link, "description without a usable fingerprint");
        return;
    }
    if(algorithm != link.fingerprintAlgorithm || value != link.fingerprintValue) {
        // Somebody other than the admitted player is trying to take this seat.
        dropLink(link.peerId, "A player's identity did not match what the game service admitted.");
        return;
    }
    if(P2PSignal::mentionsRelayCandidate(record.payload)) {
        refuse(link, "description carries a relayed candidate");
        return;
    }
    if(link.connection && !link.connection->setRemoteDescription(record.kind, record.payload)) {
        refuse(link, "description refused");
    }
}

void DirectRoomTransport::pumpConnections(std::uint32_t nowMs) {
    // Indexed rather than iterated, and every step re-checks that the link still exists: handling
    // a received value can close the connection it arrived on, which erases the entry.
    for(std::size_t i = 0; i < links_.size();) {
        const std::uint32_t peerId = links_[i]->peerId;
        Link* link = links_[i].get();
        if(!link->connection) {
            ++i;
            continue;
        }
        link->connection->update();

        // Anything held back while we were waiting for the service to attest this pair.
        if(!link->fingerprintValue.empty() && !link->deferred.empty()) {
            std::deque<P2PSignal::SignalRecord> deferred;
            deferred.swap(link->deferred);
            for(const P2PSignal::SignalRecord& record : deferred) {
                applyDescription(*link, record);
                // applyDescription can close this connection, and closing the host's connection
                // ends the session; the freed pointer must not be used again either way.
                link = findLink(peerId);
                if(link == nullptr || status_ != Status::Joined) {
                    break;
                }
            }
        }
        if(status_ != Status::Joined) {
            return;
        }
        if(link == nullptr) {
            continue;
        }

        DirectPeerConnection::LocalSignal signal;
        while(link->connection->pollLocalSignal(signal)) {
            if(P2PSignal::mentionsRelayCandidate(signal.payload)) {
                continue;   // never published: this build has no relay to offer
            }
            queueSignal(peerId, signal.kind, signal.payload);
            // queueSignal closes this connection when it cannot send what we produced, which
            // frees the link. Nothing below may touch the old pointer.
            link = findLink(peerId);
            if(link == nullptr || status_ != Status::Joined) {
                break;
            }
        }
        if(link == nullptr) {
            continue;       // the entry this loop was holding is gone
        }
        if(status_ != Status::Joined) {
            return;
        }

        std::string value;
        std::size_t taken = 0;
        while(taken < kMaxValuesPerUpdate && link->connection->pollValue(value)) {
            ++taken;
            handleReceivedValue(*link, value, nowMs);
            link = findLink(peerId);
            if(link == nullptr || status_ == Status::Closed) {
                break;
            }
        }
        if(link == nullptr) {
            continue;       // closed by what it sent; the entry is already gone
        }

        const DirectPeerConnection::State state = link->connection->state();
        if(state == DirectPeerConnection::State::Connected && !link->connected) {
            link->connected  = true;
            readinessDirty_  = true;
            if(!link->announced) {
                link->announced = true;
                const Peer* peer = findPeer(peerId);
                Event event;
                event.type    = Event::Type::PeerJoined;
                event.peerId  = peerId;
                event.name    = peer != nullptr ? peer->name : std::string();
                event.runtime = peer != nullptr ? peer->runtime : std::string();
                event.role    = peer != nullptr ? peer->role : RoomRelay::Role::Unknown;
                pushEvent(std::move(event));
            }
        }

        // Liveness on the path that carries the game, so CommandBufferPolicy sizes its buffer
        // from the link a command actually travels over rather than from an HTTP round trip.
        if(link->connected && !link->pingOutstanding
           && nowMs - link->lastPingMs > kPingIntervalMs) {
            link->lastPingMs      = nowMs;
            link->pingSentMs      = nowMs;
            link->pingToken       = nextPingToken_++;
            link->pingOutstanding = link->connection->sendValue(
                P2PWire::encodeProbeEnvelope(false, link->pingToken));
        }

        // A link may be created while handling this frame's poll response, after nowMs was
        // sampled. Treating a timestamp a millisecond in the future as unsigned elapsed time
        // closed fresh browser connections immediately. Signed deadline comparison also wraps.
        const bool deadlineMissed = !link->connected
            && SDL_TICKS_PASSED(nowMs, link->startedMs + kConnectDeadlineMs);
        if(state == DirectPeerConnection::State::Failed
           || state == DirectPeerConnection::State::Closed || deadlineMissed) {
            const Peer* peer = findPeer(peerId);
            const std::string name = peer != nullptr && !peer->name.empty()
                                         ? peer->name : std::string("another player");
            // There is no fallback to fall back to, on purpose, so the player is told plainly.
            const std::string reason = link->connected
                ? "The direct connection to " + name + " was lost."
                : "Could not open a direct connection to " + name
                      + ". Your network blocked the peer-to-peer link.";
            dropLink(peerId, reason);
            continue;       // dropLink erased this entry
        }
        ++i;
    }

    if(readinessDirty_ && connectedPeerCount() > 0) {
        readinessDirty_ = false;
        sendReadiness();
    }
}

bool DirectRoomTransport::chargeIngress(Link& link, std::size_t bytes, std::uint32_t nowMs) {
    // The ENet path's own budgets and its own window accounting, not a second set of numbers
    // that could drift away from them. Both are charged, so neither a flood of tiny messages nor
    // a few enormous ones gets through.
    const bool withinPackets = link.packetWindow.accept(nowMs, 1,
                                                        NetworkPacketPolicy::kMaxPacketsPerWindow,
                                                        NetworkPacketPolicy::kTrafficWindowMs);
    const bool withinBytes   = link.byteWindow.accept(nowMs, bytes,
                                                      NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                                                      NetworkPacketPolicy::kTrafficWindowMs);
    return withinPackets && withinBytes;
}

void DirectRoomTransport::handleReceivedValue(Link& link, const std::string& assembled,
                                             std::uint32_t nowMs) {
    if(!chargeIngress(link, assembled.size(), nowMs)) {
        dropLink(link.peerId, "A player sent more data than this game will accept.");
        return;
    }
    P2PWire::Envelope envelope;
    const char* envelopeReason = nullptr;
    if(!P2PWire::decodeEnvelope(assembled, envelope, envelopeReason)) {
        // Not a refusal to shrug off. A peer whose messages this game cannot read is a peer whose
        // command stream may already have holes in it, and applying the rest of it is how a
        // lockstep match desynchronises without anybody noticing.
        dropLink(link.peerId, std::string("A player sent something this game could not read (")
                                  + (envelopeReason != nullptr ? envelopeReason : "unreadable")
                                  + ").");
        return;
    }

    switch(envelope.kind) {
        case P2PWire::EnvelopeKind::Game: {
            // Addressed to someone else: fatal, not forwarded. This transport has no route to
            // another peer's channel and is not going to grow one, and a peer that asks for one
            // is not speaking this protocol.
            if(envelope.recipient != 0 && envelope.recipient != localPeerId_) {
                dropLink(link.peerId, "A player asked this game to pass a message to someone else.");
                return;
            }
            Event event;
            event.type    = Event::Type::GamePayload;
            event.peerId  = link.peerId;
            event.channel = static_cast<std::uint8_t>(envelope.channel);
            event.payload = std::move(envelope.payload);
            pushEvent(std::move(event));
            return;
        }
        case P2PWire::EnvelopeKind::Diagnostic: {
            Event event;
            event.type           = Event::Type::Diagnostic;
            event.peerId         = link.peerId;
            event.diagnosticKind = envelope.diagnosticKind;
            event.payload        = std::move(envelope.payload);
            pushEvent(std::move(event));
            return;
        }
        case P2PWire::EnvelopeKind::Readiness:
            link.rosterKey           = envelope.rosterKey;
            link.reportedConnections = envelope.connectedPeers;
            link.reportedReadiness   = true;
            return;
        case P2PWire::EnvelopeKind::Ping: {
            // Answered on the channel the game uses, so the measurement is of that channel.
            const std::string pong = P2PWire::encodeProbeEnvelope(true, envelope.token);
            link.connection->sendValue(pong);
            return;
        }
        case P2PWire::EnvelopeKind::Pong:
            if(link.pingOutstanding && envelope.token == link.pingToken) {
                link.pingOutstanding = false;
                link.roundTripMs     = nowMs - link.pingSentMs;
                link.haveRoundTrip   = true;
            }
            return;
    }
}

std::string DirectRoomTransport::localRosterKey() const {
    std::vector<std::uint32_t> ids;
    ids.push_back(localPeerId_);
    for(const Peer& peer : peers_) {
        ids.push_back(peer.id);
    }
    std::sort(ids.begin(), ids.end());
    std::string key;
    for(std::size_t i = 0; i < ids.size(); ++i) {
        if(i != 0) {
            key += ',';
        }
        key += std::to_string(ids[i]);
    }
    return key;
}

void DirectRoomTransport::sendReadiness() {
    std::vector<std::uint32_t> connected;
    for(const auto& link : links_) {
        if(link->connected) {
            connected.push_back(link->peerId);
        }
    }
    std::sort(connected.begin(), connected.end());
    const std::string envelope = P2PWire::encodeReadinessEnvelope(localRosterKey(), connected);
    for(auto& link : links_) {
        if(link->connected && link->connection) {
            link->connection->sendValue(envelope);
        }
    }
}

bool DirectRoomTransport::meshReady() const {
    return meshBlockedReason().empty();
}

std::string DirectRoomTransport::meshBlockedReason() const {
    if(status_ != Status::Joined) {
        return "The direct session is not ready yet.";
    }
    if(peers_.empty()) {
        // Nobody else is in the room, so there are no pairs to be ready. A host playing against
        // computer opponents in a room nobody joined is not waiting for anything.
        return std::string();
    }
    const std::string roster = localRosterKey();
    for(const Peer& peer : peers_) {
        const Link* link = nullptr;
        for(const auto& candidate : links_) {
            if(candidate->peerId == peer.id) {
                link = candidate.get();
            }
        }
        if(link == nullptr || !link->connected) {
            return "Still connecting directly to "
                 + (peer.name.empty() ? std::string("another player") : peer.name) + ".";
        }
        if(!link->reportedReadiness || link->rosterKey != roster) {
            // Either that peer has not reported yet, or it sees a different room than we do.
            // Starting across a disagreement is how a player ends up missing from a match.
            return (peer.name.empty() ? std::string("Another player") : peer.name)
                 + " has not confirmed the same set of players yet.";
        }
        // Every other player must appear in that peer's own connection list, or there is a
        // guest-to-guest link missing and its commands would never arrive.
        for(const Peer& other : peers_) {
            if(other.id == peer.id) {
                continue;
            }
            bool found = false;
            for(const std::uint32_t id : link->reportedConnections) {
                if(id == other.id) {
                    found = true;
                }
            }
            if(!found) {
                return (peer.name.empty() ? std::string("A player") : peer.name)
                     + " is not connected to "
                     + (other.name.empty() ? std::string("another player") : other.name) + " yet.";
            }
        }
        bool sawUs = false;
        for(const std::uint32_t id : link->reportedConnections) {
            if(id == localPeerId_) {
                sawUs = true;
            }
        }
        if(!sawUs) {
            return (peer.name.empty() ? std::string("A player") : peer.name)
                 + " has not confirmed the connection to this computer yet.";
        }
    }
    return std::string();
}

Uint32 DirectRoomTransport::roundTripTimeMs() const {
    Uint32 worst = 0;
    bool   sawConnectedPeer = false;
    for(const auto& link : links_) {
        if(!link->connected) {
            continue;
        }
        sawConnectedPeer = true;
        const Uint32 sample = link->haveRoundTrip ? link->roundTripMs : kAssumedRoundTripMs;
        if(sample > worst) {
            worst = sample;
        }
    }
    return sawConnectedPeer ? worst : 0;
}

Uint32 DirectRoomTransport::peerRoundTripTimeMs(std::uint32_t peerId) const {
    for(const auto& link : links_) {
        if(link->peerId == peerId) {
            return link->haveRoundTrip ? link->roundTripMs : kAssumedRoundTripMs;
        }
    }
    return 0;
}

bool DirectRoomTransport::sendEnvelopeTo(Link& link, const std::string& envelope) {
    if(!link.connection || !link.connected) {
        return false;
    }
    // The backend takes a whole value and either accepts all of it or none of it, so there is no
    // half-sent message to reason about here. What it cannot do is promise delivery of a message
    // it never accepted, which is why the caller treats false as fatal rather than as a retry.
    return link.connection->sendValue(envelope);
}

bool DirectRoomTransport::sendGamePayload(const std::uint8_t* payload, std::size_t length,
                                          int channel, std::uint32_t recipient) {
    if(status_ != Status::Joined) {
        return false;
    }
    const std::string envelope = P2PWire::encodeGameEnvelope(payload, length, channel, recipient);
    if(envelope.empty()) {
        statusMessage_ = "A game message was too large to send.";
        return false;
    }

    // A broadcast that reached two of three players and reported success is how a lockstep match
    // ends up with two histories: the peers that got it advance, the one that did not never
    // will, and nothing says so.
    //
    // This cannot un-send what an earlier recipient already accepted - nothing can. What it does
    // is refuse to call that a success: any recipient that would not take the message loses its
    // connection, and once the match is running that ends the match for this client rather than
    // letting it play on against a peer that is missing a command.
    std::vector<std::uint32_t> failed;
    bool anyTarget = false;
    for(auto& link : links_) {
        if(recipient != 0 && link->peerId != recipient) {
            continue;
        }
        if(!link->connected) {
            // A peer that is in the room but not yet connected is not a recipient in the lobby,
            // and cannot exist at all once the roster is frozen.
            if(matchStarted_) {
                failed.push_back(link->peerId);
            }
            continue;
        }
        anyTarget = true;
        if(!sendEnvelopeTo(*link, envelope)) {
            failed.push_back(link->peerId);
        }
    }

    for(const std::uint32_t peerId : failed) {
        const Peer* peer = findPeer(peerId);
        const std::string name = peer != nullptr && !peer->name.empty()
                                     ? peer->name : std::string("another player");
        dropLink(peerId, "The direct connection to " + name
                             + " could not take any more of the game's messages.");
    }
    if(!failed.empty()) {
        statusMessage_ = "A direct connection could not keep up with the match.";
        return false;
    }
    return anyTarget;
}

bool DirectRoomTransport::sendDiagnostic(RoomRelay::DiagnosticKind kind,
                                         const std::uint8_t* payload, std::size_t length) {
    if(status_ != Status::Joined) {
        return false;
    }
    const std::string envelope =
        P2PWire::encodeDiagnosticEnvelope(static_cast<std::uint8_t>(kind), payload, length);
    if(envelope.empty()) {
        return false;
    }
    bool anySent = false;
    for(auto& link : links_) {
        if(link->connected && sendEnvelopeTo(*link, envelope)) {
            anySent = true;
        }
    }
    return anySent;
}

bool DirectRoomTransport::sendMatchStart(const std::uint8_t* payload, std::size_t length) {
    if(localRole_ != RoomRelay::Role::Host || !prepareMatchStart()) return false;
    if(!peers_.empty() && !sendGamePayload(payload, length, 0, 0)) return false;
    return setRoomPhase(RoomRelay::Phase::Match);
}

bool DirectRoomTransport::prepareMatchStart() {
    if(matchStarted_) return status_ == Status::Joined;
    const std::string blocked = meshBlockedReason();
    if(!blocked.empty()) {
        finish(RoomRelay::Close::Normal, "The match could not start: " + blocked);
        return false;
    }
    freezeRoster("the players committed to starting the match");
    phase_ = RoomRelay::Phase::Match;
    return true;
}

bool DirectRoomTransport::setRoomPhase(RoomRelay::Phase phase) {
    if(status_ != Status::Joined || localRole_ != RoomRelay::Role::Host) {
        return false;
    }
    phase_ = phase;
    if(phase == RoomRelay::Phase::Match) {
        freezeRoster("the host started the match");
    }
    // Durable, not best-effort. Telling the service that the room has started is what stops a
    // grant issued a moment earlier being redeemed into a running match, and dropping that
    // update because a poll happened to be in flight is not a rare case - a poll is nearly
    // always in flight. It is queued and retried until it lands or the session ends.
    phaseUpdatePending_ = true;
    pendingPhase_       = phase;
    return true;
}

/**
    Fixes the set of players this match is played with.

    Lockstep has no catch-up protocol, so the roster cannot change once the simulation starts:
    anything that arrives afterwards has missed state it can never be given, and anything that
    leaves takes its commands with it. Freezing it locally means the match no longer depends on
    the service agreeing about who is in the room.
*/
void DirectRoomTransport::freezeRoster(const char* why) {
    if(matchStarted_) {
        return;
    }
    matchStarted_ = true;
    frozenRoster_.clear();
    for(const Peer& peer : peers_) {
        frozenRoster_.push_back(peer.id);
    }
    Event event;
    event.type    = Event::Type::Diagnostic;
    event.message = std::string("The set of players is now fixed because ") + why + ".";
    pushEvent(std::move(event));
}

bool DirectRoomTransport::isFrozenMember(std::uint32_t peerId) const {
    for(const std::uint32_t id : frozenRoster_) {
        if(id == peerId) {
            return true;
        }
    }
    return false;
}

void DirectRoomTransport::queueSignal(std::uint32_t to, P2PSignal::SignalKind kind,
                                      const std::string& payload) {
    if(outgoing_.size() >= kMaxQueuedSignals || payload.empty()
       || payload.size() > P2PSignal::Limits::kMaxSignalBytes
       || (kind == P2PSignal::SignalKind::Candidate
           && !P2PSignal::isAcceptableCandidatePayload(payload))) {
        // Dropping one of our own handshake messages would leave the other player waiting for
        // something that is never coming, so this ends the attempt instead of hiding it.
        dropLink(to, "This computer could not produce usable connection details.");
        return;
    }
    OutgoingSignal signal;
    signal.to      = to;
    signal.kind    = kind;
    signal.payload = payload;
    outgoing_.push_back(std::move(signal));
}

void DirectRoomTransport::refuse(Link& link, const char* reason) {
    ++link.refusals;
    Event event;
    event.type    = Event::Type::Refused;
    event.peerId  = link.peerId;
    event.message = reason != nullptr ? reason : "refused";
    pushEvent(std::move(event));
    if(link.refusals >= kMaxRefusalsPerPeer) {
        dropLink(link.peerId, "A player kept sending messages this game could not accept.");
    }
}

void DirectRoomTransport::dropLink(std::uint32_t peerId, const std::string& reason) {
    if(std::find(retiredPeerIds_.begin(), retiredPeerIds_.end(), peerId) == retiredPeerIds_.end()) {
        if(retiredPeerIds_.size() >= 128) {
            finish(RoomRelay::Close::Normal, "Too many connection attempts failed. Please open a new game.");
            return;
        }
        retiredPeerIds_.push_back(peerId);
    }
    // The peer's own details are read before anything is erased, because the lobby and the
    // running game identify a departing player by name, not by id.
    std::string     name;
    RoomRelay::Role role = RoomRelay::Role::Unknown;
    if(const Peer* peer = findPeer(peerId)) {
        name = peer->name;
        role = peer->role;
    }

    for(std::size_t i = 0; i < links_.size(); ++i) {
        if(links_[i]->peerId != peerId) {
            continue;
        }
        const bool wasConnected = links_[i]->connected;
        const bool wasAnnounced = links_[i]->announced;
        if(links_[i]->connection) {
            links_[i]->connection->close();
        }
        links_.erase(links_.begin() + static_cast<std::ptrdiff_t>(i));

        if(wasAnnounced || wasConnected) {
            Event event;
            event.type    = Event::Type::PeerLeft;
            event.peerId  = peerId;
            event.name    = name;
            event.role    = role;
            // A link that died under us reads as a lost connection rather than as somebody
            // choosing to leave, which is what the lobby and the game already know how to say.
            event.reason  = reason.empty() ? 0 : 2;
            event.message = reason;
            pushEvent(std::move(event));
        } else if(!reason.empty()) {
            Event event;
            event.type    = Event::Type::Diagnostic;
            event.peerId  = peerId;
            event.message = reason;
            pushEvent(std::move(event));
        }
        break;
    }

    for(std::size_t i = 0; i < peers_.size(); ++i) {
        if(peers_[i].id == peerId) {
            peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    // Who left matters more than how many are left.
    //
    // The host is the authority for the lobby and for starting the match; when it goes, the room
    // goes, exactly as it did on the relay. And once the roster is frozen there is no such thing
    // as a smaller match: a lockstep simulation cannot continue without one of its players, and
    // pretending otherwise is how a game stalls with no explanation.
    if(role == RoomRelay::Role::Host && localRole_ != RoomRelay::Role::Host) {
        finish(RoomRelay::Close::HostLeft,
               reason.empty()
                   ? (name.empty() ? std::string("The host left the game.")
                                   : "The host, " + name + ", left the game.")
                   : reason);
        return;
    }
    if(matchStarted_ && isFrozenMember(peerId)) {
        finish(RoomRelay::Close::Normal,
               name.empty() ? "A player left, so the match cannot continue."
                            : name + " left, so the match cannot continue.");
        return;
    }
    if(peers_.empty() && links_.empty() && localRole_ != RoomRelay::Role::Host && !reason.empty()) {
        finish(RoomRelay::Close::HostLeft, reason);
        return;
    }
    readinessDirty_ = true;
    for(auto& link : links_) {
        link->reportedReadiness = false;
    }
}

void DirectRoomTransport::stop(std::uint8_t reason) {
    (void)reason;
    for(auto& link : links_) {
        if(link->connection) {
            link->connection->close();
        }
    }
    links_.clear();
    // No parting message to the service. The game destroys this object immediately after
    // stopping it, so a request begun here would never be driven to completion, and a call that
    // only looks like it tells the service something is worse than not making it: the peers
    // already know, because their channels just closed, and the service expires the session on
    // its own. /v1/p2p/leave exists for callers that can wait for it; this one cannot.
    if(http_) {
        http_->cancel();
    }
    stage_ = SignalingStage::Stopped;
    if(status_ != Status::Closed) {
        status_    = Status::Closed;
        closeCode_ = RoomRelay::Close::Normal;
        if(statusMessage_.empty()) {
            statusMessage_ = "The direct session ended.";
        }
    }
}

void DirectRoomTransport::finish(std::uint16_t code, const std::string& message) {
    if(status_ == Status::Closed) {
        return;
    }
    status_        = Status::Closed;
    closeCode_     = code;
    statusMessage_ = message;
    stage_         = SignalingStage::Stopped;
    for(auto& link : links_) {
        if(link->connection) {
            link->connection->close();
        }
    }
    links_.clear();

    Event event;
    event.type    = Event::Type::Closed;
    event.code    = code;
    event.message = message;
    pushEvent(std::move(event));
}

void DirectRoomTransport::pushEvent(Event&& event) {
    // The same bound the relay transport keeps: a backlog the game cannot drain is how a lockstep
    // match desynchronises quietly, so the session ends instead.
    const std::size_t cost = event.payload.size() + event.message.size() + event.name.size() + 128;
    if(events_.size() >= kMaxQueuedEvents || eventBytes_ + cost > kMaxQueuedEventBytes) {
        events_.clear();
        eventBytes_ = 0;
        Event closed;
        closed.type    = Event::Type::Closed;
        closed.code    = RoomRelay::Close::SlowConsumer;
        closed.message = "This computer could not keep up with the match.";
        status_        = Status::Closed;
        closeCode_     = closed.code;
        statusMessage_ = closed.message;
        stage_         = SignalingStage::Stopped;
        eventBytes_   += 128;
        events_.push_back(std::move(closed));
        return;
    }
    eventBytes_ += cost;
    events_.push_back(std::move(event));
}

bool DirectRoomTransport::pollEvent(Event& event) {
    if(events_.empty()) {
        return false;
    }
    event = std::move(events_.front());
    events_.pop_front();
    const std::size_t cost = event.payload.size() + event.message.size() + event.name.size() + 128;
    eventBytes_ = eventBytes_ > cost ? eventBytes_ - cost : 0;
    return true;
}

DirectRoomTransport::Link* DirectRoomTransport::findLink(std::uint32_t peerId) {
    for(auto& link : links_) {
        if(link->peerId == peerId) {
            return link.get();
        }
    }
    return nullptr;
}

RoomSessionTransport::Peer* DirectRoomTransport::mutablePeer(std::uint32_t peerId) {
    for(Peer& peer : peers_) {
        if(peer.id == peerId) {
            return &peer;
        }
    }
    return nullptr;
}

RoomSessionTransport::Peer* DirectRoomTransport::findPeer(std::uint32_t peerId) {
    return mutablePeer(peerId);
}

const RoomSessionTransport::Peer* DirectRoomTransport::findPeer(std::uint32_t peerId) const {
    for(const Peer& peer : peers_) {
        if(peer.id == peerId) {
            return &peer;
        }
    }
    return nullptr;
}

std::size_t DirectRoomTransport::outgoingBacklogBytes() const {
    std::size_t total = 0;
    for(const auto& link : links_) {
        if(link->connection) {
            total += link->connection->bufferedAmount();
        }
    }
    return total;
}

std::size_t DirectRoomTransport::connectedPeerCount() const {
    std::size_t count = 0;
    for(const auto& link : links_) {
        if(link->connected) {
            ++count;
        }
    }
    return count;
}
