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

#ifdef __EMSCRIPTEN__

/**
    The browser HTTP backend for the relay polling transport.

    This uses the Fetch API through EM_JS rather than emscripten_fetch(), for two reasons that
    both matter here:

      - emscripten_fetch() is XMLHttpRequest underneath, which cannot say `credentials: 'omit'`
        or `cache: 'no-store'` and cannot refuse a redirect. This transport must say all three:
        the session token is the only credential in play and no cookie should ever be attached
        to a relay request, a cached exchange answer would be a replayed batch, and a redirect
        would move the request to a URL nothing validated.
      - ownership. There is no C callback anywhere in this file. The JavaScript side owns the
        request and its result; C++ holds nothing but an integer handle and asks about it from
        pump(). A request that completes after this object is gone therefore has nothing to
        write through - it finds its entry marked abandoned and deletes it. That is a stronger
        guarantee than detaching a callback, because there is no pointer to get wrong.

    Nothing here blocks. There is no synchronous XHR, no busy wait and no pthread; every fetch
    is an ordinary promise that settles on the browser's event loop between game loop
    iterations.

    Origin is not set here, and cannot be: the browser attaches its own and forbids a page from
    choosing one. That is exactly what makes Origin worth checking on the relay for browser
    clients and worth nothing for native ones.
*/

#include <Network/RelayHttpTransport.h>

#include <emscripten/emscripten.h>
#include <emscripten/em_js.h>

#include <memory>
#include <string>
#include <vector>

namespace {

/// Nothing in flight.
constexpr int kNoHandle = 0;

/// What duneRelayPollState() reports.
constexpr int kPollPending   = 0;
constexpr int kPollCompleted = 1;
constexpr int kPollFailed    = 2;
constexpr int kPollTimedOut  = 3;

} // namespace

/*
    The JavaScript side. The registry lives on globalThis rather than on Module so that it does
    not depend on how the runtime was configured, and every entry is keyed by an integer handle
    that C++ can hold safely for as long as it likes.
*/

EM_JS(int, duneRelayPollStart, (const char* urlPtr, const char* tokenPtr, const char* bodyPtr,
                                int bodyLength, int timeoutMs, int maxResponseBytes), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) {
        registry = globalThis.__duneRelayPoll = { next: 1, requests: {} };
    }
    var handle = registry.next++;
    if (registry.next > 0x7ffffffe) { registry.next = 1; }

    var url = UTF8ToString(urlPtr);
    var token = UTF8ToString(tokenPtr);

    // The body is copied out of the wasm heap now. The heap buffer can be replaced when memory
    // grows, and the request outlives this call by definition.
    var body = new Uint8Array(bodyLength > 0 ? HEAPU8.subarray(bodyPtr, bodyPtr + bodyLength) : 0);

    var entry = {
        done: 0, failed: 0, timedOut: 0, abandoned: 0, overflowed: 0,
        status: 0, bytes: null, controller: null
    };
    registry.requests[handle] = entry;

    var headers = { 'Content-Type': 'application/octet-stream' };
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
        credentials: 'omit',
        cache: 'no-store',
        redirect: 'error',
        referrerPolicy: 'no-referrer',
        signal: controller ? controller.signal : undefined
    }).then(function(response) {
        entry.status = response.status;
        var declared = response.headers.get('Content-Length');
        if (declared !== null && (!/^[0-9]+$/.test(declared) || Number(declared) > maxResponseBytes)) {
            entry.overflowed = 1;
            if (controller) controller.abort();
            throw new Error('Response too large');
        }
        if (!response.body) return new Uint8Array(0);
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
                if (part.value.length > maxResponseBytes - total) {
                    entry.overflowed = 1;
                    reader.cancel().catch(function() {});
                    if (controller) controller.abort();
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
        // A request that was released while it was in flight owns nothing any more; it only has
        // to clean up after itself. `done` is set last in both paths, so a reader can never see
        // a finished entry whose result has not been stored yet.
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

EM_JS(int, duneRelayPollState, (int handle), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    if (!entry || !entry.done) { return 0; }
    if (entry.overflowed) { return 4; }
    if (entry.failed) { return entry.timedOut ? 3 : 2; }
    return 1;
});

EM_JS(int, duneRelayPollStatus, (int handle), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    return (entry && entry.done) ? entry.status : 0;
});

EM_JS(int, duneRelayPollLength, (int handle), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) { return 0; }
    var entry = registry.requests[handle];
    return (entry && entry.bytes) ? entry.bytes.length : 0;
});

