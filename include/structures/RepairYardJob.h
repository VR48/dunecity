#ifndef REPAIR_YARD_JOB_H
#define REPAIR_YARD_JOB_H

#include <SDL_stdinc.h>

namespace RepairYardJob {
// Releasing an occupant must preserve bookings for other approaching vehicles.
// Cancellation/pickup/destruction can all arrive after the same job ended.
inline void finish(bool& repairing, Uint32& bookings) {
    if (repairing && bookings > 0) --bookings;
    repairing = false;
}

// Object IDs can expire while a vehicle is stored. Resolve before any access,
// and release stale occupancy once so the yard remains usable.
template<class Resolve, class Clear>
auto resolve(bool repairing, Resolve lookup, Clear clear) -> decltype(lookup()) {
    if (!repairing) return nullptr;
    auto* unit = lookup();
    if (!unit) clear();
    return unit;
}
}
#endif
