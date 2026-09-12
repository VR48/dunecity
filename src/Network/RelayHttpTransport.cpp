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
    The platform-independent half of the HTTPS polling transport; see
    include/Network/RelayHttpTransport.h for what it promises and why it is split this way.

    There is deliberately no SDL, no libcurl, no Emscripten and no logging in this file. The
    clock comes from the backend so the tests can drive it, and nothing here can print a session
    token by accident because nothing here prints at all.
*/

#include <Network/RelayHttpTransport.h>

#include <utility>

namespace {

/**
    How long past the backend's own request timeout the transport waits before giving up on it.

    The backends enforce RoomPoll::Timing::kRequestTimeoutMs themselves. This is the guard for
    the case where one does not - a browser fetch whose promise is never settled because the tab
    was frozen mid-flight, say. Without it the session would sit with one request in flight
    forever and look, from the game's side, exactly like a relay that had gone quiet.
*/
constexpr std::uint32_t kBackendWatchdogGraceMs = 2000;

} // namespace

RelayHttpTransport::RelayHttpTransport(const std::string& baseUrl,
                                       std::unique_ptr<RelayHttpBackend> backend,
                                       std::uint32_t firstSequence)
 : backend_(std::move(backend)),
   openUrl_(RoomPoll::pollEndpointUrl(baseUrl, "/open")),
   exchangeUrl_(RoomPoll::pollEndpointUrl(baseUrl, "/exchange")),
   closeUrl_(RoomPoll::pollEndpointUrl(baseUrl, "/close")),
   sequence_(firstSequence) {
    if(!backend_) {
        fail("The game could not start a network connection.");
        return;
    }

    RelayHttpRequest request;
    request.kind      = RelayHttpRequest::Kind::Open;
    request.url       = openUrl_;
    request.timeoutMs = RoomPoll::Timing::kRequestTimeoutMs;

    lastRequestStartMs_ = backend_->nowMs();
    if(!backend_->start(request)) {
        fail("The game could not reach the game service.");
        return;
    }
    requestInFlight_ = true;
}

RelayHttpTransport::~RelayHttpTransport() {
    // Whatever is in flight is abandoned here, before any member it might write through goes
    // away. The backends make this safe in their own way: the native one removes the easy
    // handle from its multi handle, the browser one detaches the JavaScript request from this
    // object entirely, so a late answer has nothing left to reach.
    if(backend_) {
        backend_->cancel();
    }
    requestInFlight_ = false;
}

// -------------------------------------------------------------------------------------------
// Driving
// -------------------------------------------------------------------------------------------

void RelayHttpTransport::pump() {
    if(!backend_) {
        return;
    }

    switch(stage_) {
        case Stage::Opening:  pumpOpen();     break;
        case Stage::Running:  pumpExchange(); break;
        case Stage::Finished: pumpClose();    break;
    }
}

void RelayHttpTransport::pumpOpen() {
    if(!requestInFlight_) {
        return;
    }

    RelayHttpOutcome outcome;
    if(!backend_->poll(outcome)) {
        const std::uint32_t elapsed = backend_->nowMs() - lastRequestStartMs_;
        if(elapsed > (RoomPoll::Timing::kRequestTimeoutMs + kBackendWatchdogGraceMs)) {
            backend_->cancel();
            requestInFlight_ = false;
            closeCode_ = RoomRelay::Close::Timeout;
            fail("The game service did not answer in time.");
        }
        return;
    }
    requestInFlight_ = false;

    // No automatic retry, by contract. Opening a session is the one request whose failure the
    // player has to see: a transport that quietly tried again would turn a broken endpoint into
    // a menu that hangs.
    if(outcome.kind != RelayHttpOutcome::Kind::Completed) {
        fail(outcome.error.empty() ? std::string("The game service could not be reached.")
                                   : outcome.error);
        return;
    }
    if(outcome.status != 200) {
        fail(describeHttpStatus(outcome.status));
        return;
    }

    std::string token;
    if(!RoomPoll::parseOpenResponse(outcome.body.data(), outcome.body.size(), token)) {
        closeCode_ = RoomRelay::Close::ProtocolError;
        fail("The game service sent an unusable answer.");
        return;
    }

    sessionToken_ = token;
    state_ = State::Open;
    stage_ = Stage::Running;

    // No exchange is started here on purpose. The session sends its HELLO as soon as it sees
    // the transport open, and send() starts the exchange that carries it - so the handshake
    // goes out in the first request rather than waiting behind an empty one.
}

