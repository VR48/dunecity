// Test-only send adapter: real libcurl and real relay, deterministic short accepts/backpressure.
// Never linked into the shipped game. The production queue/offset code is included unchanged.
#include <curl/curl.h>
#include <curl/websockets.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
static CURLcode forcedPartialSend(CURL*, const void*, size_t, size_t*, curl_off_t, unsigned int);
#define curl_ws_send forcedPartialSend
#include "../../src/Network/RelayWebSocketCurl.cpp"
#undef curl_ws_send

namespace {
struct SendProgress { size_t remaining = 0; unsigned calls = 0; };
std::unordered_map<CURL*, SendProgress> progress;
size_t partialAccepts = 0, blockedCalls = 0, completedFrames = 0;
struct Report {
    ~Report() { std::printf("PARTIAL_PROBE short=%zu again=%zu complete=%zu\n", partialAccepts, blockedCalls, completedFrames); }
} report;
}
static CURLcode forcedPartialSend(CURL* curl, const void* data, size_t length,
                                  size_t* sent, curl_off_t fragment, unsigned int flags) {
    if(flags & CURLWS_CLOSE) return curl_ws_send(curl, data, length, sent, fragment, flags);
    auto& p = progress[curl];
    *sent = 0;
    // Inject a retry with no progress both at frame start and during a partial frame.
    if(++p.calls % 3 == 1) { ++blockedCalls; return CURLE_AGAIN; }
    const bool first = p.remaining == 0;
    if(!first && length != p.remaining) {
        std::fprintf(stderr, "PARTIAL_PROBE wrong remainder: %zu expected %zu\n", length, p.remaining);
        std::abort();
    }
    // curl itself gets explicit frame chunks; its return is passed to the real queue.
    const size_t chunk = std::min<size_t>(length, 4096);
    const CURLcode result = curl_ws_send(curl, data, chunk, sent,
        first ? static_cast<curl_off_t>(length) : 0, flags | CURLWS_OFFSET);
    if(result == CURLE_OK && *sent) {
        if(first) p.remaining = length;
        p.remaining -= *sent;
        if(*sent < length) ++partialAccepts;
        if(p.remaining == 0) ++completedFrames;
    }
    return result;
}
