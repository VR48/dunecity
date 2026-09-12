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

#ifndef RELAYHTTPTRANSPORT_H
#define RELAYHTTPTRANSPORT_H

/**
    The relay transport that runs over ordinary HTTPS requests.

    This is the RelayWebSocket the relay session gets when the admission answer points at an
    https:// endpoint instead of a wss:// one. Everything above it - the handshake, heartbeats,
    room rules, the gameplay frames themselves - is unchanged; only the pipe is different.

    The split in this file is the point of it. All the behaviour that can be got wrong lives in
    RelayHttpTransport, which is platform-independent, has no SDL, no libcurl and no Emscripten
    in it, and gets both its clock and its HTTP from a RelayHttpBackend. The two backends
    (src/Network/RelayHttpTransportCurl.cpp, src/Network/RelayHttpTransportEmscripten.cpp) do
    nothing but issue one asynchronous request at a time and report what came back. A test
    backend does the same with a virtual clock, which is why the sequencing, the retry rules and
    the pacing can be tested at all rather than argued about.

    What the state machine guarantees, and what the tests pin down:

      - /open is attempted exactly once. There is no automatic retry: a session that cannot be
        opened is a visible failure, not a silent stall.
      - exactly one request is in flight at any moment, for the whole transport.
      - a retry re-sends the *same bytes* with the *same sequence number*. Data queued while a
        retry is pending waits for the next fresh exchange; it is never folded into a retry,
        because the server answers a repeated request from its stored copy and would never see
        the addition.
      - a response is validated in full before a single frame is delivered, and frames are
        delivered once, in order.
      - the sequence number only advances after a response was accepted, and a session that
        would wrap it ends instead.
*/

#include <Network/RelayPollProtocol.h>
#include <Network/RelayWebSocket.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

/// One HTTP request the transport wants performed.
struct RelayHttpRequest {
    enum class Kind : std::uint8_t {
        Open,       ///< POST /open, empty body, no session header
        Exchange,   ///< POST /exchange, a DHP1 batch
        Close       ///< POST /close, empty body; the answer is never read
    };

    Kind                      kind = Kind::Open;
    std::string               url;
    /// Goes into the `X-Dune-Session` header and nowhere else. Empty for Kind::Open.
    std::string               sessionToken;
    std::vector<std::uint8_t> body;
    std::uint32_t             timeoutMs = RoomPoll::Timing::kRequestTimeoutMs;
};

/// What became of it.
struct RelayHttpOutcome {
    enum class Kind : std::uint8_t {
        Completed,      ///< the server answered; `status` and `body` are what it said
        NetworkFailure, ///< no answer: connection lost, reset, or the request timed out
        Fatal           ///< something a retry cannot fix (certificate, refused URL, no memory)
    };

    Kind                      kind = Kind::Fatal;
    long                      status = 0;
    std::vector<std::uint8_t> body;
    /// Already player-facing and free of implementation detail; may be empty.
    std::string               error;
};

/**
    One asynchronous HTTP request at a time, plus a clock.

    Every method must return immediately. `poll()` is called from the game loop; a backend that
    blocks in it stalls the simulation, which is the failure this whole design exists to avoid.
*/
class RelayHttpBackend {
public:
    virtual ~RelayHttpBackend() = default;

    /// Milliseconds from an arbitrary origin. Must be monotonic within a session.
    virtual std::uint32_t nowMs() const = 0;

    /// Starts a request. Never called while another one is in flight.
    /// \return false if it could not even be started; the transport then fails the session.
    virtual bool start(const RelayHttpRequest& request) = 0;

    /// Drives the request in flight, if any.
    /// \return true, with `outcome` filled in, once it has finished
    virtual bool poll(RelayHttpOutcome& outcome) = 0;

    /// Abandons whatever is in flight. Must be safe to call at any time, including from the
    /// transport's destructor and when nothing is in flight.
    virtual void cancel() = 0;
};

/**
    The HTTPS polling transport.

    Owns the backend. Never blocks, never calls back into the game: like the WebSocket
    transports, everything is queued and drained by pump() from the game loop.
*/
class RelayHttpTransport final : public RelayWebSocket {
public:
    /**
        \param  baseUrl         the poll endpoint from the admission answer, already validated by
                                isAcceptableRelayUrl()
        \param  backend         the platform's HTTP client; must not be null
        \param  firstSequence   the sequence number the first exchange carries

        `firstSequence` is a protocol value, not a test hook: the contract fixes it at 1 and the
        one function that builds this transport in the game, createRelayHttpTransport(), always
        leaves it at 1. It is a parameter so that the behaviour at the top of the 32-bit range -
        where the session must end rather than wrap - can be exercised without four billion
        round trips.
    */
    RelayHttpTransport(const std::string& baseUrl, std::unique_ptr<RelayHttpBackend> backend,
                       std::uint32_t firstSequence = 1);
    ~RelayHttpTransport() override;

