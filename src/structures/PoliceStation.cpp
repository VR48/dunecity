#include <structures/PoliceStation.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

PoliceStation::PoliceStation(House* newOwner) : StructureBase(newOwner) {
    PoliceStation::init();
    setHealth(getMaxHealth());
}

PoliceStation::PoliceStation(InputStream& stream) : StructureBase(stream) {
    PoliceStation::init();
}

void PoliceStation::init() {
    itemID = Structure_PoliceStation;
    owner->incrementStructures(itemID);

    structureSize.x = 2;
    structureSize.y = 2;
    graphicID = ObjPic_PoliceStation;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // Atlas is a single still frame replicated across 4 slots so
    // StructureBase's animation step has somewhere to land — matches the
    // NuclearPlant approach.
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

PoliceStation::~PoliceStation() = default;
