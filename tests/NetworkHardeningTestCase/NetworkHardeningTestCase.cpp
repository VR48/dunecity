/*
 *  NetworkHardeningTestCase.cpp - regression tests for the network trust boundary
 *
 *  These tests drive the same functions and parsers the production receive path uses:
 *  NetworkPacketPolicy::classifyPacket() (called from NetworkManager::admitPacket),
 *  CommandValidation (called from CommandManager::addCommandList and Command's stream
 *  constructor), PathBudgetSync (called from Game::handleSetPathBudget), the real
 *  ENetPacketIStream over real ENet packets, and the real ChangeEventList parser.
 *
 *  Packets are built as raw bytes wherever a hostile sender would, so the fixtures are the
 *  malformed wire images themselves rather than a description of them.
 */

#include <catch2/catch_all.hpp>

#include <CommandValidation.h>
#include <DataTypes.h>
#include <Network/ChangeEventList.h>
#include <Network/ENetPacketIStream.h>
#include <Network/ENetPacketOStream.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/PathBudgetSync.h>
#include <mod/ModTransferValidation.h>

#include <enet/enet.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using NetworkPacketPolicy::LocalRole;
using NetworkPacketPolicy::PacketContext;
using NetworkPacketPolicy::PacketVerdict;
using NetworkPacketPolicy::PeerAdmission;
using NetworkPacketPolicy::SessionPhase;

namespace {

struct ENetRuntime {
    ENetRuntime() {
        if(enet_initialize() != 0) {
            throw std::runtime_error("Failed to initialize ENet");
        }
    }
    ~ENetRuntime() { enet_deinitialize(); }
};

/// Builds a raw wire image the way a hostile peer would.
class PacketBuilder {
public:
    PacketBuilder& u8(Uint8 value) {
        bytes.push_back(value);
        return *this;
    }

    PacketBuilder& u16(Uint16 value) {
        bytes.push_back(static_cast<Uint8>(value & 0xFF));
        bytes.push_back(static_cast<Uint8>((value >> 8) & 0xFF));
        return *this;
    }

    PacketBuilder& u32(Uint32 value) {
        for(int i = 0; i < 4; i++) {
            bytes.push_back(static_cast<Uint8>((value >> (8 * i)) & 0xFF));
        }
        return *this;
    }

    PacketBuilder& u64(Uint64 value) {
        for(int i = 0; i < 8; i++) {
            bytes.push_back(static_cast<Uint8>((value >> (8 * i)) & 0xFF));
        }
        return *this;
    }

    PacketBuilder& raw(const std::string& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
        return *this;
    }

    /// Length-prefixed string, exactly as ENetPacketOStream::writeString() encodes it.
    PacketBuilder& str(const std::string& value) {
        u32(static_cast<Uint32>(value.size()));
        return raw(value);
    }

    /// A string header with a length that does not describe the bytes that follow.
    PacketBuilder& lyingStringLength(Uint32 claimedLength, const std::string& actualBytes) {
        u32(claimedLength);
        return raw(actualBytes);
    }

