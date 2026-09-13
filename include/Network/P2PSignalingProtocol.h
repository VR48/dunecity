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

#ifndef P2PSIGNALINGPROTOCOL_H
#define P2PSIGNALINGPROTOCOL_H

/**
    What the signaling service is allowed to say, and how little of it is believed.

    The signaling service introduces players to each other. It hands out room membership, carries
    offers, answers and ICE candidates between two members of one room, tells each member which
    DTLS fingerprint the service admitted for the other, and reports the room phase. It never
    sees a game packet: there is no endpoint that would take one.

    The format is the same bounded key=value text the admission endpoint already uses, for the
    same reason: it can be parsed with a strict, bounded parser instead of adding a JSON parser to
    the game. Every field here is length- and charset-checked before it is believed, and an
    unknown key is skipped so the service can grow a field without a client release.

    Nothing in this header opens a socket, and nothing in it is a player-facing string: the
    transport turns a refusal into a sentence, so that the wire vocabulary never reaches the UI.
*/

#include <Network/RoomRelayProtocol.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace P2PSignal {

namespace Limits {
/**
    Sizes, chosen so that they are algebraically consistent after hex expansion.

    A real Chromium offer is around 2 KiB, but a description with many candidates or a long
    session id is far larger, and the browser transport accepts up to 65536 bytes. Hex doubles
    that, so a `sig=` line has to be able to carry 131072 characters plus its metadata, and the
    response body has to be able to carry such a line. Getting this wrong does not fail safe: it
    fails at the exact moment two real stacks try to talk to each other.
*/
constexpr std::size_t   kMaxSdpBytes        = 65536;
constexpr std::size_t   kMaxCandidateBytes  = 2048;
/// The media id that prefixes a candidate on the wire, plus its delimiter.
constexpr std::size_t   kMaxCandidateMidChars = 64;
/// The largest raw signal payload of any kind: a description is the big one.
constexpr std::size_t   kMaxSignalBytes     = kMaxSdpBytes;
/// The hex form of the largest signal payload.
constexpr std::size_t   kMaxSignalHexChars  = kMaxSignalBytes * 2;
constexpr std::size_t   kMaxResponseBytes   = 524288;
constexpr std::size_t   kMaxResponseLines   = 256;
constexpr std::size_t   kMaxLineBytes       = 131200;
constexpr std::size_t   kMaxValueBytes      = 131168;
constexpr std::size_t   kMaxKeyBytes        = 24;
/// Signal records one poll may deliver.
constexpr std::size_t   kMaxSignalsPerPoll  = 32;
/// The largest signaling POST this client sends: hex payload plus form metadata headroom.
constexpr std::size_t   kMaxSignalRequestBytes = 133120;
constexpr std::size_t   kMaxIceServers      = 4;
constexpr std::size_t   kMaxIceUrlChars     = 128;
constexpr std::size_t   kMaxSessionChars    = 64;
/// "AA:BB:..." for 32 octets is exactly 95 characters.
constexpr std::size_t   kFingerprintChars   = 95;
constexpr std::size_t   kMaxFingerprintChars = kFingerprintChars;
constexpr std::size_t   kMaxAlgorithmChars  = 16;
constexpr std::uint32_t kMaxPeerId          = 65535;
} // namespace Limits

enum class SignalKind : std::uint8_t { Offer, Answer, Candidate };

inline const char* signalKindName(SignalKind kind) {
    switch(kind) {
        case SignalKind::Offer:     return "offer";
        case SignalKind::Answer:    return "answer";
        case SignalKind::Candidate: return "candidate";
    }
    return "offer";
}

inline bool parseSignalKind(const std::string& text, SignalKind& out) {
    if(text == "offer")     { out = SignalKind::Offer;     return true; }
    if(text == "answer")    { out = SignalKind::Answer;    return true; }
    if(text == "candidate") { out = SignalKind::Candidate; return true; }
    return false;
}

/// A member of our room, as the service describes it.
struct RoomMember {
    std::uint32_t   id   = 0;
    RoomRelay::Role role = RoomRelay::Role::Unknown;
    std::string     name;
    std::string     runtime;    ///< the peer's claim, carried as one and never trusted
};

