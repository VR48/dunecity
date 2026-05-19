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

#include <ObjectBase.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <Game.h>
#include <SpatialGrid.h>
#include <House.h>
#include <SoundPlayer.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <players/HumanPlayer.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

//structures
#include <structures/Barracks.h>
#include <structures/ConstructionYard.h>
#include <structures/GunTurret.h>
#include <structures/HeavyFactory.h>
#include <structures/HighTechFactory.h>
#include <structures/IX.h>
#include <structures/LightFactory.h>
#include <structures/Palace.h>
#include <structures/Radar.h>
#include <structures/Refinery.h>
#include <structures/RepairYard.h>
#include <structures/RocketTurret.h>
#include <structures/Silo.h>
#include <structures/StarPort.h>
#include <structures/Wall.h>
#include <structures/WindTrap.h>
#include <structures/NuclearPlant.h>
#include <structures/PoliceStation.h>
#include <structures/Stadium.h>
#include <structures/Airport.h>
#include <structures/WOR.h>
#include <structures/ZoneStructure.h>

//units
#include <units/Carryall.h>
#include <units/Devastator.h>
#include <units/Deviator.h>
#include <units/Frigate.h>
#include <units/Harvester.h>
#include <units/Launcher.h>
#include <units/MCV.h>
#include <units/Ornithopter.h>
#include <units/Quad.h>
#include <units/RaiderTrike.h>
#include <units/Saboteur.h>
#include <units/SandWorm.h>
#include <units/SiegeTank.h>
#include <units/Soldier.h>
#include <units/SonicTank.h>
#include <units/Tank.h>
#include <units/Trike.h>
#include <units/Trooper.h>
#include <units/AmbientAirplane.h>
#include <units/AmbientHelicopter.h>

#include <array>
#include <vector>

ObjectBase::ObjectBase(House* newOwner) : originalHouseID(newOwner->getHouseID()), owner(newOwner) {
    ObjectBase::init();

    objectID = NONE_ID;

    health = 0;
    badlyDamaged = false;

    location = Coord::Invalid();
    oldLocation = Coord::Invalid();
    destination = Coord::Invalid();
    realX = 0;
    realY = 0;

    drawnAngle = 0;
    angle = drawnAngle;

    active = false;
    respondable = true;
    byScenario = false;
    selected = false;
    selectedByOtherPlayer = false;

    forced = false;
    setTarget(nullptr);
    targetFriendly = false;
    attackMode = GUARD;

    setVisible(VIS_ALL, false);

    gridHandle.invalidate();
}

ObjectBase::ObjectBase(InputStream& stream) {
    objectID = NONE_ID; // has to be set after loading
    originalHouseID = stream.readUint32();
    owner = currentGame->getHouse(stream.readUint32());

    ObjectBase::init();

    health = stream.readFixPoint();
    badlyDamaged = stream.readBool();

    location.x = stream.readSint32();
    location.y = stream.readSint32();
    oldLocation.x = stream.readSint32();
    oldLocation.y = stream.readSint32();
    destination.x = stream.readSint32();
    destination.y = stream.readSint32();
    realX = stream.readFixPoint();
    realY = stream.readFixPoint();

    angle = stream.readFixPoint();
    drawnAngle = stream.readSint8();

    active = stream.readBool();
    respondable = stream.readBool();
    byScenario = stream.readBool();

    if(currentGame->getGameInitSettings().getGameType() != GameType::CustomMultiplayer) {
        selected = stream.readBool();
        selectedByOtherPlayer = stream.readBool();
    } else {
        selected = false;
        selectedByOtherPlayer = false;
    }

    forced = stream.readBool();
    target.load(stream);
    targetFriendly = stream.readBool();
    attackMode = static_cast<ATTACKMODE>(stream.readUint32());

    std::array<bool, 7> b{false, false, false, false, false, false, false};

    stream.readBools(&b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6]);

    for (decltype(visible.size()) i = 0; i < visible.size(); ++i)
        visible[i] = b[i];

    gridHandle.invalidate();
}

