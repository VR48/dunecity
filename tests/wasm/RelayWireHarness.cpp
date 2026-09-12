/*
 *  RelayWireHarness.cpp - standalone regression harness for the room relay wire boundary
 *
 *  Same idea as NetworkWireHarness.cpp, for the crossplay transport: the relay envelope, the
 *  endpoint validation, the admission response parser and the deterministic state digest are all
 *  parsed or produced from untrusted input, and their length checks behave differently when
 *  size_t is 32 bits. The browser client is wasm32, so that case has to be exercised for real.
 *
 *  It has no SDL, no ENet, no Catch2 and no game data: it compiles the production headers
 *  unchanged and drives them with crafted images.
 *
 *  Build and run: see tests/wasm/run-relay-wire-harness.sh
 *
 *  Exit code 0 means every check passed; any failure prints the failing check and exits 1.
 */

#include <Network/GameStateDigest.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/RelayHttpTransport.h>
#include <Network/RelayPollProtocol.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomAdmissionClient.h>
#include <Network/RoomRelayProtocol.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
    checks++;
    if(!condition) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

/// Builds a relay -> client frame the way the Node relay does: big-endian envelope fields.
class FrameBuilder {
public:
    explicit FrameBuilder(RoomRelay::ServerMessage type) {
        bytes.push_back(static_cast<std::uint8_t>(type));
    }

    FrameBuilder& u8(std::uint8_t value) {
        bytes.push_back(value);
        return *this;
    }

    FrameBuilder& u16(std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        return *this;
    }

