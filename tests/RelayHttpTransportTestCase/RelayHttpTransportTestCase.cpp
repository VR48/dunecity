/*
 *  RelayHttpTransportTestCase.cpp - the HTTPS polling transport, end to end minus the socket.
 *
 *  The relay wire harness checks the batch codec and the relay session tests check what the
 *  session does with decoded events. Neither can reach what is between them here: the request
 *  sequencing, the retry rules, the pacing, and the promise that a frame is delivered exactly
 *  once and never before the batch carrying it has been validated in full.
 *
 *  Everything below drives the production RelayHttpTransport through a scripted backend with a
 *  virtual clock (ScriptedRelayHttpBackend.h). Nothing here is a hook: the transport reaches its
 *  HTTP client and its clock through the same interface the two real backends implement.
 */

#include <catch2/catch_all.hpp>

#include "ScriptedRelayHttpBackend.h"

#include <Network/RelayHttpTransport.h>
#include <Network/RelayPollProtocol.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

const std::string kBaseUrl = "https://dunelegacy.com/relay/v1/poll";
const std::string kToken(64, 'a');

/// The transport plus the backend it owns; the raw pointer is how the test drives it.
struct PollHarness {
    ScriptedRelayHttpBackend*           backend = nullptr;
    std::unique_ptr<RelayHttpTransport> transport;
};

PollHarness makeHarness(std::uint32_t firstSequence = 1, const std::string& url = kBaseUrl) {
    auto owned = std::make_unique<ScriptedRelayHttpBackend>();
    auto* backend = owned.get();
    PollHarness harness;
    harness.transport = std::make_unique<RelayHttpTransport>(url, std::move(owned), firstSequence);
    harness.backend = backend;
    return harness;
}

std::string openBody(const std::string& token) { return token + "\n"; }

/// Answers /open and pumps, leaving the transport open with nothing in flight.
void openSession(PollHarness& harness, const std::string& token = kToken) {
    harness.backend->completeWith(200, openBody(token));
    harness.transport->pump();
}

std::vector<std::uint8_t> frameOf(std::uint8_t marker, std::size_t length = 8) {
    return std::vector<std::uint8_t>(length, marker);
}

/**
    Builds a gateway answer.

    `declaredFrameCount` and `declaredLengths` exist so a test can say something the bytes do not
    support - a count of 65, a length past the end - which is exactly the class of answer the
    parser has to refuse.
*/
std::vector<std::uint8_t> buildResponse(std::uint32_t sequence, std::uint16_t closeCode,
                                        const std::vector<std::vector<std::uint8_t>>& frames,
                                        int declaredFrameCount = -1,
                                        const std::vector<std::uint32_t>& declaredLengths = {}) {
    std::vector<std::uint8_t> body;
    body.insert(body.end(), RoomPoll::kResponseMagic, RoomPoll::kResponseMagic + 4);
    RoomPoll::appendUint32LE(body, sequence);
    RoomPoll::appendUint16LE(body, closeCode);
    RoomPoll::appendUint16LE(body, static_cast<std::uint16_t>(
        declaredFrameCount >= 0 ? declaredFrameCount : static_cast<int>(frames.size())));
    for(std::size_t index = 0; index < frames.size(); ++index) {
        const std::uint32_t declared = (index < declaredLengths.size())
            ? declaredLengths[index] : static_cast<std::uint32_t>(frames[index].size());
        RoomPoll::appendUint32LE(body, declared);
        body.insert(body.end(), frames[index].begin(), frames[index].end());
    }
    return body;
}

struct DecodedBatch {
    std::uint32_t sequence = 0;
    std::vector<std::vector<std::uint8_t>> frames;
};

