/*
 *  CityEffectsRuntime.cpp
 *
 *  Implementation of CitySimulation's full-map effect scans and zone growth.
 *  Split out from CitySimulation.cpp because these symbols pull in Map, Tile,
 *  StructureBase, ZoneStructure, and House — heavy dependencies that the test
 *  harness deliberately does not link. The unit tests link CitySimulation.cpp
 *  with empty stubs from CityEffectsRuntime_test_stub.cpp instead.
 */

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>
#include <dunecity/TrafficSimulation.h>
#include <dunecity/ZonePower.h>

#include <globals.h>
#include <Game.h>
#include <Map.h>
#include <Tile.h>
#include <House.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>

#include <SDL2/SDL_log.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

/// The "level" used by the city sim for any city-role structure: zones
/// read it from tile density, non-zone city-role structures read it from
/// StructureBase::cityOccupancy_. Returns 0 for non-role structures.
///
/// Non-zone city-role structures (Refinery, Silo, Factories, RepairYard,
/// Radar, IX, HighTech) are floored at level 1: the player paid to build
/// them, so they should provide their tier-1 jobs immediately rather than
/// sitting "vacant" waiting on residents that won't move in without jobs.
int cityLevelOf(const Tile* t, const StructureBase* pStruct) {
    if (!pStruct) return 0;
    if (DuneCity::getStructureCityRole(pStruct->getItemID()) == DuneCity::CityRole::None) {
        return 0;
    }
    if (dynamic_cast<const ZoneStructure*>(pStruct)) {
        return t ? t->getCityZoneDensity() : 0;
    }
    const int occ = pStruct->getCityOccupancy();
    return occ > 0 ? occ : 1;
}

template<typename F>
void forEachStructureOrigin(const Map& map, F&& visit) {
    for (int y = 0; y < map.getSizeY(); ++y) {
        for (int x = 0; x < map.getSizeX(); ++x) {
            const Tile* pTile = map.getTile(x, y);
            if (!pTile || !pTile->hasANonInfantryGroundObject()) continue;

            const ObjectBase* pObj = pTile->getNonInfantryGroundObject();
            if (!pObj || !pObj->isAStructure()) continue;

            const StructureBase* pStruct = static_cast<const StructureBase*>(pObj);
            if (pStruct->getLocation().x != x || pStruct->getLocation().y != y) {
                continue;  // skip non-origin tiles of multi-tile structures
            }
            visit(x, y, pStruct);
        }
    }
}

void stampFalloff(DuneCity::CityMapLayer<uint8_t>& dst,
                  int cx, int cy, int radius, int value, int maxValue) {
    if (radius <= 0 || value == 0) return;
    const int blockSize = dst.getBlockSize();
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int dist = std::max(std::abs(dx), std::abs(dy));  // Chebyshev
            if (dist > radius) continue;
            const int contribution =
                DuneCity::falloff(value, dist, radius + 1);
            if (contribution == 0) continue;

            const int wx = cx + dx;
            const int wy = cy + dy;
            const int bx = wx / blockSize;
            const int by = wy / blockSize;
            int current = dst.get(bx, by);
            int updated = current + contribution;
            if (updated < 0)        updated = 0;
            if (updated > maxValue) updated = maxValue;
            dst.set(bx, by, static_cast<uint8_t>(updated));
        }
    }
}

}  // namespace

