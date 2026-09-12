/*
 *  CommandEmissionTestCase.cpp - regression tests for the rate at which the rolling command
 *  history is put on the wire.
 *
 *  The failure these cover is a real one: an HTTPS polling match between a browser host and a
 *  native client ran for 197 seconds and then closed with RoomRelay Close::SlowConsumer, with
 *  nothing wrong in the client's own event queue. CommandManager::update() used to emit the
 *  complete rolling history once per simulation loop iteration, so a peer simulating at 62.5
 *  cycles a second produced 62.5 relay frames a second, while the polling gateway drains at most
 *  64 frames per HTTP round trip. At a round trip near the top of the observed 0.6-1.1 s range
 *  the drain is slower than the production, the relay's per-peer send queue grows without bound
 *  and the 1 MiB slow-consumer guard closes the session mid-match.
 *
 *  So the model here is the production one, not a description of it:
 *    - PollPeerQueue is the queue from tools/room-relay/src/polling.js, with the same 1 MiB
 *      ceiling, the same 4096 frame ceiling and the same 64 frames per response,
 *    - ReceiverModel is the contiguous-run rule from CommandManager::addCommandList(), including
 *      the watermark that stops advancing at the first gap,
 *    - the sender drives the real CommandEmissionSchedule and builds the same window
 *      CommandManager::update() builds.
 *
 *  What the tests then assert is the pair of properties the change has to have: the traffic a
 *  peer produces is bounded by wall time rather than by its frame rate, and no command is ever
 *  lost - not when the simulation catches up in a burst, not when it is blocked in lockstep, and
 *  not when SDL_GetTicks() wraps.
 */

#include <catch2/catch_all.hpp>

#include <CommandEmissionSchedule.h>
#include <CommandValidation.h>
#include <Definitions.h>

#include <cstddef>
#include <deque>
#include <set>
#include <vector>

namespace {

/// One emission as the relay sees it: the window of cycles it carries and its size on the wire.
struct Emission {
    Uint32 startCycle = 0;      ///< inclusive
    Uint32 endCycle   = 0;      ///< exclusive
    std::size_t bytes = 0;
    std::vector<Uint32> commandCycles;  ///< cycles inside the window that carry a local command
};

/**
    The size one emission encodes to, counted the way the production path encodes it:
    NetworkManager::sendCommandList() writes the packet type and the simulation seed,
    CommandList::save() writes the entry count and then every entry, and Command::save() writes
    the player id, the command id and the parameter vector.
*/
std::size_t emissionBytes(std::size_t entryCount, std::size_t commandCount) {
    const std::size_t kPacketHeaderBytes  = 4 + 4 + 4;      // type + seed + entry count
    const std::size_t kEntryBytes         = 4 + 4;          // cycle + command count
    const std::size_t kCommandBytes       = 1 + 4 + 4 + 4 * 4;  // player + id + count + parameters
    return kPacketHeaderBytes + entryCount * kEntryBytes + commandCount * kCommandBytes;
}

/**
    The relay's per-peer send queue for a polling session, mirroring PollPeer in
    tools/room-relay/src/polling.js. A frame costs its own length plus the four length bytes, the
    queue is closed as a slow consumer the moment a frame would not fit, and one HTTP response
    carries at most 64 frames and at most 1 MiB.
*/
class PollPeerQueue {
public:
    static constexpr std::size_t kMaxBatchBytes       = 1048576;
    static constexpr std::size_t kMaxFramesPerResponse = 64;
    static constexpr std::size_t kMaxQueueFrames      = 4096;

    /// \return false once the slow-consumer guard has fired; the session is over at that point
    bool send(const Emission& emission) {
        if(closed_) {
            return false;
        }
        if(frames_.size() >= kMaxQueueFrames
           || (buffered_ + emission.bytes + 4) > kMaxBatchBytes) {
            frames_.clear();
            buffered_ = 0;
            closed_ = true;
            return false;
        }
        frames_.push_back(emission);
        buffered_ += emission.bytes + 4;
        if(buffered_ > peakBuffered_) {
            peakBuffered_ = buffered_;
        }
        return true;
    }