    RelayHttpTransport(const RelayHttpTransport&) = delete;
    RelayHttpTransport& operator=(const RelayHttpTransport&) = delete;

    void pump() override;
    State state() const override { return state_; }
    bool send(const std::vector<std::uint8_t>& frame) override;
    bool receive(std::vector<std::uint8_t>& frame) override;
    void close(std::uint16_t code, const std::string& reason) override;
    std::uint16_t closeCode() const override { return closeCode_; }
    const std::string& lastError() const override { return lastError_; }
    std::size_t outgoingBacklogBytes() const override { return outgoingBytes_; }

    // --- observable for tests; none of this is a hook the production build uses -------------

    /// The request sequence number the next fresh exchange will carry.
    std::uint32_t sequence() const { return sequence_; }
    /// How many retries of the current request have been spent.
    unsigned retryCount() const { return retries_; }
    /// Frames handed to the backend whose exchange has not been confirmed yet.
    std::size_t inFlightFrameCount() const { return inFlightFrames_.size(); }

private:
    /// Where the transport is in the /open -> exchange -> finished progression.
    enum class Stage : std::uint8_t {
        Opening,    ///< the one and only /open is in flight
        Running,    ///< exchanging
        Finished    ///< nothing more will be started except a best-effort /close
    };

    void pumpOpen();
    void pumpExchange();
    void pumpClose();

    void handleExchangeOutcome(RelayHttpOutcome& outcome);
    void maybeStartExchange();
    bool startExchange();
    void scheduleRetry(const std::string& error);

    /// Queues a whole batch of received frames, or none of them.
    bool admitFrames(std::vector<std::vector<std::uint8_t>>& frames);
    /// Drops the frames the last exchange carried; they are the server's problem now.
    void releaseInFlightFrames();

    void fail(const std::string& message);
    static std::string describeHttpStatus(long status);
    static bool isRetryableStatus(long status);

    std::unique_ptr<RelayHttpBackend> backend_;
    std::string   openUrl_;
    std::string   exchangeUrl_;
    std::string   closeUrl_;
    std::string   sessionToken_;

    State         state_     = State::Connecting;
    Stage         stage_     = Stage::Opening;
    std::uint16_t closeCode_ = 0;
    std::string   lastError_;

    /// Set once the server itself reported a close code, so we do not also POST /close: it
    /// already knows.
    bool          serverClosed_    = false;
    /// A best-effort /close has been started (or was decided against). Only ever done once.
    bool          closeNotified_   = false;
    bool          requestInFlight_ = false;

    std::uint32_t sequence_        = 1;
    bool          everExchanged_   = false;
    std::uint32_t lastRequestStartMs_ = 0;

    unsigned      retries_      = 0;
    bool          retryPending_ = false;
    std::uint32_t retryDelayMs_ = 0;
    std::uint32_t retryBaseMs_  = 0;

    /// The exact bytes of the exchange in flight, kept so a retry can repeat them.
    std::vector<std::uint8_t> requestBody_;
    /// The frames those bytes carry, kept so the backlog still counts them and so nothing is
    /// lost if the request has to be repeated.
    std::vector<std::vector<std::uint8_t>> inFlightFrames_;

    std::deque<std::vector<std::uint8_t>> outgoing_;
    /// Bytes still waiting to be sent *plus* bytes in flight: from the caller's point of view
    /// neither has arrived anywhere yet.
    std::size_t outgoingBytes_ = 0;

    std::deque<std::vector<std::uint8_t>> incoming_;
    std::size_t incomingBytes_ = 0;
};

/**
    Creates the HTTPS polling transport with this platform's HTTP backend.

    Defined once per platform, next to the backend it builds. createRelayWebSocket() calls this
    when the endpoint uses an HTTP scheme.

    \return the transport, or nullptr if the platform could not provide an HTTP client
*/
std::unique_ptr<RelayWebSocket> createRelayHttpTransport(const std::string& url,
                                                         const std::string& origin);

#endif // RELAYHTTPTRANSPORT_H
