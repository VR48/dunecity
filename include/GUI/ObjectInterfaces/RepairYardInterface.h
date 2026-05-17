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

#ifndef REPAIRYARDINTERFACE_H
#define REPAIRYARDINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <GUI/ProgressBar.h>
#include <GUI/VBox.h>

#include <units/UnitBase.h>

#include <structures/RepairYard.h>

class RepairYardInterface : public DefaultStructureInterface {
public:
    static RepairYardInterface* create(int objectID) {
        RepairYardInterface* tmp = new RepairYardInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit RepairYardInterface(int objectID) : DefaultStructureInterface(objectID) {
        // Left half: city-sim stats column (only relevant in city mode but
        // harmless otherwise — labels just show em-dashes).
        Uint32 color = SDL2RGB(palette[houseToPaletteIndex[pLocalHouse->getHouseID()]+3]);
        mainHBox.addWidget(&textVBox);
        cityStats_.attachTo(textVBox, color);
        textVBox.addWidget(Spacer::create(), 0.99);

        // Right half: the repair-unit progress icon. A spacer separates it
        // from the text so the icon never sits underneath the labels.
        mainHBox.addWidget(Spacer::create());
        mainHBox.addWidget(&repairUnitProgressBar);
        mainHBox.addWidget(Spacer::create());
    }

    /**
        This method updates the object interface.
        If the object doesn't exists anymore then update returns false.
        \return true = everything ok, false = the object container should be removed
    */
    bool update() override
    {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(pObject == nullptr) {
            return false;
        }

        RepairYard* pRepairYard = dynamic_cast<RepairYard*>(pObject);
        if(pRepairYard != nullptr) {
            UnitBase* pUnit = pRepairYard->getRepairUnit();

            if(pUnit != nullptr) {
                repairUnitProgressBar.setVisible(true);
                repairUnitProgressBar.setTexture(resolveItemPicture(pUnit->getItemID()));
                repairUnitProgressBar.setProgress( ((pUnit->getHealth()*100)/pUnit->getMaxHealth()).toDouble());
            } else {
                repairUnitProgressBar.setVisible(false);
            }
        }

        cityStats_.update(dynamic_cast<StructureBase*>(pObject));

        return DefaultStructureInterface::update();
    }

private:
    PictureProgressBar  repairUnitProgressBar;
    VBox                textVBox;
    CityStatsBox        cityStats_;
};

#endif // REPAIRYARDINTERFACE_H
