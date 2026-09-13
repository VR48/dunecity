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

#ifndef P2PWIREFRAMING_H
#define P2PWIREFRAMING_H

/**
    The bytes on a direct peer-to-peer data channel, and nothing else.

    The browser side of a direct match is P2PKit's RTCTransport, which carries a JSON value and
    fragments it with P2PKit's Chunker. The native side has to produce and accept exactly that,
    or browser-native crossplay does not work. This header is that agreement, written once and
    used by both native backends:

      game packet bytes
        -> envelope string      "g:<channel>:<recipient>:<hex>"   (§3.2 of the wire contract)
          -> JSON value         a JSON *string*, so no object parser is needed in C++
            -> chunk packets    {"id":..,"i":..,"n":..,"part":..} , one per channel message

    Everything here is header-only and depends on nothing but the standard library, so the tests
    exercise the real encoder and the real decoder rather than a copy of them.

    The decoder is the hostile half. Upstream's Chunker is deliberately unbounded - it trusts its
    own transport - so every bound a remote peer could push on lives here: message size, group
    count, fragment count, reassembled size and group lifetime. A violation drops the message and
    is reported to the caller as a refusal reason, never as a partial payload.
*/

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace P2PWire {

namespace Limits {
/**
    Largest single data-channel message we will parse.

    A fragment carries at most kMaxPacketSize characters, but JSON escaping of a hostile payload
    can inflate that, so the raw message bound is generous where the *content* bound is not.
*/
constexpr std::size_t kMaxChannelMessageBytes = 131072;
/// Characters per fragment when sending. Matches the vendored Chunker's default exactly.
constexpr std::size_t kMaxPacketSize = 16000;
/// Largest reassembled JSON text. A 262128-byte game packet hex-encodes to 524258 characters.
constexpr std::size_t kMaxAssembledBytes = 1048576;
/// Largest game packet, identical to the relay transport's bound.
constexpr std::size_t kMaxGamePayloadBytes = 262128;
/// Largest diagnostic payload, identical to the relay transport's bound.
constexpr std::size_t kMaxDiagnosticBytes = 4096;
/// Fragments one payload may claim.
constexpr std::size_t kMaxFragments = 128;
/// Groups that may be partially received at once, per peer.
constexpr std::size_t kMaxOpenGroups = 16;
/// Aggregate bytes held across all partially received groups, per peer.
constexpr std::size_t kMaxPendingBytes = 4194304;
/// Characters in a group id.
constexpr std::size_t kMaxGroupIdChars = 64;
/// How long a partially received group may sit before the peer is treated as broken.
constexpr std::uint32_t kGroupLifetimeMs = 15000;
} // namespace Limits

/// What a decoded envelope turned out to be.
enum class EnvelopeKind : std::uint8_t {
    Game,       ///< a serialized game packet, to be handed to GamePayloadRouter unchanged
    Diagnostic, ///< a bounded diagnostic; never a game packet and never routed as one
    /**
        This peer's view of the room and of its own direct connections.

        A host with a channel to each guest has not established that the guests can reach each
        other. Each peer says which roster it sees and which of those peers it is actually
        connected to, and the match only starts when every peer agrees and is fully connected.
    */
    Readiness,
    Ping,       ///< liveness probe; the reply measures the direct path, not a server's
    Pong
};

struct Envelope {
    EnvelopeKind              kind           = EnvelopeKind::Game;
    /// Readiness only: the sender's roster, as sorted decimal peer ids joined by commas.
    std::string               rosterKey;
    /// Readiness only: the peers the sender currently has an open direct channel to.
    std::vector<std::uint32_t> connectedPeers;
    /// Ping and pong only: echoed back unchanged, so a reply can be matched to its probe.
    std::uint32_t             token          = 0;
    /// 0 or 1, mirroring the ENet channel argument.
    int                       channel        = 0;
    /// 0 for "everyone I am connected to", or the addressed peer id. Never a forwarding request.
    std::uint32_t             recipient      = 0;
    std::uint8_t              diagnosticKind = 0;
    std::vector<std::uint8_t> payload;
};

inline bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

inline char hexDigit(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
}

inline unsigned hexValue(char c) {
    return static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10);
}