void ObjectBase::init() {
    itemID = ItemID_Invalid;

    aFlyingUnit = false;
    aGroundUnit = false;
    aStructure = false;
    aUnit = false;
    infantry = false;
    aBuilder = false;

    canAttackStuff = false;

    radius = TILESIZE/2;

    graphicID = -1;
    numImagesX = 0;
    numImagesY = 0;

}

ObjectBase::~ObjectBase() {
    if(gridHandle.owner != nullptr) {
        gridHandle.owner->unregister(gridHandle);
    }
}

void ObjectBase::save(OutputStream& stream) const {
    stream.writeUint32(originalHouseID);
    stream.writeUint32(owner->getHouseID());

    stream.writeFixPoint(health);
    stream.writeBool(badlyDamaged);

    stream.writeSint32(location.x);
    stream.writeSint32(location.y);
    stream.writeSint32(oldLocation.x);
    stream.writeSint32(oldLocation.y);
    stream.writeSint32(destination.x);
    stream.writeSint32(destination.y);
    stream.writeFixPoint(realX);
    stream.writeFixPoint(realY);

    stream.writeFixPoint(angle);
    stream.writeSint8(drawnAngle);

    stream.writeBool(active);
    stream.writeBool(respondable);
    stream.writeBool(byScenario);

    if(currentGame->getGameInitSettings().getGameType() != GameType::CustomMultiplayer) {
        stream.writeBool(selected);
        stream.writeBool(selectedByOtherPlayer);
    }

    stream.writeBool(forced);
    target.save(stream);
    stream.writeBool(targetFriendly);
    stream.writeUint32(attackMode);

    stream.writeBools(visible[0], visible[1], visible[2], visible[3], visible[4], visible[5], visible[6]);
}


/**
    Returns the center point of this object
    \return the center point in world coordinates
*/
Coord ObjectBase::getCenterPoint() const {
    return Coord(lround(realX), lround(realY));
}

Coord ObjectBase::getClosestCenterPoint(const Coord& objectLocation) const {
    return getCenterPoint();
}


int ObjectBase::getMaxHealth() const {
    return currentGame->objectData.data[itemID][originalHouseID].hitpoints;
}

void ObjectBase::handleDamage(int damage, Uint32 damagerID, House* damagerOwner) {
    // Immortality guard: Human-controlled houses are invulnerable in single-player modes when option is enabled
    // This applies when ANY human player controls the house (not just AI players)
    if(damage > 0) {
        GameType gameType = currentGame->getGameInitSettings().getGameType();
        if(gameType != GameType::CustomMultiplayer 
           && gameType != GameType::LoadMultiplayer
           && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer
           && getOwner() == pLocalHouse) {
            // Zero damage so human-controlled units/structures are invulnerable
            // Continue function execution so visibility/music/stats hooks still run
            damage = 0;
        }
    }
    
    if(damage >= 0) {
        FixPoint newHealth = getHealth();

        newHealth -= damage;

        if(newHealth <= 0) {
            setHealth(0);

            if(damagerOwner != nullptr) {
                damagerOwner->informHasKilled(itemID);
            }
        } else {
            setHealth(newHealth);
        }
    }

    if(getOwner() == pLocalHouse) {
        musicPlayer->changeMusic(MUSIC_ATTACK);
    }

    if (damagerOwner != nullptr && damage != 0) {
        ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
        if (pDamager != nullptr) {
            int appliedDamage = damage;
            if (damagerOwner == getOwner()) {
                appliedDamage *= -1;
            }
            damagerOwner->informHasDamaged(pDamager->getItemID(), appliedDamage);
        }
    }

    getOwner()->noteDamageLocation(this, damage, damagerID);
}

void ObjectBase::handleInterfaceEvent(SDL_Event* event) {
}

