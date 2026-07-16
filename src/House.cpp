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

#include <House.h>

#include <globals.h>
#include <sand.h>

#include <FileClasses/TextManager.h>

#include <players/PlayerFactory.h>
#include <players/AIPlayer.h>
#include <players/QuantBot.h>
#include <players/HumanPlayer.h>


#include <Game.h>
#include <GameInterface.h>
#include <Map.h>
#include <SoundPlayer.h>

#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/ZoneStructure.h>
#include <structures/Refinery.h>
#include <structures/HarvesterDropoff.h>
#include <structures/ConstructionYard.h>
#include <units/Carryall.h>

#include <limits>
#include <vector>
#include <units/Harvester.h>

#include <misc/exceptions.h>
#include <misc/format.h>

#include <algorithm>

House::House(int newHouse, int newCredits, int maxUnits, int maxHarvesters, Uint8 teamID, int quota) : choam(this) {
    House::init();

    houseID = ((newHouse >= 0) && (newHouse < NUM_HOUSES)) ? newHouse :  0;
    this->teamID = teamID;

    storedCredits = 0;
    startingCredits = newCredits;
    oldCredits = lround(storedCredits+startingCredits);

    this->maxUnits = maxUnits;
    this->maxHarvesters = maxHarvesters;
    this->quota = quota;

    bHadContactWithEnemy = false;
    bHadDirectContactWithEnemy = false;
    isAIActive = false;
    doneFullScaleAttack = false;

    unitBuiltValue = 0;
    structureBuiltValue = 0;
    militaryValue = 0;
    killValue = 0;
    lossValue = 0;
    numBuiltUnits = 0;
    numBuiltStructures = 0;
    destroyedValue = 0;
    numDestroyedUnits = 0;
    numDestroyedStructures = 0;
    harvestedSpice = 0;
    producedPower = 0;
    powerUsageTimer = 0;
}




House::House(InputStream& stream) : choam(this) {
    House::init();

    houseID = stream.readUint8();
    teamID = stream.readUint8();

    storedCredits = stream.readFixPoint();
    startingCredits = stream.readFixPoint();
    oldCredits = lround(storedCredits+startingCredits);
    maxUnits = stream.readSint32();
    maxHarvesters = stream.readSint32();
    quota = stream.readSint32();

    // Backward compatibility: Old saves (< 9803) only have 2 bools, new saves have 4
    // NOTE: Requires currentGame to be set (it is during load)
    if (currentGame && currentGame->getLoadedSavegameVersion() < 9803) {
        // Old save: only read original 2 flags, default new AI flags to false
        stream.readBools(&bHadContactWithEnemy, &bHadDirectContactWithEnemy);
        isAIActive = false;
        doneFullScaleAttack = false;
    } else {
        // New save: read all 4 flags
        stream.readBools(&bHadContactWithEnemy, &bHadDirectContactWithEnemy, &isAIActive, &doneFullScaleAttack);
    }

    unitBuiltValue = stream.readUint32();
    structureBuiltValue = stream.readUint32();
    militaryValue = stream.readUint32();
    killValue = stream.readUint32();
    lossValue = stream.readUint32();
    numBuiltUnits = stream.readUint32();
    numBuiltStructures = stream.readUint32();
    destroyedValue = stream.readUint32();
    numDestroyedUnits = stream.readUint32();
    numDestroyedStructures = stream.readUint32();
    harvestedSpice = stream.readFixPoint();
    producedPower = stream.readSint32();
    powerUsageTimer = stream.readSint32();

    choam.load(stream);

    Uint32 numAITeams = stream.readUint32();
    for(Uint32 i = 0; i < numAITeams; i++) {
        aiteams.emplace_back(stream);
    }

    Uint32 numPlayers = stream.readUint32();
    for(Uint32 i = 0; i < numPlayers; i++) {
        std::string playerclass = stream.readString();
        const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByPlayerClass(playerclass);
        if(pPlayerData == nullptr) {
            SDL_Log("Warning: Cannot load player '%s'", playerclass.c_str());
        } else {
            addPlayer(pPlayerData->load(stream,this));
        }
    }
}




