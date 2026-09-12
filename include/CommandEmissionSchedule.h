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

#ifndef COMMANDEMISSIONSCHEDULE_H
#define COMMANDEMISSIONSCHEDULE_H

#include <Definitions.h>

#include <SDL.h>

/**
    Paces relay command-history packets independently of rendering and simulation speed.
    Polling carries at most 64 frames per response. At a 1.1-second exchange interval,
    its drain rate is below the normal 62.5 simulation steps per second.

    Each emission retains the existing history window. Besides the 100 ms cadence,
    emit when that window reaches the previous emission's exclusive upper bound.
    CommandManager checks once per cycle, so successive windows remain contiguous
    even when simulation catch-up advances faster than wall time.
*/
class CommandEmissionSchedule {
public:
    /// Wall-clock cadence: ten emissions a second, whatever the render or simulation rate is.
    static constexpr Uint32 kIntervalMs = 100;

    /// How much history one emission repeats. This is the window CommandManager has always sent.
    static constexpr Uint32 kHistoryMs = 2500;

    /// The same window expressed in cycles, which is how the retention rule reasons about it.
    static constexpr Uint32 kHistoryCycles = MILLI2CYCLES(kHistoryMs);

    /**
        First cycle an emission at this cycle would carry, saturating at 0.
        \param  currentCycle    the cycle the emission is made at
        \return the inclusive lower bound of the emitted window
    */
    static Uint32 historyStartCycle(Uint32 currentCycle) {
        return (currentCycle > kHistoryCycles) ? (currentCycle - kHistoryCycles) : 0;
    }

    /**
        \param  nowMs           SDL_GetTicks() at the call, wrapping is handled
        \param  currentCycle    the cycle the emission would be made at
        \return true if the command history has to go out now
    */
    bool shouldEmit(Uint32 nowMs, Uint32 currentCycle) const {
        if(!everEmitted) {
            return true;
        }

        // Retention. The caller checks this once per cycle and the window start advances by at
        // most one cycle per cycle, so the first time it reaches the frontier it is exactly on
        // it: the next window still begins where the last one ended, and nothing is skipped.
        if(historyStartCycle(currentCycle) >= sentThroughCycle) {
            return true;
        }

        // Cadence. Unsigned subtraction, so the 49.7 day wrap of SDL_GetTicks() is a normal
        // interval and not a 49 day pause.
        return (nowMs - lastEmissionMs) >= kIntervalMs;
    }

    /**
        Records an emission that has just been handed to the network manager.
        \param  nowMs           the same clock reading shouldEmit() was asked with
        \param  windowEndCycle  the exclusive upper bound of the window that was sent
    */
    void noteEmission(Uint32 nowMs, Uint32 windowEndCycle) {
        everEmitted     = true;
        lastEmissionMs  = nowMs;
        sentThroughCycle = windowEndCycle;
    }

    /**
        Forgets everything about the previous session. A frontier left over from a finished match
        would suppress the retention rule in the next one, and its emission time would be read
        against a clock that has since moved on.
    */
    void reset() { *this = CommandEmissionSchedule(); }

private:
    bool   everEmitted      = false;    ///< false until the first emission of this session
    Uint32 lastEmissionMs   = 0;        ///< SDL_GetTicks() of the last emission
    Uint32 sentThroughCycle = 0;        ///< exclusive upper bound of the last emitted window
};

#endif // COMMANDEMISSIONSCHEDULE_H
