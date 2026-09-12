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

#ifndef ROOMRELAYPROTOCOL_H
#define ROOMRELAYPROTOCOL_H

/**
    Client side of the room relay wire contract in docs/room-relay-protocol.md.

    This header is deliberately free of SDL, ENet and game headers: the same code has to compile
    into the browser build, into the native build and into the standalone wire harness that runs
    where size_t is 32 bits.

    Two rules hold everywhere in this file:

      - every length is checked against the bytes that are actually present, by subtraction;
        `position + length` wraps on wasm32 and must never appear,
      - nothing is allocated or copied before its length has been checked.
*/

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace RoomRelay {

constexpr std::uint16_t kProtocolVersion = 1;

/// Client -> relay message ids.
enum class ClientMessage : std::uint8_t {
    Hello      = 0x01,
    Relay      = 0x02,
    Heartbeat  = 0x03,
    Leave      = 0x04,
    RoomPhase  = 0x05,
    Diagnostic = 0x06
};

/// Relay -> client message ids.
enum class ServerMessage : std::uint8_t {
    None             = 0x00,
    Welcome          = 0x81,
    PeerJoined       = 0x82,
    PeerLeft         = 0x83,
    RoomPhaseChanged = 0x84,
    Relay            = 0x85,
    HeartbeatAck     = 0x86,
    Error            = 0x87,
    RoomClosed       = 0x88,
    Diagnostic       = 0x89
};

enum class Role : std::uint8_t { Unknown = 0, Host = 1, Client = 2 };
enum class Phase : std::uint8_t { Lobby = 1, Match = 2 };

/// Diagnostic payload kinds carried in the relay envelope (never in the ENet game protocol).
enum class DiagnosticKind : std::uint8_t {
    StateDigest   = 1,
    LockstepStall = 2
};

/// Private-range WebSocket close codes; also used as the numeric space for ERROR.
namespace Close {
constexpr std::uint16_t Normal          = 1000;
constexpr std::uint16_t ProtocolError   = 4400;
constexpr std::uint16_t Unauthorized    = 4401;
constexpr std::uint16_t Forbidden       = 4403;
constexpr std::uint16_t RoomNotFound    = 4404;
constexpr std::uint16_t Timeout         = 4408;
constexpr std::uint16_t RoomFull        = 4409;
constexpr std::uint16_t TooLarge        = 4413;
constexpr std::uint16_t RateLimited     = 4429;
constexpr std::uint16_t SlowConsumer    = 4431;
constexpr std::uint16_t HostLeft        = 4440;
constexpr std::uint16_t ServerShutdown  = 4441;
constexpr std::uint16_t VersionMismatch = 4450;
} // namespace Close

namespace Limits {
constexpr std::size_t   kMaxFrameBytes        = 262144;
constexpr std::size_t   kMaxGamePayloadBytes  = 262128;
constexpr std::size_t   kMaxDiagnosticBytes   = 4096;
constexpr std::size_t   kMaxGrantChars        = 64;
constexpr std::size_t   kMaxRuntimeChars      = 16;
constexpr std::size_t   kMaxAppVersionChars   = 32;
constexpr std::size_t   kMaxContentHashChars  = 64;
constexpr std::size_t   kMaxNameChars         = 64;
constexpr std::size_t   kMaxMessageChars      = 200;
constexpr std::size_t   kMaxRoomCodeChars     = 16;
constexpr std::size_t   kMaxPeersPerRoom      = 8;

/// How much inbound data the client is willing to hold before it gives up on the session.
constexpr std::size_t   kMaxIncomingQueueBytes = 4 * 1024 * 1024;
/// How much outbound data may sit unsent before the session is considered broken.
constexpr std::size_t   kMaxOutgoingQueueBytes = 4 * 1024 * 1024;
constexpr std::size_t   kMaxQueuedFrames       = 4096;

/**
    How much decoded event data the relay session will hold for the game loop.

    A count alone is not a bound. Four thousand queued events, each carrying a payload of up to
    kMaxGamePayloadBytes, is a gigabyte - so the aggregate size is capped as well, and whichever
    limit is reached first ends the session. The value matches the socket's own inbound bound
    because the event queue is fed from it: the two together are the session's memory ceiling.
*/
constexpr std::size_t   kMaxQueuedEventBytes   = 4 * 1024 * 1024;

/**
    Charged against kMaxQueuedEventBytes for every event regardless of its payload.

    Without it, an unbounded number of empty events would cost nothing by weight and only the
    count would bound them; with it, both limits are meaningful on their own.
*/
constexpr std::size_t   kQueuedEventOverheadBytes = 128;
} // namespace Limits

// ---------------------------------------------------------------------------------------------
// Bounded reading and writing
// ---------------------------------------------------------------------------------------------

/**
    Reads big-endian relay envelope fields out of a buffer it does not own. Every accessor
    fails softly: once `failed()` is set it stays set and further reads return zero, so a caller
    can parse a whole message and check once at the end.
*/
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t length)
     : data_(data), length_(data != nullptr ? length : 0), position_(0), failed_(data == nullptr) {
    }

    bool failed() const { return failed_; }

    std::size_t remaining() const {
        // Subtraction form: position_ is never advanced past length_.
        return (position_ >= length_) ? 0 : (length_ - position_);
    }

    bool need(std::size_t count) {
        if(failed_ || count > remaining()) {
            failed_ = true;
            return false;
        }
        return true;
    }

    std::uint8_t readUint8() {
        if(!need(1)) return 0;
        return data_[position_++];
    }

    std::uint16_t readUint16() {
        if(!need(2)) return 0;
        const std::uint16_t value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data_[position_]) << 8)
            | static_cast<std::uint16_t>(data_[position_ + 1]));
        position_ += 2;
        return value;
    }

    std::uint32_t readUint32() {
        if(!need(4)) return 0;
        const std::uint32_t value =
              (static_cast<std::uint32_t>(data_[position_])     << 24)
            | (static_cast<std::uint32_t>(data_[position_ + 1]) << 16)
            | (static_cast<std::uint32_t>(data_[position_ + 2]) << 8)
            |  static_cast<std::uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return value;
    }

    /// Reads `count` bytes into `out`, replacing its contents. Nothing is resized until the
    /// length has been checked against what is present.
    bool readBytes(std::size_t count, std::vector<std::uint8_t>& out) {
        if(!need(count)) {
            return false;
        }
        out.assign(data_ + position_, data_ + position_ + count);
        position_ += count;
        return true;
    }

    /// Reads a uint8-prefixed string, capped by `maxLength`.
    std::string readShortString(std::size_t maxLength) {
        const std::size_t length = readUint8();
        if(failed_ || length > maxLength || !need(length)) {
            failed_ = true;
            return std::string();
        }
        std::string value(reinterpret_cast<const char*>(data_ + position_), length);
        position_ += length;
        return value;
    }

    /// Reads a uint16-prefixed string, capped by `maxLength`.
    std::string readMediumString(std::size_t maxLength) {
        const std::size_t length = readUint16();
        if(failed_ || length > maxLength || !need(length)) {
            failed_ = true;
            return std::string();
        }
        std::string value(reinterpret_cast<const char*>(data_ + position_), length);
        position_ += length;
        return value;
    }

    /// A frame with bytes left over after its declared fields is malformed, not merely odd.
    bool atEnd() {
        if(remaining() != 0) {
            failed_ = true;
            return false;
        }
        return !failed_;
    }

