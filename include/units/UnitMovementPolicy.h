#ifndef UNITMOVEMENTPOLICY_H
#define UNITMOVEMENTPOLICY_H

#include <DataTypes.h>

namespace UnitMovementPolicy {

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
