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

/**
    The bounded HTTPS client, in both flavours.

    One request at a time, never blocking, and a session token only ever travels in the
    X-Dune-Session header - never in a url, where it would end up in proxy logs, browser history
    and referrers. Response bodies are bounded before they are kept, and again while they arrive:
    the parsers refuse anything longer anyway, and reading it into memory first would hand a
    hostile service an easy allocation.

    Both callers - room admission and direct-play signalling - use this. They differ only in the
    size of answer they are prepared to read.
*/

#include <Network/BoundedHttpClient.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#else
#include <curl/curl.h>
#include <misc/FileSystem.h>
#include <filesystem>
#include <system_error>
#endif

namespace {
constexpr long kDefaultTimeoutSeconds = 20;

/// What this request may read, clamped to the ceiling no caller can raise.
std::size_t boundFor(const BoundedHttpClient::Request& request) {
    const std::size_t asked = request.maxResponseBytes == 0
                                  ? BoundedHttpClient::kMaxResponseBytes
                                  : request.maxResponseBytes;
    return asked < BoundedHttpClient::kMaxResponseBytes ? asked
                                                        : BoundedHttpClient::kMaxResponseBytes;
}

long timeoutFor(const BoundedHttpClient::Request& request) {
    return request.timeoutSeconds > 0 ? request.timeoutSeconds : kDefaultTimeoutSeconds;
}
}  // namespace

#ifdef __EMSCRIPTEN__

/*
    Browser backend.

    The same shape as the relay's HTTPS transport (src/Network/RelayHttpTransportEmscripten.cpp),
    and for the same reasons. emscripten_fetch's LOAD_TO_MEMORY reads the whole body into memory
    before anything can look at its size, follows redirects, and hands back a pointer whose
    lifetime is the callback's - none of which is acceptable for a credentialed request to a
    service this client does not control.

    So: `fetch` with `redirect: 'error'`, `credentials: 'omit'`, `cache: 'no-store'` and a
    referrer policy, an AbortController for the timeout and for cancellation, the declared
    Content-Length checked before a byte is read, and the stream counted against the same bound
    while it is read so a service that lies about the length still cannot exceed it.

    Every request is an integer handle into a registry on globalThis. C++ can hold a handle
    safely for as long as it likes: releasing it marks the entry abandoned, so a promise that
    settles afterwards cleans up after itself instead of writing into something that is gone.
*/

namespace {
constexpr int kNoHandle       = 0;
constexpr int kStatePending   = 0;
constexpr int kStateCompleted = 1;
constexpr int kStateFailed    = 2;
constexpr int kStateTimedOut  = 3;
constexpr int kStateOverflow  = 4;
} // namespace