/// Reads a client request back, so the test asserts on the bytes that were actually sent.
bool decodeRequest(const std::vector<std::uint8_t>& body, DecodedBatch& out) {
    out = DecodedBatch();
    if(body.size() < RoomPoll::Limits::kRequestHeaderBytes) return false;
    for(std::size_t index = 0; index < 4; ++index) {
        if(body[index] != static_cast<std::uint8_t>(RoomPoll::kRequestMagic[index])) return false;
    }
    out.sequence = RoomPoll::readUint32LE(body.data() + 4);
    const std::uint32_t count = RoomPoll::readUint32LE(body.data() + 8);
    std::size_t position = RoomPoll::Limits::kRequestHeaderBytes;
    for(std::uint32_t index = 0; index < count; ++index) {
        if((body.size() - position) < 4) return false;
        const std::size_t length = RoomPoll::readUint32LE(body.data() + position);
        position += 4;
        if((body.size() - position) < length) return false;
        out.frames.emplace_back(body.begin() + static_cast<std::ptrdiff_t>(position),
                                body.begin() + static_cast<std::ptrdiff_t>(position + length));
        position += length;
    }
    return position == body.size();
}

std::vector<std::vector<std::uint8_t>> drain(RelayHttpTransport& transport) {
    std::vector<std::vector<std::uint8_t>> received;
    std::vector<std::uint8_t> frame;
    while(transport.receive(frame)) {
        received.push_back(frame);
    }
    return received;
}

/// Answers the exchange in flight and lets the transport consume it.
void answerExchange(PollHarness& harness, const std::vector<std::uint8_t>& body,
                    long status = 200) {
    REQUIRE(harness.backend->inFlight);
    harness.backend->completeWith(status, body);
    harness.transport->pump();
}

/// Waits out the pacing interval and starts the next exchange.
void startNextExchange(PollHarness& harness) {
    harness.backend->advance(RoomPoll::Timing::kMinExchangeIntervalMs);
    harness.transport->pump();
}

} // namespace

// -------------------------------------------------------------------------------------------
// Opening
// -------------------------------------------------------------------------------------------

TEST_CASE("the poll transport opens with an empty POST and no credential", "[relay][http]") {
    PollHarness harness = makeHarness();

    REQUIRE(harness.backend->attemptCount() == 1);
    const auto& open = harness.backend->lastAttempt().request;
    CHECK(open.kind == RelayHttpRequest::Kind::Open);
    CHECK(open.url == "https://dunelegacy.com/relay/v1/poll/open");
    CHECK(open.body.empty());
    CHECK(open.sessionToken.empty());
    CHECK(open.timeoutMs == RoomPoll::Timing::kRequestTimeoutMs);

    // Nothing may be sent before /open has succeeded, which is what keeps HELLO behind it.
    CHECK(harness.transport->state() == RelayWebSocket::State::Connecting);
    harness.transport->pump();
    CHECK(harness.transport->state() == RelayWebSocket::State::Connecting);
    CHECK(harness.backend->attemptCount() == 1);

    openSession(harness);
    CHECK(harness.transport->state() == RelayWebSocket::State::Open);
    // Still nothing sent: the exchange that carries HELLO starts when HELLO is queued.
    CHECK(harness.backend->attemptCount() == 1);
    CHECK(harness.backend->overlappingStarts == 0);
}

TEST_CASE("a session token is accepted only in its exact form", "[relay][http][security]") {
    const std::string good(64, 'b');

    const std::vector<std::string> refused = {
        good,                               // no trailing LF
        good + "\r\n",                      // CRLF
        good + "\n\n",                      // one byte too many
        good.substr(0, 63) + "\n",          // too short
        good + "c\n",                       // too long
        std::string(64, 'A') + "\n",        // uppercase hex
        std::string(64, 'z') + "\n",        // not hex at all
        std::string("\n"),
        std::string()
    };

    for(const std::string& body : refused) {
        PollHarness harness = makeHarness();
        harness.backend->completeWith(200, body);
        harness.transport->pump();
        CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
        CHECK(harness.transport->closeCode() == RoomRelay::Close::ProtocolError);
        CHECK_FALSE(harness.transport->lastError().empty());
    }

    PollHarness accepted = makeHarness();
    accepted.backend->completeWith(200, openBody(good));
    accepted.transport->pump();
    CHECK(accepted.transport->state() == RelayWebSocket::State::Open);
}

