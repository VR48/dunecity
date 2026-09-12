/*
 *  RelaySessionTestCase.cpp - what RoomRelayClient does with its event queue.
 *
 *  The wire harness checks the codec and the transport harness checks a real connection. Neither
 *  can reach the part in between: the queue that decoded events sit in until the game loop drains
 *  it, and what happens when the game loop does not drain it.
 *
 *  That queue is bounded twice over, by count and by aggregate size, and the two bounds catch
 *  different things - four thousand small events, or sixteen large ones. Both are exercised here
 *  against the production RoomRelayClient.cpp, driven through a scripted socket supplied at link
 *  time (see MockRelayWebSocket.h). There are no test hooks in the production build.
 */

#include <catch2/catch_all.hpp>

#include "MockRelayWebSocket.h"

#include <Network/NetworkPacketTypes.h>
#include <Network/RoomRelayClient.h>
#include <Network/RoomRelayProtocol.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

/// Builds a relay -> client frame the way the relay does: big-endian envelope fields.
class ServerFrameBuilder {
public:
    explicit ServerFrameBuilder(RoomRelay::ServerMessage type) {
        bytes.push_back(static_cast<std::uint8_t>(type));
    }

    ServerFrameBuilder& u8(std::uint8_t value) {
        bytes.push_back(value);
        return *this;
    }

    ServerFrameBuilder& u16(std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        return *this;
    }