    /// One HTTP response worth of frames.
    std::vector<Emission> respond() {
        std::vector<Emission> batch;
        std::size_t bytes = 12;
        while(!frames_.empty() && batch.size() < kMaxFramesPerResponse
              && (bytes + 4 + frames_.front().bytes) <= kMaxBatchBytes) {
            const Emission frame = frames_.front();
            frames_.pop_front();
            buffered_ -= frame.bytes + 4;
            bytes += 4 + frame.bytes;
            batch.push_back(frame);
        }
        return batch;
    }

    bool closed() const { return closed_; }
    std::size_t queuedFrames() const { return frames_.size(); }
    std::size_t peakBuffered() const { return peakBuffered_; }

private:
    std::deque<Emission> frames_;
    std::size_t buffered_     = 0;
    std::size_t peakBuffered_ = 0;
    bool closed_              = false;
};

/**
    The receiving side of CommandManager::addCommandList(): entries below the watermark are
    retransmissions and are skipped, the run has to start exactly at the watermark, and the first
    gap stops the batch and every later one - the watermark never moves past a cycle that was not
    received, which is what makes a gap a permanent lockstep stall rather than a lost command.
*/
class ReceiverModel {
public:
    void deliver(const Emission& emission) {
        if(emission.endCycle <= nextExpectedCycle_) {
            return;     // pure retransmission
        }
        if(emission.startCycle > nextExpectedCycle_) {
            sawGap_ = true;     // the watermark can never advance again
            return;
        }
        for(Uint32 cycle = nextExpectedCycle_; cycle < emission.endCycle; cycle++) {
            for(const Uint32 commandCycle : emission.commandCycles) {
                if(commandCycle == cycle) {
                    appliedCommandCycles_.insert(cycle);
                }
            }
            nextExpectedCycle_ = CommandValidation::nextCycleAfter(cycle);
        }
    }

