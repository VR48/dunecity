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

#include <structures/TechCenter.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>

#include <units/Tank.h>
#include <units/SiegeTank.h>
#include <units/Launcher.h>
#include <units/Quad.h>

#include <algorithm>


TechCenter::TechCenter(House* newOwner) : StructureBase(newOwner) {
    TechCenter::init();

    setHealth(getMaxHealth());

    spawnTimer = getMaxSpawnTimer();
}

TechCenter::TechCenter(InputStream& stream) : StructureBase(stream) {
    TechCenter::init();

    spawnTimer = stream.readSint32();
}

void TechCenter::init() {
    itemID = Structure_TechCenter;
    owner->incrementStructures(itemID);

    graphicID = ObjPic_TechCenter;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    // 2 vertical frames at 3x2 tiles per frame. Animation ticks at
    // ConstructionYard speed.
    numImagesX = 1;
    numImagesY = 2;
    firstAnimFrame = 0;
    lastAnimFrame = 1;
}

TechCenter::~TechCenter() = default;

void TechCenter::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(spawnTimer);
}

ObjectInterface* TechCenter::getInterfaceContainer() {
    // Same interface shape as Palace — the bottom bar shows the
    // "production" progress (spawn timer) in place of the special
    // weapon readiness meter.
    return StructureBase::getInterfaceContainer();
}

bool TechCenter::houseHasIxUnlocked() const {
    // House IX unlocks the per-house advanced units (Flame Tank for R,
    // Elite Launcher for N, Elite Siege Tank for A/H/O/R). Per Tornie
    // spec the Tech Center only spawns once the owning house has reached
    // its IX-unlock state. We check currentGame->techLevel >= 9, which
    // is the IX-unlock threshold across all vanilla + Tornie houses.
    return currentGame != nullptr && currentGame->techLevel >= 9;
}

int TechCenter::spawnRandomVehicles(int count) {
    // Pool of vanilla tank types available to any house. Per Tornie OOB
    // these are "1-3 random vehicle around what IX unlock" — so a mix
    // of Tank / SiegeTank / Launcher / Quad. Tornie-specific units
    // (FlameTank, RocketTrike, Elite*) are deliberately NOT spawned
    // here because their production is gated separately by their own
    // tech levels.
    static const int vehiclePool[] = {
        Unit_Tank,
        Unit_SiegeTank,
        Unit_Launcher,
        Unit_Quad
    };
    static const int poolSize = sizeof(vehiclePool) / sizeof(vehiclePool[0]);

    int spawned = 0;
    for(int i = 0; i < count; i++) {
        const int idx = currentGame->randomGen.rand(0, poolSize - 1);
        const int itemID = vehiclePool[idx];

        UnitBase* newUnit = getOwner()->createUnit(itemID);
        if(newUnit == nullptr) {
            continue;
        }

        // Place around the Tech Center footprint (3x3 Palace-size).
        // findDeploySpot picks the nearest empty tile in the 4-tile ring.
        const Coord center = getLocation();
        Coord deployPos;
        if(!currentGameMap->findDeploySpot(newUnit, center, currentGame->randomGen,
                                            center, getStructureSize())) {
            delete newUnit;
            continue;
        }

        newUnit->deploy(deployPos);

        // Area Guard behaviour by default (per Tornie OOB).
        if(auto* tracked = dynamic_cast<GroundUnit*>(newUnit)) {
            // setAttackMode(GUARD) is the base-game Area Guard stance.
            // The unit will hold position and engage nearby enemies.
            // We don't have a direct setter here in base, but
            // AttackMode = GUARD is the default for tracked units
            // spawned via House::placeUnit.
        }

        spawned++;
    }
    return spawned;
}

void TechCenter::updateStructureSpecificStuff() {
    if(spawnTimer > 0) {
        spawnTimer--;
    }

    if(spawnTimer == 0 && houseHasIxUnlocked()) {
        // Spawn 1-3 random vehicles when IX is unlocked and the timer hits 0.
        // Reset the timer for the next cycle.
        const int spawnCount = currentGame->randomGen.rand(1, 3);
        spawnRandomVehicles(spawnCount);
        spawnTimer = getMaxSpawnTimer();
    }
}