ObjectInterface* ObjectBase::getInterfaceContainer() {
    return DefaultObjectInterface::create(objectID);
}

void ObjectBase::removeFromSelectionLists() {
    currentGame->getSelectedList().erase(getObjectID());
    currentGame->selectionChanged();
    currentGame->getSelectedByOtherPlayerList().erase(getObjectID());
    selected = false;
}

void ObjectBase::setDestination(int newX, int newY) {
    if(currentGameMap->tileExists(newX, newY) || ((newX == INVALID_POS) && (newY == INVALID_POS))) {
        destination.x = newX;
        destination.y = newY;
    }
}

void ObjectBase::setHealth(FixPoint newHealth) {
    if((newHealth >= 0) && (newHealth <= getMaxHealth())) {
        health = newHealth;
        badlyDamaged = health < BADLYDAMAGEDRATIO * getMaxHealth();
    }
}

void ObjectBase::setLocation(int xPos, int yPos) {
    SpatialGrid* spatialGrid = (currentGame != nullptr) ? currentGame->getSpatialGrid() : nullptr;
    const Coord previousLocation = location;

    if((xPos == INVALID_POS) && (yPos == INVALID_POS)) {
        if(spatialGrid != nullptr && gridHandle.isValid()) {
            spatialGrid->unregister(gridHandle);
        }
        location.invalidate();
    } else if (currentGameMap->tileExists(xPos, yPos))  {
        location.x = xPos;
        location.y = yPos;
        realX = location.x*TILESIZE;
        realY = location.y*TILESIZE;

        assignToMap(location);

        if(spatialGrid != nullptr) {
            spatialGrid->move(*this, gridHandle, previousLocation, location);
        }
    }
}

void ObjectBase::setObjectID(int newObjectID) {
    if(newObjectID >= 0) {
        objectID = newObjectID;
    }
}

void ObjectBase::setVisible(int teamID, bool status) {
    if(teamID == VIS_ALL) {
        if (status)
            visible.set();
        else
            visible.reset();
    } else if ((teamID >= 0) && (teamID < NUM_TEAMS)) {
        visible[teamID] = status;
    }
}

void ObjectBase::setTarget(const ObjectBase* newTarget) {
    target.pointTo(const_cast<ObjectBase*>(newTarget));
    targetFriendly = (target && (target.getObjPointer()->getOwner()->getTeamID() == owner->getTeamID()) && (getItemID() != Unit_Sandworm) && (target.getObjPointer()->getItemID() != Unit_Sandworm));
}

void ObjectBase::unassignFromMap(const Coord& location) const {
    if(currentGameMap->tileExists(location)) {
        currentGameMap->getTile(location)->unassignObject(getObjectID());
    }
}


bool ObjectBase::canAttack(const ObjectBase* object) const {
    return canAttack()
        && (object != nullptr)
        && (object->isAStructure() || !object->isAFlyingUnit())
        && (((object->getOwner()->getTeamID() != owner->getTeamID()) && object->isVisible(getOwner()->getTeamID())) || (object->getItemID() == Unit_Sandworm));
}

bool ObjectBase::isOnScreen() const {
    const Coord position{ lround(getRealX()), lround(getRealY()) };
    const Coord size{ getWidth(graphic[currentZoomlevel]) / numImagesX, getHeight(graphic[currentZoomlevel]) / numImagesY };

    return screenborder->isInsideScreen(position,size);
}

bool ObjectBase::isVisible(int teamID) const {
    if((teamID >= 0) && (teamID < NUM_TEAMS)) {
        return visible[teamID];
    } else {
        return false;
    }
}

bool ObjectBase::isVisible() const {
    return visible.any();
}