    bool sawGap() const { return sawGap_; }
    const std::set<Uint32>& appliedCommandCycles() const { return appliedCommandCycles_; }

private:
    Uint32 nextExpectedCycle_ = 0;
    bool sawGap_              = false;
    std::set<Uint32> appliedCommandCycles_;
};

/// What a run of the model is asked to do. The defaults are a 60 fps peer in a normal match.
struct Scenario {
    bool paced = true;              ///< false reproduces the old per-iteration emission
    Uint32 durationMs = 600000;     ///< wall time to simulate
    Uint32 frameMs = 16;            ///< one rendered frame; 16 ms and one cycle is 62.5 cycles a second
    Uint32 cyclesPerFrame = 1;      ///< Game's loop runs up to ten simulation cycles per frame
    bool simulationRuns = true;     ///< false is a peer blocked in lockstep: iterations, no cycles
    Uint32 commandEveryCycles = 0;  ///< 0 issues no commands
    Uint32 startMs = 0;             ///< initial SDL_GetTicks() reading
    Uint32 networkCycleBuffer = 55; ///< MILLI2CYCLES(800) + 5, an 800 ms relay round trip
    std::vector<Uint32> roundTripsMs = {900, 1100, 1050, 1100};
};

struct Result {
    std::size_t emissions    = 0;
    std::size_t bytesSent    = 0;
    std::size_t peakBuffered = 0;
    Uint32 closedAtMs        = 0;   ///< only meaningful when the queue closed
    bool closed              = false;
    bool receiverSawGap      = false;
    std::size_t commandsIssued  = 0;
    std::size_t commandsApplied = 0;
};

/**
    Runs one match through the model: a sender that emits into the relay queue, a browser that
    drains one response per round trip, and a receiver that applies what it drains.

    At the end of input generation, advance one cycle and keep running the schedule until
    the last command becomes eligible for emission, then drain the simulated connection.
    This tests eventual delivery during continued play; it does not model shutdown flushing.
*/
Result runMatch(const Scenario& scenario) {
    CommandEmissionSchedule schedule;
    PollPeerQueue relayQueue;
    ReceiverModel receiver;
    Result result;

    std::deque<Uint32> pendingCommandCycles;    // issued, still inside the emitted window
    Uint32 nowMs = scenario.startMs;
    Uint32 elapsedMs = 0;
    Uint32 cycle = 0;
    Uint32 nextExchangeMs = scenario.startMs;
    std::size_t nextRoundTrip = 0;

    // The browser starts an exchange from a loop iteration, which is why this is only ever
    // reached at frame granularity.
    const auto exchange = [&]() {
        if((nowMs - nextExchangeMs) >= 0x80000000u) {   // wrap-safe nowMs < nextExchangeMs
            return;
        }
        for(const Emission& frame : relayQueue.respond()) {
            receiver.deliver(frame);
        }
        nextExchangeMs = nowMs + scenario.roundTripsMs[nextRoundTrip % scenario.roundTripsMs.size()];
        nextRoundTrip++;
    };

    /// \return false once the relay has closed the session as a slow consumer
    const auto emit = [&]() {
        const Uint32 windowStart = CommandEmissionSchedule::historyStartCycle(cycle);
        const Uint32 windowEnd   = cycle + scenario.networkCycleBuffer;
        while(!pendingCommandCycles.empty() && pendingCommandCycles.front() < windowStart) {
            pendingCommandCycles.pop_front();
        }

        Emission emission;
        emission.startCycle = windowStart;
        emission.endCycle   = windowEnd;
        for(const Uint32 commandCycle : pendingCommandCycles) {
            if(commandCycle < windowEnd) {
                emission.commandCycles.push_back(commandCycle);
            }
        }
        emission.bytes = emissionBytes(windowEnd - windowStart, emission.commandCycles.size());

        schedule.noteEmission(nowMs, windowEnd);
        result.emissions++;
        result.bytesSent += emission.bytes;
        if(!relayQueue.send(emission)) {
            result.closed = true;
            result.closedAtMs = elapsedMs;
            return false;
        }
        return true;
    };

    bool aborted = false;
    for(; elapsedMs < scenario.durationMs && !aborted; elapsedMs += scenario.frameMs) {
        // Every iteration of Game's inner loop, of which there may be up to ten in one frame.
        // SDL_GetTicks() does not move inside a frame, which is exactly why the cadence alone
        // cannot bound a catch-up burst and the retention rule has to.
        const Uint32 iterations = scenario.simulationRuns ? scenario.cyclesPerFrame : 1;
        for(Uint32 iteration = 0; iteration < iterations; iteration++) {
            // processInput() puts a command in the timeslot for cycle + networkCycleBuffer...
            if(scenario.commandEveryCycles != 0 && scenario.simulationRuns
               && (cycle % scenario.commandEveryCycles) == 0) {
                pendingCommandCycles.push_back(cycle + scenario.networkCycleBuffer);
                result.commandsIssued++;
            }

            // ...and cmdManager.update() offers the history to the schedule, right here.
            if(!scenario.paced || schedule.shouldEmit(nowMs, cycle)) {
                if(!emit()) {
                    aborted = true;
                    break;
                }
            }

            if(scenario.simulationRuns) {
                cycle++;
            }
        }

        exchange();
        nowMs += scenario.frameMs;
    }

    if(!relayQueue.closed()) {
        if(scenario.simulationRuns) {
            cycle++;
        }
        // Stop issuing input, then allow the real cadence to send the final outstanding
        // command. This models continued play, not an unconditional shutdown flush.
        while(scenario.paced && !schedule.shouldEmit(nowMs, cycle)) {
            exchange();
            nowMs += scenario.frameMs;
        }
        emit();
        while(!relayQueue.closed() && relayQueue.queuedFrames() > 0) {
            exchange();
            nowMs += scenario.frameMs;
        }
    }

    result.peakBuffered    = relayQueue.peakBuffered();
    result.receiverSawGap  = receiver.sawGap();
    result.commandsApplied = receiver.appliedCommandCycles().size();
    return result;
}

} // namespace

TEST_CASE("Emission: a 60 fps peer used to walk the relay queue into the slow consumer guard",
          "[network][relay][emission]") {
    Scenario scenario;
    scenario.paced = false;     // the emission rate before this change: once per loop iteration

    const Result result = runMatch(scenario);

    // 62.5 frames a second against a drain of 64 per round trip at ~1.04 s: the queue grows by a
    // few frames a second until a megabyte of them is waiting, and the relay closes the session
    // in the middle of the match. The production failure took 197 s; a browser that is also
    // spending whole frames in Asyncify sleeps polls less often than this model does.
    CHECK(result.closed);
    CHECK(result.closedAtMs < scenario.durationMs);
    CHECK(result.peakBuffered > 1000000);
}

