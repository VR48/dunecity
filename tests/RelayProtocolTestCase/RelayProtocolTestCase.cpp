/*
 *  RelayProtocolTestCase.cpp - the crossplay transport's agreements with the rest of the game
 *
 *  The relay envelope itself is exercised byte by byte in tests/wasm/RelayWireHarness.cpp, which
 *  also runs under wasm32. What is checked here is something that file cannot see: three tables
 *  written independently of each other have to agree, or relay games break in ways no single
 *  table's own tests would catch.
 *
 *      1. RoomRelay::gameMessageRule()        - what the relay agrees to carry
 *      2. NetworkPacketPolicy::classifyPacket - what a receiver agrees to act on
 *      3. GamePayloadRouter::handles()        - what actually has an implementation
 *
 *  If (1) carries something (2) refuses, a relay match silently loses messages. If (1) carries
 *  something (3) does not implement, it arrives and does nothing. Both are the kind of defect
 *  that only shows up as "multiplayer is broken" weeks later.
 */

#include <catch2/catch_all.hpp>

#include <Network/GamePayloadRouter.h>
#include <Network/GameStateDigest.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomAdmissionClient.h>
#include <Network/RoomRelayProtocol.h>

#include <string>
#include <vector>

using NetworkPacketPolicy::LocalRole;
using NetworkPacketPolicy::PacketContext;
using NetworkPacketPolicy::PacketVerdict;
using NetworkPacketPolicy::PeerAdmission;
using NetworkPacketPolicy::SessionPhase;

namespace {

/// Every packet id the protocol defines.
const std::vector<Uint32> allPacketTypes = {
    NETWORKPACKET_CONNECT, NETWORKPACKET_DISCONNECT, NETWORKPACKET_PEER_CONNECTED,
    NETWORKPACKET_SENDGAMEINFO, NETWORKPACKET_SENDNAME, NETWORKPACKET_CHATMESSAGE,
    NETWORKPACKET_CHANGEEVENTLIST, NETWORKPACKET_STARTGAME, NETWORKPACKET_COMMANDLIST,
    NETWORKPACKET_SELECTIONLIST, NETWORKPACKET_CONFIG_HASH, NETWORKPACKET_SETPATHBUDGET,
    NETWORKPACKET_CLIENTSTATS, NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_REQUEST,
    NETWORKPACKET_MOD_CHUNK, NETWORKPACKET_MOD_COMPLETE, NETWORKPACKET_MOD_ACK,
    NETWORKPACKET_KEEPALIVE, NETWORKPACKET_COOP_MISSION
};

/**
    The receiving side of a relay message: whoever did not send it.

    A host-sent message is received by a client on what that client calls the host connection; a
    client-sent message is received by the host on an established client connection. On the relay
    a peer is always fully established, because the relay does not route for anybody else.
*/
PacketContext receiverContext(Uint32 packetType, bool senderIsHost, RoomRelay::Phase phase) {
    PacketContext context;
    context.packetType = packetType;
    context.localRole = senderIsHost ? LocalRole::Client : LocalRole::Host;
    context.phase = (phase == RoomRelay::Phase::Match) ? SessionPhase::InGame
                                                       : SessionPhase::Lobby;
    context.admission = PeerAdmission::Established;
    context.isHostConnection = senderIsHost;
    return context;
}

} // namespace

TEST_CASE("everything the relay carries is something the receiver will act on",
          "[relay][policy]") {
    for(const Uint32 packetType : allPacketTypes) {
        for(const bool senderIsHost : {false, true}) {
            for(const RoomRelay::Phase phase : {RoomRelay::Phase::Lobby,
                                                RoomRelay::Phase::Match}) {
                const bool relayCarries = RoomRelay::isRelayableGameMessage(
                    static_cast<std::uint16_t>(packetType), senderIsHost, phase);
                if(!relayCarries) {
                    continue;
                }

                const PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(
                    receiverContext(packetType, senderIsHost, phase));

                INFO("packet " << packetType << " sent by " << (senderIsHost ? "host" : "client")
                     << " during " << (phase == RoomRelay::Phase::Match ? "a match" : "the lobby")
                     << " was refused by the receiver: "
                     << NetworkPacketPolicy::describeVerdict(verdict));
                REQUIRE(verdict == PacketVerdict::Accept);
            }
        }
    }
}

TEST_CASE("everything the relay carries has an implementation", "[relay][router]") {
    for(const Uint32 packetType : allPacketTypes) {
        const bool relayCarries =
            RoomRelay::gameMessageRule(static_cast<std::uint16_t>(packetType)).carried;
        if(!relayCarries) {
            continue;
        }
        INFO("packet " << packetType << " is carried by the relay but nothing handles it");
        REQUIRE(GamePayloadRouter::handles(packetType));
    }
}

TEST_CASE("address-bearing and content-transfer packets never reach the relay path",
          "[relay][security]") {
    // These are the packets that can name an address to connect to, or move bytes that become
    // files. The relay refuses them, and so does the client in relay mode.
    const std::vector<Uint32> refused = {
        NETWORKPACKET_CONNECT, NETWORKPACKET_DISCONNECT, NETWORKPACKET_PEER_CONNECTED,
        NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_REQUEST, NETWORKPACKET_MOD_CHUNK,
        NETWORKPACKET_MOD_COMPLETE, NETWORKPACKET_MOD_ACK
    };

    for(const Uint32 packetType : refused) {
        INFO("packet " << packetType);
        REQUIRE_FALSE(RoomRelay::gameMessageRule(
            static_cast<std::uint16_t>(packetType)).carried);
        REQUIRE_FALSE(GamePayloadRouter::handles(packetType));
        for(const bool senderIsHost : {false, true}) {
            for(const RoomRelay::Phase phase : {RoomRelay::Phase::Lobby,
                                                RoomRelay::Phase::Match}) {
                REQUIRE_FALSE(RoomRelay::isRelayableGameMessage(
                    static_cast<std::uint16_t>(packetType), senderIsHost, phase));
            }
        }
    }
}