    ENetPacket* build() const {
        ENetPacket* packet = enet_packet_create(bytes.empty() ? nullptr : bytes.data(),
                                                bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        REQUIRE(packet != nullptr);
        return packet;
    }

private:
    std::vector<Uint8> bytes;
};

PacketContext context(Uint32 packetType, LocalRole role, SessionPhase phase,
                      PeerAdmission admission, bool isHostConnection) {
    PacketContext ctx;
    ctx.packetType = packetType;
    ctx.localRole = role;
    ctx.phase = phase;
    ctx.admission = admission;
    ctx.isHostConnection = isHostConnection;
    return ctx;
}

} // namespace

// =============================================================================
// Packet admission: pre-handshake, forged host messages, role and phase
// =============================================================================

TEST_CASE("Admission: a peer that has not completed the handshake can only drive the handshake",
          "[network][security][admission]") {
    const Uint32 handshakePackets[] = {
        NETWORKPACKET_SENDNAME, NETWORKPACKET_CONFIG_HASH, NETWORKPACKET_KEEPALIVE
    };
    for(const Uint32 packetType : handshakePackets) {
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::Lobby,
                            PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    }

    const Uint32 refusedBeforeHandshake[] = {
        NETWORKPACKET_CHATMESSAGE, NETWORKPACKET_CHANGEEVENTLIST, NETWORKPACKET_COMMANDLIST,
        NETWORKPACKET_SELECTIONLIST, NETWORKPACKET_CLIENTSTATS, NETWORKPACKET_MOD_REQUEST,
        NETWORKPACKET_MOD_ACK, NETWORKPACKET_PEER_CONNECTED
    };
    for(const Uint32 packetType : refusedBeforeHandshake) {
        const PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(
            context(packetType, LocalRole::Host, SessionPhase::Lobby,
                    PeerAdmission::Handshaking, false));
        INFO("packet type " << packetType);
        REQUIRE(verdict != PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: a connection without peer state is never obeyed",
          "[network][security][admission]") {
    for(Uint32 packetType = 0; packetType <= NETWORKPACKET_COOP_MISSION; packetType++) {
        const PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(
            context(packetType, LocalRole::Client, SessionPhase::Lobby,
                    PeerAdmission::Unidentified, true));
        INFO("packet type " << packetType);
        REQUIRE(verdict != PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: host-only control messages are refused from a peer that is not the host",
          "[network][security][admission][forgery]") {
    const Uint32 hostOnlyPackets[] = {
        NETWORKPACKET_STARTGAME, NETWORKPACKET_SETPATHBUDGET, NETWORKPACKET_CONNECT,
        NETWORKPACKET_DISCONNECT, NETWORKPACKET_SENDGAMEINFO, NETWORKPACKET_COOP_MISSION,
        NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_CHUNK, NETWORKPACKET_MOD_COMPLETE
    };

    for(const Uint32 packetType : hostOnlyPackets) {
        // Another established mesh peer forging the host's control traffic.
        const bool inGamePacket = (packetType == NETWORKPACKET_SETPATHBUDGET);
        const SessionPhase phase = inGamePacket ? SessionPhase::InGame : SessionPhase::Lobby;

        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, phase,
                            PeerAdmission::Established, false))
                == PacketVerdict::RejectNotHostPeer);

        // The same packet arriving on the host, where there is no host connection at all.
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, phase,
                            PeerAdmission::Established, false))
                == PacketVerdict::RejectWrongRole);

        // And the legitimate case still works.
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, phase,
                            PeerAdmission::Established, true))
                == PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: client-only reports are refused on a client and accepted by the host",
          "[network][security][admission]") {
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CLIENTSTATS, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, true)) == PacketVerdict::RejectWrongRole);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CLIENTSTATS, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);

    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_REQUEST, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::RejectWrongRole);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_ACK, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
}

TEST_CASE("Admission: lobby-only packets stop being accepted once the match runs",
          "[network][security][admission][phase]") {
    const Uint32 lobbyOnly[] = {
        NETWORKPACKET_SENDNAME, NETWORKPACKET_CONFIG_HASH, NETWORKPACKET_CHANGEEVENTLIST,
        NETWORKPACKET_STARTGAME, NETWORKPACKET_COOP_MISSION, NETWORKPACKET_SENDGAMEINFO,
        NETWORKPACKET_CONNECT, NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_CHUNK,
        NETWORKPACKET_MOD_COMPLETE
    };
    for(const Uint32 packetType : lobbyOnly) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::Lobby,
                            PeerAdmission::Established, true)) == PacketVerdict::Accept);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::InGame,
                            PeerAdmission::Established, true)) == PacketVerdict::RejectWrongPhase);
    }

    // A rename during a match is exactly how a peer would try to take over another player's
    // commands, because command lists are resolved to a player by name.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::RejectWrongPhase);
}

TEST_CASE("Admission: in-game traffic is refused while still in the lobby",
          "[network][security][admission][phase]") {
    const Uint32 inGameOnly[] = {
        NETWORKPACKET_COMMANDLIST, NETWORKPACKET_SELECTIONLIST, NETWORKPACKET_CLIENTSTATS
    };
    for(const Uint32 packetType : inGameOnly) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::Lobby,
                            PeerAdmission::Established, false)) == PacketVerdict::RejectWrongPhase);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::InGame,
                            PeerAdmission::Established, false)) == PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: unknown packet types are refused", "[network][security][admission]") {
    for(const Uint32 packetType : {0u, 21u, 999u, 0xFFFFFFFFu}) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::Lobby,
                            PeerAdmission::Established, true)) == PacketVerdict::RejectUnknownType);
    }
}

