#ifndef ROCKET_TURRET_POLICY_H
#define ROCKET_TURRET_POLICY_H
#include <dunecity/CityEffects.h>
#include <tuple>
#include <mmath.h>

namespace RocketTurretPolicy {
inline int defenseWeight(int item) {
    if (item == Structure_NuclearPlant) return 2;
    // Every real building needs protection, including outlying R/C/I districts.
    // Surface tiles and defensive emplacements do not recursively demand turrets.
    if (!isStructure(item) || item == Structure_Slab1 || item == Structure_Slab4
        || item == Structure_Road || item == Structure_PowerLine || item == Structure_Wall
        || item == Structure_GunTurret || item == Structure_RocketTurret) return 0;
    return 1;
}
inline bool coversBuilding(Coord turret, Coord origin, Coord size, int range) {
    // All corners must fit the actual octile weapon range. A square radius
    // around the centre incorrectly counts exposed diagonal edges as covered.
    for(int y : {0,size.y-1}) for(int x : {0,size.x-1})
        if(blockDistance(turret,Coord(origin.x+x,origin.y+y))>range) return false;
    return true;
}
inline int amenityBenefit(int landValue, bool alreadyCovered, int terrainGain) {
    if (alreadyCovered) return 0;
    return std::min(std::max(0, DuneCity::kMaxLandValue - landValue), std::max(0, terrainGain));
}
struct Score {
    int defense = 0, junction = 0, amenity = 0, proximity = 0;
    bool useful() const { return defense > 0 || (junction > 0 && amenity > 0); }
    bool betterThan(const Score& other) const {
        return std::tie(defense, junction, amenity, proximity)
             > std::tie(other.defense, other.junction, other.amenity, other.proximity);
    }
};
}
#endif
