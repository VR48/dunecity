#ifndef POLICESTATION_H
#define POLICESTATION_H

#include <structures/StructureBase.h>

/**
 * Police Station — DuneCity city-mode building.
 *
 * 2x2 footprint, single graphic, no animation. Sole source of police
 * coverage in city mode: Barracks and WOR no longer reduce crime
 * (getPoliceCoverage(Structure_Barracks) returns 0). Coverage radius
 * and annual upkeep are defined in CityEffects.h; the per-tick stamp
 * happens in runEffectsScans alongside Gun/Rocket Turret partial coverage.
 *
 * Price (500) matches SimCity Classic's TOOL_POLICESTATION cost
 * (MicropolisCore/MicropolisEngine/src/tool.cpp gCostOf[4]).
 */
class PoliceStation final : public StructureBase
{
public:
    explicit PoliceStation(House* newOwner);
    explicit PoliceStation(InputStream& stream);
    virtual ~PoliceStation();

private:
    void init();
};

#endif // POLICESTATION_H