EM_JS(int, duneSignalStart, (const char* urlPtr, const char* tokenPtr, const char* bodyPtr,
                             int bodyLength, int timeoutMs, int maxResponseBytes), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) {
        registry = globalThis.__duneP2PSignal = { next: 1, requests: {} };
    }
    var handle = registry.next++;
    if (registry.next > 0x7ffffffe) { registry.next = 1; }

    var url = UTF8ToString(urlPtr);
    var token = UTF8ToString(tokenPtr);
    // Copied out of the wasm heap now: the heap buffer can be replaced when memory grows, and
    // the request outlives this call by definition.
    var body = new Uint8Array(bodyLength > 0 ? HEAPU8.subarray(bodyPtr, bodyPtr + bodyLength) : 0);

    var entry = {
        done: 0, failed: 0, timedOut: 0, abandoned: 0, overflowed: 0,
        status: 0, bytes: null, controller: null
    };
    registry.requests[handle] = entry;

    var headers = { 'Content-Type': 'application/x-www-form-urlencoded' };
    if (token.length > 0) {
        // The session token travels here and nowhere else: never in the URL, never logged.
        headers['X-Dune-Session'] = token;
    }

    var controller = (typeof AbortController !== 'undefined') ? new AbortController() : null;
    entry.controller = controller;
    var timer = setTimeout(function() {
        entry.timedOut = 1;
        if (controller) { try { controller.abort(); } catch (e) {} }
    }, timeoutMs);

    fetch(url, {
        method: 'POST',
        headers: headers,
        body: body,
        mode: 'cors',
        // No ambient credentials: a grant must never be issued on the strength of a cookie.
        credentials: 'omit',
        cache: 'no-store',
        // A redirect would send this request, and its session header, somewhere this client
        // never agreed to talk to.
        redirect: 'error',
        referrerPolicy: 'no-referrer',
        signal: controller ? controller.signal : undefined
    }).then(function(response) {
        entry.status = response.status;
        var declared = response.headers.get('Content-Length');
        if (declared !== null && (!/^[0-9]+$/.test(declared) || Number(declared) > maxResponseBytes)) {
            entry.overflowed = 1;
            if (controller) { try { controller.abort(); } catch (e) {} }
            throw new Error('Response too large');
        }
        if (!response.body) { return new Uint8Array(0); }
        var reader = response.body.getReader();
        var chunks = [];
        var total = 0;
        function readNext() {
            return reader.read().then(function(part) {
                if (part.done) {
                    var bytes = new Uint8Array(total);
                    var offset = 0;
                    chunks.forEach(function(chunk) { bytes.set(chunk, offset); offset += chunk.length; });
                    return bytes;
                }
                // Counted while it arrives, so a service that understates Content-Length still
                // cannot make this allocate more than the parser would ever accept.
                if (part.value.length > maxResponseBytes - total) {
                    entry.overflowed = 1;
                    reader.cancel().catch(function() {});
                    if (controller) { try { controller.abort(); } catch (e) {} }
                    throw new Error('Response too large');
                }
                total += part.value.length;
                chunks.push(part.value);
                return readNext();
            });
        }
        return readNext();
    }).then(function(buffer) {
        clearTimeout(timer);
        // A request released while it was in flight owns nothing any more; it only has to clean
        // up after itself. `done` is set last in both paths, so a reader can never see a
        // finished entry whose result has not been stored yet.
        if (entry.abandoned) { delete registry.requests[handle]; return; }
        entry.bytes = buffer;
        entry.done = 1;
    }).catch(function(error) {
        clearTimeout(timer);
        if (entry.abandoned) { delete registry.requests[handle]; return; }
        entry.failed = 1;
        entry.done = 1;
    });

    return handle;
});

EM_JS(int, duneSignalState, (int handle), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    if (!entry || !entry.done) { return 0; }
    if (entry.overflowed) { return 4; }
    if (entry.failed) { return entry.timedOut ? 3 : 2; }
    return 1;
});

EM_JS(int, duneSignalStatus, (int handle), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    return (entry && entry.done) ? entry.status : 0;
});

EM_JS(int, duneSignalLength, (int handle), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    return (entry && entry.bytes) ? entry.bytes.length : 0;
});

EM_JS(void, duneSignalCopy, (int handle, char* destination, int capacity), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) { return; }
    var entry = registry.requests[handle];
    if (!entry || !entry.bytes || capacity <= 0) { return; }
    var length = entry.bytes.length < capacity ? entry.bytes.length : capacity;
    HEAPU8.set(entry.bytes.subarray(0, length), destination);
});

EM_JS(void, duneSignalRelease, (int handle), {
    var registry = globalThis.__duneP2PSignal;
    if (!registry) { return; }
    var entry = registry.requests[handle];
    if (!entry) { return; }
    // Marked before it is dropped, so a promise that settles later takes the abandoned path
    // instead of holding an answer nobody will ever read.
    entry.abandoned = 1;
    entry.bytes = null;
    if (entry.controller) { try { entry.controller.abort(); } catch (e) {} }
    delete registry.requests[handle];
});

namespace {

class FetchHttpClient final : public BoundedHttpClient {
public:
    ~FetchHttpClient() override { cancel(); }

    void begin(const Request& request) override {
        cancel();
        body_  = request.body;
        bound_ = boundFor(request);
        handle_ = duneSignalStart(request.url.c_str(), request.sessionToken.c_str(),
                                  body_.c_str(), static_cast<int>(body_.size()),
                                  static_cast<int>(timeoutFor(request) * 1000),
                                  static_cast<int>(bound_));
        if(handle_ == kNoHandle) {
            failed_ = true;
        }
    }