/// Lowercase hex of raw bytes. The only encoding a game payload ever gets on this path.
inline std::string encodeHex(const std::uint8_t* data, std::size_t length) {
    std::string out;
    out.reserve(length * 2);
    for(std::size_t i = 0; i < length; ++i) {
        out.push_back(hexDigit(static_cast<unsigned>(data[i]) >> 4));
        out.push_back(hexDigit(static_cast<unsigned>(data[i]) & 0x0F));
    }
    return out;
}

/// Strict: lowercase only, even length, bounded. Uppercase is a refusal, not a courtesy.
inline bool decodeHex(const std::string& text, std::size_t maxBytes, std::vector<std::uint8_t>& out) {
    if(text.size() % 2 != 0 || text.size() > maxBytes * 2) {
        return false;
    }
    out.clear();
    out.reserve(text.size() / 2);
    for(std::size_t i = 0; i < text.size(); i += 2) {
        if(!isHexDigit(text[i]) || !isHexDigit(text[i + 1])) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(hexValue(text[i]) * 16 + hexValue(text[i + 1])));
    }
    return true;
}

/**
    Writes one JSON string, quotes included.

    Everything this transport actually sends is escape-free ASCII; the escapes exist so that a
    payload which somehow is not cannot produce invalid JSON on the browser side.
*/
inline std::string encodeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for(const char rawChar : value) {
        const unsigned char c = static_cast<unsigned char>(rawChar);
        switch(c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if(c < 0x20) {
                    out += "\\u00";
                    out.push_back(hexDigit(c >> 4));
                    out.push_back(hexDigit(c & 0x0F));
                } else {
                    out.push_back(rawChar);
                }
        }
    }
    out.push_back('"');
    return out;
}

