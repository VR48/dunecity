#ifndef DUNECITY_ROADMAINTENANCEPOLICY_H
#define DUNECITY_ROADMAINTENANCEPOLICY_H

#include <DataTypes.h>

namespace DuneCity {

// Micropolis simulate.cpp: ordinary/light roads count once, heavy roads twice.
// Use its default (easy) city rate: 0.7/year. Roads remain 1x1 in DuneCity;
// the smaller zone footprints do not scale road maintenance. Fully funded.
struct RoadMaintenanceCensus {
    int tiles = 0;
    int heavyTiles = 0;

    void add(bool isRoad, int trafficDensity) {
        if (!isRoad) return;
        ++tiles;
        if (trafficDensity >= 192) ++heavyTiles;
    }

    // DuneCity starter-city exemption, measured in the population shown in UI.
    // Re-evaluated each census: losing population restores the exemption.
    int annualCost(int displayedPopulation) const {
        return displayedPopulation < 2000 ? 0 : (tiles + heavyTiles) * 7 / 10;
    }
};

inline bool validRoadOwner(int owner) { return owner >= 0 && owner < NUM_HOUSES; }

inline int roadOwnerAfterPlacement(bool wasRoad, int previousOwner, int builder) {
    return wasRoad && validRoadOwner(previousOwner) ? previousOwner : builder;
}

// Old automatic perimeter roads had no owner. Infer only from adjacent
// structures, never by propagating ownership through the road network.
// Closest structure wins; house ID breaks ties independently of scan order.
template<class StructureOwnerAt>
int legacyRoadOwner(int previousOwner, int x, int y, StructureOwnerAt ownerAt) {
    if (validRoadOwner(previousOwner)) return previousOwner;
    int best = previousOwner, distance = 3;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int candidate = ownerAt(x + dx, y + dy);
            if (!validRoadOwner(candidate)) continue;
            const int d = (dx != 0) + (dy != 0);
            if (d < distance || (d == distance && candidate < best)) {
                best = candidate;
                distance = d;
            }
        }
    }
    return best;
}

} // namespace DuneCity
#endif