void House::init() {
    ai = true;

    numUnits = 0;
    numStructures = 0;
    for(int i=0;i<Num_ItemID;i++) {
        numItem[i] = 0;
        numItemBuilt[i] = 0;
        numItemKills[i] = 0;
        numItemLosses[i] = 0;
        numItemDamageInflicted[i] = 0;
    }

    capacity = 0;
    powerRequirement = 0;

    numVisibleEnemyUnits = 0;
    numVisibleFriendlyUnits = 0;
}




House::~House() = default;




void House::save(OutputStream& stream) const {
    stream.writeUint8(houseID);
    stream.writeUint8(teamID);

    stream.writeFixPoint(storedCredits);
    stream.writeFixPoint(startingCredits);
    stream.writeSint32(maxUnits);
    stream.writeSint32(maxHarvesters);
    stream.writeSint32(quota);

    stream.writeBools(bHadContactWithEnemy, bHadDirectContactWithEnemy, isAIActive, doneFullScaleAttack);

    stream.writeUint32(unitBuiltValue);
    stream.writeUint32(structureBuiltValue);
    stream.writeUint32(militaryValue);
    stream.writeUint32(killValue);
    stream.writeUint32(lossValue);
    stream.writeUint32(numBuiltUnits);
    stream.writeUint32(numBuiltStructures);
    stream.writeUint32(destroyedValue);
    stream.writeUint32(numDestroyedUnits);
    stream.writeUint32(numDestroyedStructures);
    stream.writeFixPoint(harvestedSpice);
    stream.writeSint32(producedPower);
    stream.writeSint32(powerUsageTimer);

    choam.save(stream);

    stream.writeUint32(aiteams.size());
    for(const auto& aiteam : aiteams) {
        aiteam.save(stream);
    }

    stream.writeUint32(players.size());
    for(const auto& pPlayer : players) {
        stream.writeString(pPlayer->getPlayerclass());
        pPlayer->save(stream);
    }
}




void House::addPlayer(std::unique_ptr<Player> newPlayer) {
    Player* pNewPlayer = newPlayer.get();

    if(dynamic_cast<HumanPlayer*>(pNewPlayer) != nullptr && players.empty()) {
        ai = false;
    } else {
        ai = true;
    }

    players.push_back(std::move(newPlayer));

    Uint8 newPlayerID = static_cast<Uint8>((houseID << 4) | players.size());
    pNewPlayer->playerID = newPlayerID;

    currentGame->registerPlayer(pNewPlayer);
}


void House::setProducedPower(int newPower) {
    producedPower = newPower;
}


void House::addCredits(FixPoint newCredits, bool wasRefined) {
    if(newCredits > 0) {
        if(wasRefined == true) {
            harvestedSpice += newCredits;
        }

        storedCredits += newCredits;
        if(this == pLocalHouse) {
            if(((currentGame->winFlags & WINLOSEFLAGS_QUOTA) != 0) && (quota != 0)) {
                if(storedCredits >= quota) {
                    win();
                }
            }
        }
    }
}




void House::returnCredits(FixPoint newCredits) {
    if(newCredits > 0) {
        FixPoint leftCapacity = capacity - storedCredits;
        if(newCredits <= leftCapacity) {
            addCredits(newCredits, false);
        } else {
            addCredits(leftCapacity, false);
            startingCredits += (newCredits - leftCapacity);
        }
    }
}




FixPoint House::takeCredits(FixPoint amount) {
    FixPoint taken = 0;

    if(getCredits() >= 1) {
        if(storedCredits > amount) {
            taken = amount;
            storedCredits -= amount;
        } else {
            taken = storedCredits;
            storedCredits = 0;

            if(startingCredits > (amount - taken)) {
                startingCredits -= (amount - taken);
                taken = amount;
            } else {
                taken += startingCredits;
                startingCredits = 0;
            }
        }
    }

    return taken;   //the amount that was actually withdrawn
}