/**
    The DTLS fingerprint the service bound to one *pair* of peers.

    Per pair, not per player: a browser normally gives every RTCPeerConnection its own
    certificate, so the same player legitimately presents different fingerprints to different
    opponents. A record names both ends, and a client refuses one whose `to` is not itself - which
    also means a service cannot quietly hand peer A the binding it made for peer B.
*/
struct PeerFingerprint {
    std::uint32_t from = 0;     ///< the peer whose certificate this is
    std::uint32_t to   = 0;     ///< the peer it was bound for; must be the poller
    std::string   algorithm;    ///< always "sha-256"
    std::string   value;        ///< uppercase colon-separated hex, as it appears in an SDP
};

/// One offer, answer or candidate, already attributed to a room member by the service.
struct SignalRecord {
    std::uint32_t from     = 0;
    std::uint64_t sequence = 0;
    SignalKind    kind     = SignalKind::Offer;
    std::string   payload;      ///< the SDP or candidate text, decoded from hex
};

struct SessionResponse {
    bool          ok       = false;
    std::uint32_t peerId   = 0;
    std::string   session;      ///< 64 lowercase hex; lives in a header, never in a URL
    RoomRelay::Role role     = RoomRelay::Role::Unknown;
    std::uint8_t  maxPeers = 0;
    RoomRelay::Phase phase  = RoomRelay::Phase::Lobby;
    std::string   roomCode;
    std::vector<std::string> iceServers;     ///< STUN only; a TURN url is refused here
    std::string   errorCode;
    std::string   errorMessage;
};

struct PollResponse {
    bool             ok      = false;
    RoomRelay::Phase phase   = RoomRelay::Phase::Lobby;
    std::uint64_t    cursor  = 0;
    bool             closed  = false;        ///< the service ended the room
    std::uint16_t    closeCode = 0;
    std::vector<RoomMember>      members;
    std::vector<std::uint32_t>   departed;
    std::vector<PeerFingerprint> fingerprints;
    std::vector<SignalRecord>    signals;
    std::string      errorCode;
    std::string      errorMessage;
};

inline bool isLowercaseHexText(const std::string& text) {
    for(const char c : text) {
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return !text.empty();
}

/**
    A canonical decimal number, bounded before the arithmetic rather than after it.

    Checking the accumulator afterwards is not enough: 18446744073709551617 wraps a 64-bit
    accumulator to 1, which passes any limit at all. The bound is therefore applied to the digit
    about to be consumed.
*/
inline bool parseUnsigned(const std::string& text, std::uint64_t limit, std::uint64_t& out) {
    if(text.empty() || text.size() > 20) {
        return false;
    }
    if(text.size() > 1 && text[0] == '0') {
        return false;   // no leading zeros: one value, one spelling
    }
    out = 0;
    for(const char c : text) {
        if(c < '0' || c > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        // Both halves matter: the first stops (limit - digit) wrapping for a tiny limit, the
        // second stops the accumulator wrapping for a long input.
        if(digit > limit || out > (limit - digit) / 10) {
            return false;
        }
        out = out * 10 + digit;
    }
    return out <= limit;
}

/// Decodes lowercase hex into text, refusing control characters. Used for names and SDP bodies.
inline bool decodeHexText(const std::string& hex, std::size_t maxBytes, bool allowNewlines,
                          std::string& out) {
    if(hex.size() > maxBytes * 2 || hex.size() % 2 != 0 || (!hex.empty() && !isLowercaseHexText(hex))) {
        return false;
    }
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out.clear();
    out.reserve(hex.size() / 2);
    for(std::size_t i = 0; i < hex.size(); i += 2) {
        const unsigned char c = static_cast<unsigned char>(nibble(hex[i]) * 16 + nibble(hex[i + 1]));
        const bool acceptable = c >= 32 ? c != 127
                                        : (allowNewlines && (c == '\r' || c == '\n'));
        if(!acceptable) {
            return false;
        }
        out.push_back(static_cast<char>(c));
    }
    return true;
}

/// Hex-encodes text for a request body. The request side never sends raw SDP.
inline std::string encodeHexText(const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for(const unsigned char c : text) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 15]);
    }
    return out;
}