void RelayHttpTransport::pumpExchange() {
    if(requestInFlight_) {
        RelayHttpOutcome outcome;
        if(!backend_->poll(outcome)) {
            const std::uint32_t elapsed = backend_->nowMs() - lastRequestStartMs_;
            if(elapsed > (RoomPoll::Timing::kRequestTimeoutMs + kBackendWatchdogGraceMs)) {
                backend_->cancel();
                requestInFlight_ = false;
                scheduleRetry("The game service did not answer in time.");
            }
            return;
        }
        requestInFlight_ = false;
        handleExchangeOutcome(outcome);
        if(state_ != State::Open) {
            return;
        }
    }

    maybeStartExchange();
}

void RelayHttpTransport::pumpClose() {
    // Only a best-effort /close can still be in flight here. Its answer is of no interest; this
    // exists so that a caller which keeps pumping gives the request a chance to complete.
    if(!requestInFlight_) {
        return;
    }
    RelayHttpOutcome outcome;
    if(backend_->poll(outcome)) {
        requestInFlight_ = false;
    }
}

// -------------------------------------------------------------------------------------------
// Exchanges
// -------------------------------------------------------------------------------------------

void RelayHttpTransport::maybeStartExchange() {
    if(state_ != State::Open || requestInFlight_ || !backend_) {
        return;
    }

    const std::uint32_t now = backend_->nowMs();

    if(retryPending_) {
        if((now - retryBaseMs_) < retryDelayMs_) {
            return;
        }
        retryPending_ = false;
        // requestBody_ still holds the exact bytes of the request being repeated, and
        // sequence_ has not moved. Nothing queued since is folded in: the server answers a
        // repeated request from its stored copy and would never look at the addition.
        RelayHttpRequest request;
        request.kind         = RelayHttpRequest::Kind::Exchange;
        request.url          = exchangeUrl_;
        request.sessionToken = sessionToken_;
        request.body         = requestBody_;
        request.timeoutMs    = RoomPoll::Timing::kRequestTimeoutMs;

        lastRequestStartMs_ = now;
        if(!backend_->start(request)) {
            fail("The game could not reach the game service.");
            return;
        }
        requestInFlight_ = true;
        return;
    }

    // At most forty requests a second per peer, measured from the start of the previous one.
    // The server holds an otherwise empty exchange for up to 100 ms and answers early when a
    // frame arrives, so a quiet session costs far less than that ceiling.
    if(everExchanged_ && (now - lastRequestStartMs_) < RoomPoll::Timing::kMinExchangeIntervalMs) {
        return;
    }

    startExchange();
}

bool RelayHttpTransport::startExchange() {
    // Take as much of the backlog as one batch may carry. The frames stay owned here until the
    // exchange is confirmed, both because a retry has to repeat them and because the caller's
    // backlog figure must keep counting them until they have actually arrived somewhere.
    inFlightFrames_.clear();
    std::size_t total = RoomPoll::Limits::kRequestHeaderBytes;
    while(!outgoing_.empty()
          && inFlightFrames_.size() < RoomPoll::Limits::kMaxFramesPerBatch) {
        const std::size_t cost = RoomPoll::Limits::kFrameLengthBytes + outgoing_.front().size();
        // Subtraction form; `total + cost` would wrap on wasm32.
        if(total > RoomPoll::Limits::kMaxRequestBytes - cost) {
            break;
        }
        total += cost;
        inFlightFrames_.push_back(std::move(outgoing_.front()));
        outgoing_.pop_front();
    }

    if(!RoomPoll::encodeExchangeRequest(sequence_, inFlightFrames_, requestBody_)) {
        // Unreachable unless the batch builder above and the encoder disagree about the limits.
        fail("The game tried to send a message that was too large.");
        return false;
    }

    RelayHttpRequest request;
    request.kind         = RelayHttpRequest::Kind::Exchange;
    request.url          = exchangeUrl_;
    request.sessionToken = sessionToken_;
    request.body         = requestBody_;
    request.timeoutMs    = RoomPoll::Timing::kRequestTimeoutMs;

    lastRequestStartMs_ = backend_->nowMs();
    everExchanged_ = true;
    if(!backend_->start(request)) {
        fail("The game could not reach the game service.");
        return false;
    }
    requestInFlight_ = true;
    return true;
}