void House::printStat() const {
    SDL_Log("House %s: (Number of Units: %d, Number of Structures: %d)",getHouseNameByNumber( (HOUSETYPE) getHouseID()).c_str(),numUnits,numStructures);
    SDL_Log("Barracks: %d\t\tWORs: %d", numItem[Structure_Barracks],numItem[Structure_WOR]);
    SDL_Log("Light Factories: %d\tHeavy Factories: %d",numItem[Structure_LightFactory],numItem[Structure_HeavyFactory]);
    SDL_Log("IXs: %d\t\t\tPalaces: %d",numItem[Structure_IX],numItem[Structure_Palace]);
    SDL_Log("Repair Yards: %d\t\tHigh-Tech Factories: %d",numItem[Structure_RepairYard],numItem[Structure_HighTechFactory]);
    SDL_Log("Refineries: %d\t\tStarports: %d",numItem[Structure_Refinery],numItem[Structure_StarPort]);
    SDL_Log("Walls: %d\t\tRocket Turrets: %d",numItem[Structure_Wall],numItem[Structure_RocketTurret]);
    SDL_Log("Gun Turrets: %d\t\tConstruction Yards: %d",numItem[Structure_GunTurret],numItem[Structure_ConstructionYard]);
    SDL_Log("Windtraps: %d\t\tRadars: %d",numItem[Structure_WindTrap],numItem[Structure_Radar]);
    SDL_Log("Silos: %d",numItem[Structure_Silo]);
    SDL_Log("Carryalls: %d\t\tFrigates: %d",numItem[Unit_Carryall],numItem[Unit_Frigate]);
    SDL_Log("Devastators: %d\t\tDeviators: %d",numItem[Unit_Devastator],numItem[Unit_Deviator]);
    SDL_Log("Soldiers: %d\t\tTrooper: %d",numItem[Unit_Soldier],numItem[Unit_Trooper]);
    SDL_Log("Saboteur: %d\t\tSandworms: %d",numItem[Unit_Saboteur],numItem[Unit_Sandworm]);
    SDL_Log("Quads: %d\t\tTrikes: %d",numItem[Unit_Quad],numItem[Unit_Trike]);
    SDL_Log("Raiders: %d\t\tTanks: %d",numItem[Unit_RaiderTrike],numItem[Unit_Tank]);
    SDL_Log("Siege Tanks : %d\t\tSonic Tanks: %d",numItem[Unit_SiegeTank],numItem[Unit_SonicTank]);
    SDL_Log("Harvesters: %d\t\tMCVs: %d",numItem[Unit_Harvester],numItem[Unit_MCV]);
    SDL_Log("Ornithopters: %d\t\tRocket Launchers: %d",numItem[Unit_Ornithopter],numItem[Unit_Launcher]);
}




void House::updateBuildLists() {
    for(StructureBase* pStructure : structureList) {
        if(pStructure->isABuilder() && (pStructure->getOwner() == this)) {
            static_cast<BuilderBase*>(pStructure)->updateBuildList();
        }
    }
}




void House::update() {
    numVisibleEnemyUnits = 0;
    numVisibleFriendlyUnits = 0;

    if (oldCredits != getCredits()) {
        if((this == pLocalHouse) && (getCredits() > 0) && settings.audio.playCreditsSFX) {
            soundPlayer->playSound(Sound_CreditsTick);
        }
        oldCredits = getCredits();
    }

    if(storedCredits > capacity) {
        --storedCredits;
        if(storedCredits < 0) {
         storedCredits = 0;
        }

        if(this == pLocalHouse) {
            currentGame->addToNewsTicker(_("@DUNE.ENG|145#As insufficient spice storage is available, spice is lost."));
        }
    }

    powerUsageTimer--;
    if(powerUsageTimer <= 0) {
        powerUsageTimer = MILLI2CYCLES(15*1000);
        takeCredits(FixPoint(getPowerRequirement()) / 32);
    }

    choam.update();

    for(auto& pPlayer : players) {
        pPlayer->update();
    }
}




void House::incrementUnits(int itemID) {
    numUnits++;
    numItem[itemID]++;

    if(itemID != Unit_Saboteur
       && itemID != Unit_Frigate
       && itemID != Unit_Carryall
       && itemID != Unit_MCV
       && itemID != Unit_Harvester
       && itemID != Unit_RebelHarvester
       && itemID != Unit_Sandworm
       && !isAmbientUnit(itemID)) {

            militaryValue += currentGame->objectData.data[itemID][houseID].price;
    }

}