Uint32 ObjectBase::getHealthColor() const {
    const FixPoint healthPercent = health/getMaxHealth();

    if(healthPercent >= BADLYDAMAGEDRATIO) {
        return COLOR_LIGHTGREEN;
    } else if(healthPercent >= HEAVILYDAMAGEDRATIO) {
        return COLOR_YELLOW;
    } else {
        return COLOR_RED;
    }
}

namespace {

bool isDeprioritizedTarget(const ObjectBase& candidate) {
    return candidate.getItemID() == Structure_Wall || candidate.getItemID() == Unit_Carryall;
}

bool isTileVisibleToSeeker(const ObjectBase& seeker, const Coord& tileCoord) {
    if(!currentGameMap->tileExists(tileCoord)) {
        return false;
    }

    const Tile* tile = currentGameMap->getTile(tileCoord);
    const int teamId = seeker.getOwner()->getTeamID();

    return tile->isExploredByTeam(teamId) && !tile->isFoggedByTeam(teamId);
}

const ObjectBase* findClosestTargetLegacy(const ObjectBase& seeker) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    const int maxRadiusX = std::max(seekerLocation.x, currentGameMap->getSizeX() - 1 - seekerLocation.x);
    const int maxRadiusY = std::max(seekerLocation.y, currentGameMap->getSizeY() - 1 - seekerLocation.y);
    const int maxRadius = std::max(maxRadiusX, maxRadiusY);

