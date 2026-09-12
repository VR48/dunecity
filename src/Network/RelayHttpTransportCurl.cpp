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
    The native HTTP backend for the relay polling transport.

    One easy handle on a private multi handle, driven with a zero timeout from the game loop -
    the same shape RoomAdmissionClient uses, and for the same reason: it is the only way to run
    libcurl from a simulation tick without ever blocking in one.

    The TLS posture is deliberately identical to admission and to the WebSocket transport:
    certificate chain and hostname verified, no redirect ever followed, only http and https
    permitted at the libcurl level as well, and the bundled CA file preferred when the install
    ships one. A relay endpoint that answers with a redirect is not a relay endpoint.

    The session token goes into the `X-Dune-Session` header and appears nowhere else. There is
    no logging in this file at all, which is the simplest way to be sure of that.
*/

#include <Network/RelayHttpTransport.h>

#include <misc/FileSystem.h>
#include <misc/SDL2pp.h>

#include <curl/curl.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

/**
    Prefers the CA bundle the install ships, exactly as ENetHttp.cpp and RoomAdmissionClient.cpp
    do. The three copies of this are not shared yet on purpose: folding them together touches
    two files that this change has no other business in.
*/
void configurePollCertificates(CURL* curl) {
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

/// Which libcurl failures are worth repeating the same request for.
bool isTransientCurlFailure(CURLcode result) {
    switch(result) {
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            // Everything else - a refused certificate above all - is a condition that repeating
            // the request cannot change.
            return false;
    }
}

std::string describeCurlFailure(CURLcode result) {
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
        case CURLE_URL_MALFORMAT:
            return "The game service address is not valid.";
        default:
            return "The connection to the game was lost.";
    }
}

class CurlRelayHttpBackend final : public RelayHttpBackend {
public:
    ~CurlRelayHttpBackend() override {
        release();
        if(multi_ != nullptr) curl_multi_cleanup(multi_);
    }

    std::uint32_t nowMs() const override { return static_cast<std::uint32_t>(SDL_GetTicks()); }

    bool start(const RelayHttpRequest& request) override {
        release();

        // Keep the multi handle's connection cache across polls, including TLS sessions.
        if(multi_ == nullptr) multi_ = curl_multi_init();
        easy_  = curl_easy_init();
        if(multi_ == nullptr || easy_ == nullptr) {
            release();
            return false;
        }

        // The body is held here for the whole transfer: CURLOPT_POSTFIELDS does not copy, and
        // the caller's request object does not outlive this call.
        url_.assign(request.url);
        body_.assign(request.body.begin(), request.body.end());
        response_.clear();
        overflowed_ = false;
        finished_   = false;

        curl_easy_setopt(easy_, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(easy_, CURLOPT_POST, 1L);
        curl_easy_setopt(easy_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_.size()));
        curl_easy_setopt(easy_, CURLOPT_POSTFIELDS, body_.empty() ? "" : body_.data());
        curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &CurlRelayHttpBackend::writeCallback);
        curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 0L);
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 85, 0)
        curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https");
