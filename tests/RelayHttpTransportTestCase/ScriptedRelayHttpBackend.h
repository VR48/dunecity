/*
 *  ScriptedRelayHttpBackend.h - the HTTP client the polling transport is tested against.
 *
 *  RelayHttpTransport reaches the network through RelayHttpBackend and reads the clock through
 *  it too. That is the whole reason the interface exists: with a scripted backend the tests can
 *  say exactly when a request finishes, exactly what it answered and exactly what time it is,
 *  and can then assert on the bytes the transport actually put on the wire.
 *
 *  There is no hook in the production build for any of this. The game builds the transport with
 *  the real backend in createRelayHttpTransport(); the tests build it with this one.
 */

#ifndef SCRIPTEDRELAYHTTPBACKEND_H
#define SCRIPTEDRELAYHTTPBACKEND_H

#include <Network/RelayHttpTransport.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ScriptedRelayHttpBackend final : public RelayHttpBackend {
public:
    /// One request the transport asked for, and when it asked.
    struct Attempt {
        RelayHttpRequest request;
        std::uint32_t    startedAtMs = 0;
    };

    // --- RelayHttpBackend -------------------------------------------------------------------

    std::uint32_t nowMs() const override { return nowMs_; }

    bool start(const RelayHttpRequest& request) override {
        if(inFlight) {
            // The transport promises one request in flight for the whole transport. If it ever
            // breaks that promise the test that notices should fail on this counter rather than
            // on some confusing downstream symptom.
            overlappingStarts++;
        }
        if(refuseStart) {
            return false;
        }
        attempts.push_back(Attempt{request, nowMs_});
        inFlight    = true;
        hasAnswer   = false;
        answer      = RelayHttpOutcome();
        return true;
    }

    bool poll(RelayHttpOutcome& outcome) override {
        polls++;
        if(!inFlight || !hasAnswer) {
            return false;
        }
        outcome   = answer;
        hasAnswer = false;
        inFlight  = false;
        return true;
    }

    void cancel() override {
        if(inFlight) {
            cancelsWhileInFlight++;
            if(cancelTally) {
                // Shared with the test so that a cancel from the transport's destructor - which
                // takes this backend with it - can still be observed afterwards.
                (*cancelTally)++;
            }
        }
        cancels++;
        inFlight  = false;
        hasAnswer = false;
    }

    // --- test controls ----------------------------------------------------------------------

    void advance(std::uint32_t milliseconds) { nowMs_ += milliseconds; }
    void setNow(std::uint32_t milliseconds) { nowMs_ = milliseconds; }

    void completeWith(long status, const std::vector<std::uint8_t>& body) {
        answer            = RelayHttpOutcome();
        answer.kind       = RelayHttpOutcome::Kind::Completed;
        answer.status     = status;
        answer.body       = body;
        hasAnswer         = true;
    }

    void completeWith(long status, const std::string& body) {
        completeWith(status, std::vector<std::uint8_t>(body.begin(), body.end()));
    }

    void failWithNetworkError(const std::string& message = "The connection was lost.") {
        answer       = RelayHttpOutcome();
        answer.kind  = RelayHttpOutcome::Kind::NetworkFailure;
        answer.error = message;
        hasAnswer    = true;
    }

    void failFatally(const std::string& message = "The certificate could not be verified.") {
        answer       = RelayHttpOutcome();
        answer.kind  = RelayHttpOutcome::Kind::Fatal;
        answer.error = message;
        hasAnswer    = true;
    }

    const Attempt& lastAttempt() const { return attempts.back(); }
    std::size_t attemptCount() const { return attempts.size(); }

    std::vector<Attempt> attempts;
    std::shared_ptr<int> cancelTally;
    bool        inFlight             = false;
    bool        hasAnswer            = false;
    bool        refuseStart          = false;
    int         polls                = 0;
    int         cancels              = 0;
    int         cancelsWhileInFlight = 0;
    int         overlappingStarts    = 0;
    RelayHttpOutcome answer;

private:
    std::uint32_t nowMs_ = 100000;
};

#endif // SCRIPTEDRELAYHTTPBACKEND_H