EM_JS(void, duneRelayPollCopy, (int handle, char* destination, int capacity), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) { return; }
    var entry = registry.requests[handle];
    if (!entry || !entry.bytes || capacity <= 0) { return; }
    var length = entry.bytes.length < capacity ? entry.bytes.length : capacity;
    HEAPU8.set(entry.bytes.subarray(0, length), destination);
});

EM_JS(void, duneRelayPollRelease, (int handle), {
    var registry = globalThis.__duneRelayPoll;
    if (!registry) { return; }
    var entry = registry.requests[handle];
    if (!entry) { return; }
    // Marked before it is dropped, so a promise that settles later takes the abandoned path and
    // cleans up after itself instead of holding the answer forever.
    entry.abandoned = 1;
    entry.bytes = null;
    if (entry.controller) { try { entry.controller.abort(); } catch (e) {} }
    delete registry.requests[handle];
});

namespace {

class EmscriptenRelayHttpBackend final : public RelayHttpBackend {
public:
    ~EmscriptenRelayHttpBackend() override { cancel(); }

    std::uint32_t nowMs() const override {
        return static_cast<std::uint32_t>(emscripten_get_now());
    }

    bool start(const RelayHttpRequest& request) override {
        cancel();

        const char* body = request.body.empty()
                         ? "" : reinterpret_cast<const char*>(request.body.data());
        handle_ = duneRelayPollStart(request.url.c_str(), request.sessionToken.c_str(), body,
                                     static_cast<int>(request.body.size()),
                                     static_cast<int>(request.timeoutMs),
                                     static_cast<int>(RoomPoll::Limits::kMaxResponseBytes));
        return handle_ != kNoHandle;
    }

    bool poll(RelayHttpOutcome& outcome) override {
        if(handle_ == kNoHandle) {
            return false;
        }

        const int state = duneRelayPollState(handle_);
        if(state == kPollPending) {
            return false;
        }

        if(state == kPollTimedOut) {
            outcome.kind  = RelayHttpOutcome::Kind::NetworkFailure;
            outcome.error = "The game service did not answer in time.";
            cancel();
            return true;
        }
        if(state == kPollFailed) {
            // The browser deliberately hides why a fetch failed. A refused redirect, a blocked
            // request and a dropped connection all arrive here, and only the last is worth
            // repeating - so it is treated as the retryable case and the two bounded retries
            // decide it.
            outcome.kind  = RelayHttpOutcome::Kind::NetworkFailure;
            outcome.error = "The connection to the game was lost.";
            cancel();
            return true;
        }

        if(state != kPollCompleted) {
            // Includes a response rejected by the bounded JavaScript reader.
            outcome.kind  = RelayHttpOutcome::Kind::Fatal;
            outcome.error = "The connection to the game was lost.";
            cancel();
            return true;
        }

        const int length = duneRelayPollLength(handle_);
        if(length < 0
           || static_cast<std::size_t>(length) > RoomPoll::Limits::kMaxResponseBytes) {
            outcome.kind  = RelayHttpOutcome::Kind::Fatal;
            outcome.error = "The game service sent an unusable answer.";
            cancel();
            return true;
        }

        outcome.kind   = RelayHttpOutcome::Kind::Completed;
        outcome.status = static_cast<long>(duneRelayPollStatus(handle_));
        outcome.body.resize(static_cast<std::size_t>(length));
        if(length > 0) {
            duneRelayPollCopy(handle_, reinterpret_cast<char*>(outcome.body.data()), length);
        }
        cancel();
        return true;
    }

    void cancel() override {
        if(handle_ != kNoHandle) {
            duneRelayPollRelease(handle_);
            handle_ = kNoHandle;
        }
    }

private:
    int handle_ = kNoHandle;
};

} // namespace

std::unique_ptr<RelayWebSocket> createRelayHttpTransport(const std::string& url,
                                                         const std::string& origin) {
    // The browser sets Origin itself and does not let a page override it.
    (void)origin;
    return std::make_unique<RelayHttpTransport>(url,
                                                std::make_unique<EmscriptenRelayHttpBackend>());
}

#endif // __EMSCRIPTEN__