void RelayHttpTransport::handleExchangeOutcome(RelayHttpOutcome& outcome) {
    if(outcome.kind == RelayHttpOutcome::Kind::NetworkFailure) {
        scheduleRetry(outcome.error);
        return;
    }
    if(outcome.kind == RelayHttpOutcome::Kind::Fatal) {
        fail(outcome.error.empty() ? std::string("The connection to the game was lost.")
                                   : outcome.error);
        return;
    }

    if(outcome.status != 200) {
        if(isRetryableStatus(outcome.status)) {
            scheduleRetry(describeHttpStatus(outcome.status));
        } else {
            // 4xx is the server saying the request itself is wrong. Repeating it would only ask
            // the same question again, and 401/403 in particular mean this session is over.
            if(outcome.status == 401 || outcome.status == 403) {
                closeCode_ = RoomRelay::Close::Unauthorized;
            }
            fail(describeHttpStatus(outcome.status));
        }
        return;
    }

    RoomPoll::ExchangeResponse response;
    RoomPoll::BatchError parseError = RoomPoll::BatchError::None;
    if(!RoomPoll::parseExchangeResponse(outcome.body.data(), outcome.body.size(), sequence_,
                                        response, parseError)) {
        // Everything the parser refuses is a protocol error, including a batch that answers a
        // different sequence number. Retrying is not on the table: the request was answered,
        // and the answer was not usable.
        closeCode_ = RoomRelay::Close::ProtocolError;
        fail("The game service sent an unusable answer.");
        return;
    }

    // The whole batch is admitted or none of it is, so a session that ends here has delivered
    // nothing out of a response it could not hold.
    if(!admitFrames(response.frames)) {
        closeCode_ = RoomRelay::Close::SlowConsumer;
        fail("The game fell too far behind the connection.");
        return;
    }

    // The request is now known to have arrived and been answered, so its frames can go.
    releaseInFlightFrames();
    requestBody_.clear();
    retries_ = 0;
    retryPending_ = false;

    if(response.closeCode != 0) {
        // The relay drains whatever it still had for this peer into the last batch, so those
        // frames are already queued above and receive() will hand them over before the caller
        // acts on the close.
        serverClosed_ = true;
        closeCode_ = response.closeCode;
        if(lastError_.empty()) {
            lastError_ = RoomRelay::describeCloseCode(response.closeCode);
        }
        state_ = State::Closed;
        stage_ = Stage::Finished;
        return;
    }

    if(sequence_ == 0xFFFFFFFFu) {
        // Four billion exchanges is over three years at forty a second, so this is not a case
        // anyone will meet. Wrapping the sequence would make a stale answer indistinguishable
        // from a current one, which is worth ending a session over.
        closeCode_ = RoomRelay::Close::Normal;
        fail("This game session has run for too long and must be restarted.");
        return;
    }
    ++sequence_;
}

void RelayHttpTransport::scheduleRetry(const std::string& error) {
    if(retries_ >= RoomPoll::Timing::kMaxRetries) {
        fail(error.empty() ? std::string("The connection to the game was lost.") : error);
        return;
    }

    retryDelayMs_ = (retries_ == 0) ? RoomPoll::Timing::kFirstRetryDelayMs
                                    : RoomPoll::Timing::kSecondRetryDelayMs;
    retries_++;
    retryPending_ = true;
    retryBaseMs_  = backend_->nowMs();
}

bool RelayHttpTransport::admitFrames(std::vector<std::vector<std::uint8_t>>& frames) {
    if(frames.empty()) {
        return true;
    }
    if(frames.size() > RoomRelay::Limits::kMaxQueuedFrames
       || incoming_.size() > RoomRelay::Limits::kMaxQueuedFrames - frames.size()) {
        return false;
    }

    std::size_t total = 0;
    for(const auto& frame : frames) {
        // Every length is already known to be at most kMaxFrameBytes, and the whole batch at
        // most a megabyte, so this subtraction cannot be made to wrap - but it is written in
        // the same form as everywhere else so it does not have to be reasoned about again.
        if(frame.size() > RoomRelay::Limits::kMaxIncomingQueueBytes - total) {
            return false;
        }
        total += frame.size();
    }
    if(incomingBytes_ > RoomRelay::Limits::kMaxIncomingQueueBytes - total) {
        return false;
    }

    for(auto& frame : frames) {
        incomingBytes_ += frame.size();
        incoming_.push_back(std::move(frame));
    }
    frames.clear();
    return true;
}

void RelayHttpTransport::releaseInFlightFrames() {
    for(const auto& frame : inFlightFrames_) {
        outgoingBytes_ -= (frame.size() < outgoingBytes_) ? frame.size() : outgoingBytes_;
    }
    inFlightFrames_.clear();
}

