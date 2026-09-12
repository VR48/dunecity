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

#ifndef RELAYPOLLPROTOCOL_H
#define RELAYPOLLPROTOCOL_H

/**
    The batching wire format that carries relay frames over plain HTTPS requests.

    This exists because the public WebSocket endpoint could not be deployed: the same relay
    frames (docs/room-relay-protocol.md §4) are instead posted to an Apache/PHP gateway in
    length-prefixed batches, and the answer carries whatever the relay had queued for this peer.
    Nothing about the frames themselves changes - this is an envelope around them, not a second
    gameplay protocol.

    Three endpoints hang off the poll URL the admission response hands out:

      POST <socketUrl>/open       empty body        -> 64 lowercase hex characters and one LF
      POST <socketUrl>/exchange   a `DHP1` batch    -> a `DHR1` batch
      POST <socketUrl>/close      empty body        -> best effort, answer ignored

    The session token from /open travels in the `X-Dune-Session` header and nowhere else. It is
    never put in a URL, because URLs reach proxy logs, browser history and referrers; and it is
    never logged, because a log line is a credential leak with a timestamp.

    Two rules hold everywhere in this file, exactly as in RoomRelayProtocol.h:

      - every length is checked against the bytes that are actually present, by subtraction;
        `position + length` wraps on wasm32 and must never appear,
      - nothing is allocated or copied before its length has been checked.

    Byte order here is **little endian**, unlike the big-endian relay envelope. That is
    deliberate and matches the Node/PHP side of the contract; the two orders never meet, because
    this header only ever reads the batch envelope and never looks inside a frame.
*/

#include <Network/RoomRelayProtocol.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoomPoll {

// `inline` so both arrays are one entity across the whole program: a namespace-scope constexpr
// array is const, and const at namespace scope is internal linkage, which the inline functions
// below would then be referring to a different copy of in every translation unit.

/// ASCII "DHP1": client -> gateway batch.
inline constexpr char kRequestMagic[4] = {'D', 'H', 'P', '1'};
/// ASCII "DHR1": gateway -> client batch.
inline constexpr char kResponseMagic[4] = {'D', 'H', 'R', '1'};

namespace Limits {

/// The /open answer is exactly this: 64 lowercase hex characters and one LF.
constexpr std::size_t kSessionTokenChars = 64;
constexpr std::size_t kOpenResponseBytes = kSessionTokenChars + 1;

/// magic(4) + sequence(4) + frameCount(4)
constexpr std::size_t kRequestHeaderBytes = 12;
/// magic(4) + sequence(4) + closeCode(2) + frameCount(2)
constexpr std::size_t kResponseHeaderBytes = 12;
/// Every frame is preceded by its little-endian uint32 length.
constexpr std::size_t kFrameLengthBytes = 4;

constexpr std::size_t kMaxFramesPerBatch = 64;
/// One frame may not be larger than the relay's own frame ceiling.
constexpr std::size_t kMaxFrameBytes = RoomRelay::Limits::kMaxFrameBytes;   // 262144
constexpr std::size_t kMaxRequestBytes  = 1048576;
constexpr std::size_t kMaxResponseBytes = 1048576;

} // namespace Limits

namespace Timing {

/// No new exchange may start sooner than this after the previous one started: 40 requests per
/// second per peer, which is the rate the gateway is sized for.
constexpr std::uint32_t kMinExchangeIntervalMs = 25;
/// How long the server holds an otherwise empty exchange before answering. Informational on the
/// client; it is the reason an idle session costs ten requests a second rather than forty.
constexpr std::uint32_t kServerHoldMs = 100;
/// One request may take this long before it counts as a network failure.
constexpr std::uint32_t kRequestTimeoutMs = 5000;
/// Delay before the first and the second retry of a byte-identical request.
constexpr std::uint32_t kFirstRetryDelayMs  = 100;
constexpr std::uint32_t kSecondRetryDelayMs = 250;
/// After this many retries of the same request the session is lost.
constexpr unsigned kMaxRetries = 2;

} // namespace Timing

/// Why a batch could not be understood. Reported to the log, never to the player: the transport
/// turns every one of these into the same sentence, because the difference is not the player's
/// problem and the detail is a hint to whoever sent the malformed batch.
enum class BatchError {
    None,
    Truncated,          ///< fewer bytes than the declared structure needs
    BadMagic,
    SequenceMismatch,
    BadCloseCode,
    TooManyFrames,
    EmptyFrame,
    FrameTooLarge,
    TooLarge,           ///< the batch as a whole is over the ceiling
    TrailingBytes       ///< well-formed batch followed by bytes nobody asked for
};