private:
    const std::uint8_t* data_;
    std::size_t         length_;
    std::size_t         position_;
    bool                failed_;
};

/// Appends big-endian relay envelope fields to a byte vector.
class ByteWriter {
public:
    explicit ByteWriter(std::size_t reserve = 64) { bytes_.reserve(reserve); }

    void writeUint8(std::uint8_t value) { bytes_.push_back(value); }

    void writeUint16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void writeUint32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void writeRaw(const std::uint8_t* data, std::size_t length) {
        if(data != nullptr && length > 0) {
            bytes_.insert(bytes_.end(), data, data + length);
        }
    }

    /// Returns false if the string does not fit the uint8 prefix; the caller must not send.
    bool writeShortString(const std::string& text, std::size_t maxLength) {
        if(text.size() > maxLength || text.size() > 255) {
            return false;
        }
        bytes_.push_back(static_cast<std::uint8_t>(text.size()));
        writeRaw(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
        return true;
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

// ---------------------------------------------------------------------------------------------
// Field validation
// ---------------------------------------------------------------------------------------------

/// Same rule as NetworkPacketPolicy::isAcceptablePlayerName; repeated here so this header stays
/// independent of the game headers. The two must agree.
inline bool isAcceptableDisplayName(const std::string& name) {
    if(name.empty() || name.size() > Limits::kMaxNameChars) {
        return false;
    }
    for(const unsigned char c : name) {
        if(c < 32 || c == 127) {
            return false;
        }
    }
    return true;
}

inline bool isLowercaseHex(const std::string& text) {
    if(text.empty()) {
        return false;
    }
    for(const char c : text) {
        const bool digit = (c >= '0' && c <= '9');
        const bool hex   = (c >= 'a' && c <= 'f');
        if(!digit && !hex) {
            return false;
        }
    }
    return true;
}

inline bool isAcceptableGrant(const std::string& grant) {
    return grant.size() <= Limits::kMaxGrantChars && isLowercaseHex(grant);
}

inline bool isAcceptableRuntime(const std::string& runtime) {
    return runtime == "native" || runtime == "browser";
}

/// Room codes are Crockford base32 without I, L, O and U, in three dashed groups of four.
inline bool isAcceptableRoomCode(const std::string& code) {
    if(code.size() > Limits::kMaxRoomCodeChars) {
        return false;
    }
    std::size_t symbols = 0;
    for(const char c : code) {
        if(c == '-') {
            continue;
        }
        const bool digit = (c >= '0' && c <= '9');
        const bool upper = (c >= 'A' && c <= 'Z');
        if(!digit && !upper) {
            return false;
        }
        if(c == 'I' || c == 'L' || c == 'O' || c == 'U') {
            return false;
        }
        symbols++;
    }
    return symbols == 12;
}

/// Normalises user input ("abcd efgh jkmn", lower case, no dashes) into the canonical form.
/// Returns false if the input cannot be a room code at all.
inline bool normalizeRoomCode(const std::string& input, std::string& normalized) {
    std::string compact;
    compact.reserve(12);
    for(const char c : input) {
        if(c == '-' || c == ' ' || c == '\t') {
            continue;
        }
        char upper = c;
        if(upper >= 'a' && upper <= 'z') {
            upper = static_cast<char>(upper - 'a' + 'A');
        }
        if(compact.size() >= 12) {
            return false;
        }
        compact.push_back(upper);
    }
    if(compact.size() != 12) {
        return false;
    }

    std::string candidate = compact.substr(0, 4) + "-" + compact.substr(4, 4) + "-"
                          + compact.substr(8, 4);
    if(!isAcceptableRoomCode(candidate)) {
        return false;
    }
    normalized = candidate;
    return true;
}

/// Strips anything that is not printable ASCII out of a message the relay sent us, and caps it.
/// A relay message ends up in the player's UI, so it never gets to carry control characters.
inline std::string sanitizeRelayMessage(const std::string& text) {
    std::string out;
    out.reserve(text.size() < Limits::kMaxMessageChars ? text.size() : Limits::kMaxMessageChars);
    for(const char rawChar : text) {
        if(out.size() >= Limits::kMaxMessageChars) {
            break;
        }
        const unsigned char c = static_cast<unsigned char>(rawChar);
        out.push_back((c >= 32 && c <= 126) ? rawChar : ' ');
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Game message authorisation (mirrors the relay's own table)
// ---------------------------------------------------------------------------------------------

/// Who may originate a game message on the relay.
enum class SenderRule : std::uint8_t { Any, HostOnly, ClientOnly };
/// When a game message may be sent.
enum class PhaseRule : std::uint8_t { Any, LobbyOnly, MatchOnly };

struct GameMessageRule {
    bool       carried    = false;
    SenderRule senderRule = SenderRule::Any;
    PhaseRule  phaseRule  = PhaseRule::Any;
};

/**
    The same matrix the relay enforces, so the client refuses to send something the relay would
    refuse to carry and refuses to act on something it should never have received.

    The numeric ids are the values in NetworkPacketTypes.h. They are repeated rather than
    included so this header stays usable from the standalone harness; the test suite asserts
    that the two agree.
*/
inline GameMessageRule gameMessageRule(std::uint16_t gameMessageType) {
    GameMessageRule rule;
    switch(gameMessageType) {
        case 4:  rule = {true, SenderRule::HostOnly,   PhaseRule::LobbyOnly}; break; // SENDGAMEINFO
        case 5:  rule = {true, SenderRule::Any,        PhaseRule::LobbyOnly}; break; // SENDNAME
        case 6:  rule = {true, SenderRule::Any,        PhaseRule::Any};       break; // CHATMESSAGE
        case 7:  rule = {true, SenderRule::Any,        PhaseRule::LobbyOnly}; break; // CHANGEEVENTLIST
        case 8:  rule = {true, SenderRule::HostOnly,   PhaseRule::LobbyOnly}; break; // STARTGAME
        case 9:  rule = {true, SenderRule::Any,        PhaseRule::MatchOnly}; break; // COMMANDLIST
        case 10: rule = {true, SenderRule::Any,        PhaseRule::MatchOnly}; break; // SELECTIONLIST
        case 11: rule = {true, SenderRule::Any,        PhaseRule::LobbyOnly}; break; // CONFIG_HASH
        case 12: rule = {true, SenderRule::HostOnly,   PhaseRule::MatchOnly}; break; // SETPATHBUDGET
        case 13: rule = {true, SenderRule::ClientOnly, PhaseRule::MatchOnly}; break; // CLIENTSTATS
        case 19: rule = {true, SenderRule::Any,        PhaseRule::Any};       break; // KEEPALIVE
        // Campaign continuation arrives after the previous match, while the session is still
        // marked in-game, so it is allowed in both phases.
        case 20: rule = {true, SenderRule::HostOnly,   PhaseRule::Any};       break; // COOP_MISSION
        // 1 CONNECT, 2 DISCONNECT, 3 PEER_CONNECTED carry addresses; 14..18 are mod transfers.
        // Neither is carried by the relay, and neither has a code path in relay mode.
        default: rule.carried = false; break;
    }
    return rule;
}

inline bool isRelayableGameMessage(std::uint16_t gameMessageType, bool senderIsHost, Phase phase) {
    const GameMessageRule rule = gameMessageRule(gameMessageType);
    if(!rule.carried) {
        return false;
    }
    if(rule.senderRule == SenderRule::HostOnly && !senderIsHost) {
        return false;
    }
    if(rule.senderRule == SenderRule::ClientOnly && senderIsHost) {
        return false;
    }
    if(rule.phaseRule == PhaseRule::LobbyOnly && phase != Phase::Lobby) {
        return false;
    }
    if(rule.phaseRule == PhaseRule::MatchOnly && phase != Phase::Match) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Decoding relay -> client frames
// ---------------------------------------------------------------------------------------------

struct ServerFrame {
    ServerMessage type = ServerMessage::None;

    // Welcome
    std::uint16_t relayProtocolVersion = 0;
    std::uint16_t gameProtocolVersion  = 0;
    std::uint32_t peerId               = 0;
    Role          role                 = Role::Unknown;
    std::string   roomCode;
    std::uint8_t  maxPeers             = 0;
    Phase         phase                = Phase::Lobby;
    std::uint32_t maxPayloadBytes      = 0;
    std::uint16_t heartbeatIntervalMs  = 0;
    std::uint16_t livenessTimeoutMs    = 0;

    // PeerJoined / PeerLeft
    std::string   displayName;
    std::string   runtime;
    std::uint8_t  reason               = 0;

    // RoomPhaseChanged
    std::uint32_t byPeerId             = 0;

    // Relay / Diagnostic
    std::uint32_t senderPeerId         = 0;
    std::uint8_t  channel              = 0;
    std::uint8_t  flags                = 0;
    std::uint16_t gameMessageType      = 0;
    std::uint8_t  diagnosticKind       = 0;
    std::vector<std::uint8_t> payload;

    // HeartbeatAck
    std::uint32_t clientEchoMs         = 0;
    std::uint32_t serverTimeMs         = 0;

    // Error / RoomClosed
    std::uint16_t code                 = 0;
    std::string   message;
};

/**
    Decodes one relay frame.
    \param  data    the WebSocket binary payload
    \param  length  its length in bytes
    \param  out     filled in on success
    \param  error   a short description on failure
    \return true if the frame is well formed and may be acted on
*/
inline bool decodeServerFrame(const std::uint8_t* data, std::size_t length, ServerFrame& out,
                              std::string& error) {
    out = ServerFrame();

    if(data == nullptr || length == 0) {
        error = "empty frame";
        return false;
    }
    if(length > Limits::kMaxFrameBytes) {
        error = "frame exceeds the transport limit";
        return false;
    }

    ByteReader reader(data, length);
    const std::uint8_t rawType = reader.readUint8();

    switch(rawType) {
        case static_cast<std::uint8_t>(ServerMessage::Welcome): {
            out.type = ServerMessage::Welcome;
            out.relayProtocolVersion = reader.readUint16();
            out.gameProtocolVersion  = reader.readUint16();
            out.peerId               = reader.readUint32();
            const std::uint8_t role  = reader.readUint8();
            out.roomCode             = reader.readShortString(Limits::kMaxRoomCodeChars);
            out.maxPeers             = reader.readUint8();
            const std::uint8_t phase = reader.readUint8();
            out.maxPayloadBytes      = reader.readUint32();
            out.heartbeatIntervalMs  = reader.readUint16();
            out.livenessTimeoutMs    = reader.readUint16();
            if(!reader.atEnd()) {
                error = "malformed WELCOME";
                return false;
            }
            if(role != 1 && role != 2) {
                error = "WELCOME names an unknown role";
                return false;
            }
            if(phase != 1 && phase != 2) {
                error = "WELCOME names an unknown phase";
                return false;
            }
            if(out.peerId == 0) {
                error = "WELCOME assigned the broadcast id";
                return false;
            }
            if(out.maxPeers == 0 || out.maxPeers > Limits::kMaxPeersPerRoom) {
                error = "WELCOME names an implausible room size";
                return false;
            }
            if(!isAcceptableRoomCode(out.roomCode)) {
                error = "WELCOME names an unusable room code";
                return false;
            }
            out.role  = static_cast<Role>(role);
            out.phase = static_cast<Phase>(phase);
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::PeerJoined): {
            out.type = ServerMessage::PeerJoined;
            out.peerId              = reader.readUint32();
            const std::uint8_t role = reader.readUint8();
            out.displayName         = reader.readShortString(Limits::kMaxNameChars);
            out.runtime             = reader.readShortString(Limits::kMaxRuntimeChars);
            if(!reader.atEnd()) {
                error = "malformed PEER_JOINED";
                return false;
            }
            if(role != 1 && role != 2) {
                error = "PEER_JOINED names an unknown role";
                return false;
            }
            if(out.peerId == 0) {
                error = "PEER_JOINED names the broadcast id";
                return false;
            }
            if(!isAcceptableDisplayName(out.displayName)) {
                error = "PEER_JOINED names an unusable player name";
                return false;
            }
            if(!isAcceptableRuntime(out.runtime)) {
                error = "PEER_JOINED names an unknown runtime";
                return false;
            }
            out.role = static_cast<Role>(role);
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::PeerLeft): {
            out.type   = ServerMessage::PeerLeft;
            out.peerId = reader.readUint32();
            out.reason = reader.readUint8();
            if(!reader.atEnd() || out.peerId == 0) {
                error = "malformed PEER_LEFT";
                return false;
            }
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::RoomPhaseChanged): {
            out.type = ServerMessage::RoomPhaseChanged;
            const std::uint8_t phase = reader.readUint8();
            out.byPeerId = reader.readUint32();
            if(!reader.atEnd() || (phase != 1 && phase != 2)) {
                error = "malformed ROOM_PHASE_CHANGED";
                return false;
            }
            out.phase = static_cast<Phase>(phase);
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::Relay): {
            out.type            = ServerMessage::Relay;
            out.senderPeerId    = reader.readUint32();
            out.channel         = reader.readUint8();
            out.flags           = reader.readUint8();
            out.gameMessageType = reader.readUint16();
            const std::uint32_t payloadLength = reader.readUint32();
            if(reader.failed()) {
                error = "truncated RELAY header";
                return false;
            }
            if(payloadLength < 4
               || static_cast<std::size_t>(payloadLength) > Limits::kMaxGamePayloadBytes) {
                error = "RELAY declares an out-of-range payload length";
                return false;
            }
            if(!reader.readBytes(static_cast<std::size_t>(payloadLength), out.payload)
               || !reader.atEnd()) {
                error = "malformed RELAY";
                return false;
            }
            if(out.senderPeerId == 0) {
                error = "RELAY has no sender";
                return false;
            }
            if(out.channel > 1) {
                error = "RELAY names a channel that does not exist";
                return false;
            }
            // The payload carries its own little-endian packet id. The relay checks this too;
            // checking it again here means the type we authorise against is the type we then act
            // on, whatever the relay did.
            const std::uint32_t declared =
                  static_cast<std::uint32_t>(out.payload[0])
                | (static_cast<std::uint32_t>(out.payload[1]) << 8)
                | (static_cast<std::uint32_t>(out.payload[2]) << 16)
                | (static_cast<std::uint32_t>(out.payload[3]) << 24);
            if(declared != static_cast<std::uint32_t>(out.gameMessageType)) {
                error = "RELAY envelope and payload disagree about the message type";
                return false;
            }
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::Diagnostic): {
            out.type           = ServerMessage::Diagnostic;
            out.senderPeerId   = reader.readUint32();
            out.diagnosticKind = reader.readUint8();
            const std::uint32_t payloadLength = reader.readUint32();
            if(reader.failed()) {
                error = "truncated DIAGNOSTIC header";
                return false;
            }
            if(static_cast<std::size_t>(payloadLength) > Limits::kMaxDiagnosticBytes) {
                error = "DIAGNOSTIC declares an out-of-range payload length";
                return false;
            }
            if(!reader.readBytes(static_cast<std::size_t>(payloadLength), out.payload)
               || !reader.atEnd()) {
                error = "malformed DIAGNOSTIC";
                return false;
            }
            if(out.senderPeerId == 0) {
                error = "DIAGNOSTIC has no sender";
                return false;
            }
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::HeartbeatAck): {
            out.type         = ServerMessage::HeartbeatAck;
            out.clientEchoMs = reader.readUint32();
            out.serverTimeMs = reader.readUint32();
            if(!reader.atEnd()) {
                error = "malformed HEARTBEAT_ACK";
                return false;
            }
            return true;
        }

        case static_cast<std::uint8_t>(ServerMessage::Error):
        case static_cast<std::uint8_t>(ServerMessage::RoomClosed): {
            out.type = (rawType == static_cast<std::uint8_t>(ServerMessage::Error))
                     ? ServerMessage::Error : ServerMessage::RoomClosed;
            out.code = reader.readUint16();
            out.message = sanitizeRelayMessage(reader.readMediumString(Limits::kMaxMessageChars));
            if(!reader.atEnd()) {
                error = "malformed relay status message";
                return false;
            }
            return true;
        }

        default:
            error = "unknown relay message id";
            return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Encoding client -> relay frames
// ---------------------------------------------------------------------------------------------

struct HelloFields {
    std::uint16_t gameProtocolVersion = 0;
    std::string   grant;
    std::string   runtime;
    std::string   appVersion;
    std::string   contentHash;
    std::string   displayName;
};

/// Returns false, and produces nothing, if any field would violate the contract.
inline bool encodeHello(const HelloFields& fields, std::vector<std::uint8_t>& frame) {
    if(!isAcceptableGrant(fields.grant)
       || !isAcceptableRuntime(fields.runtime)
       || fields.appVersion.empty() || fields.appVersion.size() > Limits::kMaxAppVersionChars
       || fields.contentHash.size() > Limits::kMaxContentHashChars
       || !isAcceptableDisplayName(fields.displayName)) {
        return false;
    }

    ByteWriter writer(256);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::Hello));
    writer.writeUint16(kProtocolVersion);
    writer.writeUint16(fields.gameProtocolVersion);
    if(!writer.writeShortString(fields.grant, Limits::kMaxGrantChars)
       || !writer.writeShortString(fields.runtime, Limits::kMaxRuntimeChars)
       || !writer.writeShortString(fields.appVersion, Limits::kMaxAppVersionChars)
       || !writer.writeShortString(fields.contentHash, Limits::kMaxContentHashChars)
       || !writer.writeShortString(fields.displayName, Limits::kMaxNameChars)) {
        return false;
    }
    frame = writer.take();
    return true;
}

/**
    Wraps one serialized game packet.
    \param  recipient   0 to reach every other peer, or a peer id in this room
    \param  channel     0 or 1
    \param  reliable    recorded in the flags; the relay transport is always reliable and ordered
    \param  payload     the packet exactly as ENetPacketOStream produced it
*/
inline bool encodeRelay(std::uint32_t recipient, std::uint8_t channel, bool reliable,
                        const std::uint8_t* payload, std::size_t payloadLength,
                        std::vector<std::uint8_t>& frame) {
    if(payload == nullptr || channel > 1) {
        return false;
    }
    if(payloadLength < 4 || payloadLength > Limits::kMaxGamePayloadBytes) {
        return false;
    }

    const std::uint32_t declared =
          static_cast<std::uint32_t>(payload[0])
        | (static_cast<std::uint32_t>(payload[1]) << 8)
        | (static_cast<std::uint32_t>(payload[2]) << 16)
        | (static_cast<std::uint32_t>(payload[3]) << 24);
    if(declared > 0xFFFFu) {
        return false;   // the envelope carries the type as a uint16
    }

    ByteWriter writer(payloadLength + 16);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::Relay));
    writer.writeUint32(recipient);
    writer.writeUint8(channel);
    writer.writeUint8(reliable ? 1 : 0);
    writer.writeUint16(static_cast<std::uint16_t>(declared));
    writer.writeUint32(static_cast<std::uint32_t>(payloadLength));
    writer.writeRaw(payload, payloadLength);
    frame = writer.take();
    return true;
}

inline std::vector<std::uint8_t> encodeHeartbeat(std::uint32_t clientTimeMs) {
    ByteWriter writer(8);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::Heartbeat));
    writer.writeUint32(clientTimeMs);
    return writer.take();
}

inline std::vector<std::uint8_t> encodeLeave(std::uint8_t reason) {
    ByteWriter writer(4);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::Leave));
    writer.writeUint8(reason);
    return writer.take();
}

inline std::vector<std::uint8_t> encodeRoomPhase(Phase phase) {
    ByteWriter writer(4);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::RoomPhase));
    writer.writeUint8(static_cast<std::uint8_t>(phase));
    return writer.take();
}