TEST_CASE("Admission: a normal join, lobby and match sequence is accepted end to end",
          "[network][security][admission][compatibility]") {
    // Client side of a join: the connection to the host starts out handshaking.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDGAMEINFO, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CONFIG_HASH, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    // A mesh peer that connected to us sends its name before it is in the peer list.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    // Co-op mission selection and mod sync from the host.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COOP_MISSION, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_CHUNK, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    // Match running: command and selection traffic from any established mesh peer.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COMMANDLIST, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SELECTIONLIST, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SETPATHBUDGET, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);

    // Host side of the same session.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_PEER_CONNECTED, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CHANGEEVENTLIST, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_REQUEST, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COMMANDLIST, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
}

// =============================================================================
// Identity, mesh targets, received map names, client stats
// =============================================================================

TEST_CASE("Player names are bounded and free of control characters",
          "[network][security][identity]") {
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName("Stefan"));
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName("Player 1 (host)"));
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName(std::string(24, 'a')));

    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(""));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(std::string(65, 'a')));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(std::string("na\0me", 5)));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName("line\nbreak"));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName("bell\x07"));
}

TEST_CASE("Mesh connect targets must be plausible unicast addresses",
          "[network][security][mesh]") {
    const Uint32 loopback = 0x7F000001;     // 127.0.0.1
    const Uint32 privateLan = 0xC0A80105;   // 192.168.1.5
    const Uint32 publicHost = 0x08080808;   // 8.8.8.8

    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(loopback, 28747));
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 28747));
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(publicHost, 1));

    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(loopback, 0));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0x00000000, 28747));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0x000000FF, 28747));   // 0.0.0.255
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0xFFFFFFFF, 28747));   // broadcast
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0xE0000001, 28747));   // 224.0.0.1
}

TEST_CASE("Received map filenames cannot escape the multiplayer maps directory",
          "[network][security][map]") {
    std::string sanitized;

    SECTION("legitimate custom maps keep working") {
        REQUIRE(NetworkPacketPolicy::sanitizeReceivedMapFilename("Arrakis Duel.ini", sanitized));
        REQUIRE(sanitized == "Arrakis Duel.ini");

        REQUIRE(NetworkPacketPolicy::sanitizeReceivedMapFilename("4P_Spice_Bowl", sanitized));
        REQUIRE(sanitized == "4P_Spice_Bowl.ini");
    }

    SECTION("traversal, absolute and control names are refused") {
        const char* dangerous[] = {
            "../../../../etc/passwd",
            "..",
            "../evil.ini",
            "maps/../../evil.ini",
            "/etc/cron.d/evil.ini",
            "C:\\Windows\\System32\\evil.ini",
            "sub/dir.ini",
            "back\\slash.ini",
            "CON",
            "LPT1.ini",
            "trailing.",
            "trailing ",
            "bell\x07.ini"
        };
        for(const char* name : dangerous) {
            INFO("filename " << name);
            REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(name, sanitized));
        }

        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(
            std::string("nul\0byte.ini", 12), sanitized));
        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename("", sanitized));
        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(
            std::string(200, 'a') + ".ini", sanitized));
    }
}

TEST_CASE("Client stat values must be finite and non-negative",
          "[network][security][pathbudget]") {
    REQUIRE(NetworkPacketPolicy::isUsableStatValue(0.0f));
    REQUIRE(NetworkPacketPolicy::isUsableStatValue(59.94f));

    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(std::numeric_limits<float>::infinity()));
    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(-1.0f));
}

// =============================================================================
// Wire decoding: ENetPacketIStream over real packets
// =============================================================================