inline const char* describeBatchError(BatchError error) {
    switch(error) {
        case BatchError::None:             return "no error";
        case BatchError::Truncated:        return "truncated batch";
        case BatchError::BadMagic:         return "wrong batch magic";
        case BatchError::SequenceMismatch: return "batch answered a different request";
        case BatchError::BadCloseCode:     return "invalid close code";
        case BatchError::TooManyFrames:    return "too many frames in one batch";
        case BatchError::EmptyFrame:       return "zero-length frame";
        case BatchError::FrameTooLarge:    return "frame above the size ceiling";
        case BatchError::TooLarge:         return "batch above the size ceiling";
        case BatchError::TrailingBytes:    return "trailing bytes after the batch";
    }
    return "unusable batch";
}

// ---------------------------------------------------------------------------------------------
// Little-endian primitives
// ---------------------------------------------------------------------------------------------

inline void appendUint16LE(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

inline void appendUint32LE(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

/// Reads without any bounds check of its own; every caller in this header has already proved
/// that four bytes are present by subtraction.
inline std::uint32_t readUint32LE(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
         | (static_cast<std::uint32_t>(data[1]) << 8)
         | (static_cast<std::uint32_t>(data[2]) << 16)
         | (static_cast<std::uint32_t>(data[3]) << 24);
}

inline std::uint16_t readUint16LE(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0])
         | static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8));
}

// ---------------------------------------------------------------------------------------------
// Session tokens
// ---------------------------------------------------------------------------------------------

/// A session token is 64 lowercase hex characters and nothing else.
inline bool isAcceptableSessionToken(const std::string& token) {
    return token.size() == Limits::kSessionTokenChars && RoomRelay::isLowercaseHex(token);
}

/**
    Reads the /open answer.

    The shape is fixed to the byte: 64 lowercase hex characters and one LF, no CR, no trailing
    whitespace, nothing else. An answer that is merely close to that is not a session, it is
    something else answering on that URL - a captive portal, a cached error page, a proxy - and
    starting a match on top of it would only fail later and less clearly.

    \param  data    the exact response body
    \param  length  its length
    \param  token   set to the 64-character token on success
    \return true if the body was exactly one session token
*/
inline bool parseOpenResponse(const std::uint8_t* data, std::size_t length, std::string& token) {
    token.clear();
    if(data == nullptr || length != Limits::kOpenResponseBytes) {
        return false;
    }
    if(data[Limits::kSessionTokenChars] != static_cast<std::uint8_t>('\n')) {
        return false;
    }
    std::string candidate(reinterpret_cast<const char*>(data), Limits::kSessionTokenChars);
    if(!isAcceptableSessionToken(candidate)) {
        return false;
    }
    token = candidate;
    return true;
}

/**
    Which close codes a batch may carry.

    0 means the session is still open. Anything else has to be a code the relay is allowed to
    end a session with: 1000, or the private range the relay's own close codes live in. 1006 and
    friends are refused on purpose - they describe how a WebSocket died, and no server may claim
    one.
*/
inline bool isAcceptableCloseCode(std::uint16_t code) {
    return code == 0 || code == RoomRelay::Close::Normal || (code >= 3000 && code <= 4999);
}

// ---------------------------------------------------------------------------------------------
// Batches
// ---------------------------------------------------------------------------------------------

/// A parsed gateway answer. `frames` is only populated when the whole batch validated.
struct ExchangeResponse {
    std::uint32_t sequence  = 0;
    std::uint16_t closeCode = 0;    ///< 0 while the session is open
    std::vector<std::vector<std::uint8_t>> frames;
};

/// The size a batch of these frames would encode to, or 0 if it would not fit.
inline std::size_t exchangeRequestSize(const std::vector<std::vector<std::uint8_t>>& frames) {
    if(frames.size() > Limits::kMaxFramesPerBatch) {
        return 0;
    }
    std::size_t total = Limits::kRequestHeaderBytes;
    for(const auto& frame : frames) {
        if(frame.empty() || frame.size() > Limits::kMaxFrameBytes) {
            return 0;
        }
        // Subtraction form: `total + cost` would wrap on wasm32 for a crafted frame list.
        const std::size_t cost = Limits::kFrameLengthBytes + frame.size();
        if(total > Limits::kMaxRequestBytes - cost) {
            return 0;
        }
        total += cost;
    }
    return total;
}