    FrameBuilder& u32(std::uint32_t value) {
        for(int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
        return *this;
    }

    FrameBuilder& shortString(const std::string& text) {
        bytes.push_back(static_cast<std::uint8_t>(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
        return *this;
    }

    FrameBuilder& raw(const std::vector<std::uint8_t>& data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    std::vector<std::uint8_t> bytes;
};

/// The game payload as ENetPacketOStream writes it: a little-endian uint32 packet id first.
std::vector<std::uint8_t> gamePayload(std::uint32_t packetType, std::size_t extraBytes = 0) {
    std::vector<std::uint8_t> payload(4 + extraBytes, 0x5A);
    for(int shift = 0; shift < 32; shift += 8) {
        payload[shift / 8] = static_cast<std::uint8_t>((packetType >> shift) & 0xFF);
    }
    return payload;
}

bool decodes(const std::vector<std::uint8_t>& frame, RoomRelay::ServerFrame& out) {
    std::string error;
    return RoomRelay::decodeServerFrame(frame.data(), frame.size(), out, error);
}

std::vector<std::uint8_t> welcomeFrame(const std::string& roomCode = "H4PQ-7T2M-9XKB",
                                       std::uint32_t peerId = 7, std::uint8_t role = 1,
                                       std::uint8_t phase = 1, std::uint8_t maxPeers = 2) {
    return FrameBuilder(RoomRelay::ServerMessage::Welcome)
        .u16(RoomRelay::kProtocolVersion)
        .u16(5)
        .u32(peerId)
        .u8(role)
        .shortString(roomCode)
        .u8(maxPeers)
        .u8(phase)
        .u32(static_cast<std::uint32_t>(RoomRelay::Limits::kMaxGamePayloadBytes))
        .u16(5000)
        .u16(20000)
        .bytes;
}

/// `declaredLength` below zero means "declare the real payload length"; anything else is sent
/// verbatim, which is how a lying length gets tested.
std::vector<std::uint8_t> relayFrame(std::uint32_t sender, std::uint16_t declaredType,
                                     const std::vector<std::uint8_t>& payload,
                                     std::uint8_t channel = 0,
                                     long long declaredLength = -1) {
    const std::uint32_t length = (declaredLength < 0)
        ? static_cast<std::uint32_t>(payload.size())
        : static_cast<std::uint32_t>(static_cast<unsigned long long>(declaredLength));
    return FrameBuilder(RoomRelay::ServerMessage::Relay)
        .u32(sender)
        .u8(channel)
        .u8(1)
        .u16(declaredType)
        .u32(length)
        .raw(payload)
        .bytes;
}

// --- envelope ---------------------------------------------------------------------------

void testWelcome() {
    RoomRelay::ServerFrame frame;

    check(decodes(welcomeFrame(), frame), "a well-formed WELCOME is accepted");
    check(frame.type == RoomRelay::ServerMessage::Welcome, "WELCOME is identified");
    check(frame.peerId == 7, "WELCOME carries the assigned peer id");
    check(frame.role == RoomRelay::Role::Host, "WELCOME carries the assigned role");
    check(frame.roomCode == "H4PQ-7T2M-9XKB", "WELCOME carries the room code");
    check(frame.maxPeers == 2, "WELCOME carries the room size");

    check(!decodes(welcomeFrame("H4PQ-7T2M-9XKB", 0), frame),
          "WELCOME may not assign the broadcast id");
    check(!decodes(welcomeFrame("H4PQ-7T2M-9XKB", 7, 9), frame),
          "WELCOME may not name an unknown role");
    check(!decodes(welcomeFrame("H4PQ-7T2M-9XKB", 7, 1, 7), frame),
          "WELCOME may not name an unknown phase");
    check(!decodes(welcomeFrame("H4PQ-7T2M-9XKB", 7, 1, 1, 0), frame),
          "WELCOME may not name an empty room");
    check(!decodes(welcomeFrame("H4PQ-7T2M-9XKB", 7, 1, 1, 99), frame),
          "WELCOME may not name an oversized room");
    check(!decodes(welcomeFrame("not-a-room-code"), frame),
          "WELCOME may not name an unusable room code");
    check(!decodes(welcomeFrame("IIII-IIII-IIII"), frame),
          "WELCOME room codes exclude the ambiguous letters");

    std::vector<std::uint8_t> padded = welcomeFrame();
    padded.push_back(0);
    check(!decodes(padded, frame), "trailing bytes after WELCOME are refused");

    const std::vector<std::uint8_t> good = welcomeFrame();
    bool everyTruncationRefused = true;
    for(std::size_t cut = 1; cut < good.size(); cut++) {
        const std::vector<std::uint8_t> truncated(good.begin(),
                                                  good.begin() + static_cast<long>(cut));
        if(decodes(truncated, frame)) {
            everyTruncationRefused = false;
            break;
        }
    }
    check(everyTruncationRefused, "every truncation of WELCOME is refused");
}

void testPeerMembership() {
    RoomRelay::ServerFrame frame;

    const std::vector<std::uint8_t> joined = FrameBuilder(RoomRelay::ServerMessage::PeerJoined)
        .u32(11).u8(2).shortString("guest").shortString("browser").bytes;
    check(decodes(joined, frame), "a well-formed PEER_JOINED is accepted");
    check(frame.peerId == 11 && frame.role == RoomRelay::Role::Client,
          "PEER_JOINED carries the peer id and role");
    check(frame.displayName == "guest" && frame.runtime == "browser",
          "PEER_JOINED carries the name and the reported runtime");

    const std::vector<std::uint8_t> badRuntime = FrameBuilder(RoomRelay::ServerMessage::PeerJoined)
        .u32(11).u8(2).shortString("guest").shortString("server").bytes;
    check(!decodes(badRuntime, frame), "PEER_JOINED may not name an unknown runtime");

    std::string controlName = "gu";
    controlName.push_back(static_cast<char>(7));
    controlName += "est";
    const std::vector<std::uint8_t> badName = FrameBuilder(RoomRelay::ServerMessage::PeerJoined)
        .u32(11).u8(2).shortString(controlName).shortString("native").bytes;
    check(!decodes(badName, frame), "PEER_JOINED may not carry a control character in a name");

    const std::vector<std::uint8_t> zeroPeer = FrameBuilder(RoomRelay::ServerMessage::PeerJoined)
        .u32(0).u8(2).shortString("guest").shortString("native").bytes;
    check(!decodes(zeroPeer, frame), "PEER_JOINED may not name the broadcast id");

    const std::vector<std::uint8_t> left = FrameBuilder(RoomRelay::ServerMessage::PeerLeft)
        .u32(11).u8(1).bytes;
    check(decodes(left, frame) && frame.peerId == 11, "PEER_LEFT is accepted");

    const std::vector<std::uint8_t> zeroLeft = FrameBuilder(RoomRelay::ServerMessage::PeerLeft)
        .u32(0).u8(1).bytes;
    check(!decodes(zeroLeft, frame), "PEER_LEFT may not name the broadcast id");
}

void testRelayPayload() {
    RoomRelay::ServerFrame frame;

    const std::vector<std::uint8_t> payload = gamePayload(NETWORKPACKET_COMMANDLIST, 12);
    check(decodes(relayFrame(3, NETWORKPACKET_COMMANDLIST, payload), frame),
          "a well-formed RELAY is accepted");
    check(frame.payload == payload, "the game payload survives unchanged");
    check(frame.senderPeerId == 3, "the sender comes from the relay, not from the payload");

    check(!decodes(relayFrame(3, NETWORKPACKET_STARTGAME, payload), frame),
          "a declared type that disagrees with the payload is refused");
    check(!decodes(relayFrame(0, NETWORKPACKET_COMMANDLIST, payload), frame),
          "a RELAY without a sender is refused");
    check(!decodes(relayFrame(3, NETWORKPACKET_COMMANDLIST, payload, 9), frame),
          "a RELAY on a channel that does not exist is refused");

    const std::vector<std::uint8_t> tooShort(3, 0);
    check(!decodes(relayFrame(3, NETWORKPACKET_COMMANDLIST, tooShort, 0, 3), frame),
          "a payload shorter than its own header is refused");

    // On wasm32 this length wraps an additive bounds check and would otherwise be believed.
    check(!decodes(relayFrame(3, NETWORKPACKET_COMMANDLIST, payload, 0, 0xFFFFFFFFu), frame),
          "a payload length of 0xffffffff is refused without allocating");
    check(!decodes(relayFrame(3, NETWORKPACKET_COMMANDLIST, payload, 0, 0x80000000u), frame),
          "a payload length above the limit is refused without allocating");

    std::vector<std::uint8_t> padded = relayFrame(3, NETWORKPACKET_COMMANDLIST, payload);
    padded.push_back(0);
    check(!decodes(padded, frame), "trailing bytes after RELAY are refused");

    std::vector<std::uint8_t> oversized(RoomRelay::Limits::kMaxFrameBytes + 1, 0);
    oversized[0] = static_cast<std::uint8_t>(RoomRelay::ServerMessage::Relay);
    check(!decodes(oversized, frame), "a frame above the transport limit is refused");
}

void testDiagnosticAndStatus() {
    RoomRelay::ServerFrame frame;

    const std::vector<std::uint8_t> digestBytes(GameStateDigest::kEncodedSize, 0x11);
    const std::vector<std::uint8_t> diagnostic = FrameBuilder(RoomRelay::ServerMessage::Diagnostic)
        .u32(4)
        .u8(static_cast<std::uint8_t>(RoomRelay::DiagnosticKind::StateDigest))
        .u32(static_cast<std::uint32_t>(digestBytes.size()))
        .raw(digestBytes).bytes;
    check(decodes(diagnostic, frame), "a diagnostic is accepted");
    check(frame.payload.size() == GameStateDigest::kEncodedSize, "the diagnostic body survives");

    const std::vector<std::uint8_t> hugeDiagnostic = FrameBuilder(RoomRelay::ServerMessage::Diagnostic)
        .u32(4).u8(1).u32(0xFFFFFFFFu).raw(digestBytes).bytes;
    check(!decodes(hugeDiagnostic, frame), "an oversized diagnostic length is refused");

    std::string message = "refused";
    message.push_back(static_cast<char>(27));
    message += "[31m";
    std::vector<std::uint8_t> error;
    error.push_back(static_cast<std::uint8_t>(RoomRelay::ServerMessage::Error));
    error.push_back(0x11);
    error.push_back(0x3B);     // 4411 is not a defined code, but the frame is still well formed
    error.push_back(static_cast<std::uint8_t>((message.size() >> 8) & 0xFF));
    error.push_back(static_cast<std::uint8_t>(message.size() & 0xFF));
    error.insert(error.end(), message.begin(), message.end());
    check(decodes(error, frame), "a status message is accepted");
    bool printable = true;
    for(const char c : frame.message) {
        const unsigned char value = static_cast<unsigned char>(c);
        if(value < 32 || value > 126) {
            printable = false;
        }
    }
    check(printable, "a status message is reduced to printable characters");
}

void testUnknownMessages() {
    RoomRelay::ServerFrame frame;
    std::string error;

    const std::uint8_t nothing[1] = {0};
    check(!RoomRelay::decodeServerFrame(nothing, 0, frame, error), "an empty frame is refused");
    const std::uint8_t clientMessage[] = {0x01, 0x00, 0x00};
    check(!RoomRelay::decodeServerFrame(clientMessage, sizeof(clientMessage), frame, error),
          "a client message id arriving from the relay is refused");

    const std::uint8_t unknown[] = {0xFE, 0x00};
    check(!RoomRelay::decodeServerFrame(unknown, sizeof(unknown), frame, error),
          "an unknown message id is refused");
}

// --- outgoing encoding ------------------------------------------------------------------

void testEncoders() {
    RoomRelay::HelloFields fields;
    fields.gameProtocolVersion = NETWORK_PROTOCOL_VERSION;
    fields.grant       = std::string(64, 'a');
    fields.runtime     = "native";
    fields.appVersion  = "1.0.655";
    fields.contentHash = std::string(32, 'b');
    fields.displayName = "stefan";

    std::vector<std::uint8_t> hello;
    check(RoomRelay::encodeHello(fields, hello), "a valid handshake is produced");
    check(!hello.empty() && hello[0] == 0x01, "the handshake has the right message id");

    RoomRelay::HelloFields bad = fields;
    bad.grant = "not hex";
    check(!RoomRelay::encodeHello(bad, hello), "a non-hex grant is not sent");
    bad = fields;
    bad.runtime = "server";
    check(!RoomRelay::encodeHello(bad, hello), "an unknown runtime is not sent");
    bad = fields;
    bad.displayName = std::string(200, 'x');
    check(!RoomRelay::encodeHello(bad, hello), "an oversized name is not sent");
    bad = fields;
    bad.grant = std::string(200, 'a');
    check(!RoomRelay::encodeHello(bad, hello), "an oversized grant is not sent");

    const std::vector<std::uint8_t> payload = gamePayload(NETWORKPACKET_CHATMESSAGE, 8);
    std::vector<std::uint8_t> out;
    check(RoomRelay::encodeRelay(0, 0, true, payload.data(), payload.size(), out),
          "a valid game payload is wrapped");
    check(!RoomRelay::encodeRelay(0, 2, true, payload.data(), payload.size(), out),
          "a channel that does not exist is not sent");
    check(!RoomRelay::encodeRelay(0, 0, true, payload.data(), 3, out),
          "a payload shorter than its own header is not sent");
    check(!RoomRelay::encodeRelay(0, 0, true, payload.data(),
                                  RoomRelay::Limits::kMaxGamePayloadBytes + 1, out),
          "a payload above the limit is not sent");

    std::vector<std::uint8_t> diagnostic;
    check(RoomRelay::encodeDiagnostic(RoomRelay::DiagnosticKind::StateDigest, payload.data(),
                                      payload.size(), diagnostic),
          "a diagnostic is wrapped");
    check(!RoomRelay::encodeDiagnostic(RoomRelay::DiagnosticKind::StateDigest, payload.data(),
                                       RoomRelay::Limits::kMaxDiagnosticBytes + 1, diagnostic),
          "an oversized diagnostic is not sent");
}

// --- authorisation matrix ----------------------------------------------------------------

void testAuthorisationMatrix() {
    using RoomRelay::Phase;
    using RoomRelay::isRelayableGameMessage;

    // Address-bearing and content-transfer packets have no relay path at all.
    const std::uint16_t never[] = {
        NETWORKPACKET_CONNECT, NETWORKPACKET_DISCONNECT, NETWORKPACKET_PEER_CONNECTED,
        NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_REQUEST, NETWORKPACKET_MOD_CHUNK,
        NETWORKPACKET_MOD_COMPLETE, NETWORKPACKET_MOD_ACK
    };
    bool allRefused = true;
    for(const std::uint16_t type : never) {
        for(const bool isHost : {false, true}) {
            for(const Phase phase : {Phase::Lobby, Phase::Match}) {
                if(isRelayableGameMessage(type, isHost, phase)) {
                    allRefused = false;
                }
            }
        }
    }
    check(allRefused, "address-bearing and content packets are never relayable");

    check(isRelayableGameMessage(NETWORKPACKET_STARTGAME, true, Phase::Lobby),
          "the host may start the game");
    check(!isRelayableGameMessage(NETWORKPACKET_STARTGAME, false, Phase::Lobby),
          "a client may not start the game");
    check(!isRelayableGameMessage(NETWORKPACKET_STARTGAME, true, Phase::Match),
          "the game cannot be started twice");

    check(isRelayableGameMessage(NETWORKPACKET_SETPATHBUDGET, true, Phase::Match),
          "the host may set the path budget");
    check(!isRelayableGameMessage(NETWORKPACKET_SETPATHBUDGET, false, Phase::Match),
          "a client may not set the path budget");

    check(isRelayableGameMessage(NETWORKPACKET_CLIENTSTATS, false, Phase::Match),
          "a client may report its own stats");
    check(!isRelayableGameMessage(NETWORKPACKET_CLIENTSTATS, true, Phase::Match),
          "the host does not report client stats");

    check(isRelayableGameMessage(NETWORKPACKET_COMMANDLIST, false, Phase::Match),
          "commands flow during a match");
    check(!isRelayableGameMessage(NETWORKPACKET_COMMANDLIST, false, Phase::Lobby),
          "commands do not flow in the lobby");

    check(isRelayableGameMessage(NETWORKPACKET_SENDNAME, false, Phase::Lobby),
          "names are exchanged in the lobby");
    check(!isRelayableGameMessage(NETWORKPACKET_SENDNAME, false, Phase::Match),
          "identity is frozen once the match runs");

    // Campaign continuation arrives after the previous match, while the session is in-game.
    check(isRelayableGameMessage(NETWORKPACKET_COOP_MISSION, true, Phase::Match),
          "the host may send the next co-op mission during a match");
    check(isRelayableGameMessage(NETWORKPACKET_COOP_MISSION, true, Phase::Lobby),
          "the host may send the next co-op mission in the lobby");
    check(!isRelayableGameMessage(NETWORKPACKET_COOP_MISSION, false, Phase::Match),
          "a client may not choose the next co-op mission");

    check(isRelayableGameMessage(NETWORKPACKET_CHATMESSAGE, false, Phase::Lobby)
          && isRelayableGameMessage(NETWORKPACKET_CHATMESSAGE, false, Phase::Match),
          "chat works in both phases");

    check(!isRelayableGameMessage(4242, true, Phase::Match),
          "an unknown packet id is not relayable");
}

// --- endpoints ----------------------------------------------------------------------------

void testEndpointValidation() {
    std::string error;

    check(isAcceptableRelayUrl("wss://relay.example.net/v1/socket", false, error),
          "a secure endpoint is accepted");
    check(isAcceptableRelayUrl("wss://relay.example.net:8443/v1/socket", false, error),
          "a secure endpoint with a port is accepted");

    check(!isAcceptableRelayUrl("ws://relay.example.net/v1/socket", true, error),
          "a plain endpoint to a remote host is refused even in development");
    check(!isAcceptableRelayUrl("ws://127.0.0.1:8787/v1/socket", false, error),
          "a plain loopback endpoint needs the development opt-in");
    check(isAcceptableRelayUrl("ws://127.0.0.1:8787/v1/socket", true, error),
          "a plain loopback endpoint is accepted with the development opt-in");
    check(isAcceptableRelayUrl("ws://localhost:8787/v1/socket", true, error),
          "localhost counts as loopback");

    check(!isAcceptableRelayUrl("http://relay.example.net/", false, error),
          "a non-WebSocket scheme is refused");
    check(!isAcceptableRelayUrl("file:///etc/passwd", false, error),
          "a file URL is refused");
    check(!isAcceptableRelayUrl("wss://user:password@relay.example.net/", false, error),
          "credentials in the URL are refused");
    check(!isAcceptableRelayUrl("wss://relay.example.net:0/", false, error),
          "port zero is refused");
    check(!isAcceptableRelayUrl("wss://relay.example.net:99999/", false, error),
          "an out-of-range port is refused");
    check(!isAcceptableRelayUrl("wss://relay.example.net:80a/", false, error),
          "a non-numeric port is refused");
    check(!isAcceptableRelayUrl("wss:///v1/socket", false, error),
          "an endpoint without a host is refused");
    check(!isAcceptableRelayUrl("", false, error), "an empty endpoint is refused");
    check(!isAcceptableRelayUrl(std::string("wss://relay.example.net/") + std::string(600, 'a'),
                                false, error),
          "an absurdly long endpoint is refused");

    std::string withControl = "wss://relay.example.net/";
    withControl.push_back(static_cast<char>(10));
    withControl += "Host: evil";
    check(!isAcceptableRelayUrl(withControl, false, error),
          "control characters in an endpoint are refused");

    // The HTTPS polling endpoint goes through the same validator, under the same rules. The one
    // that matters most is the third check here: adding an HTTP transport must not become a way
    // to reach a remote host in the clear.
    check(isAcceptableRelayUrl("https://dunelegacy.com/relay/v1/poll", false, error),
          "a secure poll endpoint is accepted");
    check(isAcceptableRelayUrl("https://dunelegacy.com:8443/relay/v1/poll", false, error),
          "a secure poll endpoint with a port is accepted");
    check(!isAcceptableRelayUrl("http://dunelegacy.com/relay/v1/poll", true, error),
          "a plain poll endpoint to a remote host is refused even in development");
    check(!isAcceptableRelayUrl("http://127.0.0.1:8787/relay/v1/poll", false, error),
          "a plain loopback poll endpoint needs the development opt-in");
    check(isAcceptableRelayUrl("http://127.0.0.1:8787/relay/v1/poll", true, error),
          "a plain loopback poll endpoint is accepted with the development opt-in");
    check(!isAcceptableRelayUrl("https://user:password@dunelegacy.com/relay", false, error),
          "credentials in a poll endpoint are refused");
    check(!isAcceptableRelayUrl("https://dunelegacy.com/relay?x=1", false, error),
          "a query string in a poll endpoint is refused");
    check(!isAcceptableRelayUrl("https://dunelegacy.com/relay#x", false, error),
          "a fragment in a poll endpoint is refused");
    check(!isAcceptableRelayUrl("httpss://dunelegacy.com/relay", false, error),
          "a scheme that merely looks like https is refused");

    check(relayTransportKindForUrl("https://dunelegacy.com/relay/v1/poll")
              == RelayTransportKind::HttpPolling
          && relayTransportKindForUrl("http://127.0.0.1:8787/relay")
              == RelayTransportKind::HttpPolling
          && relayTransportKindForUrl("wss://relay.example.net/v1/socket")
              == RelayTransportKind::WebSocket
          && relayTransportKindForUrl("ws://127.0.0.1:8787/v1/socket")
              == RelayTransportKind::WebSocket,
          "the transport is chosen by scheme");

    check(RoomPoll::pollEndpointUrl("https://dunelegacy.com/relay/v1/poll", "/open")
              == "https://dunelegacy.com/relay/v1/poll/open"
          && RoomPoll::pollEndpointUrl("https://dunelegacy.com/relay/v1/poll/", "/exchange")
              == "https://dunelegacy.com/relay/v1/poll/exchange",
          "poll endpoints are built without doubling a slash");
}

void testRoomCodes() {
    std::string normalized;

    check(RoomRelay::normalizeRoomCode("H4PQ-7T2M-9XKB", normalized)
          && normalized == "H4PQ-7T2M-9XKB", "a canonical room code is kept");
    check(RoomRelay::normalizeRoomCode("h4pq7t2m9xkb", normalized)
          && normalized == "H4PQ-7T2M-9XKB", "a code is accepted without dashes or case");
    check(RoomRelay::normalizeRoomCode(" h4pq 7t2m 9xkb ", normalized)
          && normalized == "H4PQ-7T2M-9XKB", "spaces around a code are ignored");

    check(!RoomRelay::normalizeRoomCode("", normalized), "an empty code is refused");
    check(!RoomRelay::normalizeRoomCode("H4PQ-7T2M-9XK", normalized), "a short code is refused");
    check(!RoomRelay::normalizeRoomCode("H4PQ-7T2M-9XKBB", normalized), "a long code is refused");
    check(!RoomRelay::normalizeRoomCode("IIII-IIII-IIII", normalized),
          "the ambiguous letters are not in the alphabet");
    check(!RoomRelay::normalizeRoomCode("../../etc/passw", normalized),
          "a path is not a room code");
}

void testAdmissionParsing() {
    AdmissionResponse response;
    std::string error;

    const std::string listHeader = "status=ok\nprotocol=1\nnext=0\n";
    const std::string listedGame = "game=H4PQ-7T2M-9XKB|1|4|custom|416c696365\n";
    check(RoomAdmission::parseAdmissionResponse(listHeader, response, error, true)
          && response.games.empty(), "an empty public directory is valid");
    check(RoomAdmission::parseAdmissionResponse(listHeader + listedGame, response, error, true)
          && response.games.size() == 1 && response.games[0].hostName == "Alice"
          && response.games[0].players == 1 && response.games[0].maxPeers == 4,
          "public directory decodes host, room and seat counts");
    check(!RoomAdmission::parseAdmissionResponse(listHeader + listedGame + listedGame,
          response, error, true), "duplicate listed rooms are rejected");
    check(!RoomAdmission::parseAdmissionResponse(listHeader + "next=1\n", response, error, true),
          "duplicate pagination fields are rejected");
    check(!RoomAdmission::parseAdmissionResponse(listHeader + "status=ok\n", response, error, true),
          "duplicate directory status is rejected");
    check(!RoomAdmission::parseAdmissionResponse(listHeader + "protocol=1\n", response, error, true),
          "duplicate directory protocol is rejected");
    check(!RoomAdmission::parseAdmissionResponse("status=ok\nprotocol=1\n", response, error, true),
          "directory requires a pagination boundary");
    for(const std::string& invalid : {
        "game=H4PQ-7T2M-9XKB|1|4|custom|0a\n",
        "game=H4PQ-7T2M-9XKB|1|4|custom|zz\n",
        "game=H4PQ-7T2M-9XKB|4|4|custom|416c696365\n",
        "game=H4PQ-7T2M-9XKB|0|4|custom|416c696365\n",
        "game=H4PQ-7T2M-9XKB|1|99|custom|416c696365\n",
        "game=H4PQ-7T2M-9XKB|1|4|unknown|416c696365\n",
        "game=not-a-room|1|4|custom|416c696365\n"}) {
        check(!RoomAdmission::parseAdmissionResponse(listHeader + invalid, response, error, true),
              "malformed public game is rejected");
    }
    check(!RoomAdmission::parseAdmissionResponse(listHeader + listedGame, response, error),
          "a directory response cannot substitute for an admission grant");

    const std::string ok =
        "status=ok\n"
        "protocol=1\n"
        "room=H4PQ-7T2M-9XKB\n"
        "grant=" + std::string(64, 'a') + "\n"
        "grantExpiresMs=30000\n"
        "maxPeers=2\n"
        "url=wss://relay.example.net/v1/socket\n";
    check(RoomAdmission::parseAdmissionResponse(ok, response, error), "a success response parses");
    check(response.ok && response.roomCode == "H4PQ-7T2M-9XKB" && response.maxPeers == 2,
          "a success response carries the room");

    const std::string failure =
        "status=error\ncode=room_not_found\nmessage=That room code is not open.\n";
    check(RoomAdmission::parseAdmissionResponse(failure, response, error),
          "an error response parses");
    check(!response.ok && response.errorCode == "room_not_found",
          "an error response carries the code");

    check(!RoomAdmission::parseAdmissionResponse("", response, error),
          "an empty response is refused");
    check(!RoomAdmission::parseAdmissionResponse("room=H4PQ-7T2M-9XKB\n", response, error),
          "a response without a status is refused");
    check(!RoomAdmission::parseAdmissionResponse("status=maybe\n", response, error),
          "an unknown status is refused");
    check(!RoomAdmission::parseAdmissionResponse(
              "status=ok\nprotocol=99\nroom=H4PQ-7T2M-9XKB\ngrant=" + std::string(64, 'a')
              + "\nmaxPeers=2\nurl=wss://relay.example.net/\n", response, error),
          "another relay protocol version is refused");
    check(!RoomAdmission::parseAdmissionResponse(
              "status=ok\nprotocol=1\nroom=nope\ngrant=" + std::string(64, 'a')
              + "\nmaxPeers=2\nurl=wss://relay.example.net/\n", response, error),
          "an unusable room code is refused");
    check(!RoomAdmission::parseAdmissionResponse(
              "status=ok\nprotocol=1\nroom=H4PQ-7T2M-9XKB\ngrant=../../etc\n"
              "maxPeers=2\nurl=wss://relay.example.net/\n", response, error),
          "an unusable grant is refused");
    check(!RoomAdmission::parseAdmissionResponse(
              "status=ok\nprotocol=1\nroom=H4PQ-7T2M-9XKB\ngrant=" + std::string(64, 'a')
              + "\nmaxPeers=99\nurl=wss://relay.example.net/\n", response, error),
          "an unusable room size is refused");

    check(!RoomAdmission::parseAdmissionResponse(std::string(RoomAdmission::kMaxResponseBytes + 1,
                                                            'x'), response, error),
          "an oversized response is refused");

    std::string tooManyLines = "status=ok\n";
    for(std::size_t i = 0; i < RoomAdmission::kMaxResponseLines + 4; i++) {
        tooManyLines += "pad=1\n";
    }
    check(!RoomAdmission::parseAdmissionResponse(tooManyLines, response, error),
          "a response with too many lines is refused");

    const std::string longLine = "status=ok\nmessage="
        + std::string(RoomAdmission::kMaxLineBytes + 10, 'x') + "\n";
    check(!RoomAdmission::parseAdmissionResponse(longLine, response, error),
          "an over-long line is refused");

    std::string withControl = "status=error\nmessage=bad";
    withControl.push_back(static_cast<char>(0));
    withControl += "\n";
    check(!RoomAdmission::parseAdmissionResponse(withControl, response, error),
          "a control character in a response is refused");

    check(!RoomAdmission::parseAdmissionResponse("status=ok\n1nvalid=x\n", response, error),
          "a non-alphabetic key is refused");

    // Unknown keys are skipped so the relay can add fields later.
    const std::string withExtra = ok + "futureField=something\n";
    check(RoomAdmission::parseAdmissionResponse(withExtra, response, error) && response.ok,
          "an unknown key does not break a valid response");

    check(RoomAdmission::encodeFormValue("a b&c=d") == "a%20b%26c%3Dd",
          "form values are percent encoded");
    check(RoomAdmission::encodeFormValue("A-Z.a_z~0") == "A-Z.a_z~0",
          "unreserved characters survive form encoding");
}

// --- cross-implementation fixtures ---------------------------------------------------------

/**
    The same hex frames as tools/room-relay/test/interop.test.js.

    Two implementations agreeing with a prose specification is not the same as agreeing with each
    other: a field written in the wrong order, or a length prefix of the wrong width, reads back
    perfectly to whichever side wrote it. These byte sequences are the actual contract. If one
    has to change, docs/room-relay-protocol.md, the Node test and this harness change together,
    and the protocol version changes with them.
*/
std::vector<std::uint8_t> fromHex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    const auto nibble = [](char c) -> int {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for(std::size_t index = 0; index + 1 < hex.size(); index += 2) {
        const int high = nibble(hex[index]);
        const int low = nibble(hex[index + 1]);
        if(high < 0 || low < 0) {
            return std::vector<std::uint8_t>();
        }
        out.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return out;
}

std::string toHex(const std::vector<std::uint8_t>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for(const std::uint8_t value : bytes) {
        out.push_back(digits[(value >> 4) & 0x0F]);
        out.push_back(digits[value & 0x0F]);
    }
    return out;
}

const char* const kHelloFixture =
    "01"
    "0001"
    "0005"
    "10" "30313233343536373839616263646566"
    "06" "6e6174697665"
    "07" "312e302e363535"
    "08" "6465616462656566"
    "06" "73746566616e";

const char* const kWelcomeFixture =
    "81"
    "0001"
    "0005"
    "00000007"
    "01"
    "0e" "483450512d3754324d2d39584b42"
    "02"
    "01"
    "0003fff0"
    "1388"
    "4e20";

const char* const kPeerJoinedFixture =
    "82"
    "0000000b"
    "02"
    "05" "6775657374"
    "07" "62726f77736572";

const char* const kServerRelayFixture =
    "85"
    "00000003"
    "01"
    "01"
    "0009"
    "00000006"
    "09000000aabb";

const char* const kClientRelayFixture =
    "02"
    "00000000"
    "01"
    "01"
    "0009"
    "00000006"
    "09000000aabb";

const char* const kClientDiagnosticFixture =
    "06"
    "01"
    "00000004"
    "01020304";

void testWireFixtures() {
    // What this client sends has to be byte-identical to what the relay's own tests accept.
    RoomRelay::HelloFields fields;
    fields.gameProtocolVersion = 5;
    fields.grant       = "0123456789abcdef";
    fields.runtime     = "native";
    fields.appVersion  = "1.0.655";
    fields.contentHash = "deadbeef";
    fields.displayName = "stefan";

    std::vector<std::uint8_t> hello;
    check(RoomRelay::encodeHello(fields, hello), "the fixture handshake is produced");
    check(toHex(hello) == kHelloFixture, "the handshake matches the shared byte fixture");

    const std::vector<std::uint8_t> payload = {0x09, 0x00, 0x00, 0x00, 0xAA, 0xBB};
    std::vector<std::uint8_t> clientRelay;
    check(RoomRelay::encodeRelay(0, 1, true, payload.data(), payload.size(), clientRelay),
          "the fixture game payload is wrapped");
    check(toHex(clientRelay) == kClientRelayFixture,
          "a routed game payload matches the shared byte fixture");

    const std::vector<std::uint8_t> diagnosticBody = {0x01, 0x02, 0x03, 0x04};
    std::vector<std::uint8_t> diagnostic;
    check(RoomRelay::encodeDiagnostic(RoomRelay::DiagnosticKind::StateDigest,
                                      diagnosticBody.data(), diagnosticBody.size(), diagnostic),
          "the fixture diagnostic is wrapped");
    check(toHex(diagnostic) == kClientDiagnosticFixture,
          "a diagnostic matches the shared byte fixture");

    // And what the relay sends has to be understood exactly as the relay meant it.
    RoomRelay::ServerFrame frame;
    check(decodes(fromHex(kWelcomeFixture), frame), "the fixture WELCOME decodes");
    check(frame.peerId == 7 && frame.role == RoomRelay::Role::Host
          && frame.roomCode == "H4PQ-7T2M-9XKB" && frame.maxPeers == 2
          && frame.phase == RoomRelay::Phase::Lobby,
          "the fixture WELCOME is understood field for field");
    check(frame.maxPayloadBytes == 262128 && frame.heartbeatIntervalMs == 5000
          && frame.livenessTimeoutMs == 20000,
          "the fixture WELCOME carries the relay's own limits");

    check(decodes(fromHex(kPeerJoinedFixture), frame), "the fixture PEER_JOINED decodes");
    check(frame.peerId == 11 && frame.role == RoomRelay::Role::Client
          && frame.displayName == "guest" && frame.runtime == "browser",
          "the fixture PEER_JOINED is understood field for field");

    check(decodes(fromHex(kServerRelayFixture), frame), "the fixture RELAY decodes");
    check(frame.senderPeerId == 3 && frame.channel == 1
          && frame.gameMessageType == NETWORKPACKET_COMMANDLIST,
          "the fixture RELAY is understood field for field");
    check(frame.payload == payload, "the game payload survives the relay byte for byte");
}

// --- deterministic state digest ------------------------------------------------------------

void testStateDigest() {
    GameStateDigest::Digest digest;
    digest.gameCycle   = 400;
    digest.randomSeed  = 0xDEADBEEF;
    digest.objectCount = 37;
    digest.objectHash  = 0x0123456789ABCDEFULL;
    digest.houseHash   = 0xFEDCBA9876543210ULL;

    std::uint8_t encoded[GameStateDigest::kEncodedSize];
    GameStateDigest::encode(digest, encoded);

    GameStateDigest::Digest decoded;
    check(GameStateDigest::decode(encoded, sizeof(encoded), decoded), "a digest round trips");
    check(decoded == digest, "a digest survives the round trip unchanged");
    check(!GameStateDigest::decode(encoded, sizeof(encoded) - 1, decoded),
          "a short digest is refused");
    check(!GameStateDigest::decode(encoded, sizeof(encoded) + 1, decoded),
          "a long digest is refused");
    check(!GameStateDigest::decode(nullptr, sizeof(encoded), decoded),
          "a missing digest is refused");

    GameStateDigest::Digest other = digest;
    check(!digest.divergesFrom(other), "identical digests do not diverge");
    other.objectHash ^= 1;
    check(digest.divergesFrom(other), "a different object hash is a divergence");
    other = digest;
    other.gameCycle = 600;
    check(!digest.divergesFrom(other),
          "digests for different cycles are not compared against each other");

    // The hash has to depend on order and on width, or it would miss real divergences.
    GameStateDigest::Hasher a;
    a.mixUint32(1);
    a.mixUint32(2);
    GameStateDigest::Hasher b;
    b.mixUint32(2);
    b.mixUint32(1);
    check(a.value() != b.value(), "the digest hash depends on field order");

    GameStateDigest::Hasher c;
    c.mixUint32(1);
    GameStateDigest::Hasher d;
    d.mixUint64(1);
    check(c.value() != d.value(), "the digest hash depends on field width");

    GameStateDigest::Hasher negative;
    negative.mixInt32(-1);
    GameStateDigest::Hasher positive;
    positive.mixUint32(0xFFFFFFFFu);
    check(negative.value() == positive.value(),
          "a negative value is mixed through its two's complement pattern");

    check(GameStateDigest::describe(digest).find("cycle 400") != std::string::npos,
          "a digest describes itself for the log");
}

void testLobbyChatParsing() {
    AdmissionResponse response;
    std::string error;
    const std::string header = "status=ok\nprotocol=1\n";
    auto parse = [&](const std::string& body, AdmissionOperation operation) {
        return RoomAdmission::parseAdmissionResponse(body, response, error, false, operation);
    };
    const std::string token(64, 'a');
    check(parse(header + "session=" + token + "\ncursor=0\n", AdmissionOperation::ChatEnter), "chat confirmation accepts bounded token");
    check(!parse(header + "cursor=0\n", AdmissionOperation::ChatEnter), "chat confirmation requires token");
    check(parse(header + "cursor=1\nchat=1|c3816c696365|68656c6c6f\n", AdmissionOperation::ChatPoll)
          && response.messages[0].name == "\xc3\x81lice", "chat preserves UTF-8 bytes");
    check(!parse(header + "cursor=1\nchat=2|41|42\n", AdmissionOperation::ChatPoll), "chat cursor cannot precede messages");
    check(!parse(header + "cursor=1\nchat=1|41|420a\n", AdmissionOperation::ChatPoll), "chat refuses control injection");
    check(!parse(header + "cursor=1\nchat=1|41|42\nchat=1|41|42\n", AdmissionOperation::ChatPoll), "chat refuses repeated ids");
    check(!parse(header + "cursor=1\ncursor=2\n", AdmissionOperation::ChatPoll), "chat refuses duplicate cursor");
    check(!parse(header + "cursor=1000000000000000\n", AdmissionOperation::ChatPoll), "chat cursor is bounded on wasm32");
    check(parse(header + "cursor=12\ngap=1\n", AdmissionOperation::ChatPoll) && response.chatGap,
          "chat reports expired history");
    check(!parse(header + "cursor=12\ngap=2\n", AdmissionOperation::ChatPoll), "chat rejects invalid gap marker");
    check(!parse(header + "cursor=1\nchat=1|41|42\n", AdmissionOperation::ChatSay), "send cannot masquerade as poll");
    check(parse(header + "visibility=private\n", AdmissionOperation::Visibility) && response.visibility == "private", "host control acknowledges private visibility");
    check(parse(header + "visibility=private\nroom=ABCD-EFGH-JKMN\n", AdmissionOperation::Visibility)
          && response.roomCode == "ABCD-EFGH-JKMN", "host control returns rotated invitation code");
    check(!parse(header + "visibility=private\nroom=ABCD-EFGH-JKMN\nroom=PQRS-TVWX-YZ01\n", AdmissionOperation::Visibility),
          "host control refuses duplicate room codes");
    check(!parse(header + "visibility=private\nroom=invalid\n", AdmissionOperation::Visibility),
          "host control refuses malformed rotated invitation code");
    check(!parse(header, AdmissionOperation::Visibility), "host control requires visibility acknowledgement");
    check(!parse(header + "visibility=public\nvisibility=private\n", AdmissionOperation::Visibility), "host control refuses ambiguous acknowledgement");
    check(!parse(header + "visibility=private\n", AdmissionOperation::Room), "control response cannot replace admission grant");
}

// --- HTTPS polling ---------------------------------------------------------------------------

/// Builds a gateway answer, with room to declare a shape the bytes do not support.
std::vector<std::uint8_t> pollResponse(std::uint32_t sequence, std::uint16_t closeCode,
                                       const std::vector<std::vector<std::uint8_t>>& frames,
                                       int declaredCount = -1,
                                       const std::vector<std::uint32_t>& declaredLengths = {}) {
    std::vector<std::uint8_t> body;
    body.insert(body.end(), RoomPoll::kResponseMagic, RoomPoll::kResponseMagic + 4);
    RoomPoll::appendUint32LE(body, sequence);
    RoomPoll::appendUint16LE(body, closeCode);
    RoomPoll::appendUint16LE(body, static_cast<std::uint16_t>(
        declaredCount >= 0 ? declaredCount : static_cast<int>(frames.size())));
    for(std::size_t index = 0; index < frames.size(); ++index) {
        RoomPoll::appendUint32LE(body, index < declaredLengths.size()
            ? declaredLengths[index] : static_cast<std::uint32_t>(frames[index].size()));
        body.insert(body.end(), frames[index].begin(), frames[index].end());
    }
    return body;
}

bool pollParses(const std::vector<std::uint8_t>& body, std::uint32_t expectedSequence,
                RoomPoll::ExchangeResponse& out) {
    RoomPoll::BatchError error = RoomPoll::BatchError::None;
    return RoomPoll::parseExchangeResponse(body.data(), body.size(), expectedSequence, out,
                                           error);
}

void testPollBatches() {
    // --- fixtures. These byte strings are the contract with the Node/PHP side; if one of them
    // has to change, the two implementations have stopped agreeing.
    std::vector<std::uint8_t> encoded;
    check(RoomPoll::encodeExchangeRequest(1, {}, encoded), "an empty batch encodes");
    const std::uint8_t emptyRequest[] = {
        'D', 'H', 'P', '1', 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    check(encoded.size() == sizeof(emptyRequest)
          && std::memcmp(encoded.data(), emptyRequest, sizeof(emptyRequest)) == 0,
          "an empty batch is twelve bytes of little-endian header");

    check(RoomPoll::encodeExchangeRequest(0x01020304u, {{0xAA, 0xBB}}, encoded),
          "a one-frame batch encodes");
    const std::uint8_t oneFrameRequest[] = {
        'D', 'H', 'P', '1',
        0x04, 0x03, 0x02, 0x01,             // sequence, little endian
        0x01, 0x00, 0x00, 0x00,             // frame count
        0x02, 0x00, 0x00, 0x00,             // frame length
        0xAA, 0xBB
    };
    check(encoded.size() == sizeof(oneFrameRequest)
          && std::memcmp(encoded.data(), oneFrameRequest, sizeof(oneFrameRequest)) == 0,
          "a frame is length-prefixed little endian and copied verbatim");

    // --- round trip
    RoomPoll::ExchangeResponse response;
    check(pollParses(pollResponse(7, 0, {{1, 2, 3}, {4}}), 7, response)
          && response.sequence == 7 && response.closeCode == 0 && response.frames.size() == 2
          && response.frames[0].size() == 3 && response.frames[1].size() == 1,
          "a well-formed batch round trips");
    check(pollParses(pollResponse(7, 4440, {{1}}), 7, response) && response.closeCode == 4440
          && response.frames.size() == 1,
          "a closing batch still carries its final frames");

    // --- refusals
    const auto valid = pollResponse(1, 0, {{0xAA}});
    auto badMagic = valid;
    badMagic[3] = '2';
    check(!pollParses(badMagic, 1, response), "the response magic is checked");
    check(!pollParses(std::vector<std::uint8_t>(valid.begin(), valid.begin() + 11), 1, response),
          "a header shorter than twelve bytes is refused");
    check(!pollParses(valid, 2, response), "a batch answering another request is refused");
    check(!pollParses(pollResponse(1, 42, {{0xAA}}), 1, response),
          "an invalid close code is refused");
    check(!pollParses(pollResponse(1, 1006, {{0xAA}}), 1, response),
          "a WebSocket-only close code is refused");
    check(pollParses(pollResponse(1, 1000, {}), 1, response),
          "a normal close code is accepted");
    check(pollParses(pollResponse(1, 3999, {}), 1, response),
          "the bottom of the private close range is accepted");
    check(!pollParses(pollResponse(1, 2999, {}), 1, response),
          "just below the private close range is refused");
    check(!pollParses(pollResponse(1, 0, {{0xAA}}, 65), 1, response),
          "more than sixty-four frames is refused");
    check(!pollParses(pollResponse(1, 0, {{0xAA}}, 2), 1, response),
          "a frame count the bytes do not support is refused");
    check(!pollParses(pollResponse(1, 0, {{0xAA}}, 1, {0}), 1, response),
          "a zero-length frame is refused");
    check(!pollParses(pollResponse(1, 0, {{0xAA}}, 1,
                                   {static_cast<std::uint32_t>(
                                        RoomPoll::Limits::kMaxFrameBytes + 1)}), 1, response),
          "a frame above the ceiling is refused");
    check(!pollParses(pollResponse(1, 0, {{0xAA}}, 1, {0xFFFFFFFFu}), 1, response),
          "a frame length of four gigabytes cannot wrap the cursor");
    auto trailing = valid;
    trailing.push_back(0);
    check(!pollParses(trailing, 1, response), "trailing bytes are refused");
    check(!pollParses(std::vector<std::uint8_t>(RoomPoll::Limits::kMaxResponseBytes + 1, 0), 1,
                      response),
          "a batch above the response ceiling is refused");

    // Nothing is handed back from a refused batch, not even a prefix of it.
    check(response.frames.empty(), "a refused batch delivers no frames");

    // --- session tokens
    std::string token;
    const std::string good(64, 'a');
    const std::string body = good + "\n";
    check(RoomPoll::parseOpenResponse(reinterpret_cast<const std::uint8_t*>(body.data()),
                                      body.size(), token) && token == good,
          "a session token is sixty-four hex characters and one newline");
    const std::string noNewline = good;
    check(!RoomPoll::parseOpenResponse(reinterpret_cast<const std::uint8_t*>(noNewline.data()),
                                       noNewline.size(), token),
          "a session token without its newline is refused");
    const std::string upper = std::string(64, 'A') + "\n";
    check(!RoomPoll::parseOpenResponse(reinterpret_cast<const std::uint8_t*>(upper.data()),
                                       upper.size(), token),
          "an uppercase session token is refused");
    const std::string crlf = good + "\r\n";
    check(!RoomPoll::parseOpenResponse(reinterpret_cast<const std::uint8_t*>(crlf.data()),
                                       crlf.size(), token),
          "a CRLF session token is refused");
}

/**
    The smallest backend that can drive the transport: one scripted answer at a time and a clock
    the harness moves by hand. This exists so the state machine itself is exercised where size_t
    is 32 bits, not just the codec it calls.
*/
class HarnessHttpBackend final : public RelayHttpBackend {
public:
    std::uint32_t nowMs() const override { return now; }

    bool start(const RelayHttpRequest& request) override {
        if(inFlight) { overlapping++; }
        lastRequest = request;
        started++;
        inFlight = true;
        hasAnswer = false;
        return true;
    }

    bool poll(RelayHttpOutcome& outcome) override {
        if(!inFlight || !hasAnswer) { return false; }
        outcome = answer;
        hasAnswer = false;
        inFlight = false;
        return true;
    }

    void cancel() override {
        if(inFlight) { cancelled++; }
        inFlight = false;
        hasAnswer = false;
    }

    void answerWith(long status, const std::vector<std::uint8_t>& bytes) {
        answer = RelayHttpOutcome();
        answer.kind = RelayHttpOutcome::Kind::Completed;
        answer.status = status;
        answer.body = bytes;
        hasAnswer = true;
    }

    std::uint32_t now = 1000;
    bool inFlight = false;
    bool hasAnswer = false;
    int started = 0;
    int cancelled = 0;
    int overlapping = 0;
    RelayHttpRequest lastRequest;
    RelayHttpOutcome answer;
};

void testPollTransport() {
    auto owned = std::unique_ptr<HarnessHttpBackend>(new HarnessHttpBackend());
    HarnessHttpBackend* backend = owned.get();
    RelayHttpTransport transport("https://dunelegacy.com/relay/v1/poll", std::move(owned));

    check(backend->started == 1
          && backend->lastRequest.url == "https://dunelegacy.com/relay/v1/poll/open"
          && backend->lastRequest.body.empty()
          && backend->lastRequest.sessionToken.empty(),
          "the transport opens with an empty POST and no credential");
    check(transport.state() == RelayWebSocket::State::Connecting,
          "nothing may be sent before the session is open");

    const std::string token(64, 'c');
    const std::string openBody = token + "\n";
    backend->answerWith(200, std::vector<std::uint8_t>(openBody.begin(), openBody.end()));
    transport.pump();
    check(transport.state() == RelayWebSocket::State::Open, "a valid token opens the session");

    check(transport.send(std::vector<std::uint8_t>(4, 0x11)), "a frame is accepted");
    check(backend->started == 2
          && backend->lastRequest.url == "https://dunelegacy.com/relay/v1/poll/exchange"
          && backend->lastRequest.sessionToken == token
          && backend->lastRequest.url.find(token) == std::string::npos,
          "the session token is a header, never part of a URL");

    backend->answerWith(200, pollResponse(1, 0, {{0x81, 0x01}}));
    transport.pump();

    std::vector<std::uint8_t> received;
    check(transport.receive(received) && received.size() == 2,
          "a frame from the batch is delivered");
    check(!transport.receive(received), "and only once");
    check(transport.sequence() == 2, "the sequence advances after an accepted answer");

    // A refused batch ends the session and hands nothing over.
    backend->now += RoomPoll::Timing::kMinExchangeIntervalMs;
    transport.pump();
    check(backend->started == 3, "the next exchange starts once the interval has passed");
    auto corrupt = pollResponse(2, 0, {{0x81, 0x01}});
    corrupt.push_back(0);
    backend->answerWith(200, corrupt);
    transport.pump();
    check(transport.state() == RelayWebSocket::State::Closed
          && transport.closeCode() == RoomRelay::Close::ProtocolError
          && !transport.receive(received),
          "a malformed batch ends the session and delivers nothing");
    check(backend->overlapping == 0, "only one request is ever in flight");
}

} // namespace

int main() {
    testWelcome();
    testPeerMembership();
    testRelayPayload();
    testDiagnosticAndStatus();
    testUnknownMessages();
    testEncoders();
    testAuthorisationMatrix();
    testEndpointValidation();
    testRoomCodes();
    testAdmissionParsing();
    testLobbyChatParsing();
    testPollBatches();
    testPollTransport();
    testWireFixtures();
    testStateDigest();

    std::printf("relay wire harness: %d checks, %d failures (size_t is %zu bytes)\n",
                checks, failures, sizeof(std::size_t));
    return failures == 0 ? 0 : 1;
}