    ServerFrameBuilder& u32(std::uint32_t value) {
        for(int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
        return *this;
    }

    ServerFrameBuilder& shortString(const std::string& text) {
        bytes.push_back(static_cast<std::uint8_t>(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
        return *this;
    }

    ServerFrameBuilder& raw(const std::vector<std::uint8_t>& data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    std::vector<std::uint8_t> bytes;
};

constexpr std::uint32_t kHostPeerId = 3;
constexpr std::uint32_t kLocalPeerId = 7;

std::vector<std::uint8_t> welcomeFrame() {
    return ServerFrameBuilder(RoomRelay::ServerMessage::Welcome)
        .u16(RoomRelay::kProtocolVersion)
        .u16(NETWORK_PROTOCOL_VERSION)
        .u32(kLocalPeerId)
        .u8(2)                              // this client is a client, not the host
        .shortString("H4PQ-7T2M-9XKB")
        .u8(2)
        .u8(1)                              // lobby
        .u32(static_cast<std::uint32_t>(RoomRelay::Limits::kMaxGamePayloadBytes))
        .u16(5000)
        .u16(20000)
        .bytes;
}

std::vector<std::uint8_t> hostJoinedFrame() {
    return ServerFrameBuilder(RoomRelay::ServerMessage::PeerJoined)
        .u32(kHostPeerId)
        .u8(1)                              // host
        .shortString("desktop")
        .shortString("native")
        .bytes;
}

/// A game payload exactly as ENetPacketOStream writes it: a little-endian uint32 packet id.
std::vector<std::uint8_t> gamePayload(std::uint32_t packetType, std::size_t bodyBytes) {
    std::vector<std::uint8_t> payload(4 + bodyBytes, 0x5A);
    for(int shift = 0; shift < 32; shift += 8) {
        payload[shift / 8] = static_cast<std::uint8_t>((packetType >> shift) & 0xFF);
    }
    return payload;
}

std::vector<std::uint8_t> relayFrame(std::uint32_t packetType, std::size_t bodyBytes) {
    const std::vector<std::uint8_t> payload = gamePayload(packetType, bodyBytes);
    return ServerFrameBuilder(RoomRelay::ServerMessage::Relay)
        .u32(kHostPeerId)
        .u8(0)
        .u8(1)
        .u16(static_cast<std::uint16_t>(packetType))
        .u32(static_cast<std::uint32_t>(payload.size()))
        .raw(payload)
        .bytes;
}

std::vector<std::uint8_t> roomClosedFrame(std::uint16_t code, const std::string& message) {
    ServerFrameBuilder builder(RoomRelay::ServerMessage::RoomClosed);
    builder.u16(code).u16(static_cast<std::uint16_t>(message.size()));
    builder.bytes.insert(builder.bytes.end(), message.begin(), message.end());
    return builder.bytes;
}

RoomRelayClient::Config testConfig() {
    RoomRelayClient::Config config;
    config.socketUrl   = "ws://127.0.0.1:8787/v1/socket";
    config.grant       = std::string(64, 'a');
    config.displayName = "guest";
    config.appVersion  = "1.0.655";
    config.contentHash = std::string(32, 'b');
    config.runtime     = "native";
    config.gameProtocolVersion = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    config.allowLoopbackPlaintext = true;
    return config;
}

/**
    Brings a session up to the point where the host is a known peer.

    Everything after this is about the queue, so getting here is deliberately boring: open the
    socket, let the handshake go out, answer it, announce the host.
*/
struct JoinedSession {
    RoomRelayClient client;
    MockRelayWebSocket* socket = nullptr;

    JoinedSession() {
        MockRelayTransport::reset();

        std::string error;
        REQUIRE(client.start(testConfig(), error));
        socket = MockRelayTransport::current();
        REQUIRE(socket != nullptr);

        socket->open();
        client.update();
        REQUIRE(socket->sentFrames.size() == 1);
        REQUIRE(socket->sentFrames[0][0] == 0x01);      // HELLO

        socket->deliver(welcomeFrame());
        socket->deliver(hostJoinedFrame());
        client.update();
        REQUIRE(client.isJoined());
        REQUIRE_FALSE(client.isHost());

        RoomRelayClient::Event event;
        REQUIRE(client.pollEvent(event));
        REQUIRE(event.type == RoomRelayClient::Event::Type::PeerJoined);
        REQUIRE(event.peerId == kHostPeerId);
        REQUIRE(event.role == RoomRelay::Role::Host);
        REQUIRE_FALSE(client.pollEvent(event));
    }
};

/// Drains everything queued, counting what came out.
struct DrainResult {
    int payloads = 0;
    int closes = 0;
    int others = 0;
    std::uint16_t lastCloseCode = 0;
    std::vector<RoomRelayClient::Event::Type> order;
};

DrainResult drain(RoomRelayClient& client) {
    DrainResult result;
    RoomRelayClient::Event event;
    while(client.pollEvent(event)) {
        result.order.push_back(event.type);
        switch(event.type) {
            case RoomRelayClient::Event::Type::GamePayload: result.payloads++; break;
            case RoomRelayClient::Event::Type::Closed:
                result.closes++;
                result.lastCloseCode = event.code;
                break;
            default: result.others++; break;
        }
    }
    return result;
}

} // namespace

TEST_CASE("a joined session hands the game its events in order", "[relay][session]") {
    JoinedSession session;

    session.socket->deliver(relayFrame(NETWORKPACKET_CHATMESSAGE, 16));
    session.socket->deliver(relayFrame(NETWORKPACKET_CHATMESSAGE, 32));
    session.client.update();

    REQUIRE(session.client.queuedEventCount() == 2);
    REQUIRE(session.client.queuedEventBytes() > 0);

    RoomRelayClient::Event first;
    RoomRelayClient::Event second;
    REQUIRE(session.client.pollEvent(first));
    REQUIRE(session.client.pollEvent(second));
    REQUIRE_FALSE(session.client.pollEvent(first));

    REQUIRE(first.type == RoomRelayClient::Event::Type::GamePayload);
    REQUIRE(first.payload.size() == 4 + 16);
    REQUIRE(second.payload.size() == 4 + 32);
    REQUIRE(first.peerId == kHostPeerId);

    // Draining returns the budget; a long session must not leak queue accounting.
    REQUIRE(session.client.queuedEventCount() == 0);
    REQUIRE(session.client.queuedEventBytes() == 0);
}

TEST_CASE("the event queue is bounded by aggregate size, not only by count", "[relay][session]") {
    JoinedSession session;

    // Payloads large enough that the byte budget is reached long before the count budget. If the
    // count were the only bound, this is a gigabyte of queued events and no failure at all.
    const std::size_t bodyBytes = RoomRelay::Limits::kMaxGamePayloadBytes - 4;
    const std::size_t enoughToOverflow =
        (RoomRelay::Limits::kMaxQueuedEventBytes / RoomRelay::Limits::kMaxGamePayloadBytes) + 4;

    for(std::size_t index = 0; index < enoughToOverflow; index++) {
        session.socket->deliver(relayFrame(NETWORKPACKET_CHATMESSAGE, bodyBytes));
    }
    session.client.update();

    REQUIRE(session.client.status() == RoomRelayClient::Status::Closed);
    REQUIRE(session.client.closeCode() == RoomRelay::Close::SlowConsumer);

    // The backlog is exactly what the game could not keep up with. Applying a prefix of it is
    // how a lockstep match desynchronises quietly, so none of it survives the close.
    const DrainResult drained = drain(session.client);
    REQUIRE(drained.closes == 1);
    REQUIRE(drained.payloads == 0);
    REQUIRE(drained.others == 0);
    REQUIRE(drained.lastCloseCode == RoomRelay::Close::SlowConsumer);
    REQUIRE(drained.order.size() == 1);
    REQUIRE(session.client.queuedEventBytes() == 0);
}

TEST_CASE("the event queue is also bounded by count", "[relay][session]") {
    JoinedSession session;

    // Tiny events: thousands of them fit inside the byte budget, so only the count bound can
    // stop them. Both bounds therefore have to exist; neither subsumes the other.
    for(int index = 0; index < 6000; index++) {
        session.socket->deliver(relayFrame(NETWORKPACKET_KEEPALIVE, 0));
    }
    session.client.update();

    REQUIRE(session.client.status() == RoomRelayClient::Status::Closed);
    REQUIRE(session.client.closeCode() == RoomRelay::Close::SlowConsumer);

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.closes == 1);
    REQUIRE(drained.payloads == 0);
}

TEST_CASE("an orderly close still delivers what arrived before it", "[relay][session]") {
    JoinedSession session;

    // This is the co-op continuation shape: the host sends the next mission and then leaves, and
    // both arrive in the same pump. Dropping the mission would strand the other player.
    session.socket->deliver(relayFrame(NETWORKPACKET_COOP_MISSION, 24));
    session.socket->deliver(roomClosedFrame(RoomRelay::Close::HostLeft, "The host left."));
    session.client.update();

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.payloads == 1);
    REQUIRE(drained.closes == 1);
    REQUIRE(drained.order.size() == 2);
    REQUIRE(drained.order[0] == RoomRelayClient::Event::Type::GamePayload);
    REQUIRE(drained.order[1] == RoomRelayClient::Event::Type::Closed);
    REQUIRE(drained.lastCloseCode == RoomRelay::Close::HostLeft);
}

TEST_CASE("a closed session reports that nobody is left", "[relay][session]") {
    JoinedSession session;
    REQUIRE(session.client.peers().size() == 1);

    // The host sends the next co-op mission and then the room closes, which is the shape of a
    // campaign continuation. The mission still has to resolve its sender...
    session.socket->deliver(relayFrame(NETWORKPACKET_COOP_MISSION, 24));
    session.socket->deliver(roomClosedFrame(RoomRelay::Close::HostLeft, "The host left."));
    session.client.update();

    RoomRelayClient::Event event;
    REQUIRE(session.client.pollEvent(event));
    REQUIRE(event.type == RoomRelayClient::Event::Type::GamePayload);
    REQUIRE(session.client.findPeer(kHostPeerId) != nullptr);

    // ...and only once the close itself has been handed over does the room become empty. Callers
    // ask "who is still here" to decide whether to keep waiting - for another player's commands,
    // or for the next mission - and a dead session that still lists peers leaves them waiting
    // forever for somebody who cannot answer.
    REQUIRE(session.client.pollEvent(event));
    REQUIRE(event.type == RoomRelayClient::Event::Type::Closed);
    REQUIRE(session.client.peers().empty());
    REQUIRE(session.client.findPeer(kHostPeerId) == nullptr);
}

TEST_CASE("leaving on purpose empties the room immediately", "[relay][session]") {
    JoinedSession session;
    REQUIRE(session.client.peers().size() == 1);

    session.client.stop(1);

    REQUIRE(session.client.status() == RoomRelayClient::Status::Closed);
    REQUIRE(session.client.peers().empty());
}

TEST_CASE("the terminal event is delivered exactly once", "[relay][session]") {
    JoinedSession session;

    session.socket->deliver(roomClosedFrame(RoomRelay::Close::ServerShutdown, "restarting"));
    session.client.update();
    // A second update on a closed session must not manufacture another close.
    session.client.update();
    session.client.update();

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.closes == 1);
    REQUIRE(session.client.queuedEventCount() == 0);
}

