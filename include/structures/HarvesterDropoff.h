#ifndef HARVESTERDROPOFF_H
#define HARVESTERDROPOFF_H

#include <structures/StructureBase.h>

inline StructureBase* getHarvesterDropoff(ObjectBase* object) {
    if(object == nullptr || !object->isAStructure()) return nullptr;
    auto* structure = static_cast<StructureBase*>(object);
    return structure->acceptsHarvesterDropoff() ? structure : nullptr;
}

inline const StructureBase* getHarvesterDropoff(const ObjectBase* object) {
    if(object == nullptr || !object->isAStructure()) return nullptr;
    auto* structure = static_cast<const StructureBase*>(object);
    return structure->acceptsHarvesterDropoff() ? structure : nullptr;
}

#endif // HARVESTERDROPOFF_H