namespace DuneCity {

int32_t CitySimulation::getTotalFunds() const {
    return pLocalHouse ? static_cast<int32_t>(pLocalHouse->getCredits()) : totalFunds_;
}

bool CitySimulation::spendCityFunds(int32_t amount) {
    if (!pLocalHouse) return false;
    if (pLocalHouse->getCredits() >= amount) {
        pLocalHouse->takeCredits(FixPoint(amount));
        return true;
    }
    return false;
}

void CitySimulation::runEffectsScans() {
    if (!currentGameMap) return;

    // Reset pollution and land value at scan start. Crime is intentionally
    // NOT reset here: SC's land-value scan reads crime from the previous
    // tick (see scan.cpp line 279 — `if (crimeRateMap.get(x, y) > 190)`),
    // and we mirror that one-tick feedback delay below. Crime is reset
    // and recomputed AFTER the land-value pass finishes.
    pollutionDensityMap_.init(mapWidth_, mapHeight_, pollutionDensityMap_.getBlockSize());
    landValueMap_      .init(mapWidth_, mapHeight_, landValueMap_      .getBlockSize());

    const Map& map = *currentGameMap;

    // Pollution emission. Both zones and non-zone industrial structures
    // pollute proportional to their current level.
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const Tile* t = map.getTile(x, y);
        const int level = cityLevelOf(t, pStruct);
        const int emission = getPollutionEmission(pStruct->getItemID(), level);
        if (emission > 0) {
            stampFalloff(pollutionDensityMap_, x, y,
                         kPollutionRadius, emission, kMaxPollution);
        }
    });

    // SC pollution scan smooths twice (scan.cpp lines 298-299), which is
    // what gives a single power plant a soft-edged halo rather than the
    // hard-cut stamp radius. We approximate with two box-smooths over the
    // pollution map after stamping.
    {
        const int bs = pollutionDensityMap_.getBlockSize();
        const int bw = (mapWidth_  + bs - 1) / bs;
        const int bh = (mapHeight_ + bs - 1) / bs;
        auto smoothPass = [&]() {
            std::vector<uint8_t> tmp(bw * bh, 0);
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    int z = pollutionDensityMap_.get(bx, by);
                    if (bx > 0)      z += pollutionDensityMap_.get(bx - 1, by);
                    if (bx < bw - 1) z += pollutionDensityMap_.get(bx + 1, by);
                    if (by > 0)      z += pollutionDensityMap_.get(bx, by - 1);
                    if (by < bh - 1) z += pollutionDensityMap_.get(bx, by + 1);
                    // SC `smoothDitherMap` non-dither path: (center + 4
                    // neighbors) >> 2. (scan.cpp lines 544-556.)
                    z >>= 2;
                    if (z > kMaxPollution) z = kMaxPollution;
                    tmp[by * bw + bx] = static_cast<uint8_t>(z);
                }
            }
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    pollutionDensityMap_.set(bx, by, tmp[by * bw + bx]);
                }
            }
        };
        smoothPass();
        smoothPass();
    }

    // ---- Land value, SimCity Classic style ---------------------------------
    //
    // SC formula (scan.cpp::pollutionTerrainLandValueScan):
    //     base = 4 * (34 - cityCenterDistance / 2)
    //     base += terrainDensity   // smoothed near-water/tree score
    //     base -= pollution
    //     if crime > 190: base -= 20
    //     clamp(1, 250) if developed, else 0
    //
    // "Treat sand like water": Dune is mostly sand, so we use sand+dunes
    // tiles as the SC "water/tree" feature. Each non-developed sand tile
    // contributes to a per-block "terrain feature density" map, smoothed
    // twice to give nearby blocks a graduated bonus. The "developed"
    // gate (only tiles touching a road or structure get a value > 0)
    // keeps land value 0 in the empty desert so growth gates work as
    // they should.
    const int bs = std::max(1, landValueMap_.getBlockSize());
    const int blocksW = (mapWidth_  + bs - 1) / bs;
    const int blocksH = (mapHeight_ + bs - 1) / bs;

    // Per-block terrain feature count (raw, pre-smooth).
    std::vector<int> terrainRaw(blocksW * blocksH, 0);
    // Per-block "developed" flag — tile has a road or any ground object.
    std::vector<uint8_t> developed(blocksW * blocksH, 0);

    for (int wy = 0; wy < mapHeight_; ++wy) {
        for (int wx = 0; wx < mapWidth_; ++wx) {
            const Tile* t = map.getTile(wx, wy);
            if (!t) continue;
            const int bx = wx / bs;
            const int by = wy / bs;
            const int idx = by * blocksW + bx;
            if ((t->isSand() || t->isDunes()) && !t->hasAGroundObject()) {
                // Sand-as-water: on Dune, sand is the scenic "feature" that
                // SimCity's water/tree counted as. The header constant
                // pushes per-tile contribution well above SC's +15 so a 2x2
                // zone adjacent to a band of sand reliably clears the
                // tier-3 land-value threshold (150) after the two smooth
                // passes below.
                terrainRaw[idx] += kSandTerrainRawBonus;
            }
            if (t->isRoad() || t->hasAGroundObject()) {
                developed[idx] = 1;
            }
        }
    }

    // Two box-smooth passes (SC runs three smoothTerrain passes; two is
    // enough at our smaller map sizes to spread the feature score one
    // block in each direction with falloff).
    auto smoothBlocks = [&](std::vector<int>& src) {
        std::vector<int> dst(src.size(), 0);
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                const int center = src[by * blocksW + bx];
                int sum = center * 4;
                if (bx > 0)            sum += src[by * blocksW + (bx - 1)];
                if (bx < blocksW - 1)  sum += src[by * blocksW + (bx + 1)];
                if (by > 0)            sum += src[(by - 1) * blocksW + bx];
                if (by < blocksH - 1)  sum += src[(by + 1) * blocksW + bx];
                dst[by * blocksW + bx] = sum / 8;
            }
        }
        src = std::move(dst);
    };
    smoothBlocks(terrainRaw);
    smoothBlocks(terrainRaw);

    // Per-CITY centres, not a single global centroid.
    //
    // SC's formula assumes one contiguous city: the centroid of all
    // developed tiles IS the city's heart. On a two-player map with
    // cities in opposite corners, a global centroid lands in the empty
    // desert between them — making `dis = 34 - dist/2` clamp to 0 for
    // every block in both cities, killing the within-city gradient and
    // breaking the dist contribution entirely.
    //
    // Fix: flood-fill developed blocks into clusters and give each
    // cluster its own centroid. Every developed block measures distance
    // to ITS cluster's centroid, recovering SC's "centre is best, edges
    // less so" gradient inside each city independently.
    std::vector<int> clusterId(blocksW * blocksH, -1);
    std::vector<long long> sumX, sumY;
    std::vector<long long> count;
    {
        std::vector<std::pair<int,int>> queue;
        queue.reserve(blocksW * blocksH);
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                if (!developed[by * blocksW + bx]) continue;
                if (clusterId[by * blocksW + bx] != -1) continue;
                const int cid = static_cast<int>(sumX.size());
                sumX.push_back(0); sumY.push_back(0); count.push_back(0);
                queue.clear();
                queue.push_back({bx, by});
                clusterId[by * blocksW + bx] = cid;
                while (!queue.empty()) {
                    auto [qx, qy] = queue.back();
                    queue.pop_back();
                    sumX[cid] += qx * bs;
                    sumY[cid] += qy * bs;
                    count[cid] += 1;
                    static const int DX[4] = {1, -1, 0, 0};
                    static const int DY[4] = {0, 0, 1, -1};
                    for (int d = 0; d < 4; ++d) {
                        const int nx = qx + DX[d];
                        const int ny = qy + DY[d];
                        if (nx < 0 || ny < 0 || nx >= blocksW || ny >= blocksH) continue;
                        const int nidx = ny * blocksW + nx;
                        if (!developed[nidx] || clusterId[nidx] != -1) continue;
                        clusterId[nidx] = cid;
                        queue.push_back({nx, ny});
                    }
                }
            }
        }
    }
    std::vector<int> clusterCenterWx(sumX.size()), clusterCenterWy(sumX.size());
    for (size_t c = 0; c < sumX.size(); ++c) {
        clusterCenterWx[c] = static_cast<int>(sumX[c] / count[c]);
        clusterCenterWy[c] = static_cast<int>(sumY[c] / count[c]);
    }

    // SC formula (scan.cpp::pollutionTerrainLandValueScan), evaluated per
    // block. Distance is measured to THIS block's cluster centre.
    //     dis = 34 - getCityCenterDistance(worldX, worldY) / 2;
    //     dis = dis * 4;
    //     dis += terrainDensity;
    //     dis -= pollution;
    //     clamp(1, 250) if developed, else 0
    // Manhattan distance is clamped to 64 inside computeBaseLandValue.
    for (int by = 0; by < blocksH; ++by) {
        for (int bx = 0; bx < blocksW; ++bx) {
            const int idx = by * blocksW + bx;
            if (!developed[idx]) {
                landValueMap_.set(bx, by, 0);
                continue;
            }
            const int worldX = bx * bs;
            const int worldY = by * bs;
            const int cid = clusterId[idx];
            const int dist = std::abs(worldX - clusterCenterWx[cid])
                           + std::abs(worldY - clusterCenterWy[cid]);
            // Crime is read from the PREVIOUS scan tick — crimeRateMap_
            // is reset and recomputed below. This matches SC's one-tick
            // feedback delay; on the very first scan crime is zero and
            // no -20 penalty fires.
            const int v = computeBaseLandValue(
                dist,
                terrainRaw[idx],
                pollutionDensityMap_.get(bx, by),
                crimeRateMap_.get(bx, by));
            landValueMap_.set(bx, by, static_cast<uint8_t>(v));
        }
    }

    // Park-style land-value bonuses (Wall, Turrets). Layered on top of
    // the SC base so defensive structures still uplift adjacent value.
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const int parkBonus = getParkLandValueBonus(pStruct->getItemID());
        if (parkBonus > 0) {
            // Stadium radiates its bonus over a wider area (8 tiles vs 3
            // for walls/turrets) to match its civic-amenity role.
            const int radius = (pStruct->getItemID() == Structure_Stadium)
                ? 8 : kParkBonusRadius;
            stampFalloff(landValueMap_, x, y,
                         radius, parkBonus, kMaxLandValue);
        }
    });

    // "Sand-as-water" — long-reach bonus from open sand/dunes. The
    // terrainRaw+smooth pass above gives close-adjacency a localised
    // boost, but the /8 dilution in box-smooth means a zone more than
    // one block away from sand sees almost nothing. Stamping a falloff
    // from each sand tile bypasses that dilution, so a zone sitting at
    // the edge of the rock can reliably reach tier 3 (luxury) when sand
    // is on one or two sides. Stamps accumulate per source tile, so a
    // solid sand band produces a stronger lift than an isolated tile —
    // matching SC's "shore property is premium" intent.
    for (int wy = 0; wy < mapHeight_; ++wy) {
        for (int wx = 0; wx < mapWidth_; ++wx) {
            const Tile* t = map.getTile(wx, wy);
            if (!t) continue;
            if ((t->isSand() || t->isDunes()) && !t->hasAGroundObject()) {
                stampFalloff(landValueMap_, wx, wy,
                             kSandBonusRadius, kSandLandValueBonus,
                             kMaxLandValue);
            }
        }
    }

    // Population density is needed by the crime formula below (SC's
    // `z += populationDensityMap.worldGet(x, y)` term), so populate it
    // BEFORE clearing and recomputing crime. The traffic map is built
    // later — it's not consumed by any scan, only the overlay UI.
    populationDensityMap_.init(mapWidth_, mapHeight_, populationDensityMap_.getBlockSize());
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const Tile* originTile = map.getTile(x, y);
        const int level = cityLevelOf(originTile, pStruct);
        if (level <= 0) return;
        if (getStructureCityRole(pStruct->getItemID()) != CityRole::Residential) return;
        const int pop      = getZonePopulation(pStruct->getItemID(), level);
        const int popStamp = std::min(255, std::max(0, pop / 8));
        stampFalloff(populationDensityMap_, x, y,
                     /*radius*/ 2, popStamp, /*max*/ 255);
    });

    // Base crime, SC-Classic style (scan.cpp lines 425-432):
    //   z = 128 - landValue + popDensity, clamp(<300) before police,
    //   then clamp(0, 250) after police coverage subtraction.
    // The crime map was intentionally NOT reset at scan start so the
    // land-value pass above could read last tick's crime for the
    // `crime > 190 → -20 land value` feedback. Reset and recompute now.
    crimeRateMap_.init(mapWidth_, mapHeight_, crimeRateMap_.getBlockSize());
    {
        const int bw = (mapWidth_  + crimeRateMap_.getBlockSize() - 1) / crimeRateMap_.getBlockSize();
        const int bh = (mapHeight_ + crimeRateMap_.getBlockSize() - 1) / crimeRateMap_.getBlockSize();
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                const int lv  = landValueMap_.get(bx, by);
                const int pop = populationDensityMap_.get(bx, by);
                const int baseCrime = computeBaseCrime(lv, pop);
                crimeRateMap_.set(bx, by, static_cast<uint8_t>(baseCrime));
            }
        }
    }
    // Police coverage AND nominal cost in one pass.
    //
    // Coverage is applied for ALL owners — each side's police stations
    // protect that side's zones. Previously this was gated to the local
    // player only, with the result that AI-owned cities had uncontrolled
    // crime, which dropped their land value, which jammed zone growth at
    // density 0. In a multi-player or skirmish scenario, every city
    // needs working police for its own zones to develop.
    //
    // Nominal cost, however, stays local-only: the city budget UI pays
    // the bill for the local player's police, not the AI's. The AI's
    // police effectively run at 100% funding (no funding-slider UI).
    int32_t nominalCost = 0;
    const int fundingPct = policeFundingPercent_;
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const int itemID = pStruct->getItemID();
        const int rawCoverage = getPoliceCoverage(itemID);
        if (rawCoverage <= 0) return;

        const bool isLocal = (pStruct->getOwner() == pLocalHouse);
        const int effectiveFundingPct = isLocal ? fundingPct : 100;
        const int coverage = (rawCoverage * effectiveFundingPct) / 100;
        if (coverage > 0) {
            stampFalloff(crimeRateMap_, x, y,
                         kPoliceRadius, -coverage, kMaxCrime);
        }
        if (isLocal) {
            nominalCost += getPoliceAnnualCost(itemID);
        }
    });
    nominalPoliceCost_ = nominalCost;

    // Detect civic buildings for demand cap computation.
    hasStadium_ = false;
    hasPalace_  = false;
    hasAirport_ = false;
    hasStarport_ = false;
    forEachStructureOrigin(map, [&](int /*x*/, int /*y*/, const StructureBase* pStruct) {
        switch (pStruct->getItemID()) {
            case Structure_Stadium: hasStadium_ = true; break;
            case Structure_Palace:  hasPalace_  = true; break;
            case Structure_Airport: hasAirport_ = true; break;
            case Structure_StarPort: hasStarport_ = true; break;
            default: break;
        }
    });

    // Traffic density map — now driven by actual BFS connectivity results
    // during runZoneGrowth(). The overlay starts from a base stamp (every
    // city-role structure radiates proportional to level) then BFS-connected
    // paths add density on top, and roads absorb load.
    trafficDensityMap_.init(mapWidth_, mapHeight_, trafficDensityMap_.getBlockSize());
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const Tile* originTile = map.getTile(x, y);
        const int level = cityLevelOf(originTile, pStruct);
        if (level <= 0) return;
        if (getStructureCityRole(pStruct->getItemID()) == CityRole::None) return;
        const int trafficValue = level * 25;
        stampFalloff(trafficDensityMap_, x, y,
                     /*radius*/ 3, trafficValue, /*max*/ 255);
    });

    // Roads absorb traffic.
    const int trafficBlockSize = trafficDensityMap_.getBlockSize();
    for (int wy = 0; wy < mapHeight_; ++wy) {
        for (int wx = 0; wx < mapWidth_; ++wx) {
            const Tile* t = map.getTile(wx, wy);
            if (!t || !t->isRoad()) continue;
            const int bx = wx / trafficBlockSize;
            const int by = wy / trafficBlockSize;
            int v = trafficDensityMap_.get(bx, by);
            v -= 15;
            if (v < 0) v = 0;
            trafficDensityMap_.set(bx, by, static_cast<uint8_t>(v));
        }
    }

    // Decay growth rate map toward zero.
    decayGrowthRateMap();
}

