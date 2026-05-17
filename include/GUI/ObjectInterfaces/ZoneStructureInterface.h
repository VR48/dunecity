/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZONESTRUCTUREINTERFACE_H
#define ZONESTRUCTUREINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>

#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <structures/ZoneStructure.h>
#include <dunecity/CityConstants.h>

#include <GUI/Label.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

#include <string>

class ZoneStructureInterface : public DefaultStructureInterface {
public:
    static ZoneStructureInterface* create(int objectID) {
        ZoneStructureInterface* tmp = new ZoneStructureInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit ZoneStructureInterface(int objectID) : DefaultStructureInterface(objectID) {
        Uint32 color = SDL2RGB(palette[houseToPaletteIndex[pLocalHouse->getHouseID()]+3]);

        mainHBox.addWidget(&textVBox);

        zoneNameLabel.setTextFontSize(12);
        zoneNameLabel.setTextColor(color);
        textVBox.addWidget(&zoneNameLabel, 0.005);

        densityLabel.setTextFontSize(12);
        densityLabel.setTextColor(color);
        textVBox.addWidget(&densityLabel, 0.005);

        poweredLabel.setTextFontSize(12);
        poweredLabel.setTextColor(color);
        textVBox.addWidget(&poweredLabel, 0.005);

        cityStats_.attachTo(textVBox, color, /*isZone=*/true);

        textVBox.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if (pObject == nullptr) {
            return false;
        }

        ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(pObject);
        if (pZone == nullptr) {
            return DefaultStructureInterface::update();
        }

        std::string name;
        switch (pZone->getZoneType()) {
            case DuneCity::ZoneType::Residential: name = _("Residential Zone"); break;
            case DuneCity::ZoneType::Commercial:  name = _("Commercial Zone");  break;
            case DuneCity::ZoneType::Industrial:  name = _("Industrial Zone");  break;
            default:                              name = _("Zone");             break;
        }
        zoneNameLabel.setText(" " + name);

        // Density lives on the underlying tile (set by the city sim / zone
        // command). Sample the structure's top-left tile.
        int density = 0;
        const Coord loc = pZone->getLocation();
        if (currentGameMap != nullptr && currentGameMap->tileExists(loc.x, loc.y)) {
            density = currentGameMap->getTile(loc.x, loc.y)->getCityZoneDensity();
        }
        densityLabel.setText(" " + _("Density") + ": " + std::to_string(density) + "/8");

        // Per-tile city power grid is still stubbed, so fall back to the
        // owning house's overall power state — same fallback the old hover
        // tooltip used.
        const bool powered = pZone->getOwner()->hasPower();
        poweredLabel.setText(std::string(" ") + (powered ? _("Powered") : _("UNPOWERED")));

        cityStats_.update(pZone);

        return DefaultStructureInterface::update();
    }

private:
    VBox    textVBox;

    Label   zoneNameLabel;
    Label   densityLabel;
    Label   poweredLabel;

    CityStatsBox cityStats_;
};

#endif // ZONESTRUCTUREINTERFACE_H