TEST_CASE_METHOD(ENetRuntime, "Wire: well formed packets still decode",
                 "[network][security][wire][compatibility]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeUint32(NETWORKPACKET_CLIENTSTATS);
    ostream.writeUint32(750);
    ostream.writeFloat(59.5f);
    ostream.writeString("stefan");
    ostream.writeBool(true);
    ostream.writeBool(false);
    ostream.writeUint64(0x0123456789ABCDEFULL);

    ENetPacketIStream istream(ostream.getPacket());
    REQUIRE(istream.readUint32() == NETWORKPACKET_CLIENTSTATS);
    REQUIRE(istream.readUint32() == 750);
    REQUIRE(istream.readFloat() == Catch::Approx(59.5f));
    REQUIRE(istream.readString() == "stefan");
    REQUIRE(istream.readBool() == true);
    REQUIRE(istream.readBool() == false);
    REQUIRE(istream.readUint64() == 0x0123456789ABCDEFULL);
    REQUIRE(istream.getRemainingLength() == 0);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: unaligned fields decode correctly",
                 "[network][security][wire]") {
    // A single leading byte pushes every following field off its natural alignment. The old
    // implementation dereferenced typed pointers here, which is undefined behaviour.
    PacketBuilder builder;
    builder.u8(0xA5).u16(0xBEEF).u32(0xDEADBEEF).u64(0x0011223344556677ULL);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.readUint8() == 0xA5);
    REQUIRE(istream.readUint16() == 0xBEEF);
    REQUIRE(istream.readUint32() == 0xDEADBEEF);
    REQUIRE(istream.readUint64() == 0x0011223344556677ULL);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: truncated packets raise end of file",
                 "[network][security][wire]") {
    SECTION("nothing at all") {
        PacketBuilder builder;
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32(), InputStream::eof);
    }

    SECTION("half a field") {
        PacketBuilder builder;
        builder.u16(0x1234);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32(), InputStream::eof);
    }

    SECTION("string header without the string") {
        PacketBuilder builder;
        builder.lyingStringLength(64, "short");
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readString(), InputStream::eof);
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: overflowing string lengths are refused, not wrapped",
                 "[network][security][wire][overflow]") {
    // On wasm32 size_t is 32 bits, so the old "currentPos + length > dataLength" check wrapped
    // for these lengths and let the read run past the end of the packet.
    const Uint32 overflowLengths[] = {
        0xFFFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFFDu, 0x80000000u, 0x7FFFFFFFu
    };

    for(const Uint32 claimedLength : overflowLengths) {
        INFO("claimed length " << claimedLength);
        PacketBuilder builder;
        builder.u32(NETWORKPACKET_CHATMESSAGE).lyingStringLength(claimedLength, "abcd");
        ENetPacketIStream istream(builder.build());
        REQUIRE(istream.readUint32() == NETWORKPACKET_CHATMESSAGE);
        REQUIRE_THROWS_AS(istream.readString(), InputStream::eof);
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: booleans other than 0 and 1 are refused",
                 "[network][security][wire]") {
    PacketBuilder builder;
    builder.u8(1).u8(0).u8(2).u8(0xFF);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.readBool() == true);
    REQUIRE(istream.readBool() == false);
    REQUIRE_THROWS_AS(istream.readBool(), InputStream::error);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: collection counts are bounded by the bytes that remain",
                 "[network][security][wire][overflow]") {
    SECTION("a set that claims four billion entries") {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu).u32(1).u32(2);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32Set(), InputStream::eof);
    }

    SECTION("a vector that claims more entries than the packet can hold") {
        PacketBuilder builder;
        builder.u32(1000).u32(7);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32Vector(), InputStream::eof);
    }

    SECTION("an honest collection still decodes") {
        PacketBuilder builder;
        builder.u32(3).u32(10).u32(20).u32(30);
        ENetPacketIStream istream(builder.build());
        const std::set<Uint32> decoded = istream.readUint32Set();
        REQUIRE(decoded == std::set<Uint32>{10, 20, 30});
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: remaining length tracks consumption",
                 "[network][security][wire]") {
    PacketBuilder builder;
    builder.u32(1).u32(2);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.getRemainingLength() == 8);
    REQUIRE(istream.readUint32() == 1);
    REQUIRE(istream.getRemainingLength() == 4);
    REQUIRE(istream.readUint32() == 2);
    REQUIRE(istream.getRemainingLength() == 0);
}

// =============================================================================
// Lobby event parsing (the real ChangeEventList parser)
// =============================================================================

TEST_CASE_METHOD(ENetRuntime, "Lobby: a normal change event list round-trips",
                 "[network][security][lobby][compatibility]") {
    ChangeEventList original;
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeHouse, 0u, 2u);
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeTeam, 1u, 3u);
    original.changeEventList.emplace_back(1u, std::string("stefan"));

    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    original.save(ostream);

    ENetPacketIStream istream(ostream.getPacket());
    ChangeEventList decoded(istream);

    REQUIRE(decoded.changeEventList.size() == 3);
    auto iter = decoded.changeEventList.begin();
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::ChangeHouse);
    REQUIRE(iter->slot == 0);
    REQUIRE(iter->newValue == 2);
    ++iter;
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::ChangeTeam);
    ++iter;
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer);
    REQUIRE(iter->newStringValue == "stefan");
}

