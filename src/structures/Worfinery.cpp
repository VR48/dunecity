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

#include <structures/Worfinery.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>


Worfinery::Worfinery(House* newOwner) : StructureBase(newOwner) {
    Worfinery::init();

    setHealth(getMaxHealth());
}

Worfinery::Worfinery(InputStream& stream) : StructureBase(stream) {
    Worfinery::init();
}

void Worfinery::init() {
    itemID = Structure_Worfinery;
    owner->incrementStructures(itemID);

    graphicID = ObjPic_Worfinery;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    // 2 vertical frames at 3x2 tiles per frame. Animation ticks frame
    // index 0..1 at ConstructionYard speed (the base StructureBase
    // animation uses an internal counter that matches CY's cadence).
    numImagesX = 1;
    numImagesY = 2;
    firstAnimFrame = 0;
    lastAnimFrame = 1;
}

Worfinery::~Worfinery() = default;

void Worfinery::save(OutputStream& stream) const {
    StructureBase::save(stream);
}

ObjectInterface* Worfinery::getInterfaceContainer() {
    // Reuse the Refinery interface — same layout (storage list + production
    // button) makes sense for a building that produces Troopers instead
    // of Harvesters.
    return StructureBase::getInterfaceContainer();
}

void Worfinery::updateStructureSpecificStuff() {
    // TODO(Tornie): per spec the building produces Troopers via the
    // standard base-game trooper production path. The animation should
    // run 2 vertical frames at ConstructionYard speed. Both require the
    // dedicated Worfinery sprite which Tornie will provide later; until
    // then this is a no-op placeholder so the structure stays alive and
    // tickable without crashing.
}