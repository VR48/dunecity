/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef SPECIALVEHICLE_H
#define SPECIALVEHICLE_H

#include <data.h>

#include <vector>

inline std::vector<int> getSpecialVehiclePoolForHouse(int house, bool tornieActive) {
    if(tornieActive) {
        switch(house) {
            case HOUSE_HARKONNEN:  return { Unit_Devastator, Unit_FlameTank };
            case HOUSE_ATREIDES:   return { Unit_SonicTank, Unit_EliteLauncher };
            case HOUSE_ORDOS:      return { Unit_Deviator, Unit_EliteSiegeTank };
            case HOUSE_FREMEN:     return { Unit_EliteSiegeTank, Unit_FlameTank };
            case HOUSE_SARDAUKAR:  return { Unit_Devastator, Unit_SonicTank };
            case HOUSE_MERCENARY:  return { Unit_Devastator, Unit_Deviator };
            case HOUSE_NEUTRAL:    return { Unit_EliteLauncher, Unit_EliteSiegeTank };
            case HOUSE_REBELS:     return { Unit_SonicTank, Unit_FlameTank };
            default:               return {};
        }
    }

    switch(house) {
        case HOUSE_HARKONNEN:  return { Unit_Devastator };
        case HOUSE_ATREIDES:   return { Unit_SonicTank };
        case HOUSE_ORDOS:      return { Unit_Deviator };
        case HOUSE_FREMEN:
        case HOUSE_SARDAUKAR:
        case HOUSE_MERCENARY:
        case HOUSE_NEUTRAL:
        case HOUSE_REBELS:     return { Unit_SonicTank, Unit_Devastator };
        default:               return {};
    }
}

#endif // SPECIALVEHICLE_H