/**
    A STUN url and nothing else.

    A TURN url would mean gameplay travelling through a third party, which this transport does
    not do. Refusing it here means a compromised or misconfigured service cannot introduce one.
*/
inline bool isAcceptableIceUrl(const std::string& url) {
    if(url.size() > Limits::kMaxIceUrlChars || url.size() < 8) {
        return false;
    }
    const bool stun  = url.compare(0, 5, "stun:") == 0;
    const bool stuns = url.compare(0, 6, "stuns:") == 0;
    if(!stun && !stuns) {
        return false;
    }
    // Exactly the character set the vendored browser transport accepts (its directIceServers()
    // regex). A url this side allowed and the browser refused would fail only in the browser,
    // and only at the moment two players tried to connect.
    for(const char c : url) {
        const bool acceptable = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                             || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':'
                             || c == '[' || c == ']';
        if(!acceptable) {
            return false;
        }
    }
    return true;
}

/**
    A candidate line that would route gameplay through a relay server.

    Both ends refuse these, so neither a service that offers TURN nor a peer that asks for it can
    turn a direct match into a relayed one behind the player's back.
*/
inline bool mentionsRelayCandidate(const std::string& text) {
    std::string lowered;
    lowered.reserve(text.size());
    for(const char c : text) {
        lowered.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return lowered.find("typ relay") != std::string::npos
        || lowered.find("turn:") != std::string::npos
        || lowered.find("turns:") != std::string::npos;
}

/**
    Extracts the one DTLS fingerprint a description is allowed to carry, and checks the shape.

    The session description for a direct match describes one data channel and nothing else: one
    `m=application` media section, exactly one `a=fingerprint:sha-256` line, no audio, no video.
    Anything else is refused here rather than handed to the RTC stack.

    The fingerprint is what binds the connection to an admitted player. It is per *pair*, not per
    player: each peer connection normally has its own certificate, so the same player legitimately
    presents different fingerprints to different opponents. The signaling service binds the first
    fingerprint it sees for a (room, from, to) triple, and this side checks the description
    against the one the service attested for that pair. The RTC stack then verifies the
    certificate actually presented against the fingerprint in the description it accepted, which
    is what makes the check mean anything.
*/
inline bool extractSdpFingerprint(const std::string& sdp, std::string& algorithm, std::string& value) {
    if(sdp.empty() || sdp.size() > Limits::kMaxSdpBytes) {
        return false;
    }
    bool found = false;
    std::size_t mediaSections = 0;
    bool dataOnly = true;
    std::size_t cursor = 0;
    while(cursor <= sdp.size()) {
        std::size_t end = sdp.find('\n', cursor);
        if(end == std::string::npos) {
            end = sdp.size();
        }
        std::size_t length = end - cursor;
        if(length > 0 && sdp[cursor + length - 1] == '\r') {
            --length;
        }
        const std::string line = sdp.substr(cursor, length);
        if(line.compare(0, 2, "m=") == 0) {
            ++mediaSections;
            if(line.compare(0, 13, "m=application") != 0) {
                dataOnly = false;
            }
        } else if(line.compare(0, 14, "a=fingerprint:") == 0) {
            if(found) {
                return false;   // one description, one fingerprint
            }
            const std::string rest = line.substr(14);
            const std::size_t space = rest.find(' ');
            if(space == std::string::npos || space == 0 || space + 1 >= rest.size()) {
                return false;
            }
            std::string algorithmCandidate = rest.substr(0, space);
            std::string valueCandidate     = rest.substr(space + 1);
            if(valueCandidate.size() > Limits::kMaxFingerprintChars) {
                return false;
            }
            for(char& c : algorithmCandidate) {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            for(char& c : valueCandidate) {
                c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 'a' + 'A' : c);
            }
            // sha-256 only: it is what every current browser and libdatachannel offer, and
            // accepting a weaker digest would let a chosen-certificate attack past the binding.
            if(algorithmCandidate != "sha-256"
               || valueCandidate.size() != Limits::kFingerprintChars) {
                return false;
            }
            for(std::size_t i = 0; i < valueCandidate.size(); ++i) {
                const char c = valueCandidate[i];
                const bool separator = (i % 3) == 2;
                if(separator ? c != ':'
                             : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
            algorithm = algorithmCandidate;
            value     = valueCandidate;
            found     = true;
        }
        if(end >= sdp.size()) {
            break;
        }
        cursor = end + 1;
    }
    return found && dataOnly && mediaSections == 1;
}

/**
    A candidate as this protocol carries it: "<sdpMid>|<candidate line>".

    The rule is deliberately the same one the vendored browser transport applies in
    validateDirectCandidate(): a media id of 1..64 of `[A-Za-z0-9_-]`, a line that begins with
    "candidate:", has a host, server-reflexive or peer-reflexive type, has no relayed type, and
    contains no line breaks. If the two sides disagreed about what a candidate may be, the
    disagreement would only show up when a browser and a desktop tried to play each other.
*/
inline bool isAcceptableCandidatePayload(const std::string& payload) {
    const std::size_t split = payload.find('|');
    if(split == std::string::npos || split == 0 || split > Limits::kMaxCandidateMidChars) {
        return false;
    }
    const std::string mid  = payload.substr(0, split);
    const std::string line = payload.substr(split + 1);
    if(line.empty() || line.size() > Limits::kMaxCandidateBytes) {
        return false;
    }
    for(const char c : mid) {
        const bool acceptable = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                             || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if(!acceptable) {
            return false;
        }
    }
    for(const char c : line) {
        if(c == '\r' || c == '\n' || c == '\0') {
            return false;
        }
    }
    if(line.compare(0, 10, "candidate:") != 0) {
        return false;
    }

    // The candidate type, read as a whole token so that "typ hostile" is not a host candidate
    // and "typ relayed" is still refused.
    std::size_t typePosition = std::string::npos;
    for(std::size_t i = 0; i + 5 <= line.size(); ++i) {
        if(line[i] == ' ' && line.compare(i, 5, " typ ") == 0) {
            typePosition = i + 5;
        }
    }
    if(typePosition == std::string::npos) {
        return false;
    }
    std::size_t typeEnd = typePosition;
    while(typeEnd < line.size() && line[typeEnd] != ' ') {
        ++typeEnd;
    }
    const std::string type = line.substr(typePosition, typeEnd - typePosition);
    if(type != "host" && type != "srflx" && type != "prflx") {
        return false;
    }
    return !mentionsRelayCandidate(line);
}

/**
    Removes `a=sctp-init:` lines from a remote description before libdatachannel parses it.

    This is not a theoretical precaution. Chromium now offers the SCTP zero-RTT extension as
    `a=sctp-init:<base64>`. libdatachannel does not implement the attribute, but it reciprocates
    unknown application-level attributes into its answer, so it ends up advertising an extension
    it will not honour; the browser then opens the channel in a way the native side rejects
    ("Got unexpected message on stream 1") and both ends fail. Dropping the line before the
    description is constructed stops the false advertisement, and changes nothing else: the
    fingerprint, the ICE credentials and the DTLS role are all still there, so certificate
    verification is untouched.

    Whole lines only, matched on the exact prefix - never a substring rewrite of an SDP.
*/
inline std::string stripUnsupportedSctpInit(const std::string& sdp) {
    static const char kAttribute[] = "a=sctp-init:";
    constexpr std::size_t kAttributeLength = sizeof(kAttribute) - 1;
    std::string out;
    out.reserve(sdp.size());
    for(std::size_t position = 0; position < sdp.size();) {
        std::size_t end = sdp.find('\n', position);
        if(end == std::string::npos) {
            end = sdp.size();
        } else {
            ++end;      // the line separator belongs to the line, so CRLF survives untouched
        }
        if(sdp.compare(position, kAttributeLength, kAttribute) != 0) {
            out.append(sdp, position, end - position);
        }
        position = end;
    }
    return out;
}

/// Percent-encodes one form value; only unreserved characters survive unescaped.
inline std::string encodeFormValue(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for(const char rawChar : value) {
        const unsigned char c = static_cast<unsigned char>(rawChar);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                             || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
                             || c == '~';
        if(unreserved) {
            out.push_back(rawChar);
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

/// One key=value line, already bounded. The callback decides what the key means.
template<typename Handler>
inline bool forEachResponseLine(const std::string& body, Handler&& handler, std::string& error) {
    if(body.empty() || body.size() > Limits::kMaxResponseBytes) {
        error = "The game service sent an unusable answer.";
        return false;
    }
    std::size_t lineCount = 0;
    std::size_t cursor = 0;
    while(cursor < body.size()) {
        std::size_t lineEnd = body.find('\n', cursor);
        if(lineEnd == std::string::npos) {
            lineEnd = body.size();
        }
        std::size_t lineLength = lineEnd - cursor;
        if(lineLength > 0 && body[cursor + lineLength - 1] == '\r') {
            --lineLength;
        }
        if(lineLength > 0) {
            if(++lineCount > Limits::kMaxResponseLines || lineLength > Limits::kMaxLineBytes) {
                error = "The game service sent an unusable answer.";
                return false;
            }
            const std::string line = body.substr(cursor, lineLength);
            const std::size_t split = line.find('=');
            if(split == std::string::npos || split == 0 || split > Limits::kMaxKeyBytes
               || line.size() - split - 1 > Limits::kMaxValueBytes) {
                error = "The game service sent an unusable answer.";
                return false;
            }
            if(!handler(line.substr(0, split), line.substr(split + 1))) {
                error = "The game service sent an unusable answer.";
                return false;
            }
        }
        cursor = (lineEnd >= body.size()) ? body.size() : lineEnd + 1;
    }
    return true;
}

/// Splits "a|b|c" into exactly `count` fields. A missing or extra separator is a hard failure.
inline bool splitFields(const std::string& value, std::size_t count, std::vector<std::string>& out) {
    out.clear();
    std::size_t start = 0;
    for(std::size_t i = 0; i + 1 < count; ++i) {
        const std::size_t end = value.find('|', start);
        if(end == std::string::npos) {
            return false;
        }
        out.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    const std::string last = value.substr(start);
    if(last.find('|') != std::string::npos) {
        return false;
    }
    out.push_back(last);
    return true;
}

inline bool parsePhaseText(const std::string& text, RoomRelay::Phase& out) {
    if(text == "lobby") { out = RoomRelay::Phase::Lobby; return true; }
    if(text == "match") { out = RoomRelay::Phase::Match; return true; }
    return false;
}

/**
    Parses the answer to POST /v1/p2p/session.

    A well-formed refusal is a success for this function - the caller needs the code to say
    something useful - but an answer that is neither a clean success nor a clean refusal is not
    guessed about.
*/
inline bool parseSessionResponse(const std::string& body, SessionResponse& out, std::string& error) {
    out = SessionResponse();
    bool sawStatus = false;
    bool sawPeer = false;
    bool sawSession = false;
    bool sawRole = false;
    bool failed = false;

    const auto handler = [&](const std::string& key, const std::string& value) {
        if(key == "status") {
            if(value != "ok" && value != "error") return false;
            out.ok    = value == "ok";
            sawStatus = true;
        } else if(key == "protocol") {
            std::uint64_t parsed = 0;
            if(!parseUnsigned(value, 65535, parsed)) return false;
            if(parsed != RoomRelay::kProtocolVersion) {
                failed = true;
            }
        } else if(key == "peer") {
            std::uint64_t parsed = 0;
            if(!parseUnsigned(value, Limits::kMaxPeerId, parsed) || parsed == 0) return false;
            out.peerId = static_cast<std::uint32_t>(parsed);
            sawPeer    = true;
        } else if(key == "session") {
            if(value.size() != Limits::kMaxSessionChars || !isLowercaseHexText(value)) return false;
            out.session = value;
            sawSession  = true;
        } else if(key == "role") {
            if(value == "host")        out.role = RoomRelay::Role::Host;
            else if(value == "client") out.role = RoomRelay::Role::Client;
            else return false;
            sawRole = true;
        } else if(key == "maxPeers") {
            std::uint64_t parsed = 0;
            if(!parseUnsigned(value, RoomRelay::Limits::kMaxPeersPerRoom, parsed) || parsed < 2) {
                return false;
            }
            out.maxPeers = static_cast<std::uint8_t>(parsed);
        } else if(key == "phase") {
            if(!parsePhaseText(value, out.phase)) return false;
        } else if(key == "room") {
            if(!RoomRelay::isAcceptableRoomCode(value)) return false;
            out.roomCode = value;
        } else if(key == "ice") {
            if(out.iceServers.size() >= Limits::kMaxIceServers || !isAcceptableIceUrl(value)) {
                return false;
            }
            out.iceServers.push_back(value);
        } else if(key == "code") {
            out.errorCode = value;
        } else if(key == "message") {
            out.errorMessage = RoomRelay::sanitizeRelayMessage(value);
        }
        return true;    // unknown keys are skipped on purpose
    };

    if(!forEachResponseLine(body, handler, error)) {
        return false;
    }
    if(!sawStatus || failed) {
        error = failed ? "This version of the game cannot use that game service."
                       : "The game service sent an unusable answer.";
        return false;
    }
    if(!out.ok) {
        if(out.errorMessage.empty()) {
            out.errorMessage = "The game service refused the request.";
        }
        return true;
    }
    if(!sawPeer || !sawSession || !sawRole || out.maxPeers < 2) {
        error = "The game service sent an incomplete answer.";
        return false;
    }
    return true;
}

/**
    Parses the answer to POST /v1/p2p/poll.

    Every record is bounded before it is believed, and then the answer is checked as a whole: a
    poll answer is a snapshot of one room at one moment, and a snapshot that could not describe a
    real room - two hosts, a repeated player, somebody both present and departed, a signal from
    somebody who is not in it - is refused entirely rather than applied in part.

    \param  localPeerId  this client's own peer id, so that records bound to another pair, or a
                         role claim about ourselves, can be refused here rather than downstream
*/
inline bool parsePollResponse(const std::string& body, std::uint32_t localPeerId, PollResponse& out,
                              std::string& error) {
    out = PollResponse();
    bool sawStatus = false;
    bool sawCursor = false;

    const auto handler = [&](const std::string& key, const std::string& value) {
        if(key == "status") {
            if(value != "ok" && value != "error") return false;
            out.ok    = value == "ok";
            sawStatus = true;
        } else if(key == "phase") {
            if(!parsePhaseText(value, out.phase)) return false;
        } else if(key == "cursor") {
            std::uint64_t parsed = 0;
            if(!parseUnsigned(value, 1000000000000ull, parsed)) return false;
            out.cursor = parsed;
            sawCursor  = true;
        } else if(key == "closed") {
            std::uint64_t parsed = 0;
            if(!parseUnsigned(value, 65535, parsed)) return false;
            out.closed    = true;
            out.closeCode = static_cast<std::uint16_t>(parsed);
        } else if(key == "peer") {
            std::vector<std::string> fields;
            if(out.members.size() >= RoomRelay::Limits::kMaxPeersPerRoom
               || !splitFields(value, 4, fields)) {
                return false;
            }
            std::uint64_t id = 0;
            RoomMember member;
            if(!parseUnsigned(fields[0], Limits::kMaxPeerId, id) || id == 0) return false;
            if(fields[1] == "host")        member.role = RoomRelay::Role::Host;
            else if(fields[1] == "client") member.role = RoomRelay::Role::Client;
            else return false;
            if(!decodeHexText(fields[2], RoomRelay::Limits::kMaxNameChars, false, member.name)
               || !RoomRelay::isAcceptableDisplayName(member.name)) {
                return false;
            }
            if(!decodeHexText(fields[3], RoomRelay::Limits::kMaxRuntimeChars, false, member.runtime)) {
                return false;
            }
            member.id = static_cast<std::uint32_t>(id);
            out.members.push_back(member);
        } else if(key == "gone") {
            std::uint64_t id = 0;
            if(out.departed.size() >= RoomRelay::Limits::kMaxPeersPerRoom
               || !parseUnsigned(value, Limits::kMaxPeerId, id) || id == 0) {
                return false;
            }
            out.departed.push_back(static_cast<std::uint32_t>(id));
        } else if(key == "fp") {
            std::vector<std::string> fields;
            if(out.fingerprints.size() >= RoomRelay::Limits::kMaxPeersPerRoom
               || !splitFields(value, 4, fields)) {
                return false;
            }
            std::uint64_t id = 0;
            std::uint64_t boundTo = 0;
            if(!parseUnsigned(fields[0], Limits::kMaxPeerId, id) || id == 0) return false;
            if(!parseUnsigned(fields[1], Limits::kMaxPeerId, boundTo) || boundTo == 0
               || boundTo == id) {
                return false;
            }
            fields.erase(fields.begin() + 1);   // the rest is read as <algorithm>|<value>
            // sha-256 only, and in the exact "AA:BB:.." shape an SDP carries, so the value can
            // be compared to the description's fingerprint without normalising anything.
            if(fields[1] != "sha-256" || fields[2].size() != Limits::kFingerprintChars) {
                return false;
            }
            for(std::size_t i = 0; i < fields[2].size(); ++i) {
                const char c = fields[2][i];
                const bool separator = (i % 3) == 2;
                if(separator ? c != ':' : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
            PeerFingerprint fingerprint;
            fingerprint.from      = static_cast<std::uint32_t>(id);
            fingerprint.to        = static_cast<std::uint32_t>(boundTo);
            fingerprint.algorithm = fields[1];
            fingerprint.value     = fields[2];
            out.fingerprints.push_back(fingerprint);
        } else if(key == "sig") {
            std::vector<std::string> fields;
            if(out.signals.size() >= Limits::kMaxSignalsPerPoll || !splitFields(value, 4, fields)) {
                return false;
            }
            std::uint64_t from = 0;
            std::uint64_t sequence = 0;
            SignalRecord record;
            if(!parseUnsigned(fields[0], Limits::kMaxPeerId, from) || from == 0) return false;
            if(!parseUnsigned(fields[1], 1000000000ull, sequence)) return false;
            if(!parseSignalKind(fields[2], record.kind)) return false;
            if(!decodeHexText(fields[3], Limits::kMaxSignalBytes, true, record.payload)
               || record.payload.empty()) {
                return false;
            }
            record.from     = static_cast<std::uint32_t>(from);
            record.sequence = sequence;
            out.signals.push_back(record);
        } else if(key == "code") {
            out.errorCode = value;
        } else if(key == "message") {
            out.errorMessage = RoomRelay::sanitizeRelayMessage(value);
        }
        return true;
    };

    if(!forEachResponseLine(body, handler, error)) {
        return false;
    }
    if(!sawStatus) {
        error = "The game service sent an unusable answer.";
        return false;
    }
    if(!out.ok) {
        if(out.errorMessage.empty()) {
            out.errorMessage = "The game service refused the request.";
        }
        return true;
    }
    if(!sawCursor) {
        error = "The game service sent an incomplete answer.";
        return false;
    }
    // A record whose sequence goes backwards means the service is replaying, and a replayed
    // offer is how a negotiation gets confused.
    for(std::size_t i = 1; i < out.signals.size(); ++i) {
        if(out.signals[i].sequence <= out.signals[i - 1].sequence) {
            error = "The game service sent signals out of order.";
            return false;
        }
    }
    if(!out.signals.empty() && out.signals.back().sequence > out.cursor) {
        error = "The game service sent signals past its own cursor.";
        return false;
    }

    // The snapshot as a whole.
    std::size_t hostCount = 0;
    for(std::size_t i = 0; i < out.members.size(); ++i) {
        const RoomMember& member = out.members[i];
        if(member.role == RoomRelay::Role::Host) {
            ++hostCount;
        }
        for(std::size_t j = i + 1; j < out.members.size(); ++j) {
            // A repeated id is a contradiction; a repeated name is how one player is mistaken
            // for another everywhere the lobby and the command path use names as identity.
            if(member.id == out.members[j].id || member.name == out.members[j].name) {
                error = "The game service described the same player twice.";
                return false;
            }
        }
        for(const std::uint32_t departed : out.departed) {
            if(departed == member.id) {
                error = "The game service said a player both joined and left.";
                return false;
            }
        }
    }
    if(hostCount > 1) {
        error = "The game service described more than one host.";
        return false;
    }

    const auto isMember = [&out](std::uint32_t id) {
        for(const RoomMember& member : out.members) {
            if(member.id == id) {
                return true;
            }
        }
        return false;
    };

    for(const PeerFingerprint& fingerprint : out.fingerprints) {
        // An attestation is for one pair, and the only pair this client may act on is one that
        // ends at itself.
        if(localPeerId != 0 && fingerprint.to != localPeerId) {
            error = "The game service sent an identity bound to a different player.";
            return false;
        }
        if(fingerprint.from == localPeerId || !isMember(fingerprint.from)) {
            error = "The game service sent an identity for somebody who is not in this game.";
            return false;
        }
    }
    for(const SignalRecord& record : out.signals) {
        if(record.from == localPeerId || !isMember(record.from)) {
            error = "The game service sent a connection message from outside this game.";
            return false;
        }
        if(record.kind == SignalKind::Candidate) {
            if(!isAcceptableCandidatePayload(record.payload)) {
                error = "The game service sent an unusable connection candidate.";
                return false;
            }
        } else if(record.payload.size() > Limits::kMaxSdpBytes) {
            error = "The game service sent an oversized connection description.";
            return false;
        }
    }
    for(std::size_t i = 0; i < out.departed.size(); ++i) {
        for(std::size_t j = i + 1; j < out.departed.size(); ++j) {
            if(out.departed[i] == out.departed[j]) {
                error = "The game service said the same player left twice.";
                return false;
            }
        }
    }
    return true;
}

} // namespace P2PSignal

#endif // P2PSIGNALINGPROTOCOL_H
