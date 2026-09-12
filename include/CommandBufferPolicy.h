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

#ifndef COMMANDBUFFERPOLICY_H
#define COMMANDBUFFERPOLICY_H

#include <CommandEmissionSchedule.h>
#include <CommandValidation.h>
#include <Definitions.h>

#include <algorithm>
#include <limits>

/**
    Startup command allowance for relay lockstep. The heartbeat measures the local relay hop,
    not the complete peer path. HTTP polling additionally waits for emission and exchange slots.
    This bounded estimate assumes similar peer latency; it cannot cover arbitrary asymmetry or
    jitter. More allowance reduces waiting but delays local input. It stays fixed for the match.
    Only HTTP polling uses this policy; WebSocket relay and direct ENet keep their prior sizing.
*/
namespace CommandBufferPolicy {

constexpr Uint32 kEmissionIntervalMs = CommandEmissionSchedule::kIntervalMs;
constexpr Uint32 kRelayHoldMs = 100; // Checked against RoomPoll::Timing in the regression tests.
constexpr Uint32 kMinRelayBudgetMs = 700;
constexpr Uint32 kMinBufferCycles = 5;
constexpr Uint32 kMaxRelayBufferCycles = 70;
constexpr Uint32 kMaxRelayBudgetMs = kMaxRelayBufferCycles * GAMESPEED_DEFAULT;

// A sender can be at most the receiver's buffer minus one cycle ahead. With our window's
// exclusive end, its largest entry is then receiverCycle + receiverBuffer + senderBuffer - 2.
// Keep this inside the existing 2*receiverBuffer + slack check, even at the smallest buffer.
static_assert(kMaxRelayBufferCycles <= kMinBufferCycles + CommandValidation::kCycleWindowSlack + 2u,
              "relay windows must fit the unchanged command validation window");
static_assert(CommandEmissionSchedule::kHistoryCycles + kMaxRelayBufferCycles
                  <= CommandValidation::kMaxCommandListEntries,
              "relay history plus allowance must fit the existing packet entry cap");
static_assert(kMinRelayBudgetMs < kMaxRelayBudgetMs, "invalid relay budget range");

inline Uint32 sanitizedGameSpeedMs(int gameSpeedMs) {
    if(gameSpeedMs <= 0) return GAMESPEED_DEFAULT;
    return static_cast<Uint32>(std::min(std::max(gameSpeedMs, GAMESPEED_MIN), GAMESPEED_MAX));
}

/// Round up using this match's cycle duration, then cap both input delay and packet size.
inline Uint32 cyclesForMilliseconds(Uint32 milliseconds, int gameSpeedMs) {
    const Uint32 speed = sanitizedGameSpeedMs(gameSpeedMs);
    if(milliseconds > std::numeric_limits<Uint32>::max() - speed) {
        return kMaxRelayBufferCycles;
    }
    const Uint32 cycles = (milliseconds + speed - 1u) / speed;
    return std::min(std::max(cycles, kMinBufferCycles), kMaxRelayBufferCycles);
}

/**
    Estimate an emission wait, gateway hold and each endpoint's poll wait plus transfer leg.
    The peer hop is not measured: both terms use the local heartbeat as a bounded estimate.
    A zero sample uses the floor. The ceiling limits input delay even after a latency spike.
*/
inline Uint32 relayDeliveryBudgetMs(Uint32 relayServerRoundTripMs) {
    const Uint32 rtt = std::min(relayServerRoundTripMs, kMaxRelayBudgetMs);
    const Uint32 localPollMs = rtt + rtt / 2u;
    const Uint32 peerPollMs = rtt + rtt / 2u;
    const Uint32 budget = kEmissionIntervalMs + kRelayHoldMs + localPollMs + peerPollMs;
    return std::min(std::max(budget, kMinRelayBudgetMs), kMaxRelayBudgetMs);
}

inline Uint32 relayCommandBufferCycles(Uint32 relayServerRoundTripMs, int gameSpeedMs) {
    return cyclesForMilliseconds(relayDeliveryBudgetMs(relayServerRoundTripMs), gameSpeedMs);
}

constexpr Uint32 emittedEntryCount(Uint32 bufferCycles) {
    return CommandEmissionSchedule::kHistoryCycles + bufferCycles;
}

} // namespace CommandBufferPolicy

#endif // COMMANDBUFFERPOLICY_H