/**
    Encodes one client -> gateway batch.

    \param  sequence    the request sequence number; the answer must echo it
    \param  frames      the relay frames to carry, in order; may be empty
    \param  out         filled in on success, untouched on failure
    \return false if the batch would break a limit, which is a programming error here because
            the caller builds the batch against the same limits
*/
inline bool encodeExchangeRequest(std::uint32_t sequence,
                                  const std::vector<std::vector<std::uint8_t>>& frames,
                                  std::vector<std::uint8_t>& out) {
    const std::size_t size = exchangeRequestSize(frames);
    if(size == 0) {
        return false;
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(size);
    encoded.insert(encoded.end(), kRequestMagic, kRequestMagic + 4);
    appendUint32LE(encoded, sequence);
    appendUint32LE(encoded, static_cast<std::uint32_t>(frames.size()));
    for(const auto& frame : frames) {
        appendUint32LE(encoded, static_cast<std::uint32_t>(frame.size()));
        encoded.insert(encoded.end(), frame.begin(), frame.end());
    }

    out = std::move(encoded);
    return true;
}

/**
    Reads one gateway -> client batch.

    The whole batch is validated before a single frame is handed back: `out` stays empty unless
    every length, the count, the magic, the sequence and the close code were all acceptable and
    the last frame ended exactly at the last byte. Half a batch is not a smaller batch - if the
    answer cannot be trusted in full it cannot be trusted at all, and delivering a prefix of it
    into a lockstep match is how a desynchronisation starts.

    \param  data                the exact response body
    \param  length              its length
    \param  expectedSequence    the sequence of the request this is supposed to answer
    \param  out                 filled in only on success
    \param  error               why the batch was refused
    \return true if the batch was well formed
*/
inline bool parseExchangeResponse(const std::uint8_t* data, std::size_t length,
                                  std::uint32_t expectedSequence, ExchangeResponse& out,
                                  BatchError& error) {
    out = ExchangeResponse();
    error = BatchError::None;

    if(data == nullptr || length < Limits::kResponseHeaderBytes) {
        error = BatchError::Truncated;
        return false;
    }
    if(length > Limits::kMaxResponseBytes) {
        error = BatchError::TooLarge;
        return false;
    }
    for(std::size_t index = 0; index < 4; ++index) {
        if(data[index] != static_cast<std::uint8_t>(kResponseMagic[index])) {
            error = BatchError::BadMagic;
            return false;
        }
    }

    const std::uint32_t sequence = readUint32LE(data + 4);
    if(sequence != expectedSequence) {
        error = BatchError::SequenceMismatch;
        return false;
    }

    const std::uint16_t closeCode  = readUint16LE(data + 8);
    const std::uint16_t frameCount = readUint16LE(data + 10);
    if(!isAcceptableCloseCode(closeCode)) {
        error = BatchError::BadCloseCode;
        return false;
    }
    if(frameCount > Limits::kMaxFramesPerBatch) {
        error = BatchError::TooManyFrames;
        return false;
    }

    // First pass: walk the whole batch and check it, copying nothing. `position` never passes
    // `length`, so every remaining-byte question is a subtraction.
    std::size_t position = Limits::kResponseHeaderBytes;
    for(std::uint16_t index = 0; index < frameCount; ++index) {
        if((length - position) < Limits::kFrameLengthBytes) {
            error = BatchError::Truncated;
            return false;
        }
        const std::uint32_t frameLength = readUint32LE(data + position);
        position += Limits::kFrameLengthBytes;

        if(frameLength == 0) {
            error = BatchError::EmptyFrame;
            return false;
        }
        if(frameLength > Limits::kMaxFrameBytes) {
            error = BatchError::FrameTooLarge;
            return false;
        }
        // frameLength is now known to be at most 262144, so it fits a 32-bit size_t.
        const std::size_t frameBytes = static_cast<std::size_t>(frameLength);
        if((length - position) < frameBytes) {
            error = BatchError::Truncated;
            return false;
        }
        position += frameBytes;
    }
    if(position != length) {
        error = BatchError::TrailingBytes;
        return false;
    }

    // Second pass: the batch is known good, so copying it out cannot half-succeed.
    ExchangeResponse parsed;
    parsed.sequence  = sequence;
    parsed.closeCode = closeCode;
    parsed.frames.reserve(frameCount);
    position = Limits::kResponseHeaderBytes;
    for(std::uint16_t index = 0; index < frameCount; ++index) {
        const std::size_t frameBytes = static_cast<std::size_t>(readUint32LE(data + position));
        position += Limits::kFrameLengthBytes;
        parsed.frames.emplace_back(data + position, data + position + frameBytes);
        position += frameBytes;
    }

    out = std::move(parsed);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Endpoints
// ---------------------------------------------------------------------------------------------

/**
    Builds one poll endpoint from the base URL the admission response gave us.

    \param  baseUrl     the socket URL, with or without a trailing slash
    \param  suffix      "/open", "/exchange" or "/close"

    The base URL has already been through isAcceptableRelayUrl(), which refuses a query string
    or a fragment for exactly this reason: appending a path to `...?x=1` would produce a URL
    nobody validated.
*/
inline std::string pollEndpointUrl(const std::string& baseUrl, const char* suffix) {
    std::string url = baseUrl;
    while(!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += suffix;
    return url;
}

} // namespace RoomPoll

#endif // RELAYPOLLPROTOCOL_H