void House::decrementUnits(int itemID) {
    numUnits--;
    numItemLosses[itemID]++;

    if(itemID == Unit_Harvester) {
        decrementHarvesters();
    } else {
        numItem[itemID]--;
    }

    for(auto& pPlayer : players) {
        pPlayer->onDecrementUnits(itemID);
    }

    if(itemID != Unit_Saboteur
       && itemID != Unit_Frigate
       && itemID != Unit_Carryall
       && itemID != Unit_MCV
       && itemID != Unit_Harvester
       && itemID != Unit_RebelHarvester
       && itemID != Unit_Sandworm
       && !isAmbientUnit(itemID)) {

            lossValue += currentGame->objectData.data[itemID][houseID].price;
    }

    if (!isAlive())
        lose();


}




void House::incrementStructures(int itemID) {
    numStructures++;
    numItem[itemID]++;

    // change power requirements
    int currentItemPower = currentGame->objectData.data[itemID][houseID].power;
    if(currentItemPower >= 0) {
        powerRequirement += currentItemPower;
    }

    // change spice capacity
    capacity += currentGame->objectData.data[itemID][houseID].capacity;

    if(currentGame->gameState != GameState::Loading) {
        // do not check selection lists if we are loading
        updateBuildLists();
    }
}




void House::decrementStructures(int itemID, const Coord& location) {
    numStructures--;
    numItem[itemID]--;
    numItemLosses[itemID]++;

    // change power requirements
    int currentItemPower = currentGame->objectData.data[itemID][houseID].power;
    if(currentItemPower >= 0) {
        powerRequirement -= currentItemPower;
    }

    // change spice capacity
    capacity -= currentGame->objectData.data[itemID][houseID].capacity;

    if(currentGame->gameState != GameState::Loading) {
        // do not check selection lists if we are loading
        updateBuildLists();
    }

    if (!isAlive())
        lose();

    for(auto& pPlayer : players) {
        pPlayer->onDecrementStructures(itemID, location);
    }
}




void House::noteDamageLocation(ObjectBase* pObject, int damage, Uint32 damagerID) {
    for(auto& pPlayer : players) {
        pPlayer->onDamage(pObject, damage, damagerID);
    }
}


/**
    This method informs this house that a new unit or structure was built
    \param pObject   the object that was built
*/
void House::informWasBuilt(ObjectBase* pObject) {
    int itemID = pObject->getItemID();
    if(pObject->isAStructure()) {
        structureBuiltValue += currentGame->objectData.data[itemID][houseID].price;
        numBuiltStructures++;
    } else {
        unitBuiltValue += currentGame->objectData.data[itemID][houseID].price;
        numBuiltUnits++;
    }

    numItemBuilt[itemID]++;

    for(auto& pPlayer : players) {
        pPlayer->onObjectWasBuilt(pObject);
    }
}



/**
    This method informs this house that one of its units has killed an enemy unit or structure
    \param itemID   the ID of the enemy unit or structure
*/
void House::informHasKilled(Uint32 itemID) {
    destroyedValue += std::max(currentGame->objectData.data[itemID][houseID].price/100, 1);
    if(isStructure(itemID)) {
        numDestroyedStructures++;
    } else {
        numDestroyedUnits++;

        if(itemID != Unit_Saboteur
           && itemID != Unit_Frigate
           && itemID != Unit_Carryall
           && itemID != Unit_MCV
           && itemID != Unit_Harvester
           && itemID != Unit_Sandworm
           && !isAmbientUnit(itemID)) {

                killValue += currentGame->objectData.data[itemID][houseID].price;

        }

    }

    numItemKills[itemID]++;


    for(auto& pPlayer : players) {
        pPlayer->onIncrementUnitKills(itemID);
    }
}

/**
 This method informs this house that one of its units has damaged an enemy unit or structure
 \param itemID   the ID of the enemy unit or structure
 */

void House::informHasDamaged(Uint32 itemID, Uint32 damage) {
    numItemDamageInflicted[itemID] += damage;
}


void House::win() {
    if(getTeamID() == pLocalHouse->getTeamID()) {
        currentGame->setGameWon();
    } else {
        currentGame->setGameLost();
    }
}