TEST_CASE("the open request is never retried", "[relay][http]") {
    SECTION("a network failure is final") {
        PollHarness harness = makeHarness();
        harness.backend->failWithNetworkError();
        harness.transport->pump();

        CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
        for(int index = 0; index < 10; ++index) {
            harness.backend->advance(1000);
            harness.transport->pump();
        }
        CHECK(harness.backend->attemptCount() == 1);
    }

    SECTION("a server error is final too") {
        PollHarness harness = makeHarness();
        harness.backend->completeWith(503, std::string());
        harness.transport->pump();

        CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
        harness.backend->advance(5000);
        harness.transport->pump();
        CHECK(harness.backend->attemptCount() == 1);
    }
}

TEST_CASE("the session token travels in a header and never in a URL", "[relay][http][security]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x11)));

    REQUIRE(harness.backend->attemptCount() == 2);
    const auto& exchange = harness.backend->lastAttempt().request;
    CHECK(exchange.kind == RelayHttpRequest::Kind::Exchange);
    CHECK(exchange.url == "https://dunelegacy.com/relay/v1/poll/exchange");
    CHECK(exchange.url.find(kToken) == std::string::npos);
    CHECK(exchange.sessionToken == kToken);
}

// -------------------------------------------------------------------------------------------
// Exchanges
// -------------------------------------------------------------------------------------------

TEST_CASE("frames are carried in order and delivered exactly once", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    // The first frame starts an exchange straight away rather than waiting for a pump, so it is
    // alone in that batch. Anything queued while it is in flight rides in the next one, in the
    // order it was queued.
    REQUIRE(harness.transport->send(frameOf(0x01)));
    REQUIRE(harness.transport->send(frameOf(0x02)));
    REQUIRE(harness.transport->send(frameOf(0x03)));
    REQUIRE(harness.backend->attemptCount() == 2);

    DecodedBatch first;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, first));
    CHECK(first.sequence == 1);
    REQUIRE(first.frames.size() == 1);
    CHECK(first.frames[0] == frameOf(0x01));

    answerExchange(harness, buildResponse(1, 0, {frameOf(0xAA), frameOf(0xBB)}));

    const auto received = drain(*harness.transport);
    REQUIRE(received.size() == 2);
    CHECK(received[0] == frameOf(0xAA));
    CHECK(received[1] == frameOf(0xBB));
    CHECK(drain(*harness.transport).empty());

    startNextExchange(harness);
    DecodedBatch second;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, second));
    CHECK(second.sequence == 2);
    REQUIRE(second.frames.size() == 2);
    CHECK(second.frames[0] == frameOf(0x02));
    CHECK(second.frames[1] == frameOf(0x03));

    CHECK(harness.transport->state() == RelayWebSocket::State::Open);
    CHECK(harness.backend->overlappingStarts == 0);
}

TEST_CASE("a batch carries at most sixty-four frames", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    for(int index = 0; index < 100; ++index) {
        REQUIRE(harness.transport->send(frameOf(static_cast<std::uint8_t>(index))));
    }

    // Frame 0 left immediately; the other ninety-nine were queued behind it.
    DecodedBatch first;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, first));
    REQUIRE(first.frames.size() == 1);

    answerExchange(harness, buildResponse(1, 0, {}));
    startNextExchange(harness);

    DecodedBatch second;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, second));
    CHECK(second.sequence == 2);
    CHECK(second.frames.size() == RoomPoll::Limits::kMaxFramesPerBatch);
    CHECK(second.frames.front() == frameOf(1));

    answerExchange(harness, buildResponse(2, 0, {}));
    startNextExchange(harness);

    DecodedBatch third;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, third));
    CHECK(third.sequence == 3);
    CHECK(third.frames.size() == 35);
    CHECK(third.frames.front() == frameOf(65));
}

