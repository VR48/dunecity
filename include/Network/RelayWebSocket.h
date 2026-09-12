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

#ifndef RELAYWEBSOCKET_H
#define RELAYWEBSOCKET_H

/**
    The one thing the relay client needs from a platform: a non-blocking WebSocket that delivers
    whole binary messages.

    Two implementations exist. The native one uses libcurl's WebSocket API on a CONNECT_ONLY
    multi handle (src/Network/RelayWebSocketCurl.cpp); the browser one uses the Emscripten
    WebSocket API (src/Network/RelayWebSocketEmscripten.cpp). Neither ever blocks, and neither
    ever calls back into the game: messages are queued and drained by the game loop.
*/

#include <Network/RoomRelayProtocol.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RelayWebSocket {
public:
    enum class State {
        Connecting, ///< TCP/TLS/HTTP upgrade in progress
        Open,       ///< messages may be sent and received
        Closed      ///< finished, for any reason; see closeCode() and lastError()
    };

    virtual ~RelayWebSocket() = default;

    /// Drives the socket. Must be called from the game loop and must never block.
    virtual void pump() = 0;

    virtual State state() const = 0;

    /**
        Queues one complete binary message.
        \return false if the message was refused (too large, or the outgoing queue is full); the
                session is then closed, because dropping a lockstep message silently is worse.
    */
    virtual bool send(const std::vector<std::uint8_t>& frame) = 0;

    /**
        Takes the oldest complete received message.
        \param  frame   filled in on success
        \return false if nothing is ready
    */
    virtual bool receive(std::vector<std::uint8_t>& frame) = 0;

    /// Starts a clean close. The socket reaches Closed after a later pump().
    virtual void close(std::uint16_t code, const std::string& reason) = 0;

    /// The close code observed, or 0 if the session has not ended.
    virtual std::uint16_t closeCode() const = 0;

    /// A short, already sanitised description of the last failure; empty if there was none.
    virtual const std::string& lastError() const = 0;

    /// Bytes queued for sending that the transport has not accepted yet.
    virtual std::size_t outgoingBacklogBytes() const = 0;
};

/// Whether this build and this machine can open a relay connection at all.
struct RelayWebSocketSupport {
    bool        available = false;
    std::string reason;         ///< player-facing explanation when unavailable
};

/**
    Runtime capability check. On native builds this asks libcurl whether it actually carries the
    ws/wss protocol handlers: the macOS system libcurl does not, and the game must say so plainly
    rather than failing later with a confusing error.
*/
RelayWebSocketSupport relayWebSocketSupport();

/**
    Creates a socket and starts connecting. Never blocks.
    \param  url     a wss:// URL, or a ws:// loopback URL when the development endpoint is in use
    \param  origin  Origin header to send, or empty for none (native clients send none)
    \return the socket, or nullptr if the URL was refused or the platform cannot do this
*/
std::unique_ptr<RelayWebSocket> createRelayWebSocket(const std::string& url,
                                                     const std::string& origin);

/**
    Validates a relay endpoint before any network machinery sees it.

    wss:// is always acceptable. Plain ws:// is acceptable only for loopback hosts and only when
    the caller explicitly opted into a development endpoint - that is the one case where there is
    no network to eavesdrop on. Credentials in the URL, control characters, non-numeric or
    out-of-range ports and anything that is not ws/wss are refused outright.

    \param  url                     the endpoint
    \param  allowLoopbackPlaintext  true when the development endpoint was explicitly chosen
    \param  error                   set to a player-facing reason on failure
    \return true if the URL may be used
*/
inline bool isAcceptableRelayUrl(const std::string& url, bool allowLoopbackPlaintext,
                                 std::string& error) {
    if(url.empty() || url.size() > 512) {
        error = "The game service address is missing or too long.";
        return false;
    }
    for(const char rawChar : url) {
        const unsigned char c = static_cast<unsigned char>(rawChar);
        if(c <= 32 || c >= 127) {
            error = "The game service address contains invalid characters.";
            return false;
        }
    }

    bool secure = false;
    std::size_t cursor = 0;
    if(url.compare(0, 6, "wss://") == 0) {
        secure = true;
        cursor = 6;
    } else if(url.compare(0, 5, "ws://") == 0) {
        secure = false;
        cursor = 5;
    } else {
        error = "The game service address must start with wss://.";
        return false;
    }

    const std::size_t pathStart = url.find('/', cursor);
    const std::string authority = (pathStart == std::string::npos)
        ? url.substr(cursor) : url.substr(cursor, pathStart - cursor);
    if(authority.empty()) {
        error = "The game service address has no host.";
        return false;
    }
    if(authority.find('@') != std::string::npos) {
        error = "The game service address must not contain a user name or password.";
        return false;
    }
    if(authority.find('?') != std::string::npos || authority.find('#') != std::string::npos) {
        error = "The game service address is not valid.";
        return false;
    }

    std::string host = authority;
    const std::size_t bracketEnd = authority.rfind(']');
    const std::size_t colon = (bracketEnd == std::string::npos)
        ? authority.rfind(':') : authority.find(':', bracketEnd);
    if(colon != std::string::npos) {
        host = authority.substr(0, colon);
        const std::string port = authority.substr(colon + 1);
        if(port.empty() || port.size() > 5) {
            error = "The game service address has an invalid port.";
            return false;
        }
        unsigned long value = 0;
        for(const char c : port) {
            if(c < '0' || c > '9') {
                error = "The game service address has an invalid port.";
                return false;
            }
            value = value * 10 + static_cast<unsigned long>(c - '0');
        }
        if(value == 0 || value > 65535) {
            error = "The game service address has an invalid port.";
            return false;
        }
    }
    if(host.empty()) {
        error = "The game service address has no host.";
        return false;
    }

    if(!secure) {
        const bool loopback = (host == "127.0.0.1") || (host == "localhost")
                           || (host == "[::1]") || (host == "::1");
        if(!allowLoopbackPlaintext || !loopback) {
            error = "An unencrypted game service address is only allowed on this computer.";
            return false;
        }
    }

    return true;
}

#endif // RELAYWEBSOCKET_H