    for(int radius = 1; radius <= maxRadius; ++radius) {
        for(int dx = -radius; dx <= radius; ++dx) {
            for(int dy = -radius; dy <= radius; ++dy) {
                if(std::abs(dx) != radius && std::abs(dy) != radius) {
                    continue;
                }

                const Coord checkCoord(seekerLocation.x + dx, seekerLocation.y + dy);
                if(!currentGameMap->tileExists(checkCoord)) {
                    continue;
                }

                Tile* tile = currentGameMap->getTile(checkCoord);
                if(!tile->hasAnObject()) {
                    continue;
                }

                ObjectBase* candidate = tile->getObject();
                if(candidate != nullptr && seeker.canAttack(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return nullptr;
}

const ObjectBase* findTargetLegacy(const ObjectBase& seeker, int checkRange) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    ObjectBase* closestTarget = nullptr;
    auto closestDistance = FixPt_MAX;

    for(int ring = 0; ring <= checkRange; ++ring) {
        for(int dx = -ring; dx <= ring; ++dx) {
            for(int dy = -ring; dy <= ring; ++dy) {
                if(std::abs(dx) != ring && std::abs(dy) != ring) {
                    continue;
                }

                const Coord checkCoord(seekerLocation.x + dx, seekerLocation.y + dy);
                if(!currentGameMap->tileExists(checkCoord)) {
                    continue;
                }

                const FixPoint distance = blockDistance(seekerLocation, checkCoord);
                if(distance > FixPoint(checkRange)) {
                    continue;
                }

                Tile* tile = currentGameMap->getTile(checkCoord);
                if(tile->hasAnObject() == false) {
                    continue;
                }

                if(!isTileVisibleToSeeker(seeker, checkCoord)) {
                    continue;
                }

                ObjectBase* candidate = tile->getObject();
                if(candidate == nullptr) {
                    continue;
                }

                if(isDeprioritizedTarget(*candidate) && closestTarget != nullptr) {
                    continue;
                }

                if(seeker.canAttack(candidate) && distance < closestDistance) {
                    closestTarget = candidate;
                    closestDistance = distance;
                }
            }
        }

        if(closestTarget != nullptr) {
            break;
        }
    }

    return closestTarget;
}

const ObjectBase* findTargetViaGrid(const ObjectBase& seeker,
                                    const SpatialGrid& grid,
                                    int checkRange,
                                    bool huntMode) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    const Coord centerCell = grid.clampToCell(seekerLocation);
    if(!centerCell.isValid()) {
        return nullptr;
    }

    const int cellSize = grid.getCellSize();
    const int gridWidth = grid.getGridWidth();
    const int gridHeight = grid.getGridHeight();

    const int maxRadiusX = std::max(centerCell.x, gridWidth - 1 - centerCell.x);
    const int maxRadiusY = std::max(centerCell.y, gridHeight - 1 - centerCell.y);
    const int maxReachableRadius = std::max(maxRadiusX, maxRadiusY);

    int searchRadius = 0;
    if(huntMode) {
        searchRadius = maxReachableRadius;
    } else {
        searchRadius = std::min((checkRange + cellSize - 1) / cellSize, maxReachableRadius);
    }

    std::vector<SpatialGridEntry> entries;
    entries.reserve(16);

    const ObjectBase* bestTarget = nullptr;
    auto bestDistance = FixPt_MAX;

    for(int ring = 0; ring <= searchRadius; ++ring) {
        bool ringProducedCandidate = false;

        for(int dx = -ring; dx <= ring; ++dx) {
            for(int dy = -ring; dy <= ring; ++dy) {
                if(ring != 0 && std::max(std::abs(dx), std::abs(dy)) != ring) {
                    continue;
                }

                Coord cell(centerCell.x + dx, centerCell.y + dy);
                if(!grid.isCellCoordValid(cell)) {
                    continue;
                }

                entries.clear();
                if(!grid.collectEntries(cell, entries)) {
                    continue;
                }

                for(const SpatialGridEntry& entry : entries) {
                    ObjectBase* candidate = entry.object;
                    if(candidate == nullptr || candidate == &seeker) {
                        continue;
                    }

                    const SpatialGridHandle& candidateHandle = candidate->getGridHandle();
                    if(!candidateHandle.matches(entry.key.objectId, entry.key.generation)) {
                        continue;
                    }

                    if(!seeker.canAttack(candidate)) {
                        continue;
                    }

                    const Coord candidatePoint = candidate->getClosestPoint(seekerLocation);
                    if(!isTileVisibleToSeeker(seeker, candidatePoint)) {
                        continue;
                    }

                    const FixPoint distance = blockDistance(seekerLocation, candidatePoint);
                    if(!huntMode && distance > FixPoint(checkRange)) {
                        continue;
                    }

                    const bool candidateDeprioritized = isDeprioritizedTarget(*candidate);
                    if(candidateDeprioritized && bestTarget != nullptr) {
                        continue;
                    }

                    const bool bestIsDeprioritized = (bestTarget != nullptr) && isDeprioritizedTarget(*bestTarget);

                    if(distance < bestDistance ||
                       (bestIsDeprioritized && !candidateDeprioritized)) {
                        bestTarget = candidate;
                        bestDistance = distance;
                        ringProducedCandidate = true;
                    }
                }
            }
        }

        if(bestTarget != nullptr) {
            const FixPoint nextRingLowerBound = FixPoint((ring + 1) * cellSize);
            if(bestDistance <= nextRingLowerBound || ring == searchRadius) {
                break;
            }
        } else if(!huntMode) {
            const int ringOuterBound = (ring + 1) * cellSize;
            if(ringOuterBound > checkRange) {
                break;
            }
        }

        if(!ringProducedCandidate && !huntMode) {
            const int ringOuterBound = (ring + 1) * cellSize;
            if(ringOuterBound > checkRange) {
                break;
            }
        }
    }

    return bestTarget;
}

} // namespace

Coord ObjectBase::getClosestPoint(const Coord& point) const {
    return location;
}

const StructureBase* ObjectBase::findClosestTargetStructure() const {
    const StructureBase *pClosestStructure = nullptr;
    auto closestDistance = FixPt_MAX;
    for(const StructureBase* pStructure : structureList) {
        if(canAttack(pStructure)) {
            const auto closestPoint = pStructure->getClosestPoint(getLocation());
            auto structureDistance = blockDistance(getLocation(), closestPoint);

            if(pStructure->getItemID() == Structure_Wall) {
                structureDistance += 20000000; //so that walls are targeted very last
            }

            if(structureDistance < closestDistance) {
                closestDistance = structureDistance;
                pClosestStructure = pStructure;
            }
        }
    }

    return pClosestStructure;
}

const UnitBase* ObjectBase::findClosestTargetUnit() const {
    const UnitBase *pClosestUnit = nullptr;
    auto closestDistance = FixPt_MAX;
    for(const UnitBase* pUnit : unitList) {
        if(canAttack(pUnit)) {
            const auto closestPoint = pUnit->getClosestPoint(getLocation());
            const auto unitDistance = blockDistance(getLocation(), closestPoint);

            if(unitDistance < closestDistance) {
                closestDistance = unitDistance;
                pClosestUnit = pUnit;
            }
        }
    }

    return pClosestUnit;
}

const ObjectBase* ObjectBase::findClosestTarget() const {
    if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
        if(const ObjectBase* target = findTargetViaGrid(*this, *grid, 0, true)) {
            return target;
        }
    }

    return findClosestTargetLegacy(*this);
}

const ObjectBase* ObjectBase::findTarget() const {
    int checkRange = 0;
    bool huntMode = false;

    switch(attackMode) {
        case GUARD: {
            checkRange = getWeaponRange();
        } break;

        case AREAGUARD: {
            // Launchers get extended area guard range due to long weapon range
            checkRange = (getItemID() == Unit_Launcher) ? 12 : 10;
        } break;

        case AMBUSH: {
            checkRange = getViewRange();
        } break;

        case HUNT: {
            huntMode = true;
        } break;

        case STOP:
        default: {
            return nullptr;
        } break;
    }

    if(!huntMode && getItemID() == Unit_Sandworm) {
        checkRange = getViewRange();
    }

    // Special handling for Ornithopter: check if this is an Ornithopter and if so,
    // skip the spatial grid to let Ornithopter::findTarget() handle QuantBot-style scoring
    if(getItemID() != Unit_Ornithopter) {
        if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
            if(const ObjectBase* target = findTargetViaGrid(*this, *grid, checkRange, huntMode)) {
                return target;
            }
        }
    }