TEST_CASE("exchanges are paced at no more than forty a second", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    SECTION("the interval is measured from the start of the previous request") {
        REQUIRE(harness.transport->send(frameOf(0x01)));
        answerExchange(harness, buildResponse(1, 0, {}));

        harness.backend->advance(RoomPoll::Timing::kMinExchangeIntervalMs - 1);
        harness.transport->pump();
        CHECK(harness.backend->attemptCount() == 2);

        harness.backend->advance(1);
        harness.transport->pump();
        CHECK(harness.backend->attemptCount() == 3);
    }

    SECTION("a second of continuous exchanging stays within the budget") {
        unsigned pumps = 0;
        for(unsigned elapsed = 0; elapsed < 1000; ++elapsed) {
            harness.transport->pump();
            if(harness.backend->inFlight) {
                harness.backend->completeWith(
                    200, buildResponse(harness.transport->sequence(), 0, {}));
                harness.transport->pump();
            }
            harness.backend->advance(1);
            pumps++;
        }
        CHECK(pumps == 1000);
        // One of the attempts is /open.
        const std::size_t exchanges = harness.backend->attemptCount() - 1;
        CHECK(exchanges <= std::size_t{40});
        CHECK(exchanges >= std::size_t{39});
        CHECK(harness.backend->overlappingStarts == 0);
    }
}

TEST_CASE("the sequence advances only when a response was accepted", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    REQUIRE(harness.transport->send(frameOf(0x01)));
    CHECK(harness.transport->sequence() == 1);

    harness.backend->failWithNetworkError();
    harness.transport->pump();
    CHECK(harness.transport->sequence() == 1);

    harness.backend->advance(RoomPoll::Timing::kFirstRetryDelayMs);
    harness.transport->pump();
    answerExchange(harness, buildResponse(1, 0, {}));
    CHECK(harness.transport->sequence() == 2);
}

TEST_CASE("a session that would wrap the sequence ends instead", "[relay][http]") {
    PollHarness harness = makeHarness(0xFFFFFFFFu);
    openSession(harness);

    REQUIRE(harness.transport->send(frameOf(0x01)));
    DecodedBatch sent;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, sent));
    CHECK(sent.sequence == 0xFFFFFFFFu);

    answerExchange(harness, buildResponse(0xFFFFFFFFu, 0, {frameOf(0xCC)}));

    // The last answer is still delivered in full: the session ends, nothing is dropped.
    const auto received = drain(*harness.transport);
    REQUIRE(received.size() == 1);
    CHECK(received[0] == frameOf(0xCC));

    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
    CHECK(harness.transport->sequence() == 0xFFFFFFFFu);
    CHECK_FALSE(harness.transport->lastError().empty());
}

// -------------------------------------------------------------------------------------------
// Malformed answers
// -------------------------------------------------------------------------------------------

