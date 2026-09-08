#ifndef DUNECITY_NUCLEAR_BLAST_POLICY_H
#define DUNECITY_NUCLEAR_BLAST_POLICY_H

#include <Definitions.h>
#include <cstdint>

namespace DuneCity::NuclearBlastPolicy {
// Palace rockets strike a 5x5 tile pattern excluding its four corners.
constexpr int missileImpactTiles = 21;
constexpr int missileDamagePerTile = 100;
constexpr int plantBlastDamage = 9 * missileDamagePerTile;
// Circular area = twice that 21-tile impact footprint. 355/113 approximates pi;
// keep the geometry integer-only for deterministic simulation.
constexpr int radiusSquared = 2 * missileImpactTiles * TILESIZE * TILESIZE * 113 / 355;
constexpr int searchTiles = 4;
inline bool contains(int dx, int dy) {
    return int64_t{dx} * dx + int64_t{dy} * dy <= radiusSquared;
}
}
#endif
