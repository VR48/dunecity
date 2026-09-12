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

#ifndef ROOMADMISSIONCLIENT_H
#define ROOMADMISSIONCLIENT_H

/**
    Asks the relay's HTTPS admission endpoint for a room and a single-use grant.

    This is a separate, bounded operation from the gameplay socket on purpose: the room policy
    and the invitation check happen before a socket exists, and the credential never appears in
    a WebSocket URL where it would end up in proxy logs and browser history.

    The client never blocks. Native builds drive libcurl on a multi handle from the game loop;
    browser builds use the asynchronous Fetch API. Neither waits inside a simulation tick.

    The response format is the small key=value text in docs/room-relay-protocol.md §3.2, chosen
    so this can be parsed with the strict, bounded parser below instead of adding a JSON parser
    to the game.
*/

#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/// What the relay answered, after validation.
struct AdmissionResponse {
    bool          ok             = false;
    std::uint16_t protocol       = 0;
    std::string   roomCode;
    std::string   grant;
    std::string   socketUrl;
    std::uint32_t grantExpiresMs = 0;
    std::uint8_t  maxPeers       = 0;

    // Only when ok == false.
    std::string   errorCode;
    std::string   errorMessage;
};

namespace RoomAdmission {

constexpr std::size_t kMaxResponseBytes  = 8192;
constexpr std::size_t kMaxResponseLines  = 16;
constexpr std::size_t kMaxLineBytes      = 512;
constexpr std::size_t kMaxKeyBytes       = 32;
constexpr std::size_t kMaxValueBytes     = 480;

/**
    Parses the admission response.

    Everything is bounded before it is believed: the whole body, the number of lines, each line,
    each key and each value. Unknown keys are skipped so the relay can add fields later, but a
    malformed line is a hard failure - a half-understood admission answer is not something to
    guess about.

    \param  body    the exact response body
    \param  out     filled in on success
    \param  error   a player-facing reason on failure
    \return true if the response could be understood (including a well-formed error response)
*/
inline bool parseAdmissionResponse(const std::string& body, AdmissionResponse& out,
                                   std::string& error) {
    out = AdmissionResponse();

    if(body.empty() || body.size() > kMaxResponseBytes) {
        error = "The game service sent an unusable answer.";
        return false;
    }

    bool sawStatus = false;
    std::size_t lineCount = 0;
    std::size_t cursor = 0;

    while(cursor < body.size()) {
        std::size_t lineEnd = body.find('\n', cursor);
        if(lineEnd == std::string::npos) {
            lineEnd = body.size();
        }
        // Subtraction form throughout; cursor never passes lineEnd.
        std::size_t lineLength = lineEnd - cursor;
        if(lineLength > 0 && body[cursor + lineLength - 1] == '\r') {
            lineLength--;
        }

        if(lineLength > 0) {
            if(++lineCount > kMaxResponseLines) {
                error = "The game service sent an unusable answer.";
                return false;
            }
            if(lineLength > kMaxLineBytes) {
                error = "The game service sent an unusable answer.";
                return false;
            }

            const std::string line = body.substr(cursor, lineLength);
            const std::size_t equals = line.find('=');
            if(equals == std::string::npos || equals == 0 || equals > kMaxKeyBytes) {
                error = "The game service sent an unusable answer.";
                return false;
            }

            const std::string key   = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if(value.size() > kMaxValueBytes) {
                error = "The game service sent an unusable answer.";
                return false;
            }
            for(const char c : key) {
                const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                if(!letter) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
            }
            for(const char rawChar : value) {
                const unsigned char c = static_cast<unsigned char>(rawChar);
                if(c < 32 || c > 126) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
            }

            if(key == "status") {
                sawStatus = true;
                out.ok = (value == "ok");
                if(!out.ok && value != "error") {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
            } else if(key == "protocol") {
                unsigned long parsed = 0;
                if(value.empty() || value.size() > 5) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
                for(const char c : value) {
                    if(c < '0' || c > '9') {
                        error = "The game service sent an unusable answer.";
                        return false;
                    }
                    parsed = parsed * 10 + static_cast<unsigned long>(c - '0');
                }
                if(parsed > 65535) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
                out.protocol = static_cast<std::uint16_t>(parsed);
            } else if(key == "room") {
                out.roomCode = value;
            } else if(key == "grant") {
                out.grant = value;
            } else if(key == "url") {
                out.socketUrl = value;
            } else if(key == "maxPeers") {
                unsigned long parsed = 0;
                if(value.empty() || value.size() > 2) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
                for(const char c : value) {
                    if(c < '0' || c > '9') {
                        error = "The game service sent an unusable answer.";
                        return false;
                    }
                    parsed = parsed * 10 + static_cast<unsigned long>(c - '0');
                }
                out.maxPeers = static_cast<std::uint8_t>(parsed);
            } else if(key == "grantExpiresMs") {
                unsigned long parsed = 0;
                if(value.empty() || value.size() > 7) {
                    error = "The game service sent an unusable answer.";
                    return false;
                }
                for(const char c : value) {
                    if(c < '0' || c > '9') {
                        error = "The game service sent an unusable answer.";
                        return false;
                    }
                    parsed = parsed * 10 + static_cast<unsigned long>(c - '0');
                }
                out.grantExpiresMs = static_cast<std::uint32_t>(parsed);
            } else if(key == "code") {
                out.errorCode = value;
            } else if(key == "message") {
                out.errorMessage = RoomRelay::sanitizeRelayMessage(value);
            }
            // Any other key is ignored on purpose so the relay can grow new fields.
        }

        cursor = (lineEnd >= body.size()) ? body.size() : lineEnd + 1;
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

    if(out.protocol != RoomRelay::kProtocolVersion) {
        error = "This version of the game cannot use that game service.";
        return false;
    }
    if(!RoomRelay::isAcceptableRoomCode(out.roomCode)) {
        error = "The game service sent an unusable room code.";
        return false;
    }
    if(!RoomRelay::isAcceptableGrant(out.grant) || out.grant.empty()) {
        error = "The game service sent an unusable invitation.";
        return false;
    }
    if(out.maxPeers < 2 || out.maxPeers > RoomRelay::Limits::kMaxPeersPerRoom) {
        error = "The game service sent an unusable room size.";
        return false;
    }
    if(out.socketUrl.empty()) {
        error = "The game service did not say where to connect.";
        return false;
    }

    return true;
}

/// Percent-encodes one form value. Only unreserved characters survive unescaped.
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

} // namespace RoomAdmission

/// What the game wants from the relay.
struct AdmissionRequest {
    /// Base URL without a trailing slash, e.g. "https://relay.example.net".
    std::string baseUrl;
    /// True only when the player explicitly chose the development endpoint on this computer.
    bool        allowLoopbackPlaintext = false;

    std::string appVersion;
    std::uint16_t gameProtocol = 0;
    std::string contentHash;
    std::string runtime;        ///< "native" or "browser"; a claim, and logged as one

    bool        hosting  = true;
    std::uint8_t maxPeers = 2;
    std::string mode;           ///< "coop" or "custom"
    std::string roomCode;       ///< joining only
};

class RoomAdmissionClient {
public:
    enum class Status { Idle, InProgress, Succeeded, Failed };

    RoomAdmissionClient();
    RoomAdmissionClient(const RoomAdmissionClient&) = delete;
    RoomAdmissionClient& operator=(const RoomAdmissionClient&) = delete;
    ~RoomAdmissionClient();

    /// Starts a request. Any request already running is abandoned.
    void begin(const AdmissionRequest& request);

    /// Drives the request. Must be called from the game loop; never blocks.
    void update();

    /// Forgets any request in flight.
    void cancel();

    Status status() const { return status_; }
    const AdmissionResponse& response() const { return response_; }
    /// A player-facing sentence, already free of implementation jargon.
    const std::string& errorMessage() const { return errorMessage_; }

private:
    class Impl;

    void finishWithBody(long httpStatus, const std::string& body);
    void finishWithError(const std::string& message);

    std::unique_ptr<Impl> impl_;
    Status                status_ = Status::Idle;
    AdmissionResponse     response_;
    std::string           errorMessage_;
};

#endif // ROOMADMISSIONCLIENT_H