TEST_CASE("a malformed batch ends the session and delivers nothing", "[relay][http][security]") {
    struct Case {
        const char* what;
        std::vector<std::uint8_t> body;
    };

    const auto valid = buildResponse(1, 0, {frameOf(0xAA)});

    std::vector<Case> cases;
    cases.push_back({"wrong magic", [&] {
        auto body = valid; body[0] = 'X'; return body;
    }()});
    cases.push_back({"header cut short", std::vector<std::uint8_t>(valid.begin(),
                                                                   valid.begin() + 11)});
    cases.push_back({"a different sequence", buildResponse(2, 0, {frameOf(0xAA)})});
    cases.push_back({"an invalid close code", buildResponse(1, 42, {frameOf(0xAA)})});
    cases.push_back({"a close code from the WebSocket abnormal range",
                     buildResponse(1, 1006, {frameOf(0xAA)})});
    cases.push_back({"more frames than a batch may hold",
                     buildResponse(1, 0, {frameOf(0xAA)}, 65)});
    cases.push_back({"a count the bytes do not support",
                     buildResponse(1, 0, {frameOf(0xAA)}, 2)});
    cases.push_back({"a zero-length frame",
                     buildResponse(1, 0, {frameOf(0xAA)}, 1, {0})});
    cases.push_back({"a frame longer than the ceiling",
                     buildResponse(1, 0, {frameOf(0xAA)}, 1,
                                   {static_cast<std::uint32_t>(
                                        RoomPoll::Limits::kMaxFrameBytes + 1)})});
    cases.push_back({"a frame length past the end of the batch",
                     buildResponse(1, 0, {frameOf(0xAA)}, 1, {4096})});
    cases.push_back({"trailing bytes", [&] {
        auto body = valid; body.push_back(0); return body;
    }()});
    cases.push_back({"a batch above the response ceiling", [&] {
        auto body = valid;
        body.resize(RoomPoll::Limits::kMaxResponseBytes + 1, 0);
        return body;
    }()});

    for(const Case& testCase : cases) {
        INFO(testCase.what);
        PollHarness harness = makeHarness();
        openSession(harness);
        REQUIRE(harness.transport->send(frameOf(0x01)));

        answerExchange(harness, testCase.body);

        CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
        CHECK(harness.transport->closeCode() == RoomRelay::Close::ProtocolError);
        CHECK(drain(*harness.transport).empty());

        // A refused answer is not a network failure, so it is never repeated.
        harness.backend->advance(5000);
        harness.transport->pump();
        CHECK(harness.backend->attemptCount() == 2);
    }
}

TEST_CASE("a good batch is delivered whole or not at all", "[relay][http][security]") {
    // Five answers of just under a megabyte each; the fourth fills the inbound budget and the
    // fifth cannot be held. Nothing from the refused one may reach the caller.
    PollHarness harness = makeHarness();
    openSession(harness);

    const std::vector<std::uint8_t> chunk(65536, 0x5A);
    std::vector<std::vector<std::uint8_t>> frames(15, chunk);

    std::size_t delivered = 0;
    bool refused = false;
    for(std::uint32_t round = 1; round <= 5 && !refused; ++round) {
        harness.transport->pump();
        REQUIRE(harness.backend->inFlight);
        harness.backend->completeWith(200, buildResponse(round, 0, frames));
        harness.transport->pump();

        if(harness.transport->state() == RelayWebSocket::State::Closed) {
            refused = true;
            break;
        }
        // Deliberately not drained: the point is what happens when the game loop falls behind.
        delivered += frames.size();
        harness.backend->advance(RoomPoll::Timing::kMinExchangeIntervalMs);
    }

    CHECK(refused);
    CHECK(harness.transport->closeCode() == RoomRelay::Close::SlowConsumer);
    // Exactly the frames from the batches that were accepted, and none from the one that was not.
    CHECK(drain(*harness.transport).size() == delivered);
}

// -------------------------------------------------------------------------------------------
// Retries
// -------------------------------------------------------------------------------------------

