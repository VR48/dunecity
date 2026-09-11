#ifndef DUNECITY_CITY_FACTION_POLICY_H
#define DUNECITY_CITY_FACTION_POLICY_H
#include <data.h>
#include <DataTypes.h>
#include <cstdint>
namespace DuneCity {
inline bool cityHarkonnenProduct(bool city, int house, uint32_t builder, uint32_t product) {
    return city && house == HOUSE_HARKONNEN
        && ((builder == Structure_LightFactory && product == Unit_Trike)
            || (builder == Structure_HighTechFactory && product == Unit_Ornithopter));
}
}
#endif