TEST_CASE_METHOD(ENetRuntime, "Lobby: malformed change event lists are refused",
                 "[network][security][lobby]") {
    SECTION("event count near the 32 bit maximum") {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("more events than a lobby can ever hold") {
        PacketBuilder builder;
        builder.u32(4096);
        for(int i = 0; i < 4096; i++) {
            builder.u32(0).u32(0).u32(0);
        }
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("unknown event type") {
        PacketBuilder builder;
        builder.u32(1).u32(99).u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("truncated event") {
        PacketBuilder builder;
        builder.u32(2).u32(0).u32(0).u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("a slot far outside the lobby survives parsing and is caught by the handler bound") {
        // The parser is deliberately permissive about the slot value; the receiving handler
        // rejects it against numHouses. What matters here is that it never reaches an array.
        PacketBuilder builder;
        builder.u32(1).u32(0).u32(0xFFFFFFFFu).u32(0);
        ENetPacketIStream istream(builder.build());
        ChangeEventList decoded(istream);
        REQUIRE(decoded.changeEventList.size() == 1);
        REQUIRE(decoded.changeEventList.front().slot == 0xFFFFFFFFu);
        REQUIRE(decoded.changeEventList.front().slot >= static_cast<Uint32>(MAX_CUSTOM_GAME_PLAYERS));
    }
}

// =============================================================================
// Command validation (used by CommandManager::addCommandList)
// =============================================================================

TEST_CASE("Commands: unknown ids are refused", "[network][security][command]") {
    REQUIRE(CommandValidation::isKnownCommandID(CMD_UNIT_MOVE2POS));
    REQUIRE(CommandValidation::isKnownCommandID(CMD_MAX - 1));

    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_NONE));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_MAX));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_MAX + 1));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(0xFFFFFFFFu));
}

TEST_CASE("Commands: parameter counts must match the command table",
          "[network][security][command]") {
    // Exactly the counts Command::executeCommand() requires; a mismatch makes it throw out of
    // the simulation loop, which takes down every peer that accepted the command.
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 4));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 5));

    REQUIRE(CommandValidation::isWellFormedCommand(CMD_PLACE_STRUCTURE, 3));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_MCV_DEPLOY, 1));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_PLAYER_PAUSE, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_PLAYER_PAUSE, 1));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_STRUCTURE_DEMOLISH, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_STRUCTURE_DEMOLISH, 0));

    // The city commands read up to three optional parameters.
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_CITY_PLACE_ZONE, 3));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_CITY_TOOL, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_CITY_TOOL, 4));

    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_NONE, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MAX, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MCV_DEPLOY, 1000000));
}

TEST_CASE("Commands: the whole command table is covered", "[network][security][command]") {
    // Every executable command must have an entry, otherwise legitimate play would be dropped.
    for(Uint32 commandID = CMD_NONE + 1; commandID < CMD_MAX; commandID++) {
        INFO("command id " << commandID);
        const int arity = CommandValidation::exactParameterCount(static_cast<CMDTYPE>(commandID));
        REQUIRE(arity != -2);

        const std::size_t validCount = (arity == -1) ? 3 : static_cast<std::size_t>(arity);
        REQUIRE(CommandValidation::isWellFormedCommand(commandID, validCount));
    }
}

TEST_CASE("Commands: cycle windows bound the scheduling vector",
          "[network][security][command][overflow]") {
    const Uint32 currentCycle = 1000;
    const Uint32 buffer = 10;

    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle, currentCycle, buffer));
    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle + buffer, currentCycle, buffer));
    // Retransmissions of the rolling history reference cycles that already passed.
    REQUIRE(CommandValidation::isAcceptableCommandCycle(0, currentCycle, buffer));
    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle - 100, currentCycle, buffer));

    // A cycle number of four billion would resize the timeslot vector to four billion entries.
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(0xFFFFFFFEu, currentCycle, buffer));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(0x80000000u, currentCycle, buffer));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(
        CommandValidation::maxAcceptableCommandCycle(currentCycle, buffer) + 1, currentCycle, buffer));

    SECTION("the window arithmetic itself cannot wrap") {
        const Uint32 lateCycle = 0xFFFFFFF0u;
        REQUIRE(CommandValidation::maxAcceptableCommandCycle(lateCycle, buffer)
                == std::numeric_limits<Uint32>::max());
        // A nonsensical buffer must not wrap the limit round to a small number.
        REQUIRE(CommandValidation::maxAcceptableCommandCycle(0, 0xFFFFFFFFu)
                >= 0x7FFFFFFFu);
        REQUIRE(CommandValidation::isAcceptableCommandCycle(0xFFFFFFFFu, lateCycle, buffer));
    }

    SECTION("the processed watermark saturates instead of wrapping to zero") {
        REQUIRE(CommandValidation::nextCycleAfter(41) == 42);
        REQUIRE(CommandValidation::nextCycleAfter(std::numeric_limits<Uint32>::max())
                == std::numeric_limits<Uint32>::max());
    }
}