TEST_CASE("a retry repeats the same bytes and picks up nothing new", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    REQUIRE(harness.transport->send(frameOf(0x01)));
    const std::vector<std::uint8_t> original = harness.backend->lastAttempt().request.body;

    harness.backend->failWithNetworkError();
    harness.transport->pump();
    CHECK(harness.transport->retryCount() == 1);

    // Queued during the retry window. It must not be folded into the repeated request: the
    // server answers a repeat from its stored copy and would never look at the addition.
    REQUIRE(harness.transport->send(frameOf(0x02)));
    CHECK(harness.backend->attemptCount() == 2);

    harness.backend->advance(RoomPoll::Timing::kFirstRetryDelayMs - 1);
    harness.transport->pump();
    CHECK(harness.backend->attemptCount() == 2);

    harness.backend->advance(1);
    harness.transport->pump();
    REQUIRE(harness.backend->attemptCount() == 3);
    CHECK(harness.backend->lastAttempt().request.body == original);
    CHECK(harness.backend->lastAttempt().request.sessionToken == kToken);

    // The stored answer arrives once, and its frames are delivered once.
    answerExchange(harness, buildResponse(1, 0, {frameOf(0xAA)}));
    CHECK(drain(*harness.transport).size() == 1);
    CHECK(harness.transport->retryCount() == 0);

    // Only now does the frame queued during the retry go out, in a fresh exchange.
    startNextExchange(harness);
    DecodedBatch next;
    REQUIRE(decodeRequest(harness.backend->lastAttempt().request.body, next));
    CHECK(next.sequence == 2);
    REQUIRE(next.frames.size() == 1);
    CHECK(next.frames[0] == frameOf(0x02));
}

TEST_CASE("the retry delays are a hundred and then two hundred and fifty", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x01)));

    const std::uint32_t firstStart = harness.backend->lastAttempt().startedAtMs;

    harness.backend->failWithNetworkError();
    harness.transport->pump();
    harness.backend->advance(RoomPoll::Timing::kFirstRetryDelayMs);
    harness.transport->pump();
    REQUIRE(harness.backend->attemptCount() == 3);
    CHECK(harness.backend->lastAttempt().startedAtMs - firstStart
          == RoomPoll::Timing::kFirstRetryDelayMs);

    const std::uint32_t secondStart = harness.backend->lastAttempt().startedAtMs;
    harness.backend->failWithNetworkError();
    harness.transport->pump();
    harness.backend->advance(RoomPoll::Timing::kSecondRetryDelayMs - 1);
    harness.transport->pump();
    CHECK(harness.backend->attemptCount() == 3);
    harness.backend->advance(1);
    harness.transport->pump();
    REQUIRE(harness.backend->attemptCount() == 4);
    CHECK(harness.backend->lastAttempt().startedAtMs - secondStart
          == RoomPoll::Timing::kSecondRetryDelayMs);
    CHECK(harness.transport->retryCount() == 2);
}

TEST_CASE("a request is retried twice and no more", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x01)));

    harness.backend->failWithNetworkError();
    harness.transport->pump();
    harness.backend->advance(RoomPoll::Timing::kFirstRetryDelayMs);
    harness.transport->pump();

    harness.backend->failWithNetworkError();
    harness.transport->pump();
    harness.backend->advance(RoomPoll::Timing::kSecondRetryDelayMs);
    harness.transport->pump();

    REQUIRE(harness.backend->attemptCount() == 4);
    harness.backend->failWithNetworkError();
    harness.transport->pump();

    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
    harness.backend->advance(5000);
    harness.transport->pump();
    CHECK(harness.backend->attemptCount() == 4);
}

TEST_CASE("only the transient statuses are retried", "[relay][http]") {
    const std::vector<long> retried  = {429, 502, 503, 504};
    const std::vector<long> notRetried = {400, 401, 403, 404, 409, 413, 418, 500, 505};

    for(long status : retried) {
        INFO("status " << status);
        PollHarness harness = makeHarness();
        openSession(harness);
        REQUIRE(harness.transport->send(frameOf(0x01)));
        const std::vector<std::uint8_t> original =
            harness.backend->lastAttempt().request.body;

        harness.backend->completeWith(status, std::string());
        harness.transport->pump();
        CHECK(harness.transport->state() == RelayWebSocket::State::Open);

        harness.backend->advance(RoomPoll::Timing::kFirstRetryDelayMs);
        harness.transport->pump();
        REQUIRE(harness.backend->attemptCount() == 3);
        CHECK(harness.backend->lastAttempt().request.body == original);
    }

    for(long status : notRetried) {
        INFO("status " << status);
        PollHarness harness = makeHarness();
        openSession(harness);
        REQUIRE(harness.transport->send(frameOf(0x01)));

        harness.backend->completeWith(status, std::string());
        harness.transport->pump();
        CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
        CHECK_FALSE(harness.transport->lastError().empty());

        harness.backend->advance(5000);
        harness.transport->pump();
        CHECK(harness.backend->attemptCount() == 2);
    }
}

