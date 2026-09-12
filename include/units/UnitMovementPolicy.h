#ifndef UNITMOVEMENTPOLICY_H
#define UNITMOVEMENTPOLICY_H

#include <DataTypes.h>

namespace UnitMovementPolicy {
// Legacy bots rely on automatic restart; qBot owns explicit safety holds.
inline bool resumeStoppedHarvester(bool aiHouse, bool managedSafety) {
    return aiHouse && !managedSafety;
}


inline bool shouldCancelPickupOnMove(bool awaitingPickup, ATTACKMODE attackMode, bool bForced) {
    if(!bForced) {
        return false;
    }
    return awaitingPickup || attackMode == CARRYALLREQUESTED;
}

inline ATTACKMODE attackModeAfterCancellingPickup(ATTACKMODE current, bool isHarvester) {
    if(current != CARRYALLREQUESTED) {
        return current;
    }
    return isHarvester ? HARVEST : GUARD;
}

}

#endif