TEST_CASE("Commands: batch counts are bounded before allocation",
          "[network][security][command][overflow]") {
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(0));
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(64));
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(
        CommandValidation::kMaxCommandListEntries));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandListEntryCount(
        CommandValidation::kMaxCommandListEntries + 1));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandListEntryCount(0xFFFFFFFFu));

    REQUIRE(CommandValidation::isAcceptableCommandCountPerEntry(32));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCountPerEntry(0xFFFFFFFFu));
}

// =============================================================================
// Path budget orders (used by Game::handleSetPathBudget)
// =============================================================================

TEST_CASE("Path budget: only host-scheduled orders are accepted",
          "[network][security][pathbudget]") {
    const uint32_t interval = 375;
    const size_t minBudget = 5000;
    const size_t maxBudget = 25000;
    const uint32_t currentCycle = 750;
    const uint32_t applyCycle = PathBudgetSync::calculateApplyCycle(currentCycle, interval);

    REQUIRE(applyCycle == 1125);
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(15000, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(minBudget, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(maxBudget, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));

    SECTION("out of range budgets desync the pathfinding schedule") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(0, applyCycle, currentCycle,
                                                              interval, minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(maxBudget + 1, applyCycle,
                                                              currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(0xFFFFFFFFu, applyCycle, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("apply cycles that are not interval boundaries are refused") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, applyCycle + 1, currentCycle,
                                                              interval, minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, currentCycle + 1, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("orders from the past or the far future are refused") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 375, currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 375000, currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 0xFFFFFF00u, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("the pending queue is bounded") {
        REQUIRE(PathBudgetSync::kMaxPendingBudgetChanges > 0);
        REQUIRE(PathBudgetSync::kMaxPendingBudgetChanges <= 16);
    }
}

// =============================================================================
// Mod payload boundaries (used by ModManager::saveReceivedMod)
// =============================================================================

TEST_CASE("Mod unpack: payload bounds cannot be wrapped by a crafted length",
          "[network][security][mod-transfer][overflow]") {
    const std::size_t payloadSize = 1024;

    REQUIRE(ModTransferValidation::fitsWithinPayload(0, 1024, payloadSize));
    REQUIRE(ModTransferValidation::fitsWithinPayload(1000, 24, payloadSize));
    REQUIRE(ModTransferValidation::fitsWithinPayload(1024, 0, payloadSize));

    REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(1000, 25, payloadSize));
    REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(1025, 0, payloadSize));

    // The lengths that wrap "offset + length" when size_t is 32 bits (wasm32).
    const std::uint64_t wrappingLengths[] = {0xFFFFFFFFull, 0xFFFFFFF0ull, 0xFFFFFF00ull};
    for(const std::uint64_t length : wrappingLengths) {
        INFO("length " << length);
        REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(8, length, payloadSize));
        REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(payloadSize - 4, length, payloadSize));
    }
}

TEST_CASE("Mod unpack: payload paths stay inside the mod directory",
          "[network][security][mod-transfer]") {
    std::filesystem::path normalized;

    REQUIRE(ModTransferValidation::normalizeRelativeFilePath("mod.ini", normalized));
    REQUIRE(normalized.generic_string() == "mod.ini");

    const char* dangerous[] = {
        "../outside.ini", "data/../../outside.ini", "/absolute.ini", "C:/absolute.ini",
        "data/NUL.png", "trailing.", "data//double.png"
    };
    for(const char* name : dangerous) {
        INFO("path " << name);
        REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(name, normalized));
    }
}
