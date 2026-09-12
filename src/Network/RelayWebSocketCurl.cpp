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

#ifndef __EMSCRIPTEN__

/**
    Native relay transport on libcurl's WebSocket API.

    Shape of the thing, because it is not obvious from the outside:

      - CURLOPT_CONNECT_ONLY is set to 2, which is what turns an easy handle into a WebSocket
        that curl_ws_send()/curl_ws_recv() can drive instead of curl doing the transfer itself.
      - The easy handle lives on a multi handle so the connect and TLS handshake are non-blocking:
        pump() calls curl_multi_perform()/curl_multi_poll() with a zero timeout from the game
        loop. The handle stays added to the multi handle for the whole life of the socket, which
        libcurl requires while the connection is in use.
      - Receiving reassembles fragmented and chunked frames. curl_ws_recv() hands back at most
        one buffer's worth at a time and reports how much of the current frame is still to come;
        a message is complete when the current frame is not a continuation and has no bytes left.
      - Sending honours partial writes: curl_ws_send() may accept only part of a message, and the
        rest has to be offered again as a continuation of the same frame.

    References:
      https://curl.se/libcurl/c/libcurl-ws.html
      https://curl.se/libcurl/c/curl_ws_recv.html
      https://curl.se/libcurl/c/curl_ws_send.html
      https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html
*/

#include <Network/RelayWebSocket.h>

#include <misc/FileSystem.h>
#include <misc/SDL2pp.h>

#include <curl/curl.h>
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 86, 0)
#  define DUNECITY_HAVE_CURL_WEBSOCKET 1
#  include <curl/websockets.h>
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

#ifdef DUNECITY_HAVE_CURL_WEBSOCKET

/// Longest we wait for the TCP/TLS/upgrade handshake before giving up.
constexpr Uint32 kConnectTimeoutMs = 15000;
/// One read buffer. curl hands back at most this much per curl_ws_recv() call.
constexpr std::size_t kReadChunkBytes = 16 * 1024;

/**
    Asks the libcurl that is actually loaded whether it carries the WebSocket protocol handlers.
    The macOS system libcurl is built without them, so the compile-time check is not enough.
*/
bool curlCarriesWebSocket(bool& sawSecure) {
    sawSecure = false;
    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if(info == nullptr || info->protocols == nullptr) {
        return false;
    }

    bool sawPlain = false;
    for(const char* const* protocol = info->protocols; *protocol != nullptr; ++protocol) {
        if(std::strcmp(*protocol, "wss") == 0) {
            sawSecure = true;
        } else if(std::strcmp(*protocol, "ws") == 0) {
            sawPlain = true;
        }
    }
    return sawSecure || sawPlain;
}

/// Mirrors ENetHttp.cpp: prefer the bundled CA bundle when the install ships one.
void configureRelayCertificates(CURL* curl) {
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

    static const std::string certificateBundle = [] {
        const std::filesystem::path dataRoot = std::filesystem::path(getDuneLegacyDataDir());
        const std::filesystem::path candidates[] = {
            dataRoot / "data" / "cacert.pem",
            dataRoot / "cacert.pem",
            dataRoot / ".." / "share" / "DuneCity" / "cacert.pem"
        };
        for(const auto& candidate : candidates) {
            std::error_code error;
            if(std::filesystem::is_regular_file(candidate, error)) {
                return candidate.lexically_normal().string();
            }
        }
        return std::string{};
    }();

    if(!certificateBundle.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificateBundle.c_str());
    }
}

class CurlRelayWebSocket final : public RelayWebSocket {
public:
    CurlRelayWebSocket(const std::string& url, const std::string& origin)
     : deadline_(SDL_GetTicks() + kConnectTimeoutMs) {
        multi_ = curl_multi_init();
        easy_  = curl_easy_init();
        if(multi_ == nullptr || easy_ == nullptr) {
            fail("The game could not start a network connection.");
            return;
        }

        curl_easy_setopt(easy_, CURLOPT_URL, url.c_str());
        // 2 means "WebSocket": curl performs the HTTP upgrade and then leaves the connection to
        // curl_ws_send()/curl_ws_recv().
        curl_easy_setopt(easy_, CURLOPT_CONNECT_ONLY, 2L);

        // Certificates are always verified, chain and hostname, and a redirect is never followed:
        // a relay endpoint that answers with a redirect is not a relay endpoint.
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 0L);
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 85, 0)
        // Only these two schemes, so a URL that slipped past validation still cannot become a
        // file:// or scp:// operation.
        curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "ws,wss");