TEST_CASE("a failure a retry cannot fix is not retried", "[relay][http][security]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x01)));

    // This is what a refused certificate arrives as.
    harness.backend->failFatally("The game service certificate could not be verified.");
    harness.transport->pump();

    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
    CHECK(harness.transport->lastError() == "The game service certificate could not be verified.");
    harness.backend->advance(5000);
    harness.transport->pump();
    CHECK(harness.backend->attemptCount() == 2);
}

// -------------------------------------------------------------------------------------------
// Backlog, closing and shutdown
// -------------------------------------------------------------------------------------------

TEST_CASE("the backlog counts bytes that are still in flight", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    CHECK(harness.transport->outgoingBacklogBytes() == 0);

    REQUIRE(harness.transport->send(frameOf(0x01, 100)));
    CHECK(harness.transport->outgoingBacklogBytes() == 100);
    CHECK(harness.transport->inFlightFrameCount() == 1);

    // Queued behind the exchange that is already in flight.
    REQUIRE(harness.transport->send(frameOf(0x02, 50)));
    CHECK(harness.transport->outgoingBacklogBytes() == 150);

    answerExchange(harness, buildResponse(1, 0, {}));
    CHECK(harness.transport->outgoingBacklogBytes() == 50);
    CHECK(harness.transport->inFlightFrameCount() == 0);

    startNextExchange(harness);
    answerExchange(harness, buildResponse(2, 0, {}));
    CHECK(harness.transport->outgoingBacklogBytes() == 0);
}

TEST_CASE("a close code arrives behind the frames it follows", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x01)));

    answerExchange(harness, buildResponse(1, RoomRelay::Close::HostLeft,
                                          {frameOf(0xAA), frameOf(0xBB)}));

    const auto received = drain(*harness.transport);
    REQUIRE(received.size() == 2);
    CHECK(received[0] == frameOf(0xAA));
    CHECK(received[1] == frameOf(0xBB));

    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
    CHECK(harness.transport->closeCode() == RoomRelay::Close::HostLeft);

    // The server has already ended the session, so there is nothing to tell it.
    harness.transport->close(RoomRelay::Close::Normal, "leaving");
    CHECK(harness.backend->attemptCount() == 2);
}

TEST_CASE("closing posts an empty body once, with the session header", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    REQUIRE(harness.transport->send(frameOf(0x01)));
    REQUIRE(harness.backend->attemptCount() == 2);

    harness.transport->close(RoomRelay::Close::Normal, "leaving");

    REQUIRE(harness.backend->attemptCount() == 3);
    const auto& request = harness.backend->lastAttempt().request;
    CHECK(request.kind == RelayHttpRequest::Kind::Close);
    CHECK(request.url == "https://dunelegacy.com/relay/v1/poll/close");
    CHECK(request.body.empty());
    CHECK(request.sessionToken == kToken);
    // The exchange that was in flight was abandoned rather than left racing the close.
    CHECK(harness.backend->cancelsWhileInFlight == 1);
    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);

    harness.transport->close(RoomRelay::Close::Normal, "leaving");
    CHECK(harness.backend->attemptCount() == 3);
}

TEST_CASE("closing before the session opened tells the server nothing", "[relay][http]") {
    PollHarness harness = makeHarness();
    harness.transport->close(RoomRelay::Close::Normal, "leaving");

    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);
    CHECK(harness.backend->attemptCount() == 1);
    CHECK(harness.backend->cancelsWhileInFlight == 1);
}