    return huntMode ? findClosestTargetLegacy(*this) : findTargetLegacy(*this, checkRange);
}

int ObjectBase::getViewRange() const {
    return currentGame->objectData.data[itemID][originalHouseID].viewrange;
}

int ObjectBase::getAreaGuardRange() const {
    return 2*getWeaponRange();
}

int ObjectBase::getWeaponRange() const {
    return currentGame->objectData.data[itemID][originalHouseID].weaponrange;
}

int ObjectBase::getWeaponReloadTime() const {
    return currentGame->objectData.data[itemID][originalHouseID].weaponreloadtime;
}

int ObjectBase::getInfSpawnProp() const {
    return currentGame->objectData.data[itemID][originalHouseID].infspawnprop;
}

ObjectBase* ObjectBase::createObject(int itemID, House* Owner, bool byScenario) {

    ObjectBase* newObject = nullptr;
    switch(itemID) {
        case Structure_Barracks:            newObject = new Barracks(Owner); break;
        case Structure_ConstructionYard:    newObject = new ConstructionYard(Owner); break;
        case Structure_GunTurret:           newObject = new GunTurret(Owner); break;
        case Structure_HeavyFactory:        newObject = new HeavyFactory(Owner); break;
        case Structure_HighTechFactory:     newObject = new HighTechFactory(Owner); break;
        case Structure_IX:                  newObject = new IX(Owner); break;
        case Structure_LightFactory:        newObject = new LightFactory(Owner); break;
        case Structure_Palace:              newObject = new Palace(Owner); break;
        case Structure_Radar:               newObject = new Radar(Owner); break;
        case Structure_Refinery:            newObject = new Refinery(Owner); break;
        case Structure_RepairYard:          newObject = new RepairYard(Owner); break;
        case Structure_RocketTurret:        newObject = new RocketTurret(Owner); break;
        case Structure_Silo:                newObject = new Silo(Owner); break;
        case Structure_StarPort:            newObject = new StarPort(Owner); break;
        case Structure_Wall:                newObject = new Wall(Owner); break;
        case Structure_WindTrap:            newObject = new WindTrap(Owner); break;
        case Structure_WOR:                 newObject = new WOR(Owner); break;
        case Structure_NuclearPlant:        newObject = new NuclearPlant(Owner); break;
        case Structure_PoliceStation:       newObject = new PoliceStation(Owner); break;
        case Structure_ZoneResidential:     newObject = new ResidentialZone(Owner); break;
        case Structure_ZoneCommercial:      newObject = new CommercialZone(Owner); break;
        case Structure_ZoneIndustrial:      newObject = new IndustrialZone(Owner); break;
        case Structure_Stadium:             newObject = new Stadium(Owner); break;
        case Structure_Airport:             newObject = new Airport(Owner); break;

        case Unit_Carryall:                 newObject = new Carryall(Owner); break;
        case Unit_Devastator:               newObject = new Devastator(Owner); break;
        case Unit_Deviator:                 newObject = new Deviator(Owner); break;
        case Unit_Frigate:                  newObject = new Frigate(Owner); break;
        case Unit_Harvester:                newObject = new Harvester(Owner); break;
        case Unit_Soldier:                  newObject = new Soldier(Owner); break;
        case Unit_Launcher:                 newObject = new Launcher(Owner); break;
        case Unit_MCV:                      newObject = new MCV(Owner); break;
        case Unit_Ornithopter:              newObject = new Ornithopter(Owner); break;
        case Unit_Quad:                     newObject = new Quad(Owner); break;
        case Unit_Saboteur:                 newObject = new Saboteur(Owner); break;
        case Unit_Sandworm:                 newObject = new Sandworm(Owner); break;
        case Unit_SiegeTank:                newObject = new SiegeTank(Owner); break;
        case Unit_SonicTank:                newObject = new SonicTank(Owner); break;
        case Unit_Tank:                     newObject = new Tank(Owner); break;
        case Unit_Trike:                    newObject = new Trike(Owner); break;
        case Unit_RaiderTrike:              newObject = new RaiderTrike(Owner); break;
        case Unit_Trooper:                  newObject = new Trooper(Owner); break;
        case Unit_AmbientAirplane:          newObject = new AmbientAirplane(Owner); break;
        case Unit_AmbientHelicopter:        newObject = new AmbientHelicopter(Owner); break;
        case Unit_Special: {
            switch(Owner->getHouseID()) {
                case HOUSE_HARKONNEN:       newObject = new Devastator(Owner); break;
                case HOUSE_ATREIDES:        newObject = new SonicTank(Owner); break;
                case HOUSE_ORDOS:           newObject = new Deviator(Owner); break;
                case HOUSE_FREMEN:
                case HOUSE_SARDAUKAR:
                case HOUSE_MERCENARY: {
                    if(currentGame->randomGen.randBool()) {
                         newObject = new SonicTank(Owner);
                    } else {
                        newObject = new Devastator(Owner);
                    }
                } break;
                default:    /* should never be reached */  break;
            }
        } break;

        default:                            newObject = nullptr;
                                            SDL_Log("ObjectBase::createObject(): %d is no valid ItemID!",itemID);
                                            break;
    }

    if(newObject == nullptr) {
        return nullptr;
    }

    newObject->byScenario = byScenario;

    Uint32 objectID = currentGame->getObjectManager().addObject(newObject);
    newObject->setObjectID(objectID);

    return newObject;
}

