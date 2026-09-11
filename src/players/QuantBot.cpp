#include <dunecity/CityStructurePopulation.h>
#include <dunecity/ZonePower.h>
#include <players/LocalPointIndex.h>
#include <players/CityRoadRepairPolicy.h>
#include <players/UnitMixPolicy.h>
#include <players/CityServiceInvestmentPolicy.h>
#include <dunecity/PoliceCoveragePolicy.h>
#include <dunecity/VanillaEconomy.h>
#include <structures/AdvancedWindTrap.h>
#include <structures/Scoutpost.h>
#include <structures/ZoneStructure.h>
#include <players/RedevelopmentPolicy.h>
#include <structures/NuclearPlant.h>
#include <structures/WindTrap.h>
#include <players/TacticalSafetyPolicy.h>
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


#include <players/QuantBot.h>
#include <players/SimpleArmyPolicy.h>
#include <players/QuantBotConfig.h>

#include <Game.h>
#include <GameInitSettings.h>
#include <Map.h>
#include <sand.h>
#include <House.h>

#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <structures/ConstructionYard.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/CityPlacementPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <structures/RepairYard.h>
#include <structures/Palace.h>
#include <units/UnitBase.h>
#include <units/GroundUnit.h>
#include <units/AirUnit.h>
#include <units/MCV.h>
#include <units/Harvester.h>
#include <units/Saboteur.h>
#include <units/Devastator.h>

#include <vector>
#include <limits>
#include <units/Carryall.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>
#include <dunecity/TrafficSimulation.h>
#include <Command.h>
#include <CommandManager.h>

#include <algorithm>
#include <set>

#define AIUPDATEINTERVAL 50



 /**
  TODO

  New list from Dec 2016
  - Some harvesters getting 'stuck' by base when 100% full
  - rocket launchers are firing on units too close again...
  - unit rally points need to be adjusted for unit producers
  - add in writing of game log to a repository

  - fix game performance when toomany units


  New list from May 2016
  - units should move at start
  - fix single player campaign crash
  - fix unit allocation bug - atredes only building light tanks


  == Building Placement ==


  ia) build concrete when no placement locations are available == in progress, bugs exist ==
  iii) increase favourability of being near other buildings == 50% done ==

  1. Refinerys near spice == tried but failed ==
  4. Repair yards factories, & Turrets near enemy == 50% done ==
  5. All buildings away from enemy other that silos and turrets


  == buildings ==
  i) stop repair when just on yellow (at 50%) == 50% done, still broken for some buildings as goes into yellow health ==
  ii) silo build broken == fixed ==


  building algo still leaving gaps
  increase alignment score when sides match

  == Units ==
  ii) units that get stuck in buildings should be transported to squadcenter =%80=
  vii) fix attack timer =%80=
  viii) when attack timer exceeds a certain value then all fing units are set to area guard

  2) harvester return distance bug been introduced.= in progress ==

  3) carryalls sit over units hovering bug introduced.... fix scramble units and defend + manual carryall = 50% =

  4) theres a bug in on increment and decrement units...

  5) turn off force move to rally point after attacked = 50% =
  6) reduce turret building when lacking a military = 50% =

  7) remove turrets from nuke target calculation =50%=
  8) adjust turret placement algo to include points for proximitry to base centre =50%=



  1. Harvesters deploy away from enemy
  5. fix gun turret & gun for rocket turret

  x. Improve squad management

  == New work ==
  1. Add them with some logic =50%=
  2. fix force ratio optimisation algorithm,
  need to make it based off kill / death ratio instead of just losses =50%=
  3. create a retreate mechanism = 50% = still need to add retreat timer, say 1 retreat per minute, max
  - fix rally point and ybut deploy logic


  2. Make carryalls and ornithopers easier to hit

  ====> FIX WORM CRASH GAME BUG

  **/



QuantBot::QuantBot(House* associatedHouse, const std::string& playername, Difficulty difficulty, bool supportModeEnabled)
	: Player(associatedHouse, playername), difficulty(difficulty), supportMode(supportModeEnabled) {

	// MULTIPLAYER FIX: Use deterministic stagger based on house ID instead of random
	// This prevents desync issues in multiplayer games
	buildTimer = (getHouse()->getHouseID() % 4) * 50;  // 0-150 cycles stagger

    const QuantBotConfig& config = getQuantBotConfig();

    attackTimer = SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
        currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());

    retreatTimer = MILLI2CYCLES(60000); //turning off

	// Different AI logic for Campaign. Assumption is if player is loading they are playing a campaign game
	if ((currentGame->gameType == GameType::Campaign) || (currentGame->gameType == GameType::LoadSavegame) || (currentGame->gameType == GameType::Skirmish)) {
		gameMode = GameMode::Campaign;
	}
	else {
		gameMode = GameMode::Custom;
	}

	if (gameMode == GameMode::Campaign) {
		// Wait a while if it is a campaign game

		switch (currentGame->techLevel) {
		case 6: {
			attackTimer = MILLI2CYCLES(540000);
		}break;

		case 7: {
			attackTimer = MILLI2CYCLES(600000);
		}break;

		case 8: {
			attackTimer = MILLI2CYCLES(720000);
		}break;

		default: {
			attackTimer = MILLI2CYCLES(480000);
		}

		}
	}

	if (supportMode) {
		gameMode = GameMode::Custom;
		attackTimer = std::numeric_limits<Sint32>::max();
	}
}

std::string QuantBot::getDifficultyName() const {
    switch (difficulty) {
        case Difficulty::Easy: return "easy";
        case Difficulty::Medium: return "medium";
        case Difficulty::Hard: return "hard";
        case Difficulty::Brutal: return "brutal";
        case Difficulty::Defend: return "defend";
    }
    return "unknown";
}


QuantBot::QuantBot(InputStream& stream, House* associatedHouse) : Player(stream, associatedHouse) {
	QuantBot::init();

	difficulty = static_cast<Difficulty>(stream.readUint8());
	gameMode = static_cast<GameMode>(stream.readUint8());
	buildTimer = stream.readSint32();
	attackTimer = stream.readSint32();
	retreatTimer = stream.readSint32();

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		initialItemCount[i] = stream.readUint32();
	}
	initialMilitaryValue = stream.readSint32();
	militaryValueLimit = stream.readSint32();
	harvesterLimit = stream.readSint32();
	lastCalculatedSpice = stream.readSint32();
	campaignAIAttackFlag = stream.readBool();

	squadRallyLocation.x = stream.readSint32();
	squadRallyLocation.y = stream.readSint32();
	squadRetreatLocation.x = stream.readSint32();
	squadRetreatLocation.y = stream.readSint32();

	// Need to add in a building array for when people save and load
	// So that it keeps the count of buildings that should be on the map.
	Uint32 NumPlaceLocations = stream.readUint32();
	for (Uint32 i = 0; i < NumPlaceLocations; i++) {
		Sint32 x = stream.readSint32();
		Sint32 y = stream.readSint32();

        placeLocations.emplace_back(x, y);
    }

    try {
        supportMode = stream.readBool();
    } catch(const InputStream::eof&) {
        supportMode = false;
    } catch(const InputStream::error&) {
        supportMode = false;
    }

    if (currentGame->getLoadedSavegameVersion() >= 9827) {
        rallySelectedCycle = stream.readUint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9828) {
        nonServiceConstructionOrders = std::min<Uint32>(3, stream.readUint32());
    }
    if (currentGame->getLoadedSavegameVersion() >= 9829) {
        powerDemandSampleCycle = stream.readUint32();
        powerDemandSample = stream.readSint32();
        projectedPowerDemandGrowth = stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9830) {
        groundSquadPhase=stream.readUint32(); groundSquadStarted=stream.readUint32();
        groundSquadNextControl=stream.readUint32(); groundSquadInitialCount=stream.readUint32();
        groundSquadObjective=stream.readUint32(); groundSquadObjectiveCycle=stream.readUint32();
        const auto count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) groundSquad.insert(stream.readUint32());
        auto readMap=[&](auto& values) {
            const auto n=stream.readUint32();
            for (Uint32 i=0;i<n;++i) { const auto id=stream.readUint32(); const auto value=stream.readUint32(); values[id]=value; }
        };
        readMap(manualUnitOrders); readMap(escortAssignments);
        const auto losses=stream.readUint32();
        for (Uint32 i=0;i<losses;++i) {
            RecentStructureLoss loss;
            loss.location.x=stream.readSint32(); loss.location.y=stream.readSint32();
            loss.size.x=stream.readSint32(); loss.size.y=stream.readSint32();
            loss.cycle=stream.readUint32(); loss.item=stream.readUint32(); recentStructureLosses.push_back(loss);
        }
        performanceHistory.load(stream);
    }
    if (currentGame->getLoadedSavegameVersion() >= 9831) {
        groundSquadProgressCycle=stream.readUint32();
        groundSquadProgressLocation.x=stream.readSint32();
        groundSquadProgressLocation.y=stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9832) {
        const Uint32 count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) {
            const Uint32 key=stream.readUint32();
            defenceResponseCycles[key]=stream.readUint32();
        }
    }
    if (supportMode) {
        gameMode = GameMode::Custom;
        attackTimer = std::numeric_limits<Sint32>::max();
    }
}


void QuantBot::init() {
	// Load QuantBot configuration from file on first init
	// This will create the config file with defaults if it doesn't exist
	getQuantBotConfig();

	// Clear idle harvester counters (important for loading saved games)
    idleHarvesterCounters.clear();
    harvesterMovingCounters.clear();

	SDL_Log("QuantBot initialized with external configuration");
}


QuantBot::~QuantBot() = default;

void QuantBot::save(OutputStream& stream) const {
	Player::save(stream);

	stream.writeUint8(static_cast<Uint8>(difficulty));
	stream.writeUint8(static_cast<Uint8>(gameMode));
	stream.writeSint32(buildTimer);
	stream.writeSint32(attackTimer);
	stream.writeSint32(retreatTimer);

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		stream.writeUint32(initialItemCount[i]);
	}
	stream.writeSint32(initialMilitaryValue);
	stream.writeSint32(militaryValueLimit);
	stream.writeSint32(harvesterLimit);
	stream.writeSint32(lastCalculatedSpice);
	stream.writeBool(campaignAIAttackFlag);

	stream.writeSint32(squadRallyLocation.x);
	stream.writeSint32(squadRallyLocation.y);
	stream.writeSint32(squadRetreatLocation.x);
	stream.writeSint32(squadRetreatLocation.y);

	stream.writeUint32(placeLocations.size());
    for (const Coord& placeLocation : placeLocations) {
        stream.writeSint32(placeLocation.x);
        stream.writeSint32(placeLocation.y);
    }

    stream.writeBool(supportMode);
    stream.writeUint32(rallySelectedCycle);
    stream.writeUint32(nonServiceConstructionOrders);
    stream.writeUint32(powerDemandSampleCycle);
    stream.writeSint32(powerDemandSample);
    stream.writeSint32(projectedPowerDemandGrowth);
    stream.writeUint32(groundSquadPhase); stream.writeUint32(groundSquadStarted);
    stream.writeUint32(groundSquadNextControl); stream.writeUint32(groundSquadInitialCount);
    stream.writeUint32(groundSquadObjective); stream.writeUint32(groundSquadObjectiveCycle);
    stream.writeUint32(static_cast<Uint32>(groundSquad.size()));
    for (const auto id:groundSquad) stream.writeUint32(id);
    auto writeMap=[&](const auto& values) {
        stream.writeUint32(static_cast<Uint32>(values.size()));
        for (const auto& entry:values) { stream.writeUint32(entry.first); stream.writeUint32(entry.second); }
    };
    writeMap(manualUnitOrders); writeMap(escortAssignments);
    stream.writeUint32(static_cast<Uint32>(recentStructureLosses.size()));
    for (const auto& loss:recentStructureLosses) {
        stream.writeSint32(loss.location.x); stream.writeSint32(loss.location.y);
        stream.writeSint32(loss.size.x); stream.writeSint32(loss.size.y);
        stream.writeUint32(loss.cycle); stream.writeUint32(loss.item);
    }
    performanceHistory.save(stream);
    stream.writeUint32(groundSquadProgressCycle);
    stream.writeSint32(groundSquadProgressLocation.x);
    stream.writeSint32(groundSquadProgressLocation.y);
    writeMap(defenceResponseCycles);

}


bool QuantBot::permitsPoliceReinforcement(int unitValue) const {
    if (difficulty != Difficulty::Brutal) return true;
    if (!currentGame || militaryValueLimit <= 0) return false;
    int value = 0;
    // Match the production allocator's military valuation, including new batch members.
    for (Uint32 item = Unit_FirstID; item <= Unit_LastID; ++item) {
        if (item != Unit_Carryall && item != Unit_Harvester && item != Unit_MCV && item != Unit_Sandworm)
            value += getHouse()->getNumItems(item)
                * currentGame->objectData.data[item][getHouse()->getHouseID()].price;
    }
    return value < militaryValueLimit && value + unitValue <= militaryValueLimit;
}

void QuantBot::update() {
	// Safety check: if our house is null (e.g., during game cleanup), don't update
	if (getHouse() == nullptr) {
		return;
	}

	if (!supportMode && getPlayerclass().rfind("qBotSupport", 0) == 0) {
		supportMode = true;
		gameMode = GameMode::Custom;
		attackTimer = std::numeric_limits<Sint32>::max();
	}

	if (getGameCycleCount() == 0) {
		// The game just started and we gather some
		// Count the items once initially

		// First count all the objects we have
		for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
			initialItemCount[i] = getHouse()->getNumItems(i);
			logDebug("Initial: Item: %d  Count: %d", i, initialItemCount[i]);
		}

		// Allow Campaign AI (including support mode) one Repair Yard
		// Note: supportMode sets gameMode to Custom, so check currentGame->gameType instead
		if ((initialItemCount[Structure_RepairYard] == 0) && currentGame && currentGame->gameType == GameType::Campaign && currentGame->techLevel > 4) {
			initialItemCount[Structure_RepairYard] = 1;
			if (initialItemCount[Structure_Radar] == 0) {
				initialItemCount[Structure_Radar] = 1;
			}

			if (initialItemCount[Structure_LightFactory] == 0) {
				initialItemCount[Structure_LightFactory] = 1;
			}

			logDebug("Allow Campaign AI one Repair Yard (support: %s)", supportMode ? "yes" : "no");
		}

		// Calculate the total military value of the player
		initialMilitaryValue = 0;
		if (currentGame) {
			for (Uint32 i = Unit_FirstID; i <= Unit_LastID; i++) {
				if (i != Unit_Carryall
					&& i != Unit_Harvester
					&& i != Unit_MCV
					&& i != Unit_Sandworm) {
					// Used for campaign mode.
					initialMilitaryValue += initialItemCount[i] * currentGame->objectData.data[i][getHouse()->getHouseID()].price;
				}
			}
		}



	// Get config for this difficulty
	const QuantBotConfig& config = getQuantBotConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

	// Log which config this QuantBot is using
	logDebug("=== QuantBot [%s - %s] Initialization ===", 
		getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())).c_str(),
		gameMode == GameMode::Campaign ? "Campaign" : "Custom");

	switch (gameMode) {
	case GameMode::Campaign: {
		// Use config values for campaign mode
		harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
		militaryValueLimit = lround(initialMilitaryValue * diffSettings.militaryValueMultiplier);

		logDebug("  Difficulty: %s", 
			difficulty == Difficulty::Defend ? "Defend" :
			difficulty == Difficulty::Easy ? "Easy" :
			difficulty == Difficulty::Medium ? "Medium" :
			difficulty == Difficulty::Hard ? "Hard" : "Brutal");
		logDebug("  Mission: %d", currentGame ? currentGame->getGameInitSettings().getMission() : 0);
		logDebug("  Initial Military Value: %d", initialMilitaryValue);
		logDebug("  Initial Refineries: %d", initialItemCount[Structure_Refinery]);
		logDebug("  Config: HarvesterMult=%d, MilitaryMult=%.1fx",
			diffSettings.harvesterLimitPerRefineryMultiplier,
			diffSettings.militaryValueMultiplier);

		// Special case for late missions (mission 21+)
		if (currentGame && currentGame->getGameInitSettings().getMission() >= 21) {
			if (difficulty == Difficulty::Easy && militaryValueLimit < 2000) {
				militaryValueLimit = 2000;
				logDebug("  Mission 21+ override: MilitaryValueLimit = 2000");
			}
			else if (difficulty == Difficulty::Medium && militaryValueLimit < 4000) {
				militaryValueLimit = 4000;
				logDebug("  Mission 21+ override: MilitaryValueLimit = 4000");
			}
			else if (difficulty == Difficulty::Hard) {
				initialItemCount[Structure_Refinery] = 2;
				militaryValueLimit = 10000;
				harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
				logDebug("  Mission 21+ override: Refineries=2, MilitaryValueLimit=10000");
			}
		}

		// Refinery top-up: Ensure AI has at least the minimum refineries for difficulty
		if (diffSettings.refineryMinimum > 0 && initialItemCount[Structure_Refinery] < diffSettings.refineryMinimum) {
			int refineriesToAdd = diffSettings.refineryMinimum - initialItemCount[Structure_Refinery];
			initialItemCount[Structure_Refinery] = diffSettings.refineryMinimum;
			harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
			logDebug("  Refinery top-up: Had %d, topped up to %d (granted %d refineries)", 
				initialItemCount[Structure_Refinery] - refineriesToAdd, 
				diffSettings.refineryMinimum,
				refineriesToAdd);
		} else if (diffSettings.refineryMinimum > 0) {
			logDebug("  Refinery check: Has %d (minimum %d already met, no top-up needed)", 
				initialItemCount[Structure_Refinery], diffSettings.refineryMinimum);
		}

		// Apply game options harvester override if set and lower than calculated limit
		int harvesterOverride = currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
		if (harvesterOverride >= 0 && harvesterOverride < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterOverride);
			harvesterLimit = harvesterOverride;
		}

		logDebug("  FINAL: HarvesterLimit=%d, MilitaryValueLimit=%d", 
			harvesterLimit, militaryValueLimit);

		// Set initial unit position and group units at squad rally point (Hard and Brutal only)
		if (difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal) {
			squadRallyLocation = findSquadRallyLocation();

			// Move all military units to the squad rally location at game start
			if (squadRallyLocation.isValid()) {
				logDebug("  Moving all units to squad rally point: (%d, %d)", 
					squadRallyLocation.x, squadRallyLocation.y);

				int unitsMoved = 0;
				for (const UnitBase* pUnit : getUnitList()) {
					if (pUnit->getOwner() == getHouse()
						&& pUnit->getItemID() != Unit_Carryall
						&& pUnit->getItemID() != Unit_Sandworm
						&& pUnit->getItemID() != Unit_Harvester
						&& pUnit->getItemID() != Unit_MCV
						&& pUnit->getItemID() != Unit_Frigate) {

						doMove2Pos(pUnit, squadRallyLocation.x, squadRallyLocation.y, true);
						unitsMoved++;
					}
				}

				logDebug("  Moved %d units to rally point", unitsMoved);
			}
		}

	} break;

	case GameMode::Custom: {
		// set initial unit position
		squadRallyLocation = findSquadRallyLocation();

		// Move all military units to the squad rally location at game start
		if (squadRallyLocation.isValid()) {
			logDebug("  Moving all units to squad rally point: (%d, %d)", 
				squadRallyLocation.x, squadRallyLocation.y);

			int unitsMoved = 0;
			for (const UnitBase* pUnit : getUnitList()) {
				if (pUnit->getOwner() == getHouse()
					&& pUnit->getItemID() != Unit_Carryall
					&& pUnit->getItemID() != Unit_Sandworm
					&& pUnit->getItemID() != Unit_Harvester
					&& pUnit->getItemID() != Unit_MCV
					&& pUnit->getItemID() != Unit_Frigate) {

					doMove2Pos(pUnit, squadRallyLocation.x, squadRallyLocation.y, true);
					unitsMoved++;
				}
			}

			logDebug("  Moved %d units to rally point", unitsMoved);
		}

		// Set harvester/military limits based on map size and difficulty from config
		int mapsize = 4096; // Default fallback size
		if (currentGameMap) {
			mapsize = currentGameMap->getSizeX() * currentGameMap->getSizeY();
		}

		logDebug("  Difficulty: %s", 
			difficulty == Difficulty::Defend ? "Defend" :
			difficulty == Difficulty::Easy ? "Easy" :
			difficulty == Difficulty::Medium ? "Medium" :
			difficulty == Difficulty::Hard ? "Hard" : "Brutal");
		logDebug("  Map Size: %dx%d = %d tiles",
			currentGameMap ? currentGameMap->getSizeX() : 64,
			currentGameMap ? currentGameMap->getSizeY() : 64,
			mapsize);

		// Use config values based on map size
		if (mapsize <= 1024) {
			// Small map (32x32)
			harvesterLimit = diffSettings.harvesterLimitCustomSmallMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomSmallMap;
			logDebug("  Map Category: Small (32x32)");
		} else if (mapsize <= 4096) {
			// Medium map (62x62, 64x64)
			harvesterLimit = diffSettings.harvesterLimitCustomMediumMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomMediumMap;
			logDebug("  Map Category: Medium (64x64)");
		} else if (mapsize <= 16384) {
			// Large map (up to 128x128)
			harvesterLimit = diffSettings.harvesterLimitCustomLargeMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomLargeMap;
			logDebug("  Map Category: Large (up to 128x128)");
		} else {
			// Huge maps (> 128x128) - use config values
			harvesterLimit = diffSettings.harvesterLimitCustomHugeMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomHugeMap;
			logDebug("  Map Category: Huge (> 128x128)");
		}

		logDebug("  Config Values - Small(H:%d,M:%d) Med(H:%d,M:%d) Large(H:%d,M:%d)",
			diffSettings.harvesterLimitCustomSmallMap, diffSettings.militaryValueLimitCustomSmallMap,
			diffSettings.harvesterLimitCustomMediumMap, diffSettings.militaryValueLimitCustomMediumMap,
			diffSettings.harvesterLimitCustomLargeMap, diffSettings.militaryValueLimitCustomLargeMap);

		// Apply game options harvester override if set and lower than calculated limit
		int harvesterOverride = currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
		if (harvesterOverride >= 0 && harvesterOverride < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterOverride);
			harvesterLimit = harvesterOverride;
		}

		logDebug("  FINAL: HarvesterLimit=%d, MilitaryValueLimit=%d", 
			harvesterLimit, militaryValueLimit);

		// what is this useful for? Reseting limits or something
		/*
		if ((currentGameMap->getSizeX() * currentGameMap->getSizeY() / 480) < harvesterLimit && difficulty != Difficulty::Brutal) {
			harvesterLimit = currentGameMap->getSizeX() * currentGameMap->getSizeY() / 480;
			logDebug("Reset harvesterLimit: %d = mapX: %d * mapY: %d / 480", harvesterLimit, currentGameMap->getSizeX(), currentGameMap->getSizeY());
		}*/

	} break;

		}

		// Calculate total spice remaining on map and adjust harvester limit for both modes
		lastCalculatedSpice = 0;
		if (currentGameMap) {
			const int mapSizeX = currentGameMap->getSizeX();
			const int mapSizeY = currentGameMap->getSizeY();

			for (int x = 0; x < mapSizeX; x++) {
				for (int y = 0; y < mapSizeY; y++) {
					if (currentGameMap->tileExists(x, y)) {
						Tile* pTile = currentGameMap->getTile(x, y);
						if (pTile && pTile->hasSpice()) {
							lastCalculatedSpice += pTile->getSpice().lround();
						}
					}
				}
			}
		}

		// Apply spice-based harvester limit only for Custom mode
		if (gameMode == GameMode::Custom) {
			// Don't build more harvesters if total spice < 2000 * harvester count
			int maxHarvestersForSpice = lastCalculatedSpice / 2000;
			if (maxHarvestersForSpice < harvesterLimit) {
				harvesterLimit = std::max(1, maxHarvestersForSpice); // Always allow at least 1 harvester
				logDebug("Harvester limit reduced due to low spice: %d (spice: %d)", harvesterLimit, lastCalculatedSpice);
			}
		}

		logDebug("Initial spice calculation: %d spice remaining on map", lastCalculatedSpice);
	}

	// Recalculate spice periodically (not every cycle — full map scan is O(N) on 65K+ tiles).
	// Stagger by house ID so multiple AI players don't spike on the same frame.
	if ((getGameCycleCount() + getHouse()->getHouseID() * 100) % 500 == 0) {
		lastCalculatedSpice = 0;
		if (currentGameMap) {
			const int mapSizeX = currentGameMap->getSizeX();
			const int mapSizeY = currentGameMap->getSizeY();

			for (int x = 0; x < mapSizeX; x++) {
				for (int y = 0; y < mapSizeY; y++) {
					if (currentGameMap->tileExists(x, y)) {
						Tile* pTile = currentGameMap->getTile(x, y);
						if (pTile && pTile->hasSpice()) {
							lastCalculatedSpice += pTile->getSpice().lround();
						}
					}
				}
			}
		}
	}

	// Continuously adjust harvester limit based on remaining spice (both Campaign and Custom modes)
	// This runs every cycle to dynamically reduce harvester targets as spice depletes
	const QuantBotConfig& config = getQuantBotConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

	int baseHarvesterLimit = harvesterLimit;

	// Check if harvester override is set in game options
	int harvesterOverride = currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;

	if (harvesterOverride >= 0) {
		// Use the game options override
		baseHarvesterLimit = harvesterOverride;
	} else if (gameMode == GameMode::Custom) {
		// Custom mode: Use map size defaults from ObjectData.ini
		const int mapsize = currentGameMap->getSizeX() * currentGameMap->getSizeY();
		if (mapsize < 1024) {  // < 32x32
			baseHarvesterLimit = currentGame->objectData.harvesterLimitSmallMap;
		} else if (mapsize < 4096) {  // < 64x64
			baseHarvesterLimit = currentGame->objectData.harvesterLimitMediumMap;
		} else if (mapsize < 16384) {  // < 128x128
			baseHarvesterLimit = currentGame->objectData.harvesterLimitLargeMap;
		} else {  // >= 128x128 (Huge)
			baseHarvesterLimit = currentGame->objectData.harvesterLimitHugeMap;
		}
	} else if (gameMode == GameMode::Campaign) {
		// Campaign mode: normally baseHarvesterLimit is from multiplier * refinery count
		// BUT Brutal difficulty uses map size defaults from ObjectData.ini instead
		if (difficulty == Difficulty::Brutal) {
			const int mapsize = currentGameMap->getSizeX() * currentGameMap->getSizeY();
			if (mapsize < 1024) {  // < 32x32
				baseHarvesterLimit = currentGame->objectData.harvesterLimitSmallMap;
			} else if (mapsize < 4096) {  // < 64x64
				baseHarvesterLimit = currentGame->objectData.harvesterLimitMediumMap;
			} else if (mapsize < 16384) {  // < 128x128
				baseHarvesterLimit = currentGame->objectData.harvesterLimitLargeMap;
			} else {  // >= 128x128 (Huge)
				baseHarvesterLimit = currentGame->objectData.harvesterLimitHugeMap;
			}
		}
		// Other difficulties keep baseHarvesterLimit from multiplier * refinery count
	}

    if (harvesterOverride < 0 && gameMode == GameMode::Custom && !getHouse()->isPowerRequired())
        baseHarvesterLimit = DuneCity::vanillaHarvesterCapacity(baseHarvesterLimit);
    // The engine cap may come from an older save or an explicit scenario limit.
    if (getHouse()->getMaxHarvesters() > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, getHouse()->getMaxHarvesters());

	// Apply spice-based reduction for all modes and difficulties
	int maxHarvestersForSpice = lastCalculatedSpice / 2000;
	int oldLimit = harvesterLimit;
	harvesterLimit = std::min(baseHarvesterLimit, std::max(1, maxHarvestersForSpice));

	// Log when the limit changes
	if (oldLimit != harvesterLimit) {
		logDebug("Harvester limit adjusted: %d -> %d (spice: %d, base: %d, mode: %s, diff: %d)", 
			oldLimit, harvesterLimit, lastCalculatedSpice, baseHarvesterLimit, 
			(gameMode == GameMode::Campaign) ? "Campaign" : "Custom", static_cast<int>(difficulty));
	}

	if ((getGameCycleCount() + getHouse()->getHouseID()) % AIUPDATEINTERVAL != 0) {
		// we are not updating this AI player this cycle
		return;
	}

    updateHarvesterStrikeTelemetry();
	// Calculate the total military value of the player
	int militaryValue = 0;
	if (currentGame) {
		for (Uint32 i = Unit_FirstID; i <= Unit_LastID; i++) {
			if (i != Unit_Carryall
				&& i != Unit_Harvester
				&& i != Unit_MCV
				&& i != Unit_Sandworm) {
					militaryValue += getHouse()->getNumItems(i) * currentGame->objectData.data[i][getHouse()->getHouseID()].price;
			}
		}
	}

	// Log military stats every 30 seconds (game time)
	// MULTIPLAYER FIX: Use game cycles instead of SDL_GetTicks() to ensure
	// all clients execute this logging at the same game cycle
	static Uint32 lastMilitaryLogCycle = 0;
	const Uint32 currentCycle = getGameCycleCount();
	const Uint32 LOG_INTERVAL = MILLI2CYCLES(30000); // 30 seconds in game cycles

	if(lastMilitaryLogCycle == 0) {
		lastMilitaryLogCycle = currentCycle;
	} else if(currentCycle - lastMilitaryLogCycle >= LOG_INTERVAL) {
		SDL_Log("[QuantBot %s] ========== MILITARY STATUS ==========", getHouse()->getHouseID() == HOUSETYPE::HOUSE_HARKONNEN ? "Harkonnen" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_ATREIDES ? "Atreides" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_ORDOS ? "Ordos" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_FREMEN ? "Fremen" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_SARDAUKAR ? "Sardaukar" : "Mercenary");
		SDL_Log("[QuantBot] Military Value: %d (Initial: %d)", militaryValue, initialMilitaryValue);

		// Count units by type
		int infantry = getHouse()->getNumItems(Unit_Soldier) + getHouse()->getNumItems(Unit_Trooper) + getHouse()->getNumItems(Unit_Saboteur);
		int lightVehicles = getHouse()->getNumItems(Unit_Trike) + getHouse()->getNumItems(Unit_RaiderTrike) + getHouse()->getNumItems(Unit_Quad);
		int tanks = getHouse()->getNumItems(Unit_Tank) + getHouse()->getNumItems(Unit_SiegeTank) + getHouse()->getNumItems(Unit_Devastator) + getHouse()->getNumItems(Unit_SonicTank);
		int special = getHouse()->getNumItems(Unit_Launcher) + getHouse()->getNumItems(Unit_Deviator);
		int air = getHouse()->getNumItems(Unit_Ornithopter);

		int totalMilitary = infantry + lightVehicles + tanks + special + air;
		if(totalMilitary > 0) {
			SDL_Log("[QuantBot] Troop Composition: Infantry=%d (%.0f%%), Light=%d (%.0f%%), Tanks=%d (%.0f%%), Special=%d (%.0f%%), Air=%d (%.0f%%)",
					infantry, infantry * 100.0 / totalMilitary,
					lightVehicles, lightVehicles * 100.0 / totalMilitary,
					tanks, tanks * 100.0 / totalMilitary,
					special, special * 100.0 / totalMilitary,
					air, air * 100.0 / totalMilitary);
		}
		SDL_Log("[QuantBot] =====================================");
		lastMilitaryLogCycle = currentCycle;
	}

	checkAllUnits();

	if (buildTimer <= 0) {
		build(militaryValue);
	}
	else {
		buildTimer -= AIUPDATEINTERVAL;
	}

	if (!supportMode) {
		if (attackTimer <= 0) {
			attack(militaryValue);
		} else {
			attackTimer -= AIUPDATEINTERVAL;
		}
	} else {
		attackTimer = std::numeric_limits<Sint32>::max();
	}

	if (cityBuildTimer <= 0) {
		manageCityBuilding();
		cityBuildTimer = AIUPDATEINTERVAL * 10;
	} else {
		cityBuildTimer -= AIUPDATEINTERVAL;
	}
}


uint64_t QuantBot::traceDecision(const std::string& event, AITelemetry::Record details) const {
    if (!AITelemetry::log().enabled()) return 0;
    details.set("state_id", telemetryState);
    return AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), event, details);
}

void QuantBot::onObjectWasBuilt(const ObjectBase* pObject) {
    if (pObject) traceDecision("object_built", AITelemetry::Record().set("item", pObject->getItemID())
        .set("object", pObject->getObjectID()).set("x", pObject->getLocation().x).set("y", pObject->getLocation().y));
}


void QuantBot::onDecrementStructures(int itemID, const Coord& location) {
    if (currentGame
            && itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall)
        recentStructureLosses.push_back({location, getStructureSize(itemID), getGameCycleCount(), static_cast<Uint32>(itemID)});
    dangerUpdated = std::numeric_limits<Uint32>::max();
    traceDecision("structure_lost", AITelemetry::Record().set("item", itemID).set("x", location.x).set("y", location.y));
}


/// When we take losses we should hold off from attacking for longer...
void QuantBot::onDecrementUnits(int itemID) {
    traceDecision("unit_lost", AITelemetry::Record().set("item", itemID));
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer += MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 30 / (static_cast<Uint8>(difficulty) + 1));
		//logDebug("loss ");
			retreatTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 20);
	}
}


/// When we get kills we should re-attack sooner...
void QuantBot::onIncrementUnitKills(int itemID) {
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 15);
		//logDebug("kill ");
	}
}

void QuantBot::onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) {
	const ObjectBase* pDamager = getObject(damagerID);

	if (pDamager == nullptr || pDamager->getOwner() == getHouse() || pObject->getItemID() == Unit_Sandworm) {
		return;
	}

    // If the human has attacked us then its time to start fighting back... unless its an attack on a special unit
    // Don't trigger with fremen or saboteur
    bool bPossiblyOwnFremen = (pObject->getOwner()->getHouseID() == HOUSE_ATREIDES) && (pObject->getItemID() == Unit_Trooper) && (currentGame->techLevel > 7);
    if(gameMode == GameMode::Campaign && !pDamager->getOwner()->isAI() && !campaignAIAttackFlag && !bPossiblyOwnFremen && (pObject->getItemID() != Unit_Saboteur)) {
        campaignAIAttackFlag = true;
    } else if (pObject->isAStructure()) {
        doRepair(pObject);
        // no point scrambling to defend a missile
        if(pDamager->getItemID() != Structure_Palace) {
            scrambleUnitsAndDefend(pDamager);
        }

	}
	else if (!supportMode && pObject->isAGroundUnit()) {
		const GroundUnit* pGroundUnit = static_cast<const GroundUnit*>(pObject);

		if (pGroundUnit->isAwaitingPickup()) {
			return;
		}

		// Stop him dead in his tracks if he's going to rally point
		if (!humanControls(pGroundUnit) && pGroundUnit->wasForced() && (pGroundUnit->getItemID() != Unit_Harvester)) {
			doMove2Pos(pGroundUnit,
				pGroundUnit->getLocation().x,
				pGroundUnit->getLocation().y,
				false);
		}

		if (pGroundUnit->getItemID() == Unit_Harvester) {
			// Always keep Harvesters away from harm
			// Defend the harvester!
			const Harvester* pHarvester = static_cast<const Harvester*>(pGroundUnit);
			if (pHarvester->isActive()) {
				scrambleUnitsAndDefend(pDamager);
                auto& safety = harvesterSafety[pHarvester->getObjectID()];
                safety.nextCheck = 0;
                safety.retreatUntil = getGameCycleCount() + MILLI2CYCLES(30000);
                bool recorded = false;
                for (auto& field : unsafeFields)
                    if (blockDistance(field.location,pHarvester->getLocation()) <= 3) {
                        field.cycle = getGameCycleCount(); recorded = true; break;
                    }
                if (!recorded) unsafeFields.push_back({pHarvester->getLocation(),getGameCycleCount()});
                dangerUpdated = std::numeric_limits<Uint32>::max();
                refreshTacticalDanger();
                manageHarvesterSafety(pHarvester);
			}
		}
		else if ((pGroundUnit->getItemID() == Unit_Launcher
			|| pGroundUnit->getItemID() == Unit_Deviator)
			&& !supportMode) {
			// Keep Launchers/Deviators away from harm when taking damage (not in support mode)
			doSetAttackMode(pGroundUnit, AREAGUARD);
			int weaponRange = currentGame->objectData.data[pGroundUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
			kiteAwayFromThreat(pGroundUnit, pDamager, weaponRange);

		}
		else if (QuantBotBuildPolicy::isLightRaider(pGroundUnit->getItemID())
			&& QuantBotBuildPolicy::isArmoredTank(pDamager->getItemID())) {
			// A hit is authoritative even if targeting changed between AI updates.
			// Retreat beyond the tank's own weapon range, not merely to the squad.
			doSetAttackMode(pGroundUnit, AREAGUARD);
			kiteAwayFromThreat(pGroundUnit, pDamager, pDamager->getWeaponRange() + 2);
			traceDecision("light_raider_evade", AITelemetry::Record().set("unit", pGroundUnit->getObjectID())
				.set("threat", pDamager->getObjectID()).set("reason", "tank_hit")
				.set("desired_range", pDamager->getWeaponRange() + 2));
		}

		// If unit is below 80% then rotate them
		// If the unit is at 60% health or less and is not being forced to move anywhere
		// only do these acitons for vehicles and not when fighting turrets
		// repair them, if they are eligible to be repaired
		if (difficulty != Difficulty::Easy) {
			if (pGroundUnit->getHealth() / pGroundUnit->getMaxHealth() < 0.80_fix
				&& !pGroundUnit->isInfantry()
				&& pGroundUnit->isVisible()
				&& (pDamager->getItemID() != Structure_GunTurret
					&& pDamager->getItemID() != Structure_RocketTurret)
				) {


				// If unit isn't an infrantry then heal it once it is below 2/3 health if not an easy or medium campaign
				if (getHouse()->hasRepairYard()
					&& pGroundUnit->getHealth() / pGroundUnit->getMaxHealth() < 0.6_fix

					// don't do manual repairs if it's campaign and easy or medium difficulty
					&& !(gameMode == GameMode::Campaign && (difficulty == Difficulty::Easy || difficulty == Difficulty::Medium))
					) {
					doRepair(pGroundUnit);
				}

				// Rotate unit backwards if it is taking damage if it is softer
				else if (pGroundUnit->getItemID() != Unit_Devastator 
						&& pGroundUnit->getItemID() != Unit_SiegeTank) {
					doSetAttackMode(pGroundUnit, AREAGUARD);
					moveToOptimalSquadPosition(pGroundUnit, 6);  // 6 tile radius
				}



			}
		}
	}
}

Coord QuantBot::findMcvPlaceLocation(const MCV* pMCV) {
    AITelemetry::PerformanceScope perfScope("ai.findMcvPlaceLocation", getGameCycleCount(), getHouse()->getHouseID());
	// Always search for best location near the MCV's current position
	// This works for both first MCV and expansion MCVs.
	//
	// Perf bound: the distance penalty (-10 per tile) makes any spot more
	// than ~30 tiles away strictly worse than a closer candidate, so a full
	// map scan is pointless. Cap the outer search to a window around the
	// MCV and the inner rock-count to a small radius — the inner is just an
	// "is there room here" heuristic, not a precise survey. Without these
	// caps this function is O(W*H*innerR^2) ~= 23M ops per call on a 192^2
	// map, and gets called per undeployed MCV per AI tick.
	constexpr int kOuterRadius = 25;
	constexpr int kInnerRadius = 6;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();
	Coord mcvLocation = pMCV->getLocation();

	const int mapW = getMap().getSizeX();
	const int mapH = getMap().getSizeY();
	const int xLo = std::max(1, mcvLocation.x - kOuterRadius);
	const int xHi = std::min(mapW - 2, mcvLocation.x + kOuterRadius);
	const int yLo = std::max(1, mcvLocation.y - kOuterRadius);
	const int yHi = std::min(mapH - 2, mcvLocation.y + kOuterRadius);

	for (int placeLocationX = xLo; placeLocationX <= xHi; placeLocationX++) {
		for (int placeLocationY = yLo; placeLocationY <= yHi; placeLocationY++) {
			Coord placeLocation(placeLocationX, placeLocationY);

			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, 2, 2, false, nullptr)
                && !overlapsReservedStructure(placeLocationX,placeLocationY,2,2)
                && preservesGroundAccess(Structure_ConstructionYard,placeLocation)) {
				int locationScore = 0;

				// Calculate distance penalty (closer is better)
				int distance = lround(blockDistance(mcvLocation, placeLocation));
				locationScore -= distance * 10;  // Strong penalty for distance - MCVs should deploy near where they spawn

				// Calculate available rock in the area (more buildable space is better)
				int availableRock = 0;

				for (int x = placeLocationX - kInnerRadius; x <= placeLocationX + kInnerRadius; x++) {
					for (int y = placeLocationY - kInnerRadius; y <= placeLocationY + kInnerRadius; y++) {
						if (getMap().tileExists(x, y)) {
							const Tile* pTile = getMap().getTile(x, y);
							// Count rock tiles that aren't mountains (buildable with concrete)
							if (pTile->isRock() && !pTile->isMountain() && !pTile->hasAGroundObject()) {
								availableRock++;
							}
						}
					}
				}

				// Score based on available rock
				// A 2x2 building needs 4 tiles, so 6 buildings = 24 tiles minimum
				// But we want more space for growth
				int buildingSites = availableRock / 4;  // Rough estimate of potential building count

				if (buildingSites >= 6) {
					// Location has room for 6+ buildings, give good base score
					locationScore += 200;
					// Additional bonus for even more space (diminishing returns)
					locationScore += (buildingSites - 6) * 5;
				} else {
					// Not enough space - heavy penalty
					locationScore += buildingSites * 15;  // Still give some credit
					locationScore -= 100;  // But penalize insufficient space heavily
				}

				// Bonus for being somewhat central but not too far
				// Prefer locations that aren't at extreme corners
				int distanceFromCenter = lround(blockDistance(placeLocation, 
					Coord(getMap().getSizeX() / 2, getMap().getSizeY() / 2)));
				int mapRadius = (getMap().getSizeX() + getMap().getSizeY()) / 4;

				if (distanceFromCenter < mapRadius / 2) {
					locationScore += 20;  // Bonus for being near map center
				}

				// Pick best location
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = placeLocation;
				}
			}
		}
	}

	if (bestLocation.isValid()) {
		logDebug("MCV deployment location found at (%d, %d) with score %d", 
			bestLocation.x, bestLocation.y, bestLocationScore);
	}

	return bestLocation;
}

namespace {

CityPlacementPolicy::RoadImpact cityRoadImpact(const Map& map, int x, int y, int w, int h, Uint32 item) {
    if (!currentGame || !currentGame->isCitySimEnabled() || item == Structure_Slab1
        || item == Structure_Slab4 || item == Structure_Road) return {};
    return CityPlacementPolicy::assessRoadsOnMap(map.getSizeX(), map.getSizeY(), x, y, w, h,
        item == Structure_RocketTurret,
        [&](int tx, int ty) { return map.tileExists(tx, ty) && map.getTile(tx, ty)->isRoadConnection(); },
        [&](int tx, int ty) {
            const auto* tile=map.getTile(tx,ty);
            return tile && !tile->hasAGroundObject() && DuneCity::isCityBuildableTerrain(tile->getType());
        });
}

// A structure on this tile that is one of our city zones, or nullptr.
const StructureBase* ownZoneAt(const Map& map, int houseID, int x, int y) {
	if (!map.tileExists(x, y)) return nullptr;
	const ObjectBase* pObject = map.getTile(x, y)->getNonInfantryGroundObject();
	if (pObject == nullptr || !pObject->isAStructure()) return nullptr;
	const auto* pStructure = static_cast<const StructureBase*>(pObject);
	if (!DuneCity::isCityZoneStructure(pStructure->getItemID())) return nullptr;
	if (pStructure->getOwner() == nullptr || pStructure->getOwner()->getHouseID() != houseID) return nullptr;
	return pStructure;
}

// Count nearby R/C/I on each side, including across a single road tile.
// Two or more occupied sides identify an infill gap, independently of zone type.
int residentialInfillSides(const Map& map,int house,int x,int y,int w,int h) {
    int sides=0;
    for (int side=0;side<4;++side) {
        bool found=false;
        for (int gap=1;gap<=2 && !found;++gap) {
            const int length=side<2 ? w : h;
            for (int offset=0;offset<length;++offset) {
                const int tx=side<2 ? x+offset : (side==2 ? x-gap : x+w-1+gap);
                const int ty=side>=2 ? y+offset : (side==0 ? y-gap : y+h-1+gap);
                found |= ownZoneAt(map,house,tx,ty)!=nullptr;
            }
        }
        sides+=found;
    }
    return sides;
}

// Prefer filling a four-zone block, but only where its perimeter can carry roads.
int fourZoneBlockBonus(const Map& map, int house, int x, int y) {
    int best=0;
    for (int oy : {0,2}) for (int ox : {0,2}) {
        const int bx=x-ox, by=y-oy;
        if (!CityPlacementPolicy::fourZoneBlockFits(map.getSizeX(),map.getSizeY(),bx,by)) continue;
        bool valid=true; int neighbours=0;
        for (int sy=0; sy<4 && valid; sy+=2) for (int sx=0; sx<4 && valid; sx+=2) {
            const int zx=bx+sx, zy=by+sy;
            if (zx==x && zy==y) continue;
            const auto* zone=ownZoneAt(map,house,zx,zy);
            if (zone && zone->getLocation()==Coord(zx,zy)) { ++neighbours; continue; }
            for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
                const auto* tile=map.getTile(zx+dx,zy+dy);
                if (!tile || tile->hasAStructure() || !DuneCity::isCityZoneTerrain(tile->getType())) valid=false;
            }
        }
        for (int dy=-1; dy<=4 && valid; ++dy) for (int dx=-1; dx<=4 && valid; ++dx) {
            if (dx>=0 && dx<4 && dy>=0 && dy<4) continue;
            const auto* tile=map.getTile(bx+dx,by+dy);
            if (!tile || (!tile->isRoadConnection() && (tile->hasAStructure()
                || !DuneCity::isCityBuildableTerrain(tile->getType())))) valid=false;
        }
        if (valid) best=std::max(best,neighbours*90);
    }
    return best;
}

// True when the tile could carry a road or already does, ignoring tiles that
// the candidate footprint (x, y, w, h) is about to cover.
bool tileKeepsFrontage(const Map& map, int tx, int ty, int x, int y, int w, int h) {
	if (!map.tileExists(tx, ty)) return false;
	if (tx >= x && tx < x + w && ty >= y && ty < y + h) return false;
	const Tile* t = map.getTile(tx, ty);
	if (t->isRoad()) return true;
	return !t->hasAStructure() && !t->hasCityZone() && !t->isMountain()
		&& !t->hasAGroundObject() && DuneCity::isCityZoneTerrain(t->getType());
}

// Would a lot at (x, y, w, h) take away the last open side of a neighbouring
// zone? Every lot must keep a side where a road can run.
bool wouldLandlockNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h) {
	std::set<Uint32> checked;
	auto sealsNeighbour = [&](int nx, int ny) {
		const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
		if (pZone == nullptr || !checked.insert(pZone->getObjectID()).second) return false;
		const int zx = pZone->getX(), zy = pZone->getY();
		const int zw = pZone->getStructureSizeX(), zh = pZone->getStructureSizeY();
		for (int i = zx; i < zx + zw; i++) {
			if (tileKeepsFrontage(map, i, zy - 1, x, y, w, h)) return false;
			if (tileKeepsFrontage(map, i, zy + zh, x, y, w, h)) return false;
		}
		for (int j = zy; j < zy + zh; j++) {
			if (tileKeepsFrontage(map, zx - 1, j, x, y, w, h)) return false;
			if (tileKeepsFrontage(map, zx + zw, j, x, y, w, h)) return false;
		}
		return true;
	};
	for (int i = x; i < x + w; i++) {
		if (sealsNeighbour(i, y - 1) || sealsNeighbour(i, y + h)) return true;
	}
	for (int j = y; j < y + h; j++) {
		if (sealsNeighbour(x - 1, j) || sealsNeighbour(x + w, j)) return true;
	}
	return false;
}

// Is there one of our zones directly beside this lot, or one road tile away,
// sharing its row or column? That is the "next to each other or one away"
// pattern that keeps a road path along every row of lots.
bool alignedWithNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h) {
	const int offsets[4][2] = { { w, 0 }, { w + 1, 0 }, { 0, h }, { 0, h + 1 } };
	for (const auto& offset : offsets) {
		for (int sign = -1; sign <= 1; sign += 2) {
			const int nx = x + sign * offset[0];
			const int ny = y + sign * offset[1];
			const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
			if (pZone != nullptr && pZone->getX() == nx && pZone->getY() == ny) return true;
		}
	}
	return false;
}

} // namespace

void QuantBot::refreshTacticalDanger() {
    AITelemetry::PerformanceScope perfScope("ai.refreshTacticalDanger", getGameCycleCount(), getHouse()->getHouseID());
    const Uint32 now = getGameCycleCount();
    if (dangerUpdated != std::numeric_limits<Uint32>::max()
        && now - dangerUpdated < MILLI2CYCLES(2000)) return;
    dangerUpdated = now;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    tacticalDanger.assign(w*h, 0);
    harvesterDanger.assign(w*h, 0);
    lossDanger.assign(w*h, 0);
    visibleEnemyBases.clear();
    visibleHarvestLaunchers.clear();
    const bool emitSafety = AITelemetry::log().enabled()
        && (lastSafetyTrace == std::numeric_limits<Uint32>::max() || now-lastSafetyTrace >= MILLI2CYCLES(30000));
    AITelemetry::Record threats;
    auto stamp = [&](std::vector<int>& grid, Coord p, int radius, int strength) {
        if (p.isInvalid()) return;
        for (int y = std::max(0, p.y-radius); y <= std::min(h-1, p.y+radius); ++y)
            for (int x = std::max(0, p.x-radius); x <= std::min(w-1, p.x+radius); ++x)
                grid[y*w+x] = std::min(10000, grid[y*w+x] + strength);
    };
    auto observe = [&](const ObjectBase* object) {
        if (!object || !object->getOwner() || object->getHealth() <= 0
            || object->getOwner()->getTeamID() == getHouse()->getTeamID()
            || !object->isVisible(getHouse()->getTeamID()) || object->getLocation().isInvalid()) return;
        if (object->isAStructure()) visibleEnemyBases.push_back(object->getLocation());
        // Palace missiles are addressed by spacing, not a permanent map-wide veto.
        if (!object->canAttack() || object->getItemID() == Structure_Palace) return;
        Coord p = object->getLocation();
        if (const auto* structure = dynamic_cast<const StructureBase*>(object))
            p += Coord(structure->getStructureSizeX()/2, structure->getStructureSizeY()/2);
        const int radius = std::max(1, object->getWeaponRange()) + 2;
        stamp(tacticalDanger, p, radius, 100);
        // Launchers get an early-warning margin: a harvester must turn before missiles arrive.
        // Tracked harvesters can crush foot troops; do not treat them like tanks.
        const int harvestRadius = TacticalSafetyPolicy::harvesterThreatRadius(object->getItemID(),object->getWeaponRange());
        if (object->getItemID()==Unit_Launcher) visibleHarvestLaunchers.push_back(object->getObjectID());
        for (int y = std::max(0,p.y-harvestRadius); y <= std::min(h-1,p.y+harvestRadius); ++y)
            for (int x = std::max(0,p.x-harvestRadius); x <= std::min(w-1,p.x+harvestRadius); ++x)
                if ((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y) <= harvestRadius*harvestRadius)
                    harvesterDanger[y*w+x] += 100;
        if (emitSafety) threats.set(std::to_string(object->getObjectID()), AITelemetry::Record()
            .set("item",object->getItemID()).set("x",p.x).set("y",p.y).set("radius_tiles",radius)
            .set("harvester_radius_tiles",harvestRadius));
    };
    for (const auto* unit : getUnitList()) if (unit->isActive()) observe(unit);
    for (const auto* structure : getStructureList()) observe(structure);
    factoryEnemyClearance = TacticalSafetyPolicy::enemyClearance(tacticalDanger,w,h);
    for (const auto& loss : recentStructureLosses) {
        const Uint32 age = now - loss.cycle;
        const Uint32 lifetime=loss.item==Structure_HeavyFactory ? MILLI2CYCLES(900000) : MILLI2CYCLES(300000);
        if (age >= lifetime) continue;
        const int strength = TacticalSafetyPolicy::lossStrength(age,lifetime);
        for (int y = 0; y < loss.size.y; ++y) for (int x = 0; x < loss.size.x; ++x)
            stamp(lossDanger, loss.location + Coord(x,y), 3, strength);
    }
    unsafeFields.erase(std::remove_if(unsafeFields.begin(), unsafeFields.end(),
        [&](const auto& field) { return now-field.cycle >= MILLI2CYCLES(120000); }), unsafeFields.end());
    for (auto it = harvesterSafety.begin(); it != harvesterSafety.end();) {
        if (!currentGame->getObjectManager().getObject(it->first)) {
            if (it->second.lastLocation.isValid()) unsafeFields.push_back({it->second.lastLocation, now});
            it = harvesterSafety.erase(it);
        } else ++it;
    }
    if (emitSafety) {
        lastSafetyTrace = now;
        traceDecision("tactical_safety_snapshot", AITelemetry::Record().set("visible_threats",threats)
            .set("recent_structure_losses",recentStructureLosses.size()).set("unsafe_fields",unsafeFields.size()));
    }
}

int QuantBot::dangerAt(Coord pos, Coord size, bool losses) const {
    const auto& grid = losses ? lossDanger : tacticalDanger;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    if (pos.isInvalid() || grid.size() != static_cast<size_t>(w*h)) return 0;
    int danger = 0;
    for (int y = std::max(0,pos.y); y < std::min(h,pos.y+size.y); ++y)
        for (int x = std::max(0,pos.x); x < std::min(w,pos.x+size.x); ++x)
            danger = std::max(danger, grid[y*w+x]);
    return danger;
}

bool QuantBot::reactorClearance(Uint32 item, Coord pos) const {
    const auto size = getStructureSize(item);
    const auto critical = TacticalSafetyPolicy::protectedReactorNeighbour;
    if (!critical(item)) return true;
    auto clears = [&](Uint32 other, Coord location, Coord otherSize) {
        if (!((item == Structure_NuclearPlant && critical(other))
            || (other == Structure_NuclearPlant && critical(item)))) return true;
        return TacticalSafetyPolicy::blastClearance(pos.x,pos.y,size.x,size.y,
            location.x,location.y,otherSize.x,otherSize.y);
    };
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && !clears(structure->getItemID(), structure->getLocation(), structure->getStructureSize())) return false;
    for (const auto& entry : reservedStructures)
        if (entry.first != planningBuilder && !clears(entry.second.item, entry.second.location, getStructureSize(entry.second.item))) return false;
    return true;
}

int QuantBot::rearScore(Coord pos, Coord base) const {
    if (visibleEnemyBases.empty()) return 0;
    int fromSite = 100000, fromBase = 100000;
    for (Coord enemy : visibleEnemyBases) {
        fromSite = std::min(fromSite, std::max(std::abs(pos.x-enemy.x), std::abs(pos.y-enemy.y)));
        fromBase = std::min(fromBase, std::max(std::abs(base.x-enemy.x), std::abs(base.y-enemy.y)));
    }
    return std::clamp(fromSite-fromBase, -20, 20) * 40;
}

int QuantBot::recentFactoryLossCount() const {
    return std::count_if(recentStructureLosses.begin(), recentStructureLosses.end(), [&](const auto& loss) {
        return loss.item == Structure_HeavyFactory && getGameCycleCount()-loss.cycle < MILLI2CYCLES(120000);
    });
}

bool QuantBot::manageHarvesterSafety(const Harvester* harvester) {
    const Uint32 now = getGameCycleCount();
    auto& state = harvesterSafety[harvester->getObjectID()];
    const bool newHarvester = state.lastLocation.isInvalid();
    state.lastLocation = harvester->getLocation();
    if (now < state.nextCheck) return state.controlled;
    state.nextCheck = now + MILLI2CYCLES(2000);
    const Coord origin = harvester->getLocation(), destination = harvester->getDestination();
    auto distance = [](Coord a, Coord b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); };
    auto danger = [&](Coord p) {
        return p.isValid() && p.x < getMap().getSizeX() && p.y < getMap().getSizeY()
            && harvesterDanger.size() == static_cast<size_t>(getMap().getSizeX()*getMap().getSizeY())
            ? harvesterDanger[p.y*getMap().getSizeX()+p.x] : 0;
    };
    const bool threatened = danger(origin) > 0;
    // A remembered incident is a preference penalty, never a two-minute veto.
    auto memoryPenalty = [&](Coord p) {
        int penalty = 0;
        for (const auto& field : unsafeFields)
            if (distance(p,field.location) <= 6 && now-field.cycle < MILLI2CYCLES(120000))
                penalty = std::max(penalty, 12-static_cast<int>((now-field.cycle)/MILLI2CYCLES(10000)));
        return penalty;
    };
    auto routeSafe = [&](Coord end) {
        return TacticalSafetyPolicy::escapeCorridor(origin.x,origin.y,end.x,end.y,
            [&](int x,int y) { return danger(Coord(x,y)); });
    };
    // Ask for help before the first missile lands. Use the cached visible
    // launcher list, and one nearest threat per check; incident debounce and
    // already-committed forces keep many harvesters from recruiting repeatedly.
    const ObjectBase* clearingTarget=nullptr;
    int nearestThreat=std::numeric_limits<int>::max();
    for (Uint32 id:visibleHarvestLaunchers) {
        const auto* enemy=getObject(id);
        if (!enemy || enemy->getHealth()<=0 || !enemy->canAttack(harvester)
            || !enemy->isVisible(getHouse()->getTeamID())
            || enemy->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const int radius=TacticalSafetyPolicy::harvesterThreatRadius(enemy->getItemID(),enemy->getWeaponRange());
        const int fromHarvester=distance(origin,enemy->getLocation());
        const int fromJob=destination.isValid() ? distance(destination,enemy->getLocation()) : fromHarvester;
        if (std::min(fromHarvester,fromJob)>radius || fromHarvester>radius+6) continue;
        if (fromHarvester<nearestThreat) { clearingTarget=enemy; nearestThreat=fromHarvester; }
    }
    if (clearingTarget) scrambleUnitsAndDefend(clearingTarget,true);
    // Never replace an active safe unloading trip with another spice order.
    if (harvester->isReturning()) {
        const auto* target = dynamic_cast<const StructureBase*>(harvester->getTarget());
        if (target && target->getOwner() == getHouse() && target->getHealth() > 0
            && target->acceptsHarvesterDropoff() && danger(target->getClosestPoint(origin)) == 0
            && routeSafe(target->getClosestPoint(origin))) {
            state.controlled = true;
            return true;
        }
    }
    if (TacticalSafetyPolicy::needsRefineryRefuge(threatened,danger(destination)>0,
            harvester->isReturning(),harvester->getAmountOfSpice()>0)) {
        const StructureBase* refuge = nullptr;
        int bestRefineryScore = std::numeric_limits<int>::max();
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
                || !structure->acceptsHarvesterDropoff()) continue;
            const Coord entry = structure->getClosestPoint(origin);
            if (danger(entry) > 0 || !routeSafe(entry)) continue;
            const int score = distance(origin,entry) + structure->getHarvesterDropoffBookings()*3;
            if (score < bestRefineryScore) { refuge=structure; bestRefineryScore=score; }
        }
        if (refuge) {
            state.controlled = true;
            state.plannedDestination = refuge->getLocation();
            if (harvester->getTarget() != refuge) {
                doMove2Object(harvester,refuge);
                traceDecision("harvester_safety",AITelemetry::Record().set("object",harvester->getObjectID())
                    .set("action","retreat_refinery").set("refinery",refuge->getObjectID())
                    .set("x",origin.x).set("y",origin.y).set("cargo",harvester->getAmountOfSpice().lround()));
            }
            return true;
        }
    }
    // Do not hold a safe vehicle after enemies leave. Keep its existing safe job.
    if (!threatened && destination.isValid() && danger(destination)==0
        && (harvester->isReturning() || (harvester->getAttackMode() != STOP
            && !newHarvester && routeSafe(destination)))) {
        state.controlled = false;
        state.retreatUntil = 0;
        return false;
    }
    state.controlled = true;
    std::vector<Coord> peers;
    for (const auto* unit : getUnitList())
        if (unit != harvester && unit->isActive() && unit->getOwner() == getHouse()
            && unit->getItemID() == Unit_Harvester)
        {
            const auto peer = harvesterSafety.find(unit->getObjectID());
            const Coord reserved = peer != harvesterSafety.end() && peer->second.controlled
                ? peer->second.plannedDestination : Coord::Invalid();
            peers.push_back(reserved.isValid() ? reserved
                : unit->getDestination().isValid() ? unit->getDestination() : unit->getLocation());
        }
    auto crowdPenalty = [&](Coord candidate) {
        int result = 0;
        for (Coord peer : peers) if (distance(candidate,peer)<4) result += (4-distance(candidate,peer))*12;
        return result;
    };
    Coord best = Coord::Invalid();
    int bestScore = std::numeric_limits<int>::max();
    int safeFields = 0, rejectedRoutes = 0;
    for (int y = 0; y < getMap().getSizeY(); ++y) for (int x = 0; x < getMap().getSizeX(); ++x) {
        const Coord candidate(x,y);
        if (!getMap().getTile(x,y)->hasSpice() || danger(candidate)>0 || !harvester->canPass(x,y)) continue;
        ++safeFields;
        const int score = distance(origin,candidate)*3 + memoryPenalty(candidate) + crowdPenalty(candidate);
        if (score >= bestScore) continue;
        if (!routeSafe(candidate)) { ++rejectedRoutes; continue; }
        bestScore = score; best = candidate;
    }
    const bool foundSpice = best.isValid();
    // If no safe spice corridor exists, disperse to the nearest safe open tile.
    // There is deliberately no base-centre attraction.
    if (!foundSpice) {
        for (int y = std::max(0,origin.y-20); y <= std::min(getMap().getSizeY()-1,origin.y+20); ++y)
            for (int x = std::max(0,origin.x-20); x <= std::min(getMap().getSizeX()-1,origin.x+20); ++x) {
                const Coord candidate(x,y);
                if (danger(candidate)>0 || !harvester->canPass(x,y)) continue;
                const int score = distance(origin,candidate)*3 + crowdPenalty(candidate);
                if (score < bestScore && routeSafe(candidate)) { bestScore=score; best=candidate; }
            }
    }
    state.plannedDestination = best; // reserve immediately, before queued movement commands execute
    if (best.isValid()) {
        const auto mode = foundSpice ? HARVEST : STOP;
        if (best != destination || harvester->getAttackMode() != mode) {
            doSetAttackMode(harvester, mode);
            doMove2Pos(harvester,best.x,best.y,!foundSpice);
        }
    } else doSetAttackMode(harvester, STOP);
    state.retreatUntil = 0;
    // Retrying the same evacuation route is execution noise. Preserve every
    // distinct safe-field decision, which is what later tuning can use.
    const uint64_t safetySignature = (uint64_t(foundSpice) << 32)
        | (uint64_t(best.x & 0xffff) << 16) | uint64_t(best.y & 0xffff);
    if (lastHarvesterSafetyTrace[harvester->getObjectID()] != safetySignature) {
        traceDecision("harvester_safety", AITelemetry::Record().set("object",harvester->getObjectID())
            .set("action",foundSpice ? "redirect_spice" : best.isValid() ? "disperse" : "no_safe_route")
            .set("x",origin.x).set("y",origin.y).set("destination_x",best.x).set("destination_y",best.y)
            .set("danger",danger(origin)).set("old_destination_danger",danger(destination))
            .set("safe_fields",safeFields).set("rejected_routes",rejectedRoutes)
            .set("memory_penalty",best.isValid()?memoryPenalty(best):0)
            .set("crowding_penalty",best.isValid()?crowdPenalty(best):0)
            .set("cargo",harvester->getAmountOfSpice().lround()));
        lastHarvesterSafetyTrace[harvester->getObjectID()] = safetySignature;
    }
    return true;
}

bool QuantBot::nearRecentStructureLoss(int x, int y, int width, int height) const {
    for (const auto& loss : recentStructureLosses)
        if (CityPlacementPolicy::recentLossBlocks(x, y, width, height,
                loss.location.x, loss.location.y, loss.size.x, loss.size.y,
                getGameCycleCount() - loss.cycle, MILLI2CYCLES(60000))) return true;
    return false;
}

bool QuantBot::overlapsReservedStructure(int x, int y, int width, int height) const {
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder) continue;
        const auto& plan = entry.second;
        const auto size = getStructureSize(plan.item);
        if (CityPlacementPolicy::overlaps(x, y, width, height,
                plan.location.x, plan.location.y, size.x, size.y)) return true;
    }
    return false;
}

namespace {
bool needsGroundExit(Uint32 item) {
    return item == Structure_HeavyFactory || item == Structure_LightFactory
        || item == Structure_Refinery || item == Structure_RepairYard
        || item == Structure_Barracks || item == Structure_WOR
        || item == Structure_StarPort || item == Structure_PoliceStation;
}
bool blocksGroundAccess(Uint32 item) {
    return item != Structure_Road && item != Structure_Slab1 && item != Structure_Slab4;
}
}

void QuantBot::clearPlacementCache(bool geometryChanged) {
    placementCache.clear();
    if (geometryChanged) {
        cityServiceSearch.invalidate();
        cityTurretSearch.invalidate();
    }
}

bool QuantBot::preservesGroundAccess(Uint32 item, Coord pos) {
    if (!blocksGroundAccess(item)) return true;
    if (!pos.isValid()) return false;
    const auto size=getStructureSize(item);
    auto passable=[&](int x,int y) {
        if (!getMap().tileExists(x,y)) return false;
        const auto* tile=getMap().getTile(x,y);
        if (tile->isMountain() || tile->hasAStructure()) return false;
        for (const auto& entry:reservedStructures) {
            if (entry.first==planningBuilder || !blocksGroundAccess(entry.second.item)) continue;
            const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
            if (x>=p.x && x<p.x+extent.x && y>=p.y && y<p.y+extent.y) return false;
        }
        return true;
    };
    if (!GroundAccessPolicy::allows({pos.x,pos.y,size.x,size.y},needsGroundExit(item),passable,
            item==Structure_RocketTurret)) return false;
    auto keepsExit=[&](Coord p,Coord extent) {
        for (int ey=p.y-1;ey<=p.y+extent.y;++ey) for (int ex=p.x-1;ex<=p.x+extent.x;++ex) {
            if (ex>=p.x && ex<p.x+extent.x && ey>=p.y && ey<p.y+extent.y) continue;
            if (ex>=pos.x && ex<pos.x+size.x && ey>=pos.y && ey<pos.y+size.y) continue;
            if (passable(ex,ey)) return true;
        }
        return false;
    };
    // Check only producers touching this placement, so its last deployment
    // opening cannot be covered. No scan of all factories or moving units.
    std::set<Uint32> checked;
    for (int y=pos.y-1;y<=pos.y+size.y;++y) for (int x=pos.x-1;x<=pos.x+size.x;++x) {
        if (x>=pos.x && x<pos.x+size.x && y>=pos.y && y<pos.y+size.y) continue;
        if (!getMap().tileExists(x,y)) continue;
        const auto* object=getMap().getTile(x,y)->getNonInfantryGroundObject();
        if (!object || !object->isAStructure() || !needsGroundExit(object->getItemID())
            || !checked.insert(object->getObjectID()).second) continue;
        if (!keepsExit(object->getLocation(),getStructureSize(object->getItemID()))) return false;
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder || !needsGroundExit(entry.second.item)) continue;
        const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
        if (CityPlacementPolicy::overlaps(pos.x-1,pos.y-1,size.x+2,size.y+2,p.x,p.y,extent.x,extent.y)
            && !keepsExit(p,extent)) return false;
    }
    return true;
}

bool QuantBot::redevelopmentZones(Uint32 item, Coord pos, std::vector<Uint32>& zones) const {
    zones.clear();
    auto* sim = currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    if (!sim || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return false;
    const Coord size = getStructureSize(item);
    bool range = false;
    for (int y=pos.y; y<pos.y+size.y; ++y) for (int x=pos.x; x<pos.x+size.x; ++x) {
        const auto* tile = getMap().getTile(x,y);
        if (!tile || !tile->isRock()) return false;
        range = range || getMap().isWithinBuildRange(x,y,getHouse());
        const auto* object = tile->getNonInfantryGroundObject();
        if (!object) { if (tile->isBlocked() || tile->hasCityZone()) return false; continue; }
        const auto* zone = dynamic_cast<const ZoneStructure*>(object);
        if (!zone || zone->getOwner() != getHouse()) return false;
        const Coord z = zone->getLocation();
        if (sim->getLandValueMap().worldGet(z.x,z.y) > 64
            || zone->getCivicOverlay() != ZoneStructure::CivicOverlay::None) return false;
        if (std::find(zones.begin(),zones.end(),zone->getObjectID()) == zones.end()) zones.push_back(zone->getObjectID());
    }
    return range && !zones.empty() && zones.size() <= 4;
}

Coord QuantBot::findRedevelopmentSite(Uint32 item) {
    AITelemetry::PerformanceScope perfScope("ai.findRedevelopmentSite", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame->isCitySimEnabled()
        || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return Coord::Invalid();
    const Coord size=getStructureSize(item), base=findBaseCentre(getHouse()->getHouseID());
    Coord best=Coord::Invalid();
    int bestScore=std::numeric_limits<int>::max();
    auto bestFactoryRank=TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    auto* sim=currentGame->getCitySimulation();
    if (!sim) return best;
    for (int y=std::max(0,base.y-50); y<=std::min(getMap().getSizeY()-size.y,base.y+50); ++y)
        for (int x=std::max(0,base.x-50); x<=std::min(getMap().getSizeX()-size.x,base.x+50); ++x) {
            const Coord pos(x,y);
            std::vector<Uint32> zones;
            if (!redevelopmentZones(item,pos,zones) || overlapsReservedStructure(x,y,size.x,size.y)
                || nearRecentStructureLoss(x,y,size.x,size.y) || dangerAt(pos,size)>0
                || !reactorClearance(item,pos) || !preservesGroundAccess(item,pos) || !cityRoadImpact(getMap(),x,y,size.x,size.y,item).preservesConnections) continue;
            int score=static_cast<int>(zones.size())*100;
            for (Uint32 id : zones) {
                const auto* zone=static_cast<const ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                const Coord z=zone->getLocation();
                const int density=std::max(int(getMap().getTile(z.x,z.y)->getCityZoneDensity()),
                    zone->getResidentialPopulation() > 0 ? 1 : 0);
                const auto& state=sim->getHouseState(getHouse()->getHouseID());
                const bool residential=zone->getItemID()==Structure_ZoneResidential;
                const int demand=residential ? state.resValve : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                score += RedevelopmentPolicy::displacementCost(density,sim->getLandValueMap().worldGet(z.x,z.y),demand,residential?2000:1500);
            }
            score += blockDistance(pos,base).lround()-rearScore(pos,base);
            const auto rank=TacticalSafetyPolicy::factorySiteRank(dangerAt(pos,size,true),
                TacticalSafetyPolicy::footprintClearance(factoryEnemyClearance,getMap().getSizeX(),getMap().getSizeY(),
                    x,y,size.x,size.y),0,-score);
            if (TacticalSafetyPolicy::productionFactory(item) ? rank>bestFactoryRank : score<bestScore) {
                bestFactoryRank=rank; bestScore=score; best=pos;
            }
        }
    return best;
}

Coord QuantBot::findPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    refreshTacticalDanger();
    int accessRejected = 0, pollutionRejected = 0;
	// Check per-build-cycle cache first
	auto cacheIt = placementCache.find(itemID);
	if (cacheIt != placementCache.end()) {
		return cacheIt->second;
	}

	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	int bestLocationScore = std::numeric_limits<int>::min();
	Coord bestLocation = Coord::Invalid();
    int bestSiteTier = -1;
    int bestInfill=0;
    bool bestSafe=false;
    const bool factoryPlacement = TacticalSafetyPolicy::productionFactory(itemID);
    auto bestFactoryRank = TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    AITelemetry::Record bestQuality;
    auto bestReactorRank = TacticalSafetyPolicy::reactorSiteRank(100000,100000,false,std::numeric_limits<int>::min());
    int candidates = 0, threatRejected = 0, blastRejected = 0, lossRejected = 0;

	bool itemIsBuilder = (itemID == Structure_HeavyFactory
		|| itemID == Structure_RepairYard
		|| itemID == Structure_LightFactory
		|| itemID == Structure_WOR
		|| itemID == Structure_Barracks
		|| itemID == Structure_StarPort);

	// City zones follow road-frontage rules instead of the compact-base
	// scoring: they sit next to each other or one road tile apart, and never
	// pack into blocks that landlock the inner lots.
	const bool cityZonePlacement = currentGame && currentGame->isCitySimEnabled()
		&& DuneCity::isCityZoneStructure(itemID);
	const int houseID = getHouse()->getHouseID();

	// Bound search to radius around base center instead of scanning entire map
	int searchRadius = 50;
	int mapW = getMap().getSizeX();
	int mapH = getMap().getSizeY();
	int startX = std::max(0, baseCenter.x - searchRadius);
	int startY = std::max(0, baseCenter.y - searchRadius);
	int endX = std::min(mapW - newSizeX, baseCenter.x + searchRadius);
	int endY = std::min(mapH - newSizeY, baseCenter.y + searchRadius);

    // Plan both sides of the pollution buffer, including other yards' queues.
    struct CityNeighbour { Coord location, size; bool pollutes, sensitive; DuneCity::CityRole role; };
    std::vector<CityNeighbour> cityNeighbours;
    auto* citySim = currentGame && currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    const auto newRole = DuneCity::getStructureCityRole(itemID);
    const bool newSensitive = newRole == DuneCity::CityRole::Residential || newRole == DuneCity::CityRole::Commercial;
    const bool newPolluter = DuneCity::getPollutionEmission(itemID, DuneCity::getStructureMaxLevel(itemID)) > 0;
    auto addNeighbour = [&](Uint32 item, Coord location, Coord size) {
        const auto role = DuneCity::getStructureCityRole(item);
        cityNeighbours.push_back({location, size,
            DuneCity::getPollutionEmission(item, DuneCity::getStructureMaxLevel(item)) > 0,
            role == DuneCity::CityRole::Residential || role == DuneCity::CityRole::Commercial, role});
    };
    if (citySim && (newSensitive || newPolluter)) {
        for (const auto* structure : getStructureList())
            if (structure->getOwner() == getHouse() && structure->getHealth() > 0)
                addNeighbour(structure->getItemID(), structure->getLocation(), structure->getStructureSize());
        for (const auto& entry : reservedStructures)
            if (entry.first != planningBuilder)
                addNeighbour(entry.second.item, entry.second.location, getStructureSize(entry.second.item));
    }

	// Pre-collect spice tile positions for refinery placement (avoids O(N^2) inner loop)
	std::vector<Coord> spiceTiles;
	if (itemID == Structure_Refinery) {
		// Only scan spice in a wider area around the base (no need for full map)
		int spiceRadius = searchRadius + 30;
		int spStartX = std::max(0, baseCenter.x - spiceRadius);
		int spStartY = std::max(0, baseCenter.y - spiceRadius);
		int spEndX = std::min(mapW - 1, baseCenter.x + spiceRadius);
		int spEndY = std::min(mapH - 1, baseCenter.y + spiceRadius);
		for (int sx = spStartX; sx <= spEndX; sx++) {
			for (int sy = spStartY; sy <= spEndY; sy++) {
				if (getMap().tileExists(sx, sy) && getMap().getTile(sx, sy)->hasSpice()) {
					spiceTiles.emplace_back(sx, sy);
				}
			}
		}
	}

	for (int placeLocationX = startX; placeLocationX <= endX; placeLocationX++) {
		for (int placeLocationY = startY; placeLocationY <= endY; placeLocationY++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, newSizeX, newSizeY,
				false, (itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {

                ++candidates;
                if (overlapsReservedStructure(placeLocationX, placeLocationY, newSizeX, newSizeY)) continue;
                // Use the same origin sample and role-specific gate as zone growth.
                // Industry tolerates pollution; R/C must not become vacant dead lots.
                if (citySim && cityZonePlacement && DuneCity::isPollutionBlockingGrowth(
                    citySim->getPollutionDensityMap().worldGet(placeLocationX, placeLocationY), newRole, 1)) {
                    ++pollutionRejected;
                    continue;
                }
                if (!preservesGroundAccess(itemID,Coord(placeLocationX,placeLocationY))) { ++accessRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall
                    && itemID != Structure_NuclearPlant
                    && nearRecentStructureLoss(placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++lossRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall) {
                    if (itemID != Structure_NuclearPlant && dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY)) > 0) { ++threatRejected; continue; }
                    if (!TacticalSafetyPolicy::reactorPlacementAllowed(itemID, reactorClearance(itemID, Coord(placeLocationX, placeLocationY)))) { ++blastRejected; continue; }
                }
                const auto roads = cityRoadImpact(getMap(), placeLocationX, placeLocationY, newSizeX, newSizeY, itemID);
                if (!roads.preservesConnections) continue;
                if (currentGame && currentGame->isCitySimEnabled()
                    && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) continue;
				int locationScore = 0;
                const int blockBonus = cityZonePlacement ? fourZoneBlockBonus(getMap(),houseID,placeLocationX,placeLocationY) : 0;
                locationScore += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				int placeLocationEndX = placeLocationX + newSizeX;
				int placeLocationEndY = placeLocationY + newSizeY;

		// Big bonus if building is directly at the map edge
		bool atMapEdge = (placeLocationX == 0 || placeLocationX + newSizeX >= getMap().getSizeX() ||
		                  placeLocationY == 0 || placeLocationY + newSizeY >= getMap().getSizeY());
		if (atMapEdge) {
			locationScore += 12;  // Bonus for edge placement
		}

			// Count adjacent friendly structures and track unique buildings per side
			int adjacentFriendlyStructureTiles = 0;
			int oneTileGapFriendlyStructureTiles = 0;
			std::set<Uint32> northSideBuildings;  // Buildings touching north side
			std::set<Uint32> southSideBuildings;  // Buildings touching south side
			std::set<Uint32> eastSideBuildings;   // Buildings touching east side
			std::set<Uint32> westSideBuildings;   // Buildings touching west side

			// Evaluate surrounding tiles
			for (int i = placeLocationX - 1; i <= placeLocationEndX; i++) {
				for (int j = placeLocationY - 1; j <= placeLocationEndY; j++) {
					if (getMap().tileExists(i, j) && (getMap().getSizeX() > i) && (0 <= i) && (getMap().getSizeY() > j) && (0 <= j)) {
					if (getMap().getTile(i, j)->hasAStructure()) {
						// Favor being near our buildings, avoid enemy buildings
						if (getMap().getTile(i, j)->getOwner() == getHouse()->getHouseID()) {
							adjacentFriendlyStructureTiles++;
							locationScore += cityZonePlacement ? 0 : 10;  // compact bases only; lots keep frontage instead

							// Track which side this building is on and which building it is
							const ObjectBase* pObject = getMap().getTile(i, j)->getObject();
							if (pObject) {
								Uint32 buildingID = pObject->getObjectID();

								// North side (j == placeLocationY - 1)
								if (j == placeLocationY - 1 && i >= placeLocationX && i < placeLocationEndX) {
									northSideBuildings.insert(buildingID);
								}
								// South side (j == placeLocationEndY)
								if (j == placeLocationEndY && i >= placeLocationX && i < placeLocationEndX) {
									southSideBuildings.insert(buildingID);
								}
								// West side (i == placeLocationX - 1)
								if (i == placeLocationX - 1 && j >= placeLocationY && j < placeLocationEndY) {
									westSideBuildings.insert(buildingID);
								}
								// East side (i == placeLocationEndX)
								if (i == placeLocationEndX && j >= placeLocationY && j < placeLocationEndY) {
									eastSideBuildings.insert(buildingID);
								}
							}
						}
						else {
							locationScore -= 10;
						}
					}
					else if (!getMap().getTile(i, j)->isRock() && !getMap().getTile(i, j)->hasPreparedFoundation()) {
						// Favor non-rock tiles (open buildable terrain)
						locationScore += 4;
					}
				else if (getMap().getTile(i, j)->hasAGroundObject()) {
					if (getMap().getTile(i, j)->getOwner() != getHouse()->getHouseID()) {
						// Avoid building next to enemy units
						locationScore -= 100;
					}
					// No penalty for own units
				}
					}
		// Don't penalize tiles outside map - edge placement should be encouraged
			}
		}

		// A second perimeter identifies structures separated by exactly one tile.
		// This lets some bases form lanes and courtyards without using randomness,
		// which would risk multiplayer lockstep divergence.
		for (int i = placeLocationX - 2; i <= placeLocationEndX + 1; i++) {
			for (int j = placeLocationY - 2; j <= placeLocationEndY + 1; j++) {
				const bool onOuterRing = (i == placeLocationX - 2 || i == placeLocationEndX + 1
					|| j == placeLocationY - 2 || j == placeLocationEndY + 1);
				if (!onOuterRing || !getMap().tileExists(i, j)) {
					continue;
				}
				const Tile* tile = getMap().getTile(i, j);
				if (tile->hasAStructure() && tile->getOwner() == getHouse()->getHouseID()) {
					oneTileGapFriendlyStructureTiles++;
				}
			}
		}

	// Penalty if any single side is touching multiple different buildings (gap-filling)
	int sidesWithMultipleBuildings = 0;
	if (northSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (southSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (eastSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (westSideBuildings.size() > 1) sidesWithMultipleBuildings++;

	if (sidesWithMultipleBuildings > 0) {
		// BAD: At least one side is touching multiple buildings (gap-filling)
		// Penalty should be smaller than benefit of adjacency to discourage but not completely prohibit
		locationScore -= 10 * sidesWithMultipleBuildings;
	}

	// Deterministically vary ordinary base spacing. One third of placements
	// prefer a one-cell lane, one third remain compact, and one third are
	// neutral. Defensive pieces, slabs, and refineries retain their specialist
	// placement behavior.
	const bool supportsVariedSpacing = !(currentGame && currentGame->isCitySimEnabled())
		&& getHouse()->getNumStructures() >= 3
		&& itemID != Structure_GunTurret
		&& itemID != Structure_RocketTurret
		&& itemID != Structure_Wall
		&& itemID != Structure_Slab1
		&& itemID != Structure_Slab4
		&& itemID != Structure_Refinery;
	if (supportsVariedSpacing) {
		const int spacingStyle = (getHouse()->getHouseID() * 37 + itemID * 17
			+ getHouse()->getNumStructures()) % 3;
		if (spacingStyle == 0) {
			locationScore -= adjacentFriendlyStructureTiles * 16;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 8) * 6;
		} else if (spacingStyle == 2) {
			locationScore -= adjacentFriendlyStructureTiles * 5;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 4) * 2;
		}
	}

	// Bonus for building on concrete tiles
	for (int i = placeLocationX; i < placeLocationEndX; i++) {
		for (int j = placeLocationY; j < placeLocationEndY; j++) {
			if (getMap().tileExists(i, j) && getMap().getTile(i, j)->hasPreparedFoundation()) {
				locationScore += 2;  // Small bonus - concrete protects from damage
			}
		}
	}

		// Building-specific positioning
		if (itemIsBuilder || factoryPlacement || itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
            if (!factoryPlacement)
			    locationScore -= lround(blockDistance(squadRallyLocation, Coord(placeLocationX, placeLocationY)));
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else if (itemID == Structure_Refinery) {
			// Refineries prefer being close to spice deposits
			int closestSpiceDistance = 10000;
			for (const auto& spiceCoord : spiceTiles) {
				int spiceDistance = lround(blockDistance(Coord(placeLocationX, placeLocationY), spiceCoord));
				if (spiceDistance < closestSpiceDistance) {
					closestSpiceDistance = spiceDistance;
				}
			}
			if (closestSpiceDistance < 10000) {
				locationScore += 50 - closestSpiceDistance * 2; // Strong bonus for being closer to spice
			}

			// Bonus for adjacent sand tiles (harvester access)
			// Double bonus if the sand has spice
			Coord structureSize = getStructureSize(itemID);
			for (int adjX = placeLocationX - 1; adjX <= placeLocationX + structureSize.x; adjX++) {
				for (int adjY = placeLocationY - 1; adjY <= placeLocationY + structureSize.y; adjY++) {
					// Skip tiles inside the structure footprint
					if (adjX >= placeLocationX && adjX < placeLocationX + structureSize.x &&
						adjY >= placeLocationY && adjY < placeLocationY + structureSize.y) {
						continue;
					}
					if (getMap().tileExists(adjX, adjY)) {
						const Tile* pTile = getMap().getTile(adjX, adjY);
						if (pTile->isSand()) {
							if (pTile->hasSpice()) {
								locationScore += 6; // Double bonus for sand with spice
							} else {
								locationScore += 3; // Base bonus for sand
							}
						}
					}
				}
			}

			// Also apply base center distance penalty (but weaker than spice bonus)
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else {
			// For other buildings, apply base center distance penalty
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		}

		// === CITY MODE: grid alignment + road spacing + zone-type scoring ===
		if (currentGame && currentGame->isCitySimEnabled()) {

			// Grid alignment: snap to a 3-cell grid (2-tile footprint +
			// 1-tile road gap) anchored on the base centre. Positions
			// that land on grid intersections get a massive bonus so the
			// AI naturally builds in neat rows with roads between.
			int gridOffsetX = ((placeLocationX - baseCenter.x) % 3 + 3) % 3;
			int gridOffsetY = ((placeLocationY - baseCenter.y) % 3 + 3) % 3;
            if (cityZonePlacement) {
                locationScore += blockBonus;
                if (CityPlacementPolicy::fourZoneGridSlot(placeLocationX,placeLocationY,baseCenter.x,baseCenter.y)) locationScore += 60;
            } else if (gridOffsetX == 0 && gridOffsetY == 0) {
				locationScore += 40;  // grid alignment bonus
			} else if (cityZonePlacement && alignedWithNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				locationScore += 50;  // continues a row of lots: touching or one road tile apart
			} else {
				locationScore -= 40;  // off-grid penalty
			}

			// Road-spacing: check 4 sides for road / open / structure.
			int sidesWithRoad = 0;
			int sidesWithOpen = 0;
			int sidesTouchingStructure = 0;

			struct SideCheck { int startI, startJ, endI, endJ; };
			SideCheck sides[4] = {
				{ placeLocationX, placeLocationY - 1, placeLocationEndX, placeLocationY },
				{ placeLocationX, placeLocationEndY, placeLocationEndX, placeLocationEndY + 1 },
				{ placeLocationX - 1, placeLocationY, placeLocationX, placeLocationEndY },
				{ placeLocationEndX, placeLocationY, placeLocationEndX + 1, placeLocationEndY }
			};

			for (const auto& side : sides) {
				bool sideHasRoad = false, sideHasOpen = false, sideTouchesStruct = false;
				for (int si = side.startI; si < side.endI; si++) {
					for (int sj = side.startJ; sj < side.endJ; sj++) {
						if (!getMap().tileExists(si, sj)) continue;
						const Tile* t = getMap().getTile(si, sj);
						if (t->isRoad()) sideHasRoad = true;
						else if (t->hasAStructure() || t->hasCityZone()) sideTouchesStruct = true;
						else if (t->isRock() && !t->isMountain() && !t->hasAGroundObject()) sideHasOpen = true;
					}
				}
				if (sideHasRoad) sidesWithRoad++;
				if (sideHasOpen) sidesWithOpen++;
				if (sideTouchesStruct) sidesTouchingStructure++;
			}

			if (sidesWithRoad == 0 && sidesWithOpen == 0) {
				continue;  // landlocked — skip
			}
			if (cityZonePlacement && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				continue;  // would take a neighbour's last road frontage
			}
			locationScore += sidesWithRoad * 25;
			locationScore += sidesWithOpen * 5;
			locationScore -= sidesTouchingStructure * (cityZonePlacement && blockBonus>0 ? 0 : 30);

			// Zone-type proximity scoring:
			// R/C avoid industrial pollution (radius 5) but want it
			// within supply range (16). I clusters with itself and
			// wants residential nearby for workers.
			bool isResidential = (itemID == Structure_ZoneResidential);
			bool isCommercial  = (itemID == Structure_ZoneCommercial);
			bool isIndustrial  = (itemID == Structure_ZoneIndustrial);

			if (isResidential || isCommercial || isIndustrial) {
				// Favour lots that use sand so rock stays free for Dune
				// structures. The placement check already guarantees at
				// least one rock tile under the lot.
				for (int px = placeLocationX; px < placeLocationEndX; px++) {
					for (int py = placeLocationY; py < placeLocationEndY; py++) {
						if (getMap().tileExists(px, py)) {
							const Tile* t = getMap().getTile(px, py);
							if (t->isSand() || t->isDunes()) locationScore += 6;
						}
					}
				}

				int closestIndDist = 100;
				int nearbyRes = 0, nearbyCom = 0, nearbyInd = 0;

				for (const StructureBase* pStruct : getStructureList()) {
					if (pStruct->getOwner() != getHouse()) continue;
					int dist = lround(blockDistance(
						Coord(placeLocationX, placeLocationY), pStruct->getLocation()));
					if (dist > 16) continue;  // outside supply radius

					int sid = pStruct->getItemID();
					if (sid == Structure_ZoneIndustrial) {
						nearbyInd++;
						if (dist < closestIndDist) closestIndDist = dist;
					}
					if (sid == Structure_ZoneResidential) nearbyRes++;
					if (sid == Structure_ZoneCommercial) nearbyCom++;
				}

				if (isResidential || isCommercial) {
					auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;

					// Penalty if within pollution radius of industrial
					if (closestIndDist <= 5) {
						locationScore -= 50;
					}
					// Bonus if industrial is reachable but outside pollution
					else if (closestIndDist <= 16) {
						locationScore += 20;
					}

					// Pollution density penalty (city sim layer)
					if (citySim) {
						const auto& polMap = citySim->getPollutionDensityMap();
						int totalPollution = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalPollution += polMap.worldGet(px, py);
							}
						}
						locationScore -= totalPollution / 10;
					}

					// Crime rate penalty (city sim layer)
					if (citySim) {
						const auto& crimeMap = citySim->getCrimeRateMap();
						int totalCrime = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalCrime += crimeMap.worldGet(px, py);
							}
						}
						locationScore -= totalCrime / 5;
					}

					// Sand adjacency bonus — higher land value near sand/desert
					Coord zoneSize = getStructureSize(itemID);
					int sandBonus = 0;
					for (int adjX = placeLocationX - 1; adjX <= placeLocationX + zoneSize.x; adjX++) {
						for (int adjY = placeLocationY - 1; adjY <= placeLocationY + zoneSize.y; adjY++) {
							if (adjX >= placeLocationX && adjX < placeLocationX + zoneSize.x &&
								adjY >= placeLocationY && adjY < placeLocationY + zoneSize.y)
								continue;
							if (getMap().tileExists(adjX, adjY) && getMap().getTile(adjX, adjY)->isSand())
								sandBonus += 5;
						}
					}
					locationScore += sandBonus;

					if (isResidential) {
						// Employment access: bonus if C or I zones reachable
						if (nearbyCom > 0 || nearbyInd > 0) locationScore += 20;
						// Extra for having both (mixed economy nearby)
						if (nearbyCom > 0 && nearbyInd > 0) locationScore += 10;
						// R ↔ C synergy
						if (nearbyCom > 0) locationScore += 15;
					}

					if (isCommercial) {
						// Commercial wants both R (customers) and I (supply) nearby
						if (nearbyRes > 0) locationScore += 20;
						if (nearbyInd > 0) locationScore += 15;
						// Strong bonus for being between R and I
						if (nearbyRes > 0 && nearbyInd > 0) locationScore += 15;
					}
				}

				if (isIndustrial) {
					// I clusters with other I (pollution doesn't affect I)
					locationScore += nearbyInd * 10;

					// I should stay away from R/C to avoid polluting them
					// but within commute distance (6-16 tiles = sweet spot)
					int closestResDist = 100;
					int closestComDist = 100;
					for (const StructureBase* pStruct : getStructureList()) {
						if (pStruct->getOwner() != getHouse()) continue;
						int sid = pStruct->getItemID();
						int dist = lround(blockDistance(
							Coord(placeLocationX, placeLocationY), pStruct->getLocation()));
						if (sid == Structure_ZoneResidential && dist < closestResDist)
							closestResDist = dist;
						if (sid == Structure_ZoneCommercial && dist < closestComDist)
							closestComDist = dist;
					}

					// Sweet spot: outside pollution radius but within commute
					if (closestResDist >= 6 && closestResDist <= 16) {
						locationScore += 25;
					} else if (closestResDist < 6) {
						locationScore -= 30;  // too close — will pollute residential
					} else if (closestResDist > 16) {
						locationScore -= 10;  // too far — no workers
					}

					if (closestComDist >= 6 && closestComDist <= 16) {
						locationScore += 15;
					} else if (closestComDist < 6) {
						locationScore -= 20;  // too close to commercial
					}
				}
			}
		}

                int siteTier = 0;
                AITelemetry::Record quality;
                quality.set("four_zone_block_bonus",blockBonus);
                if (citySim && (newSensitive || newPolluter)) {
                    int separation = 1000000;
                    int nearestR = 1000000, nearestC = 1000000, nearestI = 1000000;
                    for (const auto& neighbour : cityNeighbours) {
                        const int originDistance = std::max(std::abs(placeLocationX-neighbour.location.x),
                                                            std::abs(placeLocationY-neighbour.location.y));
                        if (neighbour.role == DuneCity::CityRole::Residential) nearestR = std::min(nearestR, originDistance);
                        if (neighbour.role == DuneCity::CityRole::Commercial) nearestC = std::min(nearestC, originDistance);
                        if (neighbour.role == DuneCity::CityRole::Industrial) nearestI = std::min(nearestI, originDistance);
                        if ((newSensitive && neighbour.pollutes) || (newPolluter && neighbour.sensitive))
                            separation = std::min(separation, CityPlacementPolicy::footprintDistance(
                                placeLocationX, placeLocationY, newSizeX, newSizeY,
                                neighbour.location.x, neighbour.location.y, neighbour.size.x, neighbour.size.y));
                    }
                    auto inReachOrMissing = [](int distance) { return distance == 1000000 || distance <= DuneCity::kSupplyRadius; };
                    const bool withinSupply = newRole == DuneCity::CityRole::Residential
                        ? inReachOrMissing(std::min(nearestC, nearestI))
                        : newRole == DuneCity::CityRole::Commercial
                            ? inReachOrMissing(nearestR) && inReachOrMissing(nearestI)
                            : inReachOrMissing(nearestR);
                    siteTier = CityPlacementPolicy::cityPlacementTier(withinSupply, separation);
                    quality.set("supply_reachable", withinSupply).set("pollution_buffer_tiles", separation == 1000000 ? -1 : separation)
                        .set("nearest_res_origin", nearestR == 1000000 ? -1 : nearestR)
                        .set("nearest_com_origin", nearestC == 1000000 ? -1 : nearestC)
                        .set("nearest_ind_origin", nearestI == 1000000 ? -1 : nearestI);
                    locationScore += CityPlacementPolicy::pollutionSeparationScore(separation);
                    if (newSensitive) {
                        int pollution = 0, value = 0, traffic = 0, sand = 0;
                        for (int x = placeLocationX; x < placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY; y < placeLocationY+newSizeY; ++y) {
                                pollution += citySim->getPollutionDensityMap().worldGet(x, y);
                                value += citySim->getLandValueMap().worldGet(x, y);
                                traffic += citySim->getTrafficDensityMap().worldGet(x, y);
                            }
                        for (int x = placeLocationX-1; x <= placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY-1; y <= placeLocationY+newSizeY; ++y) {
                                if (x >= placeLocationX && x < placeLocationX+newSizeX
                                    && y >= placeLocationY && y < placeLocationY+newSizeY) continue;
                                if (!getMap().tileExists(x,y)) continue;
                                const auto* tile = getMap().getTile(x,y);
                                if (!tile->hasAStructure() && (tile->isSand() || tile->isDunes())) ++sand;
                            }
                        const int meanPollution = pollution/(newSizeX*newSizeY);
                        const int meanTraffic = traffic/(newSizeX*newSizeY);
                        siteTier = CityPlacementPolicy::sensitivePlacementTier(withinSupply, separation,
                            meanPollution, meanTraffic);
                        quality.set("mean_pollution", meanPollution).set("mean_traffic", meanTraffic)
                            .set("mean_land_value", value/(newSizeX*newSizeY)).set("adjacent_sand", sand);
                        locationScore += CityPlacementPolicy::residentialCommercialEnvironmentScore(
                            meanPollution, value/(newSizeX*newSizeY), sand, meanTraffic);
                    }
                }

                const int infill = cityZonePlacement && itemID==Structure_ZoneResidential
                    ? residentialInfillSides(getMap(),houseID,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                quality.set("residential_infill_sides",infill);
                const int lossRisk = dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY), true);
                // Safety outranks pollution/grid preferences; losses decay over five minutes.
                siteTier += lossRisk == 0 ? 6 : 0;
                locationScore -= lossRisk * 5;
                const int rear = (itemID == Structure_NuclearPlant || factoryPlacement) ? rearScore(Coord(placeLocationX, placeLocationY), baseCenter) : 0;
                locationScore += rear;
                const int enemyClearance = factoryPlacement ? TacticalSafetyPolicy::footprintClearance(
                    factoryEnemyClearance,mapW,mapH,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                const auto factoryRank = TacticalSafetyPolicy::factorySiteRank(lossRisk,enemyClearance,siteTier,locationScore);
                const bool clearsReactor = itemID != Structure_NuclearPlant || reactorClearance(itemID,Coord(placeLocationX,placeLocationY));
                const int fireRisk = itemID == Structure_NuclearPlant ? dangerAt(Coord(placeLocationX,placeLocationY),Coord(newSizeX,newSizeY)) : 0;
                const auto reactorRank = TacticalSafetyPolicy::reactorSiteRank(fireRisk,lossRisk,clearsReactor,locationScore);
                quality.set("enemy_clearance_tiles", enemyClearance)
                    .set("recent_loss_risk", lossRisk).set("enemy_fire_risk", fireRisk)
                    .set("rear_score", rear).set("reactor_clearance", clearsReactor);

				// Pick this location if it has the best score
				if (itemID == Structure_NuclearPlant ? reactorRank > bestReactorRank
                    : factoryPlacement ? factoryRank > bestFactoryRank
                    : CityPlacementPolicy::preferCitySite(lossRisk==0,infill,siteTier,locationScore,
                        bestSafe,bestInfill,bestSiteTier,bestLocationScore)) {
                    bestReactorRank = reactorRank;
                    bestFactoryRank = factoryRank;
                    bestSiteTier = siteTier;
                    bestSafe=lossRisk==0; bestInfill=infill;
                    bestQuality = quality.set("tier", siteTier).set("score", locationScore);
					bestLocationScore = locationScore;
					bestLocation = Coord(placeLocationX, placeLocationY);
				}
			}
		}
	}

    if (bestLocation.isInvalid()) {
        bestLocation = findRedevelopmentSite(itemID);
        if (bestLocation.isValid()) bestQuality.set("redevelopment",1);
    }
	placementCache[itemID] = bestLocation;
    bestQuality.set("legal_candidates",candidates).set("threat_rejections",threatRejected)
        .set("blast_rejections",blastRejected).set("recent_loss_rejections",lossRejected)
        .set("ground_access_rejections",accessRejected).set("pollution_rejections",pollutionRejected);
    placementScoreDetails[itemID] = bestQuality;
	return bestLocation;
}

Coord QuantBot::findSlabPlaceLocation(Uint32 itemID) {
	int slabSizeX = getStructureSize(itemID).x;
	int slabSizeY = getStructureSize(itemID).y;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();

	// Check all map tiles for valid slab placement
	for (int x = 0; x <= getMap().getSizeX() - slabSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - slabSizeY; y++) {
			// Check if this location is valid for slab placement
			if (getMap().okayToPlaceStructure(x, y, slabSizeX, slabSizeY, false, getHouse())) {

				int locationScore = 0;
				bool hasExistingSlab = false;

				// Check if any of the slab tiles already have concrete
				for (int i = x; i < x + slabSizeX; i++) {
					for (int j = y; j < y + slabSizeY; j++) {
						if (getMap().getTile(i, j)->hasPreparedFoundation()) {
							hasExistingSlab = true;
							break;
						}
					}
					if (hasExistingSlab) break;
				}

				// Skip if already has concrete - we don't want to place over existing slabs
				if (hasExistingSlab) {
					continue;
				}

			// Count adjacent tiles - favor building next to existing buildings or concrete
			int adjacentStructureTiles = 0;
			int adjacentConcreteTiles = 0;
			int adjacentRockTiles = 0;

			for (int i = x - 1; i <= x + slabSizeX; i++) {
				for (int j = y - 1; j <= y + slabSizeY; j++) {
					if (getMap().tileExists(i, j)) {
						const Tile* pTile = getMap().getTile(i, j);

						// Check if this is directly adjacent (edge-touching, not diagonal)
						bool isDirectlyAdjacent = ((i == x - 1 || i == x + slabSizeX) && j >= y && j < y + slabSizeY) ||
						                          ((j == y - 1 || j == y + slabSizeY) && i >= x && i < x + slabSizeX);

						if (isDirectlyAdjacent) {
							// Count structures that are directly adjacent (highest priority)
							if (pTile->hasAStructure() && pTile->getOwner() == getHouse()->getHouseID()) {
								adjacentStructureTiles++;
							}
							// Count concrete tiles that are directly adjacent (second priority)
							else if (pTile->hasPreparedFoundation()) {
							adjacentConcreteTiles++;
						}
							// Count rock tiles that are directly adjacent (room to expand)
							else if (pTile->isRock() && !pTile->isMountain()) {
							adjacentRockTiles++;
							}
						}
					}
				}
			}

		// SCORING: Favor building next to existing buildings or concrete
		// 1. Highest priority: directly adjacent to our structures
		locationScore += adjacentStructureTiles * 10;

		// 2. Second priority: directly adjacent to existing concrete
		locationScore += adjacentConcreteTiles * 5;

		// 3. Bonus for adjacent rock (room to expand)
		locationScore += adjacentRockTiles * 2;


				// Pick this location if it has the best score
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}

Coord QuantBot::findTurretPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	// Use squad rally location (enemy direction) as approximation of threat
	Coord enemyDirection = squadRallyLocation.isValid() ? squadRallyLocation : Coord::Invalid();

	// If no squad rally, find closest enemy structure
	if (!enemyDirection.isValid()) {
		FixPoint closestEnemyDistance = FixPt_MAX;
		for (const StructureBase* pStructure : getStructureList()) {
			if (pStructure && pStructure->getOwner() && pStructure->getOwner()->getTeamID() != getHouse()->getTeamID()) {
				FixPoint distance = blockDistance(baseCenter, pStructure->getLocation());
				if (distance < closestEnemyDistance) {
					closestEnemyDistance = distance;
					enemyDirection = pStructure->getLocation();
				}
			}
		}
	}

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(x, y, newSizeX, newSizeY, false,
				(itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {

                if (overlapsReservedStructure(x, y, newSizeX, newSizeY)
                    || !preservesGroundAccess(itemID,Coord(x,y))) continue;
                const auto roads = cityRoadImpact(getMap(), x, y, newSizeX, newSizeY, itemID);
                if (!roads.preservesConnections) continue;
				FixPoint score = 0;
                score += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				Coord candidatePos(x, y);

				// 1. Favor being CLOSE to base center (integrated into base, not perimeter)
				FixPoint distanceFromBase = blockDistance(candidatePos, baseCenter);
				score -= distanceFromBase * 2; // Penalty for being far from center

				// 2. Strong bonus for adjacency to own buildings
				int adjacentOwnBuildings = 0;
				for (int dx = -1; dx <= newSizeX; dx++) {
					for (int dy = -1; dy <= newSizeY; dy++) {
						// Check tiles around the structure
						if ((dx == -1 || dx == newSizeX || dy == -1 || dy == newSizeY) && 
							getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (pTile->hasAStructure()) {
								const StructureBase* pStructure = dynamic_cast<const StructureBase*>(pTile->getObject());
								if (pStructure && pStructure->getOwner() == getHouse()) {
									adjacentOwnBuildings++;
								}
							}
						}
					}
				}
				score += adjacentOwnBuildings * 15; // Strong bonus for being next to own buildings

				// 3. Favor the side of the base closest to the enemy
				// We want turrets between our base and the enemy
				if (enemyDirection.isValid() && baseCenter.isValid()) {
					// Calculate vector from base to enemy
					int baseToEnemyX = enemyDirection.x - baseCenter.x;
					int baseToEnemyY = enemyDirection.y - baseCenter.y;

					// Calculate vector from base to candidate position
					int baseToCandidateX = candidatePos.x - baseCenter.x;
					int baseToCandidateY = candidatePos.y - baseCenter.y;

					// Dot product: positive if candidate is on the enemy side of base
					int dotProduct = baseToEnemyX * baseToCandidateX + baseToEnemyY * baseToCandidateY;
					if (dotProduct > 0) {
						score += dotProduct / 10; // Bonus for being on enemy-facing side
					}
				}

				// 4. Slight preference for sand over rock (buildable terrain)
				int sandTiles = 0;
				for (int dx = 0; dx < newSizeX; dx++) {
					for (int dy = 0; dy < newSizeY; dy++) {
						if (getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (!pTile->isRock()) {
								sandTiles++;
							}
						}
					}
				}
				score += sandTiles * 2; // Minor bonus for sand

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}

bool QuantBot::selectCityServiceInvestment(const BuilderBase* builder, int money, bool emergency,
                                         Uint32& selectedItem, Coord& selectedSite, bool landValueOnly, Uint32 requiredItem) {
    AITelemetry::PerformanceScope perfScope("ai.selectCityServiceInvestment", getGameCycleCount(), getHouse()->getHouseID());
    using CityServiceInvestmentPolicy::Value;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || !sim->isInitialized()) return false;
    const int house = getHouse()->getHouseID(), w = getMap().getSizeX(), h = getMap().getSizeY();
    const auto& prices = currentGame->objectData.data;
    unsigned available = 0;
    const std::array<Uint32,2> serviceItems{Structure_PoliceStation,Structure_RocketTurret};
    for (unsigned i=0;i<serviceItems.size();++i)
        if ((requiredItem == NONE_ID || requiredItem == serviceItems[i])
            && builder->isAvailableToBuild(serviceItems[i]) && money > prices[serviceItems[i]][house].price)
            available |= 1u << i;
    if (!available || (landValueOnly && !(available & 2))) return false;
    const unsigned mode = landValueOnly ? 2 : emergency ? 1 : 0;
    // A yard with its own reservation excludes that plan from marginal gains.
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto selectResult = [&](const CityServiceResults& results) {
        const CityServiceSite* best = nullptr;
        for (unsigned i=0;i<serviceItems.size();++i) {
            const auto& candidate = results[mode][i];
            if (!(available & (1u<<i)) || candidate.site.isInvalid()) continue;
            if (!best || candidate.value.betterThan(best->value)) {
                best = &candidate;
                selectedItem = serviceItems[i];
            }
        }
        if (!best) return false;
        selectedSite = best->site;
        const auto& value = best->value;
        traceDecision("city_service_investment", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",selectedItem).set("x",selectedSite.x).set("y",selectedSite.y).set("emergency",emergency)
            .set("crime_reduction",value.crime).set("annual_tax_gain",value.tax)
            .set("crime_utility",value.crimeUtility).set("dangerous_relief",value.dangerousRelief)
            .set("estimated_growth_tax",value.growthTax).set("threat_defense_value",value.defense)
            .set("build_cost",value.buildCost).set("annual_upkeep",value.upkeep)
            .set("power_cost",value.powerCost).set("placement_overlap_penalty",value.overlapPenalty)
            .set("horizon_years",1));
        return true;
    };
    if (const auto* cached = cityServiceSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.cache_hit",1,-1,false);
        return selectResult(*cached);
    }
    if (!cityServiceSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.search_deferred",1,-1,false);
        return false;
    }
    auto& results = cityServiceSearch.result();
    const CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),house);

    auto plannedTerrain = sim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }
    const auto& state = sim->getHouseState(house);
    const auto& data = currentGame->objectData.data;
    const bool powered = getHouse()->getProducedPower() >= getHouse()->getPowerRequirement();
    struct Property {
        Coord p;
        int item, value, crime, baseCrime, coverage, population, nextPopulation, demand, pollution, threat;
    };
    std::vector<Property> properties;
    std::vector<const UnitBase*> threats;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->getHealth() > 0 && unit->isVisible(getHouse()->getTeamID())
            && data[unit->getItemID()][unit->getOwner()->getHouseID()].weapondamage > 0)
            threats.push_back(unit);
    }
    std::vector<Coord> turretSites, stationSites;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        if (structure->getItemID()==Structure_RocketTurret) turretSites.push_back(structure->getLocation());
        if (structure->getItemID()==Structure_PoliceStation && structure->getHealth()>0)
            stationSites.push_back(structure->getLocation());
    }
    for (const auto& entry:reservedStructures)
        if (entry.first!=planningBuilder && entry.second.item==Structure_PoliceStation)
            stationSites.push_back(entry.second.location);
    DuneCity::CityMapLayer<int32_t> plannedPolice;
    plannedPolice.init(w,h,DuneCity::kPoliceMapBlockSize);
    auto addSource = [&](int item,Coord p,Coord size,int funding,bool hasPower) {
        const int strength=DuneCity::getPoliceCoverage(item);
        if (strength<=0) return;
        const auto source=DuneCity::policeSource(getMap(),p.x,p.y,size.x,size.y,strength,funding,hasPower);
        DuneCity::addPoliceCoverage(plannedPolice,w,h,source.x,source.y,source.strength);
    };
    for (const auto* structure:getStructureList()) {
        const auto* owner=structure->getOwner();
        if (!owner || structure->getHealth()<=0) continue;
        addSource(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
            sim->getHouseState(owner->getHouseID()).policeFundingPercent,
            owner->getProducedPower()>=owner->getPowerRequirement());
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder) continue;
        const auto& plan=entry.second;
        addSource(plan.item,plan.location,getStructureSize(plan.item),state.policeFundingPercent,powered);
    }
    DuneCity::smoothPoliceCoverage(plannedPolice,w,h);
    int totalPopulation = 0, sampleCount = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Coord p = structure->getLocation();
        if (!getMap().tileExists(p.x, p.y)) continue;
        const int item = structure->getItemID();
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        const int level = zone ? getMap().getTile(p.x,p.y)->getCityZoneDensity() : structure->getCityOccupancy();
        const int pop = DuneCity::getStructurePopulation(structure, level);
        totalPopulation += pop;
        const int value = sim->getLandValueMap().worldGet(p.x,p.y);
        if (value > 0) ++sampleCount;
        int coverage = plannedPolice.worldGet(p.x,p.y);
        const int landBlockSize = sim->getLandValueMap().getBlockSize();
        int plannedValue = plannedTerrain.landValueContribution(p.x,p.y,landBlockSize)
            - sim->getParkTerrain().landValueContribution(p.x,p.y,landBlockSize);
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const auto& plan = entry.second;
            if (!DuneCity::usesParkTerrain(plan.item))
                plannedValue += CityServiceInvestmentPolicy::parkContribution(plan.item,
                    plan.location.x,plan.location.y,p.x,p.y,landBlockSize,plannedTerrain);
        }
        const int base = sim->getCrimeBeforePoliceMap().worldGet(p.x,p.y);
        const int crime = std::clamp(base - coverage, 0, 250);
        int threat = 0;
        if (RocketTurretPolicy::defenseWeight(item)) {
            for (const auto* unit : threats) {
                const Coord u = unit->getLocation();
                const int distance = std::max(std::abs(p.x-u.x),std::abs(p.y-u.y));
                if (distance <= 12) threat += data[unit->getItemID()][unit->getOwner()->getHouseID()].price * (13-distance)/13;
            }
            threat = std::min(threat, data[item][house].price) * RocketTurretPolicy::defenseWeight(item) / 4;
            for (const Coord t : turretSites) {
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
            for (const auto& entry : reservedStructures) {
                if (entry.first == planningBuilder || entry.second.item != Structure_RocketTurret) continue;
                const Coord t = entry.second.location;
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
        }
        const int demand = item == Structure_ZoneResidential ? state.resValve
            : item == Structure_ZoneCommercial ? state.comValve : item == Structure_ZoneIndustrial ? state.indValve : 0;
        const int nextPop = zone && item == Structure_ZoneResidential
            ? DuneCity::ResidentialPopulation::grow(pop,sim->getPopulationDensityMap().worldGet(p.x,p.y))
            : zone && level < DuneCity::getStructureMaxLevel(item) ? DuneCity::getZonePopulation(item,level+1) : pop;
        properties.push_back({p,item,std::min(250,value+plannedValue),crime,base,coverage,pop,nextPop,demand,
            sim->getPollutionDensityMap().worldGet(p.x,p.y),threat});
    }
    AITelemetry::log().performance(getGameCycleCount(),house,"service.properties",properties.size(),-1,false);
    LocalPointIndex propertyIndex(w,h);
    for (size_t i=0;i<properties.size();++i) propertyIndex.add(properties[i].p.x,properties[i].p.y,i);
    for (unsigned itemIndex=0; itemIndex<serviceItems.size(); ++itemIndex) {
        const Uint32 item = serviceItems[itemIndex];
        // Search both service types once, independent of the first yard's
        // upgrades/reserve. Each caller applies its own affordability and tech
        // checks when selecting. Otherwise an early low-tech yard can starve
        // every later upgraded yard's rocket search.
        if (!data[item][house].enabled || data[item][house].techLevel > currentGame->techLevel
            || getHouse()->getCredits() <= data[item][house].price) continue;
        Value itemBest;
        Coord itemSite = Coord::Invalid();
        const Coord size = getStructureSize(item);
        std::vector<bool> candidates(w*h, false);
        for (const auto& p : properties) {
            if (p.crime < 60 && (item != Structure_RocketTurret || (p.value >= 250 && p.threat == 0))) continue;
            for (int y=std::max(scan.begin/w,p.p.y-23); y<=std::min({h-size.y,p.p.y+23,(scan.end-1)/w}); ++y)
                for (int x=std::max(0,p.p.x-23); x<=std::min(w-size.x,p.p.x+23); ++x) candidates[y*w+x]=true;
        }
        AITelemetry::PerformanceScope itemScope("ai.service_site_search",getGameCycleCount(),house,item);
        int scoredSites = 0;
        for (int cell=scan.begin;cell<scan.end;++cell) {
            const int x=cell%w, y=cell/w;
            if (x>w-size.x || y>h-size.y) continue;
            if (!candidates[y*w+x] || overlapsReservedStructure(x,y,size.x,size.y)
                || !getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,item)
                || !preservesGroundAccess(item,Coord(x,y))) continue;
            const auto road = cityRoadImpact(getMap(),x,y,size.x,size.y,item);
            if (!road.preservesConnections || (item == Structure_RocketTurret && road.junctionBonus <= 0)
                || wouldLandlockNeighbouringZone(getMap(),house,x,y,size.x,size.y)) continue;
            ++scoredSites;
            Value value;
            value.buildCost = data[item][house].price;
            // Placement-only cost discourages another station beside one already
            // built/planned. It does not change stacking in the simulation.
            if (item == Structure_PoliceStation) {
                for (const Coord p : stationSites)
                    value.overlapPenalty += CityServiceInvestmentPolicy::stationOverlapCost(value.buildCost,
                        std::max(std::abs(x-p.x),std::abs(y-p.y)));
            }
            value.upkeep = (DuneCity::getPoliceAnnualCost(item)*state.policeFundingPercent/100).lround();
            const int power = std::max(0,data[item][house].power);
            const int spare = std::max(0,getHouse()->getProducedPower()-getHouse()->getPowerRequirement());
            // Power upkeep (50-second budget year / 15-second power bill) plus
            // the share of a windtrap needed when spare generation is insufficient.
            value.powerCost = power*50/(32*15) + std::max(0,power-spare)*data[Structure_WindTrap][house].price
                / std::max(1,-data[Structure_WindTrap][house].power);
            const auto source = DuneCity::policeSource(getMap(),x,y,size.x,size.y,
                DuneCity::getPoliceCoverage(item),state.policeFundingPercent,powered);
            int valueGainSum = 0, neighbourhoodGainSum = 0, growthTax = 0;
            propertyIndex.visit(x,y,23,[&](size_t propertyID) {
                const auto& p=properties[propertyID];
                const int distance = std::max(std::abs(x-p.p.x),std::abs(y-p.p.y));
                const int added = DuneCity::policeCoverageAt(source.x,source.y,p.p.x,p.p.y,2,source.strength,w,h);
                const int reduction = DuneCity::marginalCrimeReduction(p.baseCrime,p.coverage,added);
                if (p.population > 0) {
                    value.crime += reduction;
                    value.crimeUtility += CityServiceInvestmentPolicy::underservedUtility(
                        CityServiceInvestmentPolicy::crimeHarm(p.crime,p.item,p.population)
                        - CityServiceInvestmentPolicy::crimeHarm(p.crime-reduction,p.item,p.population),p.coverage);
                    value.dangerousRelief += std::max(0,p.crime-191) - std::max(0,p.crime-reduction-191);
                }
                const int park = CityServiceInvestmentPolicy::parkContribution(item,x,y,p.p.x,p.p.y,
                    sim->getLandValueMap().getBlockSize(),plannedTerrain);
                const int restored = p.crime > 190 && p.crime-reduction <= 190 ? 20 : 0;
                const int gain = std::min(250-p.value,park+restored);
                if (p.value > 0) valueGainSum += gain;
                const auto role = DuneCity::getStructureCityRole(p.item);
                if (p.value > 0 && (role == DuneCity::CityRole::Residential || role == DuneCity::CityRole::Commercial))
                    neighbourhoodGainSum += gain;
                // Conservative forecast: at most a quarter of one demanded growth
                // level, only where improved value and acceptable pollution allow it.
                if (powered && p.demand > 0 && p.population > 0 && p.nextPopulation > p.population
                    && (p.item == Structure_ZoneResidential || p.item == Structure_ZoneCommercial)
                    && p.pollution < 128 && gain > 0)
                    growthTax += DuneCity::computeAnnualTaxRevenue(p.nextPopulation-p.population,
                        sim->getCityTax(),std::max(1,state.avgLandValue)) * std::min(gain,64) / (4*64);
                if (powered && item == Structure_RocketTurret && distance <= data[item][house].weaponrange)
                    value.defense += p.threat;
            });
            value.tax = CityServiceInvestmentPolicy::annualTaxGain(totalPopulation,sim->getCityTax(),valueGainSum,sampleCount);
            value.growthTax = growthTax;
            if (item == Structure_RocketTurret) {
                if (value.crime <= 0) continue; // Civic turrets must reduce actual crime.
                value.neighbourhoodTax = CityServiceInvestmentPolicy::annualTaxGain(
                    totalPopulation,sim->getCityTax(),neighbourhoodGainSum,sampleCount);
            }
            for (unsigned resultMode=0;resultMode<results.size();++resultMode) {
                if (!value.useful(resultMode == 1)) continue;
                if (resultMode == 2 && (item != Structure_RocketTurret || !value.landValueTurretEligible())) continue;
                auto& result = results[resultMode][itemIndex];
                if (result.site.isInvalid() || value.betterThan(result.value)) result = {Coord(x,y),value};
            }
            itemSite = results[mode][itemIndex].site;
            itemBest = results[mode][itemIndex].value;
        }
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scored_sites",scoredSites,item,false);
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scanned_tiles",scan.end-scan.begin,item,false);
        traceDecision("city_service_candidate", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",item).set("eligible",itemSite.isValid()).set("x",itemSite.x).set("y",itemSite.y)
            .set("crime_reduction",itemBest.crime).set("annual_tax_gain",itemBest.tax)
            .set("crime_utility",itemBest.crimeUtility).set("dangerous_relief",itemBest.dangerousRelief)
            .set("res_com_tax_gain",itemBest.neighbourhoodTax)
            .set("estimated_growth_tax",itemBest.growthTax).set("threat_defense_value",itemBest.defense)
            .set("build_cost",itemBest.buildCost).set("annual_upkeep",itemBest.upkeep)
            .set("power_cost",itemBest.powerCost).set("placement_overlap_penalty",itemBest.overlapPenalty)
            .set("emergency",emergency));
    }
    return selectResult(results);
}

Coord QuantBot::findCityTurretPlaceLocation(Uint32 itemID, int* defenseScore, int* amenityScore,
                                            int* crimeBenefit, int* crimeHotspot) {
    AITelemetry::PerformanceScope perfScope("ai.findCityTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    if (defenseScore) *defenseScore = 0;
    if (amenityScore) *amenityScore = 0;
    if (crimeBenefit) *crimeBenefit = 0;
    if (crimeHotspot) *crimeHotspot = 0;
    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (!citySim || itemID != Structure_RocketTurret) return Coord::Invalid();
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto resultSite = [&](const CityTurretResult& result) {
        if (defenseScore) *defenseScore = result.defense;
        if (amenityScore) *amenityScore = result.amenity;
        if (crimeBenefit) *crimeBenefit = result.crime;
        if (crimeHotspot) *crimeHotspot = result.hotspot;
        return result.site;
    };
    if (const auto* cached = cityTurretSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.cache_hit",1,itemID,false);
        return resultSite(*cached);
    }
    if (!cityTurretSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.search_deferred",1,itemID,false);
        return Coord::Invalid();
    }
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    const CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),getHouse()->getHouseID(),
        key == NONE_ID ? 1 : cityReadyYardCount);
    auto plannedTerrain = citySim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }


    // Existing and planned turrets count as coverage, so additional yards do
    // not buy the same protection/amenity repeatedly.
    std::vector<Coord> turrets;
    struct Target { Coord position; int defense; int value; bool covered; };
    std::vector<Target> targets;
    const int defenseRadius = std::max(1,
        currentGame->objectData.data[itemID][getHouse()->getHouseID()].weaponrange - 1);
    auto addTarget = [&](Uint32 item, Coord pos, Coord size) {
        const int weight = RocketTurretPolicy::defenseWeight(item);
        const bool amenity = item == Structure_ZoneResidential || item == Structure_ZoneCommercial;
        if (!weight && !amenity) return;
        // Use the same structure origin that receives city land-value scans.
        const Coord point = weight ? Coord(pos.x + size.x/2, pos.y + size.y/2) : pos;
        targets.push_back({point, weight, citySim->getLandValueMap().worldGet(point.x, point.y), false});
    };
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (structure->getItemID() == Structure_RocketTurret) turrets.push_back(structure->getLocation());
        else addTarget(structure->getItemID(), structure->getLocation(), structure->getStructureSize());
    }
    for (const auto& entry : reservedStructures) {
        const auto& plan = entry.second;
        if (plan.item == Structure_RocketTurret) {
            if (entry.first != planningBuilder) turrets.push_back(plan.location);
        }
        else addTarget(plan.item, plan.location, getStructureSize(plan.item));
    }
    for (auto& target : targets) {
        int coverage = 0;
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (const auto& turret : turrets) {
            if (target.defense
                ? std::max(std::abs(turret.x-target.position.x), std::abs(turret.y-target.position.y)) <= radius
                : plannedTerrain.marginalGain(turret.x,turret.y,DuneCity::kParkLandValueBonus,
                    target.position.x,target.position.y)>0) {
                if (++coverage >= (target.defense == 2 ? 2 : 1)) { target.covered = true; break; }
            }
        }
    }
    std::vector<bool> candidates(w*h, false);
    for (const auto& target : targets) {
        if (target.covered || (!target.defense && target.value >= DuneCity::kMaxLandValue)) continue;
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (int y = std::max(scan.begin/w, target.position.y-radius); y <= std::min({h-1, target.position.y+radius,(scan.end-1)/w}); ++y)
            for (int x = std::max(0, target.position.x-radius); x <= std::min(w-1, target.position.x+radius); ++x)
                candidates[y*w+x] = true;
    }
    // City crime is a third legitimate turret role. It is deliberately
    // secondary to critical asset defence, but lets the 15% service effect
    // fill road intersections in districts that police have not covered.
    struct CrimeTarget { Coord position; int crime; int plannedCoverage = 0; };
    std::vector<CrimeTarget> crimeTargets;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (DuneCity::getStructureCityRole(structure->getItemID()) == DuneCity::CityRole::None) continue;
        const Coord p = structure->getLocation();
        const int crime = citySim->getCrimeRateMap().worldGet(p.x, p.y);
        if (crime >= 80) {
            crimeTargets.push_back({p, crime});
            for (int y = std::max(scan.begin/w, p.y-DuneCity::kPoliceRadius); y <= std::min({h-1, p.y+DuneCity::kPoliceRadius,(scan.end-1)/w}); ++y)
                for (int x = std::max(0, p.x-DuneCity::kPoliceRadius); x <= std::min(w-1, p.x+DuneCity::kPoliceRadius); ++x)
                    candidates[y*w+x] = true;
        }
    }
    auto plannedCoverageAt = [&](Coord point) {
        int coverage = 0;
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const int strength = DuneCity::getPoliceCoverage(entry.second.item);
            if (strength <= 0) continue;
            const Coord size = getStructureSize(entry.second.item);
            const auto source = DuneCity::policeSource(getMap(), entry.second.location.x,
                entry.second.location.y, size.x, size.y, strength,
                citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
                getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
            coverage += DuneCity::policeCoverageAt(source.x,source.y,point.x,point.y,2,
                source.strength,w,h);
        }
        return coverage;
    };
    for (auto& target : crimeTargets) target.plannedCoverage = plannedCoverageAt(target.position);
    Coord best = Coord::Invalid();
    RocketTurretPolicy::Score bestScore;
    int bestCrimeBenefit = 0, bestCrimeHotspot = 0;
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.scanned_tiles",scan.end-scan.begin,itemID,false);
    for (int cell=scan.begin;cell<scan.end;++cell) {
        const int x=cell%w, y=cell/w;
        if (!candidates[y*w+x] || overlapsReservedStructure(x, y, 1, 1)) continue;
        if (!getMap().okayToPlaceStructure(x, y, 1, 1, false, getHouse(), false, itemID)) continue;
        if (!preservesGroundAccess(itemID,Coord(x,y))) continue;
        const auto roads = cityRoadImpact(getMap(), x, y, 1, 1, itemID);
        if (!roads.preservesConnections
            || wouldLandlockNeighbouringZone(getMap(), getHouse()->getHouseID(), x, y, 1, 1)) continue;
        RocketTurretPolicy::Score score;
        score.junction = roads.junctionBonus;
        int candidateCrimeBenefit = 0, candidateCrimeHotspot = 0;
        for (const auto& target : targets) {
            if (target.covered) continue;
            const int distance = std::max(std::abs(x-target.position.x), std::abs(y-target.position.y));
            if (target.defense && distance <= defenseRadius) {
                score.defense += target.defense;
                score.proximity += target.defense * (defenseRadius-distance);
            } else if (!target.defense) {
                score.amenity += RocketTurretPolicy::amenityBenefit(target.value, false,
                    plannedTerrain.marginalGain(x,y,DuneCity::getParkLandValueBonus(itemID),
                        target.position.x,target.position.y));
            }
        }
        const auto source = DuneCity::policeSource(getMap(), x, y, 1, 1, DuneCity::getPoliceCoverage(itemID),
            citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
            getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
        for (const auto& target : crimeTargets) {
            const int added = DuneCity::policeCoverageAt(source.x,source.y,target.position.x,target.position.y,
                2,source.strength,w,h);
            const int reduction = DuneCity::marginalCrimeReduction(
                citySim->getCrimeBeforePoliceMap().worldGet(target.position.x, target.position.y),
                citySim->getPoliceCoverageMap().worldGet(target.position.x, target.position.y)
                    + target.plannedCoverage, added);
            candidateCrimeBenefit += reduction;
            if (reduction > 0) candidateCrimeHotspot = std::max(candidateCrimeHotspot, target.crime);
        }
        // Amenity normally ranges in the tens. A turret must reduce crime in
        // several blocks before it competes with a demanded tax-base zone.
        score.amenity += candidateCrimeBenefit / 8;
        if (score.useful() && (!best.isValid() || score.betterThan(bestScore))) {
            best = Coord(x, y); bestScore = score;
            bestCrimeBenefit = candidateCrimeBenefit;
            bestCrimeHotspot = candidateCrimeHotspot;
        }
    }
    if (defenseScore) *defenseScore = bestScore.defense;
    if (amenityScore) *amenityScore = bestScore.amenity;
    if (crimeBenefit) *crimeBenefit = bestCrimeBenefit;
    if (crimeHotspot) *crimeHotspot = bestCrimeHotspot;
    cityTurretSearch.result() = {best,bestScore.defense,bestScore.amenity,bestCrimeBenefit,bestCrimeHotspot};
    return best;
}

Coord QuantBot::findEffectiveTurretPlaceLocation(Uint32 itemID) {
    if (currentGame && currentGame->isCitySimEnabled() && itemID == Structure_RocketTurret)
        return findCityTurretPlaceLocation(itemID);
    return findTurretPlaceLocation(itemID);
}

Coord QuantBot::findPlaceLocationSimple(Uint32 itemID) {
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(x, y, newSizeX, newSizeY, false,
				(itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {
                if (!preservesGroundAccess(itemID,Coord(x,y))) continue;

				FixPoint score = 0;

				// Base scoring - favor being close to existing buildings
				FixPoint closestOwnBuildingDistance = FixPt_MAX;
				for (const StructureBase* pStructure : getStructureList()) {
					if (pStructure->getOwner() == getHouse()) {
						FixPoint distance = blockDistance(Coord(x, y), Coord(pStructure->getX(), pStructure->getY()));
						if (distance < closestOwnBuildingDistance) {
							closestOwnBuildingDistance = distance;
						}
					}
				}
				if (closestOwnBuildingDistance < FixPt_MAX) {
					score += 50 - closestOwnBuildingDistance; // Bonus for being close to our buildings
				}

				// Building-specific placement preferences
				if (itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
					// Turrets prefer map edges for defensive positioning
					int distanceToEdge = std::min({x, y, getMap().getSizeX() - 1 - x, getMap().getSizeY() - 1 - y});
					score += (10 - distanceToEdge) * 5; // Higher score for being closer to edges

					// Rocket turrets also prefer being close to squad rally point
					if (itemID == Structure_RocketTurret) {
						FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
						score += 30 - distanceToRally * 2; // Bonus for being close to rally point
					}
				}
				else if (itemID == Structure_Refinery) {
					// Refineries prefer being close to spice deposits
					FixPoint closestSpiceDistance = FixPt_MAX;
					for (int spiceX = 0; spiceX < getMap().getSizeX(); spiceX++) {
						for (int spiceY = 0; spiceY < getMap().getSizeY(); spiceY++) {
							if (getMap().tileExists(spiceX, spiceY) && getMap().getTile(spiceX, spiceY)->hasSpice()) {
								FixPoint spiceDistance = blockDistance(Coord(x, y), Coord(spiceX, spiceY));
								if (spiceDistance < closestSpiceDistance) {
									closestSpiceDistance = spiceDistance;
								}
							}
						}
					}
					if (closestSpiceDistance < FixPt_MAX) {
						score += 50 - closestSpiceDistance * 2; // Higher bonus for being closer to spice
					}
				}
				else if (itemID == Structure_HeavyFactory || itemID == Structure_LightFactory || 
						 itemID == Structure_WOR || itemID == Structure_Barracks || itemID == Structure_StarPort) {
					// Production buildings prefer being close to rally point and base center
					FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
					FixPoint distanceToBase = blockDistance(findBaseCentre(getHouse()->getHouseID()), Coord(x, y));
					score += 20 - distanceToRally / 2; // Bonus for being close to rally point
					score += 20 - distanceToBase; // Bonus for being close to base center
				}

				// Favor map edges in general for defensive positioning
				if (x == 0 || x == getMap().getSizeX() - newSizeX || y == 0 || y == getMap().getSizeY() - newSizeY) {
					score += 10;
				}

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}


void QuantBot::build(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.build", getGameCycleCount(), getHouse()->getHouseID());
    refreshTacticalDanger();
    planningBuilder = NONE_ID;
    recentStructureLosses.erase(std::remove_if(recentStructureLosses.begin(), recentStructureLosses.end(),
        [&](const auto& loss) { return getGameCycleCount() - loss.cycle >= MILLI2CYCLES(900000); }), recentStructureLosses.end());
    clearPlacementCache();
    cityServiceSearch.reset();
    cityTurretSearch.reset();
    for (auto it = reservedStructures.begin(); it != reservedStructures.end();) {
        const auto* builder = dynamic_cast<const BuilderBase*>(currentGame->getObjectManager().getObject(it->first));
        if (!builder || builder->getOwner() != getHouse() || builder->getProductionQueueSize() == 0)
            it = reservedStructures.erase(it);
        else ++it;
    }

	int houseID = getHouse()->getHouseID();
	auto& data = currentGame->objectData.data;

	int itemCount[Num_ItemID];
	for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
		itemCount[i] = getHouse()->getNumItems(i);
	}

	int activeHeavyFactoryCount = 0;
    int activeLightFactoryCount = 0;
	int activeHighTechFactoryCount = 0;
    int ornithopterFactoryCount = 0;
	int activeRepairYardCount = 0;
    int queuedProductionCost = 0;
    int queuedMilitaryValue = 0;
    int mcvUpgradesInProgress = 0;

	// Let's try just running this once...
	if (squadRallyLocation.isInvalid()) {
		squadRallyLocation = findSquadRallyLocation();
		squadRetreatLocation = findSquadRetreatLocation();
		if (gameMode != GameMode::Campaign) {
			retreatAllUnits();
		}
	}

	// Next add in the objects we are building
	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() == getHouse()) {
			if (pStructure->isABuilder()) {
				const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->isUpgrading()
                    && !pBuilder->isAvailableToBuild(Unit_MCV)) ++mcvUpgradesInProgress;
				if (pBuilder->getProductionQueueSize() > 0) {
                    int builderQueuedCost = 0;
					for (const auto& queued : pBuilder->getBuildList()) {
						itemCount[queued.itemID] += queued.num;
                        builderQueuedCost += queued.num * queued.price;
                        if (QuantBotBuildPolicy::militaryItem(queued.itemID))
                            queuedMilitaryValue += queued.num * data[queued.itemID][houseID].price;
					}
					queuedProductionCost += std::max(0, builderQueuedCost - pBuilder->getProductionProgress().lround());
                    if (pBuilder->getItemID() == Structure_HeavyFactory) {
						activeHeavyFactoryCount++;
					}
					else if (pBuilder->getItemID() == Structure_LightFactory) {
                        ++activeLightFactoryCount;
                    }
                    else if (pBuilder->getItemID() == Structure_HighTechFactory) {
						activeHighTechFactoryCount++;
                        if (pBuilder->getCurrentProducedItem() == Unit_Ornithopter
                            && !pBuilder->isOnHold() && !pBuilder->isUpgrading()) ++ornithopterFactoryCount;
					}
				}
			}
			else if (pStructure->getItemID() == Structure_RepairYard) {
				const RepairYard* pRepairYard = static_cast<const RepairYard*>(pStructure);
				if (!pRepairYard->isFree()) {
					activeRepairYardCount++;
				}

			}

			// Unit deployment position - disabled, just deploy units normally
			// Production buildings will deploy units at their default position
		}


	}

	int money = getHouse()->getCredits();
    militaryValue += queuedMilitaryValue;
	const bool citySimEnabled = currentGame && currentGame->isCitySimEnabled();
    const bool powerRules = getHouse()->isPowerRequired();
    const bool turretPowerRequired = getGameInitSettings().getGameOptions().rocketTurretsNeedPower;
    const bool vanillaEconomy = !citySimEnabled && !powerRules;
    // Buildings pay gradually. Previously the same unspent cash funded more
    // orders every pass even though it was already committed to existing queues.
    if (vanillaEconomy) money = std::max(0, money - queuedProductionCost);
    const int economyReserve = vanillaEconomy ? std::max(2000,
        data[Structure_Refinery][houseID].price + 2 * data[Unit_Harvester][houseID].price) : 0;

	// Per-house city stats — CitySimulation now tracks these per player.
	int ownResPop = 0, ownComPop = 0, ownIndPop = 0, ownTotalPop = 0;
	int ownAvgLandValue = 0;
	int16_t ownResValve = 0, ownComValve = 0, ownIndValve = 0;
	bool ownHasStadium = false, ownHasAirport = false;
	if (citySimEnabled) {
		auto* citySim = currentGame->getCitySimulation();
		if (citySim) {
			const auto& hs = citySim->getHouseState(getHouse()->getHouseID());
			ownResPop = hs.resPop;
			ownComPop = hs.comPop;
			ownIndPop = hs.indPop;
			ownTotalPop = hs.getTotalPop();
			ownAvgLandValue = hs.avgLandValue;
			ownResValve = hs.resValve;
			ownComValve = hs.comValve;
			ownIndValve = hs.indValve;
			ownHasStadium = hs.hasStadium;
			ownHasAirport = hs.hasAirport;
		}
	}

	// Strategic structures must eventually outrank repeatable choices such as
	// factories, zones, and reactive turrets. These timers stay runtime-only so
	// loading an older save starts a fresh bounded wait without changing save data.
	const Uint32 currentBuildCycle = getGameCycleCount();
    // Forecast two minutes from the last thirty seconds of actual demand.
    // Sample on simulation cycles and save it so peers/reloads agree. Falling
    // or flat demand removes the measured trend; latent zone load is reserved below.
    if (citySimEnabled) {
        const int required = getHouse()->getPowerRequirement();
        if (powerDemandSampleCycle == 0) {
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        } else if (currentBuildCycle-powerDemandSampleCycle >= MILLI2CYCLES(30000)) {
            projectedPowerDemandGrowth = QuantBotBuildPolicy::projectedPowerGrowth(
                powerDemandSample,required,currentBuildCycle-powerDemandSampleCycle,MILLI2CYCLES(120000));
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        }
    }
	const Uint32 noEligibilityCycle = std::numeric_limits<Uint32>::max();
	bool anyConstructionYardCanBuildIX = false;
	bool anyConstructionYardCanBuildPalace = false;
	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() != getHouse()
			|| pStructure->getItemID() != Structure_ConstructionYard
			|| !pStructure->isABuilder()) {
			continue;
		}

		const BuilderBase* pConstructionYard = static_cast<const BuilderBase*>(pStructure);
		anyConstructionYardCanBuildIX |= pConstructionYard->isAvailableToBuild(Structure_IX);
		anyConstructionYardCanBuildPalace |= pConstructionYard->isAvailableToBuild(Structure_Palace);
	}

	const bool customStrategicPlanning = gameMode == GameMode::Custom && !supportMode;
	const bool stablePower = getHouse()->hasPower();
	const int palaceTarget = QuantBotBuildPolicy::palaceTarget(
		getGameInitSettings().getGameOptions().onlyOnePalace, citySimEnabled,
		ownTotalPop * DuneCity::CitySimulation::kPopDisplayMultiplier);
	const bool palaceAllowedNow = itemCount[Structure_Palace] < palaceTarget;
	const bool ixEligible = customStrategicPlanning
		&& itemCount[Structure_IX] == 0
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_HighTechFactory] > 0
		&& itemCount[Structure_RepairYard] > 0
		&& anyConstructionYardCanBuildIX;
	const bool palaceEligible = customStrategicPlanning
		&& palaceAllowedNow
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_LightFactory] > 0
		&& anyConstructionYardCanBuildPalace;

	auto updateEligibilityTimer = [&](bool eligible, Uint32& eligibleSinceCycle) {
		if (!eligible) {
			eligibleSinceCycle = noEligibilityCycle;
		} else if (eligibleSinceCycle == noEligibilityCycle) {
			eligibleSinceCycle = currentBuildCycle;
		}
	};
	updateEligibilityTimer(ixEligible, ixEligibleSinceCycle);
	updateEligibilityTimer(palaceEligible, palaceEligibleSinceCycle);

	Uint32 ixWaitMs = 75000;
	Uint32 palaceWaitMs = 120000;
	switch (difficulty) {
		case Difficulty::Medium:
			ixWaitMs = 50000;
			palaceWaitMs = 90000;
			break;
		case Difficulty::Hard:
			ixWaitMs = 35000;
			palaceWaitMs = 60000;
			break;
		case Difficulty::Brutal:
			ixWaitMs = 20000;
			palaceWaitMs = 45000;
			break;
		case Difficulty::Defend:
		case Difficulty::Easy:
			break;
	}

	const bool ixOverdue = ixEligible
		&& currentBuildCycle - ixEligibleSinceCycle >= MILLI2CYCLES(ixWaitMs);
	const bool palaceOverdue = palaceEligible
		&& currentBuildCycle - palaceEligibleSinceCycle >= MILLI2CYCLES(palaceWaitMs);
	Uint32 strategicReserveItem = NONE_ID;
	if (stablePower && ixOverdue) {
		strategicReserveItem = Structure_IX;
	} else if (stablePower && palaceOverdue) {
		strategicReserveItem = Structure_Palace;
	}
	// Reserve only the cost of a feasible order. An unplaceable structure must
	// not freeze production indefinitely, and excess funds remain spendable.
	if (strategicReserveItem != NONE_ID && !findPlaceLocation(strategicReserveItem).isValid()) {
		strategicReserveItem = NONE_ID;
	}
	int strategicReserveCost = strategicReserveItem == NONE_ID ? 0
		: data[strategicReserveItem][houseID].price;

    int spiceCompetitors = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)
        if (getHouse(h) && getHouse(h)->getNumStructures() > 0) ++spiceCompetitors;
    const int spiceShare = lastCalculatedSpice / std::max(1, spiceCompetitors);
    const int mapSpiceHarvesterTarget = vanillaEconomy
        ? DuneCity::vanillaHarvesterTarget(lastCalculatedSpice, spiceCompetitors, harvesterLimit)
        : QuantBotBuildPolicy::desiredSpiceHarvesters(lastCalculatedSpice, spiceCompetitors, harvesterLimit);
    const int spiceHarvesterTarget = QuantBotBuildPolicy::refineryThroughputHarvesterTarget(
        spiceShare, mapSpiceHarvesterTarget, getHouse()->getNumItems(Structure_Refinery), harvesterLimit);
    const int fundedHarvesterTarget = QuantBotBuildPolicy::fundedSpiceHarvesters(
        mapSpiceHarvesterTarget, getHouse()->getNumItems(Structure_Refinery));
    const int cityWorkingReserve = data[Unit_Tank][houseID].price
        + (itemCount[Unit_Harvester] < fundedHarvesterTarget ? data[Unit_Harvester][houseID].price : 0)
        + (itemCount[Structure_Refinery] < QuantBotBuildPolicy::desiredSpiceRefineries(
            mapSpiceHarvesterTarget,itemCount[Unit_Harvester]) ? data[Structure_Refinery][houseID].price : 0);
    bool orderedSpiceHarvester = false;

    int largestGenerator = 0;
    int currentZonePower = 0, matureZonePower = 0, committedPowerDemand = 0;
    std::array<int,3> developingZones{};
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const int power = data[structure->getItemID()][structure->getOriginalHouseID()].power;
        if (power < 0) largestGenerator = std::max(largestGenerator, -power);
        if (citySimEnabled && DuneCity::isCityZoneStructure(structure->getItemID())) {
            const auto* zone = static_cast<const ZoneStructure*>(structure);
            currentZonePower += zone->getZonePowerDraw();
            const Coord pos = zone->getLocation();
            if (getMap().tileExists(pos.x,pos.y)
                && DuneCity::getStructurePopulation(zone,getMap().getTile(pos.x,pos.y)->getCityZoneDensity())
                    < DuneCity::getZonePopulation(zone->getItemID(),1))
                ++developingZones[zone->getItemID()-Structure_ZoneResidential];
            matureZonePower += DuneCity::getZonePower(structure->getItemID(), 3);
        }
    }
    if (citySimEnabled) for (Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
        committedPowerDemand += std::max(0,itemCount[item]-getHouse()->getNumItems(item))
            * (DuneCity::isCityZoneStructure(item) ? DuneCity::getZonePower(item,3) : std::max(0,data[item][houseID].power));
    const int zoneGrowthHeadroom = QuantBotBuildPolicy::cityGrowthPowerHeadroom(
        currentZonePower,matureZonePower,projectedPowerDemandGrowth,committedPowerDemand);
    const int cityPowerReserve = citySimEnabled ? QuantBotBuildPolicy::cityPowerReserve(
        getHouse()->getPowerRequirement(), largestGenerator) + zoneGrowthHeadroom : 0;
    const int cityYardTarget = citySimEnabled ? QuantBotBuildPolicy::cityConstructionYardTarget(
        std::max(0, money - queuedProductionCost), ownResValve, ownComValve, ownIndValve) : 0;
    const int cityConstructionCapacity = itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV];

    auto decisionState = [&]() {
        return AITelemetry::Record().set("credits", getHouse()->getCredits()).set("spendable", money)
            .set("military", militaryValue).set("military_limit", militaryValueLimit)
            .set("unit_limit_reached", getHouse()->isGroundUnitLimitReached()).set("max_units", getHouse()->getMaxUnits())
            .set("power_rules_enabled", powerRules).set("rocket_turrets_need_power", turretPowerRequired).set("city_effects_enabled", citySimEnabled)
            .set("economy_reserve", economyReserve).set("queued_production_cost", queuedProductionCost).set("queued_military_value", queuedMilitaryValue)
            .set("power_produced", getHouse()->getProducedPower()).set("power_required", getHouse()->getPowerRequirement())
            .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
            .set("zone_growth_headroom",zoneGrowthHeadroom).set("committed_power_demand",committedPowerDemand)
            .set("city_power_reserve_target", cityPowerReserve).set("largest_generator_nominal", largestGenerator)
            .set("res_count", itemCount[Structure_ZoneResidential]).set("com_count", itemCount[Structure_ZoneCommercial])
            .set("ind_count", itemCount[Structure_ZoneIndustrial])
            .set("construction_yards", itemCount[Structure_ConstructionYard])
            .set("mcvs_including_queued", itemCount[Unit_MCV])
            .set("city_yard_target", cityYardTarget)
            .set("city_mcv_cash", citySimEnabled ? std::max(0, money - queuedProductionCost) : 0)
            .set("city_mcv_working_reserve", citySimEnabled ? std::max(1000, cityWorkingReserve) : 0)
            .set("city_mcv_shortfall", citySimEnabled ? std::max(0, cityYardTarget - cityConstructionCapacity) : 0)
            .set("vanilla_yard_target", vanillaEconomy ? DuneCity::vanillaYardTarget(
                money, getHouse()->getNumItems(Unit_Harvester)) : 0)
            .set("mcv_upgrade_in_progress", mcvUpgradesInProgress > 0)
            .set("mcv_upgrades_in_progress", mcvUpgradesInProgress)
            .set("mcv_shortfall", vanillaEconomy ? DuneCity::vanillaMcvShortfall(
                money, getHouse()->getNumItems(Unit_Harvester), itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]) : 0)
            .set("construction_plans_first", citySimEnabled)
            .set("res_demand", ownResValve).set("com_demand", ownComValve).set("ind_demand", ownIndValve)
            .set("res_pop", ownResPop).set("com_pop", ownComPop).set("ind_pop", ownIndPop)
            .set("land_value", ownAvgLandValue).set("spice_remaining", lastCalculatedSpice)
            .set("spice_share", spiceShare).set("map_harvester_target", mapSpiceHarvesterTarget)
            .set("harvester_target", spiceHarvesterTarget).set("refinery_harvester_target",
                QuantBotBuildPolicy::refineryThroughputHarvesterTarget(spiceShare, 0,
                    getHouse()->getNumItems(Structure_Refinery), harvesterLimit))
            .set("repair_baseline", QuantBotBuildPolicy::baselineRepairYards(
                getHouse()->getNumItems(Structure_HeavyFactory), militaryValue))
            .set("harvester_ai_limit", harvesterLimit)
            .set("harvester_engine_limit", getHouse()->getMaxHarvesters())
            .set("funded_harvester_target", vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
            .set("storage_capacity", getHouse()->getCapacity())
            .set("nuclear_pending", itemCount[Structure_NuclearPlant] - getHouse()->getNumItems(Structure_NuclearPlant))
            .set("windtrap_pending", itemCount[Structure_WindTrap] - getHouse()->getNumItems(Structure_WindTrap))
            .set("stadium_committed", itemCount[Structure_Stadium]).set("airport_committed", itemCount[Structure_Airport])
            .set("strategic_item", strategicReserveItem).set("strategic_reserve", strategicReserveCost);
    };

    auto powerGenerationPending = [&]() {
        return itemCount[Structure_NuclearPlant] > getHouse()->getNumItems(Structure_NuclearPlant)
            || itemCount[Structure_WindTrap] > getHouse()->getNumItems(Structure_WindTrap);
    };

	auto chooseCityZone = [&](const BuilderBase* builder, bool bootstrap) {
		auto ranked = QuantBotBuildPolicy::rankZones(
			itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
			itemCount[Structure_ZoneIndustrial], ownResValve, ownComValve, ownIndValve, bootstrap);
        bool residentialInfill=false;
        if (!bootstrap && ownResValve>=500 && builder->isAvailableToBuild(Structure_ZoneResidential)) {
            const auto site=findPlaceLocation(Structure_ZoneResidential);
            residentialInfill=site.isValid() && residentialInfillSides(getMap(),houseID,site.x,site.y,2,2)>=2;
            QuantBotBuildPolicy::prioritizeResidentialInfill(ranked,ownResValve,residentialInfill);
        }
        Uint32 selected = NONE_ID;
        AITelemetry::Record candidates;
        for (Uint32 candidate : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
            const int demand = candidate == Structure_ZoneResidential ? ownResValve
                : candidate == Structure_ZoneCommercial ? ownComValve : ownIndValve;
            int rank = -1;
            for (int i = 0; i < 3; ++i) if (ranked[i] == candidate) rank = i;
            candidates.set(std::to_string(candidate), AITelemetry::Record().set("rank", rank)
                .set("demand", demand).set("normalized_demand", QuantBotBuildPolicy::normalizedZoneDemand(candidate, demand))
                .set("count_including_queued", itemCount[candidate]));
        }
        AITelemetry::Record evaluated;
        for (Uint32 candidate : ranked) {
            if (candidate == NONE_ID) continue;
            // Demand and site suitability select the tax candidate; the economic
            // comparison below decides whether more spice capacity is better.
            const char* reason = "lower_rank_not_evaluated";
            if (selected == NONE_ID) {
                if (!builder->isAvailableToBuild(candidate)) reason = "unavailable";
                else if (!findPlaceLocation(candidate).isValid()) reason = "no_site";
                else { selected = candidate; reason = "selected"; }
            }
            evaluated.set(std::to_string(candidate), reason);
        }
        // No-site retries are sampled; successful choices are always recorded.
        if (AITelemetry::log().enabled() && (selected != NONE_ID || !lastZoneTraceCycle.count(builder->getObjectID())
                || getGameCycleCount() - lastZoneTraceCycle[builder->getObjectID()] >= MILLI2CYCLES(30000))) {
            lastZoneTraceCycle[builder->getObjectID()] = getGameCycleCount();
            zoneDecisionIds[builder->getObjectID()] = traceDecision("zone_evaluation", AITelemetry::Record()
                .set("builder", builder->getObjectID()).set("bootstrap", bootstrap)
                .set("rule", residentialInfill ? "residential_infill" : "demand_threshold_then_normalized_demand").set("selected", selected)
                .set("expansion_policy", "demand_led_tax_candidate")
                .set("result", selected != NONE_ID ? "selected"
                    : ranked[0] == NONE_ID ? "no_positive_demand" : "no_available_site_or_building")
                .set("state", decisionState()).set("candidates", candidates).set("evaluated", evaluated));
        }
        return selected;
	};

    // Site searches are already cached. Only inspect a bounded neighbourhood
    // for transport distance, once per build pass; never run a path search.
    int refineryTripCycles = -1;
    int refineryFieldRisk = 0;
    auto chooseCityEconomy = [&](const BuilderBase* builder, bool opening) {
        using namespace CityEconomyInvestmentPolicy;
        const Uint32 zone = chooseCityZone(builder, opening);
        const auto* sim = currentGame->getCitySimulation();
        if (!sim) return zone;
        const int tax = sim->getCityTax();
        const int land = ownAvgLandValue>0 ? ownAvgLandValue : 128;
        const int workers = itemCount[Unit_Harvester], refs = itemCount[Structure_Refinery];
        const bool capacityNeeded = refineryCapacityNeeded(refs,workers,spiceHarvesterTarget);
        const bool hedge = zone == Structure_ZoneResidential && itemCount[zone] == 0;
        Investment residential, refinery;
        auto setupInvestment = [&](Uint32 item, Coord site, int power) {
            const Coord size = getStructureSize(item);
            int foundation = 0, roads = 0;
            for (int y=site.y-1;y<=site.y+size.y;++y) for (int x=site.x-1;x<=site.x+size.x;++x) {
                if (!getMap().tileExists(x,y)) continue;
                const auto* tile = getMap().getTile(x,y);
                const bool footprint = x>=site.x && x<site.x+size.x && y>=site.y && y<site.y+size.y;
                if (footprint) foundation += !tile->hasPreparedFoundation();
                else if (!tile->isRoad() && !tile->hasAGroundObject()
                    && DuneCity::isCityBuildableTerrain(tile->getType())) ++roads;
            }
            // Amortize generation even when today's windtrap has spare power.
            Investment result;
            result.cost = data[item][houseID].price + foundation*data[Structure_Slab1][houseID].price
                + power*data[Structure_WindTrap][houseID].price/std::max(1,-data[Structure_WindTrap][houseID].power);
            // House::placeStructure auto-paves frontage without a construction
            // charge. Budget only its future ordinary-road maintenance; this
            // allows for growth beyond the 2,000-pop starter exemption.
            result.annualUpkeep = power/8 + roads*7/10;
            return result;
        };
        if (zone != NONE_ID) {
            const Coord site = findPlaceLocation(zone);
            const bool industry = zone == Structure_ZoneIndustrial;
            const int pollution = sim->getPollutionDensityMap().worldGet(site.x,site.y);
            const int crime = sim->getCrimeRateMap().worldGet(site.x,site.y);
            const int localValue = sim->getLandValueMap().worldGet(site.x,site.y);
            // Forecast low density on ordinary land, medium on good clean land.
            // Do not value a newly zoned plot as an instant high-density tower.
            const int level = localValue>=128 && pollution<=DuneCity::kPollutionGrowthThreshold && crime<128 ? 2 : 1;
            const int population = DuneCity::getZonePopulation(zone,level);
            const int power = DuneCity::getZonePower(zone,level);
            const int unfinished = developingZones[zone-Structure_ZoneResidential]
                + std::max(0,itemCount[zone]-getHouse()->getNumItems(zone));
            const int demand = zone == Structure_ZoneResidential ? ownResValve : industry ? ownIndValve : ownComValve;
            residential = setupInvestment(zone,site,power);
            residential.annualIncome = DuneCity::computeAnnualTaxRevenue(population,tax,land);
            if (zone != Structure_ZoneResidential) {
                // One C/I job supports eight residents. Credit only half the
                // tax of existing/pending housing currently short of jobs.
                const int waitingR = developingZones[0] + std::max(0,itemCount[Structure_ZoneResidential]
                    - getHouse()->getNumItems(Structure_ZoneResidential));
                const int shortage = std::max(0,ownResPop+waitingR*16-(ownComPop+ownIndPop)*8);
                residential.annualIncome += DuneCity::computeAnnualTaxRevenue(std::min(shortage,population*8),tax,land)/2;
            }
            residential.delayCycles = DuneCity::kCyclesPerCityYear; // ~60 s growth/foundation allowance
            residential.confidence = zoneConfidence(demand,zone==Structure_ZoneResidential ? 2000 : 1500,
                industry ? 0 : pollution,crime,unfinished);
        }
        Coord refinerySite = Coord::Invalid();
        if (capacityNeeded && spiceShare>0 && builder->isAvailableToBuild(Structure_Refinery))
            refinerySite = findPlaceLocation(Structure_Refinery);
        if (refinerySite.isValid()) {
            if (refineryTripCycles<0) {
                int distance = 65;
                Coord field = Coord::Invalid();
                // Nearest sampled field within 32 tiles. Missing local spice
                // gets a long-trip estimate; distant spice isn't called depleted.
                for (int dy=-32;dy<=32;dy+=2) for (int dx=-32;dx<=32;dx+=2) {
                    const Coord p(refinerySite.x+dx,refinerySite.y+dy);
                    if (!getMap().tileExists(p.x,p.y) || !getMap().getTile(p.x,p.y)->hasSpice()) continue;
                    const int d = std::abs(dx)+std::abs(dy);
                    if (d<distance) { distance=d; field=p; }
                }
                refineryFieldRisk = dangerAt(refinerySite,getStructureSize(Structure_Refinery))
                    + (field.isValid() ? dangerAt(field,Coord(1,1)) : 0);
                // Harmonic mean of empty outbound and full (60%) return speed.
                const FixPoint travelSpeed = data[Unit_Harvester][houseID].maxspeed * 0.75_fix;
                refineryTripCycles = travelSpeed>0 ? (FixPoint(2*distance*TILESIZE)/travelSpeed).lround() : horizonCycles;
            }
            const int fillCycles = (FixPoint(HARVESTERMAXSPICE)/HARVESTSPEED).lround();
            const int unloadCycles = HARVESTERMAXSPICE*8/5; // Refinery's 0.625 spice/cycle
            const int roundTrip = fillCycles+unloadCycles+refineryTripCycles;
            const int workerIncome = HARVESTERMAXSPICE*int(DuneCity::kCyclesPerCityYear)/std::max(1,roundTrip);
            // Half the theoretical bay throughput allows queues and manoeuvring.
            const int bayIncome = int(DuneCity::kCyclesPerCityYear)*5/16;
            const bool freeWorker = workers < harvesterLimit;
            const int power = std::max(0,data[Structure_Refinery][houseID].power);
            refinery = setupInvestment(Structure_Refinery,refinerySite,power);
            refinery.annualIncome = marginalSpiceIncome(workers,refs,freeWorker,workerIncome,bayIncome);
            refinery.delayCycles = data[Structure_Refinery][houseID].buildtime*15 + roundTrip;
            const int forecastFleetSpice = (workers+int(freeWorker))*workerIncome*4;
            refinery.confidence = static_cast<int>(std::min<int64_t>(1000,int64_t(spiceShare)*1000/std::max(1,forecastFleetSpice)));
            if (refineryFieldRisk>0) refinery.confidence/=2;
        }
        Uint32 selected = preferRefinery(refinery,residential,capacityNeeded,hedge) ? Structure_Refinery : zone;
        if (selected == zone && zone!=NONE_ID && !hedge && residential.confidence==0) selected=NONE_ID;
        // Save for a winning refinery instead of spending its money on another
        // cheap lot every pass. The caller preserves urgent non-economic work.
        const bool funded = selected!=NONE_ID && money>=data[selected][houseID].price;
        if (AITelemetry::log().enabled() && selected!=NONE_ID
            && (!lastEconomyTraceCycle.count(builder->getObjectID())
                || getGameCycleCount()-lastEconomyTraceCycle[builder->getObjectID()]>=MILLI2CYCLES(30000))) {
            lastEconomyTraceCycle[builder->getObjectID()]=getGameCycleCount();
            auto describe = [](const Investment& i) { return AITelemetry::Record().set("cost",i.cost)
                .set("annual_income",i.annualIncome).set("annual_upkeep",i.annualUpkeep)
                .set("delay_cycles",i.delayCycles).set("confidence_per_mille",i.confidence).set("proceeds",i.proceeds()); };
            traceDecision("city_economy_comparison",AITelemetry::Record().set("builder",builder->getObjectID())
                .set("selected",selected).set("zone",zone).set("hedge",hedge).set("funded",funded)
                .set("capacity_needed",capacityNeeded).set("workers",workers).set("refineries",refs)
                .set("sustainable_workers",spiceHarvesterTarget).set("horizon_cycles",horizonCycles)
                .set("spice_share",spiceShare).set("trip_cycles",refineryTripCycles).set("field_risk",refineryFieldRisk)
                .set("tax_candidate",describe(residential)).set("refinery_candidate",describe(refinery)));
        }
        return selected;
    };

	bool emitStatsLog = false;

    if (!supportMode && (militaryValue > 0 || getHouse()->getNumStructures() > 0)) {
        const Uint32 currentCycle = getGameCycleCount();
        if(currentCycle - lastStatsLogCycle >= MILLI2CYCLES(30000)) {
			emitStatsLog = true;
            if (gameMode == GameMode::Custom) {
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d  built: %d  kill: %d  loss: %d remaining spice: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, getHouse()->getUnitBuiltValue(),
                    getHouse()->getKillValue(), getHouse()->getLossValue(), lastCalculatedSpice, getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            } else {
                // Campaign mode - include initial military value and multiplier
                const QuantBotConfig& config = getQuantBotConfig();
                const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d (init: %d, mult: %.1fx)  built: %d  kill: %d  loss: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, initialMilitaryValue, diffSettings.militaryValueMultiplier,
                    getHouse()->getUnitBuiltValue(), getHouse()->getKillValue(), getHouse()->getLossValue(), getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            }
            lastStatsLogCycle = currentCycle;
        }
    }


    if (AITelemetry::log().enabled() && (telemetryState == 0
            || getGameCycleCount() - lastTelemetrySnapshotCycle >= MILLI2CYCLES(30000))) {
        lastTelemetrySnapshotCycle = getGameCycleCount();
        AITelemetry::Record actual, queued;
        for (int i = ItemID_FirstID; i <= ItemID_LastID; ++i) {
            const int count = getHouse()->getNumItems(i);
            if (count) actual.set(std::to_string(i), count);
            if (itemCount[i] != count) queued.set(std::to_string(i), itemCount[i] - count);
        }
        AITelemetry::Record harvesters;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() != getHouse()) continue;
            if (const auto* harvester = dynamic_cast<const Harvester*>(unit)) {
                const auto pos = unit->getLocation(), dest = unit->getDestination();
                harvesters.set(std::to_string(unit->getObjectID()), AITelemetry::Record()
                    .set("x", pos.x).set("y", pos.y).set("destination_x", dest.x).set("destination_y", dest.y)
                    .set("active", unit->isActive()).set("harvesting", harvester->isHarvesting())
                    .set("returning", harvester->isReturning()).set("cargo", harvester->getAmountOfSpice().lround())
                    .set("health", unit->getHealth().lround()).set("mode", unit->getAttackMode()));
            }
        }
        AITelemetry::Record cityHealth;
        if (citySimEnabled && currentGame->getCitySimulation()) {
            auto* sim = currentGame->getCitySimulation();
            const auto& hs = sim->getHouseState(getHouse()->getHouseID());
            int samples = 0, pollution = 0, crime = 0, traffic = 0;
            int resCrime = 0, comCrime = 0, extremeCrime = 0, slowPollution = 0, blockedPollution = 0;
            // Keep telemetry in the same named bands shown to players and
            // defined by Micropolis's crime overlay.
            int crimeBands[4] = {};
            for (const StructureBase* structure : getStructureList()) {
                if (structure->getOwner() != getHouse()) continue;
                const Uint32 type = structure->getItemID();
                if (type < Structure_ZoneResidential || type > Structure_ZoneIndustrial) continue;
                const Coord p = structure->getLocation();
                ++samples;
                pollution += sim->getPollutionDensityMap().worldGet(p.x, p.y);
                const int localCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                const int localPollution = sim->getPollutionDensityMap().worldGet(p.x, p.y);
                crime += localCrime;
                ++crimeBands[localCrime < 64 ? 0 : localCrime < 128 ? 1 : localCrime < 192 ? 2 : 3];
                resCrime += type == Structure_ZoneResidential && localCrime > 100;
                comCrime += type == Structure_ZoneCommercial && localCrime > 80;
                extremeCrime += localCrime > 190;
                if (type != Structure_ZoneIndustrial) {
                    slowPollution += localPollution >= 80;
                    blockedPollution += localPollution >= 160;
                }
                traffic += sim->getTrafficDensityMap().worldGet(p.x, p.y);
            }
            cityHealth.set("unemployment", hs.unemploymentRate).set("tax_rate", sim->getCityTax())
                .set("last_tax_revenue", hs.budget.getLastTaxRevenue()).set("police_expense", hs.lastPoliceExpense)
                .set("road_tiles", hs.roads.tiles).set("heavy_road_tiles", hs.roads.heavyTiles)
                .set("road_expense", hs.roads.annualCost(hs.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier))
                .set("zone_origin_samples", samples).set("pollution_mean", samples ? pollution / samples : 0)
                .set("crime_mean", samples ? crime / samples : 0).set("traffic_mean", samples ? traffic / samples : 0)
                .set("res_crime_above100",resCrime).set("com_crime_above80",comCrime).set("crime_above190",extremeCrime)
                .set("res_com_pollution_atleast80",slowPollution).set("res_com_pollution_atleast160",blockedPollution)
                .set("crime_bands", AITelemetry::Record().set("safe_0_63",crimeBands[0])
                    .set("light_64_127",crimeBands[1]).set("moderate_128_191",crimeBands[2])
                    .set("dangerous_192_250",crimeBands[3]));
            if (telemetryState == 0 || getGameCycleCount()-lastCityBuildingSnapshotCycle >= MILLI2CYCLES(120000)) {
                lastCityBuildingSnapshotCycle = getGameCycleCount();
                AITelemetry::Record buildings;
                int count = 0;
                for (const auto* structure : getStructureList()) {
                    if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
                    const auto item = structure->getItemID();
                    const auto pos = structure->getLocation(), size = structure->getStructureSize();
                    const auto* tile = getMap().getTile(pos.x,pos.y);
                    if (!tile) continue;
                    const auto role = DuneCity::getStructureCityRole(item);
                    const int level = DuneCity::isCityZoneStructure(item) ? tile->getCityZoneDensity()
                        : role == DuneCity::CityRole::None ? 0 : std::max(1,int(structure->getCityOccupancy()));
                    int generation = 0;
                    if (const auto* reactor = dynamic_cast<const NuclearPlant*>(structure)) generation = reactor->getProducedPower();
                    else if (const auto* windtrap = dynamic_cast<const WindTrap*>(structure)) generation = windtrap->getProducedPower();
                    buildings.set(std::to_string(structure->getObjectID()), AITelemetry::Record()
                        .set("item",item).set("x",pos.x).set("y",pos.y).set("width",size.x).set("height",size.y)
                        .set("health",structure->getHealth().lround()).set("max_health",structure->getMaxHealth())
                        .set("role",static_cast<int>(role)).set("level",level).set("max_level",DuneCity::getStructureMaxLevel(item))
                        .set("population",DuneCity::getStructurePopulation(structure,level))
                        .set("res_supply",DuneCity::getStructureResidentialSupply(structure,level))
                        .set("com_supply",DuneCity::getCommercialSupply(item,level)).set("ind_supply",DuneCity::getIndustrialSupply(item,level))
                        .set("palace_com_population",item == Structure_Palace ? DuneCity::getPalaceCommercialPopulation(level) : 0)
                        .set("land_value",sim->getLandValueMap().worldGet(pos.x,pos.y))
                        .set("population_density",sim->getPopulationDensityMap().worldGet(pos.x,pos.y))
                        .set("pollution",sim->getPollutionDensityMap().worldGet(pos.x,pos.y))
                        .set("crime",sim->getCrimeRateMap().worldGet(pos.x,pos.y))
                        .set("crime_before_police",sim->getCrimeBeforePoliceMap().worldGet(pos.x,pos.y))
                        .set("police_coverage",sim->getPoliceCoverageMap().worldGet(pos.x,pos.y))
                        .set("traffic_density",sim->getTrafficDensityMap().worldGet(pos.x,pos.y))
                        .set("growth_rate",sim->getGrowthRateMap().worldGet(pos.x,pos.y))
                        .set("pollution_emission",DuneCity::getPollutionEmission(item,level))
                        .set("land_value_bonus",DuneCity::getParkLandValueBonus(item))
                        .set("police_strength",DuneCity::getPoliceCoverage(item)).set("police_cost",DuneCity::getPoliceAnnualCost(item).lround()).set("police_cost_milli",(DuneCity::getPoliceAnnualCost(item)*1000).lround())
                        .set("power_nominal",currentGame->objectData.data[item][structure->getOriginalHouseID()].power)
                        .set("power_generated",generation));
                    ++count;
                }
                traceDecision("city_building_snapshot", AITelemetry::Record().set("buildings",buildings).set("count",count)
                    .set("police_funding",hs.policeFundingPercent).set("state",decisionState()));
            }
        }

        int actualGeneratorPower = 0;
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse()) continue;
            if (const auto* wind = dynamic_cast<const WindTrap*>(structure)) actualGeneratorPower += wind->getProducedPower();
            else if (const auto* nuclear = dynamic_cast<const NuclearPlant*>(structure)) actualGeneratorPower += nuclear->getProducedPower();
            else if (const auto* advanced = dynamic_cast<const AdvancedWindTrap*>(structure)) actualGeneratorPower += advanced->getProducedPower();
            else if (const auto* scout = dynamic_cast<const Scoutpost*>(structure)) actualGeneratorPower += scout->getProducedPower();
        }
        // Observer-only comparison data, never fed back into AI decisions.
        AITelemetry::Record opponents;
        for (int h = 0; h < NUM_HOUSES; ++h) {
            const auto* house = currentGame->getHouse(h);
            if (!house) continue;
            int survivingMilitary = 0;
            for (Uint32 type=Unit_FirstID; type<=Unit_LastID; ++type)
                if (type != Unit_Carryall && type != Unit_Harvester && type != Unit_MCV && type != Unit_Sandworm)
                    survivingMilitary += house->getNumItems(type) * currentGame->objectData.data[type][h].price;
            opponents.set(std::to_string(h), AITelemetry::Record()
                .set("team", house->getTeamID()).set("alive", house->isAlive())
                .set("credits", house->getCredits()).set("military", survivingMilitary).set("military_basis", "current_unit_counts")
                .set("harvesters", house->getNumItems(Unit_Harvester))
                .set("refineries", house->getNumItems(Structure_Refinery))
                .set("heavy_factories", house->getNumItems(Structure_HeavyFactory))
                .set("construction_yards", house->getNumItems(Structure_ConstructionYard))
                .set("economy_totals", AITelemetry::log().economyTotals(h)).set("combat_rewards", house->combatRewardStats(currentGame->objectData)));
        }
        telemetryState = AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), "state_snapshot",
            AITelemetry::Record().set("ai", "QuantBot").set("name", getPlayername())
                .set("difficulty", static_cast<int>(difficulty)).set("support", supportMode)
                .set("house_name", getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())))
                .set("team", getHouse()->getTeamID())
                .set("state", decisionState()).set("city_health", cityHealth).set("actual", actual).set("queued", queued)
                .set("harvesters", harvesters).set("house_comparison", opponents)
                .set("power_accounting", AITelemetry::Record().set("generators", actualGeneratorPower)
                    .set("reported", getHouse()->getProducedPower())
                    .set("difference", getHouse()->getProducedPower() - actualGeneratorPower))
                .set("economy_totals", AITelemetry::log().economyTotals(houseID))
                .set("economy", AITelemetry::Record()
                    .set("harvested_spice_total", getHouse()->getHarvestedSpice().lround())
                    .set("stored_spice_credits", getHouse()->getStoredCredits().lround())
                    .set("city_credit_balance", getHouse()->getCityCredits().lround())
                    .set("starting_credit_balance", getHouse()->getStartingCredits().lround()))
                .set("heavy_busy", activeHeavyFactoryCount).set("light_busy",activeLightFactoryCount).set("repair_busy", activeRepairYardCount)
                .set("built_value", getHouse()->getUnitBuiltValue()).set("kill_value", getHouse()->getKillValue())
                .set("loss_value", getHouse()->getLossValue()));
    }

    // Infantry retains its separate difficulty quota. Vehicle openings use
    // the current technology and actual production availability below.
	int infantryPercent = 10;
	switch (difficulty) {
		case Difficulty::Defend:
		case Difficulty::Easy:
			infantryPercent = 18;
			break;
		case Difficulty::Medium:
			infantryPercent = 15;
			break;
		case Difficulty::Hard:
			infantryPercent = 12;
			break;
		case Difficulty::Brutal:
			break;
	}

    constexpr std::array<Uint32,8> mixItems = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
        Unit_SonicTank, Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};
    UnitMixPolicy::Weights damage{}, rewardMilli{}, killBonusMilli{}, lostValue{}, scores{}, defaults{}, prices{}, lossMilli{};
    const auto& ratios = getQuantBotConfig().getRatios(houseID);
    const std::array<int,5> configured = {static_cast<int>(ratios.tank*10000),
        static_cast<int>(ratios.siegeTank*10000), static_cast<int>(ratios.launcher*10000),
        static_cast<int>(ratios.special*10000), static_cast<int>(ratios.ornithopter*10000)};
    std::array<bool,8> available{};
    for (const auto* structure : getStructureList()) {
        const auto* builder = dynamic_cast<const BuilderBase*>(structure);
        if (!builder || builder->getOwner() != getHouse()
            || (builder->getItemID() != Structure_LightFactory && builder->getItemID() != Structure_HeavyFactory
                && builder->getItemID() != Structure_HighTechFactory)) continue;
        for (size_t i=0; i<8; ++i) {
            if (i == 3) {
                for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    available[i] |= builder->isAvailableToBuild(special);
            } else available[i] |= builder->isAvailableToBuild(mixItems[i]);
        }
    }
    if (getHouse()->isAirUnitLimitReached()) available[4]=false;
    UnitMixPolicy::Weights configuredWeights{};
    for (size_t i=0; i<5; ++i) configuredWeights[i] = configured[i];
    const auto openingMix = UnitMixPolicy::openingMix(currentGame->techLevel, configuredWeights, available);
    for (size_t i=0; i<8; ++i) defaults[i] = openingMix[i];
    int64_t totalDamage = 0, totalRewardMilli = 0, totalLostValue = 0;
    for (size_t i=0; i<8; ++i) {
        const Uint32 item = mixItems[i];
        int priorPrice = std::max(1, data[item][houseID].price);
        damage[i] = std::max(0, getHouse()->getNumItemDamageInflicted(item));
        rewardMilli[i] = getHouse()->getCombatReward(item).total();
        killBonusMilli[i] = getHouse()->getCombatReward(item).killBonusMilli;
        lostValue[i] = int64_t(getHouse()->getNumLostItems(item)) * data[item][houseID].price;
        bool enabled = data[item][houseID].enabled && data[item][houseID].price > 0
            && data[item][houseID].techLevel <= currentGame->techLevel;
        if (i == 3) {
            damage[i] = rewardMilli[i] = killBonusMilli[i] = lostValue[i] = 0;
            enabled = false;
            priorPrice = 700; // Preserve the special-unit group's existing prior.
            for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator}) {
                damage[i] += getHouse()->getNumItemDamageInflicted(special);
                rewardMilli[i] += getHouse()->getCombatReward(special).total();
                killBonusMilli[i] += getHouse()->getCombatReward(special).killBonusMilli;
                lostValue[i] += int64_t(getHouse()->getNumLostItems(special)) * data[special][houseID].price;
                enabled |= data[special][houseID].enabled && data[special][houseID].price > 0
                    && data[special][houseID].techLevel <= currentGame->techLevel;
            }
            damage[i] = std::max<int64_t>(0, damage[i]);
        }
        totalDamage += damage[i];
        totalRewardMilli += rewardMilli[i];
        totalLostValue += lostValue[i];
        prices[i] = priorPrice * 1000;
        lossMilli[i] = lostValue[i] * 1000;
        if (!enabled || !available[i]) { available[i] = false; continue; }
        scores[i] = UnitMixPolicy::performanceScore(rewardMilli[i], lostValue[i]*1000, priorPrice*1000);
    }
    const bool learningUnitMix = totalRewardMilli >= 3000000;
    performanceHistory.update(getGameCycleCount(),rewardMilli,lossMilli);
    if (learningUnitMix) {
        for (size_t i=0;i<8;++i) scores[i]=available[i]
            ? UnitMixPolicy::performanceScore(performanceHistory.reward[i],performanceHistory.loss[i],prices[i]) : 0;
        scores=UnitMixPolicy::exploredScores(scores,performanceHistory.reward,performanceHistory.loss,prices,available);
    }
    const auto allocationWeights = UnitMixPolicy::sharpenScores(scores);
    const auto rawMix = UnitMixPolicy::normalize(allocationWeights);
    const int performanceConfidenceBps = UnitMixPolicy::evidenceConfidenceBps(totalLostValue, militaryValue);
    const auto unitMix = UnitMixPolicy::allocate(scores, defaults, learningUnitMix, vanillaEconomy,
                                                  totalLostValue, militaryValue);
    lastUnitMixBps = unitMix;
    const int rawOrnithopterBps = rawMix[4];
    const FixPoint tankPercent = FixPoint(unitMix[0])/10000;
    const FixPoint siegePercent = FixPoint(unitMix[1])/10000;
    const FixPoint launcherPercent = FixPoint(unitMix[2])/10000;
    const FixPoint specialPercent = FixPoint(unitMix[3])/10000;
    const FixPoint ornithopterPercent = FixPoint(unitMix[4])/10000;
    const int lightVehicleBps = unitMix[5] + unitMix[6] + unitMix[7];
    const int lightVehiclePercent = lightVehicleBps / 100;

	int lightVehicleValue = data[Unit_Trike][houseID].price * itemCount[Unit_Trike]
		+ data[Unit_RaiderTrike][houseID].price * itemCount[Unit_RaiderTrike]
		+ data[Unit_Quad][houseID].price * itemCount[Unit_Quad];
	int infantryValue = data[Unit_Soldier][houseID].price * itemCount[Unit_Soldier]
		+ data[Unit_Infantry][houseID].price * itemCount[Unit_Infantry]
		+ data[Unit_Trooper][houseID].price * itemCount[Unit_Trooper]
		+ data[Unit_Troopers][houseID].price * itemCount[Unit_Troopers];
	int lightVehicleCount = itemCount[Unit_Trike] + itemCount[Unit_RaiderTrike] + itemCount[Unit_Quad];
	int infantryCount = itemCount[Unit_Soldier] + itemCount[Unit_Infantry]
		+ itemCount[Unit_Trooper] + itemCount[Unit_Troopers];

    const int productionCash = std::max(0, money - (vanillaEconomy ? 0 : queuedProductionCost)
        - std::max(strategicReserveCost, economyReserve) - (citySimEnabled ? cityWorkingReserve : 0));
    const int fundedArmyValue = QuantBotBuildPolicy::fundedArmyTarget(militaryValue, militaryValueLimit, productionCash);
    const int vehiclePlanValue = std::max(0, fundedArmyValue - infantryValue);
    if (emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("unit_mix", AITelemetry::Record().set("basis", !learningUnitMix ? "configured" : vanillaEconomy ? "lifetime_evidence_weighted_value_per_loss" : "lifetime_value_per_loss")
            .set("tank_bps", (tankPercent * 10000).lround()).set("siege_bps", (siegePercent * 10000).lround())
            .set("special_bps", (specialPercent * 10000).lround()).set("launcher_bps", (launcherPercent * 10000).lround())
            .set("ornithopter_bps", (ornithopterPercent * 10000).lround()).set("raw_ornithopter_bps", rawOrnithopterBps)
            .set("light_vehicle_percent", lightVehiclePercent).set("light_vehicle_bps", lightVehicleBps)
            .set("trike_bps", unitMix[5]).set("raider_trike_bps", unitMix[6]).set("quad_bps", unitMix[7])
            .set("allocation_types", 8).set("tech_level", currentGame->techLevel)
            .set("opening_light_bps", openingMix[5]+openingMix[6]+openingMix[7]).set("total_damage", totalDamage)
            .set("total_reward_milli", totalRewardMilli).set("total_lost_value", totalLostValue)
            .set("performance_exponent_milli",1500).set("performance_confidence_bps", performanceConfidenceBps).set("funded_army_value",fundedArmyValue)
            .set("vehicle_plan_value",vehiclePlanValue).set("queued_military_value",queuedMilitaryValue).set("mix_inputs", [&]() {
                AITelemetry::Record inputs;
                for (size_t i=0; i<8; ++i) inputs.set(i == 3 ? "special" : std::to_string(mixItems[i]),
                    AITelemetry::Record().set("damage", damage[i]).set("reward_milli", rewardMilli[i]).set("kill_bonus_milli", killBonusMilli[i])
                        .set("lost_value", lostValue[i]).set("score", scores[i]).set("allocation_weight",allocationWeights[i])
                        .set("lifetime_reward_milli",performanceHistory.reward[i]).set("lifetime_loss_milli",performanceHistory.loss[i])
                        .set("available", available[i]).set("opening_bps", openingMix[i]).set("target_bps", unitMix[i]));
                return inputs;
            }()).set("infantry_percent", infantryPercent));
    }

	// lets analyse damage inflicted

	if (emitStatsLog) {
		logDebug("Adaptive unit mix: damage=%lld light=%d bps", static_cast<long long>(totalDamage), lightVehicleBps);

		logDebug("  Tank: %d/%d %f Siege: %d/%d %f Special: %d/%d %f Launch: %d/%d %f Orni: %d/%d %f",
			getHouse()->getNumItemDamageInflicted(Unit_Tank), getHouse()->getNumLostItems(Unit_Tank) * 300, tankPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SiegeTank), getHouse()->getNumLostItems(Unit_SiegeTank) * 600, siegePercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SonicTank) + getHouse()->getNumItemDamageInflicted(Unit_Devastator) + getHouse()->getNumItemDamageInflicted(Unit_Deviator),
			getHouse()->getNumLostItems(Unit_SonicTank) * 600 + getHouse()->getNumLostItems(Unit_Devastator) * 800 + getHouse()->getNumLostItems(Unit_Deviator) * 750,
			specialPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Launcher), getHouse()->getNumLostItems(Unit_Launcher) * 450, launcherPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Ornithopter), getHouse()->getNumLostItems(Unit_Ornithopter) * data[Unit_Ornithopter][houseID].price, ornithopterPercent.toDouble()
		);
	}

	// End of adaptive unit prioritisation algorithm

	// Track unique structures ordered this tick to prevent multiple CYs
	// from building the same thing. Zones and turrets are excluded (want multiples).
	std::set<Uint32> orderedThisTick;
    bool roadMaintenanceAttempted = false;

    // Give city construction first access to this pass's planning budget.
    // Air gets its allocation before ground production, then light precedes heavy overflow.
    // Stable ordering keeps peers deterministic.
    // Keep IDs, not pointers: an earlier yard may demolish a later zone.
    std::vector<std::pair<int, Uint32>> planningOrder;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse())
            planningOrder.emplace_back(QuantBotBuildPolicy::productionPlanningPriority(citySimEnabled,
                structure->getItemID(), structure->getItemID() == Structure_ConstructionYard
                    && static_cast<const ConstructionYard*>(structure)->isWaitingToPlace()), structure->getObjectID());
    std::stable_sort(planningOrder.begin(), planningOrder.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    cityReadyYardCount = std::max(1,int(std::count_if(planningOrder.begin(),planningOrder.end(),
        [](const auto& row){return row.first == 3;})));
    if (citySimEnabled) CityPlanningPolicy::rotateYards(planningOrder,getGameCycleCount());
	for (const auto& entry : planningOrder) {
        const auto* pStructure = dynamic_cast<const StructureBase*>(getObject(entry.second));
		if (pStructure && pStructure->getOwner() == getHouse()) {
            if (pStructure->getItemID() == Structure_NuclearPlant
                && !pStructure->isRepairing() && pStructure->getHealth() > 0
                && pStructure->getHealth() < pStructure->getMaxHealth() && money >= 5) {
                doRepair(pStructure);
                traceDecision("reactor_repair", AITelemetry::Record()
                    .set("object", pStructure->getObjectID()).set("health", pStructure->getHealth().lround())
                    .set("max_health", pStructure->getMaxHealth()).set("credits", money));
            }
			else if ((pStructure->isRepairing() == false)
				&& (pStructure->getHealth() < pStructure->getMaxHealth())
				&& (!getGameInitSettings().getGameOptions().concreteRequired
					|| pStructure->getItemID() == Structure_Palace) // Palace repairs for free
				&& (pStructure->getItemID() != Structure_Refinery
					&& pStructure->getItemID() != Structure_Silo
					&& pStructure->getItemID() != Structure_Radar
					&& pStructure->getItemID() != Structure_WindTrap))
			{
				doRepair(pStructure);
			}
			else if ((pStructure->isRepairing() == false)
				&& (pStructure->getHealth() < pStructure->getMaxHealth() * 0.40_fix)
				&& money > 1000) {
				doRepair(pStructure);
			}
			else if (!pStructure->isRepairing() && pStructure->getHealth() > 0
                && pStructure->getHealth() < pStructure->getMaxHealth() && money > 5000) {
				// Repair if we are rich
				doRepair(pStructure);
			}
			else if (pStructure->getItemID() == Structure_RocketTurret
                && !pStructure->isRepairing() && pStructure->getHealth() > 0
                && pStructure->getHealth() < pStructure->getMaxHealth()) {
				if (!getGameInitSettings().getGameOptions().structuresDegradeOnConcrete || pStructure->hasATarget()) {
					doRepair(pStructure);
				}
			}
			// Windtrap repair: Keep windtraps at max health to maintain power buffer
			// Damaged windtraps produce less power, so repair them to maintain 200 power surplus
			else if (powerRules && pStructure->getItemID() == Structure_WindTrap
				&& pStructure->getHealth() < pStructure->getMaxHealth()
				&& !pStructure->isRepairing()) {
				int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
				// Always repair if power surplus is below 200 (our buffer target)
				// Or repair if we have money and power is below 300 (some buffer room)
				if (powerExcess < 200 || (money > 500 && powerExcess < 300)) {
					doRepair(pStructure);
					logDebug("POWER: Repairing windtrap to maintain power buffer (excess: %d)", powerExcess);
				}
			}

			// Special weapon launch logic (not for support AI)
			if (pStructure->getItemID() == Structure_Palace && !supportMode) {

				const Palace* pPalace = static_cast<const Palace*>(pStructure);
				if (pPalace->isSpecialWeaponReady()) {

					if (houseID != HOUSE_HARKONNEN && houseID != HOUSE_SARDAUKAR) {
						doSpecialWeapon(pPalace);
					}
					else {
						int enemyHouseID = -1;
						int enemyHouseBuildingCount = 0;

						for (int i = 0; i < NUM_HOUSES; i++) {
							if (getHouse(i) != nullptr) {
								if (getHouse(i)->getTeamID() != getHouse()->getTeamID() && getHouse(i)->getNumStructures() > enemyHouseBuildingCount) {
									enemyHouseBuildingCount = getHouse(i)->getNumStructures();
									enemyHouseID = i;
								}
							}
						}

					if ((enemyHouseID != -1) && (houseID == HOUSE_HARKONNEN || houseID == HOUSE_SARDAUKAR)) {
						Coord target = findBestDeathHandTarget(enemyHouseID);
						if (target.isValid()) {
							doLaunchDeathhand(pPalace, target.x, target.y);
						}
					}
					}
				}
			}

			if (pStructure->isABuilder()) {
				const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false);

				// Log all builder status for campaign AIs (not just CY)
				if (gameMode == GameMode::Campaign && !supportMode && pStructure->getItemID() != Structure_ConstructionYard) {
					logDebug("PRODUCTION: %s - Upgrading:%d Queue:%d Credits:%d", 
						getItemNameByID(pStructure->getItemID()).c_str(),
						pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(), money);
				}

                if (emitStatsLog && AITelemetry::log().enabled()) {
                    traceDecision("producer_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("item", pBuilder->getItemID()).set("queue", pBuilder->getProductionQueueSize())
                        .set("current_item", pBuilder->getCurrentProducedItem()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("waiting_to_place", pBuilder->isWaitingToPlace())
                        .set("health", pBuilder->getHealth().lround()).set("max_health", pBuilder->getMaxHealth())
                        .set("progress_credits", pBuilder->getProductionProgress().lround())
                        .set("unit_limit_blocked", pBuilder->isUnitLimitReached(pBuilder->getCurrentProducedItem()))
                        .set("military_limit_blocked", militaryValue >= militaryValueLimit)
                        .set("power_deficit", !getHouse()->hasPower())
                        .set("x", pBuilder->getLocation().x).set("y", pBuilder->getLocation().y)
                        .set("credits", getHouse()->getCredits()));
                }
				// Correlate only decisions made for this builder in this planning pass.
                zoneDecisionIds.erase(pBuilder->getObjectID());

                // Record actual queue acceptance for unit and structure production.
				auto produceItemWithLogging = [&](Uint32 itemID, int sourceLine, const char* rule = "unit_mix_or_prerequisite") {
					if (gameMode == GameMode::Campaign && !supportMode && currentGame) {
						std::string itemName = getItemNameByID(itemID);
						logDebug("Queuing %s (ID:%d)", itemName.c_str(), itemID);
					}
					const int before = pBuilder->getProductionQueueSize();
                    const auto preOrder = AITelemetry::log().enabled() ? decisionState() : AITelemetry::Record();
					doProduceItem(pBuilder, itemID);
					const bool accepted = pBuilder->getProductionQueueSize() > before;
                    if (AITelemetry::log().enabled()) traceDecision("production_order", AITelemetry::Record()
                        .set("builder", pBuilder->getObjectID()).set("builder_item", pBuilder->getItemID())
                        .set("item", itemID).set("item_name", getItemNameByID(itemID)).set("accepted", accepted)
                        .set("source_line", sourceLine).set("rule", rule).set("queue_before", before).set("queue_after", pBuilder->getProductionQueueSize())
                        .set("quoted_price", [&]() { for (const auto& item : pBuilder->getBuildList())
                            if (item.itemID == itemID) return static_cast<int>(item.price); return 0; }())
                        .set("zone_decision", itemID >= Structure_ZoneResidential && itemID <= Structure_ZoneIndustrial ? zoneDecisionIds[pBuilder->getObjectID()] : 0)
                        .set("state", preOrder));
					if (!accepted && emitStatsLog) {
						logDebug("PRODUCTION: builder=%u rejected item=%u credits=%d", pBuilder->getObjectID(), itemID, money);
					}
					return accepted;
				};

				// Restore the reserved portion after this builder's decisions, keeping
				// all existing local spending deductions for subsequent builders.
				struct RestoreReservedCredits {
					int& money;
					int reserved;
					~RestoreReservedCredits() { money += reserved; }
				} reserve{money, pStructure->getItemID() == Structure_ConstructionYard
					? 0 : money - QuantBotBuildPolicy::spendableCredits(money, std::max(strategicReserveCost, economyReserve))};
				money -= reserve.reserved;

				if (!pBuilder->isUpgrading() && pBuilder->getProductionQueueSize() < 1
					&& money > 1500) {
					const int customItem = chooseLowPriorityCustomUnit(pBuilder);
					if (customItem != ItemID_Invalid) {
						produceItemWithLogging(customItem, __LINE__);
						itemCount[customItem]++;
						money -= data[customItem][houseID].price;
						militaryValue += data[customItem][houseID].price;
						continue;
					}
				}

				switch (pStructure->getItemID()) {

			case Structure_LightFactory: {
				if (!pBuilder->isUpgrading()
					&& money > (citySimEnabled ? 500 : 700)
					&& pBuilder->getProductionQueueSize() < 1
					&& pBuilder->getBuildListSize() > 0
					&& militaryValue < militaryValueLimit
					&& ((citySimEnabled && itemCount[Structure_HeavyFactory] == 0)
						|| ((!learningUnitMix && lightVehicleCount < 2)
                            || int64_t(lightVehicleValue) * 10000 < int64_t(vehiclePlanValue) * lightVehicleBps))) {

					if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel() && getHouse()->getCredits() > 1500) {
						doUpgrade(pBuilder);
					}
					else if (!getHouse()->isGroundUnitLimitReached()) {
                        Uint32 itemID = NONE_ID;
                        int64_t bestDeficit = 0;
                        for (size_t i=5; i<8; ++i) {
                            const Uint32 candidate = mixItems[i];
                            if (!pBuilder->isAvailableToBuild(candidate)) continue;
                            const auto deficit = UnitMixPolicy::deficit(unitMix[i], vehiclePlanValue,
                                itemCount[candidate], data[candidate][houseID].price);
                            if (deficit > bestDeficit) { bestDeficit = deficit; itemID = candidate; }
                        }
                        // Bootstrap a small sample before combat learning begins.
                        if (itemID == NONE_ID && !learningUnitMix && lightVehicleCount < 2)
                            for (size_t i=5; i<8; ++i)
                                if (pBuilder->isAvailableToBuild(mixItems[i])
                                    && (itemID == NONE_ID || itemCount[mixItems[i]] < itemCount[itemID])) itemID = mixItems[i];
                        if (itemID != NONE_ID && militaryValue + data[itemID][houseID].price <= militaryValueLimit
                            && produceItemWithLogging(itemID, __LINE__, "adaptive_light_mix")) {
                            ++itemCount[itemID];
                            ++lightVehicleCount;
                            lightVehicleValue += data[itemID][houseID].price;
                            militaryValue += data[itemID][houseID].price;
                            money -= data[itemID][houseID].price;
                        }
					}
				}
			} break;

		case Structure_WOR: {
			if (!citySimEnabled
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 450
				&& militaryValue < militaryValueLimit
				&& !getHouse()->isGroundUnitLimitReached()
				&& (infantryCount < 3
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				for (Uint32 candidate : {Unit_Trooper, Unit_Troopers}) {
					if (pBuilder->isAvailableToBuild(candidate)
						&& (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID])) {
						itemID = candidate;
					}
				}
				if (itemID != NONE_ID && militaryValue+data[itemID][houseID].price<=militaryValueLimit
                    && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;
                    money -= data[itemID][houseID].price;
				}
			}
		} break;

		case Structure_Barracks: {
			if (!citySimEnabled
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 300
				&& militaryValue < militaryValueLimit
				&& !getHouse()->isGroundUnitLimitReached()
				&& (infantryCount < 3
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				for (Uint32 candidate : {Unit_Soldier, Unit_Infantry}) {
					if (pBuilder->isAvailableToBuild(candidate)
						&& (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID])) {
						itemID = candidate;
					}
				}
				if (itemID != NONE_ID && militaryValue+data[itemID][houseID].price<=militaryValueLimit
                    && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;
                    money -= data[itemID][houseID].price;
				}
			}
		} break;

                case Structure_HighTechFactory: {
                    const int ornithopterPrice = data[Unit_Ornithopter][houseID].price;
                    const int carryallPrice = data[Unit_Carryall][houseID].price;
                    const int ornithopterValue = ornithopterPrice * itemCount[Unit_Ornithopter];
                    const int carryallTarget = (militaryValue + itemCount[Unit_Harvester] * 500) / 3000;
                    QuantBotBuildPolicy::AirProductionState air;
                    air.busy = pBuilder->getProductionQueueSize() > 0;
                    air.upgrading = pBuilder->isUpgrading();
                    air.airLimit = getHouse()->isAirUnitLimitReached();
                    air.ornithopterAvailable = pBuilder->isAvailableToBuild(Unit_Ornithopter);
                    air.carryallAvailable = pBuilder->isAvailableToBuild(Unit_Carryall);
                    air.canUpgrade = pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel();
                    air.spendable = money; // Economy/strategic reserves were removed above.
                    air.ornithopterPrice = ornithopterPrice;
                    air.carryallPrice = carryallPrice;
                    air.carryalls = itemCount[Unit_Carryall];
                    air.carryallTarget = carryallTarget;
                    air.armyValue = militaryValue;
                    air.armyLimit = militaryValueLimit;
                    air.vehiclePlanValue = vehiclePlanValue;
                    air.airCommittedValue = ornithopterValue;
                    air.airTargetBps = unitMix[4];
                    const auto decision = QuantBotBuildPolicy::chooseAirProduction(air);
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        traceDecision("air_production_decision", AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("reason",decision.reason)
                            .set("credits",getHouse()->getCredits()).set("planning_spendable",money)
                            .set("economy_reserve",economyReserve).set("strategic_reserve",strategicReserveCost)
                            .set("cash_threshold",ornithopterPrice).set("ornithopter_price",ornithopterPrice)
                            .set("air_target_value",(vehiclePlanValue*ornithopterPercent).lround())
                            .set("air_committed_value",ornithopterValue).set("air_target_bps",unitMix[4])
                            .set("military_value",militaryValue).set("military_limit",militaryValueLimit)
                            .set("ornithopter_available",air.ornithopterAvailable)
                            .set("carryall_target",carryallTarget).set("carryalls_committed",itemCount[Unit_Carryall])
                            .set("queue",pBuilder->getProductionQueueSize()).set("current_item",pBuilder->getCurrentProducedItem())
                            .set("upgrading",pBuilder->isUpgrading()).set("hold",pBuilder->isOnHold()));
                    }
                    using AirOrder = QuantBotBuildPolicy::AirOrder;
                    if (decision.order == AirOrder::Upgrade) {
                        if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) doUpgrade(pBuilder);
                        else doRepair(pBuilder);
                    } else if (decision.order == AirOrder::Ornithopter || decision.order == AirOrder::Carryall) {
                        const Uint32 item = decision.order == AirOrder::Ornithopter ? Unit_Ornithopter : Unit_Carryall;
                        if (produceItemWithLogging(item, __LINE__)) {
                            ++itemCount[item];
                            money -= data[item][houseID].price;
                            if (item == Unit_Ornithopter) militaryValue += ornithopterPrice;
                        }
                    }
                } break;

				case Structure_HeavyFactory: {
					// Log HF status when idle with money (Custom mode diagnostics)
					if (gameMode == GameMode::Custom && emitStatsLog) {
						logDebug("HF=%u: upgrading=%d queue=%d buildList=%d upgLv=%d/%d unitLimit=%d spendable=%d hold=%d military=%d/%d reserve=%d",
							pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
							pBuilder->getBuildListSize(),
							pBuilder->getCurrentUpgradeLevel(), pBuilder->getMaxUpgradeLevel(),
							getHouse()->isGroundUnitLimitReached(), money, pBuilder->isOnHold(),
							militaryValue, militaryValueLimit, strategicReserveCost);
					}
                    const int cityMcvCash = std::max(0, money - queuedProductionCost);
                    const bool prioritizeCityMcv = citySimEnabled && gameMode == GameMode::Custom
                        && !getHouse()->isGroundUnitLimitReached()
                        && QuantBotBuildPolicy::canFundCityYard(cityMcvCash, data[Unit_MCV][houseID].price,
                            itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV], cityYardTarget, cityWorkingReserve);
                    const bool prioritizeMcv = prioritizeCityMcv || (vanillaEconomy && gameMode == GameMode::Custom
                        && !getHouse()->isGroundUnitLimitReached()
                        && DuneCity::prioritizeVanillaMcv(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV], data[Unit_MCV][houseID].price));
                    const int mcvShortfall = citySimEnabled
                        ? std::max(0, cityYardTarget - itemCount[Structure_ConstructionYard] - itemCount[Unit_MCV])
                        : DuneCity::vanillaMcvShortfall(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]);
					// only if the factory isn't busy
					if ((pBuilder->isUpgrading() == false) && (pBuilder->getProductionQueueSize() < 1) && (pBuilder->getBuildListSize() > 0)) {
						// we need a construction yard. Build an MCV if we don't have a starport
						if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
							&& itemCount[Unit_MCV] + itemCount[Structure_ConstructionYard] + itemCount[Structure_StarPort] < 1
							&& pBuilder->isAvailableToBuild(Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()) {
							produceItemWithLogging(Unit_MCV, __LINE__);
							itemCount[Unit_MCV]++;
						}
                        else if (prioritizeMcv && pBuilder->isAvailableToBuild(Unit_MCV)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__,
                                    citySimEnabled ? "city_cash_construction_capacity" : "cash_construction_capacity")) {
                                ++itemCount[Unit_MCV];
                                money -= data[Unit_MCV][houseID].price;
                            }
                        }
                        else if (prioritizeMcv && mcvUpgradesInProgress < mcvShortfall
                            && !pBuilder->isAvailableToBuild(Unit_MCV)
                            && pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
                            && (citySimEnabled ? cityMcvCash : money) >= pBuilder->getUpgradeCost()
                                + (citySimEnabled ? std::max(1000, cityWorkingReserve) : 1000)) {
                            if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
                                const bool accepted = doUpgrade(pBuilder);
                                if (accepted) {
                                    ++mcvUpgradesInProgress;
                                    money -= pBuilder->getUpgradeCost();
                                }
                                traceDecision("mcv_unlock", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                                    .set("accepted", accepted).set("rule", citySimEnabled
                                        ? "city_cash_construction_capacity" : "cash_construction_capacity"));
                            } else if (!pBuilder->isRepairing()) {
                                doRepair(pBuilder);
                            }
                        }
						else if ((citySimEnabled || vanillaEconomy) && !orderedSpiceHarvester
                            && itemCount[Unit_Harvester] < (vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
                            && itemCount[Unit_Harvester] < getHouse()->getNumItems(Structure_Refinery) * 3
                            && pBuilder->isAvailableToBuild(Unit_Harvester) && !getHouse()->isGroundUnitLimitReached()
                            && money + (vanillaEconomy ? reserve.reserved : 0) >= data[Unit_Harvester][houseID].price
                                + (vanillaEconomy ? 1000 : data[Unit_Tank][houseID].price)) {
                            if (produceItemWithLogging(Unit_Harvester, __LINE__, "spice_economy")) {
                                ++itemCount[Unit_Harvester];
                                money -= data[Unit_Harvester][houseID].price;
                                orderedSpiceHarvester = true;
                            }
                        }
						else if ((money > 10000) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								doUpgrade(pBuilder);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (gameMode == GameMode::Custom && !vanillaEconomy && !citySimEnabled
							&& pBuilder->isAvailableToBuild(Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] < std::min(8, money / 4000)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__, "construction_capacity")) {
                                itemCount[Unit_MCV]++;
                                money -= data[Unit_MCV][houseID].price;
                            }
						}
						else if (gameMode == GameMode::Custom
							&& !vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
							&& pBuilder->isAvailableToBuild(Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Unit_Harvester] < militaryValue / 1000
							&& itemCount[Unit_Harvester] < harvesterLimit) {
							// In case we get given lots of money, it will eventually run out so we need to be prepared
							// Classic-mode harvester expansion; city spice investment is handled above.
							produceItemWithLogging(Unit_Harvester, __LINE__);
							itemCount[Unit_Harvester]++;
						}
						else if (!vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
							&& itemCount[Unit_Harvester] < harvesterLimit
							&& pBuilder->isAvailableToBuild(Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& (money < 2000 || gameMode == GameMode::Campaign)) {
							// Classic-mode recovery; city spice investment is handled above.
							produceItemWithLogging(Unit_Harvester, __LINE__);
							itemCount[Unit_Harvester]++;
						}
						else if ((money > 500) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							// Upgrade before military — unlocks MCV(1), Launcher(2), SiegeTank(3)
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								doUpgrade(pBuilder);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (money > (citySimEnabled || vanillaEconomy ? 500 : 2000)
							&& militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached()) {
                            const std::array<Uint32,6> types = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                                Unit_Devastator, Unit_SonicTank, Unit_Deviator};
                            const int specialValue = data[Unit_Devastator][houseID].price * itemCount[Unit_Devastator]
                                + data[Unit_SonicTank][houseID].price * itemCount[Unit_SonicTank]
                                + data[Unit_Deviator][houseID].price * itemCount[Unit_Deviator];
                            const std::array<FixPoint,6> targets = {tankPercent,siegePercent,launcherPercent,
                                specialPercent,specialPercent,specialPercent};
                            std::array<QuantBotBuildPolicy::AllocationCandidate,6> candidates;
                            AITelemetry::Record choices;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item = types[i];
                                candidates[i] = {data[item][houseID].price,
                                    i>=3 ? specialValue : data[item][houseID].price * itemCount[item],
                                    (targets[i]*10000).lround(), pBuilder->isAvailableToBuild(item)};
                            }
                            const int normalHorizon = vehiclePlanValue;
                            const int expansionHorizon = vehiclePlanValue;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item=types[i];
                                const auto& c = candidates[i];
                                choices.set(std::to_string(item), AITelemetry::Record().set("price",c.price)
                                    .set("committed_value",c.committedValue).set("target_bps",c.targetBps)
                                    .set("available",c.available).set("affordable",c.price<=money)
                                    .set("deficit_scaled",int64_t(normalHorizon)*c.targetBps-int64_t(c.committedValue)*10000)
                                    .set("expansion_deficit_scaled",int64_t(expansionHorizon)*c.targetBps-int64_t(c.committedValue)*10000));
                            }
                            int selected = QuantBotBuildPolicy::fundedDeficit(
                                candidates,militaryValue,money,militaryValueLimit,vehiclePlanValue);
                            const bool expansionFallback = selected < 0;
                            if (expansionFallback) selected = QuantBotBuildPolicy::capacityFill(
                                candidates,militaryValue,money,militaryValueLimit);
                            // Candidate tables are useful when the choice changes, but logging the
                            // identical no-deficit decision for every idle factory rapidly exhausts
                            // the match capture. Keep a periodic heartbeat for diagnosis.
                            const Uint32 allocationCycle = getGameCycleCount();
                            const uint64_t allocationSignature = (uint64_t(selected + 1) << 1)
                                | static_cast<uint64_t>(expansionFallback);
                            const auto traceIt = lastHeavyAllocationTrace.find(pBuilder->getObjectID());
                            const bool allocationChanged = traceIt == lastHeavyAllocationTrace.end()
                                || traceIt->second.first != allocationSignature
                                || allocationCycle - traceIt->second.second >= MILLI2CYCLES(30000);
                            if (allocationChanged) {
                                traceDecision("heavy_allocation_decision",AITelemetry::Record()
                                    .set("builder",pBuilder->getObjectID()).set("army_value",militaryValue)
                                    .set("army_limit",militaryValueLimit).set("horizon_value",normalHorizon)
                                    .set("expansion_horizon",expansionHorizon).set("expansion_fallback",expansionFallback)
                                    .set("spendable",money).set("candidates",choices)
                                    .set("selected",selected<0 ? NONE_ID : types[selected])
                                    .set("reason",selected<0 ? "no_affordable_capacity"
                                        : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit"));
                                lastHeavyAllocationTrace[pBuilder->getObjectID()] = {allocationSignature, allocationCycle};
                            }
                            if (selected>=0 && produceItemWithLogging(types[selected],__LINE__,
                                expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit")) {
                                ++itemCount[types[selected]];
                                money -= candidates[selected].price;
                                militaryValue += candidates[selected].price;
                            }
						}
					}

				} break;

				case Structure_StarPort: {
					const StarPort* pStarPort = static_cast<const StarPort*>(pBuilder);
					if (pStarPort->okToOrder()) {
						const Choam& choam = getHouse()->getChoam();

						// We need a construction yard!!
						if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
							&& pStarPort->isAvailableToBuild(Unit_MCV)
							&& choam.getNumAvailable(Unit_MCV) > 0
							&& itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] < 1) {
							produceItemWithLogging(Unit_MCV, __LINE__);
							itemCount[Unit_MCV]++;
							money = money - choam.getPrice(Unit_MCV);
						}

						if (money > choam.getPrice(Unit_Carryall) && choam.getNumAvailable(Unit_Carryall) > 0 && itemCount[Unit_Carryall] == 0) {
							// Get at least one Carryall
							produceItemWithLogging(Unit_Carryall, __LINE__);
							itemCount[Unit_Carryall]++;
							money = money - choam.getPrice(Unit_Carryall);
						}

						while (money > choam.getPrice(Unit_Harvester) && choam.getNumAvailable(Unit_Harvester) > 0 && itemCount[Unit_Harvester] < harvesterLimit) {
							produceItemWithLogging(Unit_Harvester, __LINE__);
							itemCount[Unit_Harvester]++;
							money = money - choam.getPrice(Unit_Harvester);
						}

						int itemCountUnits = itemCount[Unit_Tank] + itemCount[Unit_SiegeTank] + itemCount[Unit_Launcher] + itemCount[Unit_Harvester];

						while (money > choam.getPrice(Unit_Carryall) && choam.getNumAvailable(Unit_Carryall) > 0 && itemCount[Unit_Carryall] < itemCountUnits / 7) {
							produceItemWithLogging(Unit_Carryall, __LINE__);
							itemCount[Unit_Carryall]++;
							money = money - choam.getPrice(Unit_Carryall);
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_SiegeTank) && choam.getNumAvailable(Unit_SiegeTank) > 0
							&& choam.isCheap(Unit_SiegeTank) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_SiegeTank, __LINE__);
							itemCount[Unit_SiegeTank]++;
							money = money - choam.getPrice(Unit_SiegeTank);
							militaryValue += data[Unit_SiegeTank][houseID].price;
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Launcher) && choam.getNumAvailable(Unit_Launcher) > 0
							&& choam.isCheap(Unit_Launcher) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Launcher, __LINE__);
							itemCount[Unit_Launcher]++;
							money = money - choam.getPrice(Unit_Launcher);
							militaryValue += data[Unit_Launcher][houseID].price;
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Tank) && choam.getNumAvailable(Unit_Tank) > 0
							&& choam.isCheap(Unit_Tank) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Tank, __LINE__);
							itemCount[Unit_Tank]++;
							money = money - choam.getPrice(Unit_Tank);
							militaryValue += data[Unit_Tank][houseID].price;
						}



						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Ornithopter) && choam.getNumAvailable(Unit_Ornithopter) > 0
							&& choam.isCheap(Unit_Ornithopter) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Ornithopter, __LINE__);
							itemCount[Unit_Ornithopter]++;
							money = money - choam.getPrice(Unit_Ornithopter);
							militaryValue += data[Unit_Ornithopter][houseID].price;
						}



						doPlaceOrder(pStarPort);
					}

				} break;

				case Structure_ConstructionYard: {

				const ConstructionYard* pConstYard = static_cast<const ConstructionYard*>(pBuilder);

                // Compare the funded next-wave mix with fielded AND queued units.
                // Cash targets must not hide a heavy-unit production bottleneck.
                const int fundedArmy = vehiclePlanValue;
                int heavyValue = 0;
                for (const auto unit : {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                        Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    heavyValue += itemCount[unit] * data[unit][houseID].price;
                const int heavyDeficit = std::max(0,
                    (fundedArmy * (tankPercent + siegePercent + launcherPercent + specialPercent)).lround() - heavyValue);
                const int lightDeficit = std::max<int64_t>(0,int64_t(fundedArmy)*lightVehicleBps/10000-lightVehicleValue);
                const bool lightBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_LightFactory),
                        itemCount[Structure_LightFactory],activeLightFactoryCount,lightDeficit,
                        money,economyReserve,data[Structure_LightFactory][houseID].price);
                const int airDeficit = std::max(0, (fundedArmy * ornithopterPercent).lround()
                    - itemCount[Unit_Ornithopter] * data[Unit_Ornithopter][houseID].price);
                const bool heavyBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_HeavyFactory),
                        itemCount[Structure_HeavyFactory], activeHeavyFactoryCount, heavyDeficit,
                        money, economyReserve, data[Structure_HeavyFactory][houseID].price);
                const bool airBacklog = QuantBotBuildPolicy::allAirFactoriesBuildingOrnithopters(
                        getHouse()->getNumItems(Structure_HighTechFactory), itemCount[Structure_HighTechFactory], ornithopterFactoryCount)
                    && !getHouse()->isAirUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_HighTechFactory),
                        itemCount[Structure_HighTechFactory], activeHighTechFactoryCount, airDeficit,
                        money, economyReserve, data[Structure_HighTechFactory][houseID].price);

				// Each yard owns its concrete/structure placement sequence. Sharing
				// a single FIFO lets the faster yard consume the other's locations.
				planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false);
                auto& placeLocations = builderPlaceLocations[pBuilder->getObjectID()];
				if (pBuilder->getProductionQueueSize() == 0) placeLocations.clear();
				if (emitStatsLog) {
					logDebug("PRODUCTION: CY=%u upgrading=%d queue=%d hold=%d credits=%d buildList=%d R/C/I=%d/%d/%d reserve=%u/%d",
						pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
						pBuilder->isOnHold(), money, pBuilder->getBuildListSize(),
						itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
						itemCount[Structure_ZoneIndustrial], strategicReserveItem, strategicReserveCost);
					auto* balanceCity = currentGame ? currentGame->getCitySimulation() : nullptr;
					const int taxIncome = citySimEnabled
						? DuneCity::computeAnnualTaxRevenue(ownTotalPop, balanceCity ? balanceCity->getCityTax() : 7, ownAvgLandValue) / 60 : 0;
					int factoryTarget = QuantBotBuildPolicy::desiredHeavyFactories(citySimEnabled, taxIncome, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());
                    if (vanillaEconomy) factoryTarget = DuneCity::vanillaFactoryTarget(factoryTarget, getHouse()->getNumItems(Unit_Harvester), money);
					const int tech = currentGame ? currentGame->techLevel : 8;
					const bool policyPrerequisites = citySimEnabled || vanillaEconomy || tech <= 4
						|| (itemCount[Structure_RepairYard] > 0 && (tech <= 6 || itemCount[Structure_IX] > 0));
					const char* factoryReason = !pBuilder->isAvailableToBuild(Structure_HeavyFactory) ? "unavailable"
						: money <= std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) ? "cash-reserve"
						: militaryValue >= militaryValueLimit ? "military-limit"
						: getHouse()->isGroundUnitLimitReached() ? "unit-limit"
						: heavyBacklog && itemCount[Structure_HeavyFactory] < 24 ? "funded-unit-backlog"
						: itemCount[Structure_HeavyFactory] >= factoryTarget ? "target-met"
						: !policyPrerequisites ? "tech-policy" : "expansion-due";
                    traceDecision("builder_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("queue", pBuilder->getProductionQueueSize()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("heavy_target", factoryTarget)
                        .set("heavy_reason", factoryReason).set("heavy_busy", activeHeavyFactoryCount)
                        .set("heavy_deficit", heavyDeficit).set("air_deficit", airDeficit)
                        .set("heavy_backlog", heavyBacklog).set("air_backlog", airBacklog)
                        .set("light_backlog",lightBacklog).set("light_busy",activeLightFactoryCount).set("light_deficit",lightDeficit)
                        .set("high_tech_busy", activeHighTechFactoryCount)
                        .set("high_tech_building_ornithopters", ornithopterFactoryCount)
                        .set("heavy_economy_target", vanillaEconomy ? std::max(1, getHouse()->getNumItems(Unit_Harvester) / 3) : 0)
                        .set("heavy_cash_target", vanillaEconomy ? 1 + std::max(0, money - 10000) / 4000 : 0)
                        .set("heavy_opening_target", vanillaEconomy ? std::min(factoryTarget, 2 * std::clamp(itemCount[Structure_ConstructionYard], 1, 8)) : 0)
                        .set("heavy_losses_2min", recentFactoryLossCount())
                        .set("repair_busy", activeRepairYardCount).set("repair_cap", QuantBotBuildPolicy::repairYardCap(getHouse()->getNumItems(Structure_HeavyFactory)))
                        .set("state", decisionState()));
					logDebug("BUILD-BALANCE: CY=%u HF=%d queued=%d busy=%d target=%d reason=%s RY=%d queued=%d busy=%d cap=%d taxPerSec=%d power=%d/%d",
						pBuilder->getObjectID(), getHouse()->getNumItems(Structure_HeavyFactory),
						itemCount[Structure_HeavyFactory] - getHouse()->getNumItems(Structure_HeavyFactory),
						activeHeavyFactoryCount, factoryTarget, factoryReason,
						getHouse()->getNumItems(Structure_RepairYard),
						itemCount[Structure_RepairYard] - getHouse()->getNumItems(Structure_RepairYard),
						activeRepairYardCount, QuantBotBuildPolicy::repairYardCap(getHouse()->getNumItems(Structure_HeavyFactory)),
						taxIncome, getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
				}

					if (!pBuilder->isUpgrading() && getHouse()->getCredits() > 100 && (pBuilder->getProductionQueueSize() < 1) && pBuilder->getBuildListSize()) {

						// Campaign Build order, iterate through the buildings, if the number that exist
						// is less than the number that should exist, then build the one that is missing

						if (gameMode == GameMode::Campaign && difficulty != Difficulty::Brutal) {
							//logDebug("GameMode Campaign.. ");

						for (int i = Structure_FirstID; i <= Structure_LastID; i++) {
							if (itemCount[i] < initialItemCount[i]
								&& pBuilder->isAvailableToBuild(i)
								&& findPlaceLocation(i).isValid()
								&& !pBuilder->isUpgrading()
								&& pBuilder->getProductionQueueSize() < 1) {

								logDebug("***CampAI Build itemID: %o structure count: %o, initial count: %o", i, itemCount[i], initialItemCount[i]);
								produceItemWithLogging(i, __LINE__);
								itemCount[i]++;  // Increment immediately to prevent multiple CYs from building same item
							}
						}

							// If Campaign AI can't build military, let it build up its cash reserves and defenses

							if (pStructure->getHealth() < pStructure->getMaxHealth()) {
								doRepair(pBuilder);
								int health = pStructure->getHealth().lround();
								int maxHealth = pStructure->getMaxHealth();
								logDebug("PRODUCTION: Repairing CY, health: %d/%d", health, maxHealth);
							}
							else if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
								&& !pBuilder->isUpgrading()
								&& itemCount[Unit_Harvester] >= harvesterLimit
								&& money > 1500) {  // Don't upgrade if low on money (need money for structures/units)

								doUpgrade(pBuilder);
								logDebug("PRODUCTION: Upgrading CY to level %d, credits: %d", pBuilder->getCurrentUpgradeLevel() + 1, money);
							}
							else if ((!getHouse()->hasPower())
								&& pBuilder->getProductionQueueSize() == 0) {
								// Prefer nuclear plant over windtrap
								if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
									&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
									produceItemWithLogging(Structure_NuclearPlant, __LINE__);
									itemCount[Structure_NuclearPlant]++;
									logDebug("***CampAI Build Nuclear Plant: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
									&& findPlaceLocation(Structure_WindTrap).isValid()) {
									produceItemWithLogging(Structure_WindTrap, __LINE__);
									itemCount[Structure_WindTrap]++;
									logDebug("***CampAI Build windtrap: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								}
							}
							else if ((getHouse()->getStoredCredits() > getHouse()->getCapacity() * 0.90_fix)  // Only build when 90% full
								&& pBuilder->isAvailableToBuild(Structure_Silo)
								&& findPlaceLocation(Structure_Silo).isValid()
								&& pBuilder->getProductionQueueSize() == 0) {

								produceItemWithLogging(Structure_Silo, __LINE__);
								itemCount[Structure_Silo]++;

								logDebug("***CampAI Build A new Silo increasing count to: %d (credits: %d/%d)", itemCount[Structure_Silo], getHouse()->getStoredCredits().lround(), getHouse()->getCapacity());
							}
							else if (money > 3000
								&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
								&& pBuilder->getProductionQueueSize() == 0
								&& (itemCount[Structure_RocketTurret] <
									(itemCount[Structure_Silo] + itemCount[Structure_Refinery]) * 2)
								&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {

								produceItemWithLogging(Structure_RocketTurret, __LINE__);
								itemCount[Structure_RocketTurret]++;

								logDebug("***CampAI Build A new Rocket turret increasing count to: %d", itemCount[Structure_RocketTurret]);
							}
							// City zone structures for campaign AI — pick by live demand AND ratio.
							else if (currentGame && currentGame->isCitySimEnabled()
								&& money > 200
								&& pBuilder->getProductionQueueSize() == 0
								&& itemCount[Structure_WindTrap] > 0) {
								const int resCount = itemCount[Structure_ZoneResidential];
								const int comCount = itemCount[Structure_ZoneCommercial];
								const int indCount = itemCount[Structure_ZoneIndustrial];
								const Uint32 zoneID = chooseCityZone(pBuilder, false);

								if (zoneID != NONE_ID && pBuilder->isAvailableToBuild(zoneID)
									&& findPlaceLocation(zoneID).isValid()) {
									produceItemWithLogging(zoneID, __LINE__);
									itemCount[zoneID]++;
									logDebug("***CampAI CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d)",
										getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
										ownResValve, ownComValve, ownIndValve);
								}
							}

							// MULTIPLAYER FIX: Use deterministic timer instead of random
							buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
						}
						else {
								// custom AI starts here:

								Uint32 itemID = NONE_ID;
                const char* structureRule = "no_eligible_structure";
                Coord crimeServiceSite = Coord::Invalid();
								bool skipRemainingStructureLogic = false;

				// Skip build order if something is already queued
								if (pBuilder->getProductionQueueSize() > 0) {
									skipRemainingStructureLogic = true;
								}

							// Count enemy ornithopters - use MAXIMUM from a single enemy house, not sum
								int maxEnemyOrnithopters = 0;
								int totalEnemyOrnithopters = 0;
								if (currentGame) {
								for (int i = 0; i < NUM_HOUSES; i++) {
									const House* pHouse = currentGame->getHouse(i);
									if (pHouse && pHouse->getTeamID() != getHouse()->getTeamID()) {
										int houseOrnis = pHouse->getNumItems(Unit_Ornithopter);
										totalEnemyOrnithopters += houseOrnis;
										if (houseOrnis > maxEnemyOrnithopters) {
											maxEnemyOrnithopters = houseOrnis;
										}
									}
									}
								}
								const int requiredTurrets = std::max(maxEnemyOrnithopters * 2, totalEnemyOrnithopters);
								const bool ixExpected = data[Structure_IX][houseID].enabled
									&& data[Structure_IX][houseID].techLevel <= currentGame->techLevel;
								const bool palaceExpected = data[Structure_Palace][houseID].enabled
									&& data[Structure_Palace][houseID].techLevel <= currentGame->techLevel;
								const bool strategicInfrastructureIncomplete = customStrategicPlanning
									&& ((ixExpected && itemCount[Structure_IX] == 0)
										|| (palaceExpected && itemCount[Structure_Palace] == 0));
								const int activeRocketTurretGoal = strategicInfrastructureIncomplete
									? std::min(requiredTurrets, 2)
									: requiredTurrets;

								// Power buffer check for rocket turrets (2 windtraps = 200 power buffer + 25 turret = 225)
								// Only applies if rocketTurretsNeedPower is enabled
								auto hasPowerBufferForTurret = [&]() {
									if (!turretPowerRequired) {
										return true; // No power requirement, always allow
									}
									int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
									// Need 225 (200 buffer + 25 turret cost) so we maintain 200 after building
									return powerExcess >= 225;
								};

								// CRITICAL: Counter enemy ornithopters ASAP (prep prerequisites if needed)
								if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& maxEnemyOrnithopters > 0
									&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal) {
								bool hasWindtrap = itemCount[Structure_WindTrap] > 0;
								bool hasRadar = itemCount[Structure_Radar] > 0;

							if (pBuilder->getCurrentUpgradeLevel() < 2) {
							if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
								doRepair(pBuilder);
											logDebug("COUNTER-ORNITHOPTER: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
										} else if (!pBuilder->isUpgrading() && pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								doUpgrade(pBuilder);
											logDebug("COUNTER-ORNITHOPTER: Upgrading CY (level %d -> %d)", pBuilder->getCurrentUpgradeLevel(), pBuilder->getCurrentUpgradeLevel() + 1);
							}
									} else if (!hasWindtrap && (!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
										logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasRadar && pBuilder->isAvailableToBuild(Structure_Radar) && getHouse()->hasPower()) {
						itemID = Structure_Radar; structureRule = "radar_prerequisite";
										logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasPowerBufferForTurret()) {
										int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
										if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
											&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
											itemID = Structure_NuclearPlant; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
										} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
											&& findPlaceLocation(Structure_WindTrap).isValid()) {
											itemID = Structure_WindTrap; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
										}
									} else if (pBuilder->isAvailableToBuild(Structure_RocketTurret)
							&& hasPowerBufferForTurret()
							            && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
							itemID = Structure_RocketTurret; structureRule = "rocket_defense";
										logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", maxEnemyOrnithopters, activeRocketTurretGoal);
									}
								}

								// Essential infrastructure - Build Order:
								// 1. WindTrap (if 0)
								// 2. Refinery (if 0)
								// 3. Refinery (ratio with harvesters)
								// 4. Refinery (< 4, money < 2000)
				// 5. StarPort (skip if nothing in CHOAM and no heavy factory)
				// 6. Radar
				// 7. Light Factory
				// 8. Repair Yard (if starport or heavy factory exists)
				// 8b. 2 Rocket Turrets (if starport or heavy factory exists)
				// 8c. Counter ornithopters (turrets < 2x max enemy ornis)
				// 9. Heavy Factory (money > 500)
				// 10. High Tech Factory (if no carryalls in CHOAM or no starport)

				// 1. WindTrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_WindTrap] == 0 
					&& (!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
					}
				// 1b. Power Deficit Recovery - prefer nuclear plant, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& (!getHouse()->hasPower() || (turretPowerRequired
                        && itemCount[Structure_RocketTurret] > 0
                        && getHouse()->getProducedPower() < getHouse()->getPowerRequirement()))) {
					int powerDeficit = getHouse()->getPowerRequirement() - getHouse()->getProducedPower();
					if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
						&& money >= data[Structure_NuclearPlant][houseID].price
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
					} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
					}
				}
                // 1c. Cover zone maturation/recovery and queued consumers as
                // well as the normal reserve. Keep one generator in flight.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
                    && itemCount[Structure_Refinery] >= QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget)) {
					const int produced  = getHouse()->getProducedPower();
					const int required  = getHouse()->getPowerRequirement();
					const int buffer    = produced - required;
					const int targetBuffer = cityPowerReserve;
					if (required > 0 && buffer < targetBuffer) {
						// Prefer an affordable nuclear plant for capacity; the shared
						// investment policy below retains cheap wind for small starts.
						if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
							&& money >= data[Structure_NuclearPlant][houseID].price
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
									 buffer, targetBuffer, required);
						} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d)",
									 buffer, targetBuffer);
						}
					}
				}
				// Low-spice economy: skip additional refineries, pivot to R/I/C zones
				const bool lowSpiceEconomy = (lastCalculatedSpice < 500);
				const bool isCitySim = (currentGame && currentGame->isCitySimEnabled());

                // The first refinery supplies income and unlocks the tech tree.
                // Further processing capacity competes with demanded tax growth.
                const int openingRefineries = QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget);
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] == 0
                    && pBuilder->isAvailableToBuild(Structure_Refinery)
                    && findPlaceLocation(Structure_Refinery).isValid()) {
                    skipRemainingStructureLogic = true;
                    structureRule = "city_saving_for_spice_opening";
                    if (money >= data[Structure_Refinery][houseID].price) {
                        itemID = Structure_Refinery;
                        structureRule = "city_spice_opening";
                        if (itemCount[Unit_Harvester] < harvesterLimit) ++itemCount[Unit_Harvester];
                    }
                    if (emitStatsLog) traceDecision("city_spice_opening", AITelemetry::Record()
                        .set("target",openingRefineries).set("refineries",itemCount[Structure_Refinery])
                        .set("spice_share",spiceShare).set("credits",money)
                        .set("price",data[Structure_Refinery][houseID].price).set("funded",itemID != NONE_ID));
                }

                // One demanded residential plot hedges spice income immediately.
                // After that, buy the better return until the small opening
                // economy exists; don't force a commercial/industrial seed.
                const int openingZones = itemCount[Structure_ZoneResidential]
                    + itemCount[Structure_ZoneCommercial] + itemCount[Structure_ZoneIndustrial];
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && ((itemCount[Structure_ZoneResidential] == 0 && ownResValve>0)
                        || (openingZones<6 && itemCount[Structure_Refinery]<openingRefineries))) {
                    const Uint32 investment = chooseCityEconomy(pBuilder,true);
                    if (investment != NONE_ID) {
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_opening_investment";
                        if (money>=data[investment][houseID].price) {
                            itemID=investment;
                            structureRule="city_opening_investment";
                        }
                    }
                }

                // After the seed economy, save for the first vehicle factory and
                // its available prerequisite. Cheap zoning must not spend that
                // money afresh on every tick. Unavailable/unplaceable tech does
                // not reserve cash, and essential power above still takes priority.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && (itemCount[Structure_ZoneResidential] > 0 || ownResValve<=0)) {
                    for (Uint32 candidate : {Structure_HeavyFactory, Structure_Radar, Structure_LightFactory}) {
                        if (itemCount[candidate] > 0 || !pBuilder->isAvailableToBuild(candidate)
                            || !findPlaceLocation(candidate).isValid()) continue;
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_vehicle_production";
                        if (money >= data[candidate][houseID].price) {
                            itemID = candidate;
                            structureRule = "city_vehicle_production_bootstrap";
                        }
                        if (emitStatsLog) traceDecision("city_bootstrap_reserve", AITelemetry::Record()
                            .set("item",candidate).set("price",data[candidate][houseID].price)
                            .set("credits",money).set("funded",itemID != NONE_ID));
                        break;
                    }
                }

                // Ongoing economic expansion uses the same comparison after
                // essential services and military infrastructure below.
                int developedZones = 0, dangerousZones = 0;
                if (isCitySim) {
                    const auto* sim = currentGame->getCitySimulation();
                    if (sim && sim->isInitialized()) {
                        for (const auto* structure : getStructureList()) {
                            const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
                            if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) continue;
                            const Coord p = zone->getLocation();
                            if (!getMap().tileExists(p.x, p.y)
                                || DuneCity::getStructurePopulation(zone,getMap().getTile(p.x,p.y)->getCityZoneDensity()) == 0) continue;
                            ++developedZones;
                            if (sim->getCrimeRateMap().worldGet(p.x, p.y) >= 192) ++dangerousZones;
                        }
                    }
                }
                const unsigned serviceInterval = QuantBotBuildPolicy::crimeServiceOrderInterval(
                    developedZones, dangerousZones);
                bool serviceEvaluated = false;
                auto selectCrimeService = [&](const char* rule) {
                    if (itemID != NONE_ID || skipRemainingStructureLogic || !isCitySim || serviceEvaluated) return;
                    serviceEvaluated = true;
                    if (selectCityServiceInvestment(pBuilder, money, serviceInterval > 0, itemID, crimeServiceSite))
                        structureRule = rule;
                };
                // A turret that repays its entire cost through land-value tax
                // alone earns an early investment slot when it also reduces
                // existing crime and improves residential/commercial value.
                // Keep cash for harvesters/refineries and a combat unit; cap the
                // frequency so it cannot monopolise the construction yards.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_HeavyFactory] > 0
                    && serviceInterval != 2 // Widespread dangerous crime still gets the strongest service comparison first.
                    && QuantBotBuildPolicy::crimeServiceOrderDue(3, nonServiceConstructionOrders)
                    && selectCityServiceInvestment(pBuilder,
                        std::max(0,money-cityWorkingReserve-queuedProductionCost), false,
                        itemID, crimeServiceSite, true)) {
                    structureRule = "turret_land_value_investment";
                }
                if (QuantBotBuildPolicy::crimeServiceOrderDue(serviceInterval, nonServiceConstructionOrders))
                    selectCrimeService("city_crime_reserved_order");
				// 2. Refinery (if 0) — non-city-sim path; city sim handles refineries above.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& itemCount[Structure_Refinery] == 0
					&& pBuilder->isAvailableToBuild(Structure_Refinery)) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
					if (itemCount[Unit_Harvester] < harvesterLimit) {
						itemCount[Unit_Harvester]++;
					}
				}

                // Establish repairs before more factories/tech consume the opening
                // grant. Count queued yards to avoid duplicate orders from parallel CYs.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && gameMode == GameMode::Custom
                    && itemCount[Structure_RepairYard] < QuantBotBuildPolicy::baselineRepairYards(
                        getHouse()->getNumItems(Structure_HeavyFactory), militaryValue)
                    && getHouse()->getNumItems(Structure_Refinery) > 0
                    && money >= data[Structure_RepairYard][houseID].price + 1000
                    && pBuilder->isAvailableToBuild(Structure_RepairYard)
                    && findPlaceLocation(Structure_RepairYard).isValid()) {
                    itemID = Structure_RepairYard;
                    structureRule = "early_repair_capacity";
                }

                // Unlock carryalls and air production once the first heavy
                // factory is operational, before repeating the expansion loop.
                // Queued High Tech factories count, so other yards keep expanding.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && vanillaEconomy && gameMode == GameMode::Custom
                    && getHouse()->getNumItems(Structure_HeavyFactory) > 0
                    && itemCount[Structure_HighTechFactory] == 0
                    && money > std::max(2000, economyReserve + data[Structure_HighTechFactory][houseID].price)
                    && pBuilder->isAvailableToBuild(Structure_HighTechFactory)
                    && findPlaceLocation(Structure_HighTechFactory).isValid()) {
                    itemID = Structure_HighTechFactory; structureRule = "first_air_production";
                }

				// 3. Refinery (ratio: 1 refinery per 3 harvesters) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& !lowSpiceEconomy
					&& itemCount[Structure_Refinery] < (vanillaEconomy
                        ? QuantBotBuildPolicy::desiredSpiceRefineries(spiceHarvesterTarget, itemCount[Unit_Harvester])
                        : itemCount[Unit_Harvester] / 3)
			&& pBuilder->isAvailableToBuild(Structure_Refinery)
			&& !(gameMode == GameMode::Campaign && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
						if (itemCount[Unit_Harvester] < harvesterLimit) {
							itemCount[Unit_Harvester]++;
						}
					}
				// 4. Refinery (< 4, money < 2000) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
				&& !isCitySim
				&& !lowSpiceEconomy
				&& gameMode != GameMode::Campaign
				&& itemCount[Structure_Refinery] < 4
				&& pBuilder->isAvailableToBuild(Structure_Refinery)
					&& money < 2000) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
					if (itemCount[Unit_Harvester] < harvesterLimit) {
						itemCount[Unit_Harvester]++;
					}
				}
				// City income gate: in city sim mode, defer military infrastructure
				// (StarPort/Radar/LightFactory) until the city has at least the
				// seed economy in place. Uses zone *count*, not population —
				// zones stay at density 0 (and contribute 0 pop) until growth
				// conditions kick in, so a pop-based gate locks military out
				// indefinitely if the AI hasn't laid roads/supply yet.
				constexpr int kCityIncomeReadyZones = 3;
				const int kCityZoneCount = itemCount[Structure_ZoneResidential]
					+ itemCount[Structure_ZoneCommercial]
					+ itemCount[Structure_ZoneIndustrial];
				const bool cityIncomeReady = !isCitySim || kCityZoneCount >= kCityIncomeReadyZones || money > 3000;

                // Expand saturated light production before optional heavy capacity.
                // Pending factories count, so parallel yards add one lane at a time.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && lightBacklog
                    && pBuilder->isAvailableToBuild(Structure_LightFactory)
                    && findPlaceLocation(Structure_LightFactory).isValid()) {
                    itemID = Structure_LightFactory; structureRule = "light_unit_backlog";
                }
                // Wealthy vanilla openings need parallel MCV/troop production
                // before optional infrastructure. First/expanding refineries above
                // keep their priority; queued factories count towards this target.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && vanillaEconomy && gameMode == GameMode::Custom
                    && itemCount[Structure_Refinery] > 0
                    && militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached()
                    && pBuilder->isAvailableToBuild(Structure_HeavyFactory)) {
                    const int target = DuneCity::vanillaFactoryTarget(
                        QuantBotBuildPolicy::desiredHeavyFactories(false, 0, money,
                            getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount()),
                        getHouse()->getNumItems(Unit_Harvester), money);
                    if (DuneCity::prioritizeVanillaFactory(money, itemCount[Structure_HeavyFactory],
                            itemCount[Structure_ConstructionYard], target)
                        && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                        itemID = Structure_HeavyFactory; structureRule = "cash_factory_expansion";
                    }
                }
                if (itemID == NONE_ID && !skipRemainingStructureLogic && heavyBacklog
                    && itemCount[Structure_HeavyFactory] < 24
                    && pBuilder->isAvailableToBuild(Structure_HeavyFactory)
                    && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                    itemID = Structure_HeavyFactory; structureRule = "heavy_unit_backlog";
                }
				// 5. StarPort (skip if nothing available/enabled in CHOAM and no heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_StarPort] == 0
					&& pBuilder->isAvailableToBuild(Structure_StarPort) 
					&& findPlaceLocation(Structure_StarPort).isValid()
					&& (gameMode != GameMode::Campaign || money > 1000)
					&& [&]() {
						const auto& objData = currentGame->objectData.data;
						int houseID = getHouse()->getHouseID();
						bool hasUsefulStarportUnits = 
							(objData[Unit_Tank][houseID].enabled && getHouse()->getChoam().getNumAvailable(Unit_Tank) > 0) ||
							(objData[Unit_SiegeTank][houseID].enabled && getHouse()->getChoam().getNumAvailable(Unit_SiegeTank) > 0) ||
							(objData[Unit_Launcher][houseID].enabled && getHouse()->getChoam().getNumAvailable(Unit_Launcher) > 0) ||
							(objData[Unit_Harvester][houseID].enabled && getHouse()->getChoam().getNumAvailable(Unit_Harvester) > 0) ||
							(objData[Unit_Carryall][houseID].enabled && getHouse()->getChoam().getNumAvailable(Unit_Carryall) > 0);
						return (itemCount[Structure_HeavyFactory] > 0 || hasUsefulStarportUnits);
					}()) {
					itemID = Structure_StarPort; structureRule = "starport_supply";
				}
				// 6. Radar
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_Radar] == 0
					&& pBuilder->isAvailableToBuild(Structure_Radar)
					&& money > 500) {
					itemID = Structure_Radar; structureRule = "radar_prerequisite";
				}
				// 7. Light Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_LightFactory] == 0
					&& pBuilder->isAvailableToBuild(Structure_LightFactory)
					&& money > 500) {
					itemID = Structure_LightFactory; structureRule = "light_production";
				}
				// Custom vanilla invests in vehicle production; retain infantry
				// infrastructure for campaign and other-mod build policies.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& itemCount[Structure_Barracks] == 0
					&& pBuilder->isAvailableToBuild(Structure_Barracks)
					&& money > 400) {
					itemID = Structure_Barracks; structureRule = "infantry_production";
				}
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& itemCount[Structure_WOR] == 0
					&& pBuilder->isAvailableToBuild(Structure_WOR)
					&& money > 600) {
					itemID = Structure_WOR; structureRule = "infantry_production";
				}
				// 8. Repair Yard (only if starport or heavy factory exists)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] == 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->isAvailableToBuild(Structure_RepairYard)) {
					itemID = Structure_RepairYard; structureRule = "repair_capacity";
					logDebug("Build Repair Yard... money: %d", money);
				}
				// 8a. Upgrade CY to level 2 for rocket turrets
				//     City sim: upgrade early (no repair yard needed) so turrets protect the colony
				//     Non-city: requires repair yard + starport/heavy factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& pBuilder->getCurrentUpgradeLevel() < 2
					&& !pBuilder->isUpgrading()
					&& ((currentGame && currentGame->isCitySimEnabled() && money > 500)
						|| (itemCount[Structure_RepairYard] > 0
							&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
							&& itemCount[Structure_RocketTurret] < 2
                    && (!vanillaEconomy || money > economyReserve + data[Structure_RocketTurret][houseID].price)))) {
					if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
						doRepair(pBuilder);
						logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
					} else if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
						doUpgrade(pBuilder);
						logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", pBuilder->getCurrentUpgradeLevel() + 1);
					}
				}
                // Windtrap damage does not reduce output; repair for survival
                // uses the ordinary building-repair policy, not power recovery.
				// 8b-pre. Build power for turret buffer — prefer nuclear, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getGameInitSettings().getGameOptions().rocketTurretsNeedPower
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& !hasPowerBufferForTurret()) {
					int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: 225)", powerExcess);
					} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("TURRET-POWER: Windtrap for turret buffer (excess: %d, need: 225)", powerExcess);
					}
				}
				// 8b. Two baseline rocket turrets after repair yard (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& itemCount[Structure_RocketTurret] < 2
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", itemCount[Structure_RocketTurret] + 1);
				}
				// 8c. Counter enemy ornithopters (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& maxEnemyOrnithopters > 0
					&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
				}
				// 9. Heavy Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HeavyFactory] == 0
					&& pBuilder->isAvailableToBuild(Structure_HeavyFactory)
					&& money > 500) {
					itemID = Structure_HeavyFactory; structureRule = "heavy_production";
					logDebug("Build first Heavy Factory... money: %d", money);
				}
				// 10. High Tech Factory (first one - after heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HighTechFactory] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& pBuilder->isAvailableToBuild(Structure_HighTechFactory)
					&& money > 1000) {
					itemID = Structure_HighTechFactory; structureRule = "air_production";
					logDebug("Build first High Tech Factory... money: %d", money);
				}
				// 11. House IX (after essential production buildings)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0 
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_HighTechFactory] > 0
					&& itemCount[Structure_RepairYard] > 0
					&& pBuilder->isAvailableToBuild(Structure_IX) 
					&& money > 1000) {
					itemID = Structure_IX; structureRule = "advanced_tech";
					logDebug("Build IX... money: %d", money);
				}
				if (ixOverdue && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& stablePower
					&& money > 1000
					&& pBuilder->isAvailableToBuild(Structure_IX)
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_IX; structureRule = "advanced_tech";
				}
				// 12. Additional Heavy Factories (expansion).
				//     Income supports steady expansion; large cash surpluses fund
				//     extra tank capacity, bounded while military demand remains.
				//     Non-city uses the existing money/4000 target, also bounded.
				//     City mode uses actual build availability; classic prerequisites remain.
				//     Requirements outside city mode are progressive based on tech level:
				//     Tech 4: No prerequisites (just money and need)
				//     Tech 5-6: Require Repair Yard
				//     Tech 7+: Require Repair Yard + IX
				if (itemID == NONE_ID && !skipRemainingStructureLogic
								&& money > std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) && pBuilder->isAvailableToBuild(Structure_HeavyFactory)) {

								int creditsPerSec = 0;
								if (isCitySim) {
									auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
									int tax = citySim ? citySim->getCityTax() : 7;
									int32_t annual = DuneCity::computeAnnualTaxRevenue(ownTotalPop, tax, ownAvgLandValue);
									creditsPerSec = annual / 60;
								}
								int desiredHFs = QuantBotBuildPolicy::desiredHeavyFactories(isCitySim, creditsPerSec, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());

                                if (vanillaEconomy) desiredHFs = DuneCity::vanillaFactoryTarget(desiredHFs, getHouse()->getNumItems(Unit_Harvester), money);
								const bool needMore = itemCount[Structure_HeavyFactory] < desiredHFs
									&& militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached();

								if (needMore) {
									int techLevel = currentGame ? currentGame->techLevel : 8;
									bool prerequisitesMet = false;

									if (isCitySim || vanillaEconomy || techLevel <= 4) {
										// City and vanilla production use the actual tech tree, not an extra IX policy gate.
										prerequisitesMet = true;
									}
									else if (techLevel <= 6) {
										// Tech 5-6: Require Repair Yard
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1);
									}
									else {
										// Tech 7+: Require both Repair Yard and IX
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1 && itemCount[Structure_IX] >= 1);
									}

									if (prerequisitesMet) {
										itemID = Structure_HeavyFactory; structureRule = "heavy_production";
										logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  desired: %d  tech: %d",
											activeHeavyFactoryCount, getHouse()->getNumItems(Structure_HeavyFactory), money, desiredHFs, techLevel);
									}
								}
							}
				// 13. Vanilla refinery ratio. City mode compares capacity and tax returns.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
						&& !lowSpiceEconomy
						&& ((itemCount[Structure_Refinery] * 3.5_fix < itemCount[Unit_Harvester])
					|| (currentGame && currentGame->techLevel < 4))
						&& pBuilder->isAvailableToBuild(Structure_Refinery)
						&& !(gameMode == GameMode::Campaign && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
						// Only increment if below limit (free harvester will only spawn if below limit)
						if (itemCount[Unit_Harvester] < harvesterLimit) {
							itemCount[Unit_Harvester]++;
						}
					}
				// 14. Expand repair only when existing capacity is busy and production supports it.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
							&& pBuilder->isAvailableToBuild(Structure_RepairYard) && money > std::max(2000, economyReserve + data[Structure_RepairYard][houseID].price)
							&& QuantBotBuildPolicy::needsExtraRepairYard(itemCount[Structure_RepairYard],
								activeRepairYardCount, getHouse()->getNumItems(Structure_HeavyFactory), militaryValue)) {
							itemID = Structure_RepairYard; structureRule = "repair_capacity";
							logDebug("Build Repair Yard: have=%d busy=%d cap=%d military=%d", itemCount[Structure_RepairYard], activeRepairYardCount,
								QuantBotBuildPolicy::repairYardCap(getHouse()->getNumItems(Structure_HeavyFactory)), militaryValue);
						}
				// 15. Extra air capacity only for funded aircraft demand, after heavy backlog.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > std::max(3000, economyReserve + data[Structure_HighTechFactory][houseID].price) && pBuilder->isAvailableToBuild(Structure_HighTechFactory)
									&& itemCount[Structure_HighTechFactory] > 0
                                    && airBacklog && (!heavyBacklog || itemCount[Structure_HeavyFactory] >= 24)
                                    && activeHighTechFactoryCount >= itemCount[Structure_HighTechFactory]) {
									itemID = Structure_HighTechFactory; structureRule = "air_production";
								}
				// 16. Silos (when storage is 80%+ full)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_Silo] == getHouse()->getNumItems(Structure_Silo)
                    && itemCount[Structure_Refinery] == getHouse()->getNumItems(Structure_Refinery)
                    && getHouse()->getStoredCredits() > getHouse()->getCapacity() * 0.80_fix
					&& pBuilder->isAvailableToBuild(Structure_Silo)) {
									itemID = Structure_Silo; structureRule = "spice_storage";
					logDebug("Build Silo - storage at %d/%d", getHouse()->getStoredCredits().lround(), getHouse()->getCapacity());
								}
                selectCrimeService("city_service_investment");
                // Civic turrets use the shared investment comparison above.
				// 17b. Palace (after military infrastructure)
				//       City sim: 1 palace per 30000 population
				{
				const bool palaceAllowed = itemCount[Structure_Palace] < palaceTarget
					&& !orderedThisTick.count(Structure_Palace);
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > 5000
									&& pBuilder->isAvailableToBuild(Structure_Palace)
									&& palaceAllowed
									&& itemCount[Structure_HeavyFactory] > 0
									&& itemCount[Structure_LightFactory] > 0) {
								itemID = Structure_Palace; structureRule = "palace_strategy";
							}
				if (palaceOverdue && !skipRemainingStructureLogic
					&& stablePower
					&& money > 5000
					&& pBuilder->isAvailableToBuild(Structure_Palace)
					&& palaceAllowed
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_LightFactory] > 0
					&& itemID != Structure_Palace
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_Palace; structureRule = "palace_strategy";
				}
				}
				// Round out vanilla bases with regular turrets and short wall lines.
				// Fixed count targets keep this deterministic and prevent defence spam.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
					&& itemCount[Structure_HeavyFactory] > 0 && money > std::max(1000, economyReserve + 1000)
                    && (!vanillaEconomy || itemCount[Unit_Harvester] >= spiceHarvesterTarget)) {
					const int productionBuildings = itemCount[Structure_LightFactory]
						+ itemCount[Structure_HeavyFactory] + itemCount[Structure_Barracks]
						+ itemCount[Structure_WOR];
                    const bool rocketTech = data[Structure_RocketTurret][houseID].enabled
                        && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel;
                    const Uint32 defenceTurret = rocketTech ? Structure_RocketTurret : Structure_GunTurret;
                    const int desiredDefenceTurrets = 1 + productionBuildings / 3;
                    const int desiredWalls = 2 + (itemCount[Structure_GunTurret]
                        + itemCount[Structure_RocketTurret]) * 2;
                    if (itemCount[defenceTurret] < desiredDefenceTurrets
                        && pBuilder->isAvailableToBuild(defenceTurret)
                        && (!rocketTech || hasPowerBufferForTurret())
                        && findEffectiveTurretPlaceLocation(defenceTurret).isValid()) {
                        itemID = defenceTurret;
                        structureRule = rocketTech ? "rocket_defense" : "ground_defense";
					} else if (itemCount[Structure_Wall] < desiredWalls
						&& pBuilder->isAvailableToBuild(Structure_Wall)
						&& findPlaceLocation(Structure_Wall).isValid()) {
						itemID = Structure_Wall; structureRule = "ground_defense";
					}
				}
				// 18. City zone structures (when city sim is active)
				// Zones are 2x2 structures built via the CY; runZoneGrowth()
				// requires an actual structure object, so tile-flag placement
				// (CMD_CITY_PLACE_ZONE without a structure) does not work.
				// 18b. Civic buildings: Stadium (resPop > 500) and Airport (comPop > 20)
				//      Build these before more zones to unlock civic caps.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 500) {
					// Use AI's own population and civic building checks
					if (!ownHasStadium && itemCount[Structure_Stadium] == 0
						&& ownResPop > 500
						&& pBuilder->isAvailableToBuild(Structure_Stadium)
						&& findPlaceLocation(Structure_Stadium).isValid()) {
						itemID = Structure_Stadium; structureRule = "residential_civic";
						logDebug("CITY-CIVIC: Building Stadium (ownResPop=%d > 500, no stadium)",
							ownResPop);
					}
					else if (!ownHasAirport && itemCount[Structure_Airport] == 0
						&& ownComPop > 20
						&& pBuilder->isAvailableToBuild(Structure_Airport)
						&& findPlaceLocation(Structure_Airport).isValid()) {
						itemID = Structure_Airport; structureRule = "commercial_civic";
						logDebug("CITY-CIVIC: Building Airport (ownComPop=%d > 20, no airport)",
							ownComPop);
					}
				}

				// Rank zones by live demand and the R/I/C balance. Try the next
				// candidate if the preferred zone has no available building site.
				// In city sim, zones are the economic base — only windtrap is
				// required so the AI doesn't gate growth behind military
				// infrastructure that itself requires population (e.g. Starport
				// now needs 20000 pop). Outside city sim there's no zone path
				// here at all.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 200
					&& itemCount[Structure_WindTrap] > 0) {
					// Zones consume power as they grow. Before placing one,
					// ensure we have surplus power. If not, build a nuclear
					// plant (or windtrap fallback) first.
					constexpr int kZonePowerHeadroom = 24;  // worst case: industrial L3
					const int powerSurplus = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if (powerSurplus < kZonePowerHeadroom) {
						if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_NuclearPlant))
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						} else if ((!powerGenerationPending() && pBuilder->isAvailableToBuild(Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						}
					} else {
						const int resCount = itemCount[Structure_ZoneResidential];
						const int comCount = itemCount[Structure_ZoneCommercial];
						const int indCount = itemCount[Structure_ZoneIndustrial];
						const Uint32 zoneID = chooseCityEconomy(pBuilder, false);

						if (zoneID != NONE_ID && pBuilder->isAvailableToBuild(zoneID)
							&& findPlaceLocation(zoneID).isValid()) {
							itemID = zoneID; structureRule = "city_economy";
							logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d surplus=%d)",
								getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
								ownResValve, ownComValve, ownIndValve, powerSurplus);
						}
					}
				}

                // All city power branches share the same investment decision.
                // Include committed consumers, but never change vanilla policy.
                if (isCitySim && (itemID == Structure_NuclearPlant || itemID == Structure_WindTrap)
                    && itemCount[Structure_WindTrap] > 0) {
                    const int need = std::max(1,getHouse()->getPowerRequirement()
                        + cityPowerReserve - getHouse()->getProducedPower());
                    const Coord windSite = pBuilder->isAvailableToBuild(Structure_WindTrap)
                        ? findPlaceLocation(Structure_WindTrap) : Coord::Invalid();
                    const Coord nuclearSite = pBuilder->isAvailableToBuild(Structure_NuclearPlant)
                        ? findPlaceLocation(Structure_NuclearPlant) : Coord::Invalid();
                    const int windOutput = std::max(1,-data[Structure_WindTrap][houseID].power);
                    const int windNeeded = (need+windOutput-1)/windOutput;
                    std::vector<Coord> windSites;
                    const Coord size = getStructureSize(Structure_WindTrap);
                    if (windSite.isValid()) windSites.push_back(windSite);
                    // Count disjoint usable footprints only up to this order's
                    // demand. Overlapping candidate tiles are not spare land.
                    if (windSite.isValid() && nuclearSite.isValid()
                        && !QuantBotBuildPolicy::preferNuclearPower(need,windOutput,
                            data[Structure_WindTrap][houseID].price,data[Structure_NuclearPlant][houseID].price,
                            windNeeded,std::max(0,money-queuedProductionCost),
                            getHouse()->hasPower() ? cityWorkingReserve : 0,!getHouse()->hasPower())) {
                        for (int y=0; y<=getMap().getSizeY()-size.y && int(windSites.size())<windNeeded; ++y)
                            for (int x=0; x<=getMap().getSizeX()-size.x && int(windSites.size())<windNeeded; ++x) {
                                const Coord site(x,y);
                                if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,Structure_WindTrap)
                                    || overlapsReservedStructure(x,y,size.x,size.y) || dangerAt(site,size)>0
                                    || nearRecentStructureLoss(x,y,size.x,size.y)
                                    || !preservesGroundAccess(Structure_WindTrap,site)
                                    || !reactorClearance(Structure_WindTrap,site)
                                    || !cityRoadImpact(getMap(),x,y,size.x,size.y,Structure_WindTrap).preservesConnections
                                    || wouldLandlockNeighbouringZone(getMap(),houseID,x,y,size.x,size.y)) continue;
                                bool overlaps = false;
                                for (const Coord p : windSites)
                                    if (x<p.x+size.x && x+size.x>p.x && y<p.y+size.y && y+size.y>p.y) { overlaps=true; break; }
                                if (!overlaps) windSites.push_back(site);
                            }
                    }
                    const int reserveCash = getHouse()->hasPower() ? cityWorkingReserve : 0;
                    const int cash = std::max(0,money-queuedProductionCost);
                    const bool nuclear = nuclearSite.isValid() && QuantBotBuildPolicy::preferNuclearPower(
                        need,windOutput,data[Structure_WindTrap][houseID].price,
                        data[Structure_NuclearPlant][houseID].price,windSites.size(),cash,reserveCash,!getHouse()->hasPower());
                    itemID = nuclear ? Structure_NuclearPlant : windSite.isValid() ? Structure_WindTrap : NONE_ID;
                    if (itemID == NONE_ID) skipRemainingStructureLogic = true;
                    traceDecision("city_generator_choice", AITelemetry::Record().set("item",itemID)
                        .set("need",need).set("committed_demand",committedPowerDemand).set("cash",cash)
                        .set("forecast_growth",projectedPowerDemandGrowth).set("forecast_seconds",120)
                        .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
                        .set("zone_growth_headroom",zoneGrowthHeadroom).set("power_shortage",!getHouse()->hasPower())
                        .set("nuclear_available",pBuilder->isAvailableToBuild(Structure_NuclearPlant))
                        .set("nuclear_placement",placementScoreDetails[Structure_NuclearPlant])
                        .set("nuclear_site",nuclearSite.isValid()).set("nuclear_price",data[Structure_NuclearPlant][houseID].price)
                        .set("working_reserve",reserveCash).set("wind_needed",windNeeded)
                        .set("wind_sites",int(windSites.size())).set("nuclear",nuclear)
                        .set("industrial_demand",ownIndValve));
                }
			// Dedup: skip if another CY already ordered this unique structure
			// this tick. Zones and turrets are allowed in multiples.
			if (itemID != NONE_ID) {
				bool isMultiBuild = (itemID == Structure_ZoneResidential
					|| itemID == Structure_ZoneCommercial
					|| itemID == Structure_ZoneIndustrial
					|| itemID == Structure_RocketTurret
					|| itemID == Structure_GunTurret
					|| itemID == Structure_PoliceStation
					|| itemID == Structure_Wall
					|| itemID == Structure_Slab1);
				if (!isMultiBuild && orderedThisTick.count(itemID)) {
					logDebug("DEDUP: Skipping %s — already ordered by another CY this tick",
						getItemNameByID(itemID).c_str());
					if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "deduplicated"));
				itemID = NONE_ID;
				crimeServiceSite = Coord::Invalid();
				}
			}

			// Retry useful economic work when another yard claimed the strategic
			// order, or a chosen structure has no feasible footprint. Never
			// default to residential regardless of demand or power.
			if (itemID != NONE_ID && itemID != Structure_RocketTurret
				&& itemID != Structure_GunTurret && itemID != Structure_PoliceStation
                && !findPlaceLocation(itemID).isValid()) {
				if (emitStatsLog) logDebug("PRODUCTION: CY=%u no site for item=%u", pBuilder->getObjectID(), itemID);
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "no_site"));
				itemID = NONE_ID;
			}
			if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim && money > 200
				&& getHouse()->getProducedPower() - getHouse()->getPowerRequirement() >= 24
				&& itemCount[Structure_WindTrap] > 0) {
				itemID = chooseCityEconomy(pBuilder, false); structureRule = "city_economy_fallback";
			}

            if (isCitySim && itemID != NONE_ID
                && (structureRule == std::string("city_economy") || structureRule == std::string("city_economy_fallback"))
                && money < data[itemID][houseID].price) {
                itemID=NONE_ID;
                skipRemainingStructureLogic=true;
            }
            if (vanillaEconomy && itemID != NONE_ID && money < data[itemID][houseID].price) {
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "committed_cash")
                    .set("spendable", money).set("queued_production_cost", queuedProductionCost));
                itemID = NONE_ID;
            }
			if (emitStatsLog) logDebug("BUILD-CHOICE: CY=%u item=%u credits=%d skip=%d",
				pBuilder->getObjectID(), itemID, money, skipRemainingStructureLogic);
			Coord selectedPlaceLocation = Coord::Invalid();
			if (itemID != NONE_ID && pBuilder->isAvailableToBuild(itemID)) {
                if (crimeServiceSite.isValid()) selectedPlaceLocation = crimeServiceSite;
                else if (itemID == Structure_PoliceStation) {
                    // Legacy demand/emergency rules use the same bounded service
                    // scorer; no alternate full-map search bypasses its overlap costs.
                    Uint32 service = NONE_ID;
                    selectCityServiceInvestment(pBuilder,money,true,service,selectedPlaceLocation,false,itemID);
                } else selectedPlaceLocation = (itemID == Structure_RocketTurret || itemID == Structure_GunTurret)
                    ? findEffectiveTurretPlaceLocation(itemID) : findPlaceLocation(itemID);
			}

            if (AITelemetry::log().enabled() && (itemID != NONE_ID || emitStatsLog)) {
                AITelemetry::Record site;
                site.set("valid", selectedPlaceLocation.isValid()).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y);
                if (selectedPlaceLocation.isValid() && currentGame->getCitySimulation()) {
                    auto* sim = currentGame->getCitySimulation();
                    const int x = selectedPlaceLocation.x, y = selectedPlaceLocation.y;
                    const auto size = getStructureSize(itemID);
                    const auto roads = cityRoadImpact(getMap(), x, y, size.x, size.y, itemID);
                    if (placementCache.count(itemID) && placementCache[itemID] == selectedPlaceLocation)
                        site.set("placement_quality", placementScoreDetails[itemID]);
                    site.set("road_connections_preserved", roads.preservesConnections)
                        .set("roads_covered", roads.roadsCovered).set("redundant_roads_reused",roads.redundantRoadsCovered).set("junction_bonus", roads.junctionBonus);
                    site.set("pollution", sim->getPollutionDensityMap().worldGet(x, y))
                        .set("crime", sim->getCrimeRateMap().worldGet(x, y))
                        .set("traffic", sim->getTrafficDensityMap().worldGet(x, y))
                        .set("land_value", sim->getLandValueMap().worldGet(x, y));
                }
                traceDecision("construction_selection", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                    .set("item", itemID).set("rule", structureRule).set("skip", skipRemainingStructureLogic)
                    .set("state", decisionState()).set("site", site));
            }

			if (selectedPlaceLocation.isValid()) {
				// Pre-lay concrete only for specific structures that need max health:
				// - Heavy Factory (upgrades need full health)
				// - High Tech Factory (upgrades need full health)
				// - Windtraps (only for turret power buffer - need max power output)
				// - Rocket Turrets (need max health for defense)
				std::vector<Uint32> zonesToRemove;
                const bool redevelop = redevelopmentZones(itemID, selectedPlaceLocation, zonesToRemove);
				bool needsConcrete = !redevelop && getGameInitSettings().getGameOptions().concreteRequired
					&& (itemID == Structure_HeavyFactory
						|| itemID == Structure_HighTechFactory
						|| itemID == Structure_RocketTurret
						|| (itemID == Structure_WindTrap
							&& itemCount[Structure_RepairYard] > 0
							&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
							&& pBuilder->getCurrentUpgradeLevel() >= 2));

				if (needsConcrete) {
					Coord location = selectedPlaceLocation;
					Coord structureSize = getStructureSize(itemID);

					// Determine starting corner based on build range (like AIPlayer)
					int incI = 1, incJ = 1;
					int startI = location.x, startJ = location.y;

					if (getMap().isWithinBuildRange(location.x, location.y, getHouse())) {
						startI = location.x; startJ = location.y; incI = 1; incJ = 1;
					} else if (getMap().isWithinBuildRange(location.x + structureSize.x - 1, location.y, getHouse())) {
						startI = location.x + structureSize.x - 1; startJ = location.y; incI = -1; incJ = 1;
					} else if (getMap().isWithinBuildRange(location.x, location.y + structureSize.y - 1, getHouse())) {
						startI = location.x; startJ = location.y + structureSize.y - 1; incI = 1; incJ = -1;
					} else {
						startI = location.x + structureSize.x - 1; startJ = location.y + structureSize.y - 1; incI = -1; incJ = -1;
					}

                    // Bulk concrete is useful only when all four tiles are bare.
                    // A partial road/concrete foundation needs individual slabs.
                    const bool useSlab4 = QuantBotBuildPolicy::useBulkFoundation(
                        structureSize.x,structureSize.y,pBuilder->isAvailableToBuild(Structure_Slab4),
                        [&](int dx,int dy) { return getMap().getTile(location.x+dx,location.y+dy)->hasPreparedFoundation(); });
					// Queue concrete slabs for each tile, preferring Slab4 (2x2) when available
					for (int i = startI; abs(i - startI) < structureSize.x; i += incI) {
						for (int j = startJ; abs(j - startJ) < structureSize.y; j += incJ) {
							const Tile* pTile = getMap().getTile(i, j);

                            const int slabSize=QuantBotBuildPolicy::foundationSlabSize(
                                i-location.x,j-location.y,useSlab4,pTile->hasPreparedFoundation());
                            const Uint32 slab=slabSize==2 ? Structure_Slab4 : Structure_Slab1;
                            if (slabSize && pBuilder->isAvailableToBuild(slab)) {
                                placeLocations.emplace_back(i,j);
                                doProduceItem(pBuilder,slab);
                                logDebug("CONCRETE: Queuing %s at (%d,%d) for %s",
                                    getItemNameByID(slab).c_str(),i,j,getItemNameByID(itemID).c_str());
                            }
						}
					}

					// Store building location and queue the building
					placeLocations.push_back(location);
				}

				if (produceItemWithLogging(itemID, __LINE__, structureRule)) {
                    if (isCitySim && itemID != Structure_Road && itemID != Structure_Slab1
                        && itemID != Structure_Slab4) {
                        if (crimeServiceSite.isValid()) nonServiceConstructionOrders = 0;
                        else nonServiceConstructionOrders = std::min<Uint32>(3, nonServiceConstructionOrders + 1);
                    }
                    if (redevelop) {
                        AITelemetry::Record removed;
                        for (Uint32 id : zonesToRemove) {
                            auto* zone=dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                            if (!zone || zone->getOwner()!=getHouse()) continue;
                            const Coord z=zone->getLocation();
                            const auto* sim=currentGame->getCitySimulation();
                            const auto& state=sim->getHouseState(getHouse()->getHouseID());
                            const int demand=zone->getItemID()==Structure_ZoneResidential ? state.resValve
                                : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                            removed.set(std::to_string(id),AITelemetry::Record().set("item",zone->getItemID())
                                .set("demand",demand).set("land_value",sim->getLandValueMap().worldGet(z.x,z.y))
                                .set("density",getMap().getTile(z.x,z.y)->getCityZoneDensity()));
                            itemCount[zone->getItemID()]--;
                            zone->demolish();
                        }
                        traceDecision("redevelopment_committed", AITelemetry::Record().set("builder",planningBuilder)
                            .set("item",itemID).set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y)
                            .set("removed_zones",removed));
                    }
                    if (itemID==Structure_Refinery && itemCount[Unit_Harvester]<harvesterLimit
                        && (structureRule==std::string("city_opening_investment")
                            || structureRule==std::string("city_economy") || structureRule==std::string("city_economy_fallback")))
                        ++itemCount[Unit_Harvester];
					reservedStructures[planningBuilder] = {itemID, selectedPlaceLocation};
                    if (placeLocations.empty()) placeLocations.push_back(selectedPlaceLocation);
                    traceDecision("site_reserved", AITelemetry::Record().set("builder", planningBuilder)
                        .set("item", itemID).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y));
                    orderedThisTick.insert(itemID);
                    itemCount[itemID]++;
					money -= data[itemID][houseID].price;
					if (itemID == strategicReserveItem) strategicReserveCost = 0;
					clearPlacementCache();
				} else {
					placeLocations.clear();
				}
			}
			else if (itemID != NONE_ID && pBuilder->isAvailableToBuild(itemID)) {
				// Only build concrete slabs to expand buildable area for structures that need it:
				// Heavy Factory, High Tech Factory, Rocket Turrets, and turret-related Windtraps
				bool needsConcreteExpansion = (itemID == Structure_HeavyFactory
					|| itemID == Structure_HighTechFactory
					|| itemID == Structure_RocketTurret
					|| (itemID == Structure_WindTrap 
						&& itemCount[Structure_RepairYard] > 0 
						&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
						&& pBuilder->getCurrentUpgradeLevel() >= 2));

				if (needsConcreteExpansion && pBuilder->isAvailableToBuild(Structure_Slab1)) {
					Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
					if (slabLocation.isValid()) {
						doProduceItem(pBuilder, Structure_Slab1);
						logDebug("Building concrete slab to expand buildable area for itemID %d at (%d,%d)", itemID, slabLocation.x, slabLocation.y);
					} else {
					// Cannot place slab - silenced (too spammy)
					}
				} else {
					logDebug("Cannot build itemID %d: no place to build and slabs not available", itemID);
				}
			}
		else if (itemID != NONE_ID && !pBuilder->isAvailableToBuild(itemID)) {
			logDebug("Cannot build itemID %d: not available (prerequisites not met)", itemID);
		}
		else if (itemID == NONE_ID && !skipRemainingStructureLogic && emitStatsLog) {
			logDebug("No structure selected to build (money: %d, skipRemaining: %d)", money, skipRemainingStructureLogic);
		}

        // Only use spare yard capacity, after all strategic/city choices.
        if (isCitySim && itemID == NONE_ID && !skipRemainingStructureLogic
            && !roadMaintenanceAttempted && !pBuilder->isUpgrading()
            && pBuilder->getProductionQueueSize() == 0 && pBuilder->isAvailableToBuild(Structure_Road)) {
            roadMaintenanceAttempted = true;
            const int roadPrice = std::max(1,data[Structure_Road][houseID].price);
            const int repaired = queueCityRoadRepairs(pBuilder,std::min(8,std::max(0,money-economyReserve)/roadPrice));
            money -= repaired*roadPrice;
        }

		// City yards use the demand-ranked fallback before placement above.
		// Outside city mode an otherwise idle yard can extend concrete.
		if (!isCitySim && money > 500 && pBuilder->getProductionQueueSize() < 1
			&& itemID == NONE_ID && pBuilder->isAvailableToBuild(Structure_Slab1)) {
			Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
			if (slabLocation.isValid()) doProduceItem(pBuilder, Structure_Slab1);
		}

						}
					}

				if (pBuilder->isWaitingToPlace()) {
					Uint32 itemToBePlaced = pBuilder->getCurrentProducedItem();
					logDebug("PRODUCTION: CY waiting to place itemID: %d, credits: %d, queued locations: %zu", itemToBePlaced, money, placeLocations.size());
					Coord location;
					bool placementIssueHandled = false;
                    auto tracePlacementIssue = [&](const char* event, const char* reason, const Coord& site) {
                        if (!AITelemetry::log().enabled()) return;
                        AITelemetry::Record tiles;
                        const Coord size = getStructureSize(itemToBePlaced);
                        if (site.isValid()) for (int dx = 0; dx < size.x; ++dx) for (int dy = 0; dy < size.y; ++dy) {
                            const int x = site.x + dx, y = site.y + dy;
                            const Tile* tile = getMap().tileExists(x,y) ? getMap().getTile(x, y) : nullptr;
                            AITelemetry::Record state;
                            state.set("x", x).set("y", y).set("exists", tile != nullptr);
                            if (tile) {
                                state.set("terrain", tile->getType()).set("blocked", tile->isBlocked())
                                    .set("zone", static_cast<int>(tile->getCityZoneType())).set("road", tile->isRoad())
                                    .set("in_build_range", getMap().isWithinBuildRange(x, y, getHouse()));
                                if (const auto* object = tile->getGroundObject())
                                    state.set("occupant", object->getObjectID()).set("occupant_item", object->getItemID())
                                        .set("occupant_structure", object->isAStructure());
                            }
                            tiles.set(std::to_string(dx) + "," + std::to_string(dy), state);
                        }
                        traceDecision(event, AITelemetry::Record().set("builder", pConstYard->getObjectID())
                            .set("item", itemToBePlaced).set("reason", reason).set("x", site.x).set("y", site.y)
                            .set("reserved_overlap", site.isValid() && overlapsReservedStructure(site.x, site.y, size.x, size.y))
                            .set("recent_loss_nearby", site.isValid() && nearRecentStructureLoss(site.x, site.y, size.x, size.y))
                            .set("enemy_fire_risk", dangerAt(site,size)).set("recent_loss_risk", dangerAt(site,size,true))
                            .set("reactor_clearance", site.isValid() && reactorClearance(itemToBePlaced,site))
                            .set("planned_locations_remaining", placeLocations.size()).set("tiles", tiles));
                    };


					// Check if we have a pre-stored location (from concrete pre-placement)
					if (!placeLocations.empty()) {
						location = placeLocations.front();
						Coord itemsize = getStructureSize(itemToBePlaced);

						// Verify the location is still valid
						if (getMap().okayToPlaceStructure(location.x, location.y, itemsize.x, itemsize.y, false, getHouse(), false, itemToBePlaced)
                            && (itemToBePlaced != Structure_Road || !getMap().getTile(location.x,location.y)->isRoadConnection())
                            && cityRoadImpact(getMap(), location.x, location.y, itemsize.x, itemsize.y, itemToBePlaced).preservesConnections
                            && !overlapsReservedStructure(location.x, location.y, itemsize.x, itemsize.y)
                            && preservesGroundAccess(itemToBePlaced,location)
                            && (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret
                                || itemToBePlaced == Structure_Wall || itemToBePlaced == Structure_Road
                                || itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4
                                || (!nearRecentStructureLoss(location.x, location.y, itemsize.x, itemsize.y)
                                    && dangerAt(location,itemsize) == 0 && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,location))))) {
							placeLocations.pop_front();
							logDebug("PRODUCTION: Using pre-stored location (%d,%d) for itemID: %d", location.x, location.y, itemToBePlaced);
						} else if (itemToBePlaced == Structure_Road || itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4) {
							// Road/concrete placement failed (maybe already repaired), cancel and move on
							tracePlacementIssue("placement_cancel", itemToBePlaced == Structure_Road ? "planned_road_invalid" : "planned_concrete_invalid", location);
                            doCancelItem(pConstYard, itemToBePlaced);
							placeLocations.pop_front();
							logDebug("PRODUCTION: Cancelled concrete at (%d,%d) - already placed or invalid", location.x, location.y);
							location = Coord::Invalid();
							placementIssueHandled = true;
						} else {
                            // Finished structures do not require concrete. If a unit,
                            // another building or a changed road blocks the old site,
                            // find another legal footprint instead of throwing the order away.
                            const Coord oldSite = location;
                            placeLocations.pop_front();
                            placementCache.erase(itemToBePlaced);
                            location = (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret)
                                ? findEffectiveTurretPlaceLocation(itemToBePlaced) : findPlaceLocation(itemToBePlaced);
                            if (location.isInvalid()) {
                                placeLocations.push_front(oldSite);
                                if (emitStatsLog) tracePlacementIssue("placement_deferred", "planned_site_blocked", oldSite);
                            } else {
                                tracePlacementIssue("placement_replan", "planned_site_blocked", oldSite);
                            }
                            placementIssueHandled = true; // Suppress the no-dynamic-site branch below.
                        }
					} else {
						// No pre-stored location, find one dynamically
					if (itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4) {
						// For concrete slabs, use specialized slab placement method
						location = findSlabPlaceLocation(itemToBePlaced);
					} else if (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret) {
						// For turrets, try city placement first (near crime hotspots),
						// falling back to normal perimeter placement
						location = findEffectiveTurretPlaceLocation(itemToBePlaced);
					} else {
						// For other structures, use normal method that favors adjacency
						location = findPlaceLocation(itemToBePlaced);
						}
					}

                        // A completed generator must not hold the only yard
                        // forever because every legal site is in a threat halo.
                        // Prefer safe, separated reactors and the least exposed
                        // fallback; preserve roads and neighbouring access.
                        if (location.isInvalid() && (itemToBePlaced == Structure_NuclearPlant
                            || itemToBePlaced == Structure_WindTrap)) {
                            const Coord size = getStructureSize(itemToBePlaced);
                            auto bestRecoveryRank = TacticalSafetyPolicy::reactorSiteRank(100000,100000,false,std::numeric_limits<int>::min());
                            int bestRisk = std::numeric_limits<int>::max();
                            int bestDistance = std::numeric_limits<int>::max();
                            for (int x=0; x<=getMap().getSizeX()-size.x; ++x) {
                                for (int y=0; y<=getMap().getSizeY()-size.y; ++y) {
                                    const Coord site(x,y);
                                    if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,itemToBePlaced)
                                        || overlapsReservedStructure(x,y,size.x,size.y)
                                        || !preservesGroundAccess(itemToBePlaced,site)
                                        || !TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,site))
                                        || !cityRoadImpact(getMap(),x,y,size.x,size.y,itemToBePlaced).preservesConnections
                                        || (currentGame->isCitySimEnabled()
                                            && wouldLandlockNeighbouringZone(getMap(),houseID,x,y,size.x,size.y))) continue;
                                    const int risk = dangerAt(site,size)
                                        + (nearRecentStructureLoss(x,y,size.x,size.y) ? 1000 : 0);
                                    const Coord yard = pConstYard->getLocation();
                                    const int distance = std::abs(x-yard.x)+std::abs(y-yard.y);
                                    const auto rank = TacticalSafetyPolicy::reactorSiteRank(risk,0,reactorClearance(itemToBePlaced,site),-distance);
                                    if (itemToBePlaced == Structure_NuclearPlant ? rank > bestRecoveryRank
                                        : risk < bestRisk || (risk == bestRisk && distance < bestDistance)) {
                                        bestRecoveryRank = rank;
                                        bestRisk = risk; bestDistance = distance; location = site;
                                    }
                                }
                            }
                            if (location.isValid()) {
                                placeLocations.clear();
                                traceDecision("placement_power_recovery", AITelemetry::Record()
                                    .set("builder",planningBuilder).set("item",itemToBePlaced)
                                    .set("x",location.x).set("y",location.y).set("risk",bestRisk));
                            }
                        }
                        if (location.isValid() && !preservesGroundAccess(itemToBePlaced,location)) {
                            tracePlacementIssue("placement_deferred", "ground_exit_blocked", location);
                            location=Coord::Invalid();
                            placementIssueHandled=true;
                        }
						if (location.isValid()) {
							traceDecision("placement_request", AITelemetry::Record().set("builder", pConstYard->getObjectID())
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y));
                            const bool placed = doPlaceStructure(pConstYard, location.x, location.y);
                            traceDecision("placement_result", AITelemetry::Record().set("builder", planningBuilder)
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y).set("success", placed));
                            if (itemToBePlaced != Structure_Slab1 && itemToBePlaced != Structure_Slab4) {
                                if (placed) reservedStructures.erase(planningBuilder);
                                else { reservedStructures[planningBuilder] = {itemToBePlaced, location};
                                    if (placeLocations.empty()) placeLocations.push_front(location); }
                            }
                            clearPlacementCache();
							logDebug("PRODUCTION: Placed structure itemID: %d at (%d,%d)", itemToBePlaced, location.x, location.y);
						}
						else if (!placementIssueHandled) {
							if (emitStatsLog) logDebug("PRODUCTION: Holding finished item %d until a legal site is available", itemToBePlaced);
							if (emitStatsLog) tracePlacementIssue("placement_deferred", "no_dynamic_site", location);
						}
					}
                    // Pre-plan queue=0 often means a yard is about to receive
                    // an order, not that it stayed idle. Record the outcome too.
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        const char* result = pBuilder->isUpgrading() ? "upgrading"
                            : pBuilder->getProductionQueueSize() > 0 ? "queued"
                            : getHouse()->getCredits() <= 100 ? "insufficient_cash"
                            : pBuilder->getBuildListSize() == 0 ? "no_build_options"
                            : "empty_after_planning";
                        traceDecision("yard_planning_result", AITelemetry::Record()
                            .set("builder", pBuilder->getObjectID()).set("result", result)
                            .set("queue", pBuilder->getProductionQueueSize())
                            .set("upgrading", pBuilder->isUpgrading()).set("hold", pBuilder->isOnHold())
                            .set("state", decisionState()));
                    }
				} break;
				}
			}
		}
	}

	// MULTIPLAYER FIX: Use deterministic timer instead of random
	buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
}


void QuantBot::scrambleUnitsAndDefend(const ObjectBase* intruder, bool clearingSpice) {
    AITelemetry::PerformanceScope perfScope("ai.scrambleUnitsAndDefend", getGameCycleCount(), getHouse()->getHouseID());
    if (supportMode || !intruder || intruder->getHealth() <= 0
        || intruder->getOwner()->getTeamID() == getHouse()->getTeamID()) return;
    const Coord contact = intruder->getLocation();
    if (!getMap().tileExists(contact)) return;
    // Debounce volleys by local district, independently for air and ground.
    // This state is saved: neither rendering speed nor telemetry affects orders.
    const Uint32 key = ((contact.y / 8) * ((getMap().getSizeX()+7)/8) + contact.x/8) * 2
        + (intruder->isAFlyingUnit() ? 1 : 0);
    const Uint32 now = getGameCycleCount();
    auto previous = defenceResponseCycles.find(key);
    if (previous != defenceResponseCycles.end() && now-previous->second < MILLI2CYCLES(clearingSpice ? 5000 : 2000)) return;
    defenceResponseCycles[key] = now;
    auto value = [&](const ObjectBase* object) {
        const int price = currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
        return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
    };
    int threatValue = value(intruder), committed = 0;
    constexpr int clearingRadius=18;
    if (clearingSpice) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()->getTeamID()!=getHouse()->getTeamID() && structure->getHealth()>0
            && structure->canAttack() && structure->getItemID()!=Structure_Palace
            && structure->isVisible(getHouse()->getTeamID())
            && blockDistance(contact,structure->getLocation())<=8) threatValue+=value(structure);
    }
    std::vector<SimpleArmyPolicy::Responder> candidates;
    for (const auto* unit : getUnitList()) {
        if (!unit->isActive() || !unit->canAttack()) continue;
        if (unit != intruder && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->isVisible(getHouse()->getTeamID())
            && unit->isAFlyingUnit() == intruder->isAFlyingUnit()
            && blockDistance(contact,unit->getLocation()) <= 8) threatValue += value(unit);
        if (unit->getOwner()!=getHouse() || !unit->isRespondable() || humanControls(unit)
            || !unit->canAttack(intruder) || unit->isBadlyDamaged() || unit->getAttackMode()==RETREAT
            || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Harvester) continue;
        if (clearingSpice && blockDistance(contact,unit->getLocation())>clearingRadius) continue;
        const auto* target=unit->getTarget();
        const bool hostileTarget=target && target->getHealth()>0
            && target->getOwner()->getTeamID()!=getHouse()->getTeamID();
        // Count troops already fighting or travelling to this contact. They do
        // not need a fresh command on every hit. Do not pull units out of another fight.
        if (hostileTarget && target->isAFlyingUnit()==intruder->isAFlyingUnit()
            && blockDistance(contact,target->getLocation())<=8) { committed+=value(unit); continue; }
        if (hostileTarget && blockDistance(unit->getLocation(),target->getLocation())<=unit->getWeaponRange()) continue;
        candidates.push_back({unit->getObjectID(),value(unit),blockDistance(contact,unit->getLocation()).lround()});
    }
    const auto response=clearingSpice
        ? SimpleArmyPolicy::clearingForce(threatValue,committed,candidates,clearingRadius)
        : SimpleArmyPolicy::reinforcements(threatValue,committed,candidates);
    int dispatched=0;
    for (const auto id:response) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        groundSquad.erase(id);
        // A local, non-forced attack permits nearer enemies and kiting. Area
        // guard keeps the response at the incident instead of chasing a base afterwards.
        const_cast<UnitBase*>(unit)->setGuardPoint(contact);
        doSetAttackMode(unit,AREAGUARD);
        doAttackObject(unit,intruder,false);
        ++dispatched;
    }
    if (dispatched) traceDecision("defence_response",AITelemetry::Record().set("target",intruder->getObjectID())
        .set("x",contact.x).set("y",contact.y).set("threat_value",threatValue)
        .set("already_committed_value",committed).set("required_value",SimpleArmyPolicy::responseValue(threatValue))
        .set("reason",clearingSpice ? "clear_spice_launcher" : "under_attack")
        .set("dispatched",dispatched));
}

bool QuantBot::tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                          const QuantBotConfig& config) {
    if (!diffSettings.ornithopterAttackEnabled) {
        ornithopterStrikeTeam.reset();
        return false;
    }

    const Map& map = getMap();
    const int maxDim = std::max(map.getSizeX(), map.getSizeY());

    int effectiveThreshold = diffSettings.ornithopterAttackThreshold;
    if (maxDim > 64) {
        if (maxDim >= 128) {
            effectiveThreshold *= 3;
        } else {
            effectiveThreshold *= 2;
        }
    }
    if (effectiveThreshold <= 0) {
        effectiveThreshold = 1;
    }

    std::vector<const UnitBase*> availableOrnithopters;
    std::set<Uint32> currentMemberIds;

    for (const UnitBase* pUnit : getUnitList()) {
        if (pUnit->getOwner() != getHouse()
            || pUnit->getItemID() != Unit_Ornithopter
            || !pUnit->isActive()
            || pUnit->isBadlyDamaged()
            || !pUnit->isRespondable() || humanControls(pUnit)) {
            continue;
        }

        availableOrnithopters.push_back(pUnit);
        currentMemberIds.insert(pUnit->getObjectID());
    }

    const int totalOrnithopters = getHouse()->getNumItems(Unit_Ornithopter);
    const int readyOrnithopters = static_cast<int>(availableOrnithopters.size());
    const bool noFriendlyStructures = (getHouse()->getNumStructures() == 0);
    const bool forceLastStandStrike = noFriendlyStructures && readyOrnithopters > 0;
    const int appliedThreshold = forceLastStandStrike ? std::max(readyOrnithopters, 1) : effectiveThreshold;

    if (!forceLastStandStrike && readyOrnithopters < effectiveThreshold) {
        ornithopterStrikeTeam.reset();
        return false;
    }

    if (!ornithopterStrikeTeam.memberIds.empty()) {
        for (auto it = ornithopterStrikeTeam.memberIds.begin(); it != ornithopterStrikeTeam.memberIds.end();) {
            if (currentMemberIds.count(*it) == 0U) {
                it = ornithopterStrikeTeam.memberIds.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (ornithopterStrikeTeam.isActive()) {
        ornithopterStrikeTeam.minMembers = appliedThreshold;
        if (static_cast<int>(ornithopterStrikeTeam.memberIds.size()) < ornithopterStrikeTeam.minMembers) {
            ornithopterStrikeTeam.reset();
        }
    }

    auto ensureOrders = [&](const ObjectBase* target) -> bool {
        if (target == nullptr) {
            return false;
        }

        bool issued = false;
        const Uint32 targetId = target->getObjectID();

        for (const UnitBase* pUnit : availableOrnithopters) {
            const Uint32 unitId = pUnit->getObjectID();
            ornithopterStrikeTeam.memberIds.insert(unitId);

            if (!pUnit->canAttack(target)) {
                continue;
            }

            const ObjectBase* currentTarget = pUnit->hasATarget() ? pUnit->getTarget() : nullptr;
            const bool needsNewTarget = (currentTarget == nullptr) || (currentTarget->getObjectID() != targetId);
            const bool needsMode = pUnit->getAttackMode() != HUNT;

            if (needsMode) {
                doSetAttackMode(pUnit, HUNT);
                issued = true;
            }

            if (needsNewTarget) {
                doAttackObject(pUnit, target, true);
                issued = true;
            }
        }

        return issued;
    };

    // Opportunistic reactor strike. Require half the ready wing within 12 tiles
    // and no visible anti-air covering the plant or the straight approach corridor.
    const StructureBase* nearbyReactor = nullptr;
    int bestReactorDistance = std::numeric_limits<int>::max();
    for (const StructureBase* reactor : getStructureList()) {
        if (reactor->getItemID() != Structure_NuclearPlant || !reactor->isActive()
            || reactor->getOwner()->getTeamID() == getHouse()->getTeamID()
            || !reactor->isVisible(getHouse()->getTeamID())) continue;
        int nearby = 0;
        int totalDistance = 0;
        int antiAir = 0;
        for (const UnitBase* aircraft : availableOrnithopters) {
            const int distance = blockDistance(aircraft->getLocation(), reactor->getLocation()).lround();
            if (aircraft->canAttack(reactor) && distance <= 12) ++nearby;
            totalDistance += distance;
        }
        if (!QuantBotBuildPolicy::easyReactorStrike(nearby, readyOrnithopters, 0)) continue;
        for (const UnitBase* aircraft : availableOrnithopters) {
            if (antiAir > 0) break;
            auto checkDefender = [&](const ObjectBase* defender) {
                if (antiAir > 0 || !defender->isActive() || !defender->isVisible(getHouse()->getTeamID())
                    || defender->getOwner()->getTeamID() == getHouse()->getTeamID()
                    || !defender->canAttack(aircraft)) return;
                const int range = currentGame->objectData.data[defender->getItemID()][defender->getOwner()->getHouseID()].weaponrange;
                const Coord from = aircraft->getLocation(), to = reactor->getLocation();
                const Coord dp = defender->getLocation();
                if (dp.x < std::min(from.x,to.x)-range || dp.x > std::max(from.x,to.x)+range
                    || dp.y < std::min(from.y,to.y)-range || dp.y > std::max(from.y,to.y)+range) return;
                const int steps = std::max(1, std::max(std::abs(to.x-from.x), std::abs(to.y-from.y)));
                for (int step = 0; step <= steps; ++step) {
                    const Coord point(from.x+(to.x-from.x)*step/steps, from.y+(to.y-from.y)*step/steps);
                    if (blockDistance(point, defender->getLocation()) <= range) { ++antiAir; break; }
                }
            };
            for (const StructureBase* defender : getStructureList()) checkDefender(defender);
            for (const UnitBase* defender : getUnitList()) checkDefender(defender);
        }
        if (QuantBotBuildPolicy::easyReactorStrike(nearby, readyOrnithopters, antiAir)
            && totalDistance < bestReactorDistance) {
            nearbyReactor = reactor;
            bestReactorDistance = totalDistance;
        }
    }
    if (nearbyReactor != nullptr) {
        ornithopterStrikeTeam.setTarget(nearbyReactor->getObjectID(), appliedThreshold);
        const bool issued = ensureOrders(nearbyReactor);
        if (issued) traceDecision("ornithopter_reactor_strike", AITelemetry::Record()
            .set("target", nearbyReactor->getObjectID()).set("ready_aircraft", readyOrnithopters)
            .set("total_distance", bestReactorDistance).set("reason", "nearby_clear_approach"));
        return issued;
    }

    if (ornithopterStrikeTeam.isActive()) {
        const ObjectBase* existingTarget = currentGame->getObjectManager().getObject(ornithopterStrikeTeam.targetId);
        if (existingTarget == nullptr || !existingTarget->isActive()) {
            ornithopterStrikeTeam.reset();
        } else {
            return ensureOrders(existingTarget);
        }
    }

    const int myTeam = getHouse()->getTeamID();
    const House* myHouse = getHouse();

    const ObjectBase* teamTarget = nullptr;
    double bestTargetScore = -1.0;

    auto evaluateCandidate = [&](const ObjectBase* candidate, const QuantBotConfig::TargetPriority& priority) {
        if (!candidate || !candidate->isActive()) {
            return;
        }

        const House* owner = candidate->getOwner();
        if (!owner || owner->getTeamID() == myTeam) {
            return;
        }

        if (!candidate->isVisible(myTeam)) {
            return;
        }

        const int weight = priority.build + priority.target;
        if (weight <= 0) {
            return;
        }

        double candidateScore = -1.0;
        for (const UnitBase* pOrnithopter : availableOrnithopters) {
            if (!pOrnithopter->canAttack(candidate)) {
                continue;
            }

            FixPoint distanceFP = blockDistance(pOrnithopter->getLocation(), candidate->getLocation());
            const double score = static_cast<double>(weight) / (distanceFP.toDouble() + 1.0);
            if (score > candidateScore) {
                candidateScore = score;
            }
        }

        if (candidateScore <= 0.0) {
            return;
        }

        if (candidateScore > bestTargetScore) {
            bestTargetScore = candidateScore;
            teamTarget = candidate;
        }
    };

    for (const StructureBase* pStructure : getStructureList()) {
        evaluateCandidate(pStructure, config.getStructurePriority(pStructure->getItemID()));
    }

    for (const UnitBase* pEnemy : getUnitList()) {
        if (pEnemy->getOwner() == myHouse) {
            continue;
        }
        evaluateCandidate(pEnemy, config.getUnitPriority(pEnemy->getItemID()));
    }

    if (teamTarget == nullptr) {
        ornithopterStrikeTeam.reset();
        return false;
    }

    ornithopterStrikeTeam.reset();
    ornithopterStrikeTeam.setTarget(teamTarget->getObjectID(), appliedThreshold);
    const bool launched = ensureOrders(teamTarget);

    if (launched) {
        if (forceLastStandStrike) {
            logDebug("Ornithopter strike launched despite threshold (last structure destroyed): ready=%d total=%d forcedThreshold=%d",
                     readyOrnithopters, totalOrnithopters, appliedThreshold);
        } else {
            logDebug("Ornithopter strike launched: %d units (effective threshold %d)",
                     totalOrnithopters, effectiveThreshold);
        }
    }

    return launched;
}


void QuantBot::attack(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.attack", getGameCycleCount(), getHouse()->getHouseID());
	if (supportMode) {
		attackTimer = std::numeric_limits<Sint32>::max();
		return;
	}

    // Get config for this difficulty
    const QuantBotConfig& config = getQuantBotConfig();
    const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    attackTimer = SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
        currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());
    traceDecision("attack_schedule",AITelemetry::Record().set("delay_cycles",attackTimer)
        .set("base_cycles",MILLI2CYCLES(config.attackTimerMs)));

	// Check if this difficulty is allowed to attack at all
	if (!diffSettings.attackEnabled) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "difficulty_disabled"));
		logDebug("Don't attack. Difficulty %d has attackEnabled = false", static_cast<int>(difficulty));
		return;
	}

    tryLaunchOrnithopterStrike(diffSettings, config);

	// Main attack loop - check military strength threshold
	// Campaign mode: Use difficulty-specific threshold from config
	// Custom mode: Use global config threshold (same for all difficulties)
	float attackThresholdPercent = (gameMode == GameMode::Campaign) 
		? diffSettings.attackThresholdPercent 
		: config.attackThresholdPercent;

	FixPoint attackThreshold = FixPoint(static_cast<int>(attackThresholdPercent * 100)) / 100;
	const bool vanillaCustom = gameMode == GameMode::Custom && !getHouse()->isPowerRequired();
    const int requiredMilitary = vanillaCustom
        ? DuneCity::vanillaAttackThreshold((militaryValueLimit * attackThreshold).lround(), static_cast<int>(difficulty))
        : (militaryValueLimit * attackThreshold).lround();
    if (militaryValue < requiredMilitary) {
        // Recheck readiness promptly; do not miss a short-lived strength window.
        if (vanillaCustom && difficulty == Difficulty::Brutal)
            attackTimer = std::min(attackTimer, static_cast<int>(MILLI2CYCLES(15000)));
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "army_threshold").set("military", militaryValue).set("limit", militaryValueLimit)
            .set("required_military", requiredMilitary));
		logDebug("Don't attack. Not enough troops: house: %d  dif: %d  mStr: %d  mLim: %d (need %.1f%%)",
			getHouse()->getHouseID(), static_cast<Uint8>(difficulty), militaryValue, militaryValueLimit, attackThresholdPercent * 100.0f);
		return;
	}

	// Don't attack if you don't yet have a repair yard, if you are at least tech 5
	if (getHouse()->getNumItems(Structure_RepairYard) == 0 && currentGame->techLevel > 4) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "repair_prerequisite"));
		logDebug("Don't attack. Wait until you have a repair yard.");
		return;
	}

    launchGroundHunt();
}

void QuantBot::onHumanUnitOrder(Uint32 id) {
    manualUnitOrders[id] = getGameCycleCount();
    groundSquad.erase(id);
    escortAssignments.erase(id);
}

bool QuantBot::humanControls(const UnitBase* unit) const {
    const auto it = manualUnitOrders.find(unit->getObjectID());
    return it != manualUnitOrders.end() && (getGameCycleCount()-it->second < MILLI2CYCLES(120000)
        || unit->wasForced() || unit->isMoving() || unit->hasATarget());
}

void QuantBot::launchGroundHunt() {
    if (supportMode) return;
    int count=0,value=0;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || !unit->isRespondable()
            || !unit->isAGroundUnit() || !unit->canAttack() || unit->isBadlyDamaged()
            || unit->getAttackMode()==RETREAT || humanControls(unit)
            || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Harvester) continue;
        // Existing fights/defensive responses finish first. There is no assembly
        // quota or forced shared object target to make idle troops ignore neighbours.
        const auto* target=unit->getTarget();
        if (target && target->getHealth()>0) continue;
        if (unit->getAttackMode()==HUNT && !unit->wasForced()) continue;
        doSetAttackMode(unit,GUARD); // releases any old AI movement order
        doSetAttackMode(unit,HUNT);
        ++count;
        value+=currentGame->objectData.data[unit->getItemID()][getHouse()->getHouseID()].price;
    }
    traceDecision("ground_hunt",AITelemetry::Record().set("members",count).set("value",value));
}

void QuantBot::releaseLegacyGroundSquad() {
    if (!groundSquadPhase && groundSquad.empty()) return;
    for (const auto id:groundSquad) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getOwner()!=getHouse() || humanControls(unit)) continue;
        doSetAttackMode(unit,GUARD);
        doSetAttackMode(unit,groundSquadPhase==2 ? HUNT : AREAGUARD);
    }
    groundSquad.clear(); groundSquadPhase=0; groundSquadObjective=NONE_ID;
    squadRallyLocation=Coord::Invalid();
    rallySelectedCycle=std::numeric_limits<Uint32>::max();
}

void QuantBot::onCombatReward(Uint32 attacker, Uint32 target, const CombatReward::Totals& reward) {
    for (auto& raid : harvesterStrikeTraces) for (auto& member : raid.members) if (member.id==attacker) {
        member.reward.damageMilli += reward.damageMilli;
        member.reward.killBonusMilli += reward.killBonusMilli;
        member.reward.hpRemovedMilli += reward.hpRemovedMilli;
        member.reward.hits += reward.hits;
        member.reward.kills += reward.kills;
        if (raid.target==target && reward.kills>0) raid.targetKilledByStrike=true;
        return; // Membership belongs to one observed strike at a time.
    }
}

void QuantBot::finishTelemetry() { updateHarvesterStrikeTelemetry(true); }

void QuantBot::updateHarvesterStrikeTelemetry(bool final) {
    AITelemetry::PerformanceScope perfScope("ai.updateHarvesterStrikeTelemetry", getGameCycleCount(), getHouse()->getHouseID());
    if (!AITelemetry::log().enabled()) { harvesterStrikeTraces.clear(); return; }
    const Uint32 now=getGameCycleCount();
    for (auto it=harvesterStrikeTraces.begin(); it!=harvesterStrikeTraces.end();) {
        auto& raid=*it;
        const auto* target=currentGame->getObjectManager().getObject(raid.target);
        const bool visible=target && target->isVisible(getHouse()->getTeamID());
        if (visible) raid.lastVisible=now;
        // A strike may outlive the initial forced attack command. Only abandon
        // a visible harvester when it moves under meaningful protection; a
        // harmless command expiry must reissue the same target instead.
        int targetDefence = 0;
        bool protectedByTurret = false;
        if (visible && target && target->isActive()) {
            for (const auto* building : getStructureList()) {
                if (building->getOwner()->getTeamID() != target->getOwner()->getTeamID()
                    || !building->isVisible(getHouse()->getTeamID()) || !building->canAttack()) continue;
                if (blockDistance(target->getLocation(), building->getLocation())
                    <= building->getWeaponRange() + 3) {
                    protectedByTurret = true;
                    break;
                }
            }
            if (!protectedByTurret) for (const auto* guard : getUnitList()) {
                if (!guard->isActive() || !guard->canAttack() || !guard->isVisible(getHouse()->getTeamID())
                    || guard->getOwner()->getTeamID() != target->getOwner()->getTeamID()) continue;
                if (blockDistance(target->getLocation(), guard->getLocation())
                    <= std::max(6, guard->getWeaponRange() + 2))
                    targetDefence += currentGame->objectData.data[guard->getItemID()][guard->getOriginalHouseID()].price;
            }
        }
        const bool targetDefended = protectedByTurret || targetDefence > 2000;
        int alive=0, lost=0, captured=0, lostValue=0;
        bool engaged=false, stillAssigned=false;
        int64_t damage=0, bonus=0;
        AITelemetry::Record members;
        for (const auto& member : raid.members) {
            const auto* unit=dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(member.id));
            const bool dead=!unit || unit->getHealth()<=0;
            const bool changedOwner=unit && !dead && unit->getOwner()!=getHouse();
            if (dead) { ++lost; lostValue+=member.price; }
            else if (changedOwner) ++captured;
            else {
                ++alive;
                const auto* currentTarget=unit->getTarget();
                stillAssigned |= currentTarget && currentTarget->getObjectID()==raid.target;
                engaged |= currentTarget && unit->isActive()
                    && blockDistance(unit->getLocation(),currentTarget->getLocation())<=unit->getWeaponRange();
            }
            damage+=member.reward.damageMilli; bonus+=member.reward.killBonusMilli;
            members.set(std::to_string(member.id),AITelemetry::Record().set("item",member.item)
                .set("lost",dead).set("captured",changedOwner).set("damage_value_milli",member.reward.damageMilli)
                .set("kill_bonus_milli",member.reward.killBonusMilli).set("reward_milli",member.reward.total()));
        }
        int retargeted = 0;
        // Observation only: squad orders must not depend on telemetry being enabled.
        (engaged ? raid.engagementCycles : raid.transitCycles) += now-raid.sampled;
        raid.sampled=now;
        const char* outcome=nullptr;
        if (raid.targetKilledByStrike) outcome="target_killed_by_strike";
        else if (!target || target->getHealth()<=0) outcome="target_removed";
        else if (target->getOwner()->getTeamID()==getHouse()->getTeamID()) outcome="target_captured";
        else if (alive==0) outcome="force_lost_or_captured";
        else if (!visible && now-raid.lastVisible>=MILLI2CYCLES(15000)) outcome="target_lost_contact";
        else if (visible && !target->isActive()) outcome="target_transport_or_inactive";
        else if (targetDefended) outcome="target_entered_defended_area";
        else if (!stillAssigned && now-raid.start>=MILLI2CYCLES(5000)) outcome="attackers_unavailable_target_survived";
        else if (now-raid.start>=MILLI2CYCLES(120000)) outcome="timeout_target_survived";
        if (!outcome && final) outcome="game_ended_while_active";
        if (outcome || now-raid.logged>=MILLI2CYCLES(5000)) {
            traceDecision(outcome ? "harvester_strike_outcome" : "harvester_strike_progress",AITelemetry::Record()
                .set("raid_id",raid.id).set("target",raid.target).set("outcome",outcome ? outcome : "active")
                .set("target_visible",visible).set("target_present",target!=nullptr)
                .set("elapsed_cycles",now-raid.start).set("transit_cycles",raid.transitCycles)
                .set("engagement_cycles",raid.engagementCycles).set("survivors",alive).set("losses",lost)
                .set("captured",captured).set("lost_value",lostValue).set("damage_value_milli",damage)
                .set("kill_bonus_milli",bonus).set("reward_milli",damage+bonus).set("members",members)
                .set("target_defence",targetDefence).set("target_turret_covered",protectedByTurret)
                .set("retargeted_members",retargeted));
            raid.logged=now;
        }
        if (outcome) it=harvesterStrikeTraces.erase(it); else ++it;
    }
}

Coord QuantBot::findSquadRallyLocation() {
    const Uint32 now=getGameCycleCount();
    // One bounded search per 30 simulation seconds, including failed searches.
    if (rallySelectedCycle!=std::numeric_limits<Uint32>::max()
        && now-rallySelectedCycle<MILLI2CYCLES(30000)) return squadRallyLocation;
    rallySelectedCycle=now;
    int x=0,y=0,count=0;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || unit->getItemID()!=Unit_Harvester) continue;
        const auto* harvester=static_cast<const Harvester*>(unit);
        if (harvester->isReturning() || !harvester->isHarvesting()) continue;
        x+=unit->getX(); y+=unit->getY(); ++count;
    }
    Coord centre=count ? Coord(x/count,y/count) : findBaseCentre(getHouse()->getHouseID());
    if (centre.isInvalid()) return Coord::Invalid();
    const UnitBase* closest=nullptr;
    int distance=std::numeric_limits<int>::max();
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->canAttack() || unit->isAFlyingUnit()
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !unit->isVisible(getHouse()->getTeamID())) continue;
        const int d=blockDistance(centre,unit->getLocation()).lround();
        if (d<distance) { distance=d; closest=unit; }
    }
    // Stand on the enemy-facing side of the working harvesters, without selecting
    // an enemy base as a compulsory destination for every unit.
    if (closest) {
        const Coord delta=closest->getLocation()-centre;
        const int scale=std::max(1,std::max(std::abs(delta.x),std::abs(delta.y)));
        centre+=Coord(delta.x*3/scale,delta.y*3/scale);
    }
    auto usable=[&](Coord p) { return getMap().tileExists(p) && !getMap().getTile(p)->isMountain()
        && !getMap().getTile(p)->hasAStructure() && dangerAt(p)==0; };
    if (squadRallyLocation.isValid() && blockDistance(centre,squadRallyLocation)<=5 && usable(squadRallyLocation))
        return squadRallyLocation;
    Coord best=Coord::Invalid(); int bestScore=std::numeric_limits<int>::max();
    for (int dy=-8;dy<=8;++dy) for (int dx=-8;dx<=8;++dx) {
        const Coord p=centre+Coord(dx,dy);
        if (!usable(p)) continue;
        int open=0;
        for (const Coord d:{Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) open+=usable(p+d);
        const int score=(std::abs(dx)+std::abs(dy))*2+(4-open)*4;
        if (score<bestScore) { bestScore=score; best=p; }
    }
    if (best.isValid()) traceDecision("harvest_army_rally",AITelemetry::Record().set("x",best.x).set("y",best.y)
        .set("working_harvesters",count));
    return best;
}

Coord QuantBot::findSquadRetreatLocation() {
	Coord newSquadRetreatLocation = Coord::Invalid();

	FixPoint closestDistance = FixPt_MAX;
	for (const StructureBase* pStructure : getStructureList()) {
		// if it is our building, check to see if it is closer to the squad rally point then we are
		if (pStructure->getOwner()->getHouseID() == getHouse()->getHouseID()) {
			Coord closestStructurePoint = pStructure->getClosestPoint(squadRallyLocation);
			FixPoint structureDistance = blockDistance(squadRallyLocation, closestStructurePoint);

			if (structureDistance < closestDistance) {
				closestDistance = structureDistance;
				newSquadRetreatLocation = closestStructurePoint;
			}
		}
	}

	return newSquadRetreatLocation;
}

Coord QuantBot::findBaseCentre(int houseID) {
	int buildingCount = 0;
	int totalX = 0;
	int totalY = 0;

	for (const StructureBase* pCurrentStructure : getStructureList()) {
		if (pCurrentStructure->getOwner()->getHouseID() == houseID && pCurrentStructure->getStructureSizeX() != 1) {
			// Lets find the center of mass of our squad
			buildingCount++;
			totalX += pCurrentStructure->getX();
			totalY += pCurrentStructure->getY();
		}
	}

	Coord baseCentreLocation = Coord::Invalid();

	if (buildingCount > 0) {
		baseCentreLocation.x = totalX / buildingCount;
		baseCentreLocation.y = totalY / buildingCount;
	}

	return baseCentreLocation;
}
const UnitBase* QuantBot::findLightRaiderTarget(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    // Do not send raiders across the whole map looking for an ideal target.
    // Their normal hunt logic handles that; this only exploits visible prey in
    // a local combat pocket.
    constexpr int searchRadius = 12;
    const UnitBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive() || !candidate->isVisible(getHouse()->getTeamID())
            || !QuantBotBuildPolicy::isLightRaiderPreferredTarget(candidate->getItemID())
            || !raider->canAttack(candidate)) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= searchRadius && distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

const UnitBase* QuantBot::findThreateningTank(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    const UnitBase* threat = nullptr;
    FixPoint threatDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive()
            || !QuantBotBuildPolicy::isArmoredTank(candidate->getItemID())
            || candidate->getOwner()->getTeamID() == getHouse()->getTeamID()
            || candidate->getTarget() != raider) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= candidate->getWeaponRange() && distance < threatDistance) {
            threat = candidate;
            threatDistance = distance;
        }
    }
    return threat;
}

double QuantBot::getProductionBuildingMultiplier(int itemID) const {
	switch (itemID) {
		case Structure_ConstructionYard:
			return 2.0;
		case Structure_RepairYard:
			return 2.0;
		case Structure_HeavyFactory:
			return 1.5;
		case Structure_Refinery:
			return 1.3;
		case Structure_StarPort:
			return 1.3;
		default:
			return 1.0;
	}
}

Coord QuantBot::findBestDeathHandTarget(int enemyHouseID) {
	const QuantBotConfig& config = getQuantBotConfig();
	const int myTeam = getHouse()->getTeamID();

    const Coord reactorTarget = findNuclearMissileTarget();
    if (reactorTarget.isValid()) return reactorTarget;

	const StructureBase* bestTarget = nullptr;
	double bestScore = -1.0;

	// Evaluate each enemy structure as a potential target
	for (const StructureBase* pCandidate : getStructureList()) {
		if (!pCandidate || !pCandidate->isActive()) {
			continue;
		}

		if (pCandidate->getOwner()->getHouseID() != enemyHouseID) {
			continue;
		}

		if (!pCandidate->isVisible(myTeam)) {
			continue;
		}

		// Get base priority from config
		const QuantBotConfig::TargetPriority& priority = config.getStructurePriority(pCandidate->getItemID());
		const int weight = priority.build + priority.target;
		if (weight <= 0) {
			continue;
		}

		// Apply production building multiplier
		const double productionMultiplier = getProductionBuildingMultiplier(pCandidate->getItemID());
		double score = static_cast<double>(weight) * productionMultiplier;

		// Center of mass calculation: add weighted value of nearby buildings
		// Death hand has 10-tile inaccuracy, so check 5-tile radius for nearby targets
		const Coord candidatePos = pCandidate->getLocation();
		constexpr int CHECK_RADIUS = 5;
		double centerOfMassBonus = 0.0;

		for (const StructureBase* pNearby : getStructureList()) {
			if (!pNearby || !pNearby->isActive() || pNearby == pCandidate) {
				continue;
			}

			if (pNearby->getOwner()->getHouseID() != enemyHouseID) {
				continue;
			}

			if (!pNearby->isVisible(myTeam)) {
				continue;
			}

			FixPoint distance = blockDistance(candidatePos, pNearby->getLocation());
			if (distance.toDouble() <= CHECK_RADIUS) {
				// Get this nearby building's priority weight
				const QuantBotConfig::TargetPriority& nearbyPriority = config.getStructurePriority(pNearby->getItemID());
				const int nearbyWeight = nearbyPriority.build + nearbyPriority.target;

				if (nearbyWeight > 0) {
					// Add distance-weighted contribution: closer buildings contribute more
					centerOfMassBonus += static_cast<double>(nearbyWeight) / (distance.toDouble() + 1.0);
				}
			}
		}

		// Final score is base score plus center of mass bonus
		score += centerOfMassBonus;

		if (score > bestScore) {
			bestScore = score;
			bestTarget = pCandidate;
		}
	}

	if (bestTarget != nullptr) {
		return bestTarget->getLocation();
	}

	// Fallback to center of base if no suitable target found
	return findBaseCentre(enemyHouseID);
}


Coord QuantBot::findSquadCenter(int houseID) {
	int squadSize = 0;

	int totalX = 0;
	int totalY = 0;

	for (const UnitBase* pCurrentUnit : getUnitList()) {
		if (pCurrentUnit->getOwner()->getHouseID() == houseID
			&& pCurrentUnit->getItemID() != Unit_Carryall
			&& pCurrentUnit->getItemID() != Unit_Harvester
			&& pCurrentUnit->getItemID() != Unit_Frigate
			&& pCurrentUnit->getItemID() != Unit_MCV

			// Stop freeman making tanks roll forward
			&& !(currentGame->techLevel > 6 && pCurrentUnit->getItemID() == Unit_Trooper)
			&& pCurrentUnit->getItemID() != Unit_Saboteur
			&& pCurrentUnit->getItemID() != Unit_Sandworm

			// Don't let troops moving to rally point contribute
			/*
			&& pCurrentUnit->getAttackMode() != RETREAT
			&& pCurrentUnit->getDestination().x != squadRallyLocation.x
			&& pCurrentUnit->getDestination().y != squadRallyLocation.y*/) {

			// Lets find the center of mass of our squad
			squadSize++;
			totalX += pCurrentUnit->getX();
			totalY += pCurrentUnit->getY();
		}

	}

	Coord squadCenterLocation = Coord::Invalid();

	if (squadSize > 0) {
		squadCenterLocation.x = totalX / squadSize;
		squadCenterLocation.y = totalY / squadSize;
	}

	return squadCenterLocation;
}

/**
 * Kite away from a threat while moving towards squad center.
 * Calculates a retreat position that maintains weapon range from the threat
 * while moving closer to the squad center.
 * 
 * @param pUnit The unit to move (must be non-null and respondable)
 * @param pThreat The threatening unit to kite away from (must be non-null)
 * @param desiredRange The desired distance to maintain from threat (typically weapon range)
 */
void QuantBot::kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange) {
	// Safety checks
	if (!pUnit || !pThreat || !pUnit->isRespondable() || !currentGameMap) {
		return;
	}

	// Don't kite if pathfinding is overloaded
	if (currentGame && currentGame->isPathQueueStressed()) {
		return;
	}

	// CRITICAL: Prevent command spam - only issue kite commands if unit is not currently moving
	// or if destination is significantly different (>2 tiles)
	Coord unitLocation = pUnit->getLocation();
	Coord unitDestination = pUnit->getDestination();

	if (pUnit->isMoving() && unitDestination.isValid() && unitDestination != unitLocation) {
		// Unit is already moving - check if it's moving away from the threat
		Coord threatLocation = pThreat->getLocation();
		FixPoint distDestToThreat = blockDistance(unitDestination, threatLocation);
		FixPoint distCurrentToThreat = blockDistance(unitLocation, threatLocation);

		// If already moving away from threat, don't interrupt
		if (distDestToThreat > distCurrentToThreat) {
			return;
		}
	}

	Coord threatLocation = pThreat->getLocation();

	// Calculate current distance to threat
	FixPoint distToThreat = blockDistance(unitLocation, threatLocation);

	// If already at or beyond desired range, no need to kite
	if (distToThreat >= desiredRange) {
		return;
	}

	// Find squad center (prefer rally location as it's more stable)
	Coord squadCenter = squadRallyLocation.isValid() ? squadRallyLocation : findSquadCenter(getHouse()->getHouseID());

	// If no squad center, just move directly away from threat
	if (!squadCenter.isValid()) {
		squadCenter = unitLocation;
	}

	// Calculate direction vectors
	FixPoint dx_threat = unitLocation.x - threatLocation.x;
	FixPoint dy_threat = unitLocation.y - threatLocation.y;
	FixPoint dx_squad = squadCenter.x - unitLocation.x;
	FixPoint dy_squad = squadCenter.y - unitLocation.y;

	// Normalize threat direction (away from threat)
	FixPoint threatDist = FixPoint::sqrt(dx_threat * dx_threat + dy_threat * dy_threat);
	if (threatDist < 0.1_fix) {
		threatDist = 0.1_fix;  // Avoid division by zero
	}
	FixPoint nx_away = dx_threat / threatDist;
	FixPoint ny_away = dy_threat / threatDist;

	// Normalize squad direction (towards squad)
	FixPoint squadDist = FixPoint::sqrt(dx_squad * dx_squad + dy_squad * dy_squad);
	if (squadDist < 0.1_fix) {
		squadDist = 0.1_fix;
	}
	FixPoint nx_squad = dx_squad / squadDist;
	FixPoint ny_squad = dy_squad / squadDist;

	// Blend: 70% away from threat, 30% towards squad
	// This prioritizes safety while still moving towards friendlies
	FixPoint blend_x = nx_away * 0.7_fix + nx_squad * 0.3_fix;
	FixPoint blend_y = ny_away * 0.7_fix + ny_squad * 0.3_fix;

	// Normalize blended direction
	FixPoint blendDist = FixPoint::sqrt(blend_x * blend_x + blend_y * blend_y);
	if (blendDist < 0.1_fix) {
		blendDist = 0.1_fix;
	}
	blend_x /= blendDist;
	blend_y /= blendDist;

	// Calculate retreat distance proportional to threat proximity
	// Closer threats = longer retreat to reach weapon range edge
	FixPoint retreatDistance = desiredRange - distToThreat;
	if (retreatDistance < 1) {
		retreatDistance = 1;  // Minimum 1-tile retreat
	}

	// Calculate target position
	int targetX = lround(unitLocation.x + blend_x * retreatDistance);
	int targetY = lround(unitLocation.y + blend_y * retreatDistance);

	// Clamp to map boundaries with 1-tile safety margin
	int mapWidth = currentGameMap->getSizeX();
	int mapHeight = currentGameMap->getSizeY();
	targetX = std::max(1, std::min(mapWidth - 2, targetX));
	targetY = std::max(1, std::min(mapHeight - 2, targetY));

	// Issue move command (forced so unit actually retreats instead of immediately canceling to attack)
	doMove2Pos(pUnit, targetX, targetY, true);
    // A unit can reissue this exact retreat each AI tick. One record per
    // unit/threat encounter is enough to explain the tactical decision.
    const uint64_t kiteSignature = (uint64_t(pThreat->getObjectID()) << 16)
        | static_cast<uint64_t>(desiredRange & 0xffff);
    if (lastKiteTrace[pUnit->getObjectID()] != kiteSignature) {
        traceDecision("combat_kite", AITelemetry::Record().set("unit", pUnit->getObjectID())
            .set("target", pThreat->getObjectID()).set("distance", distToThreat.lround())
            .set("desired_range", desiredRange).set("x", targetX).set("y", targetY));
        lastKiteTrace[pUnit->getObjectID()] = kiteSignature;
    }
}

/**
 * Move a unit to the optimal squad position.
 * Chooses between actual squad center and squad rally point based on which is closer.
 * Only moves if the unit is outside the radius of both positions.
 * 
 * @param pUnit The unit to potentially move
 * @param squadRadius The acceptable radius around either position (unit won't move if within this radius)
 */
void QuantBot::moveToOptimalSquadPosition(const UnitBase* unit, FixPoint radius, int* orderBudget) {
    if (!unit || !unit->isRespondable() || humanControls(unit) || unit->hasATarget()
        || unit->wasForced() || unit->isMoving() || squadRallyLocation.isInvalid()) return;
    const_cast<UnitBase*>(unit)->setGuardPoint(squadRallyLocation);
    if (unit->getAttackMode()!=RETREAT && unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
    if (blockDistance(unit->getLocation(),squadRallyLocation)<=radius) return;
    // A queued path can exist before isMoving becomes true. Leave its destination
    // alone as well, instead of submitting a different slot on the next AI tick.
    const Coord destination=unit->getDestination();
    if (destination.isValid() && destination!=unit->getLocation()
        && blockDistance(destination,squadRallyLocation)<=radius*2) return;
    int reactiveBudget=1;
    if (!orderBudget) orderBudget=&reactiveBudget;
    if (*orderBudget<=0 || currentGame->getPathRequestQueueSize()>150) return;
    const auto offset=SimpleArmyPolicy::rallyOffset(unit->getObjectID(),radius.lround(),[&](int x,int y) {
        const Coord p=squadRallyLocation+Coord(x,y);
        return getMap().tileExists(p) && unit->canPass(p.x,p.y) && dangerAt(p)==0;
    });
    if (!offset) return;
    const Coord p=squadRallyLocation+Coord(offset->first,offset->second);
    doMove2Pos(unit,p.x,p.y,false);
    --*orderBudget;
}

/**
	Set a rally / retreat location for all our military units.
	This should be near our base but within it
	The retreat mode causes all our military units to move
	to this squad rally location

*/
void QuantBot::retreatAllUnits() {

	// Set the new squad rally location
	squadRallyLocation = findSquadRallyLocation();
	squadRetreatLocation = findSquadRetreatLocation();

	// turning this off fow now
	//retreatTimer = MILLI2CYCLES(90000);

	// If no base exists yet, there is no retreat location
	if (squadRallyLocation.isValid() && squadRetreatLocation.isValid()) {
		for (const UnitBase* pUnit : getUnitList()) {
			if (pUnit->getOwner() == getHouse()
				&& pUnit->getItemID() != Unit_Carryall
				&& pUnit->getItemID() != Unit_Sandworm
				&& pUnit->getItemID() != Unit_Harvester
				&& pUnit->getItemID() != Unit_MCV
				&& pUnit->getItemID() != Unit_Frigate) {

				doSetAttackMode(pUnit, RETREAT);
			}
		}
	}
}


/**
	In dune it is best to mass military units in one location.
	This function determines a squad leader by finding the unit with the most central location
	Amongst all of a players units.

	Rocket launchers and Ornithopters are excluded from having this role as on the
	battle field these units should always have other supporting units to work with

*/
    void QuantBot::checkAllUnits() {
    AITelemetry::PerformanceScope perfScope("ai.checkAllUnits", getGameCycleCount(), getHouse()->getHouseID());
        // Safety check: if our house is null (e.g., during game cleanup), don't check units
        if (getHouse() == nullptr) {
            return;
        }

        refreshTacticalDanger();
        releaseLegacyGroundSquad();
        if (!supportMode) squadRallyLocation = findSquadRallyLocation();
        const QuantBotConfig& config = getQuantBotConfig();
        const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
        // Defence is sized on contact. No fixed reserve owns troops or prevents
        // the main body helping when a city/harvester is under attack.
        escortAssignments.clear();
        int rallyOrdersRemaining=4;
        int combatCount=0;
        for (const auto* unit:getUnitList()) if (unit->getOwner()==getHouse() && unit->isActive()
            && unit->isAGroundUnit() && unit->canAttack()) ++combatCount;
        int rallyRadius=3;
        while (rallyRadius*rallyRadius*2<std::max(1,combatCount)) ++rallyRadius;
        for (auto it=defenceResponseCycles.begin();it!=defenceResponseCycles.end();)
            if (getGameCycleCount()-it->second>MILLI2CYCLES(30000)) it=defenceResponseCycles.erase(it); else ++it;
        // Use rally location instead of squad center to avoid constant destination changes
        Coord squadCenterLocation = squadRallyLocation;
        if(!supportMode) {
            tryLaunchOrnithopterStrike(diffSettings, config);
        }

        for (const UnitBase* pUnit : getUnitList()) {
            // Safety check: skip null units (can happen during unit destruction)
            if (pUnit == nullptr) {
                continue;
            }

            if (pUnit->getOwner()==getHouse() && humanControls(pUnit)) continue;
            // Combat spacing applies to defenders and escorts too, before their
            // strategic-role early return. Guard orders already leave targets alone.
            if (!supportMode && pUnit->getOwner() == getHouse()
                && (pUnit->getItemID() == Unit_Launcher || pUnit->getItemID() == Unit_Deviator)) {
                const auto* target = pUnit->getTarget();
                const int range = currentGame->objectData.data[pUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
                if (target && QuantBotBuildPolicy::needsKiting(
                        blockDistance(pUnit->getLocation(), target->getLocation()).lround(), range,
                        difficulty == Difficulty::Easy, target->isAUnit() && !static_cast<const UnitBase*>(target)->isAFlyingUnit())) {
                    doSetAttackMode(pUnit, AREAGUARD);
                    kiteAwayFromThreat(pUnit, target, range);
                    continue;
                }
            }

            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive()
                && QuantBotBuildPolicy::isLightRaider(pUnit->getItemID())) {
                if (const UnitBase* tank = findThreateningTank(pUnit)) {
                    doSetAttackMode(pUnit, AREAGUARD);
                    kiteAwayFromThreat(pUnit, tank, tank->getWeaponRange() + 2);
                    traceDecision("light_raider_evade", AITelemetry::Record().set("unit", pUnit->getObjectID())
                        .set("threat", tank->getObjectID()).set("reason", "tank_targeting_in_range")
                        .set("distance", blockDistance(pUnit->getLocation(), tank->getLocation()).lround())
                        .set("desired_range", tank->getWeaponRange() + 2));
                    continue;
                }
                if (!pUnit->wasForced() && pUnit->getAttackMode() == HUNT) {
                    if (const UnitBase* prey = findLightRaiderTarget(pUnit);
                        prey && prey != pUnit->getTarget()) {
                        doAttackObject(pUnit, prey, false);
                        traceDecision("light_raider_target", AITelemetry::Record().set("unit", pUnit->getObjectID())
                            .set("target", prey->getObjectID()).set("target_item", prey->getItemID())
                            .set("distance", blockDistance(pUnit->getLocation(), prey->getLocation()).lround()));
                    }
                }
            }

            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive() && pUnit->canAttack()
                && pUnit->getItemID() != Unit_Harvester && pUnit->getItemID() != Unit_Saboteur
                && !pUnit->isBadlyDamaged() && !pUnit->hasATarget() && !pUnit->wasForced()
                && pUnit->getAttackMode() != HUNT && pUnit->getAttackMode() != RETREAT
                && squadRallyLocation.isValid()) {
                moveToOptimalSquadPosition(pUnit,rallyRadius,&rallyOrdersRemaining);
                continue;
            }

            if (pUnit->getItemID() == Unit_Saboteur && pUnit->getOwner() == getHouse()) {
                logDebug("SABOTEUR CHECK: At (%d,%d) Mode=%d Target=%s Forced=%d", 
                    pUnit->getLocation().x, pUnit->getLocation().y,
                    pUnit->getAttackMode(),
                    pUnit->hasATarget() ? "Yes" : "No",
                    pUnit->wasForced() ? 1 : 0);
            }

            // Safety check: skip units with invalid owner
            if (pUnit->getOwner() == nullptr) {
                continue;
            }

		if (pUnit->getOwner() == getHouse()) {
                switch (pUnit->getItemID()) {
                case Unit_MCV: {
                    const MCV* pMCV = static_cast<const MCV*>(pUnit);
                    if (pMCV != nullptr) {
                        if (planningBuilder != NONE_ID) {
                            planningBuilder=NONE_ID;
                            clearPlacementCache();
                        }
                        //logDebug("MCV: forced: %d  moving: %d  canDeploy: %d",
                        //pMCV->wasForced(), pMCV->isMoving(), pMCV->canDeploy());

                        if (pMCV->canDeploy() && !pMCV->wasForced() && !pMCV->isMoving()
                            && !overlapsReservedStructure(pMCV->getX(),pMCV->getY(),2,2)
                            && preservesGroundAccess(Structure_ConstructionYard,pMCV->getLocation())) {
                            //logDebug("MCV: Deployed");
                            doDeploy(pMCV);
                            clearPlacementCache();
                        }
                        else if (!pMCV->isMoving() && !pMCV->wasForced()) {
                            Coord pos = findMcvPlaceLocation(pMCV);
                                doMove2Pos(pMCV, pos.x, pos.y, true);
                            /*
                            if(getHouse()->getNumItems(Unit_Carryall) > 0){
                                doRequestCarryallDrop(pMCV);
                            }*/
                        }
                    }
                } break;

                case Unit_Harvester: {
                    const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                    if(pHarvester != nullptr && pHarvester->isActive()) {
                        if (manageHarvesterSafety(pHarvester)) break;
                        // Existing check for early return with half spice
						if(getHouse()->getNumItems(Structure_Refinery) < 4
							&& getHouse()->getCredits() < 1000
							&& pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE/2) {
                            doReturn(pHarvester);
                        }

                        // Check if harvester is stuck: not moving for extended period
                        // (Regardless of what it THINKS it's doing - harvesting/returning/idle)
                        bool isMoving = pHarvester->isMoving();

                        if(!isMoving) {
                            // Harvester is not moving - increment stuck counter
                            idleHarvesterCounters[pHarvester->getObjectID()]++;
                            harvesterMovingCounters[pHarvester->getObjectID()] = 0; // Reset moving counter

                            // 10 seconds at 60 fps = 600 game cycles
                            if(idleHarvesterCounters[pHarvester->getObjectID()] >= 600) {
                                // Harvester has been stuck for 10 seconds - take action based on spice level
                                FixPoint spiceAmount = pHarvester->getAmountOfSpice();

                                // If harvester has significant spice (>300 or >40% full), tell it to return
                                if(spiceAmount > 300 || spiceAmount > (HARVESTERMAXSPICE * 2) / 5) {
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - forcing RETURN", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doReturn(pHarvester);
                                } else {
                                    // Low/no spice - reset to harvest mode
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - resetting to HARVEST", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doSetAttackMode(pHarvester, HARVEST);
                                }
                                idleHarvesterCounters[pHarvester->getObjectID()] = 0; // Reset counter
                            }
                        } else {
                            // Harvester is moving - increment moving counter
                            harvesterMovingCounters[pHarvester->getObjectID()]++;

                            // Only reset stuck counter if continuously moving for 30+ cycles (0.5 seconds)
                            // This ignores brief jitter/animation frames
                            if(harvesterMovingCounters[pHarvester->getObjectID()] >= 30) {
                                if(idleHarvesterCounters[pHarvester->getObjectID()] > 0) {
                                    idleHarvesterCounters[pHarvester->getObjectID()] = 0;
                                }
                            }
                        }
                    }
                } break;

                case Unit_Carryall: {
                } break;

                case Unit_Frigate: {
                } break;

                case Unit_Sandworm: {
                } break;

                case Unit_Ornithopter: {
                    if (!diffSettings.ornithopterAttackEnabled) {
                        if (!pUnit->hasATarget() && !pUnit->wasForced()) {
                            Coord ownBaseCentre = findBaseCentre(getHouse()->getHouseID());
                            if (ownBaseCentre.isValid() && ownBaseCentre != pUnit->getGuardPoint()) {
                                const_cast<UnitBase*>(pUnit)->setGuardPoint(ownBaseCentre.x, ownBaseCentre.y);
                            }
                        }
                    } else if(!supportMode) {
                        if (!pUnit->hasATarget() && !pUnit->wasForced()) {
                            Coord rally = findSquadRallyLocation();
                            if (rally.isValid() && rally != pUnit->getGuardPoint()) {
                                const_cast<UnitBase*>(pUnit)->setGuardPoint(rally.x, rally.y);
                            }
                        }
                    }
                } break;

                case Unit_Saboteur: {
                    // Saboteurs operate independently - always keep them in HUNT mode
                    if (pUnit->getAttackMode() != HUNT && !pUnit->wasForced()) {
                        logDebug("SABOTEUR: Unit at (%d,%d) was in mode %d, setting to HUNT", 
                            pUnit->getLocation().x, pUnit->getLocation().y, pUnit->getAttackMode());
                        doSetAttackMode(pUnit, HUNT);
                    }
                } break;

                default: {
                    if (supportMode) {
                        break;
                    }

                    int squadRadius = lround(FixPoint::sqrt(getHouse()->getNumUnits()
                        - getHouse()->getNumItems(Unit_Harvester)
                        - getHouse()->getNumItems(Unit_Carryall)
                        - getHouse()->getNumItems(Unit_Ornithopter)
                        - getHouse()->getNumItems(Unit_Sandworm)
                        - getHouse()->getNumItems(Unit_MCV))) + 1;

                    // Safety check: ensure owner is valid before comparing
                    if (pUnit->getOwner() != nullptr && pUnit->getOwner()->getHouseID() != pUnit->getOriginalHouseID()) {
                        // If its a devastator and its not ours, blow it up!!
                        if (pUnit->getItemID() == Unit_Devastator) {
                            const Devastator* pDevastator = static_cast<const Devastator*>(pUnit);
                            doStartDevastate(pDevastator);
                            doSetAttackMode(pDevastator, HUNT);
                        }
                        /*
                        else if (pUnit->getItemID() == Unit_Ornithopter) {
                            if (pUnit->getAttackMode() != HUNT) {
                                doSetAttackMode(pUnit, HUNT);
                            }
                        }*/
                        else if (pUnit->getItemID() == Unit_Harvester) {
                            const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                            if (pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE / 5) {
                                doReturn(pHarvester);
                            }
                            else {
                                    doMove2Pos(pUnit, squadCenterLocation.x, squadCenterLocation.y, true);
                            }
                        }
                        else {
                            // Send deviated unit to squad centre with tight radius (force movement)
                            if (pUnit->getAttackMode() != AREAGUARD) {
                                doSetAttackMode(pUnit, AREAGUARD);
                            }

                            // Use small radius (2 tiles) to ensure deviated units actually move to squad
                            moveToOptimalSquadPosition(pUnit, 2,&rallyOrdersRemaining);
                        }
                    }
					else if ((pUnit->getItemID() == Unit_Launcher || pUnit->getItemID() == Unit_Deviator)
                        && pUnit->hasATarget() && (difficulty != Difficulty::Easy)) {
					// Special logic to keep launchers/deviators away from harm
					const ObjectBase* pTarget = pUnit->getTarget();
					if (pTarget != nullptr && pTarget->getItemID() != Unit_Ornithopter) {
						FixPoint distToTarget = blockDistance(pUnit->getLocation(), pTarget->getLocation());
						int weaponRange = currentGame->objectData.data[pUnit->getItemID()][getHouse()->getHouseID()].weaponrange;

						// Only kite if target is dangerously close (within weaponRange - 2 tiles)
						// Launcher (range 9): kite at ≤7, Deviator (range 7): kite at ≤5
						if (distToTarget <= weaponRange - 2) {
							doSetAttackMode(pUnit, AREAGUARD);
							kiteAwayFromThreat(pUnit, pTarget, weaponRange);
                        }
                    }
                    }
                    else if (pUnit->getItemID() != Unit_Ornithopter && pUnit->getItemID() != Unit_Saboteur && pUnit->getAttackMode() != HUNT && !pUnit->hasATarget() && !pUnit->wasForced()) {
                        if (pUnit->getAttackMode() == AREAGUARD && squadCenterLocation.isValid() && (gameMode != GameMode::Campaign)) {
							if (!pUnit->hasATarget()) {
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius,&rallyOrdersRemaining);
                            }
                        }
                        else if (pUnit->getAttackMode() == RETREAT) {
                            if (!pUnit->wasForced()) {
                                if (pUnit->getHealth() < pUnit->getMaxHealth()) {
                                    doRepair(pUnit);
                                }
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius + 2,&rallyOrdersRemaining);
                            }

                            // Check if we've reached the retreat position
                            Coord actualSquadCenter = findSquadCenter(getHouse()->getHouseID());
                            FixPoint distToSquadCenter = actualSquadCenter.isValid() ? 
                                blockDistance(pUnit->getLocation(), actualSquadCenter) : FixPt_MAX;
                            FixPoint distToRallyPoint = squadRallyLocation.isValid() ? 
                                blockDistance(pUnit->getLocation(), squadRallyLocation) : FixPt_MAX;

                            // If within radius of either, we've finished retreating
                            if (distToSquadCenter <= squadRadius + 2 || distToRallyPoint <= squadRadius + 2) {
                                // We have finished retreating back to the rally point
                                doSetAttackMode(pUnit, AREAGUARD);
                            }
                        }
                        else if (pUnit->getAttackMode() == GUARD
                            && ((pUnit->getDestination() != squadRallyLocation) || (blockDistance(pUnit->getLocation(), squadRallyLocation) <= squadRadius))) {
                            // A newly deployed unit has reached the rally point, or has been diverted => Change it to area guard
                            logDebug("UNIT GUARD->AREAGUARD: %s at (%d,%d)", 
                                getItemNameByID(pUnit->getItemID()).c_str(), 
                                pUnit->getLocation().x, pUnit->getLocation().y);
                            doSetAttackMode(pUnit, AREAGUARD);
                        }
                    }
                } break;
            }
        }
    }
}

int QuantBot::queueCityRoadRepairs(const BuilderBase* yard, int limit) {
    if (limit <= 0) return 0;
    std::vector<CityRoadRepairPolicy::Footprint> buildings;
    for (const StructureBase* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isActive()) continue;
        const Coord p=structure->getLocation(), size=getStructureSize(structure->getItemID());
        buildings.push_back({p.x,p.y,size.x,size.y});
    }
    const auto sites=CityRoadRepairPolicy::candidates(buildings,[&](int x,int y) {
        if (!getMap().tileExists(x,y)) return false;
        const Tile* tile=getMap().getTile(x,y);
        return !tile->isRoadConnection() && !tile->hasCityZone() && !tile->hasAGroundObject()
            && tile->isRock() && !tile->isMountain()
            && !overlapsReservedStructure(x,y,1,1)
            && getMap().okayToPlaceStructure(x,y,1,1,false,getHouse(),false,Structure_Road);
    },[&](int x,int y) {
        return getMap().tileExists(x,y) && getMap().getTile(x,y)->isRoadConnection();
    });
    int queued=0;
    AITelemetry::Record locations;
    for (const auto& site:sites) {
        if (queued>=limit) break;
        builderPlaceLocations[yard->getObjectID()].emplace_back(site.first,site.second);
        doProduceItem(yard,Structure_Road);
        locations.set(std::to_string(queued),AITelemetry::Record().set("x",site.first).set("y",site.second));
        ++queued;
    }
    if (queued) traceDecision("city_road_repair",AITelemetry::Record().set("builder",yard->getObjectID())
        .set("rule","idle_yard_road_gaps").set("segments_queued",queued).set("locations",locations));
    return queued;
}

void QuantBot::manageCityBuilding() {
    AITelemetry::PerformanceScope perfScope("ai.manageCityBuilding", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame) return;
    auto* citySim = currentGame->getCitySimulation();
    if (!citySim || !citySim->isInitialized()) return;
    if (!currentGameMap) return;

    Coord baseCenter = findBaseCentre(getHouse()->getHouseID());
    if (!baseCenter.isValid()) return;

    // Zone structures are now built through the Construction Yard build
    // order (see the Structure_ConstructionYard case in build()).  The old
    // tile-flag approach (CMD_CITY_PLACE_ZONE without a backing structure)
    // created phantom zones that runZoneGrowth() ignored (it requires an
    // actual structure object) and that blocked real zone placement.
    //
    // Road placement remains here: roads are tile-level and don't need the
    // Construction Yard pipeline.

    // Place roads in the gaps between zones/structures. A tile gets a road
    // when it is adjacent to a zone or structure on at least one side AND
    // adjacent to an existing road or zone on at least one side (keeps the
    // network continuous). Scan outward from base center.
    int roadsPlaced = 0;
    constexpr int MAX_ROADS_PER_ROUND = 8;
    constexpr int CITY_RADIUS = 20;

    static constexpr int dx4[] = { 0, 1, 0, -1 };
    static constexpr int dy4[] = { -1, 0, 1, 0 };

    // First repair a missing transport link. City growth checks real road
    // reachability (R -> C, C -> I, I -> R), so spreading local road stubs
    // cannot help two otherwise healthy districts that are disconnected.
    struct RoadLink { Coord from; Coord to; DuneCity::CityRole sourceRole; DuneCity::CityRole targetRole; };
    auto targetRoleFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::CityRole::Commercial;
            case DuneCity::CityRole::Commercial: return DuneCity::CityRole::Industrial;
            case DuneCity::CityRole::Industrial: return DuneCity::CityRole::Residential;
            default: return DuneCity::CityRole::None;
        }
    };
    auto zoneTypeFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::ZoneType::Residential;
            case DuneCity::CityRole::Commercial: return DuneCity::ZoneType::Commercial;
            case DuneCity::CityRole::Industrial: return DuneCity::ZoneType::Industrial;
            default: return DuneCity::ZoneType::None;
        }
    };
    auto perimeterRoads = [&](Coord zone) {
        static constexpr int pdx[] = {-1,0,1,2, -1,0,1,2, -1,2, -1,2};
        static constexpr int pdy[] = {-1,-1,-1,-1, 2,2,2,2, 0,0, 1,1};
        std::vector<Coord> roads;
        for (int i = 0; i < 12; ++i) {
            const int x = zone.x + pdx[i], y = zone.y + pdy[i];
            if (currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->isRoadConnection())
                roads.emplace_back(x, y);
        }
        return roads;
    };

    struct ZoneRoad { Coord zone; DuneCity::CityRole role; std::vector<Coord> roads; };
    std::vector<ZoneRoad> zoneRoads;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const auto role = DuneCity::getStructureCityRole(structure->getItemID());
        if (role == DuneCity::CityRole::None) continue;
        auto roads = perimeterRoads(structure->getLocation());
        if (!roads.empty()) zoneRoads.push_back({structure->getLocation(), role, std::move(roads)});
    }

    DuneCity::TrafficSimulation traffic;
    traffic.init(citySim);
    RoadLink bestLink{Coord::Invalid(), Coord::Invalid(), DuneCity::CityRole::None, DuneCity::CityRole::None};
    int bestLinkDistance = std::numeric_limits<int>::max();
    for (const auto& source : zoneRoads) {
        const auto targetRole = targetRoleFor(source.role);
        if (targetRole == DuneCity::CityRole::None
            || traffic.makeTraffic(source.zone.x, source.zone.y, zoneTypeFor(targetRole)) == 1) continue;
        for (const auto& target : zoneRoads) {
            if (target.role != targetRole) continue;
            for (const auto& from : source.roads) for (const auto& to : target.roads) {
                const int distance = std::abs(from.x - to.x) + std::abs(from.y - to.y);
                if (distance > 1 && distance <= DuneCity::kMaxTrafficDistance && distance < bestLinkDistance) {
                    bestLink = {from, to, source.role, targetRole};
                    bestLinkDistance = distance;
                }
            }
        }
    }

    std::set<std::pair<int, int>> scheduledRoads;
    if (bestLink.from.isValid()) {
        std::vector<Coord> route;
        auto appendLeg = [&](Coord& cursor, int target, bool horizontal) {
            const int step = ((horizontal ? target - cursor.x : target - cursor.y) >= 0) ? 1 : -1;
            while ((horizontal ? cursor.x : cursor.y) != target) {
                if (horizontal) cursor.x += step; else cursor.y += step;
                if (cursor != bestLink.to) route.push_back(cursor);
            }
        };
        Coord cursor = bestLink.from;
        appendLeg(cursor, bestLink.to.x, true);
        appendLeg(cursor, bestLink.to.y, false);

        for (const auto& road : route) {
            if (roadsPlaced >= MAX_ROADS_PER_ROUND) break;
            if (!currentGameMap->tileExists(road.x, road.y)) break;
            Tile* tile = currentGameMap->getTile(road.x, road.y);
            if (tile->isRoadConnection()) continue;
            if (tile->hasCityZone() || tile->hasAStructure() || !tile->isRock() || tile->isMountain()) break;
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) break;
            if (!scheduledRoads.emplace(road.x, road.y).second) continue;
            currentGame->getCommandManager().addCommand(Command(getPlayerID(), CMD_CITY_TOOL,
                static_cast<Uint32>(road.x), static_cast<Uint32>(road.y),
                static_cast<Uint32>(DuneCity::CityTool_Road)));
            ++roadsPlaced;
        }
        if (roadsPlaced > 0) {
            traceDecision("city_road_link", AITelemetry::Record()
                .set("source_role", static_cast<int>(bestLink.sourceRole))
                .set("target_role", static_cast<int>(bestLink.targetRole))
                .set("from_x", bestLink.from.x).set("from_y", bestLink.from.y)
                .set("to_x", bestLink.to.x).set("to_y", bestLink.to.y)
                .set("distance", bestLinkDistance).set("segments_queued", roadsPlaced));
        }
    }

    for (int r = 1; r <= CITY_RADIUS && roadsPlaced < MAX_ROADS_PER_ROUND; r++) {
        for (int angle = 0; angle < r * 8 && roadsPlaced < MAX_ROADS_PER_ROUND; angle++) {
            int ox, oy;
            int side = angle / (r * 2);
            int pos = angle % (r * 2);
            switch (side) {
                case 0: ox = -r + pos; oy = -r; break;
                case 1: ox = r; oy = -r + pos; break;
                case 2: ox = r - pos; oy = r; break;
                default: ox = -r; oy = r - pos; break;
            }

            int tx = baseCenter.x + ox;
            int ty = baseCenter.y + oy;

            if (tx < 0 || tx >= currentGameMap->getSizeX() || ty < 0 || ty >= currentGameMap->getSizeY()) continue;
            if (!currentGameMap->tileExists(tx, ty)) continue;

            Tile* tile = currentGameMap->getTile(tx, ty);
            // Skip tiles that already have road, zone, structure, or aren't buildable
            if (tile->isRoad() || tile->hasCityZone() || tile->hasAStructure()) continue;
            if (scheduledRoads.count({tx, ty}) != 0) continue;
            if (!tile->isRock() || tile->isMountain()) continue;
            // Allow rubble tiles (destroyed structures) — road clears the rubble
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) continue;

            bool nearStructure = false;
            bool nearRoadOrStructure = false;
            for (int d = 0; d < 4; d++) {
                int nx = tx + dx4[d];
                int ny = ty + dy4[d];
                if (nx < 0 || nx >= currentGameMap->getSizeX() || ny < 0 || ny >= currentGameMap->getSizeY()) continue;
                if (!currentGameMap->tileExists(nx, ny)) continue;
                const Tile* nb = currentGameMap->getTile(nx, ny);
                if (nb->hasCityZone() || nb->hasAStructure()) nearStructure = true;
                if (nb->isRoad() || nb->hasCityZone() || nb->hasAStructure()) nearRoadOrStructure = true;
            }

            // Place road if tile is next to a structure AND connects to
            // existing road network or another structure
            if (nearStructure && nearRoadOrStructure) {
                currentGame->getCommandManager().addCommand(
                    Command(getPlayerID(), CMD_CITY_TOOL,
                            static_cast<Uint32>(tx), static_cast<Uint32>(ty),
                            static_cast<Uint32>(1)));
                scheduledRoads.emplace(tx, ty);
                roadsPlaced++;
            }
        }
    }
}