TEST_CASE("destroying the transport cancels what was in flight", "[relay][http]") {
    auto tally = std::make_shared<int>(0);

    {
        auto owned = std::make_unique<ScriptedRelayHttpBackend>();
        owned->cancelTally = tally;
        auto transport = std::make_unique<RelayHttpTransport>(kBaseUrl, std::move(owned));
        // Destroyed with the /open request still outstanding.
    }
    CHECK(*tally == 1);

    *tally = 0;
    {
        auto owned = std::make_unique<ScriptedRelayHttpBackend>();
        owned->cancelTally = tally;
        auto* backend = owned.get();
        auto transport = std::make_unique<RelayHttpTransport>(kBaseUrl, std::move(owned));
        backend->completeWith(200, openBody(kToken));
        transport->pump();
        REQUIRE(transport->send(frameOf(0x01)));
        REQUIRE(backend->inFlight);
    }
    CHECK(*tally == 1);
}

TEST_CASE("a backend that cannot start a request fails the session", "[relay][http]") {
    auto owned = std::make_unique<ScriptedRelayHttpBackend>();
    owned->refuseStart = true;
    RelayHttpTransport transport(kBaseUrl, std::move(owned));

    CHECK(transport.state() == RelayWebSocket::State::Closed);
    CHECK_FALSE(transport.lastError().empty());
    transport.pump();
    CHECK(transport.state() == RelayWebSocket::State::Closed);
}

TEST_CASE("a send after the session ended is refused", "[relay][http]") {
    PollHarness harness = makeHarness();
    openSession(harness);
    harness.transport->close(RoomRelay::Close::Normal, "leaving");
    CHECK_FALSE(harness.transport->send(frameOf(0x01)));
}

TEST_CASE("an oversized frame is refused rather than truncated", "[relay][http][security]") {
    PollHarness harness = makeHarness();
    openSession(harness);

    CHECK_FALSE(harness.transport->send(std::vector<std::uint8_t>()));
    CHECK(harness.transport->state() == RelayWebSocket::State::Closed);

    PollHarness other = makeHarness();
    openSession(other);
    CHECK_FALSE(other.transport->send(
        std::vector<std::uint8_t>(RoomPoll::Limits::kMaxFrameBytes + 1, 0x7F)));
    CHECK(other.transport->state() == RelayWebSocket::State::Closed);
}

// -------------------------------------------------------------------------------------------
// Endpoints
// -------------------------------------------------------------------------------------------

TEST_CASE("the transport is chosen by the endpoint scheme", "[relay][http]") {
    CHECK(relayTransportKindForUrl("https://dunelegacy.com/relay/v1/poll")
          == RelayTransportKind::HttpPolling);
    CHECK(relayTransportKindForUrl("http://127.0.0.1:8787/relay/v1/poll")
          == RelayTransportKind::HttpPolling);
    CHECK(relayTransportKindForUrl("wss://relay.example.net/v1/socket")
          == RelayTransportKind::WebSocket);
    CHECK(relayTransportKindForUrl("ws://127.0.0.1:8787/v1/socket")
          == RelayTransportKind::WebSocket);
}

TEST_CASE("poll endpoints are built without doubling a slash", "[relay][http]") {
    CHECK(RoomPoll::pollEndpointUrl("https://dunelegacy.com/relay/v1/poll", "/open")
          == "https://dunelegacy.com/relay/v1/poll/open");
    CHECK(RoomPoll::pollEndpointUrl("https://dunelegacy.com/relay/v1/poll/", "/exchange")
          == "https://dunelegacy.com/relay/v1/poll/exchange");
    CHECK(RoomPoll::pollEndpointUrl("https://dunelegacy.com/relay/v1/poll///", "/close")
          == "https://dunelegacy.com/relay/v1/poll/close");

    PollHarness harness = makeHarness(1, "https://dunelegacy.com/relay/v1/poll/");
    CHECK(harness.backend->lastAttempt().request.url
          == "https://dunelegacy.com/relay/v1/poll/open");
}