// -------------------------------------------------------------------------------------------
// RelayWebSocket
// -------------------------------------------------------------------------------------------

bool RelayHttpTransport::send(const std::vector<std::uint8_t>& frame) {
    if(state_ == State::Closed) {
        return false;
    }
    if(frame.empty() || frame.size() > RoomPoll::Limits::kMaxFrameBytes) {
        fail("The game tried to send a message that was too large.");
        return false;
    }
    // Subtraction form: frame.size() is already known to be at most kMaxFrameBytes, which is
    // well under the queue bound, so this cannot underflow and cannot wrap on wasm32.
    if(outgoingBytes_ > RoomRelay::Limits::kMaxOutgoingQueueBytes - frame.size()
       || outgoing_.size() >= RoomRelay::Limits::kMaxQueuedFrames) {
        // Refusing here and ending the session is deliberate, exactly as in the WebSocket
        // transports: quietly discarding a queued game message would desynchronise the match
        // instead of reporting a problem.
        fail("This computer could not keep up with the game connection.");
        return false;
    }

    outgoingBytes_ += frame.size();
    outgoing_.push_back(frame);

    // Start carrying it now if the pacing allows, so a message does not wait for the next game
    // loop iteration on top of the round trip it already has to make.
    if(state_ == State::Open) {
        maybeStartExchange();
    }
    return state_ != State::Closed;
}

bool RelayHttpTransport::receive(std::vector<std::uint8_t>& frame) {
    if(incoming_.empty()) {
        return false;
    }
    frame = std::move(incoming_.front());
    incoming_.pop_front();
    incomingBytes_ -= (frame.size() < incomingBytes_) ? frame.size() : incomingBytes_;
    return true;
}

void RelayHttpTransport::close(std::uint16_t code, const std::string& reason) {
    // The reason never goes on the wire: /close carries an empty body, and the session token in
    // the header is the whole of what the server needs to know.
    (void)reason;

    if(closeCode_ == 0) {
        closeCode_ = code;
    }
    state_ = State::Closed;
    stage_ = Stage::Finished;

    if(closeNotified_ || !backend_) {
        return;
    }
    closeNotified_ = true;

    if(requestInFlight_) {
        // One request in flight at a time, for the whole transport. Whatever the exchange would
        // have carried no longer matters.
        backend_->cancel();
        requestInFlight_ = false;
    }
    if(sessionToken_.empty() || serverClosed_) {
        // Either there is no session to release, or the server already ended it and telling it
        // again would only be another request it has to refuse.
        return;
    }

    RelayHttpRequest request;
    request.kind         = RelayHttpRequest::Kind::Close;
    request.url          = closeUrl_;
    request.sessionToken = sessionToken_;
    request.timeoutMs    = RoomPoll::Timing::kRequestTimeoutMs;

    // Best effort in the strict sense: it is dispatched, its answer is never read, and if the
    // caller drops the transport before it completes it goes with it. The server expires
    // abandoned sessions on its own; this only makes the common case immediate.
    if(backend_->start(request)) {
        requestInFlight_ = true;
        lastRequestStartMs_ = backend_->nowMs();
        RelayHttpOutcome outcome;
        if(backend_->poll(outcome)) {
            requestInFlight_ = false;
        }
    }
}

void RelayHttpTransport::fail(const std::string& message) {
    if(lastError_.empty()) {
        lastError_ = message;
    }
    state_ = State::Closed;
    stage_ = Stage::Finished;
    if(backend_ && requestInFlight_) {
        backend_->cancel();
        requestInFlight_ = false;
    }
}

bool RelayHttpTransport::isRetryableStatus(long status) {
    // 429 is included because the gateway uses it for a momentary rate ceiling rather than a
    // refusal; the bounded delay and the two-retry budget are what keep that from becoming a
    // hammer. Every other 4xx is the server rejecting the request itself, and repeating it
    // would only produce the same answer.
    return status == 429 || status == 502 || status == 503 || status == 504;
}

std::string RelayHttpTransport::describeHttpStatus(long status) {
    switch(status) {
        case 400:
        case 422:
            return "The game service could not understand the game.";
        case 401:
        case 403:
            return "The game service refused the connection.";
        case 404:
            return "That game is no longer available.";
        case 413:
            return "The game tried to send a message that was too large.";
        case 429:
            return "The game service is busy. Please try again in a moment.";
        case 502:
        case 503:
        case 504:
            return "The game service is temporarily unavailable.";
        default:
            break;
    }
    return "The game service could not be reached.";
}