#endif
        curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(kConnectTimeoutMs));
        curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
        configureRelayCertificates(easy_);

        if(!origin.empty()) {
            const std::string header = "Origin: " + origin;
            headers_ = curl_slist_append(headers_, header.c_str());
        }
        // The relay refuses compression; asking for it would only add an attack surface.
        headers_ = curl_slist_append(headers_, "Sec-WebSocket-Extensions:");
        if(headers_ != nullptr) {
            curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, headers_);
        }

        if(curl_multi_add_handle(multi_, easy_) != CURLM_OK) {
            fail("The game could not start a network connection.");
            return;
        }
        attached_ = true;
    }

    ~CurlRelayWebSocket() override {
        if(attached_ && multi_ != nullptr && easy_ != nullptr) {
            curl_multi_remove_handle(multi_, easy_);
        }
        if(easy_ != nullptr) {
            curl_easy_cleanup(easy_);
        }
        if(multi_ != nullptr) {
            curl_multi_cleanup(multi_);
        }
        if(headers_ != nullptr) {
            curl_slist_free_all(headers_);
        }
    }

    void pump() override {
        if(state_ == State::Closed) {
            return;
        }

        if(state_ == State::Connecting) {
            driveHandshake();
            if(state_ != State::Open) {
                return;
            }
        }

        drainOutgoing();
        drainIncoming();
    }

    State state() const override { return state_; }

    bool send(const std::vector<std::uint8_t>& frame) override {
        if(state_ == State::Closed) {
            return false;
        }
        if(frame.empty() || frame.size() > RoomRelay::Limits::kMaxFrameBytes) {
            fail("The game tried to send a message that was too large.");
            return false;
        }
        if(outgoingBytes_ + frame.size() > RoomRelay::Limits::kMaxOutgoingQueueBytes
           || outgoing_.size() >= RoomRelay::Limits::kMaxQueuedFrames) {
            // Refusing here and ending the session is deliberate: quietly discarding a queued
            // game message would desynchronise the match instead of reporting a problem.
            fail("This computer could not keep up with the game connection.");
            return false;
        }

        outgoingBytes_ += frame.size();
        outgoing_.push_back(frame);
        drainOutgoing();
        return state_ != State::Closed;
    }

    bool receive(std::vector<std::uint8_t>& frame) override {
        if(incoming_.empty()) {
            return false;
        }
        frame = std::move(incoming_.front());
        incoming_.pop_front();
        incomingBytes_ -= (frame.size() < incomingBytes_) ? frame.size() : incomingBytes_;
        return true;
    }

    void close(std::uint16_t code, const std::string& reason) override {
        if(state_ == State::Closed) {
            return;
        }
        // A close frame is a new frame, and libcurl refuses to start one while a previous frame
        // still has payload outstanding ("starting new frame with N bytes from last one
        // remaining to be sent"). If a large message was only partially written, the socket is
        // already not keeping up; going quiet is the honest outcome and the relay's own liveness
        // deadline notices within seconds.
        if(state_ == State::Open && frameSent_ == 0) {
            // A close frame carries a big-endian status code followed by an optional reason.
            std::vector<std::uint8_t> body;
            body.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
            body.push_back(static_cast<std::uint8_t>(code & 0xFF));
            const std::string trimmed = RoomRelay::sanitizeRelayMessage(reason).substr(0, 100);
            body.insert(body.end(), trimmed.begin(), trimmed.end());

            std::size_t sent = 0;
            curl_ws_send(easy_, body.empty() ? "" : reinterpret_cast<const char*>(body.data()),
                         body.size(), &sent, 0, CURLWS_CLOSE);
        }
        closeCode_ = (closeCode_ != 0) ? closeCode_ : code;
        state_ = State::Closed;
    }

    std::uint16_t closeCode() const override { return closeCode_; }
    const std::string& lastError() const override { return lastError_; }
    std::size_t outgoingBacklogBytes() const override { return outgoingBytes_; }

