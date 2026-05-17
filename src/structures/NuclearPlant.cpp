#include <structures/NuclearPlant.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

NuclearPlant::NuclearPlant(House* newOwner) : StructureBase(newOwner) {
    NuclearPlant::init();

    setHealth(getMaxHealth());
}

NuclearPlant::NuclearPlant(InputStream& stream) : StructureBase(stream) {
    NuclearPlant::init();
}

void NuclearPlant::init() {
    itemID = Structure_NuclearPlant;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;
    graphicID = ObjPic_NuclearPlant;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 8;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
}

NuclearPlant::~NuclearPlant() = default;

bool NuclearPlant::update() {
    return StructureBase::update();
}

void NuclearPlant::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    StructureBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int NuclearPlant::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[Structure_NuclearPlant][originalHouseID].power);
    FixPoint ratio = getHealth() / getMaxHealth();
    return lround(ratio * nominal);
}
