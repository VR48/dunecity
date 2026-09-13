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

#ifndef ROOMRELAYCLIENT_H
#define ROOMRELAYCLIENT_H

/**
    The relay session: logical peers, the handshake, membership, heartbeats and deadlines.

    This owns its own idea of a peer. It does not fabricate ENetPeer objects: a relay peer has a
    relay-assigned id and nothing that looks like an address, which is the whole point - there is
    no field anywhere in this path that can name a host and port to connect to.

    Everything is queued. update() drains the socket into a queue of typed events; the caller
    drains that queue from the game loop. No relay event ever calls into the simulation directly.
*/

#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>
#include <Network/RoomSessionTransport.h>

#include <misc/SDL2pp.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class RoomRelayClient : public RoomSessionTransport {
public:
    /**
        The peer, status and event types are the shared ones.

        They used to live here. They moved to RoomSessionTransport when the direct peer-to-peer
        transport appeared, because NetworkManager's receive path names them and must not care
        which transport produced them. The aliases keep every existing call site spelled the way
        it was.
    */
    using Peer   = RoomSessionTransport::Peer;
    using Status = RoomSessionTransport::Status;
    using Event  = RoomSessionTransport::Event;

    struct Config {
        std::string   socketUrl;
        std::string   origin;                   ///< browsers set this themselves; empty natively
        std::string   grant;
        std::string   displayName;
        std::string   appVersion;
        std::string   contentHash;
        std::string   runtime;                  ///< "native" or "browser"
        std::uint16_t gameProtocolVersion = 0;
        bool          allowLoopbackPlaintext = false;
    };

    RoomRelayClient();
    RoomRelayClient(const RoomRelayClient&) = delete;
    RoomRelayClient& operator=(const RoomRelayClient&) = delete;
    ~RoomRelayClient() override;

    /**
        Validates the endpoint, opens the socket and sends the handshake once it is open.
        \param  config  session parameters, including the single-use grant
        \param  error   set to a player-facing reason if the session cannot be started
        \return true if the session is now connecting
    */
    bool start(const Config& config, std::string& error);

    /// Drives the socket and fills the event queue. Call once per game loop iteration.
    void update() override;

    /// Sends LEAVE and closes. Safe to call more than once.
    void stop(std::uint8_t reason) override;

    bool pollEvent(Event& event) override;

    /// Events waiting for the game loop. Both of these are bounded; see pushEvent().
    std::size_t queuedEventCount() const { return events_.size(); }
    std::size_t queuedEventBytes() const { return eventBytes_; }

    Status status() const override { return status_; }
    bool   isJoined() const override { return status_ == Status::Joined; }
    bool   isHost() const override { return localRole_ == RoomRelay::Role::Host; }
    const std::string& roomCode() const override { return roomCode_; }
    /// Host-only admission response can rotate the invitation without reconnecting peers.
    void updateInvitationCode(const std::string& code) override {
        if(RoomRelay::isAcceptableRoomCode(code)) roomCode_ = code;
    }
    std::uint32_t localPeerId() const override { return localPeerId_; }
    std::uint8_t  maxPeers() const override { return maxPeers_; }
    RoomRelay::Phase phase() const override { return phase_; }

    /// A player-facing sentence describing the current state or the reason it ended.
    const std::string& statusMessage() const override { return statusMessage_; }
    std::uint16_t closeCode() const override { return closeCode_; }

    /// Round-trip time to the relay in milliseconds, or 0 before the first heartbeat answer.
    Uint32 roundTripTimeMs() const override { return roundTripMs_; }
    RelayTransportKind transportKind() const override {
        return relayTransportKindForUrl(config_.socketUrl);
    }

    /**
        Bytes handed to the transport that it has not written to the socket yet.

        A caller that produces faster than the socket drains can watch this instead of finding
        out when the session dies of a full outgoing queue.
    */
    std::size_t outgoingBacklogBytes() const override {
        return socket_ ? socket_->outgoingBacklogBytes() : 0;
    }

    const std::vector<Peer>& peers() const override { return peers_; }
    Peer* findPeer(std::uint32_t peerId) override;
    const Peer* findPeer(std::uint32_t peerId) const override;

    /**
        Sends one serialized game packet.
        \param  payload     the packet exactly as ENetPacketOStream produced it
        \param  length      its length
        \param  channel     0 or 1
        \param  recipient   0 for every other peer in the room, or a peer id
        \return false if the message was refused locally; the reason is in statusMessage()
    */
    bool sendGamePayload(const std::uint8_t* payload, std::size_t length, int channel,
                         std::uint32_t recipient) override;

    /// Host only: declares the room phase so the relay can apply the right rules.
    bool setRoomPhase(RoomRelay::Phase phase) override;

    /**
        Marks the local view of the room as in-match without telling the relay.

        A client calls this when its own simulation starts, so that a command is never refused
        locally because the host's phase change has not been applied yet. The relay's own view
        is still what authorises routing; this only stops the client refusing itself.
    */
    void assumeMatchPhase() override { phase_ = RoomRelay::Phase::Match; }

    /// Sends a bounded diagnostic to the room. Never part of the ENet game protocol.
    bool sendDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                        std::size_t length) override;

    /// Gameplay goes through a server on this transport, which is the whole difference.
    bool isDirectSession() const override { return false; }

