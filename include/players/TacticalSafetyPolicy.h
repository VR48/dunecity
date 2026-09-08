#ifndef TACTICAL_SAFETY_POLICY_H
#define TACTICAL_SAFETY_POLICY_H
#include <algorithm>
#include <data.h>
#include <players/CityPlacementPolicy.h>

namespace TacticalSafetyPolicy {
inline bool protectedReactorNeighbour(int type) {
    return type == Structure_NuclearPlant || type == Structure_HeavyFactory
        || type == Structure_ConstructionYard || type == Structure_RepairYard
        || type == Structure_HighTechFactory || type == Structure_IX
        || type == Structure_Palace || type == Structure_StarPort || type == Structure_Refinery;
}
// Four clear tiles between footprints, exceeding the reactor's 3.66-tile radius.
inline bool blastClearance(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
    return CityPlacementPolicy::footprintDistance(x,y,w,h,bx,by,bw,bh) >= 5;
}
inline int lossStrength(unsigned age, unsigned lifetime) {
    if (!lifetime || age >= lifetime) return 0;
    return 1 + 100 * (lifetime-age) / lifetime;
}
// Skip the first tile when escaping a firing zone. For outbound spice journeys,
// the caller separately checks the vehicle's current tile before using this.
template<class Danger>
int corridorDanger(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int result = 0;
    for (int i = 2; i <= steps; ++i)
        result = std::max(result, danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps));
    return result;
}
// Escape an existing firing zone without entering stronger danger or re-entering
// danger after reaching safety. This is a corridor estimate, not a path proof.
template<class Danger>
bool escapeCorridor(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int previous = danger(x,y);
    for (int i=1; i<=steps; ++i) {
        const int next = danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps);
        if (next > previous) return false;
        previous = next;
    }
    return previous == 0;
}

}
#endif