#endif
        curl_easy_setopt(easy_, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
        curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy_, CURLOPT_USERAGENT, "DuneCity");

        configurePollCertificates(easy_);

        headers_ = curl_slist_append(headers_, "Content-Type: application/octet-stream");
        headers_ = curl_slist_append(headers_, "Accept: application/octet-stream");
        headers_ = curl_slist_append(headers_, "Cache-Control: no-store");
        // Without this libcurl waits for a 100-continue on larger bodies, which would add a
        // round trip to exactly the requests that are already the slowest.
        headers_ = curl_slist_append(headers_, "Expect:");
        if(!request.sessionToken.empty()) {
            const std::string header = "X-Dune-Session: " + request.sessionToken;
            headers_ = curl_slist_append(headers_, header.c_str());
        }
        // Native clients send no Origin: it is a browser's statement about a page, and one
        // invented by a native client would be a claim the relay must not believe anyway.
        if(headers_ != nullptr) {
            curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, headers_);
        }

        if(curl_multi_add_handle(multi_, easy_) != CURLM_OK) {
            release();
            return false;
        }
        attached_ = true;

        // One non-blocking nudge so the connection starts now rather than on the next pump.
        int running = 0;
        curl_multi_perform(multi_, &running);
        return true;
    }

    bool poll(RelayHttpOutcome& outcome) override {
        if(easy_ == nullptr || finished_) {
            return false;
        }

        int running = 0;
        if(curl_multi_perform(multi_, &running) != CURLM_OK) {
            finished_ = true;
            outcome.kind  = RelayHttpOutcome::Kind::NetworkFailure;
            outcome.error = "The connection to the game was lost.";
            release();
            return true;
        }

        // Zero timeout: this runs inside the game loop and must return immediately.
        int readyDescriptors = 0;
        curl_multi_poll(multi_, nullptr, 0, 0, &readyDescriptors);

        int queued = 0;
        while(CURLMsg* message = curl_multi_info_read(multi_, &queued)) {
            if(message->msg != CURLMSG_DONE || message->easy_handle != easy_) {
                continue;
            }
            const CURLcode result = message->data.result;
            long status = 0;
            curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);

            finished_ = true;
            if(overflowed_) {
                // Refused rather than truncated: half an answer must never be parsed.
                outcome.kind  = RelayHttpOutcome::Kind::Fatal;
                outcome.error = "The game service sent an unusable answer.";
            } else if(result != CURLE_OK) {
                outcome.kind = isTransientCurlFailure(result)
                             ? RelayHttpOutcome::Kind::NetworkFailure
                             : RelayHttpOutcome::Kind::Fatal;
                outcome.error = describeCurlFailure(result);
            } else {
                outcome.kind   = RelayHttpOutcome::Kind::Completed;
                outcome.status = status;
                outcome.body.assign(response_.begin(), response_.end());
            }
            release();
            return true;
        }

        return false;
    }

    void cancel() override { release(); }

private:
    static std::size_t writeCallback(char* data, std::size_t size, std::size_t count,
                                     void* userData) {
        auto* self = static_cast<CurlRelayHttpBackend*>(userData);
        if(self == nullptr) {
            return 0;
        }
        const std::size_t bytes = size * count;
        // Subtraction form, and the transfer is aborted rather than the answer truncated.
        if(bytes > RoomPoll::Limits::kMaxResponseBytes
           || self->response_.size() > RoomPoll::Limits::kMaxResponseBytes - bytes) {
            self->overflowed_ = true;
            return 0;
        }
        self->response_.insert(self->response_.end(), data, data + bytes);
        return bytes;
    }

    void release() {
        if(attached_ && multi_ != nullptr && easy_ != nullptr) {
            curl_multi_remove_handle(multi_, easy_);
            attached_ = false;
        }
        if(easy_ != nullptr) {
            curl_easy_cleanup(easy_);
            easy_ = nullptr;
        }
        if(headers_ != nullptr) {
            curl_slist_free_all(headers_);
            headers_ = nullptr;
        }
    }

    CURLM*      multi_    = nullptr;
    CURL*       easy_     = nullptr;
    curl_slist* headers_  = nullptr;
    bool        attached_ = false;
    bool        finished_ = false;
    bool        overflowed_ = false;

    std::string               url_;
    std::string               body_;
    std::vector<std::uint8_t> response_;
};

} // namespace

std::unique_ptr<RelayWebSocket> createRelayHttpTransport(const std::string& url,
                                                         const std::string& origin) {
    // Native clients send no Origin header; see the comment in start() above.
    (void)origin;
    return std::make_unique<RelayHttpTransport>(url,
                                                std::make_unique<CurlRelayHttpBackend>());
}

#endif // !__EMSCRIPTEN__
