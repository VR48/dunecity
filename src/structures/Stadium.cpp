#include <structures/Stadium.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

Stadium::Stadium(House* newOwner) : StructureBase(newOwner) {
    Stadium::init();
    setHealth(getMaxHealth());
}

Stadium::Stadium(InputStream& stream) : StructureBase(stream) {
    Stadium::init();
}

void Stadium::init() {
    itemID = Structure_Stadium;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;
    graphicID = ObjPic_Stadium;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

Stadium::~Stadium() = default;