private:
    void fail(const std::string& message) {
        if(lastError_.empty()) {
            lastError_ = message;
        }
        state_ = State::Closed;
    }

    void driveHandshake() {
        int running = 0;
        CURLMcode code = curl_multi_perform(multi_, &running);
        if(code != CURLM_OK) {
            fail("The game could not reach the game service.");
            return;
        }

        // Zero timeout: this is called from the game loop and must return immediately.
        int readyDescriptors = 0;
        curl_multi_poll(multi_, nullptr, 0, 0, &readyDescriptors);

        int queued = 0;
        while(CURLMsg* message = curl_multi_info_read(multi_, &queued)) {
            if(message->msg != CURLMSG_DONE || message->easy_handle != easy_) {
                continue;
            }
            if(message->data.result != CURLE_OK) {
                fail(describeConnectFailure(message->data.result));
                return;
            }
            long status = 0;
            curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
            if(status != 0 && status != 101) {
                fail("The game service refused the connection.");
                return;
            }
            state_ = State::Open;
            return;
        }

        // A finished handle always leaves a completion message behind, and the loop above acts
        // on it. Anything else - including a handle that reports nothing still running without
        // having said why - is left to the deadline rather than guessed at, because a false
        // failure here would look to the player like the relay is down.
        (void)running;

        if(SDL_TICKS_PASSED(SDL_GetTicks(), deadline_)) {
            closeCode_ = RoomRelay::Close::Timeout;
            fail("The game service did not answer in time.");
        }
    }

    static std::string describeConnectFailure(CURLcode result) {
        switch(result) {
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CACERT_BADFILE:
            case CURLE_SSL_CONNECT_ERROR:
                return "The game service certificate could not be verified.";
            case CURLE_COULDNT_RESOLVE_HOST:
                return "The game service address could not be found.";
            case CURLE_COULDNT_CONNECT:
                return "The game service could not be reached.";
            case CURLE_OPERATION_TIMEDOUT:
                return "The game service did not answer in time.";
            case CURLE_UNSUPPORTED_PROTOCOL:
                return "This copy of the game cannot open secure game connections.";
            default:
                return "The game could not reach the game service.";
        }
    }

    /**
        Offers queued messages to curl, one WebSocket frame per message.

        The partial-write contract, from curl_ws_send(3):

          - Without CURLWS_OFFSET, libcurl writes a frame header for exactly `buflen` bytes and
            `fragsize` is not used. "fragsize should always be set to zero unless a (huge) frame
            shall be sent using multiple calls with partial content per call explicitly."
          - "If the return value is CURLE_OK but sent is less than the given buflen, libcurl was
            unable to consume the complete payload in a single call. In this case the application
            must call this function again until all payload is processed."
          - CURLWS_OFFSET with a zero fragsize continues a frame that is already in progress.

        So the first call for a message declares the whole message as one frame and passes no
        fragsize, and any remainder is offered as a continuation. Passing a non-zero fragsize on
        that first call, as this used to, was ignored by libcurl but read as if it mattered - and
        it is the parameter libcurl would start validating if it ever validated one.

        CURLWS_CONT is deliberately absent: that splits one *message* across several frames,
        which this transport never needs.
    */
    void drainOutgoing() {
        while(state_ == State::Open && !outgoing_.empty()) {
            const std::vector<std::uint8_t>& frame = outgoing_.front();
            const std::size_t remaining = frame.size() - frameSent_;

            std::size_t sent = 0;
            unsigned int flags = CURLWS_BINARY;
            if(frameSent_ != 0) {
                flags |= CURLWS_OFFSET;     // continuing the frame the first call started
            }

            const CURLcode result = curl_ws_send(
                easy_, reinterpret_cast<const char*>(frame.data() + frameSent_), remaining,
                &sent, 0, flags);

            if(result == CURLE_AGAIN) {
                return;         // socket is full; try again on the next pump
            }
            if(result != CURLE_OK) {
                fail("The connection to the game was lost.");
                return;
            }
            if(sent == 0) {
                return;         // no progress right now
            }

            frameSent_ += sent;
            if(frameSent_ >= frame.size()) {
                outgoingBytes_ -= (frame.size() < outgoingBytes_) ? frame.size() : outgoingBytes_;
                outgoing_.pop_front();
                frameSent_ = 0;
            }
        }
    }

    /**
        Reads whatever curl has, reassembling fragments and chunks into whole messages. Ping,
        pong and close frames are interleaved with data frames and are handled here; curl answers
        pings itself, so a ping only needs to be consumed and ignored.
    */
    void drainIncoming() {
        char buffer[kReadChunkBytes];

        while(state_ == State::Open) {
            std::size_t received = 0;
            const struct curl_ws_frame* meta = nullptr;
            const CURLcode result = curl_ws_recv(easy_, buffer, sizeof(buffer), &received, &meta);

            if(result == CURLE_AGAIN) {
                return;
            }
            if(result == CURLE_GOT_NOTHING || result == CURLE_RECV_ERROR) {
                closeCode_ = (closeCode_ != 0) ? closeCode_ : RoomRelay::Close::Normal;
                fail("The connection to the game was lost.");
                return;
            }
            if(result != CURLE_OK || meta == nullptr) {
                fail("The connection to the game was lost.");
                return;
            }

            if((meta->flags & CURLWS_CLOSE) != 0) {
                // A close frame body starts with a big-endian status code.
                if(received >= 2) {
                    closeCode_ = static_cast<std::uint16_t>(
                        (static_cast<unsigned char>(buffer[0]) << 8)
                        | static_cast<unsigned char>(buffer[1]));
                } else if(closeCode_ == 0) {
                    closeCode_ = RoomRelay::Close::Normal;
                }
                lastError_ = RoomRelay::describeCloseCode(closeCode_);
                state_ = State::Closed;
                return;
            }
            if((meta->flags & (CURLWS_PING | CURLWS_PONG)) != 0) {
                continue;       // liveness only; curl answers pings for us
            }
            if((meta->flags & CURLWS_TEXT) != 0) {
                closeCode_ = RoomRelay::Close::ProtocolError;
                fail("The game service sent an unexpected message.");
                return;
            }

            // The limit is applied to the bytes already held plus what is still announced, so a
            // huge message is refused before it has been assembled rather than after. Every
            // comparison is in subtraction form against a value that is known to be in range.
            constexpr std::size_t kLimit = RoomRelay::Limits::kMaxFrameBytes;
            const std::size_t announced = (meta->bytesleft > 0)
                ? static_cast<std::size_t>(meta->bytesleft) : 0;
            if(received > kLimit || announced > kLimit
               || assembly_.size() > kLimit - received
               || (assembly_.size() + received) > kLimit - announced) {
                closeCode_ = RoomRelay::Close::TooLarge;
                fail("The game service sent a message that was too large.");
                return;
            }

            assembly_.insert(assembly_.end(), buffer, buffer + received);

            const bool frameComplete = (meta->bytesleft == 0);
            const bool messageComplete = frameComplete && ((meta->flags & CURLWS_CONT) == 0);
            if(!messageComplete) {
                continue;
            }

            if(incomingBytes_ + assembly_.size() > RoomRelay::Limits::kMaxIncomingQueueBytes
               || incoming_.size() >= RoomRelay::Limits::kMaxQueuedFrames) {
                fail("The game fell too far behind the connection.");
                return;
            }

            incomingBytes_ += assembly_.size();
            incoming_.push_back(std::move(assembly_));
            assembly_.clear();
        }
    }

    CURLM*      multi_    = nullptr;
    CURL*       easy_     = nullptr;
    curl_slist* headers_  = nullptr;
    bool        attached_ = false;

    State       state_     = State::Connecting;
    Uint32      deadline_  = 0;
    std::uint16_t closeCode_ = 0;
    std::string lastError_;

    std::deque<std::vector<std::uint8_t>> outgoing_;
    std::size_t outgoingBytes_ = 0;
    std::size_t frameSent_     = 0;

    std::vector<std::uint8_t>             assembly_;
    std::deque<std::vector<std::uint8_t>> incoming_;
    std::size_t incomingBytes_ = 0;
};