/// Appends one code point as UTF-8. Used only by the \\uXXXX escape path.
inline void appendUtf8(std::string& out, std::uint32_t codePoint) {
    if(codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if(codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if(codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

/**
    Reads one JSON string starting at `cursor`, which must be on the opening quote.

    \param  maxBytes  refuse rather than allocate beyond this, whatever the input claims
    \return true on success, with `cursor` left just past the closing quote
*/
inline bool decodeJsonString(const std::string& text, std::size_t& cursor, std::size_t maxBytes,
                             std::string& out) {
    if(cursor >= text.size() || text[cursor] != '"') {
        return false;
    }
    ++cursor;
    out.clear();
    while(cursor < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[cursor]);
        if(c == '"') {
            ++cursor;
            return true;
        }
        if(c < 0x20) {
            return false;   // a raw control character is never valid inside a JSON string
        }
        if(out.size() >= maxBytes) {
            return false;
        }
        if(c != '\\') {
            out.push_back(text[cursor]);
            ++cursor;
            continue;
        }
        if(cursor + 1 >= text.size()) {
            return false;
        }
        const char escape = text[cursor + 1];
        cursor += 2;
        switch(escape) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                const auto readHex4 = [&text, &cursor](std::uint32_t& value) {
                    if(cursor + 4 > text.size()) {
                        return false;
                    }
                    value = 0;
                    for(std::size_t i = 0; i < 4; ++i) {
                        const char digit = text[cursor + i];
                        unsigned nibble = 0;
                        if(digit >= '0' && digit <= '9')      nibble = static_cast<unsigned>(digit - '0');
                        else if(digit >= 'a' && digit <= 'f') nibble = static_cast<unsigned>(digit - 'a' + 10);
                        else if(digit >= 'A' && digit <= 'F') nibble = static_cast<unsigned>(digit - 'A' + 10);
                        else return false;
                        value = value * 16 + nibble;
                    }
                    cursor += 4;
                    return true;
                };
                std::uint32_t unit = 0;
                if(!readHex4(unit)) {
                    return false;
                }
                if(unit >= 0xD800 && unit <= 0xDBFF) {
                    // High surrogate: the low half must follow, or the string is malformed.
                    if(cursor + 2 > text.size() || text[cursor] != '\\' || text[cursor + 1] != 'u') {
                        return false;
                    }
                    cursor += 2;
                    std::uint32_t low = 0;
                    if(!readHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                } else if(unit >= 0xDC00 && unit <= 0xDFFF) {
                    return false;   // lone low surrogate
                }
                if(out.size() + 4 > maxBytes) {
                    return false;
                }
                appendUtf8(out, unit);
                break;
            }
            default:
                return false;
        }
    }
    return false;   // unterminated
}

/**
    Decimal, bounded, no sign, no leading zero beyond "0" itself.

    The bound is applied to the digit about to be consumed rather than to the result: checking
    afterwards lets 18446744073709551617 wrap a 64-bit accumulator to 1 and pass a limit of 1.
*/
inline bool parseBoundedDecimal(const std::string& text, std::uint64_t limit, std::uint64_t& out) {
    if(text.empty() || text.size() > 20) {
        return false;
    }
    if(text.size() > 1 && text[0] == '0') {
        return false;
    }
    out = 0;
    for(const char c : text) {
        if(c < '0' || c > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if(digit > limit || out > (limit - digit) / 10) {
            return false;
        }
        out = out * 10 + digit;
    }
    return out <= limit;
}

/**
    Builds the envelope text (§3.2) for one game packet.
    \return the JSON value text, ready to be fragmented, or an empty string if the payload is
            outside the agreed bounds
*/
inline std::string encodeGameEnvelope(const std::uint8_t* payload, std::size_t length, int channel,
                                      std::uint32_t recipient) {
    if(payload == nullptr || length == 0 || length > Limits::kMaxGamePayloadBytes
       || (channel != 0 && channel != 1)) {
        return std::string();
    }
    std::string body = "g:";
    body.push_back(static_cast<char>('0' + channel));
    body += ':';
    body += std::to_string(recipient);
    body += ':';
    body += encodeHex(payload, length);
    return encodeJsonString(body);
}

/// Builds the envelope text for one bounded diagnostic. Never carries a game packet.
inline std::string encodeDiagnosticEnvelope(std::uint8_t kind, const std::uint8_t* payload,
                                            std::size_t length) {
    if(length > Limits::kMaxDiagnosticBytes || (payload == nullptr && length > 0)) {
        return std::string();
    }
    std::string body = "d:";
    body += std::to_string(static_cast<unsigned>(kind));
    body += ':';
    body += encodeHex(payload, length);
    return encodeJsonString(body);
}

/// Builds a readiness envelope: what this peer sees, and who it can actually reach.
inline std::string encodeReadinessEnvelope(const std::string& rosterKey,
                                           const std::vector<std::uint32_t>& connectedPeers) {
    std::string body = "r:";
    body += rosterKey;
    body += ':';
    for(std::size_t i = 0; i < connectedPeers.size(); ++i) {
        if(i != 0) {
            body += ',';
        }
        body += std::to_string(connectedPeers[i]);
    }
    return encodeJsonString(body);
}

/// Builds a ping or a pong. The token is echoed so a reply can be matched to its probe.
inline std::string encodeProbeEnvelope(bool pong, std::uint32_t token) {
    std::string body = pong ? "q:" : "p:";
    body += std::to_string(token);
    return encodeJsonString(body);
}

/// Parses "1,2,3" into peer ids. Empty input is an empty list; anything malformed is a failure.
inline bool parsePeerIdList(const std::string& text, std::vector<std::uint32_t>& out) {
    out.clear();
    if(text.empty()) {
        return true;
    }
    std::size_t start = 0;
    while(true) {
        const std::size_t end = text.find(',', start);
        const std::string field = text.substr(start, end == std::string::npos ? std::string::npos
                                                                             : end - start);
        std::uint64_t value = 0;
        if(!parseBoundedDecimal(field, 65535, value) || value == 0 || out.size() >= 16) {
            return false;
        }
        out.push_back(static_cast<std::uint32_t>(value));
        if(end == std::string::npos) {
            return true;
        }
        start = end + 1;
    }
}

/**
    Decodes one reassembled JSON value into an envelope.

    \param  valueText  the JSON text a peer sent, i.e. what JSON.stringify() produced
    \param  out        filled in only on success
    \param  reason     a short, non player-facing refusal reason on failure

    Failure is never something to skip past: the caller closes the connection, because a peer
    sending something this cannot understand is not a peer whose command stream is trustworthy.
*/
inline bool decodeEnvelope(const std::string& valueText, Envelope& out, const char*& reason) {
    std::size_t cursor = 0;
    std::string body;
    if(!decodeJsonString(valueText, cursor, Limits::kMaxAssembledBytes, body)) {
        reason = "envelope is not a JSON string";
        return false;
    }
    if(cursor != valueText.size()) {
        reason = "trailing data after envelope";
        return false;
    }
    if(body.size() < 2 || body[1] != ':') {
        reason = "envelope has no kind";
        return false;
    }

    const char kind = body[0];
    // How many colon-separated fields this kind has, counting the kind itself. The last field is
    // never split, so a payload containing a colon could not confuse the reader even if hex
    // encoding did not already rule that out.
    std::size_t wanted = 0;
    switch(kind) {
        case 'g': wanted = 4; break;
        case 'd': wanted = 3; break;
        case 'r': wanted = 3; break;
        case 'p':
        case 'q': wanted = 2; break;
        default:
            reason = "unknown envelope kind";
            return false;
    }

    std::vector<std::string> fields;
    fields.reserve(wanted);
    std::size_t start = 0;
    while(fields.size() + 1 < wanted) {
        const std::size_t end = body.find(':', start);
        if(end == std::string::npos) {
            reason = "envelope has too few fields";
            return false;
        }
        fields.push_back(body.substr(start, end - start));
        start = end + 1;
    }
    fields.push_back(body.substr(start));

    out = Envelope();
    switch(kind) {
        case 'g': {
            std::uint64_t channel = 0;
            std::uint64_t recipient = 0;
            if(!parseBoundedDecimal(fields[1], 1, channel)) {
                reason = "envelope channel is not 0 or 1";
                return false;
            }
            if(!parseBoundedDecimal(fields[2], 0xFFFFFFFFull, recipient)) {
                reason = "envelope recipient is not a peer id";
                return false;
            }
            if(fields[3].empty()
               || !decodeHex(fields[3], Limits::kMaxGamePayloadBytes, out.payload)) {
                reason = "envelope payload is not bounded lowercase hex";
                return false;
            }
            out.kind      = EnvelopeKind::Game;
            out.channel   = static_cast<int>(channel);
            out.recipient = static_cast<std::uint32_t>(recipient);
            return true;
        }
        case 'd': {
            std::uint64_t diagnostic = 0;
            if(!parseBoundedDecimal(fields[1], 255, diagnostic)) {
                reason = "diagnostic kind is out of range";
                return false;
            }
            if(!decodeHex(fields[2], Limits::kMaxDiagnosticBytes, out.payload)) {
                reason = "diagnostic payload is not bounded lowercase hex";
                return false;
            }
            out.kind           = EnvelopeKind::Diagnostic;
            out.diagnosticKind = static_cast<std::uint8_t>(diagnostic);
            return true;
        }
        case 'r': {
            std::vector<std::uint32_t> roster;
            if(fields[1].size() > 128 || !parsePeerIdList(fields[1], roster)
               || !parsePeerIdList(fields[2], out.connectedPeers)) {
                reason = "readiness envelope is malformed";
                return false;
            }
            out.kind      = EnvelopeKind::Readiness;
            out.rosterKey = fields[1];
            return true;
        }
        case 'p':
        case 'q': {
            std::uint64_t token = 0;
            if(!parseBoundedDecimal(fields[1], 0xFFFFFFFFull, token)) {
                reason = "probe token is out of range";
                return false;
            }
            out.kind  = kind == 'q' ? EnvelopeKind::Pong : EnvelopeKind::Ping;
            out.token = static_cast<std::uint32_t>(token);
            return true;
        }
        default:
            break;
    }
    reason = "unknown envelope kind";
    return false;
}

/// One fragment, exactly as P2PKit's Chunker defines it.
struct ChunkPacket {
    std::string   id;
    std::uint32_t index = 0;
    std::uint32_t total = 0;
    std::string   part;
};

/**
    Serializes one fragment.

    Key order is id, i, n, part and there is no whitespace, matching what the browser produces.
    Order does not matter to either reader; matching it keeps captures comparable.
*/
inline std::string encodeChunkPacket(const ChunkPacket& packet) {
    std::string out = "{\"id\":";
    out += encodeJsonString(packet.id);
    out += ",\"i\":";
    out += std::to_string(packet.index);
    out += ",\"n\":";
    out += std::to_string(packet.total);
    out += ",\"part\":";
    out += encodeJsonString(packet.part);
    out += '}';
    return out;
}

/**
    Splits one JSON value text into fragments, exactly as Chunker.split() does.

    Always yields at least one fragment, so an empty value still produces a message.
*/
inline std::vector<std::string> splitIntoChunkPackets(const std::string& groupId,
                                                      const std::string& data) {
    std::vector<std::string> out;
    const std::size_t size = Limits::kMaxPacketSize;
    const std::size_t total = data.empty() ? 1 : (data.size() + size - 1) / size;
    out.reserve(total);
    for(std::size_t i = 0; i < total; ++i) {
        ChunkPacket packet;
        packet.id    = groupId;
        packet.index = static_cast<std::uint32_t>(i);
        packet.total = static_cast<std::uint32_t>(total);
        packet.part  = data.substr(i * size, size);
        out.push_back(encodeChunkPacket(packet));
    }
    return out;
}

/**
    Parses one data-channel message into a fragment.

    A minimal, strict JSON object reader: it is the only JSON *object* this transport ever has to
    understand, and writing it here avoids adding a general JSON parser to the game. Unknown keys
    are skipped so the browser side can grow a field without breaking native clients; the four
    known keys are all required, must have the right type, and must not repeat.
*/
inline bool parseChunkPacket(const std::string& message, ChunkPacket& out, const char*& reason) {
    if(message.size() > Limits::kMaxChannelMessageBytes) {
        reason = "channel message is too large";
        return false;
    }
    std::size_t cursor = 0;
    const auto skipSpace = [&message, &cursor]() {
        while(cursor < message.size()
              && (message[cursor] == ' ' || message[cursor] == '\t' || message[cursor] == '\n'
                  || message[cursor] == '\r')) {
            ++cursor;
        }
    };
    skipSpace();
    if(cursor >= message.size() || message[cursor] != '{') {
        reason = "chunk is not a JSON object";
        return false;
    }
    ++cursor;

    bool sawId = false;
    bool sawIndex = false;
    bool sawTotal = false;
    bool sawPart = false;
    skipSpace();
    if(cursor < message.size() && message[cursor] == '}') {
        ++cursor;
    } else {
        for(std::size_t field = 0; ; ++field) {
            if(field >= 16) {
                reason = "chunk has too many fields";
                return false;
            }
            skipSpace();
            std::string key;
            if(!decodeJsonString(message, cursor, Limits::kMaxGroupIdChars, key)) {
                reason = "chunk key is not a bounded string";
                return false;
            }
            skipSpace();
            if(cursor >= message.size() || message[cursor] != ':') {
                reason = "chunk field has no value";
                return false;
            }
            ++cursor;
            skipSpace();

            if(key == "id" || key == "part") {
                const bool isPart = key == "part";
                if((isPart && sawPart) || (!isPart && sawId)) {
                    reason = "chunk repeats a field";
                    return false;
                }
                std::string value;
                if(!decodeJsonString(message, cursor,
                                     isPart ? Limits::kMaxPacketSize : Limits::kMaxGroupIdChars,
                                     value)) {
                    reason = "chunk string field is malformed or too long";
                    return false;
                }
                if(isPart) {
                    out.part = value;
                    sawPart  = true;
                } else {
                    out.id = value;
                    sawId  = true;
                }
            } else if(key == "i" || key == "n") {
                const bool isTotal = key == "n";
                if((isTotal && sawTotal) || (!isTotal && sawIndex)) {
                    reason = "chunk repeats a field";
                    return false;
                }
                const std::size_t numberStart = cursor;
                while(cursor < message.size() && message[cursor] >= '0' && message[cursor] <= '9') {
                    ++cursor;
                }
                std::uint64_t value = 0;
                if(!parseBoundedDecimal(message.substr(numberStart, cursor - numberStart),
                                        Limits::kMaxFragments, value)) {
                    reason = "chunk index or count is out of range";
                    return false;
                }
                if(isTotal) {
                    out.total = static_cast<std::uint32_t>(value);
                    sawTotal  = true;
                } else {
                    out.index = static_cast<std::uint32_t>(value);
                    sawIndex  = true;
                }
            } else {
                // Unknown key: skip exactly one scalar value, never a nested structure.
                if(cursor < message.size() && message[cursor] == '"') {
                    std::string ignored;
                    if(!decodeJsonString(message, cursor, Limits::kMaxPacketSize, ignored)) {
                        reason = "chunk has an unreadable unknown field";
                        return false;
                    }
                } else {
                    const std::size_t scalarStart = cursor;
                    while(cursor < message.size() && message[cursor] != ',' && message[cursor] != '}') {
                        if(message[cursor] == '{' || message[cursor] == '[' || message[cursor] == '"') {
                            reason = "chunk has a structured unknown field";
                            return false;
                        }
                        ++cursor;
                    }
                    if(cursor == scalarStart) {
                        reason = "chunk has an empty unknown field";
                        return false;
                    }
                }
            }

            skipSpace();
            if(cursor < message.size() && message[cursor] == ',') {
                ++cursor;
                continue;
            }
            if(cursor < message.size() && message[cursor] == '}') {
                ++cursor;
                break;
            }
            reason = "chunk object is malformed";
            return false;
        }
    }
    skipSpace();
    if(cursor != message.size()) {
        reason = "trailing data after chunk";
        return false;
    }

    if(!sawId || !sawIndex || !sawTotal || !sawPart) {
        reason = "chunk is missing a required field";
        return false;
    }
    if(out.id.empty() || out.id.size() > Limits::kMaxGroupIdChars) {
        reason = "chunk group id is not bounded";
        return false;
    }
    for(const char c : out.id) {
        const bool acceptable = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                             || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if(!acceptable) {
            reason = "chunk group id has an unacceptable character";
            return false;
        }
    }
    if(out.total == 0 || out.total > Limits::kMaxFragments) {
        reason = "chunk count is out of range";
        return false;
    }
    if(out.index >= out.total) {
        reason = "chunk index is outside its group";
        return false;
    }
    return true;
}

/**
    Reassembles fragments from one peer.

    Bounded in every direction a remote peer controls: how many groups it may open, how much it
    may hold unfinished, how many fragments a group may claim, how large the result may be and
    how long an incomplete group may live.

    What it deliberately does not do is recover. A malformed fragment, a contradictory one, an
    exhausted bound or a group that ran out of time is Outcome::Fatal, and the caller closes that
    peer's connection with an explicit reason. Dropping the offending message and carrying on is
    exactly how a lockstep match applies a prefix of a command stream and desynchronises quietly;
    the relay transport already refuses to do that, and so does this one.
*/
class ChunkAssembler {
public:
    enum class Outcome {
        Pending,    ///< understood, but the payload is not complete yet
        Complete,   ///< `out` holds the reassembled JSON value text
        Duplicate,  ///< an exact repeat of a fragment already held; ignored, as upstream does
        Fatal       ///< this peer cannot be trusted to be in step; close the connection
    };

    /**
        Feeds one data-channel message.
        \param  nowMs   a monotonic millisecond clock, used for group expiry
        \param  reason  set on Outcome::Fatal, for the log and for the player-facing sentence
    */
    Outcome ingest(const std::string& message, std::uint32_t nowMs, std::string& out,
                   const char*& reason) {
        reason = nullptr;
        if(!expire(nowMs, reason)) {
            return Outcome::Fatal;
        }

        ChunkPacket packet;
        if(!parseChunkPacket(message, packet, reason)) {
            return Outcome::Fatal;
        }
        if(packet.part.size() > Limits::kMaxPacketSize) {
            reason = "fragment is larger than the agreed fragment size";
            return Outcome::Fatal;
        }
        if(packet.total == 1) {
            if(packet.index != 0) {
                reason = "single-fragment group has a non-zero index";
                return Outcome::Fatal;
            }
            out = packet.part;
            return Outcome::Complete;
        }

        auto entry = groups_.find(packet.id);
        if(entry == groups_.end()) {
            if(groups_.size() >= Limits::kMaxOpenGroups) {
                reason = "too many payloads are in flight at once";
                return Outcome::Fatal;
            }
            if(static_cast<std::size_t>(packet.total) * Limits::kMaxPacketSize
               > Limits::kMaxAssembledBytes) {
                reason = "group claims more than the payload bound";
                return Outcome::Fatal;
            }
            Group group;
            group.parts.resize(packet.total);
            group.present.assign(packet.total, false);
            group.startedMs = nowMs;
            entry = groups_.emplace(packet.id, std::move(group)).first;
        }
        Group& group = entry->second;
        if(group.parts.size() != packet.total) {
            reason = "group changed its fragment count";
            return Outcome::Fatal;
        }
        if(group.present[packet.index]) {
            if(group.parts[packet.index] != packet.part) {
                reason = "group repeated a fragment with different contents";
                return Outcome::Fatal;
            }
            return Outcome::Duplicate;
        }
        if(group.bytes + packet.part.size() > Limits::kMaxAssembledBytes
           || pendingBytes_ + packet.part.size() > Limits::kMaxPendingBytes) {
            reason = "payload exceeded the size this game will reassemble";
            return Outcome::Fatal;
        }
        group.bytes  += packet.part.size();
        pendingBytes_ += packet.part.size();
        group.parts[packet.index] = packet.part;
        group.present[packet.index] = true;
        ++group.received;
        if(group.received < packet.total) {
            return Outcome::Pending;
        }

        out.clear();
        out.reserve(group.bytes);
        for(const std::string& part : group.parts) {
            out += part;
        }
        pendingBytes_ -= group.bytes;
        groups_.erase(entry);
        return Outcome::Complete;
    }

    /**
        Times out groups that never completed.
        \return false when one did, which is fatal to that peer's connection
    */
    bool expire(std::uint32_t nowMs, const char*& reason) {
        for(const auto& entry : groups_) {
            // Subtraction on an unsigned clock, so a wrapped clock still measures an interval.
            if(nowMs - entry.second.startedMs > Limits::kGroupLifetimeMs) {
                reason = "a payload never finished arriving";
                return false;
            }
        }
        return true;
    }

    void reset() {
        groups_.clear();
        pendingBytes_ = 0;
    }

    std::size_t openGroupCount() const { return groups_.size(); }
    std::size_t pendingBytes() const { return pendingBytes_; }

private:
    struct Group {
        std::vector<std::string> parts;
        std::vector<bool>        present;
        std::size_t              received  = 0;
        std::size_t              bytes     = 0;
        std::uint32_t            startedMs = 0;
    };

    std::map<std::string, Group> groups_;
    std::size_t                  pendingBytes_ = 0;
};

} // namespace P2PWire

#endif // P2PWIREFRAMING_H