TEST_CASE("the shared payload handler claims exactly the shared packet set", "[relay][router]") {
    // Anything the mesh transport handles itself must not also be claimed here, or the mesh
    // handshake would be handled twice.
    REQUIRE_FALSE(GamePayloadRouter::handles(NETWORKPACKET_CONNECT));
    REQUIRE_FALSE(GamePayloadRouter::handles(NETWORKPACKET_PEER_CONNECTED));
    REQUIRE_FALSE(GamePayloadRouter::handles(NETWORKPACKET_MOD_CHUNK));

    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_SENDNAME));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_CHATMESSAGE));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_CHANGEEVENTLIST));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_CONFIG_HASH));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_COOP_MISSION));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_STARTGAME));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_COMMANDLIST));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_SELECTIONLIST));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_CLIENTSTATS));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_SETPATHBUDGET));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_SENDGAMEINFO));
    REQUIRE(GamePayloadRouter::handles(NETWORKPACKET_KEEPALIVE));

    REQUIRE_FALSE(GamePayloadRouter::handles(4242));
}

TEST_CASE("a relay name is acceptable exactly when a player name is", "[relay][policy]") {
    // Two independent copies of the same rule; the relay header cannot include the game header.
    const std::vector<std::string> names = {
        "", "stefan", std::string(64, 'x'), std::string(65, 'x'), "a b", "\xc3\xbc""mlaut"
    };
    for(const std::string& name : names) {
        INFO("name '" << name << "'");
        REQUIRE(RoomRelay::isAcceptableDisplayName(name)
                == NetworkPacketPolicy::isAcceptablePlayerName(name));
    }

    std::string withControl = "ste";
    withControl.push_back(static_cast<char>(1));
    withControl += "fan";
    REQUIRE_FALSE(RoomRelay::isAcceptableDisplayName(withControl));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(withControl));
}

TEST_CASE("the relay envelope declares the same game protocol the mesh does", "[relay]") {
    // The admission request and the handshake both carry NETWORK_PROTOCOL_VERSION; if the relay
    // envelope could not represent it, mixed versions would go undetected.
    REQUIRE(NETWORK_PROTOCOL_VERSION <= 0xFFFF);
    REQUIRE(RoomRelay::kProtocolVersion == 1);
}

TEST_CASE("a relay endpoint is only plaintext for explicit loopback development", "[relay][security]") {
    std::string error;

    REQUIRE(isAcceptableRelayUrl("wss://relay.example.net/v1/socket", false, error));
    REQUIRE_FALSE(isAcceptableRelayUrl("ws://relay.example.net/v1/socket", true, error));
    REQUIRE_FALSE(isAcceptableRelayUrl("ws://127.0.0.1:8787/v1/socket", false, error));
    REQUIRE(isAcceptableRelayUrl("ws://127.0.0.1:8787/v1/socket", true, error));
    REQUIRE_FALSE(isAcceptableRelayUrl("wss://user:pass@relay.example.net/", false, error));
    REQUIRE_FALSE(isAcceptableRelayUrl("http://relay.example.net/", false, error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("an admission answer is parsed strictly", "[relay][security]") {
    AdmissionResponse response;
    std::string error;

    const std::string good =
        "status=ok\nprotocol=1\nroom=H4PQ-7T2M-9XKB\ngrant=" + std::string(64, 'a')
        + "\ngrantExpiresMs=30000\nmaxPeers=2\nurl=wss://relay.example.net/v1/socket\n";
    REQUIRE(RoomAdmission::parseAdmissionResponse(good, response, error));
    REQUIRE(response.ok);
    REQUIRE(response.roomCode == "H4PQ-7T2M-9XKB");
    REQUIRE(response.maxPeers == 2);

    REQUIRE_FALSE(RoomAdmission::parseAdmissionResponse("", response, error));
    REQUIRE_FALSE(RoomAdmission::parseAdmissionResponse("room=X\n", response, error));
    REQUIRE_FALSE(RoomAdmission::parseAdmissionResponse(
        std::string(RoomAdmission::kMaxResponseBytes + 1, 'x'), response, error));
}

TEST_CASE("a state digest only compares like with like", "[relay][digest]") {
    GameStateDigest::Digest a;
    a.gameCycle = 200;
    a.randomSeed = 7;
    a.objectHash = 99;

    GameStateDigest::Digest b = a;
    REQUIRE_FALSE(a.divergesFrom(b));

    b.objectHash = 100;
    REQUIRE(a.divergesFrom(b));

    b = a;
    b.gameCycle = 400;
    b.objectHash = 100;
    REQUIRE_FALSE(a.divergesFrom(b));

    Uint8 encoded[GameStateDigest::kEncodedSize];
    GameStateDigest::encode(a, encoded);
    GameStateDigest::Digest decoded;
    REQUIRE(GameStateDigest::decode(encoded, sizeof(encoded), decoded));
    REQUIRE(decoded == a);
    REQUIRE_FALSE(GameStateDigest::decode(encoded, sizeof(encoded) - 1, decoded));
}