    void update() override { /* the browser drives fetch; there is nothing to pump */ }

    bool poll(Result& out) override {
        if(failed_) {
            failed_ = false;
            out = Result();
            out.failure        = Failure::Network;
            out.transportError = "the game service could not be reached";
            return true;
        }
        if(handle_ == kNoHandle) {
            return false;
        }
        const int state = duneSignalState(handle_);
        if(state == kStatePending) {
            return false;
        }

        out = Result();
        if(state == kStateCompleted) {
            out.httpStatus = duneSignalStatus(handle_);
            const int length = duneSignalLength(handle_);
            if(length > 0 && static_cast<std::size_t>(length) <= bound_) {
                out.body.resize(static_cast<std::size_t>(length));
                duneSignalCopy(handle_, &out.body[0], length);
            }
        } else if(state == kStateOverflow) {
            out.failure        = Failure::TooLarge;
            out.transportError = "the game service sent more than this game will read";
        } else if(state == kStateTimedOut) {
            out.failure        = Failure::Timeout;
            out.transportError = "the game service did not answer in time";
        } else {
            // The browser deliberately hides why a fetch failed: a refused redirect, a blocked
            // request and a dropped connection all arrive here. It cannot tell a certificate
            // failure apart either, so this stays the general case.
            out.failure        = Failure::Network;
            out.transportError = "the game service could not be reached";
        }
        cancel();
        return true;
    }

    bool busy() const override {
        return failed_ || (handle_ != kNoHandle && duneSignalState(handle_) == kStatePending);
    }

    void cancel() override {
        if(handle_ != kNoHandle) {
            duneSignalRelease(handle_);
            handle_ = kNoHandle;
        }
        failed_ = false;
    }

private:
    int         handle_ = kNoHandle;
    bool        failed_ = false;
    std::size_t bound_  = kMaxResponseBytes;
    std::string body_;
};

} // namespace

#else

namespace {

void configureSignalingCertificates(CURL* curl) {
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

/// Native backend: one easy handle on a multi handle, driven from the game loop.
class CurlHttpClient final : public BoundedHttpClient {
public:
    ~CurlHttpClient() override { teardown(); }

    void begin(const Request& request) override {
        teardown();
        body_  = request.body;
        url_   = request.url;
        bound_ = boundFor(request);
        response_.clear();
        overflowed_ = false;
        finished_   = false;
        result_     = Result();

        multi_ = curl_multi_init();
        easy_  = curl_easy_init();
        if(multi_ == nullptr || easy_ == nullptr) {
            teardown();
            finished_              = true;
            result_.failure        = Failure::Network;
            result_.transportError = "the game service could not be reached";
            return;
        }

        curl_easy_setopt(easy_, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(easy_, CURLOPT_POST, 1L);
        curl_easy_setopt(easy_, CURLOPT_POSTFIELDS, body_.c_str());
        curl_easy_setopt(easy_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_.size()));
        curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &CurlHttpClient::writeCallback);
        curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, 0L);
        // No ambient proxy. The session token travels in a header, and an environment variable
        // should not be able to decide who else gets to see it.
        curl_easy_setopt(easy_, CURLOPT_PROXY, "");
        // http is only ever reached for an explicitly chosen loopback development endpoint; the
        // transport refuses every other plaintext address before a request is built.
        curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(easy_, CURLOPT_TIMEOUT, timeoutFor(request));
        curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy_, CURLOPT_USERAGENT, "DuneCity");
        configureSignalingCertificates(easy_);

