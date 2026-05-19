#include <structures/Airport.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

Airport::Airport(House* newOwner) : StructureBase(newOwner) {
    Airport::init();
    setHealth(getMaxHealth());
}

Airport::Airport(InputStream& stream) : StructureBase(stream) {
    Airport::init();
}

void Airport::init() {
    itemID = Structure_Airport;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;
    graphicID = ObjPic_Airport;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

Airport::~Airport() = default;