void House::lose(bool bSilent) {
    if(!bSilent) {
        try {
            currentGame->addToNewsTicker(fmt::sprintf(_("House '%s' has been defeated."), getHouseNameByNumber( (HOUSETYPE) getHouseID())));
        } catch (std::exception& e) {
            SDL_Log("House::lose(): %s", e.what());
        }
    }

    if((getTeamID() == pLocalHouse->getTeamID()) && ((currentGame->winFlags & WINLOSEFLAGS_HUMAN_HAS_BUILDINGS) != 0)) {

        bool finished = true;

        for(int i=0; i < NUM_HOUSES; i++) {
            House* pHouse = currentGame->getHouse(i);
            if(pHouse != nullptr && pHouse->isAlive() && pHouse->getTeamID() == pLocalHouse->getTeamID()) {
                finished = false;
                break;
            }
        }

        if(finished) {
            // pLocalHouse is destroyed and this is a game finish condition
            if((currentGame->loseFlags & WINLOSEFLAGS_HUMAN_HAS_BUILDINGS) != 0) {
                // house has won
                currentGame->setGameWon();
            } else {
                // house has lost
                currentGame->setGameLost();
            }
        }

    } else if((currentGame->winFlags & WINLOSEFLAGS_AI_NO_BUILDINGS) != 0) {
        //if the only players left are on the thisPlayers team, pLocalHouse has won
        bool finished = true;

        for(int i=0; i < NUM_HOUSES; i++) {
            House* pHouse = currentGame->getHouse(i);
            if(pHouse != nullptr && pHouse->isAlive() && pHouse->getTeamID() != 0 && pHouse->getTeamID() != pLocalHouse->getTeamID()) {
                finished = false;
                break;
            }
        }

        if(finished) {
            // all AI players are destroyed and this is a game finish condition
            if((currentGame->loseFlags & WINLOSEFLAGS_AI_NO_BUILDINGS) != 0) {
                // house has won
                currentGame->setGameWon();
            } else {
                // house has lost
                currentGame->setGameLost();
            }
        }
    }
}




void House::freeHarvester(int xPos, int yPos) {
    // Don't spawn free harvester if at harvester limit
    if(isHarvesterLimitReached()) {
        return;
    }

    if(currentGameMap->tileExists(xPos, yPos)
        && currentGameMap->getTile(xPos, yPos)->hasAGroundObject()
        && getHarvesterDropoff(currentGameMap->getTile(xPos, yPos)->getGroundObject()) != nullptr)
    {
        StructureBase* refinery = getHarvesterDropoff(currentGameMap->getTile(xPos, yPos)->getGroundObject());
        Coord closestPos = currentGameMap->findClosestEdgePoint(
            refinery->getLocation() + Coord(refinery->getStructureSizeX() - 1, 0), Coord(1,1));

        Carryall* carryall = static_cast<Carryall*>(createUnit(Unit_Carryall));
        Harvester* harvester = static_cast<Harvester*>(createUnit(Unit_Harvester));
        harvester->setAmountOfSpice(5);
        carryall->setOwned(false);
        carryall->giveCargo(harvester);
        carryall->deploy(closestPos);
        carryall->setDropOfferer(true);

        if (closestPos.x == 0)
            carryall->setAngle(RIGHT);
        else if (closestPos.x == currentGameMap->getSizeX()-1)
            carryall->setAngle(LEFT);
        else if (closestPos.y == 0)
            carryall->setAngle(DOWN);
        else if (closestPos.y == currentGameMap->getSizeY()-1)
            carryall->setAngle(UP);

        harvester->setTarget(refinery);
        harvester->setActive(false);
        carryall->setTarget(refinery);
    }
}




