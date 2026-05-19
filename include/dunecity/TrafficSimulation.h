#ifndef DUNECITY_TRAFFICSIMULATION_H
#define DUNECITY_TRAFFICSIMULATION_H

#include <dunecity/CityConstants.h>
#include <dunecity/CityMapLayer.h>

#include <vector>
#include <cstdint>

class Map;
class Tile;

namespace DuneCity {

class CitySimulation;

/**
 * Traffic pathfinding and density tracking ported from Micropolis traffic.cpp.
 *
 * Uses a stack-based depth-limited search from zone perimeters along conductive
 * (road) tiles to find connectivity to destination zone types.
 */
class TrafficSimulation {
public:
    TrafficSimulation();

    void init(CitySimulation* sim);

    /**
     * Find a traffic route from zone at (x,y) to a destination zone type.
     * @return 1 = connected, 0 = tried but failed, -1 = no road access
     */
    int makeTraffic(int x, int y, ZoneType destZone);

    struct Pos { int x, y; };

    /// Road tiles visited during the last successful makeTraffic call.
    /// Empty if the last call returned NoRoad or NoDestination.
    const std::vector<Pos>& getLastPath() const { return pathTiles_; }

private:
    bool findPerimeterRoad(int zoneX, int zoneY, int& roadX, int& roadY) const;
    bool tryDrive(int startX, int startY, ZoneType destZone);

    /**
     * Try to move from current position in a direction that isn't "lastDir".
     * @return direction index 0-3 (N/E/S/W) or -1 if dead end
     */
    int tryGo(int x, int y, int lastDir) const;
    bool isRoad(int x, int y) const;
    bool driveDone(int x, int y, ZoneType destZone) const;
    void addToTrafficDensityMap();

    CitySimulation* sim_ = nullptr;

    std::vector<Pos> driveStack_;
    std::vector<Pos> pathTiles_;  ///< road tiles visited on last successful drive
    static constexpr int DX[4] = { 0, 1, 0, -1 };
    static constexpr int DY[4] = { -1, 0, 1, 0 };
};

} // namespace DuneCity

#endif // DUNECITY_TRAFFICSIMULATION_H