TEST_CASE("Emission: pacing keeps the same match inside the relay's queue for its whole length",
          "[network][relay][emission]") {
    Scenario scenario;
    scenario.commandEveryCycles = 31;   // a busy player, about two commands a second

    const Result result = runMatch(scenario);

    CHECK_FALSE(result.closed);
    CHECK_FALSE(result.receiverSawGap);

    // Ten emissions a second, measured against wall time rather than against the 37500 cycles
    // this match simulated. The loop iterates every 16 ms, so the cadence lands on 112 ms.
    const std::size_t seconds = scenario.durationMs / 1000;
    CHECK(result.emissions <= 11 * seconds);
    CHECK(result.emissions >= 8 * seconds);

    // The queue holds a handful of frames instead of the 600-odd it took to reach the ceiling.
    CHECK(result.peakBuffered < 64u * 1024u);

    // And the point of the exercise: the cheaper traffic still carried every command.
    CHECK(result.commandsIssued > 1000);
    CHECK(result.commandsApplied == result.commandsIssued);
}

TEST_CASE("Emission: a peer blocked in lockstep stops retransmitting once per frame",
          "[network][relay][emission]") {
    Scenario scenario;
    scenario.durationMs = 5000;
    scenario.simulationRuns = false;    // waiting for another player: iterations, but no cycles

    Scenario legacy = scenario;
    legacy.paced = false;

    const Result waiting = runMatch(scenario);
    const Result before  = runMatch(legacy);

    // No cycle ran, so every one of the frames on the left was a byte-identical retransmission
    // of the one before it - and the peer that is already struggling is the one that sent them.
    CHECK(before.emissions >= 300);
    CHECK(waiting.emissions <= 51);
    CHECK(waiting.bytesSent * 6 < before.bytesSent);
}

TEST_CASE("Emission: a catch-up burst cannot age a cycle out of the history unsent",
          "[network][relay][emission]") {
    CommandEmissionSchedule schedule;

    // The worst case for the cadence: the simulation advances while the clock does not, which is
    // what ten cycles inside one rendered frame look like from the schedule's side. Nothing but
    // the retention rule can fire here.
    const Uint32 frozenMs = 12345;
    const Uint32 networkCycleBuffer = 55;
    const Uint32 cycles = 5000;

    Uint32 emissions = 0;
    Uint32 lastWindowEnd = 0;
    bool everEmitted = false;
    bool contiguous = true;

    for(Uint32 cycle = 0; cycle < cycles; cycle++) {
        if(!schedule.shouldEmit(frozenMs, cycle)) {
            continue;
        }
        const Uint32 windowStart = CommandEmissionSchedule::historyStartCycle(cycle);
        if(everEmitted && windowStart > lastWindowEnd) {
            contiguous = false;     // a cycle fell out of the history without ever being sent
        }
        lastWindowEnd = cycle + networkCycleBuffer;
        everEmitted = true;
        schedule.noteEmission(frozenMs, lastWindowEnd);
        emissions++;
    }

    // Successive windows meet exactly, so their union covers every cycle from 0 to the frontier
    // and a receiver's watermark can walk straight through them.
    CHECK(contiguous);

    // The history is 156 cycles and the buffer 55, so the retention rule fires every 211 cycles
    // and no more often: it is a floor under the emission rate, not a second cadence.
    CHECK(emissions >= cycles / (CommandEmissionSchedule::kHistoryCycles + networkCycleBuffer));
    CHECK(emissions < 40);

    // The cycles past the frontier are not lost, they are merely not sent yet: at the last cycle
    // of the burst the frontier is still inside the history window, so the next emission will
    // begin exactly where this one ended.
    CHECK(lastWindowEnd + CommandEmissionSchedule::kHistoryCycles >= cycles);
}

