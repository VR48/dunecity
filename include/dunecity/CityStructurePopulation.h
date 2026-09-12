#ifndef DUNECITY_CITY_STRUCTURE_POPULATION_H
#define DUNECITY_CITY_STRUCTURE_POPULATION_H
#include <dunecity/CityEffects.h>
#include <dunecity/ResidentialPopulation.h>
#include <structures/ZoneStructure.h>

namespace DuneCity {
inline int getStructurePopulation(const StructureBase* structure, int level) {
    if (const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        zone && zone->getZoneType() == ZoneType::Residential)
        return zone->getResidentialPopulation();
    return getZonePopulation(structure->getItemID(),level);
}
inline int getStructureTaxBaseEighths(const StructureBase* structure, int level) {
    return taxablePopulationEighths(structure->getItemID(), getStructurePopulation(structure, level), level);
}
inline int getStructureResidentialSupply(const StructureBase* structure, int level) {
    if (const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        zone && zone->getZoneType() == ZoneType::Residential)
        return ResidentialPopulation::supply(zone->getResidentialPopulation());
    return getResidentialSupply(structure->getItemID(),level);
}
}
#endif