StructureBase* House::placeStructure(Uint32 builderID, int itemID, int xPos, int yPos, bool byScenario, bool bForcePlacing) {
    if(!currentGameMap->tileExists(xPos,yPos)) {
        return nullptr;
    }

    BuilderBase* pBuilder = (builderID == NONE_ID) ? nullptr : dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderID));

    if(currentGame->getGameInitSettings().getGameOptions().onlyOnePalace && pBuilder != nullptr && itemID == Structure_Palace && getNumItems(Structure_Palace) > 0) {
        if(this == pLocalHouse && pBuilder->isSelected()) {
            currentGame->setCursorMode(Game::CursorMode_Normal);
        }
        return nullptr;
    }

    switch (itemID) {
        case (Structure_Slab1): {
            // Slabs are no normal buildings
            currentGameMap->getTile(xPos, yPos)->setType(Terrain_Slab);
            currentGameMap->getTile(xPos, yPos)->setRoad(false);
            currentGameMap->getTile(xPos, yPos)->setOwner(getHouseID());
            currentGameMap->viewMap(getHouseID(), xPos, yPos, currentGame->objectData.data[Structure_Slab1][houseID].viewrange);
    //      currentGameMap->getTile(xPos, yPos)->clearTerrain();

            if(pBuilder != nullptr) {
                pBuilder->unSetWaitingToPlace();

                if(this == pLocalHouse) {
                    if(pBuilder->isSelected()) {
                        currentGame->setCursorMode(Game::CursorMode_Normal);
                    }

                    pLocalPlayer->onPlaceStructure(nullptr);
                }
            }

            return nullptr;

        } break;

        case (Structure_Road): {
            // Road is a tile-state structure (like Slab1) — not selectable,
            // no StructureBase object, just a flag on the underlying rock.
            // Power flows globally in DuneCity, so roads don't conduct
            // electricity; they only matter as the city's transport substrate
            // and as a visual road overlay.
            Tile* pTile = currentGameMap->getTile(xPos, yPos);
            pTile->setRoad(true);
            pTile->setDestroyedStructureTile(DestroyedStructure_None);
            pTile->setOwner(getHouseID());
            currentGameMap->viewMap(getHouseID(), xPos, yPos, currentGame->objectData.data[Structure_Road][houseID].viewrange);

            if(pBuilder != nullptr) {
                pBuilder->unSetWaitingToPlace();

                if(this == pLocalHouse) {
                    if(pBuilder->isSelected()) {
                        currentGame->setCursorMode(Game::CursorMode_Normal);
                    }

                    pLocalPlayer->onPlaceStructure(nullptr);
                }
            }

            return nullptr;

        } break;

        case (Structure_Slab4): {
            // Slabs are no normal buildings
            currentGameMap->for_each(xPos, yPos, xPos + 2, yPos + 2,
                [=](Tile& t) {
                    if (t.hasAGroundObject() || !t.isRock() || t.isMountain())
                        return;

                    t.setType(Terrain_Slab);
                    t.setRoad(false);
                    t.setOwner(houseID);
                    currentGameMap->viewMap(getHouseID(), t.getLocation().x, t.getLocation().y, currentGame->objectData.data[Structure_Slab4][houseID].viewrange);
                    //pTile->clearTerrain();
                });

            if(pBuilder != nullptr) {
                pBuilder->unSetWaitingToPlace();

                if(this == pLocalHouse) {
                    if(pBuilder->isSelected()) {
                        currentGame->setCursorMode(Game::CursorMode_Normal);
                    }

                    pLocalPlayer->onPlaceStructure(nullptr);
                }
            }

            return nullptr;

        } break;

        default: {
            ObjectBase* newObject = ObjectBase::createObject(itemID,this,byScenario);
            StructureBase* newStructure = dynamic_cast<StructureBase*>(newObject);
            if(newStructure == nullptr) {
                delete newObject;
                THROW(std::runtime_error, "Cannot create structure with itemID %d!", itemID);
            }

            if(bForcePlacing == false) {
                // check if there is already something on this tile
                for(int i=0;i<newStructure->getStructureSizeX();i++) {
                    for(int j=0;j<newStructure->getStructureSizeY();j++) {
                        if((currentGameMap->tileExists(xPos+i, yPos+j) == false) || (currentGameMap->getTile(xPos+i, yPos+j)->hasAGroundObject() == true)) {
                            delete newObject;
                            return nullptr;
                        }
                    }
                }
            }

            for(int i=0;i<newStructure->getStructureSizeX();i++) {
                for(int j=0;j<newStructure->getStructureSizeY();j++) {
                    if(currentGameMap->tileExists(xPos+i, yPos+j)) {
                        currentGameMap->getTile(xPos+i, yPos+j)->clearTerrain();
                    }
                }
            }

            newStructure->setLocation(xPos, yPos);

            // DuneCity: when city-sim mode is active, auto-pave a 1-tile
            // road ring around any freshly-placed building (zones AND
            // regular Dune structures like Refineries/Barracks/etc.) so
            // every plot is road-accessible by default — the SimCity
            // "zone with road frontage" pattern, generalised. Only empty
            // rock tiles get the flag; sand, mountain, and tiles with
            // existing ground objects are skipped. The structure's own
            // footprint tiles get any prior road flag cleared so the
            // auto-tiled road overlay doesn't render under the building
            // art. Players can still place buildings on top of these
            // auto-roads later (road is a tile flag, not a StructureBase).
            if (currentGame && currentGame->isCitySimEnabled() && itemID != Structure_Wall) {
                const int sx = newStructure->getStructureSizeX();
                const int sy = newStructure->getStructureSizeY();
                for (int dy = 0; dy < sy; ++dy) {
                    for (int dx = 0; dx < sx; ++dx) {
                        if (Tile* t = currentGameMap->getTile(xPos + dx, yPos + dy)) {
                            t->setRoad(false);
                        }
                    }
                }
                for (int dy = -1; dy <= sy; ++dy) {
                    for (int dx = -1; dx <= sx; ++dx) {
                        const bool onFootprint = (dx >= 0 && dx < sx && dy >= 0 && dy < sy);
                        if (onFootprint) continue;
                        const int tx = xPos + dx;
                        const int ty = yPos + dy;
                        if (!currentGameMap->tileExists(tx, ty)) continue;
                        Tile* t = currentGameMap->getTile(tx, ty);
                        if (t->hasAGroundObject()) continue;
                        if (t->getType() != Terrain_Rock) continue;
                        t->setRoad(true);
                    }
                }
            }

            if ((builderID != NONE_ID) && (itemID != Structure_Wall)) {
                newStructure->setJustPlaced();
            }

            // at the beginning of the game the first refinery gets a harvester for free (brought by a carryall)
            if((itemID == Structure_Refinery) && ( ((currentGame->gameState == GameState::Start) && (numItem[Unit_Harvester] <= 0)) || (builderID != NONE_ID)) ) {
                freeHarvester(xPos, yPos);
            }

            // if this structure was built by a construction yard this construction yard must be informed
            if(pBuilder != nullptr) {
                pBuilder->unSetWaitingToPlace();

                if(itemID == Structure_Palace) {
                    // cancel all other palaces
                    for(StructureBase* pStructure : structureList) {
                        if(pStructure->getOwner() == this && pStructure->getItemID() == Structure_ConstructionYard) {
                            ConstructionYard* pConstructionYard = static_cast<ConstructionYard*>(pStructure);
                            if(pBuilder != pConstructionYard) {
                                pConstructionYard->doCancelItem(Structure_Palace, false);
                            }
                        }
                    }
                }

                if (this == pLocalHouse) {
                    if(pBuilder->isSelected()) {
                        currentGame->setCursorMode(Game::CursorMode_Normal);
                    }
                    pLocalPlayer->onPlaceStructure(newStructure);

                    // DuneCity: Check for first zone milestone
                    if (newStructure != nullptr && currentGame != nullptr) {
                        auto* citySim = currentGame->getCitySimulation();
                        if (citySim != nullptr && !citySim->wasFirstZoneMilestoneTriggered()) {
                            // Check if this is a zone structure (R/C/I)
                            ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(newStructure);
                            if (pZone != nullptr) {
                                citySim->onFirstZoneBuilt();
                            }
                        }
                    }
                }

                // only if we were constructed by construction yard
                // => inform house of the building
                pBuilder->getOwner()->informWasBuilt(newObject);
            }

            if(newStructure->isABuilder()) {
                static_cast<BuilderBase*>(newStructure)->updateBuildList();
            }

            return newStructure;

        } break;
    }

    return nullptr;
}