private:
    /**
        What happens to events that are already queued when the session ends.

        A session that ends in order - the host left, the relay closed the room, the socket went
        away - may still have valid events queued ahead of the close, and they matter: a co-op
        continuation is sent immediately before the host disconnects, and discarding it would
        strand the other player. Those are kept, with the close appended after them.

        A session that ends *because* the queue overflowed is the opposite case. The backlog is
        precisely the thing the game could not keep up with, and applying a prefix of it is how a
        lockstep match desynchronises quietly. Those events are dropped, and the close is the
        only thing left to deliver.
    */
    enum class PendingEvents { Keep, Discard };

    void handleFrame(const std::vector<std::uint8_t>& frame);
    void handleWelcome(const RoomRelay::ServerFrame& frame);
    void handlePeerJoined(const RoomRelay::ServerFrame& frame);
    void handlePeerLeft(const RoomRelay::ServerFrame& frame);
    void handleRelayPayload(RoomRelay::ServerFrame& frame);
    void finish(std::uint16_t code, const std::string& message,
                PendingEvents pending = PendingEvents::Keep);
    void pushEvent(Event&& event);
    bool sendFrame(const std::vector<std::uint8_t>& frame);

    /// What one event costs against the queue's byte budget.
    static std::size_t eventCost(const Event& event);

    std::unique_ptr<RelayWebSocket> socket_;
    Config          config_;
    Status          status_        = Status::Idle;
    RoomRelay::Role localRole_     = RoomRelay::Role::Unknown;
    RoomRelay::Phase phase_        = RoomRelay::Phase::Lobby;
    std::uint32_t   localPeerId_   = 0;
    std::uint8_t    maxPeers_      = 0;
    std::string     roomCode_;
    std::string     statusMessage_;
    std::uint16_t   closeCode_     = 0;

    std::vector<Peer>  peers_;
    std::deque<Event>  events_;
    /// Aggregate cost of everything in events_, kept in step with it on both ends.
    std::size_t        eventBytes_ = 0;

    bool   helloSent_          = false;
    Uint32 lastHeartbeatSent_  = 0;
    Uint32 lastFrameReceived_  = 0;
    Uint32 roundTripMs_        = 0;
    Uint32 heartbeatIntervalMs_ = 5000;
    Uint32 livenessTimeoutMs_   = 20000;

    /// A relay that keeps refusing what we send means the two sides disagree about the rules.
    Uint32 refusalsFromRelay_  = 0;

    static constexpr std::size_t kMaxQueuedEvents = 4096;
    static constexpr Uint32      kMaxRelayRefusals = 32;
};

#endif // ROOMRELAYCLIENT_H