void CitySimulation::runZoneGrowth() {
    if (!currentGameMap) return;

    const Map& map = *currentGameMap;
    const int bs = landValueMap_.getBlockSize();

    // We treat zones AND non-zone city-role structures the same: each has
    // a current "level" (0..maxLevel), each contributes supply at that
    // level, each can grow toward its maxLevel as long as it's powered,
    // land-value floor met, and demand thresholds satisfied.
    struct CityNode {
        int x, y;
        StructureBase* pStruct;
        ZoneStructure*  pZone;     // nullptr for non-zone city-role structures
        CityRole       role;
        int            level;
        int            maxLevel;
        int            supplyR, supplyC, supplyI;
        bool           isDuneStructure;  // true for non-zone city-role (don't destroy)
    };
    std::vector<CityNode> nodes;

    for (int y = 0; y < map.getSizeY(); ++y) {
        for (int x = 0; x < map.getSizeX(); ++x) {
            const Tile* t = map.getTile(x, y);
            if (!t || !t->hasANonInfantryGroundObject()) continue;
            ObjectBase* pObj = const_cast<Tile*>(t)->getNonInfantryGroundObject();
            if (!pObj || !pObj->isAStructure()) continue;
            StructureBase* pStruct = static_cast<StructureBase*>(pObj);
            if (pStruct->getLocation().x != x || pStruct->getLocation().y != y) continue;

            const int itemID = pStruct->getItemID();
            const CityRole role = getStructureCityRole(itemID);
            if (role == CityRole::None) continue;

            ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(pStruct);
            const int level = pZone ? t->getCityZoneDensity()
                                    : std::max<int>(1, pStruct->getCityOccupancy());
            const int maxLevel = getStructureMaxLevel(itemID);

            nodes.push_back({
                x, y, pStruct, pZone, role, level, maxLevel,
                getResidentialSupply(itemID, level),
                getCommercialSupply (itemID, level),
                getIndustrialSupply (itemID, level),
                pZone == nullptr,
            });
        }
    }

    int totalResidentialSupply = 0;
    int totalJobSupply = 0;
    for (const auto& n : nodes) {
        totalResidentialSupply += n.supplyR;
        totalJobSupply         += n.supplyC + n.supplyI;
    }
    int freeResidents = std::max(0, totalResidentialSupply - totalJobSupply);

    // ---- Compute RCI demand valves BEFORE the growth loop ----
    // Uses the Micropolis-shaped employment/migration/births model with
    // civic cap influence from Stadium/Palace/Airport/Starport.
    {
        int curRes = 0, curCom = 0, curInd = 0;
        for (const auto& n : nodes) {
            const int pop = getZonePopulation(n.pStruct->getItemID(), n.level);
            switch (n.role) {
                case CityRole::Residential: curRes += pop; break;
                case CityRole::Commercial:  curCom += pop; break;
                case CityRole::Industrial:  curInd += pop; break;
                default: break;
            }
        }

        ValveInputs vi;
        vi.resPop = curRes;
        vi.comPop = curCom;
        vi.indPop = curInd;
        vi.taxRate = cityTax_;
        vi.hasStadium  = hasStadium_;
        vi.hasPalace   = hasPalace_;
        vi.hasAirport  = hasAirport_;
        vi.hasStarport = hasStarport_;
        vi.popThreshold = 2000;

        const ValveOutputs vo = computeDemandValves(vi);
        resValve_ = vo.resValve;
        comValve_ = vo.comValve;
        indValve_ = vo.indValve;
    }

    // Traffic connectivity engine (BFS along road network).
    TrafficSimulation trafficSim;
    trafficSim.init(this);

    // Initialize growth rate map if needed.
    if (growthRateMap_.getBlockSize() == 0 || mapWidth_ == 0) {
        growthRateMap_.init(mapWidth_, mapHeight_, 2);
    }

    for (auto& n : nodes) {
        const Coord pos = n.pStruct->getLocation();
        if (pos.isInvalid()) continue;

        const int targetLevel = n.level + 1;

        // Local supply within kSupplyRadius.
        int localComm = 0, localInd = 0, localRes = 0;
        for (const auto& s : nodes) {
            const int dist = std::max(std::abs(s.x - pos.x), std::abs(s.y - pos.y));
            if (dist > kSupplyRadius) continue;
            localComm += s.supplyC;
            localInd  += s.supplyI;
            localRes  += s.supplyR;
        }

        const int landValue = landValueMap_.get(pos.x / bs, pos.y / bs);
        const int pollution = pollutionDensityMap_.get(
            pos.x / pollutionDensityMap_.getBlockSize(),
            pos.y / pollutionDensityMap_.getBlockSize());
        const int crime = crimeRateMap_.get(
            pos.x / crimeRateMap_.getBlockSize(),
            pos.y / crimeRateMap_.getBlockSize());
        const bool powered = n.pStruct->getOwner() &&
                             n.pStruct->getOwner()->getProducedPower() >=
                             n.pStruct->getOwner()->getPowerRequirement();

        bool grew = false;
        bool declined = false;

        // Traffic connectivity attempt: BFS along road network to find
        // the complementary zone type. R->C, C->I, I->R.
        TrafficResult traffic = TrafficResult::Connected;  // default for L0
        if (n.level > 0) {
            ZoneType destZone = ZoneType::None;
            switch (n.role) {
                case CityRole::Residential: destZone = ZoneType::Commercial;  break;
                case CityRole::Commercial:  destZone = ZoneType::Industrial;  break;
                case CityRole::Industrial:  destZone = ZoneType::Residential; break;
                default: break;
            }
            if (destZone != ZoneType::None) {
                const int trafResult = trafficSim.makeTraffic(pos.x, pos.y, destZone);
                if (trafResult < 0)     traffic = TrafficResult::NoRoad;
                else if (trafResult == 0) traffic = TrafficResult::NoDestination;
                else                      traffic = TrafficResult::Connected;

                // Successful traffic stamps density along all visited road
                // tiles, matching SC's addToTrafficDensityMap() pattern.
                if (traffic == TrafficResult::Connected) {
                    const int tbs = trafficDensityMap_.getBlockSize();
                    for (const auto& pt : trafficSim.getLastPath()) {
                        const int tbx = pt.x / tbs;
                        const int tby = pt.y / tbs;
                        int tv = trafficDensityMap_.get(tbx, tby) + 50;
                        if (tv > 240) tv = 240;  // SC cap
                        trafficDensityMap_.set(tbx, tby, static_cast<uint8_t>(tv));
                    }
                }
            }
        }

        // Zone score: globalValve + local evaluation (now includes traffic,
        // pollution, crime, and land value per zone type).
        const int valve = (n.role == CityRole::Residential) ? resValve_
                        : (n.role == CityRole::Commercial)  ? comValve_
                        :                                     indValve_;
        int localEval = computeLocalEval(n.role, landValue, pollution,
                                         crime, traffic);
        // Commercial zones get supply-side and airport adjustments on top
        // of the base local eval (spec Section D: commercial desirability).
        if (n.role == CityRole::Commercial) {
            localEval += computeCommercialRate(localRes, localInd, hasAirport_);
        }
        const int zscore = computeZscore(valve, localEval, powered);

        // Stochastic growth gate: 1-in-16 chance per day.
        // L0->L1 bootstrap skips the gate.
        const uint32_t roll = (static_cast<uint32_t>(pos.x) * 73u
                             + static_cast<uint32_t>(pos.y) * 149u
                             + lastProcessedDay_) % 16u;
        const bool pollutionBlocked = isPollutionBlockingGrowth(pollution, n.role, targetLevel);
        const bool pollutionSlowed  = isPollutionSlowingGrowth(pollution, n.role);
        const bool growthRolled = (n.level == 0)
            || (roll == 0 && (!pollutionSlowed || (lastProcessedDay_ % 2u == 0)));

        // --- Growth attempt: requires zscore above threshold ---
        if (zscore > kZscoreGrowthGate && n.level < n.maxLevel
            && growthRolled && !pollutionBlocked) {
            const int lvFloor = getDemandLandValueFloor(targetLevel);
            if (landValue >= lvFloor) {
                bool meets = false;
                const bool bootstrapping = (n.level == 0);
                switch (n.role) {
                    case CityRole::Residential:
                        meets = bootstrapping
                             || (localComm + localInd) >= getDemandJobsThreshold(targetLevel);
                        break;
                    case CityRole::Commercial:
                        meets = bootstrapping
                             || (localRes >= getDemandResidentialThreshold(targetLevel)
                                 && localInd >= getDemandJobsThreshold(targetLevel) / 2);
                        break;
                    case CityRole::Industrial:
                        meets = bootstrapping
                             || localRes >= getDemandResidentialThreshold(targetLevel);
                        break;
                    default: break;
                }
                // Transport gate: growth beyond level 1 requires road
                // connectivity (not just adjacency). No road = no growth.
                if (meets && !bootstrapping && traffic == TrafficResult::NoRoad) {
                    meets = false;
                }

                if (meets) {
                    if (n.pZone) {
                        for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                            for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                                Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                                if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(targetLevel));
                            }
                        }
                        n.pZone->refreshZonePowerDraw();
                    } else {
                        n.pStruct->setCityOccupancy(static_cast<uint8_t>(targetLevel));
                    }
                    n.level = targetLevel;
                    if (n.role != CityRole::Residential) {
                        const int newSupply = (n.role == CityRole::Commercial)
                            ? getCommercialSupply (n.pStruct->getItemID(), targetLevel)
                            : getIndustrialSupply (n.pStruct->getItemID(), targetLevel);
                        const int prevSupply = (n.role == CityRole::Commercial) ? n.supplyC : n.supplyI;
                        const int delta = std::max(0, newSupply - prevSupply);
                        freeResidents = std::max(0, freeResidents - delta);
                    }
                    grew = true;

                    // Update growth rate map.
                    {
                        const int gbs = growthRateMap_.getBlockSize();
                        const int gbx = pos.x / gbs;
                        const int gby = pos.y / gbs;
                        int gr = growthRateMap_.get(gbx, gby) + kGrowthRateIncrement;
                        if (gr > 127) gr = 127;
                        growthRateMap_.set(gbx, gby, static_cast<int8_t>(gr));
                    }

                    SDL_Log("[CitySim] %s GREW (%d,%d) item=%d %d->%d "
                            "zscore=%d valve=%d lv=%d poll=%d crime=%d traf=%d",
                            n.pZone ? "zone" : "bldg",
                            pos.x, pos.y, n.pStruct->getItemID(),
                            n.level - 1, n.level,
                            zscore, valve, landValue, pollution, crime,
                            static_cast<int>(traffic));
                }
            }
        }

        // --- Demand-based decline ---
        // Non-zone Dune structures (Refinery, Silo, etc.) decline in
        // occupancy but are never destroyed. Zones can go to L0 (vacant).
        if (!grew && n.level > 0) {
            const uint32_t declineRoll = (static_cast<uint32_t>(pos.x) * 97u
                                        + static_cast<uint32_t>(pos.y) * 173u
                                        + lastProcessedDay_) % 16u;
            if (shouldZoneDecline(zscore, n.level, declineRoll)) {
                // Non-zone Dune structures floor at level 1 (never destroyed).
                const int floorLevel = n.isDuneStructure ? 1 : 0;
                const int newLevel = std::max(floorLevel, n.level - 1);
                if (newLevel < n.level) {
                    if (n.pZone) {
                        for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                            for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                                Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                                if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(newLevel));
                            }
                        }
                        n.pZone->refreshZonePowerDraw();
                    } else {
                        n.pStruct->setCityOccupancy(static_cast<uint8_t>(newLevel));
                    }
                    n.level = newLevel;
                    declined = true;

                    // Update growth rate map (negative).
                    {
                        const int gbs = growthRateMap_.getBlockSize();
                        const int gbx = pos.x / gbs;
                        const int gby = pos.y / gbs;
                        int gr = growthRateMap_.get(gbx, gby) + kGrowthRateDecrement;
                        if (gr < -128) gr = -128;
                        growthRateMap_.set(gbx, gby, static_cast<int8_t>(gr));
                    }

                    SDL_Log("[CitySim] %s DECLINED (%d,%d) item=%d %d->%d "
                            "zscore=%d valve=%d lv=%d poll=%d crime=%d traf=%d (demand)",
                            n.pZone ? "zone" : "bldg",
                            pos.x, pos.y, n.pStruct->getItemID(),
                            n.level + 1, n.level,
                            zscore, valve, landValue, pollution, crime,
                            static_cast<int>(traffic));
                }
            }
        }

        // Power-starved decline: guaranteed drop when unpowered.
        // Non-zone Dune structures floor at level 1.
        if (!grew && !declined && !powered && n.level > 1) {
            const int floorLevel = n.isDuneStructure ? 1 : 1;  // both floor at 1 for power
            const int newLevel = std::max(floorLevel, n.level - 1);
            if (newLevel < n.level) {
                if (n.pZone) {
                    for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                        for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                            Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                            if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(newLevel));
                        }
                    }
                    n.pZone->refreshZonePowerDraw();
                } else {
                    n.pStruct->setCityOccupancy(static_cast<uint8_t>(newLevel));
                }
                n.level = newLevel;
                SDL_Log("[CitySim] %s DECLINED (%d,%d) item=%d %d->%d (power-starved)",
                        n.pZone ? "zone" : "bldg",
                        pos.x, pos.y, n.pStruct->getItemID(),
                        n.level + 1, n.level);
            }
        }
    }

    // Traffic pollution: road blocks with heavy traffic density contribute
    // pollution, closing SC's feedback loop where busy roads degrade nearby
    // residential value. Applied after all BFS trips have stamped density.
    {
        const int tbs = trafficDensityMap_.getBlockSize();
        const int pbs = pollutionDensityMap_.getBlockSize();
        const int tw = (mapWidth_  + tbs - 1) / tbs;
        const int th = (mapHeight_ + tbs - 1) / tbs;
        for (int tby = 0; tby < th; ++tby) {
            for (int tbx = 0; tbx < tw; ++tbx) {
                const int tp = getTrafficPollution(trafficDensityMap_.get(tbx, tby));
                if (tp <= 0) continue;
                // Map traffic block coords to pollution block coords.
                const int wx = tbx * tbs;
                const int wy = tby * tbs;
                const int pbx = wx / pbs;
                const int pby = wy / pbs;
                int p = pollutionDensityMap_.get(pbx, pby) + tp;
                if (p > kMaxPollution) p = kMaxPollution;
                pollutionDensityMap_.set(pbx, pby, static_cast<uint8_t>(p));
            }
        }
    }

    // Recompute population totals.
    int newRes = 0, newCom = 0, newInd = 0;
    for (const auto& n : nodes) {
        const int pop = getZonePopulation(n.pStruct->getItemID(), n.level);
        switch (n.role) {
            case CityRole::Residential: newRes += pop; break;
            case CityRole::Commercial:  newCom += pop; break;
            case CityRole::Industrial:  newInd += pop; break;
            default: break;
        }
    }
    if (newRes != resPop_ || newCom != comPop_ || newInd != indPop_) {
        SDL_Log("[CitySim] population R:%d->%d C:%d->%d I:%d->%d total=%d",
                resPop_, newRes, comPop_, newCom, indPop_, newInd,
                newRes + newCom + newInd);
    }
    resPop_ = newRes;
    comPop_ = newCom;
    indPop_ = newInd;
}