UnitBase* House::createUnit(int itemID, bool byScenario) {
    ObjectBase* newObject = ObjectBase::createObject(itemID,this,byScenario);
    UnitBase* newUnit = dynamic_cast<UnitBase*>(newObject);

    if(newUnit == nullptr) {
        delete newObject;
        THROW(std::runtime_error, "Cannot create unit with itemID %d!", itemID);
    }

    return newUnit;
}




UnitBase* House::placeUnit(int itemID, int xPos, int yPos, bool byScenario) {
    UnitBase* newUnit = nullptr;
    if(currentGameMap->tileExists(xPos, yPos) == true) {
        Tile* pTile = currentGameMap->getTile(xPos,yPos);

        if(itemID == Unit_Saboteur || itemID == Unit_Soldier || itemID == Unit_Trooper) {
            if((pTile->hasANonInfantryGroundObject() == true) || (pTile->infantryNotFull() == false)) {
                // infantry units can not placed on non-infantry units or structures (or the tile is already full of infantry units)
                return nullptr;
            }
        } else {
            if(pTile->hasAGroundObject() == true) {
                // non-infantry units can not placed on a tile where already some other unit or structure is placed on
                return nullptr;
            }
        }

        newUnit = createUnit(itemID, byScenario);
    }

    if (newUnit) {
        Coord pos = Coord(xPos, yPos);
        if (newUnit->canPass(xPos, yPos)) {
            newUnit->deploy(pos);
        } else {
            newUnit->setVisible(VIS_ALL, false);
            newUnit->destroy();
            newUnit = nullptr;
        }
    }

    return newUnit;
}


