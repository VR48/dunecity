#include <structures/AdvancedWindTrap.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <ScreenBorder.h>
#include <mmath.h>

#include <GUI/ObjectInterfaces/WindTrapInterface.h>

AdvancedWindTrap::AdvancedWindTrap(House* newOwner) : StructureBase(newOwner) {
    AdvancedWindTrap::init();

    setHealth(getMaxHealth());
}

AdvancedWindTrap::AdvancedWindTrap(InputStream& stream) : StructureBase(stream) {
    AdvancedWindTrap::init();
}

void AdvancedWindTrap::init() {
    itemID = Structure_AdvancedWindTrap;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;

    graphicID = ObjPic_AdvancedWindTrap;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // The AdvancedWindTrap sprite atlas has 3 identical frames in a single row
    // (48×48 each), so the building renders as its own static sprite — no
    // visible animation and no flag overlay. Do NOT copy WindTrap's synthetic
    // animation parameters — those only apply to the 320×224 texture generated
    // by generateWindtrapAnimationFrames().
    numImagesX = 3;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame = 2;
}

AdvancedWindTrap::~AdvancedWindTrap() = default;

bool AdvancedWindTrap::update() {
    bool bResult = StructureBase::update();

    if(bResult) {
        if(justPlacedTimer <= 0 || curAnimFrame != 0) {
            curAnimFrame = (currentGame->getGameCycleCount()/8) % 3;
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

ObjectInterface* AdvancedWindTrap::getInterfaceContainer() {
    if((pLocalHouse == owner) || (debug == true)) {
        return WindTrapInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

int AdvancedWindTrap::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[Structure_AdvancedWindTrap][originalHouseID].power);
    FixPoint ratio = getHealth() / getMaxHealth();
    return lround(ratio * nominal);
}
