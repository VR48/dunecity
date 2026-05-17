#ifndef DUNECITY_CITYCONSTANTS_H
#define DUNECITY_CITYCONSTANTS_H

#include <Command.h>

#include <cstdint>

namespace DuneCity {

enum class ZoneType : uint8_t {
    None        = 0,
    Residential = 1,
    Commercial  = 2,
    Industrial  = 3
};

constexpr int NUM_CITY_PHASES    = 16;
constexpr int CITY_PHASE_INTERVAL = 1;
constexpr int16_t MAX_TAX_RATE   = 20;

enum CityToolType : uint32_t {
    CityTool_Bulldoze  = 0,
    CityTool_Road      = 1,
    CityTool_PowerLine = 2
};

struct CityTilePlacementState {
    bool supportedTerrain = false;
    bool isMountain = false;
    bool hasGroundObject = false;
    bool hasCityZone = false;
    bool hasRoad = false;
};

struct CityBuildCommandDescriptor {
    int commandId = CMD_NONE;
    uint32_t parameter = 0;
};

inline CityTilePlacementState makeCityTilePlacementState(
        bool isRock, bool isMountain, bool hasGroundObject,
        bool hasCityZone, bool hasRoad) {
    return { isRock && !isMountain, isMountain, hasGroundObject, hasCityZone, hasRoad };
}

inline bool canPlaceRoad(const CityTilePlacementState& s) {
    return s.supportedTerrain && !s.isMountain && !s.hasGroundObject
           && !s.hasCityZone && !s.hasRoad;
}

inline bool applyRoadPlacement(CityTilePlacementState& state) {
    if (!canPlaceRoad(state)) {
        return false;
    }

    state.hasRoad = true;
    return true;
}

inline CityBuildCommandDescriptor getRoadPlacementCommandDescriptor() {
    return CityBuildCommandDescriptor{CMD_CITY_TOOL, static_cast<uint32_t>(CityTool_Road)};
}

inline const char* getRoadPlacementModeLabel() {
    return "Road tool";
}

} // namespace DuneCity

#endif // DUNECITY_CITYCONSTANTS_H