        headers_ = curl_slist_append(headers_, "Content-Type: application/x-www-form-urlencoded");
        headers_ = curl_slist_append(headers_, "Expect:");
        if(!request.sessionToken.empty()) {
            // Header, never a query parameter: this is a credential.
            const std::string header = "X-Dune-Session: " + request.sessionToken;
            headers_ = curl_slist_append(headers_, header.c_str());
        }
        if(headers_ != nullptr) {
            curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, headers_);
        }
        if(curl_multi_add_handle(multi_, easy_) != CURLM_OK) {
            teardown();
            finished_              = true;
            result_.failure        = Failure::Network;
            result_.transportError = "the game service could not be reached";
            return;
        }
        added_ = true;
        busy_  = true;
    }

    void update() override {
        if(!busy_ || multi_ == nullptr) {
            return;
        }
        int running = 0;
        if(curl_multi_perform(multi_, &running) != CURLM_OK) {
            complete(Failure::Network, "the game service could not be reached");
            return;
        }
        int ready = 0;
        curl_multi_poll(multi_, nullptr, 0, 0, &ready);

        int queued = 0;
        while(CURLMsg* message = curl_multi_info_read(multi_, &queued)) {
            if(message->msg != CURLMSG_DONE) {
                continue;
            }
            if(message->data.result != CURLE_OK) {
                if(overflowed_) {
                    // The write callback aborted the transfer because the answer exceeded what
                    // this caller said it would read.
                    complete(Failure::TooLarge,
                             "the game service sent more than this game will read");
                    return;
                }
                switch(message->data.result) {
                    case CURLE_PEER_FAILED_VERIFICATION:
                    case CURLE_SSL_CACERT_BADFILE:
                    case CURLE_SSL_CONNECT_ERROR:
                        complete(Failure::Certificate,
                                 "the game service certificate could not be verified");
                        break;
                    case CURLE_OPERATION_TIMEDOUT:
                        complete(Failure::Timeout, "the game service did not answer in time");
                        break;
                    case CURLE_COULDNT_RESOLVE_HOST:
                        complete(Failure::NameResolution,
                                 "the game service address could not be found");
                        break;
                    default:
                        complete(Failure::Network, "the game service could not be reached");
                        break;
                }
                return;
            }
            long status = 0;
            curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
            result_.httpStatus = status;
            result_.body       = response_;
            complete(Failure::None, std::string());
            return;
        }
    }

    bool poll(Result& out) override {
        if(!finished_) {
            return false;
        }
        out       = result_;
        finished_ = false;
        result_   = Result();
        return true;
    }

    bool busy() const override { return busy_; }

    void cancel() override {
        teardown();
        finished_ = false;
        result_   = Result();
    }

private:
    static std::size_t writeCallback(char* data, std::size_t size, std::size_t count, void* user) {
        auto* self = static_cast<CurlHttpClient*>(user);
        const std::size_t length = size * count;
        if(length > self->bound_ || self->response_.size() > self->bound_ - length) {
            // Refuses the transfer rather than growing without a bound. Counted as it arrives,
            // so a service that understates Content-Length gains nothing.
            self->overflowed_ = true;
            return 0;
        }
        self->response_.append(data, length);
        return length;
    }

    void complete(Failure failure, const std::string& transportError) {
        if(!transportError.empty()) {
            result_.failure        = failure;
            result_.transportError = transportError;
        }
        teardown();
        finished_ = true;
    }

    void teardown() {
        if(multi_ != nullptr && easy_ != nullptr && added_) {
            curl_multi_remove_handle(multi_, easy_);
        }
        added_ = false;
        if(easy_ != nullptr) {
            curl_easy_cleanup(easy_);
            easy_ = nullptr;
        }
        if(multi_ != nullptr) {
            curl_multi_cleanup(multi_);
            multi_ = nullptr;
        }
        if(headers_ != nullptr) {
            curl_slist_free_all(headers_);
            headers_ = nullptr;
        }
        busy_ = false;
    }

    CURLM*      multi_   = nullptr;
    CURL*       easy_    = nullptr;
    curl_slist* headers_ = nullptr;
    bool        added_   = false;
    bool        busy_       = false;
    bool        finished_   = false;
    bool        overflowed_ = false;
    std::size_t bound_      = kMaxResponseBytes;
    std::string body_;
    std::string url_;
    std::string response_;
    Result      result_;
};

} // namespace

#endif

std::unique_ptr<BoundedHttpClient> createBoundedHttpClient() {
#ifdef __EMSCRIPTEN__
    return std::make_unique<FetchHttpClient>();
#else
    return std::make_unique<CurlHttpClient>();
#endif
}
