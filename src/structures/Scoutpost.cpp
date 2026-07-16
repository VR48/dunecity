/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <structures/Scoutpost.h>

#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <GUI/ObjectInterfaces/WindTrapInterface.h>
#include <globals.h>

#include <Bullet.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <Game.h>
#include <House.h>
#include <Map.h>

Scoutpost::Scoutpost(House* newOwner) : TurretBase(newOwner) {
    Scoutpost::init();

    setHealth(getMaxHealth());
}

Scoutpost::Scoutpost(InputStream& stream) : TurretBase(stream) {
    Scoutpost::init();
}

void Scoutpost::init() {
    itemID = Structure_Scoutpost;
    owner->incrementStructures(itemID);

    structureSize.x = 1;
    structureSize.y = 1;

    attackSound = Sound_RocketSmall;
    bulletType = Bullet_SmallRocket;

    graphicID = ObjPic_Scoutpost;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

Scoutpost::~Scoutpost() = default;

bool Scoutpost::canAttack(const ObjectBase* object) const {
    return object != nullptr
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || object->getItemID() == Unit_Sandworm)
        && object->isVisible(getOwner()->getTeamID());
}

ObjectInterface* Scoutpost::getInterfaceContainer() {
    if((pLocalHouse == owner) || (debug == true)) {
        return WindTrapInterface::create(objectID);
    }

    return DefaultObjectInterface::create(objectID);
}

void Scoutpost::updateStructureSpecificStuff() {
    if(justPlacedTimer <= 0 || curAnimFrame != 0) {
        curAnimFrame = 2 + ((currentGame->getGameCycleCount() / 16) % 2);
    }

    auto* citySim = currentGame->getCitySimulation();
    if(citySim) {
        citySim->registerPowerSource(location.x, location.y, getProducedPower());
    }

    if(target && target.getObjPointer() != nullptr) {
        if(!canAttack(target.getObjPointer()) || !targetInWeaponRange()) {
            setTarget(nullptr);
            if(findTargetTimer < 25) {
                findTargetTimer = 25 + (objectID % 15);
            }
        } else {
            attack();
        }
    } else if((attackMode != STOP) && (findTargetTimer == 0)) {
        setTarget(findTarget());
        findTargetTimer = 50 + (objectID % 20);
    }

    if(findTargetTimer > 0) {
        findTargetTimer--;
    }

    if(weaponTimer > 0) {
        weaponTimer--;
    }
}

void Scoutpost::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    TurretBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int Scoutpost::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[itemID][originalHouseID].power);
    FixPoint ratio = getHealth() / getMaxHealth();
    return lround(ratio * nominal);
}
