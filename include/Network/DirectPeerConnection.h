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

#ifndef DIRECTPEERCONNECTION_H
#define DIRECTPEERCONNECTION_H

/**
    One WebRTC data channel to one other player, with the platform hidden.

    Two implementations exist and they have to produce the same bytes, because they talk to each
    other: natively this is libdatachannel (src/Network/DirectPeerConnectionLibdatachannel.cpp),
    in the browser it is P2PKit's RTCTransport reached through a small bridge
    (src/Network/DirectPeerConnectionEmscripten.cpp). What goes through the channel is defined by
    include/Network/P2PWireFraming.h and by nothing else.

    Everything is queued, in both directions. libdatachannel calls back on its own threads and the
    browser calls back from the event loop; neither may touch the simulation, so both push into
    queues that the game loop drains through pollLocalSignal() and pollMessage(). The transport
    above this interface never sees a callback.
*/

#include <Network/P2PSignalingProtocol.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class DirectPeerConnection {
public:
    enum class State {
        New,        ///< created, nothing negotiated yet
        Connecting, ///< negotiating or gathering
        Connected,  ///< the data channel is open and gameplay may flow
        Failed,     ///< it will not connect; lastError() says what happened
        Closed      ///< finished
    };

    /// Something to publish to the other side through the signaling service.
    struct LocalSignal {
        P2PSignal::SignalKind kind    = P2PSignal::SignalKind::Offer;
        std::string           payload;    ///< SDP text, or one candidate line
    };

    virtual ~DirectPeerConnection() = default;

    /// Applies a remote description. The caller has already verified its DTLS fingerprint.
    virtual bool setRemoteDescription(P2PSignal::SignalKind kind, const std::string& sdp) = 0;

    /// Applies one remote ICE candidate. Refused if it would route through a relay.
    virtual bool addRemoteCandidate(const std::string& candidate) = 0;

    /**
        Sends one JSON value, exactly as RTCTransport.send() does.

        Fragmentation belongs to the backend, because the browser's belongs to P2PKit: the
        browser backend hands the value to RTCTransport and lets its Chunker split it, and the
        native backend splits it itself with include/Network/P2PWireFraming.h so that the two
        produce the same channel messages. Callers above this interface never see a fragment.

        \return false when the channel is not open, or is too backlogged to take more
    */
    virtual bool sendValue(const std::string& value) = 0;

    /// Bytes handed to the channel that it has not put on the network yet.
    virtual std::size_t bufferedAmount() const = 0;

    /// Drives the backend if it needs driving, and moves callback results into the queues.
    virtual void update() = 0;

    virtual void close() = 0;

    /// Takes one locally produced offer, answer or candidate. False when there is none.
    virtual bool pollLocalSignal(LocalSignal& out) = 0;

    /// Takes one fully reassembled JSON value. False when there is none.
    virtual bool pollValue(std::string& out) = 0;

    virtual State state() const = 0;

    /// Non player-facing detail for the log; the transport writes the player's sentence.
    virtual const std::string& lastError() const = 0;
};

struct DirectPeerConnectionOptions {
    /// STUN urls only. A TURN url never reaches here: P2PSignal::isAcceptableIceUrl refuses it.
    std::vector<std::string> iceServers;
    /**
        Which side creates the offer and the data channel.

        Decided by peer id, not by role, so both sides agree without another round trip and a
        rejoining host cannot end up offering to someone who is also offering.
    */
    bool        initiator = false;
    /**
        Data channel label.

        "p2pkit" is what the browser transport opens, and the native backend refuses a channel
        with any other label, so a peer cannot open a second, differently-shaped channel beside
        the negotiated one.
    */
    std::string label     = "p2pkit";
};

/// True when this build has a real peer connection backend.
bool isDirectPeerConnectionAvailable();

/**
    Creates one peer connection.
    \param  error  set to a non player-facing reason when the result is null
*/
std::unique_ptr<DirectPeerConnection> createDirectPeerConnection(const DirectPeerConnectionOptions& options,
                                                                std::string& error);

#endif // DIRECTPEERCONNECTION_H
