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

#include <Network/RoomAdmissionClient.h>

#include <misc/FileSystem.h>
#include <misc/SDL2pp.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten/fetch.h>
#else
#  include <curl/curl.h>
#endif

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {

/// Longest we wait for an admission answer before telling the player it did not work.
constexpr long kAdmissionTimeoutSeconds = 10;

/**
    Checks the admission base URL with the same rules as the gameplay socket: https:// always,
    plain http:// only for loopback and only when the development endpoint was chosen.
    Implemented by mapping the scheme onto the WebSocket one so there is a single rule.
*/
bool isAcceptableAdmissionBaseUrl(const std::string& baseUrl, bool allowLoopbackPlaintext,
                                  std::string& error) {
    std::string mapped;
    if(baseUrl.compare(0, 8, "https://") == 0) {
        mapped = "wss://" + baseUrl.substr(8);
    } else if(baseUrl.compare(0, 7, "http://") == 0) {
        mapped = "ws://" + baseUrl.substr(7);
    } else {
        error = "The game service address must start with https://.";
        return false;
    }
    if(!mapped.empty() && mapped.back() == '/') {
        mapped.pop_back();
    }
    return isAcceptableRelayUrl(mapped, allowLoopbackPlaintext, error);
}

std::string buildFormBody(const AdmissionRequest& request) {
    std::string body;
    body += "app=dunecity";
    body += "&appVersion=" + RoomAdmission::encodeFormValue(request.appVersion);
    body += "&gameProtocol=" + std::to_string(static_cast<unsigned>(request.gameProtocol));
    body += "&contentHash=" + RoomAdmission::encodeFormValue(request.contentHash);
    body += "&runtime=" + RoomAdmission::encodeFormValue(request.runtime);
    if(request.operation == AdmissionOperation::Visibility) {
        body += "&room=" + RoomAdmission::encodeFormValue(request.roomCode);
        body += "&control=" + RoomAdmission::encodeFormValue(request.controlToken);
        body += request.publicRoom ? "&visibility=public" : "&visibility=private";
    } else if(request.operation != AdmissionOperation::Room) {
        body += "&session=" + RoomAdmission::encodeFormValue(request.chatSession);
        body += "&name=" + RoomAdmission::hexText(request.displayName);
        body += "&text=" + RoomAdmission::hexText(request.chatText);
        body += "&cursor=" + std::to_string(request.chatCursor);
    } else if(request.listing) {
        body += "&offset=" + std::to_string(request.listOffset);
    } else if(request.hosting) {
        body += "&maxPeers=" + std::to_string(static_cast<unsigned>(request.maxPeers));
        body += "&mode=" + RoomAdmission::encodeFormValue(request.mode);
        body += request.publicRoom ? "&visibility=public" : "&visibility=private";
    } else {
        body += "&room=" + RoomAdmission::encodeFormValue(request.roomCode);
        body += request.publicOnly ? "&publicOnly=1" : "&publicOnly=0";
    }
    return body;
}

std::string buildEndpointUrl(const AdmissionRequest& request) {
    std::string base = request.baseUrl;
    while(!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    switch(request.operation) {
        case AdmissionOperation::Visibility: return base + "/v1/admission/visibility";
        case AdmissionOperation::ChatEnter: return base + "/v1/lobby/enter";
        case AdmissionOperation::ChatPoll: return base + "/v1/lobby/poll";
        case AdmissionOperation::ChatSay: return base + "/v1/lobby/say";
        default: break;
    }
    return base + (request.listing ? "/v1/admission/list"
        : request.hosting ? "/v1/admission/host" : "/v1/admission/join");
}

#ifndef __EMSCRIPTEN__
void configureAdmissionCertificates(CURL* curl) {
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
#endif

} // namespace

// -------------------------------------------------------------------------------------------
// Platform state
// -------------------------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__

/**
    Browser implementation on the asynchronous Fetch API.

    The fetch callbacks can fire long after the menu that started the request has gone, so they
    write into a shared state object rather than into the client. The client reads that state
    from the game loop.
*/
class RoomAdmissionClient::Impl {
public:
    struct SharedState {
        bool        finished  = false;
        bool        cancelled = false;
        long        httpStatus = 0;
        std::string body;
    };

    std::shared_ptr<SharedState> state;
    emscripten_fetch_t*          fetch = nullptr;

    ~Impl() { abandon(); }

    void abandon() {
        if(state) {
            state->cancelled = true;
        }
        if(fetch != nullptr) {
            emscripten_fetch_close(fetch);
            fetch = nullptr;
        }
    }

    static void onSucceeded(emscripten_fetch_t* fetch) {
        auto* state = static_cast<SharedState*>(fetch->userData);
        if(state != nullptr) {
            SharedState& shared = *state;
            if(!shared.cancelled) {
                shared.httpStatus = fetch->status;
                const std::size_t length = static_cast<std::size_t>(fetch->numBytes);
                if(length <= RoomAdmission::kMaxResponseBytes && fetch->data != nullptr) {
                    shared.body.assign(fetch->data, fetch->data + length);
                }
                shared.finished = true;
            }
        }
        // Impl owns the fetch. Closing here would leave Impl::fetch dangling and
        // close it twice when the game loop consumes the result or cancels.
    }

    static void onFailed(emscripten_fetch_t* fetch) {
        // HTTP refusals (expired invitation, full room, incompatible content) use
        // this callback too. Preserve their bounded body so the UI can explain them.
        onSucceeded(fetch);
    }

    std::string url;
    std::string body;
    const char* headers[3] = {"Content-Type", "application/x-www-form-urlencoded", nullptr};
};

#else

/// Native implementation: one easy handle on a private multi handle, driven from the game loop.
class RoomAdmissionClient::Impl {
public:
    CURLM*      multi   = nullptr;
    CURL*       easy    = nullptr;
    curl_slist* headers = nullptr;
    bool        attached = false;
    std::string body;
    std::string postFields;
    std::string url;
    bool        bodyOverflowed = false;

    ~Impl() { release(); }

    void release() {
        if(attached && multi != nullptr && easy != nullptr) {
            curl_multi_remove_handle(multi, easy);
            attached = false;
        }
        if(easy != nullptr) {
            curl_easy_cleanup(easy);
            easy = nullptr;
        }
        if(multi != nullptr) {
            curl_multi_cleanup(multi);
            multi = nullptr;
        }
        if(headers != nullptr) {
            curl_slist_free_all(headers);
            headers = nullptr;
        }
    }

    static std::size_t writeCallback(char* data, std::size_t size, std::size_t count,
                                     void* userData) {
        auto* self = static_cast<Impl*>(userData);
        const std::size_t bytes = size * count;
        if(self == nullptr) {
            return 0;
        }
        // Refuse rather than truncate: a half-read admission answer must not be acted on.
        if(bytes > RoomAdmission::kMaxResponseBytes
           || self->body.size() > RoomAdmission::kMaxResponseBytes - bytes) {
            self->bodyOverflowed = true;
            return 0;   // aborts the transfer
        }
        self->body.append(data, bytes);
        return bytes;
    }
};

#endif

// -------------------------------------------------------------------------------------------

RoomAdmissionClient::RoomAdmissionClient() = default;
RoomAdmissionClient::~RoomAdmissionClient() = default;

void RoomAdmissionClient::cancel() {
    impl_.reset();
    if(status_ == Status::InProgress) {
        status_ = Status::Idle;
    }
}

void RoomAdmissionClient::finishWithError(const std::string& message) {
    impl_.reset();
    response_ = AdmissionResponse();
    errorMessage_ = message;
    status_ = Status::Failed;
}

void RoomAdmissionClient::finishWithBody(long httpStatus, const std::string& body) {
    impl_.reset();

    AdmissionResponse parsed;
    std::string error;
    if(!RoomAdmission::parseAdmissionResponse(body, parsed, error, listing_, operation_)) {
        finishWithError(error);
        return;
    }

    if(!parsed.ok) {
        response_ = parsed;
        errorMessage_ = parsed.errorMessage;
        status_ = Status::Failed;
        return;
    }
    if(httpStatus != 200) {
        finishWithError("The game service refused the request.");
        return;
    }

    response_ = parsed;
    errorMessage_.clear();
    status_ = Status::Succeeded;
}

void RoomAdmissionClient::begin(const AdmissionRequest& request) {
    cancel();
    response_ = AdmissionResponse();
    errorMessage_.clear();
    listing_ = request.listing;
    operation_ = request.operation;

    std::string error;
    if(!isAcceptableAdmissionBaseUrl(request.baseUrl, request.allowLoopbackPlaintext, error)) {
        finishWithError(error);
        return;
    }
    if(request.runtime != "native" && request.runtime != "browser") {
        finishWithError("The game could not describe itself to the game service.");
        return;
    }
    if(request.operation == AdmissionOperation::Room && !request.hosting && !request.listing) {
        std::string normalized;
        if(!RoomRelay::normalizeRoomCode(request.roomCode, normalized)) {
            finishWithError("That game code is not valid. Codes look like ABCD-EFGH-JKMN.");
            return;
        }
    }
    if(request.operation == AdmissionOperation::Room && request.hosting && !request.listing
       && (request.maxPeers < 2 || request.maxPeers > RoomRelay::Limits::kMaxPeersPerRoom)) {
        finishWithError("That number of players is not supported online.");
        return;
    }

    AdmissionRequest normalizedRequest = request;
    if(request.operation == AdmissionOperation::Room && !request.hosting && !request.listing) {
        RoomRelay::normalizeRoomCode(request.roomCode, normalizedRequest.roomCode);
    }

    impl_ = std::make_unique<Impl>();
    status_ = Status::InProgress;

#ifdef __EMSCRIPTEN__
    impl_->url  = buildEndpointUrl(normalizedRequest);
    impl_->body = buildFormBody(normalizedRequest);
    impl_->state = std::make_shared<Impl::SharedState>();

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "POST");
    // LOAD_TO_MEMORY without EMSCRIPTEN_FETCH_SYNCHRONOUS: the browser must never be blocked.
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
    attr.timeoutMSecs = static_cast<unsigned long>(kAdmissionTimeoutSeconds * 1000);
    attr.requestHeaders = impl_->headers;
    attr.requestData = impl_->body.c_str();
    attr.requestDataSize = impl_->body.size();
    attr.onsuccess = &Impl::onSucceeded;
    attr.onerror = &Impl::onFailed;
    attr.userData = impl_->state.get();

    impl_->fetch = emscripten_fetch(&attr, impl_->url.c_str());
    if(impl_->fetch == nullptr) {
        finishWithError("The game service could not be reached.");
    }
#else
    impl_->url        = buildEndpointUrl(normalizedRequest);
    impl_->postFields = buildFormBody(normalizedRequest);

    impl_->multi = curl_multi_init();
    impl_->easy  = curl_easy_init();
    if(impl_->multi == nullptr || impl_->easy == nullptr) {
        finishWithError("The game could not start a network request.");
        return;
    }

    curl_easy_setopt(impl_->easy, CURLOPT_URL, impl_->url.c_str());
    curl_easy_setopt(impl_->easy, CURLOPT_POST, 1L);
    curl_easy_setopt(impl_->easy, CURLOPT_POSTFIELDS, impl_->postFields.c_str());
    curl_easy_setopt(impl_->easy, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(impl_->postFields.size()));
    curl_easy_setopt(impl_->easy, CURLOPT_WRITEFUNCTION, &Impl::writeCallback);
    curl_easy_setopt(impl_->easy, CURLOPT_WRITEDATA, impl_.get());
    curl_easy_setopt(impl_->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(impl_->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    // A redirect here could move the request to a scheme or host the validation never saw.
    curl_easy_setopt(impl_->easy, CURLOPT_FOLLOWLOCATION, 0L);
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 85, 0)
    curl_easy_setopt(impl_->easy, CURLOPT_PROTOCOLS_STR, "http,https");
#endif
    curl_easy_setopt(impl_->easy, CURLOPT_TIMEOUT, kAdmissionTimeoutSeconds);
    curl_easy_setopt(impl_->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(impl_->easy, CURLOPT_USERAGENT, "DuneCity");
    configureAdmissionCertificates(impl_->easy);

    impl_->headers = curl_slist_append(impl_->headers,
                                       "Content-Type: application/x-www-form-urlencoded");
    impl_->headers = curl_slist_append(impl_->headers, "Expect:");
    if(impl_->headers != nullptr) {
        curl_easy_setopt(impl_->easy, CURLOPT_HTTPHEADER, impl_->headers);
    }

    if(curl_multi_add_handle(impl_->multi, impl_->easy) != CURLM_OK) {
        finishWithError("The game could not start a network request.");
        return;
    }
    impl_->attached = true;
#endif
}

void RoomAdmissionClient::update() {
    if(status_ != Status::InProgress || !impl_) {
        return;
    }

#ifdef __EMSCRIPTEN__
    if(impl_->state && impl_->state->finished) {
        const long httpStatus = impl_->state->httpStatus;
        const std::string body = impl_->state->body;
        if(body.empty()) {
            finishWithError("The game service could not be reached.");
        } else {
            finishWithBody(httpStatus, body);
        }
    }
#else
    int running = 0;
    if(curl_multi_perform(impl_->multi, &running) != CURLM_OK) {
        finishWithError("The game service could not be reached.");
        return;
    }

    int readyDescriptors = 0;
    curl_multi_poll(impl_->multi, nullptr, 0, 0, &readyDescriptors);

    int queued = 0;
    while(CURLMsg* message = curl_multi_info_read(impl_->multi, &queued)) {
        if(message->msg != CURLMSG_DONE || message->easy_handle != impl_->easy) {
            continue;
        }
        const CURLcode result = message->data.result;
        long httpStatus = 0;
        curl_easy_getinfo(impl_->easy, CURLINFO_RESPONSE_CODE, &httpStatus);

        if(impl_->bodyOverflowed) {
            finishWithError("The game service sent an unusable answer.");
            return;
        }
        if(result != CURLE_OK) {
            switch(result) {
                case CURLE_PEER_FAILED_VERIFICATION:
                case CURLE_SSL_CACERT_BADFILE:
                case CURLE_SSL_CONNECT_ERROR:
                    finishWithError("The game service certificate could not be verified.");
                    break;
                case CURLE_OPERATION_TIMEDOUT:
                    finishWithError("The game service did not answer in time.");
                    break;
                case CURLE_COULDNT_RESOLVE_HOST:
                    finishWithError("The game service address could not be found.");
                    break;
                default:
                    finishWithError("The game service could not be reached.");
                    break;
            }
            return;
        }

        const std::string body = impl_->body;
        finishWithBody(httpStatus, body);
        return;
    }
#endif
}