void CitySimulation::decayGrowthRateMap() {
    const int gbs = growthRateMap_.getBlockSize();
    if (gbs <= 0) return;
    const int gw = (mapWidth_  + gbs - 1) / gbs;
    const int gh = (mapHeight_ + gbs - 1) / gbs;
    for (int by = 0; by < gh; ++by) {
        for (int bx = 0; bx < gw; ++bx) {
            int v = growthRateMap_.get(bx, by);
            if (v > 0) {
                v -= kGrowthRateDecay;
                if (v < 0) v = 0;
            } else if (v < 0) {
                v += kGrowthRateDecay;
                if (v > 0) v = 0;
            }
            growthRateMap_.set(bx, by, static_cast<int8_t>(v));
        }
    }
}

void CitySimulation::runDailyBudget() {
    if (!currentGameMap) return;
    const Map& map = *currentGameMap;

    // Per-house aggregation: total city population (for tax) and total
    // police annual cost (for the bill). Walked once across the map so
    // each owner's bill is independent of the others — every player,
    // human or AI, pays for their own police and collects their own
    // taxes. Revenue and costs are divided by kBudgetTicksPerYear (50)
    // so each 1-second tick delivers 1/50th of the annual amount.
    struct HouseBudget {
        int     pop       = 0;
        int32_t policeCost = 0;
    };
    std::vector<std::pair<House*, HouseBudget>> houseBudgets;

    auto findOrAdd = [&](House* h) -> HouseBudget& {
        for (auto& [hh, hb] : houseBudgets) {
            if (hh == h) return hb;
        }
        houseBudgets.push_back({h, HouseBudget{}});
        return houseBudgets.back().second;
    };

    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const House* constOwner = pStruct->getOwner();
        if (!constOwner) return;
        House* owner = currentGame->getHouse(constOwner->getHouseID());
        if (!owner) return;
        const Tile* t = map.getTile(x, y);
        const int itemID = pStruct->getItemID();
        HouseBudget& hb = findOrAdd(owner);

        if (getStructureCityRole(itemID) != CityRole::None) {
            const int level = cityLevelOf(t, pStruct);
            hb.pop += getZonePopulation(itemID, level);
        }
        hb.policeCost += getPoliceAnnualCost(itemID);
    });

    int32_t localRevenue = 0;
    int32_t localPaid    = 0;
    bool    localTouched = false;

    for (auto& [house, hb] : houseBudgets) {
        // Annual values divided by days-per-year for smooth daily payout
        const int32_t annualRevenue = computeAnnualTaxRevenue(hb.pop, cityTax_);
        const int fundingPct = (house == pLocalHouse) ? policeFundingPercent_ : 100;
        const int32_t annualPaid = (hb.policeCost * fundingPct) / 100;

        const int32_t dailyRevenue = annualRevenue / kBudgetTicksPerYear;
        const int32_t dailyPaid    = annualPaid    / kBudgetTicksPerYear;
        const int32_t net = dailyRevenue - dailyPaid;

        if (net > 0) {
            house->returnCredits(FixPoint(net));
        } else if (net < 0) {
            house->takeCredits(FixPoint(-net));
        }

        if (house == pLocalHouse) {
            // Store annualised figures for the budget UI
            localRevenue = annualRevenue;
            localPaid    = annualPaid;
            localTouched = true;
        }

        // Log once per city year to avoid spam (48 ticks/year)
        if (cityDay_ == 0) {
            SDL_Log("[CitySim] year=%d house=%d pop=%d rate=%d%% annual_revenue=%d annual_police=%d daily_net=%+d",
                    cityYear_, house->getHouseID(), hb.pop, cityTax_,
                    annualRevenue, annualPaid, net);
        }
    }

    // Update local-player UI state. Budget UI shows annualised figures;
    // the actual credit pool gets the daily slice via returnCredits/takeCredits.
    if (localTouched) {
        lastPoliceExpense_ = localPaid;
        budget_.setLastTaxRevenue(localRevenue);
    }
}

}  // namespace DuneCity