inline bool encodeDiagnostic(DiagnosticKind kind, const std::uint8_t* payload,
                             std::size_t payloadLength, std::vector<std::uint8_t>& frame) {
    if(payloadLength > Limits::kMaxDiagnosticBytes || (payload == nullptr && payloadLength > 0)) {
        return false;
    }
    ByteWriter writer(payloadLength + 8);
    writer.writeUint8(static_cast<std::uint8_t>(ClientMessage::Diagnostic));
    writer.writeUint8(static_cast<std::uint8_t>(kind));
    writer.writeUint32(static_cast<std::uint32_t>(payloadLength));
    writer.writeRaw(payload, payloadLength);
    frame = writer.take();
    return true;
}

/// A short, player-facing explanation for a relay close code. Never shows protocol jargon.
inline const char* describeCloseCode(std::uint16_t code) {
    switch(code) {
        case Close::Normal:          return "The game session ended.";
        case Close::ProtocolError:   return "The connection sent something unexpected.";
        case Close::Unauthorized:    return "That invitation is no longer valid.";
        case Close::Forbidden:       return "That action was not allowed in this game.";
        case Close::RoomNotFound:    return "That game is no longer open.";
        case Close::Timeout:         return "The connection stopped responding.";
        case Close::RoomFull:        return "That game is full.";
        case Close::TooLarge:        return "A message was too large to send.";
        case Close::RateLimited:     return "Too many messages were sent too quickly.";
        case Close::SlowConsumer:    return "This computer fell too far behind the game.";
        case Close::HostLeft:        return "The host left the game.";
        case Close::ServerShutdown:  return "The game service is restarting.";
        case Close::VersionMismatch: return "This version cannot join that game.";
        default:                     return "The connection to the game was lost.";
    }
}

} // namespace RoomRelay

#endif // ROOMRELAYPROTOCOL_H