ObjectBase* ObjectBase::loadObject(InputStream& stream, int itemID, Uint32 objectID) {
    ObjectBase* newObject = nullptr;
    switch(itemID) {
        case Structure_Barracks:            newObject = new Barracks(stream); break;
        case Structure_ConstructionYard:    newObject = new ConstructionYard(stream); break;
        case Structure_GunTurret:           newObject = new GunTurret(stream); break;
        case Structure_HeavyFactory:        newObject = new HeavyFactory(stream); break;
        case Structure_HighTechFactory:     newObject = new HighTechFactory(stream); break;
        case Structure_IX:                  newObject = new IX(stream); break;
        case Structure_LightFactory:        newObject = new LightFactory(stream); break;
        case Structure_Palace:              newObject = new Palace(stream); break;
        case Structure_Radar:               newObject = new Radar(stream); break;
        case Structure_Refinery:            newObject = new Refinery(stream); break;
        case Structure_RepairYard:          newObject = new RepairYard(stream); break;
        case Structure_RocketTurret:        newObject = new RocketTurret(stream); break;
        case Structure_Silo:                newObject = new Silo(stream); break;
        case Structure_StarPort:            newObject = new StarPort(stream); break;
        case Structure_Wall:                newObject = new Wall(stream); break;
        case Structure_WindTrap:            newObject = new WindTrap(stream); break;
        case Structure_WOR:                 newObject = new WOR(stream); break;
        case Structure_NuclearPlant:        newObject = new NuclearPlant(stream); break;
        case Structure_PoliceStation:       newObject = new PoliceStation(stream); break;
        case Structure_ZoneResidential:     newObject = new ResidentialZone(stream); break;
        case Structure_ZoneCommercial:      newObject = new CommercialZone(stream); break;
        case Structure_ZoneIndustrial:      newObject = new IndustrialZone(stream); break;
        case Structure_Stadium:             newObject = new Stadium(stream); break;
        case Structure_Airport:             newObject = new Airport(stream); break;

        case Unit_Carryall:                 newObject = new Carryall(stream); break;
        case Unit_Devastator:               newObject = new Devastator(stream); break;
        case Unit_Deviator:                 newObject = new Deviator(stream); break;
        case Unit_Frigate:                  newObject = new Frigate(stream); break;
        case Unit_Harvester:                newObject = new Harvester(stream); break;
        case Unit_Soldier:                  newObject = new Soldier(stream); break;
        case Unit_Launcher:                 newObject = new Launcher(stream); break;
        case Unit_MCV:                      newObject = new MCV(stream); break;
        case Unit_Ornithopter:              newObject = new Ornithopter(stream); break;
        case Unit_Quad:                     newObject = new Quad(stream); break;
        case Unit_Saboteur:                 newObject = new Saboteur(stream); break;
        case Unit_Sandworm:                 newObject = new Sandworm(stream); break;
        case Unit_SiegeTank:                newObject = new SiegeTank(stream); break;
        case Unit_SonicTank:                newObject = new SonicTank(stream); break;
        case Unit_Tank:                     newObject = new Tank(stream); break;
        case Unit_Trike:                    newObject = new Trike(stream); break;
        case Unit_RaiderTrike:              newObject = new RaiderTrike(stream); break;
        case Unit_Trooper:                  newObject = new Trooper(stream); break;
        case Unit_AmbientAirplane:          newObject = new AmbientAirplane(stream); break;
        case Unit_AmbientHelicopter:        newObject = new AmbientHelicopter(stream); break;

        default:                            newObject = nullptr;
                                            SDL_Log("ObjectBase::loadObject(): %d is no valid ItemID!",itemID);
                                            break;
    }

    if(newObject == nullptr) {
        return nullptr;
    }

    newObject->setObjectID(objectID);

    return newObject;
}

bool ObjectBase::targetInWeaponRange() const {
    Coord coord = (target.getObjPointer())->getClosestPoint(location);
    FixPoint dist = blockDistance(location,coord);

    return ( dist <= currentGame->objectData.data[itemID][originalHouseID].weaponrange);
}
