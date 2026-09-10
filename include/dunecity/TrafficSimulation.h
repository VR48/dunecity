#ifndef DUNECITY_TRAFFICSIMULATION_H
#define DUNECITY_TRAFFICSIMULATION_H

#include <dunecity/CityConstants.h>
#include <dunecity/CityTrafficPolicy.h>

namespace DuneCity {
class CitySimulation;

// Deterministic BFS connectivity for existing 2x2 zone perimeters. Density
// sampling/decay follow Micropolis; connectivity remains our bounded BFS.
class TrafficSimulation {
public:
    TrafficSimulation();
    void init(CitySimulation* sim);
    // 1 = connected, 0 = no destination, -1 = no perimeter road.
    int makeTraffic(int x, int y, ZoneType destZone);
    using Pos = CityTraffic::Point;
    // Ordered successful route including start and destination, empty on failure.
    const std::vector<Pos>& getLastPath() const { return routeFinder_.route(); }
private:
    bool findPerimeterRoad(int zoneX, int zoneY, int& roadX, int& roadY) const;
    bool tryDrive(int startX, int startY, ZoneType destZone);
    bool isRoad(int x, int y) const;
    bool driveDone(int x, int y, ZoneType destZone) const;
    CityTraffic::RouteFinder routeFinder_;
    static constexpr int DX[4] = { 0, 1, 0, -1 };
    static constexpr int DY[4] = { -1, 0, 1, 0 };
};
} // namespace DuneCity
#endif