/**
    This method returns the center of the base of this house.
    \return the coordinate of the center in tile coordinates
*/
Coord House::getCenterOfMainBase() const {
    struct StructureCenter {
        const StructureBase* structure;
        Coord centerPx;
    };

    std::vector<StructureCenter> ownedStructures;
    ownedStructures.reserve(structureList.size());

    long long sumX = 0;
    long long sumY = 0;

    for(const StructureBase* pStructure : structureList) {
        if(pStructure->getOwner() != this) {
            continue;
        }

        Coord centerPx = pStructure->getCenterPoint();
        ownedStructures.push_back({pStructure, centerPx});
        sumX += centerPx.x;
        sumY += centerPx.y;
    }

    if(ownedStructures.empty()) {
        return Coord::Invalid();
    }

    const double avgX = static_cast<double>(sumX) / ownedStructures.size();
    const double avgY = static_cast<double>(sumY) / ownedStructures.size();

    const StructureCenter* best = &ownedStructures.front();
    double bestDistSq = std::numeric_limits<double>::max();

    for(const auto& entry : ownedStructures) {
        const double dx = static_cast<double>(entry.centerPx.x) - avgX;
        const double dy = static_cast<double>(entry.centerPx.y) - avgY;
        const double distSq = dx*dx + dy*dy;

        if(distSq < bestDistSq) {
            bestDistSq = distSq;
            best = &entry;
        }
    }

    const Coord structureLocation = best->structure->getLocation();
    const Coord structureSize = best->structure->getStructureSize();

    return structureLocation + Coord(structureSize.x / 2, structureSize.y / 2);
}


/**
    This method returns the position of the strongest unit
    \return the coordinate of the strongest unit
*/
Coord House::getStrongestUnitPosition() const {
    Coord strongestUnitPosition = Coord::Invalid();
    Sint32 strongestUnitCost = 0;
    for(const UnitBase* pUnit : unitList) {
        if(pUnit->getOwner() == this) {
            Sint32 currentCost = currentGame->objectData.data[pUnit->getItemID()][houseID].price;

            if(currentCost > strongestUnitCost) {
                strongestUnitPosition = pUnit->getLocation();
                strongestUnitCost = currentCost;
            }
        }
    }

    return strongestUnitPosition;
}




void House::decrementHarvesters() {
    numItem[Unit_Harvester]--;

    if(numItem[Unit_Harvester] <= 0) {
        numItem[Unit_Harvester] = 0;

        if(hasRefinery()) {
            Coord   closestPos;
            FixPoint    closestDistance = FixPt_MAX;
            StructureBase *pClosestRefinery = nullptr;

            for(StructureBase* pStructure : structureList) {
                if(pStructure->acceptsHarvesterDropoff() && (pStructure->getOwner() == this) && (pStructure->getHealth() > 0)) {
                    Coord pos = pStructure->getLocation();

                    Coord closestPoint = pStructure->getClosestPoint(pos);
                    FixPoint refineryDistance = blockDistance(pos, closestPoint);
                    if(!pClosestRefinery || (refineryDistance < closestDistance)) {
                            closestDistance = refineryDistance;
                            pClosestRefinery = pStructure;
                            closestPos = pos;
                    }
                }
            }

            if(pClosestRefinery && (currentGame->gameState == GameState::Running)) {
                freeHarvester(pClosestRefinery->getLocation());
            }
        }
    }
}