TEST_CASE("a socket that dies is reported as a close, not as silence", "[relay][session]") {
    JoinedSession session;

    session.socket->closeFromPeer(RoomRelay::Close::Timeout, "The connection stopped responding.");
    session.client.update();

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.closes == 1);
    REQUIRE(drained.lastCloseCode == RoomRelay::Close::Timeout);
    REQUIRE(session.client.status() == RoomRelayClient::Status::Closed);
}

TEST_CASE("a payload from a peer the relay never announced is ignored", "[relay][session]") {
    JoinedSession session;

    std::vector<std::uint8_t> fromStranger = relayFrame(NETWORKPACKET_CHATMESSAGE, 8);
    fromStranger[1] = 0;
    fromStranger[2] = 0;
    fromStranger[3] = 0;
    fromStranger[4] = 99;       // a sender id that was never announced to us

    session.socket->deliver(fromStranger);
    session.client.update();

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.payloads == 0);
    REQUIRE(drained.closes == 0);
    REQUIRE(session.client.status() == RoomRelayClient::Status::Joined);
}

TEST_CASE("a message the sender's role may not send is refused by the client too",
          "[relay][session][security]") {
    JoinedSession session;

    // CLIENTSTATS only ever travels from a client to the host, so the host sending one is a rule
    // violation. The relay applies this rule as well; applying it again here means a relay that
    // has been tampered with still cannot make this client act on a forbidden message.
    session.socket->deliver(relayFrame(NETWORKPACKET_CLIENTSTATS, 20));
    session.client.update();

    const DrainResult drained = drain(session.client);
    REQUIRE(drained.payloads == 0);
    REQUIRE(session.client.status() == RoomRelayClient::Status::Joined);
}

TEST_CASE("a session refuses to start when the platform cannot open sockets",
          "[relay][session]") {
    MockRelayTransport::reset();
    MockRelayTransport::setSupported(false, "This browser cannot open game connections.");

    RoomRelayClient client;
    std::string error;
    REQUIRE_FALSE(client.start(testConfig(), error));
    REQUIRE(error == "This browser cannot open game connections.");
    REQUIRE(client.status() == RoomRelayClient::Status::Closed);

    MockRelayTransport::reset();
}

TEST_CASE("a session refuses an endpoint that is not acceptable", "[relay][session][security]") {
    MockRelayTransport::reset();

    RoomRelayClient client;
    RoomRelayClient::Config config = testConfig();
    config.allowLoopbackPlaintext = false;      // plain ws to loopback now needs the opt-in

    std::string error;
    REQUIRE_FALSE(client.start(config, error));
    REQUIRE_FALSE(error.empty());
    // Nothing was even asked of the transport.
    REQUIRE(MockRelayTransport::lastUrl().empty());

    MockRelayTransport::reset();
}