#endif // DUNECITY_HAVE_CURL_WEBSOCKET

} // namespace

RelayWebSocketSupport relayWebSocketSupport() {
    RelayWebSocketSupport support;
#ifndef DUNECITY_HAVE_CURL_WEBSOCKET
    support.available = false;
    support.reason = "This build of the game was made without support for online play through "
                     "the game service.";
    return support;
#else
    bool secure = false;
    if(!curlCarriesWebSocket(secure)) {
        support.available = false;
        support.reason = "The network library on this computer does not support the game "
                         "service. Installing the game's own copy of that library fixes this.";
        return support;
    }
    support.available = true;
    if(!secure) {
        support.reason = "The network library on this computer supports unencrypted game "
                         "connections only.";
    }
    return support;
#endif
}

std::unique_ptr<RelayWebSocket> createRelayWebSocket(const std::string& url,
                                                     const std::string& origin) {
#ifndef DUNECITY_HAVE_CURL_WEBSOCKET
    (void)url;
    (void)origin;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "RelayWebSocket: this build has no libcurl WebSocket support");
    return nullptr;
#else
    if(!relayWebSocketSupport().available) {
        return nullptr;
    }
    return std::make_unique<CurlRelayWebSocket>(url, origin);
#endif
}

#endif // !__EMSCRIPTEN__
