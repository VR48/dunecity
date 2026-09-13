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

#ifndef ROOMSESSIONTRANSPORT_H
#define ROOMSESSIONTRANSPORT_H

/**
    A room session, as the game loop sees one.

    There are two implementations. RoomRelayClient carries gameplay through a server; it is the
    legacy transport, kept for older tests and releases. DirectRoomTransport carries gameplay
    peer-to-peer over WebRTC data channels and uses HTTPS only for admission and for introducing
    peers to each other; it is what Play Online uses now.

    The interface exists so that the hardened receive path in NetworkManager - the ownership,
    host and size validation, and GamePayloadRouter behind it - is written once and shared. A new
    transport does not get to bring its own game parser.

    The contract both sides keep: nothing here ever calls into the simulation. update() drains
    whatever the transport has, pollEvent() hands it to the game loop.
*/

#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <misc/SDL2pp.h>

#include <cstdint>
#include <string>
#include <vector>

class RoomSessionTransport {
public:
    /// A peer in the room. Deliberately has no address, no port and no socket.
    struct Peer {
        std::uint32_t   id      = 0;
        RoomRelay::Role role    = RoomRelay::Role::Unknown;
        std::string     name;
        std::string     runtime;        ///< client-reported, never trusted for anything

        // Config verification state, mirroring what the ENet path keeps per connection.
        std::string     gameVersion;
        std::string     quantBotConfigHash;
        std::string     objectDataHash;

        // Abuse accounting for messages this peer sent that the client itself refused.
        Uint32          refusedMessages = 0;
        Uint32          lastRefuseTime  = 0;
        Uint32          lastRefuseLog   = 0;

        bool isHost() const { return role == RoomRelay::Role::Host; }
    };

    enum class Status {
        Idle,           ///< nothing started
        Connecting,     ///< session being established
        Handshaking,    ///< admitted, still negotiating
        Joined,         ///< in a room
        Closed          ///< finished; see statusMessage() and closeCode()
    };

    /// One thing that happened, for the game loop to act on.
    struct Event {
        enum class Type {
            PeerJoined,
            PeerLeft,
            GamePayload,
            Diagnostic,
            PhaseChanged,
            Refused,        ///< something we sent was refused; not fatal
            Closed          ///< the session ended
        };

        Type              type              = Type::Closed;
        std::uint32_t     peerId            = 0;
        std::string       name;
        std::string       runtime;
        RoomRelay::Role   role              = RoomRelay::Role::Unknown;
        std::uint8_t      reason            = 0;
        std::uint8_t      channel           = 0;
        std::uint16_t     gameMessageType   = 0;
        std::uint8_t      diagnosticKind    = 0;
        RoomRelay::Phase  phase             = RoomRelay::Phase::Lobby;
        std::uint16_t     code              = 0;
        std::string       message;
        std::vector<std::uint8_t> payload;
    };

    virtual ~RoomSessionTransport() = default;

    /// Drives the transport and fills the event queue. Call once per game loop iteration.
    virtual void update() = 0;

    /// Leaves the room and closes. Safe to call more than once.
    virtual void stop(std::uint8_t reason) = 0;

    virtual bool pollEvent(Event& event) = 0;

    virtual Status status() const = 0;
    virtual bool   isJoined() const = 0;
    virtual bool   isHost() const = 0;

    virtual const std::string& roomCode() const = 0;
    /// Host-only admission response can rotate the invitation without reconnecting peers.
    virtual void updateInvitationCode(const std::string& code) = 0;

    virtual std::uint32_t    localPeerId() const = 0;
    virtual std::uint8_t     maxPeers() const = 0;
    virtual RoomRelay::Phase phase() const = 0;

    /// A player-facing sentence describing the current state or the reason it ended.
    virtual const std::string& statusMessage() const = 0;
    virtual std::uint16_t      closeCode() const = 0;

    /// Round-trip time in milliseconds, or 0 before the first sample.
    virtual Uint32 roundTripTimeMs() const = 0;

    virtual RelayTransportKind transportKind() const = 0;

    /// Bytes handed to the transport that have not left this process yet.
    virtual std::size_t outgoingBacklogBytes() const = 0;

    virtual const std::vector<Peer>& peers() const = 0;
    virtual Peer*       findPeer(std::uint32_t peerId) = 0;
    virtual const Peer* findPeer(std::uint32_t peerId) const = 0;

    /**
        Sends one serialized game packet.
        \param  payload     the packet exactly as ENetPacketOStream produced it
        \param  recipient   0 for every other peer in the room, or a peer id
    */
    virtual bool sendGamePayload(const std::uint8_t* payload, std::size_t length, int channel,
                                 std::uint32_t recipient) = 0;

    /// Host only: declares the room phase.
    virtual bool setRoomPhase(RoomRelay::Phase phase) = 0;

    /// Marks the local view of the room as in-match without waiting for the host's declaration.
    virtual void assumeMatchPhase() = 0;

    /// Sends a bounded diagnostic to the room. Never part of the ENet game protocol.
    virtual bool sendDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                                std::size_t length) = 0;

    /**
        True when gameplay travels directly between players.

        The lobby uses it for what it says to the player, and the command scheduler uses it to
        decide that the HTTP pacing budget does not apply: there is no HTTP request in the
        gameplay path of a direct session to pace.
    */
    virtual bool isDirectSession() const = 0;
};

#endif // ROOMSESSIONTRANSPORT_H
