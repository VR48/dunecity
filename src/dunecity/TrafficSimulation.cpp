/*
 *  TrafficSimulation.cpp
 *
 *  BFS-based traffic connectivity with Micropolis density sampling.
 *  Each zone attempts to reach a complementary zone type via the road network:
 *    Residential -> Commercial
 *    Commercial  -> Industrial
 *    Industrial  -> Residential
 *
 *  Returns TrafficResult::Connected / NoDestination / NoRoad.
 *  On Connected, returns the ordered route for density sampling by the caller.
 */

#include <dunecity/TrafficSimulation.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>

#include <globals.h>
#include <Map.h>
#include <Tile.h>
#include <structures/StructureBase.h>

#include <algorithm>
#include <cstdlib>

namespace DuneCity {

TrafficSimulation::TrafficSimulation() = default;

void TrafficSimulation::init(CitySimulation*) {
    routeFinder_.clear();
}

bool TrafficSimulation::isRoad(int x, int y) const {
    if (!currentGameMap) return false;
    if (!currentGameMap->tileExists(x, y)) return false;
    return currentGameMap->getTile(x, y)->isRoadConnection();
}

bool TrafficSimulation::driveDone(int x, int y, ZoneType destZone) const {
    if (!currentGameMap) return false;
    // Check 4 adjacent tiles for a structure matching the destination role
    for (int d = 0; d < 4; ++d) {
        const int nx = x + DX[d];
        const int ny = y + DY[d];
        if (!currentGameMap->tileExists(nx, ny)) continue;
        const Tile* t = currentGameMap->getTile(nx, ny);
        if (!t || !t->hasANonInfantryGroundObject()) continue;
        const ObjectBase* pObj = t->getNonInfantryGroundObject();
        if (!pObj || !pObj->isAStructure()) continue;
        const StructureBase* pStruct = static_cast<const StructureBase*>(pObj);

        CityRole role = getStructureCityRole(pStruct->getItemID());
        bool match = false;
        switch (destZone) {
            case ZoneType::Residential: match = (role == CityRole::Residential); break;
            case ZoneType::Commercial:  match = (role == CityRole::Commercial);  break;
            case ZoneType::Industrial:  match = (role == CityRole::Industrial);  break;
            default: break;
        }
        if (match) return true;
    }
    return false;
}

bool TrafficSimulation::findPerimeterRoad(int zoneX, int zoneY,
                                          int& roadX, int& roadY) const {
    // 2x2 zone footprint: scan the perimeter (ring of tiles around the zone)
    // Order: top row, bottom row, left col, right col (excluding corners
    // already covered by rows).
    static const int perimDX[] = { -1, 0, 1, 2,  -1, 0, 1, 2,  -1, 2,  -1, 2 };
    static const int perimDY[] = { -1,-1,-1,-1,   2, 2, 2, 2,   0, 0,   1, 1 };
    static constexpr int kPerimCount = 12;

    for (int i = 0; i < kPerimCount; ++i) {
        const int px = zoneX + perimDX[i];
        const int py = zoneY + perimDY[i];
        if (isRoad(px, py)) {
            roadX = px;
            roadY = py;
            return true;
        }
    }
    return false;
}

bool TrafficSimulation::tryDrive(int startX, int startY, ZoneType destZone) {
    routeFinder_.clear();
    if (!currentGameMap) return false;
    return routeFinder_.find(currentGameMap->getSizeX(),currentGameMap->getSizeY(),
        {startX,startY},kMaxTrafficDistance,
        [&](int x,int y) { return isRoad(x,y); },
        [&](int x,int y) { return driveDone(x,y,destZone); });
}

int TrafficSimulation::makeTraffic(int x, int y, ZoneType destZone) {
    routeFinder_.clear();
    int roadX = 0, roadY = 0;
    if (!findPerimeterRoad(x, y, roadX, roadY)) {
        return -1;  // NoRoad
    }

    if (tryDrive(roadX, roadY, destZone)) {
        return 1;   // Connected
    }

    return 0;  // NoDestination
}

constexpr int TrafficSimulation::DX[4];
constexpr int TrafficSimulation::DY[4];

} // namespace DuneCity
