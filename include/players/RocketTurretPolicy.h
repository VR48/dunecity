#ifndef ROCKET_TURRET_POLICY_H
#define ROCKET_TURRET_POLICY_H
#include <dunecity/CityEffects.h>
#include <tuple>

namespace RocketTurretPolicy {
inline int defenseWeight(int item) {
    if (item == Structure_NuclearPlant) return 2;
    return item == Structure_HeavyFactory || item == Structure_RepairYard ? 1 : 0;
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