TEST_CASE("Emission: every command survives a match simulated ten cycles to the frame",
          "[network][relay][emission]") {
    Scenario scenario;
    scenario.durationMs = 120000;
    scenario.frameMs = 4;               // a fast machine catching up after a stall...
    scenario.cyclesPerFrame = 10;       // ...at the guardrail, 2500 cycles a second
    scenario.commandEveryCycles = 7;
    scenario.roundTripsMs = {1100};     // the worst round trip that was observed

    const Result result = runMatch(scenario);

    CHECK_FALSE(result.closed);
    CHECK_FALSE(result.receiverSawGap);
    CHECK(result.commandsIssued > 40000);
    CHECK(result.commandsApplied == result.commandsIssued);

    // Forty times the cycle rate of a normal match, and the traffic barely moved: the retention
    // rule fires every 211 cycles, which at this speed is every 84 ms, so the emission rate is
    // the cadence plus a little rather than the 2500 a second the cycles would have produced.
    const std::size_t seconds = scenario.durationMs / 1000;
    CHECK(result.emissions <= 12 * seconds);

    // The same burst on the old path does not survive its first second.
    Scenario legacy = scenario;
    legacy.paced = false;
    legacy.durationMs = 2000;

    const Result before = runMatch(legacy);
    CHECK(before.closed);
    CHECK(before.closedAtMs < 1000);
}

TEST_CASE("Emission: the cadence survives the SDL_GetTicks() wrap", "[network][relay][emission]") {
    Scenario scenario;
    scenario.durationMs = 30000;
    scenario.commandEveryCycles = 31;

    Scenario wrapping = scenario;
    wrapping.startMs = 0xFFFFFFFFu - 300u;      // wraps ~300 ms into the run

    const Result plain   = runMatch(scenario);
    const Result wrapped = runMatch(wrapping);

    // Unsigned subtraction makes the wrap an ordinary interval: the same match emits the same
    // frames, rather than falling silent for the 49 days a signed comparison would see. A peer
    // that stops emitting for even a few seconds is a peer every other one waits for.
    CHECK(wrapped.emissions == plain.emissions);
    CHECK_FALSE(wrapped.closed);
    CHECK_FALSE(wrapped.receiverSawGap);
    CHECK(wrapped.commandsIssued > 0);
    CHECK(wrapped.commandsApplied == wrapped.commandsIssued);
}

TEST_CASE("Emission: a new match does not inherit the previous match's frontier",
          "[network][relay][emission]") {
    CommandEmissionSchedule schedule;

    // A long match: the last emission covered cycles up to 40000 at a late clock reading.
    schedule.noteEmission(5000000u, 40000u);
    CHECK_FALSE(schedule.shouldEmit(5000000u, 39000u));

    schedule.reset();

    // The next match starts at cycle 0 on a clock that has not moved. Without the reset the
    // stale frontier of 40000 would sit above every window start the new match produces for its
    // first ten minutes, leaving the retention rule unable to fire at all.
    CHECK(schedule.shouldEmit(5000000u, 0u));
    schedule.noteEmission(5000000u, 55u);
    CHECK_FALSE(schedule.shouldEmit(5000050u, 1u));
    CHECK(schedule.shouldEmit(5000100u, 1u));

    // And the retention rule works again on the new match's scale.
    CHECK(schedule.shouldEmit(5000000u, CommandEmissionSchedule::kHistoryCycles + 55u));
}

TEST_CASE("Emission: input after a sent frontier arrives once in the next scheduled window",
          "[network][relay][emission]") {
    CommandEmissionSchedule schedule;
    ReceiverModel receiver;
    constexpr Uint32 buffer = 55;

    Emission first;
    first.endCycle = buffer;
    receiver.deliver(first);
    schedule.noteEmission(1000, first.endCycle);

    // Input immediately after that send is assigned to the exclusive end, not a cycle
    // the receiver has already acknowledged. Advancing once makes it eligible to send.
    CHECK_FALSE(schedule.shouldEmit(1001, 1));
    REQUIRE(schedule.shouldEmit(1100, 1));
    Emission next;
    next.endCycle = buffer + 1;
    next.commandCycles.push_back(buffer);
    receiver.deliver(next);
    receiver.deliver(next);  // repeated history must not reapply it
    CHECK_FALSE(receiver.sawGap());
    REQUIRE(receiver.appliedCommandCycles().size() == 1);
    CHECK(*receiver.appliedCommandCycles().begin() == buffer);
}
