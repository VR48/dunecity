/*
 *  ZoneSimulation.cpp - Zone growth and decline logic
 *
 *  Implements road-adjacency bonuses: zones near roads grow faster.
 *  Ported from Micropolis zone.cpp logic.
 */

#include <dunecity/ZoneSimulation.h>
#include <dunecity/CitySimulation.h>

#include <globals.h>
#include <Map.h>
#include <Tile.h>

namespace DuneCity {

ZoneSimulation::ZoneSimulation() = default;

void ZoneSimulation::init(CitySimulation* sim) {
    sim_ = sim;
}

// =============================================================================
// Road Proximity Helper
// =============================================================================

bool ZoneSimulation::hasNearbyRoad(int x, int y) const {
    const Map* m = map_ ? map_ : currentGameMap;
    if (!m) return false;

    // Check 3x3 area around the zone tile for any conductive (road) tile
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue; // skip center (the zone tile itself)
            if (m->tileExists(x + dx, y + dy)) {
                const Tile* tile = m->getTile(x + dx, y + dy);
                if (tile && tile->isRoad()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// =============================================================================
// Zone Evaluation Functions
//
// Each eval function scores local conditions for a zone tile.
// Score ranges are derived from the Micropolis original logic.
// Road proximity adds a bonus score to reflect faster development
// when a zone has road access.
// =============================================================================

int16_t ZoneSimulation::evalRes(int x, int y, int trafGood) const {
    // Base score derived from traffic: well-connected areas score higher
    int16_t score = 35 + trafGood;

    if (hasNearbyRoad(x, y)) {
        score += 500; // Road adjacency bonus
    }

    return score;
}

int16_t ZoneSimulation::evalCom(int x, int y, int trafGood) const {
    // Base commercial score: moderate floor, scales with traffic quality
    int16_t score = 30 + trafGood;

    if (hasNearbyRoad(x, y)) {
        score += 300; // Road adjacency bonus
    }

    return score;
}

int16_t ZoneSimulation::evalInd(int x, int y, int trafGood) const {
    // Base industrial score: moderate floor, scales with traffic quality
    int16_t score = 20 + trafGood;

    if (hasNearbyRoad(x, y)) {
        score += 400; // Road adjacency bonus (bonus for road access to silos)
    }

    return score;
}

// =============================================================================
// Zone Growth Handlers
//
// doResIn / doComIn / doIndIn handle the "grow" direction for each zone.
// When a zone is adjacent to a road it receives a +1 bonus population increment
// on top of the normal +1, giving a 25% growth boost over the base rate.
// =============================================================================

void ZoneSimulation::doResIn(int x, int y, uint8_t pop, int /*landVal*/) {
    if (pop >= 8) return; // Already at max density

    // Normal growth: +1, Road bonus: +2 (25% effective boost over +1)
    int growth = hasNearbyRoad(x, y) ? 2 : 1;
    incRateOfGrowth(x, y, growth);
}

void ZoneSimulation::doCommercial(int x, int y, bool /*zonePower*/) {
    // Stub — commercial processing not yet implemented
}

void ZoneSimulation::doIndustrial(int x, int y, bool /*zonePower*/) {
    // Stub — industrial processing not yet implemented
}

void ZoneSimulation::doComIn(int x, int y, uint8_t pop, int /*landVal*/) {
    if (pop >= 8) return;

    int growth = hasNearbyRoad(x, y) ? 2 : 1;
    incRateOfGrowth(x, y, growth);
}

void ZoneSimulation::doIndIn(int x, int y, uint8_t pop, int /*landVal*/) {
    if (pop >= 8) return;

    int growth = hasNearbyRoad(x, y) ? 2 : 1;
    incRateOfGrowth(x, y, growth);
}

void ZoneSimulation::doResidential(int x, int y, bool /*zonePower*/) {
    // Stub — residential processing not yet implemented
}

void ZoneSimulation::doResOut(int x, int y, uint8_t pop, int /*landVal*/) {
    // Stub — residential decline not yet implemented
}

void ZoneSimulation::doComOut(int x, int y, uint8_t pop, int /*landVal*/) {
    // Stub — commercial decline not yet implemented
}

void ZoneSimulation::doIndOut(int x, int y, uint8_t pop, int /*landVal*/) {
    // Stub — industrial decline not yet implemented
}

// =============================================================================
// Zone Density Processing
// =============================================================================

void ZoneSimulation::doZone(int x, int y) {
    if (!currentGameMap) return;
    if (!currentGameMap->tileExists(x, y)) return;

    const Tile* tile = currentGameMap->getTile(x, y);
    if (!tile || tile->getCityZoneType() == ZoneType::None) return;

    // Stub — full zone processing not yet implemented
}

// =============================================================================
// Land Pollution Calculation
// =============================================================================

int ZoneSimulation::getLandPollutionValue(int x, int y) const {
    // Stub — pollution value calculation
    return 0;
}

// =============================================================================
// Growth Rate Tracking
// =============================================================================

void ZoneSimulation::incRateOfGrowth(int /*x*/, int /*y*/, int /*amount*/) {
    // Stub — growth rate tracking
}

// =============================================================================
// Zone Counts for Milestones
// =============================================================================

void ZoneSimulation::getZoneCounts(int& resCount, int& comCount, int& indCount) const {
    resCount = 0;
    comCount = 0;
    indCount = 0;
}

} // namespace DuneCity
