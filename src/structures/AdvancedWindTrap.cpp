#include <structures/AdvancedWindTrap.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

#include <GUI/ObjectInterfaces/WindTrapInterface.h>

AdvancedWindTrap::AdvancedWindTrap(House* newOwner, Uint32 newItemID) : StructureBase(newOwner) {
    AdvancedWindTrap::init(newItemID);

    setHealth(getMaxHealth());
}

AdvancedWindTrap::AdvancedWindTrap(InputStream& stream, Uint32 newItemID) : StructureBase(stream) {
    AdvancedWindTrap::init(newItemID);
}

void AdvancedWindTrap::init(Uint32 newItemID) {
    itemID = newItemID;
    owner->incrementStructures(itemID);

    switch(itemID) {
        case Structure_AdvancedWindTrapMK2:
            structureSize.x = 2;
            structureSize.y = 3;
            graphicID = ObjPic_AdvancedWindTrap2x3;
            break;

        case Structure_AdvancedWindTrapMK3:
            structureSize.x = 3;
            structureSize.y = 2;
            graphicID = ObjPic_AdvancedWindTrap3x2;
            break;

        case Structure_AdvancedWindTrap:
        default:
            itemID = Structure_AdvancedWindTrap;
            structureSize.x = 3;
            structureSize.y = 3;
    graphicID = ObjPic_AdvancedWindTrap;
            break;
    }

    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // Tornie advanced windtraps use the compact custom structure sheet:
    // build site, destroyed, active frame A, active frame B.
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

AdvancedWindTrap::~AdvancedWindTrap() = default;

ObjectInterface* AdvancedWindTrap::getInterfaceContainer() {
    if((pLocalHouse == owner) || (debug == true)) {
        return WindTrapInterface::create(objectID);
    }

    return DefaultObjectInterface::create(objectID);
}

bool AdvancedWindTrap::update() {
    bool bResult = StructureBase::update();

    if(bResult) {
        if(justPlacedTimer <= 0 || curAnimFrame != 0) {
            curAnimFrame = 2 + ((currentGame->getGameCycleCount()/8) % 2);
        }

        auto* citySim = currentGame->getCitySimulation();
        if (citySim) {
            citySim->registerPowerSource(location.x, location.y, getProducedPower());
        }
    }

    return bResult;
}

void AdvancedWindTrap::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    StructureBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int AdvancedWindTrap::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[itemID][originalHouseID].power);
    FixPoint ratio = getHealth() / getMaxHealth();
    return lround(ratio * nominal);
}
