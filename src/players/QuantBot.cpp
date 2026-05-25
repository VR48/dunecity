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
    
    // MULTIPLAYER FIX: Add deterministic attack timer variation per house
    // Spreads attacks across 75 seconds to prevent synchronized mass attacks
    const int houseID = static_cast<int>(getHouse()->getHouseID());
    const int attackVariation = (houseID - 3) * MILLI2CYCLES(15000);  // -45s to +30s variation
    attackTimer = MILLI2CYCLES(config.attackTimerMs) + attackVariation;
    
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

	// Recalculate spice every AI update cycle for both Campaign and Custom modes
	// Do this BEFORE the AI update interval check so it always happens
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


void QuantBot::onObjectWasBuilt(const ObjectBase* pObject) {
}


void QuantBot::onDecrementStructures(int itemID, const Coord& location) {
}


/// When we take losses we should hold off from attacking for longer...
void QuantBot::onDecrementUnits(int itemID) {
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
            const QuantBotConfig& config = getQuantBotConfig();
            const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
            scrambleUnitsAndDefend(pDamager, diffSettings.structureDefenders);
        }

	}
	else if (!supportMode && pObject->isAGroundUnit()) {
		const GroundUnit* pGroundUnit = static_cast<const GroundUnit*>(pObject);

		if (pGroundUnit->isAwaitingPickup()) {
			return;
		}

		// Stop him dead in his tracks if he's going to rally point
		if (pGroundUnit->wasForced() && (pGroundUnit->getItemID() != Unit_Harvester)) {
			doMove2Pos(pGroundUnit,
				pGroundUnit->getCenterPoint().x,
				pGroundUnit->getCenterPoint().y,
				false);
		}

		if (pGroundUnit->getItemID() == Unit_Harvester) {
			// Always keep Harvesters away from harm
			// Defend the harvester!
			const Harvester* pHarvester = static_cast<const Harvester*>(pGroundUnit);
			if (pHarvester->isActive() && (!pHarvester->isReturning()) && pHarvester->getAmountOfSpice() > 0) {
				const QuantBotConfig& config = getQuantBotConfig();
				const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
				scrambleUnitsAndDefend(pDamager, diffSettings.harvesterDefenders);
				doReturn(pHarvester);
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
		else if ((currentGame->techLevel > 3)
			&& (pGroundUnit->getItemID() == Unit_Quad)
			&& !pDamager->isInfantry()
			&& (pDamager->getItemID() != Unit_RaiderTrike)
			&& (pDamager->getItemID() != Unit_Trike)
			&& (pDamager->getItemID() != Unit_Quad)) {
			// We want out quads as raiders
			// Quads flee from every unit except trikes, infantry and other quads (but only if quads are not our main vehicle for that techlevel)
			doSetAttackMode(pGroundUnit, AREAGUARD);
			moveToOptimalSquadPosition(pGroundUnit, 6);  // 6 tile radius
		}
		else if ((currentGame->techLevel > 3)
			&& ((pGroundUnit->getItemID() == Unit_RaiderTrike) || (pGroundUnit->getItemID() == Unit_Trike))
			&& !pDamager->isInfantry()
			&& (pDamager->getItemID() != Unit_RaiderTrike)
			&& (pDamager->getItemID() != Unit_Trike)) {
			// Quads flee from every unit except infantry and other trikes (but only if trikes are not our main vehicle for that techlevel)
			// We want to use our light vehicles as raiders.
			// This means they are free to engage other light military units
			// but should run away from tanks

			doSetAttackMode(pGroundUnit, AREAGUARD);
			moveToOptimalSquadPosition(pGroundUnit, 6);  // 6 tile radius

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
	// Always search for best location near the MCV's current position
	// This works for both first MCV and expansion MCVs
	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();
	Coord mcvLocation = pMCV->getLocation();

	// Don't place on the very edge of the map
	for (int placeLocationX = 1; placeLocationX < getMap().getSizeX() - 1; placeLocationX++) {
		for (int placeLocationY = 1; placeLocationY < getMap().getSizeY() - 1; placeLocationY++) {
			Coord placeLocation(placeLocationX, placeLocationY);

			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, 2, 2, false, nullptr)) {
				int locationScore = 0;
				
				// Calculate distance penalty (closer is better)
				int distance = lround(blockDistance(mcvLocation, placeLocation));
				locationScore -= distance * 10;  // Strong penalty for distance - MCVs should deploy near where they spawn
				
				// Calculate available rock in the area (more buildable space is better)
				int availableRock = 0;
				int searchRadius = 12;  // Search area around potential deployment location
				
				for (int x = placeLocationX - searchRadius; x <= placeLocationX + searchRadius; x++) {
					for (int y = placeLocationY - searchRadius; y <= placeLocationY + searchRadius; y++) {
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

Coord QuantBot::findPlaceLocation(Uint32 itemID) {
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;
	
	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());
	
	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();
	
	bool itemIsBuilder = (itemID == Structure_HeavyFactory
		|| itemID == Structure_RepairYard
		|| itemID == Structure_LightFactory
		|| itemID == Structure_WOR
		|| itemID == Structure_Barracks
		|| itemID == Structure_StarPort);

	// Check all map tiles for valid building placement
	for (int placeLocationX = 0; placeLocationX <= getMap().getSizeX() - newSizeX; placeLocationX++) {
		for (int placeLocationY = 0; placeLocationY <= getMap().getSizeY() - newSizeY; placeLocationY++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, newSizeX, newSizeY,
				false, (itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {

				int locationScore = 0;
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
							locationScore += 10;  // Base linear bonus
							
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
					else if (!getMap().getTile(i, j)->isRock() && !getMap().getTile(i, j)->isConcrete()) {
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

	// Bonus for building on concrete tiles
	for (int i = placeLocationX; i < placeLocationEndX; i++) {
		for (int j = placeLocationY; j < placeLocationEndY; j++) {
			if (getMap().tileExists(i, j) && getMap().getTile(i, j)->isConcrete()) {
				locationScore += 2;  // Small bonus - concrete protects from damage
			}
		}
	}

		// Building-specific positioning
		if (itemIsBuilder || itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
			locationScore -= lround(blockDistance(squadRallyLocation, Coord(placeLocationX, placeLocationY)));
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else if (itemID == Structure_Refinery) {
			// Refineries prefer being close to spice deposits
			int closestSpiceDistance = 10000;
			for (int spiceX = 0; spiceX < getMap().getSizeX(); spiceX++) {
				for (int spiceY = 0; spiceY < getMap().getSizeY(); spiceY++) {
					if (getMap().tileExists(spiceX, spiceY) && getMap().getTile(spiceX, spiceY)->hasSpice()) {
						int spiceDistance = lround(blockDistance(Coord(placeLocationX, placeLocationY), Coord(spiceX, spiceY)));
						if (spiceDistance < closestSpiceDistance) {
							closestSpiceDistance = spiceDistance;
						}
					}
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
			if (gridOffsetX == 0 && gridOffsetY == 0) {
				locationScore += 80;  // strong grid alignment bonus
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
			locationScore += sidesWithRoad * 25;
			locationScore += sidesWithOpen * 5;
			locationScore -= sidesTouchingStructure * 30;

			// Zone-type proximity scoring:
			// R/C avoid industrial pollution (radius 5) but want it
			// within supply range (16). I clusters with itself and
			// wants residential nearby for workers.
			bool isResidential = (itemID == Structure_ZoneResidential);
			bool isCommercial  = (itemID == Structure_ZoneCommercial);
			bool isIndustrial  = (itemID == Structure_ZoneIndustrial);

			if (isResidential || isCommercial || isIndustrial) {
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

				// Pick this location if it has the best score
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = Coord(placeLocationX, placeLocationY);
				}
			}
		}
	}
	
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
						if (getMap().getTile(i, j)->isConcrete()) {
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
							else if (pTile->isConcrete()) {
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

				FixPoint score = 0;
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

Coord QuantBot::findCityTurretPlaceLocation(Uint32 itemID) {
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	// Collect key Dune builder buildings and nuclear plants for proximity scoring
	struct BuilderTarget {
		Coord pos;
		int priority;
	};
	std::vector<BuilderTarget> builders;

	for (const StructureBase* pStructure : getStructureList()) {
		if (!pStructure || !pStructure->getOwner() || pStructure->getOwner() != getHouse())
			continue;
		Uint32 sid = pStructure->getItemID();
		int prio = 0;
		if (sid == Structure_ConstructionYard)    prio = 80;
		else if (sid == Structure_NuclearPlant)   prio = 90;
		else if (sid == Structure_HeavyFactory)   prio = 70;
		else if (sid == Structure_HighTechFactory) prio = 60;
		else continue;
		builders.push_back({pStructure->getLocation(), prio});
	}

	if (builders.empty()) return Coord::Invalid();

	// Get crime rate map for scoring
	auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			if (!getMap().okayToPlaceStructure(x, y, newSizeX, newSizeY, false,
				getHouse(), false, itemID))
				continue;

			FixPoint score = 0;
			Coord candidatePos(x, y);

			// Crime rate: favour placing in high crime areas (turrets reduce crime)
			if (citySim) {
				const auto& crimeMap = citySim->getCrimeRateMap();
				int totalCrime = 0;
				for (int px = x; px < x + newSizeX; px++) {
					for (int py = y; py < y + newSizeY; py++) {
						totalCrime += crimeMap.worldGet(px, py);
					}
				}
				// Strong bonus for high crime — +1 per 3 crime (uint8 0-255)
				score += totalCrime / 3;
			}

			// Proximity to builder buildings and nuclear power
			for (const auto& b : builders) {
				FixPoint dist = blockDistance(candidatePos, b.pos);
				if (dist < 10) {
					score += FixPoint(b.priority) * (10 - dist) / 10;
				}
			}

			// Penalty for distance from base center (don't build at map edges)
			FixPoint distFromBase = blockDistance(candidatePos, baseCenter);
			score -= distFromBase;

			// Adjacency bonus — turrets should be next to buildings
			int adjacentOwn = 0;
			for (int dx = -1; dx <= newSizeX; dx++) {
				for (int dy = -1; dy <= newSizeY; dy++) {
					if ((dx == -1 || dx == newSizeX || dy == -1 || dy == newSizeY)
						&& getMap().tileExists(x + dx, y + dy)) {
						const Tile* pTile = getMap().getTile(x + dx, y + dy);
						if (pTile->hasAStructure()) {
							const StructureBase* pAdj = dynamic_cast<const StructureBase*>(pTile->getObject());
							if (pAdj && pAdj->getOwner() == getHouse())
								adjacentOwn++;
						}
					}
				}
			}
			score += adjacentOwn * 10;

			if (score > bestScore) {
				bestScore = score;
				bestLocation = Coord(x, y);
			}
		}
	}

	return bestLocation;
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
	int houseID = getHouse()->getHouseID();
	auto& data = currentGame->objectData.data;

	int itemCount[Num_ItemID];
	for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
		itemCount[i] = getHouse()->getNumItems(i);
	}

	int activeHeavyFactoryCount = 0;
	int activeHighTechFactoryCount = 0;
	int activeRepairYardCount = 0;

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
				if (pBuilder->getProductionQueueSize() > 0) {
					itemCount[pBuilder->getCurrentProducedItem()]++;
					if (pBuilder->getItemID() == Structure_HeavyFactory) {
						activeHeavyFactoryCount++;
					}
					else if (pBuilder->getItemID() == Structure_HighTechFactory) {
						activeHighTechFactoryCount++;
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


	// Second attempt at unit prioritisation
	// This algorithm calculates damage dealt over units lost value for each unit type
	// referred to as damage loss ratio (dlr)
	// It then prioritises the build of units with a higher dlr



	FixPoint dlrTank = getHouse()->getNumItemDamageInflicted(Unit_Tank) / FixPoint((1 + getHouse()->getNumLostItems(Unit_Tank)) * data[Unit_Tank][houseID].price);
	FixPoint dlrSiege = getHouse()->getNumItemDamageInflicted(Unit_SiegeTank) / FixPoint((1 + getHouse()->getNumLostItems(Unit_SiegeTank)) * data[Unit_SiegeTank][houseID].price);
	int numSpecialUnitsDamageInflicted = getHouse()->getNumItemDamageInflicted(Unit_Devastator) + getHouse()->getNumItemDamageInflicted(Unit_SonicTank) + getHouse()->getNumItemDamageInflicted(Unit_Deviator);
	int weightedNumLostSpecialUnits = (getHouse()->getNumLostItems(Unit_Devastator) * data[Unit_Devastator][houseID].price)
		+ (getHouse()->getNumLostItems(Unit_SonicTank) * data[Unit_SonicTank][houseID].price)
		+ (getHouse()->getNumLostItems(Unit_Deviator) * data[Unit_Deviator][houseID].price)
		+ 700; // middle ground 1 for special units
	FixPoint dlrSpecial = FixPoint(numSpecialUnitsDamageInflicted) / FixPoint(weightedNumLostSpecialUnits);
	FixPoint dlrLauncher = getHouse()->getNumItemDamageInflicted(Unit_Launcher) / FixPoint((1 + getHouse()->getNumLostItems(Unit_Launcher)) * data[Unit_Launcher][houseID].price);
	FixPoint dlrOrnithopter = getHouse()->getNumItemDamageInflicted(Unit_Ornithopter) / FixPoint((1 + getHouse()->getNumLostItems(Unit_Ornithopter)) * data[Unit_Ornithopter][houseID].price);

	Sint32 totalDamage = getHouse()->getNumItemDamageInflicted(Unit_Tank)
		+ getHouse()->getNumItemDamageInflicted(Unit_SiegeTank)
		+ getHouse()->getNumItemDamageInflicted(Unit_Devastator)
		+ getHouse()->getNumItemDamageInflicted(Unit_Launcher)
		+ getHouse()->getNumItemDamageInflicted(Unit_Ornithopter);

	// Harkonnen can't build ornithopers
	if (houseID == HOUSE_HARKONNEN) {
		dlrOrnithopter = 0;
	}

	// Ordos can't build Launchers
	if (houseID == HOUSE_ORDOS) {
		dlrLauncher = 0;
	}

	// Sonic tanks can get into negative damage territory
	if (dlrSpecial < 0) {
		dlrSpecial = 0;
	}

	FixPoint dlrTotal = dlrTank + dlrSiege + dlrSpecial + dlrLauncher + dlrOrnithopter;

	if (dlrTotal < 0) {
		dlrTotal = 0;
	}

	/// Calculate ratios of launcher, special and light tanks. Remainder will be tank
	FixPoint launcherPercent = dlrLauncher / dlrTotal;
	FixPoint specialPercent = dlrSpecial / dlrTotal;
	FixPoint siegePercent = dlrSiege / dlrTotal;
	FixPoint ornithopterPercent = dlrOrnithopter / dlrTotal;
	FixPoint tankPercent = dlrTank / dlrTotal;



	// If we haven't done much damage just keep all ratios at optimised defaults
	// These ratios are based on end game stats over a number of AI test runs to see
	// Which units perform. By and large launchers and siege tanks have the best damage to loss ratio
	// commenting this out for now

	// Use config values for unit ratios in early game (varies by difficulty AND house)
	if (totalDamage < 3000) {
		const QuantBotConfig& config = getQuantBotConfig();
		const QuantBotConfig::UnitRatios& ratios = config.getRatios(houseID);
		
		tankPercent = FixPoint(static_cast<int>(ratios.tank * 100)) / 100;
		siegePercent = FixPoint(static_cast<int>(ratios.siegeTank * 100)) / 100;
		launcherPercent = FixPoint(static_cast<int>(ratios.launcher * 100)) / 100;
		specialPercent = FixPoint(static_cast<int>(ratios.special * 100)) / 100;
		ornithopterPercent = FixPoint(static_cast<int>(ratios.ornithopter * 100)) / 100;
		
		if (emitStatsLog) {
			logDebug("Using config unit ratios for house %d difficulty %d - Tank: %.2f, Siege: %.2f, Launcher: %.2f, Special: %.2f, Orni: %.2f",
				houseID, static_cast<int>(difficulty), ratios.tank, ratios.siegeTank, ratios.launcher, ratios.special, ratios.ornithopter);
		}
	}

	// Cap any single unit type at 80% and redistribute excess proportionally
	// This prevents AI from building only one unit type
	auto capAndRedistribute = [&]() {
		const FixPoint maxRatio = 0.80_fix;
		const int maxIterations = 5; // Prevent infinite loops
		
		for (int iter = 0; iter < maxIterations; ++iter) {
			// Find unit with highest ratio exceeding cap
			FixPoint maxPercent = 0;
			FixPoint* pMaxUnit = nullptr;
			
			if (tankPercent > maxRatio && tankPercent > maxPercent) {
				maxPercent = tankPercent;
				pMaxUnit = &tankPercent;
			}
			if (siegePercent > maxRatio && siegePercent > maxPercent) {
				maxPercent = siegePercent;
				pMaxUnit = &siegePercent;
			}
			if (launcherPercent > maxRatio && launcherPercent > maxPercent) {
				maxPercent = launcherPercent;
				pMaxUnit = &launcherPercent;
			}
			if (specialPercent > maxRatio && specialPercent > maxPercent) {
				maxPercent = specialPercent;
				pMaxUnit = &specialPercent;
			}
			if (ornithopterPercent > maxRatio && ornithopterPercent > maxPercent) {
				maxPercent = ornithopterPercent;
				pMaxUnit = &ornithopterPercent;
			}
			
			// No unit exceeds cap, we're done
			if (pMaxUnit == nullptr) {
				break;
			}
			
			// Calculate excess to redistribute
			FixPoint excess = *pMaxUnit - maxRatio;
			*pMaxUnit = maxRatio;
			
			// Calculate total of remaining units (excluding the capped one)
			FixPoint remainingTotal = 0;
			if (pMaxUnit != &tankPercent) remainingTotal += tankPercent;
			if (pMaxUnit != &siegePercent) remainingTotal += siegePercent;
			if (pMaxUnit != &launcherPercent) remainingTotal += launcherPercent;
			if (pMaxUnit != &specialPercent) remainingTotal += specialPercent;
			if (pMaxUnit != &ornithopterPercent) remainingTotal += ornithopterPercent;
			
			// Redistribute excess proportionally to other units
			if (remainingTotal > 0) {
				if (pMaxUnit != &tankPercent) tankPercent += (tankPercent / remainingTotal) * excess;
				if (pMaxUnit != &siegePercent) siegePercent += (siegePercent / remainingTotal) * excess;
				if (pMaxUnit != &launcherPercent) launcherPercent += (launcherPercent / remainingTotal) * excess;
				if (pMaxUnit != &specialPercent) specialPercent += (specialPercent / remainingTotal) * excess;
				if (pMaxUnit != &ornithopterPercent) ornithopterPercent += (ornithopterPercent / remainingTotal) * excess;
			} else {
				// All other units are zero, distribute equally among them
				FixPoint numOtherUnits = 0;
				if (pMaxUnit != &tankPercent) numOtherUnits += 1;
				if (pMaxUnit != &siegePercent) numOtherUnits += 1;
				if (pMaxUnit != &launcherPercent) numOtherUnits += 1;
				if (pMaxUnit != &specialPercent) numOtherUnits += 1;
				if (pMaxUnit != &ornithopterPercent) numOtherUnits += 1;
				
				if (numOtherUnits > 0) {
					FixPoint equalShare = excess / numOtherUnits;
					if (pMaxUnit != &tankPercent) tankPercent += equalShare;
					if (pMaxUnit != &siegePercent) siegePercent += equalShare;
					if (pMaxUnit != &launcherPercent) launcherPercent += equalShare;
					if (pMaxUnit != &specialPercent) specialPercent += equalShare;
					if (pMaxUnit != &ornithopterPercent) ornithopterPercent += equalShare;
				}
			}
		}
	};
	
	capAndRedistribute();

	// lets analyse damage inflicted

	if (emitStatsLog) {
		logDebug("Dmg: %d DLR: %f", totalDamage, dlrTotal.toFloat());

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

	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() == getHouse()) {
			if ((pStructure->isRepairing() == false)
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
			else if ((pStructure->isRepairing() == false) && money > 5000) {
				// Repair if we are rich
				doRepair(pStructure);
			}
			else if (pStructure->getItemID() == Structure_RocketTurret) {
				if (!getGameInitSettings().getGameOptions().structuresDegradeOnConcrete || pStructure->hasATarget()) {
					doRepair(pStructure);
				}
			}
			// Windtrap repair: Keep windtraps at max health to maintain power buffer
			// Damaged windtraps produce less power, so repair them to maintain 200 power surplus
			else if (pStructure->getItemID() == Structure_WindTrap
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
				
				// Log all builder status for campaign AIs (not just CY)
				if (gameMode == GameMode::Campaign && !supportMode && pStructure->getItemID() != Structure_ConstructionYard) {
					logDebug("PRODUCTION: %s - Upgrading:%d Queue:%d Credits:%d", 
						getItemNameByID(pStructure->getItemID()).c_str(),
						pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(), money);
				}
				
				// Helper to log production for campaign enemy bots
				auto produceItemWithLogging = [&](Uint32 itemID) {
					if (gameMode == GameMode::Campaign && !supportMode && currentGame) {
						std::string itemName = getItemNameByID(itemID);
						logDebug("Queuing %s (ID:%d)", itemName.c_str(), itemID);
					}
					doProduceItem(pBuilder, itemID);
				};
				
				switch (pStructure->getItemID()) {

			case Structure_LightFactory: {
				if (!pBuilder->isUpgrading()
					&& (gameMode == GameMode::Campaign || (currentGame && currentGame->isCitySimEnabled()))
					&& money > (currentGame && currentGame->isCitySimEnabled() ? 500 : 1000)
					&& itemCount[Structure_HeavyFactory] == 0  // Only produce from Light Factory if no Heavy Factory exists
					&& pBuilder->getProductionQueueSize() < 1
					&& pBuilder->getBuildListSize() > 0
					&& militaryValue < militaryValueLimit) {

					if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel() && getHouse()->getCredits() > 1500) {
						doUpgrade(pBuilder);
					}
					else if (!getHouse()->isGroundUnitLimitReached()) {
						Uint32 itemID = NONE_ID;

						if (pBuilder->isAvailableToBuild(Unit_RaiderTrike)) {
							itemID = Unit_RaiderTrike;
						}
						else if (pBuilder->isAvailableToBuild(Unit_Quad)) {
							itemID = Unit_Quad;
						}
						else if (pBuilder->isAvailableToBuild(Unit_Trike)) {
							itemID = Unit_Trike;
						}

						if (itemID != NONE_ID) {
							produceItemWithLogging(itemID);
							itemCount[itemID]++;
						}
					}
				}
			} break;

		case Structure_WOR: {
			// QuantBot does not produce infantry from WOR - disabled
			// Units will not be produced even if WOR exists
		} break;

		case Structure_Barracks: {
			// QuantBot does not produce infantry from Barracks - disabled
			// Units will not be produced even if Barracks exists
		} break;

				case Structure_HighTechFactory: {
					int ornithopterValue = data[Unit_Ornithopter][houseID].price * itemCount[Unit_Ornithopter];
					
					if (pBuilder->isAvailableToBuild(Unit_Carryall)
						&& itemCount[Unit_Carryall] < (militaryValue + itemCount[Unit_Harvester] * 500) / 3000
						&& (pBuilder->getProductionQueueSize() < 1)
						&& money > 1000
						&& !getHouse()->isAirUnitLimitReached()) {
						produceItemWithLogging(Unit_Carryall);
						itemCount[Unit_Carryall]++;
					}
					else if ((money > 500) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
						if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
							doUpgrade(pBuilder);
						}
						else {
							doRepair(pBuilder);
						}
					}
					else if (pBuilder->isAvailableToBuild(Unit_Ornithopter)
						&& (militaryValue * ornithopterPercent > ornithopterValue)
						&& (pBuilder->getProductionQueueSize() < 1)
						&& !getHouse()->isAirUnitLimitReached()
						&& money > 1200) {
						// Current value and what percentage of military we want used to determine
						// whether to build an additional unit.
						produceItemWithLogging(Unit_Ornithopter);
						itemCount[Unit_Ornithopter]++;
						money -= data[Unit_Ornithopter][houseID].price;
						militaryValue += data[Unit_Ornithopter][houseID].price;
					}
				} break;

				case Structure_HeavyFactory: {
					// only if the factory isn't busy
					if ((pBuilder->isUpgrading() == false) && (pBuilder->getProductionQueueSize() < 1) && (pBuilder->getBuildListSize() > 0)) {
						// we need a construction yard. Build an MCV if we don't have a starport
						if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
							&& itemCount[Unit_MCV] + itemCount[Structure_ConstructionYard] + itemCount[Structure_StarPort] < 1
							&& pBuilder->isAvailableToBuild(Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()) {
							produceItemWithLogging(Unit_MCV);
							itemCount[Unit_MCV]++;
						}
						else if ((money > 10000) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								doUpgrade(pBuilder);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (gameMode == GameMode::Custom
							&& pBuilder->isAvailableToBuild(Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()) {
							// City sim: 1 CY base + 1 per 150 credits/second income
							// Non-city: scales with money (1 per 4000 credits)
							int currentCYs = itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV];
							int desiredCYs = 1;
							if (currentGame && currentGame->isCitySimEnabled()) {
								auto* citySim = currentGame->getCitySimulation();
								if (citySim) {
									int totalPop = citySim->getTotalPop();
									int tax = citySim->getCityTax();
									int avgLV = citySim->getAvgLandValue();
									int32_t annual = DuneCity::computeAnnualTaxRevenue(totalPop, tax, avgLV);
									int creditsPerSec = annual / 60;
									desiredCYs = 1 + creditsPerSec / 150;
								}
							} else {
								desiredCYs = money / 4000;
							}
							if (currentCYs < desiredCYs) {
								produceItemWithLogging(Unit_MCV);
								itemCount[Unit_MCV]++;
								logDebug("MCV: Building MCV %d/%d (city-income scaling)",
									currentCYs + 1, desiredCYs);
							}
						}
						else if (gameMode == GameMode::Custom
							&& pBuilder->isAvailableToBuild(Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Unit_Harvester] < militaryValue / 1000
							&& itemCount[Unit_Harvester] < harvesterLimit) {
							// In case we get given lots of money, it will eventually run out so we need to be prepared
							produceItemWithLogging(Unit_Harvester);
							itemCount[Unit_Harvester]++;
						}
						else if (itemCount[Unit_Harvester] < harvesterLimit
							&& pBuilder->isAvailableToBuild(Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& (money < 2000 || gameMode == GameMode::Campaign)) {
							//logDebug("*Building a Harvester.",
							//itemCount[Unit_Harvester], harvesterLimit, money);
							produceItemWithLogging(Unit_Harvester);
							itemCount[Unit_Harvester]++;
						}
						else if ((money > 500) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								doUpgrade(pBuilder);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (money > (currentGame && currentGame->isCitySimEnabled() ? 500 : 2000)
							&& militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached()) {
							// Limit enemy military units based on difficulty

							// Calculate current value of units
							int launcherValue = data[Unit_Launcher][houseID].price * itemCount[Unit_Launcher];
							int specialValue = data[Unit_Devastator][houseID].price * itemCount[Unit_Devastator]
								+ data[Unit_Deviator][houseID].price * itemCount[Unit_Deviator]
								+ data[Unit_SonicTank][houseID].price * itemCount[Unit_SonicTank];
							int siegeValue = data[Unit_SiegeTank][houseID].price * itemCount[Unit_SiegeTank];


							/// Use current value and what percentage of military we want to determine
							/// whether to build an additional unit.
							if (pBuilder->isAvailableToBuild(Unit_Launcher) && (militaryValue * launcherPercent > launcherValue)) {
								produceItemWithLogging(Unit_Launcher);
								itemCount[Unit_Launcher]++;
								money -= data[Unit_Launcher][houseID].price;
								militaryValue += data[Unit_Launcher][houseID].price;
							}
							else if (pBuilder->isAvailableToBuild(Unit_Devastator) && (militaryValue * specialPercent > specialValue)) {
								produceItemWithLogging(Unit_Devastator);
								itemCount[Unit_Devastator]++;
								money -= data[Unit_Devastator][houseID].price;
								militaryValue += data[Unit_Devastator][houseID].price;
							}
							else if (pBuilder->isAvailableToBuild(Unit_SonicTank) && (militaryValue * specialPercent > specialValue)) {
								produceItemWithLogging(Unit_SonicTank);
								itemCount[Unit_SonicTank]++;
								money -= data[Unit_SonicTank][houseID].price;
								militaryValue += data[Unit_SonicTank][houseID].price;
							}
							else if (pBuilder->isAvailableToBuild(Unit_Deviator) && (militaryValue * specialPercent > specialValue)) {
								produceItemWithLogging(Unit_Deviator);
								itemCount[Unit_Deviator]++;
								money -= data[Unit_Deviator][houseID].price;
								militaryValue += data[Unit_Deviator][houseID].price;
							}
							else if (pBuilder->isAvailableToBuild(Unit_SiegeTank) && (militaryValue * siegePercent > siegeValue)) {
								produceItemWithLogging(Unit_SiegeTank);
								itemCount[Unit_SiegeTank]++;
								money -= data[Unit_Tank][houseID].price;
								militaryValue += data[Unit_SiegeTank][houseID].price;
							}
							else if (pBuilder->isAvailableToBuild(Unit_Tank)) {
								// Tanks for all else
								produceItemWithLogging(Unit_Tank);
								itemCount[Unit_Tank]++;
								money -= data[Unit_Tank][houseID].price;
								militaryValue += data[Unit_Tank][houseID].price;
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
							produceItemWithLogging(Unit_MCV);
							itemCount[Unit_MCV]++;
							money = money - choam.getPrice(Unit_MCV);
						}

						if (money > choam.getPrice(Unit_Carryall) && choam.getNumAvailable(Unit_Carryall) > 0 && itemCount[Unit_Carryall] == 0) {
							// Get at least one Carryall
							produceItemWithLogging(Unit_Carryall);
							itemCount[Unit_Carryall]++;
							money = money - choam.getPrice(Unit_Carryall);
						}

						while (money > choam.getPrice(Unit_Harvester) && choam.getNumAvailable(Unit_Harvester) > 0 && itemCount[Unit_Harvester] < harvesterLimit) {
							produceItemWithLogging(Unit_Harvester);
							itemCount[Unit_Harvester]++;
							money = money - choam.getPrice(Unit_Harvester);
						}

						int itemCountUnits = itemCount[Unit_Tank] + itemCount[Unit_SiegeTank] + itemCount[Unit_Launcher] + itemCount[Unit_Harvester];

						while (money > choam.getPrice(Unit_Carryall) && choam.getNumAvailable(Unit_Carryall) > 0 && itemCount[Unit_Carryall] < itemCountUnits / 7) {
							produceItemWithLogging(Unit_Carryall);
							itemCount[Unit_Carryall]++;
							money = money - choam.getPrice(Unit_Carryall);
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_SiegeTank) && choam.getNumAvailable(Unit_SiegeTank) > 0
							&& choam.isCheap(Unit_SiegeTank) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_SiegeTank);
							itemCount[Unit_SiegeTank]++;
							money = money - choam.getPrice(Unit_SiegeTank);
							militaryValue += data[Unit_SiegeTank][houseID].price;
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Launcher) && choam.getNumAvailable(Unit_Launcher) > 0
							&& choam.isCheap(Unit_Launcher) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Launcher);
							itemCount[Unit_Launcher]++;
							money = money - choam.getPrice(Unit_Launcher);
							militaryValue += data[Unit_Launcher][houseID].price;
						}

						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Tank) && choam.getNumAvailable(Unit_Tank) > 0
							&& choam.isCheap(Unit_Tank) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Tank);
							itemCount[Unit_Tank]++;
							money = money - choam.getPrice(Unit_Tank);
							militaryValue += data[Unit_Tank][houseID].price;
						}



						while (militaryValue < militaryValueLimit && money > choam.getPrice(Unit_Ornithopter) && choam.getNumAvailable(Unit_Ornithopter) > 0
							&& choam.isCheap(Unit_Ornithopter) && militaryValue < militaryValueLimit && money > 2000) {
							produceItemWithLogging(Unit_Ornithopter);
							itemCount[Unit_Ornithopter]++;
							money = money - choam.getPrice(Unit_Ornithopter);
							militaryValue += data[Unit_Ornithopter][houseID].price;
						}



						doPlaceOrder(pStarPort);
					}

				} break;

				case Structure_ConstructionYard: {

					// If rocket turrets don't need power then let's build some for defense
					int rocketTurretValue = itemCount[Structure_RocketTurret] * 250;

					// disable rocket turrets for now
					if (getGameInitSettings().getGameOptions().rocketTurretsNeedPower || true) {
						rocketTurretValue = 1000000; // If rocket turrets need power we don't want to build them
					}

				const ConstructionYard* pConstYard = static_cast<const ConstructionYard*>(pBuilder);

				// Only log production status when something changes (not every cycle)
				static int lastQueueSize = -1;
				static bool lastUpgrading = false;
				static int lastBuildListSize = -1;
				
				if(pBuilder->getProductionQueueSize() != lastQueueSize || 
				   pBuilder->isUpgrading() != lastUpgrading || 
				   pBuilder->getBuildListSize() != lastBuildListSize) {
					logDebug("PRODUCTION: CY Status - Upgrading:%d Queue:%d Credits:%d BuildList:%d", 
						pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(), money, pBuilder->getBuildListSize());
					lastQueueSize = pBuilder->getProductionQueueSize();
					lastUpgrading = pBuilder->isUpgrading();
					lastBuildListSize = pBuilder->getBuildListSize();
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
								produceItemWithLogging(i);
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
							else if ((getHouse()->getProducedPower() < getHouse()->getPowerRequirement())
								&& pBuilder->getProductionQueueSize() == 0) {
								// Prefer nuclear plant over windtrap
								if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
									&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
									produceItemWithLogging(Structure_NuclearPlant);
									itemCount[Structure_NuclearPlant]++;
									logDebug("***CampAI Build Nuclear Plant: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
									&& findPlaceLocation(Structure_WindTrap).isValid()) {
									produceItemWithLogging(Structure_WindTrap);
									itemCount[Structure_WindTrap]++;
									logDebug("***CampAI Build windtrap: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								}
							}
							else if ((getHouse()->getStoredCredits() > getHouse()->getCapacity() * 0.90_fix)  // Only build when 90% full
								&& pBuilder->isAvailableToBuild(Structure_Silo)
								&& findPlaceLocation(Structure_Silo).isValid()
								&& pBuilder->getProductionQueueSize() == 0) {

								produceItemWithLogging(Structure_Silo);
								itemCount[Structure_Silo]++;

								logDebug("***CampAI Build A new Silo increasing count to: %d (credits: %d/%d)", itemCount[Structure_Silo], getHouse()->getStoredCredits().lround(), getHouse()->getCapacity());
							}
							else if (money > 3000
								&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
								&& findTurretPlaceLocation(Structure_RocketTurret).isValid()
								&& pBuilder->getProductionQueueSize() == 0
								&& (itemCount[Structure_RocketTurret] <
									(itemCount[Structure_Silo] + itemCount[Structure_Refinery]) * 2)) {

								produceItemWithLogging(Structure_RocketTurret);
								itemCount[Structure_RocketTurret]++;

								logDebug("***CampAI Build A new Rocket turret increasing count to: %d", itemCount[Structure_RocketTurret]);
							}
							// City zone structures for campaign AI — pick by demand valve
							else if (currentGame && currentGame->isCitySimEnabled()
								&& money > 200
								&& pBuilder->getProductionQueueSize() == 0
								&& itemCount[Structure_WindTrap] > 0) {
								auto* citySim = currentGame->getCitySimulation();
								int16_t rValve = citySim ? citySim->getResValve() : 0;
								int16_t cValve = citySim ? citySim->getComValve() : 0;
								int16_t iValve = citySim ? citySim->getIndValve() : 0;

								Uint32 zoneID = Structure_ZoneResidential;
								if (cValve > rValve && cValve >= iValve)
									zoneID = Structure_ZoneCommercial;
								else if (iValve > rValve && iValve > cValve)
									zoneID = Structure_ZoneIndustrial;

								// Residential zones require heavy factory economy (money > 500)
								if (zoneID == Structure_ZoneResidential && money <= 500)
									zoneID = NONE_ID;

								if (zoneID != NONE_ID && pBuilder->isAvailableToBuild(zoneID)
									&& findPlaceLocation(zoneID).isValid()) {
									produceItemWithLogging(zoneID);
									itemCount[zoneID]++;
									logDebug("***CampAI CITY-ZONE: Building %s (valves=R%+d C%+d I%+d)",
										getItemNameByID(zoneID).c_str(), rValve, cValve, iValve);
								}
							}

							// MULTIPLAYER FIX: Use deterministic timer instead of random
							buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
						}
						else {
								// custom AI starts here:

								Uint32 itemID = NONE_ID;
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
								int requiredTurrets = std::max(maxEnemyOrnithopters * 2, totalEnemyOrnithopters);

								// Power buffer check for rocket turrets (2 windtraps = 200 power buffer + 25 turret = 225)
								// Only applies if rocketTurretsNeedPower is enabled
								auto hasPowerBufferForTurret = [&]() {
									if (!getGameInitSettings().getGameOptions().rocketTurretsNeedPower) {
										return true; // No power requirement, always allow
									}
									int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
									// Need 225 (200 buffer + 25 turret cost) so we maintain 200 after building
									return powerExcess >= 225;
								};

								// CRITICAL: Counter enemy ornithopters ASAP (prep prerequisites if needed)
								if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& maxEnemyOrnithopters > 0
									&& itemCount[Structure_RocketTurret] < requiredTurrets) {
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
									} else if (!hasWindtrap && pBuilder->isAvailableToBuild(Structure_WindTrap)) {
						itemID = Structure_WindTrap;
										logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasRadar && pBuilder->isAvailableToBuild(Structure_Radar) && getHouse()->hasPower()) {
						itemID = Structure_Radar;
										logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasPowerBufferForTurret()) {
										int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
										if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
											&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
											itemID = Structure_NuclearPlant;
											logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
										} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
											&& findPlaceLocation(Structure_WindTrap).isValid()) {
											itemID = Structure_WindTrap;
											logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
										}
									} else if (pBuilder->isAvailableToBuild(Structure_RocketTurret)
							&& findTurretPlaceLocation(Structure_RocketTurret).isValid()
										&& hasPowerBufferForTurret()) {
							itemID = Structure_RocketTurret;
										logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", maxEnemyOrnithopters, requiredTurrets);
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
					&& pBuilder->isAvailableToBuild(Structure_WindTrap)) {
						itemID = Structure_WindTrap;
					}
				// 1b. Power Deficit Recovery - prefer nuclear plant, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getHouse()->getProducedPower() < getHouse()->getPowerRequirement()) {
					int powerDeficit = getHouse()->getPowerRequirement() - getHouse()->getProducedPower();
					if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant;
						logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
					} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap;
						logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
					}
				}
				// 1c. City-mode proportional power buffer.
				//
				// Buffer scales with zone power demand: 10% surplus over
				// current powerRequirement. No zones → no buffer needed
				// (step 1 already builds the first windtrap). This avoids
				// the AI blowing its entire bank on windtraps at game start
				// when there are no zones yet.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()) {
					const int produced  = getHouse()->getProducedPower();
					const int required  = getHouse()->getPowerRequirement();
					const int buffer    = produced - required;
					const int targetBuffer = required / 10;  // 10% surplus
					if (required > 0 && buffer < targetBuffer) {
						// Prefer Nuclear: one plant = 10 Windtraps, and a
						// city packed with zones has limited rock left for
						// more Windtrap footprints.
						if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant;
							logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
									 buffer, targetBuffer, required);
						} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap;
							logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d)",
									 buffer, targetBuffer);
						}
					}
				}
				// 2. Refinery (if 0)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_Refinery] == 0 
					&& pBuilder->isAvailableToBuild(Structure_Refinery)) {
					itemID = Structure_Refinery;
					if (itemCount[Unit_Harvester] < harvesterLimit) {
						itemCount[Unit_Harvester]++;
					}
				}
				// Low-spice economy: skip additional refineries, pivot to R/I/C zones
				const bool lowSpiceEconomy = (lastCalculatedSpice < 500);

				// 3. Refinery (ratio: 1 refinery per 3 harvesters) — skip when no spice
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !lowSpiceEconomy
					&& itemCount[Structure_Refinery] < itemCount[Unit_Harvester] / 3
			&& pBuilder->isAvailableToBuild(Structure_Refinery)
			&& !(gameMode == GameMode::Campaign && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery;
						if (itemCount[Unit_Harvester] < harvesterLimit) {
							itemCount[Unit_Harvester]++;
						}
					}
				// 4. Refinery (< 4, money < 2000) - get free harvester when low on credits
				//    Skip when spice is scarce — refineries won't help, build zones instead
				if (itemID == NONE_ID && !skipRemainingStructureLogic
				&& !lowSpiceEconomy
				&& gameMode != GameMode::Campaign
				&& itemCount[Structure_Refinery] < 4
				&& pBuilder->isAvailableToBuild(Structure_Refinery)
					&& money < 2000) {
					itemID = Structure_Refinery;
					if (itemCount[Unit_Harvester] < harvesterLimit) {
						itemCount[Unit_Harvester]++;
					}
				}
				// 5. StarPort (skip if nothing available/enabled in CHOAM and no heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
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
					itemID = Structure_StarPort;
				}
				// 6. Radar
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_Radar] == 0 
					&& pBuilder->isAvailableToBuild(Structure_Radar) 
					&& money > 500) {
				itemID = Structure_Radar;
			}
				// 7. Light Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_LightFactory] == 0
					&& pBuilder->isAvailableToBuild(Structure_LightFactory)
					&& money > 500) {
					itemID = Structure_LightFactory;
				}
				// 8. Repair Yard (only if starport or heavy factory exists)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] == 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->isAvailableToBuild(Structure_RepairYard)) {
					itemID = Structure_RepairYard;
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
							&& itemCount[Structure_RocketTurret] < 2))) {
					if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
						doRepair(pBuilder);
						logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
					} else if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
						doUpgrade(pBuilder);
						logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", pBuilder->getCurrentUpgradeLevel() + 1);
					}
				}
								// Helper to check if any windtraps need repair (damaged = less power)
								auto repairDamagedWindtraps = [&]() -> bool {
									for (const StructureBase* pStructure : getStructureList()) {
										if (pStructure->getOwner() == getHouse() 
											&& pStructure->getItemID() == Structure_WindTrap
							&& pStructure->getHealth() < pStructure->getMaxHealth()
							&& !pStructure->isRepairing()) {
							doRepair(pStructure);
							logDebug("TURRET-POWER: Repairing damaged windtrap for max power generation");
							return true; // Repairing one
						}
					}
					return false; // None need repair
				};
				
				// 8b-pre-repair. Repair damaged windtraps before building turrets (damaged = less power)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getGameInitSettings().getGameOptions().rocketTurretsNeedPower
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& !hasPowerBufferForTurret()) {
					// Try to repair damaged windtraps first - they produce less power when damaged
					repairDamagedWindtraps();
				}
				
				// 8b-pre. Build power for turret buffer — prefer nuclear, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getGameInitSettings().getGameOptions().rocketTurretsNeedPower
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& !hasPowerBufferForTurret()) {
					int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant;
						logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: 225)", powerExcess);
					} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap;
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
					&& findTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret;
					logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", itemCount[Structure_RocketTurret] + 1);
				}
				// 8c. Counter enemy ornithopters (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)
					&& findTurretPlaceLocation(Structure_RocketTurret).isValid()
					&& [&]() {
						int maxEnemyOrnithopters = 0;
						if (currentGame) {
							for (int i = 0; i < NUM_HOUSES; i++) {
								const House* pHouse = currentGame->getHouse(i);
								if (pHouse && pHouse->getTeamID() != getHouse()->getTeamID()) {
									int houseOrnis = pHouse->getNumItems(Unit_Ornithopter);
									if (houseOrnis > maxEnemyOrnithopters) {
										maxEnemyOrnithopters = houseOrnis;
									}
								}
							}
						}
						return (maxEnemyOrnithopters > 0 && itemCount[Structure_RocketTurret] < maxEnemyOrnithopters * 2);
					}()) {
					itemID = Structure_RocketTurret;
					logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
				}
				// 9. Heavy Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_HeavyFactory] == 0
					&& pBuilder->isAvailableToBuild(Structure_HeavyFactory)
					&& money > 500) {
					itemID = Structure_HeavyFactory;
					logDebug("Build first Heavy Factory... money: %d", money);
				}
				// 10. High Tech Factory (first one - after heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_HighTechFactory] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& pBuilder->isAvailableToBuild(Structure_HighTechFactory)
					&& money > 1000) {
					itemID = Structure_HighTechFactory;
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
					itemID = Structure_IX;
					logDebug("Build IX... money: %d", money);
				}
				// 12. Additional Heavy Factories (expansion)
						// Requirements are progressive based on tech level:
						// Tech 4: No prerequisites (just money and need)
						// Tech 5-6: Require Repair Yard
						// Tech 7+: Require Repair Yard + IX
				if (itemID == NONE_ID && !skipRemainingStructureLogic
								&& money > 3000 && pBuilder->isAvailableToBuild(Structure_HeavyFactory)
								&& (activeHeavyFactoryCount >= itemCount[Structure_HeavyFactory] || itemCount[Structure_HeavyFactory] < 1 + money / 4000)) {

								int techLevel = currentGame ? currentGame->techLevel : 8;
								bool prerequisitesMet = false;

							if (techLevel <= 4) {
								// Tech 4: Can build additional Heavy Factories without prerequisites
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
									itemID = Structure_HeavyFactory;
									logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  limit: %d  tech: %d",
										activeHeavyFactoryCount, getHouse()->getNumItems(Structure_HeavyFactory), money, 1 + money / 4000, techLevel);
								}
							}
				// 13. Refineries for harvester ratio — skip when no spice
				if (itemID == NONE_ID && !skipRemainingStructureLogic
						&& !lowSpiceEconomy
						&& ((itemCount[Structure_Refinery] * 3.5_fix < itemCount[Unit_Harvester])
					|| (currentGame && currentGame->techLevel < 4))
						&& pBuilder->isAvailableToBuild(Structure_Refinery)
						&& !(gameMode == GameMode::Campaign && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery;
						// Only increment if below limit (free harvester will only spawn if below limit)
						if (itemCount[Unit_Harvester] < harvesterLimit) {
							itemCount[Unit_Harvester]++;
						}
					}
				// 14. Additional Repair Yards (1 per 6000 military value)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
							&& pBuilder->isAvailableToBuild(Structure_RepairYard) && money > 2000
							&& itemCount[Structure_RepairYard] * 6000 < militaryValue) {
							itemID = Structure_RepairYard;
							logDebug("Build Repair Yard: have %d, need %d (military: %d)", itemCount[Structure_RepairYard], (militaryValue / 6000) + 1, militaryValue);
						}
				// 15. Additional High Tech Factories (if all existing ones are busy)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > 3000 && pBuilder->isAvailableToBuild(Structure_HighTechFactory)
									&& itemCount[Structure_HighTechFactory] > 0 && activeHighTechFactoryCount >= itemCount[Structure_HighTechFactory]) {
									itemID = Structure_HighTechFactory;
								}
				// 16. Silos (when storage is 80%+ full)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_HeavyFactory] > 0
					&& getHouse()->getStoredCredits() > getHouse()->getCapacity() * 0.80_fix
					&& pBuilder->isAvailableToBuild(Structure_Silo)) {
									itemID = Structure_Silo;
					logDebug("Build Silo - storage at %d/%d", getHouse()->getStoredCredits().lround(), getHouse()->getCapacity());
								}
				// 17. City protection turrets (city sim only) — before Palace
				//     Floor: 1 turret per CY + nuclear + heavy factory
				//     Beyond floor: only build if city has crime (avg crime > 0)
				//     Uses findCityTurretPlaceLocation which prioritises high
				//     crime areas and proximity to builder buildings + nuclear.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 500
					&& hasPowerBufferForTurret()
					&& pBuilder->isAvailableToBuild(Structure_RocketTurret)) {
					int keyBuildings = itemCount[Structure_ConstructionYard]
						+ itemCount[Structure_NuclearPlant]
						+ itemCount[Structure_HeavyFactory];
					int turretFloor = keyBuildings;
					int currentTurrets = itemCount[Structure_RocketTurret];
					bool shouldBuild = false;

					if (currentTurrets < turretFloor) {
						// Below minimum — always build
						shouldBuild = true;
					} else if (auto* citySim = currentGame->getCitySimulation()) {
						// Above minimum — only build if there's crime to suppress
						const auto& crimeMap = citySim->getCrimeRateMap();
						int maxCrime = 0;
						for (int cx = 0; cx < citySim->getMapWidth(); cx++) {
							for (int cy = 0; cy < citySim->getMapHeight(); cy++) {
								int c = crimeMap.worldGet(cx, cy);
								if (c > maxCrime) maxCrime = c;
							}
						}
						// Only add turrets if there's meaningful crime (>30 on 0-255 scale)
						shouldBuild = (maxCrime > 30);
					}

					if (shouldBuild) {
						Coord loc = findCityTurretPlaceLocation(Structure_RocketTurret);
						if (loc.isValid()) {
							itemID = Structure_RocketTurret;
							logDebug("CITY-TURRET: Building rocket turret %d (floor=%d, crime-driven=%s)",
								currentTurrets + 1, turretFloor,
								currentTurrets >= turretFloor ? "yes" : "no");
						}
					}
				}
				// 17b. Palace (after military infrastructure)
				//       City sim: 1 palace per 30000 population
				{
				bool palaceAllowed;
				if (currentGame && currentGame->isCitySimEnabled()) {
					int cityPop = 0;
					if (auto* citySim = currentGame->getCitySimulation()) {
						cityPop = citySim->getTotalPop();
					}
					palaceAllowed = (itemCount[Structure_Palace] < 1 + cityPop / 25);
				} else {
					palaceAllowed = (itemCount[Structure_Palace] == 0 || !getGameInitSettings().getGameOptions().onlyOnePalace);
				}
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > 5000
									&& pBuilder->isAvailableToBuild(Structure_Palace)
									&& palaceAllowed
									&& itemCount[Structure_HeavyFactory] > 0
									&& itemCount[Structure_LightFactory] > 0) {
								itemID = Structure_Palace;
							}
				}
				// 18. City zone structures (when city sim is active)
				// Zones are 2x2 structures built via the CY; runZoneGrowth()
				// requires an actual structure object, so tile-flag placement
				// (CMD_CITY_PLACE_ZONE without a structure) does not work.
				// 18b. Civic buildings: Stadium (resPop > 500) and Airport (comPop > 100)
				//      Build these before more zones to unlock civic caps.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 500) {
					auto* citySim = currentGame->getCitySimulation();
					if (citySim) {
						// Stadium: needed when resPop exceeds 500 (SC Classic civic cap)
						if (!citySim->getHasStadium()
							&& citySim->getResPop() > 500
							&& pBuilder->isAvailableToBuild(Structure_Stadium)
							&& findPlaceLocation(Structure_Stadium).isValid()) {
							itemID = Structure_Stadium;
							logDebug("CITY-CIVIC: Building Stadium (resPop=%d > 500, no stadium)",
								citySim->getResPop());
						}
						// Airport: needed when comPop exceeds 100 (SC Classic civic cap)
						else if (!citySim->getHasAirport()
							&& citySim->getComPop() > 100
							&& pBuilder->isAvailableToBuild(Structure_Airport)
							&& findPlaceLocation(Structure_Airport).isValid()) {
							itemID = Structure_Airport;
							logDebug("CITY-CIVIC: Building Airport (comPop=%d > 100, no airport)",
								citySim->getComPop());
						}
					}
				}

				// Zone type is chosen by demand valves: build whichever R/I/C
				// has the highest positive demand. Falls back to R if all
				// valves are equal or negative.
				// Prerequisites scale with tech level:
				//   Tech 1-3: windtrap + refinery + light factory
				//   Tech 4-5: + heavy factory
				//   Tech 6+:  + starport
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 200
					&& itemCount[Structure_WindTrap] > 0
					&& itemCount[Structure_Refinery] > 0
					&& (lowSpiceEconomy || itemCount[Structure_LightFactory] > 0)
					&& (lowSpiceEconomy || currentGame->techLevel < 4 || itemCount[Structure_HeavyFactory] > 0)
					&& (lowSpiceEconomy || currentGame->techLevel < 6 || itemCount[Structure_StarPort] > 0)) {
					// Zones consume power as they grow. Before placing one,
					// ensure we have surplus power. If not, build a nuclear
					// plant (or windtrap fallback) first.
					constexpr int kZonePowerHeadroom = 24;  // worst case: industrial L3
					const int powerSurplus = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if (powerSurplus < kZonePowerHeadroom) {
						if (pBuilder->isAvailableToBuild(Structure_NuclearPlant)
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant;
							logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						} else if (pBuilder->isAvailableToBuild(Structure_WindTrap)
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap;
							logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						}
					} else {
						// Pick zone type by demand valve — build whichever
						// has the highest demand (R/C/I valves from city sim).
						auto* citySim = currentGame->getCitySimulation();
						int16_t rValve = citySim ? citySim->getResValve() : 0;
						int16_t cValve = citySim ? citySim->getComValve() : 0;
						int16_t iValve = citySim ? citySim->getIndValve() : 0;

						Uint32 zoneID = Structure_ZoneResidential;
						if (cValve > rValve && cValve >= iValve) {
							zoneID = Structure_ZoneCommercial;
						} else if (iValve > rValve && iValve > cValve) {
							zoneID = Structure_ZoneIndustrial;
						}

						// Residential zones require heavy factory economy (money > 500)
						if (zoneID == Structure_ZoneResidential && money <= 500)
							zoneID = NONE_ID;

						int resCount = itemCount[Structure_ZoneResidential];
						int comCount = itemCount[Structure_ZoneCommercial];
						int indCount = itemCount[Structure_ZoneIndustrial];

						if (zoneID != NONE_ID && pBuilder->isAvailableToBuild(zoneID)
							&& findPlaceLocation(zoneID).isValid()) {
							itemID = zoneID;
							logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d surplus=%d)",
								getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
								rValve, cValve, iValve, powerSurplus);
						}
					}
				}

			// Dedup: skip if another CY already ordered this unique structure
			// this tick. Zones and turrets are allowed in multiples.
			if (itemID != NONE_ID) {
				bool isMultiBuild = (itemID == Structure_ZoneResidential
					|| itemID == Structure_ZoneCommercial
					|| itemID == Structure_ZoneIndustrial
					|| itemID == Structure_RocketTurret
					|| itemID == Structure_GunTurret
					|| itemID == Structure_Wall
					|| itemID == Structure_Slab1);
				if (!isMultiBuild && orderedThisTick.count(itemID)) {
					logDebug("DEDUP: Skipping %s — already ordered by another CY this tick",
						getItemNameByID(itemID).c_str());
					itemID = NONE_ID;
				}
			}

			if (pBuilder->isAvailableToBuild(itemID) && findPlaceLocation(itemID).isValid() && itemID != NONE_ID) {
				// Pre-lay concrete only for specific structures that need max health:
				// - Heavy Factory (upgrades need full health)
				// - High Tech Factory (upgrades need full health)
				// - Windtraps (only for turret power buffer - need max power output)
				// - Rocket Turrets (need max health for defense)
				bool needsConcrete = getGameInitSettings().getGameOptions().concreteRequired
					&& (itemID == Structure_HeavyFactory
						|| itemID == Structure_HighTechFactory
						|| itemID == Structure_RocketTurret
						|| (itemID == Structure_WindTrap
							&& itemCount[Structure_RepairYard] > 0
							&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
							&& pBuilder->getCurrentUpgradeLevel() >= 2));

				if (needsConcrete) {
					Coord location = findPlaceLocation(itemID);
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

					// Queue concrete slabs for each tile, preferring Slab4 (2x2) when available
					for (int i = startI; abs(i - startI) < structureSize.x; i += incI) {
						for (int j = startJ; abs(j - startJ) < structureSize.y; j += incJ) {
							const Tile* pTile = getMap().getTile(i, j);

							// For structures >= 2x2, try to use Slab4 for the first 2x2 area
							if (structureSize.x > 1 && structureSize.y > 1
								&& pBuilder->isAvailableToBuild(Structure_Slab4)
								&& abs(i - location.x) < 2 && abs(j - location.y) < 2) {
								// Only queue Slab4 at the origin corner (covers 2x2)
								if (i == location.x && j == location.y && pTile->getType() != Terrain_Slab) {
									placeLocations.emplace_back(i, j);
									doProduceItem(pBuilder, Structure_Slab4);
									logDebug("CONCRETE: Queuing Slab4 at (%d,%d) for %s", i, j, getItemNameByID(itemID).c_str());
								}
							} else if (pTile->getType() != Terrain_Slab) {
								// Use Slab1 for remaining tiles or if Slab4 not available
								if (pBuilder->isAvailableToBuild(Structure_Slab1)) {
									placeLocations.emplace_back(i, j);
									doProduceItem(pBuilder, Structure_Slab1);
									logDebug("CONCRETE: Queuing Slab1 at (%d,%d) for %s", i, j, getItemNameByID(itemID).c_str());
								}
							}
						}
					}

					// Store building location and queue the building
					placeLocations.push_back(location);
				}

				orderedThisTick.insert(itemID);
				produceItemWithLogging(itemID);
				itemCount[itemID]++;
			}
			else if (itemID != NONE_ID && pBuilder->isAvailableToBuild(itemID) && !findPlaceLocation(itemID).isValid()) {
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
		else if (itemID == NONE_ID && !skipRemainingStructureLogic) {
			logDebug("No structure selected to build (money: %d, skipRemaining: %d)", money, skipRemainingStructureLogic);
		}
		
		// Proactive concrete building: Expand base perimeter when idle and have spare money
		if (money > 500 && pBuilder->getProductionQueueSize() < 1 && itemID == NONE_ID
			&& pBuilder->isAvailableToBuild(Structure_Slab1)) {
			Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
			if (slabLocation.isValid()) {
				doProduceItem(pBuilder, Structure_Slab1);
				logDebug("PROACTIVE: Building concrete slab to expand base (money: %d) at (%d,%d)", money, slabLocation.x, slabLocation.y);
			}
		}
		
						}
					}

				if (pBuilder->isWaitingToPlace()) {
					Uint32 itemToBePlaced = pBuilder->getCurrentProducedItem();
					logDebug("PRODUCTION: CY waiting to place itemID: %d, credits: %d, queued locations: %zu", itemToBePlaced, money, placeLocations.size());
					Coord location;
					
					// Check if we have a pre-stored location (from concrete pre-placement)
					if (!placeLocations.empty()) {
						location = placeLocations.front();
						Coord itemsize = getStructureSize(itemToBePlaced);
						
						// Verify the location is still valid
						if (getMap().okayToPlaceStructure(location.x, location.y, itemsize.x, itemsize.y, false, getHouse(), false, itemToBePlaced)) {
							placeLocations.pop_front();
							logDebug("PRODUCTION: Using pre-stored location (%d,%d) for itemID: %d", location.x, location.y, itemToBePlaced);
						} else if (itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4) {
							// Concrete placement failed (maybe already placed), cancel and move on
							doCancelItem(pConstYard, itemToBePlaced);
							placeLocations.pop_front();
							logDebug("PRODUCTION: Cancelled concrete at (%d,%d) - already placed or invalid", location.x, location.y);
							location = Coord::Invalid();
						} else {
							// Building location invalid, cancel
							doCancelItem(pConstYard, itemToBePlaced);
							placeLocations.pop_front();
							logDebug("PRODUCTION: Cancelled building at (%d,%d) - location became invalid", location.x, location.y);
							location = Coord::Invalid();
						}
					} else {
						// No pre-stored location, find one dynamically
					if (itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4) {
						// For concrete slabs, use specialized slab placement method
						location = findSlabPlaceLocation(itemToBePlaced);
					} else if (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret) {
						// For turrets, use city placement in city sim (near key buildings),
						// otherwise use default placement (enemy-facing perimeter)
						if (currentGame && currentGame->isCitySimEnabled()) {
							location = findCityTurretPlaceLocation(itemToBePlaced);
						} else {
							location = findTurretPlaceLocation(itemToBePlaced);
						}
					} else {
						// For other structures, use normal method that favors adjacency
						location = findPlaceLocation(itemToBePlaced);
						}
					}

						if (location.isValid()) {
							doPlaceStructure(pConstYard, location.x, location.y);
							logDebug("PRODUCTION: Placed structure itemID: %d at (%d,%d)", itemToBePlaced, location.x, location.y);
						}
						else {
							logDebug("PRODUCTION ERROR: Failed to find placement location for item %d, cancelling", itemToBePlaced);
							doCancelItem(pConstYard, itemToBePlaced);
						}
					}
				} break;
				}
			}
		}
	}

	// MULTIPLAYER FIX: Use deterministic timer instead of random
	buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
}


void QuantBot::scrambleUnitsAndDefend(const ObjectBase* pIntruder, int numUnits) {
	if (supportMode) {
		return;
	}
	for (const UnitBase* pUnit : getUnitList()) {
		if (pUnit->isRespondable() && (pUnit->getOwner() == getHouse())) {
			if (!pUnit->hasATarget() && !pUnit->wasForced()) {
				Uint32 itemID = pUnit->getItemID();
				if ((itemID != Unit_Harvester) && (itemID != Unit_MCV) && (itemID != Unit_Carryall)
					&& (itemID != Unit_Frigate) && (itemID != Unit_Saboteur) && (itemID != Unit_Sandworm)) {

					doSetAttackMode(pUnit, AREAGUARD);
					doAttackObject(pUnit, pIntruder, true);

					// Request carryall drop for ground units if far away (except Launchers/Deviators)
					if (getGameInitSettings().getGameOptions().manualCarryallDrops
						&& pUnit->isVisible()
						&& pUnit->isAGroundUnit()
						&& (itemID != Unit_Deviator)
						&& (itemID != Unit_Launcher)
						&& (blockDistance(pUnit->getLocation(), pUnit->getDestination()) >= 10)
						&& (pUnit->getHealth() / pUnit->getMaxHealth() > BADLYDAMAGEDRATIO)) {

						doRequestCarryallDrop(static_cast<const GroundUnit*>(pUnit));
					}

					if (--numUnits == 0) {
						break;
					}
				}
			}
		}
	}
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
            || !pUnit->isRespondable()) {
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
	if (supportMode) {
		attackTimer = std::numeric_limits<Sint32>::max();
		return;
	}

    // Get config for this difficulty
    const QuantBotConfig& config = getQuantBotConfig();
    const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    // MULTIPLAYER FIX: Reset attack timer with deterministic house-based variation
    const int houseID = static_cast<int>(getHouse()->getHouseID());
    const int attackVariation = (houseID - 3) * MILLI2CYCLES(15000);  // -45s to +30s variation
    attackTimer = MILLI2CYCLES(config.attackTimerMs) + attackVariation;

	// Check if this difficulty is allowed to attack at all
	if (!diffSettings.attackEnabled) {
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
	if (militaryValue < militaryValueLimit * attackThreshold) {
		logDebug("Don't attack. Not enough troops: house: %d  dif: %d  mStr: %d  mLim: %d (need %.1f%%)",
			getHouse()->getHouseID(), static_cast<Uint8>(difficulty), militaryValue, militaryValueLimit, attackThresholdPercent * 100.0f);
		return;
	}

	// Don't attack if you don't yet have a repair yard, if you are at least tech 5
	if (getHouse()->getNumItems(Structure_RepairYard) == 0 && currentGame->techLevel > 4) {
		logDebug("Don't attack. Wait until you have a repair yard.");
		return;
	}

	int attackSquadSize = 0; // how many units AI will send in attack squad
	
	// First count existing hunting units
	for (const UnitBase* pUnit : getUnitList()) {
		if (pUnit->isRespondable()
			&& (pUnit->getOwner() == getHouse())
			&& pUnit->isActive()
			&& !pUnit->isBadlyDamaged()
			&& pUnit->getAttackMode() == HUNT
			&& pUnit->getItemID() != Unit_Harvester
			&& pUnit->getItemID() != Unit_MCV
			&& pUnit->getItemID() != Unit_Carryall
			&& pUnit->getItemID() != Unit_Ornithopter
			&& pUnit->getItemID() != Unit_Sandworm) {
			attackSquadSize++;
		}
	}

	// Calculate attack force value limit based on military value and difficulty ratio
	int attackForceValueLimit = static_cast<int>(militaryValueLimit * diffSettings.attackForceMilitaryValueRatio);
	int attackForceValueUsed = 0;
	
	logDebug("=== ATTACK INITIATED: %s (%s) ===", 
		getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())).c_str(),
		difficulty == Difficulty::Easy ? "Easy" : 
		difficulty == Difficulty::Medium ? "Medium" : 
		difficulty == Difficulty::Hard ? "Hard" : 
		difficulty == Difficulty::Brutal ? "Brutal" : "Defend");
	logDebug("  Military: %d/%d  AttackForce: %d (%.0f%% limit)", 
		militaryValue, militaryValueLimit, attackForceValueLimit, 
		diffSettings.attackForceMilitaryValueRatio * 100);

	for (const UnitBase* pUnit : getUnitList()) {
		if (pUnit->isRespondable()
			&& (pUnit->getOwner() == getHouse())
			&& pUnit->isActive()
			&& !pUnit->isBadlyDamaged()
			&& !pUnit->wasForced()
			&& pUnit->getAttackMode() != RETREAT
			&& pUnit->getAttackMode() != HUNT  // Don't add units that are already hunting
			&& pUnit->getItemID() != Unit_Harvester
			&& pUnit->getItemID() != Unit_MCV
			&& pUnit->getItemID() != Unit_Carryall
			&& pUnit->getItemID() != Unit_Ornithopter
			&& pUnit->getItemID() != Unit_Sandworm)

		{	
			// Check if adding this unit would exceed the attack force value limit
			int unitValue = currentGame->objectData.data[pUnit->getItemID()][getHouse()->getHouseID()].price;
			if (attackForceValueUsed + unitValue > attackForceValueLimit) {
				logDebug("Attack force value limit reached: %d/%d (skipping unit ID %d worth %d)", 
					attackForceValueUsed, attackForceValueLimit, 
					pUnit->getItemID(), unitValue);
				break;  // Stop adding units to attack
			}
			
			// Send unit to attack and track its value
			doSetAttackMode(pUnit, HUNT);
			attackForceValueUsed += unitValue;
			attackSquadSize++;
		}
	}
	logDebug("  Sent %d units to HUNT (attack value: %d/%d)", 
		attackSquadSize, attackForceValueUsed, attackForceValueLimit);
	logDebug("=== END ATTACK ===");

}


Coord QuantBot::findSquadRallyLocation() {
	int buildingCount = 0;
	int totalX = 0;
	int totalY = 0;

	int enemyBuildingCount = 0;
	int enemyTotalX = 0;
	int enemyTotalY = 0;

	for (const StructureBase* pCurrentStructure : getStructureList()) {
		if (pCurrentStructure->getOwner()->getHouseID() == getHouse()->getHouseID()) {
			// Lets find the center of mass of our squad
			buildingCount++;
			totalX += pCurrentStructure->getX();
			totalY += pCurrentStructure->getY();
		}
		else if (pCurrentStructure->getOwner()->getTeamID() != getHouse()->getTeamID()) {
			enemyBuildingCount++;
			enemyTotalX += pCurrentStructure->getX();
			enemyTotalY += pCurrentStructure->getY();
		}
	}

	Coord baseCentreLocation = Coord::Invalid();
	if (enemyBuildingCount > 0 && buildingCount > 0) {
		baseCentreLocation.x = lround((totalX / buildingCount) * 0.75_fix + (enemyTotalX / enemyBuildingCount) * 0.25_fix);
		baseCentreLocation.y = lround((totalY / buildingCount) * 0.75_fix + (enemyTotalY / enemyBuildingCount) * 0.25_fix);
	}

	//logDebug("Squad rally location: %d, %d", baseCentreLocation.x , baseCentreLocation.y );

	return baseCentreLocation;
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
	
	if (unitDestination.isValid() && unitDestination != unitLocation) {
		// Unit is already moving - check if it's moving away from the threat
		Coord threatLocation = pThreat->getLocation();
		FixPoint distDestToThreat = blockDistance(unitDestination, threatLocation);
		FixPoint distCurrentToThreat = blockDistance(unitLocation, threatLocation);
		
		// If already moving away from threat, don't interrupt
		if (distDestToThreat >= distCurrentToThreat) {
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
}

/**
 * Move a unit to the optimal squad position.
 * Chooses between actual squad center and squad rally point based on which is closer.
 * Only moves if the unit is outside the radius of both positions.
 * 
 * @param pUnit The unit to potentially move
 * @param squadRadius The acceptable radius around either position (unit won't move if within this radius)
 */
void QuantBot::moveToOptimalSquadPosition(const UnitBase* pUnit, FixPoint squadRadius) {
	if (!pUnit || !pUnit->isRespondable()) {
		return;
	}

	// Calculate actual squad center (dynamic, based on unit positions)
	Coord actualSquadCenter = findSquadCenter(getHouse()->getHouseID());
	
	// Use established rally location (static, set by AI)
	Coord rallyPoint = squadRallyLocation;
	
	// If neither location is valid, do nothing
	if (!actualSquadCenter.isValid() && !rallyPoint.isValid()) {
		return;
	}
	
	Coord unitLocation = pUnit->getLocation();
	Coord unitDestination = pUnit->getDestination();
	
	// Calculate distances to both positions
	FixPoint distToSquadCenter = actualSquadCenter.isValid() ? 
		blockDistance(unitLocation, actualSquadCenter) : FixPt_MAX;
	FixPoint distToRallyPoint = rallyPoint.isValid() ? 
		blockDistance(unitLocation, rallyPoint) : FixPt_MAX;
	
	// Check if unit is already within acceptable radius of either position
	bool withinSquadRadius = (distToSquadCenter <= squadRadius);
	bool withinRallyRadius = (distToRallyPoint <= squadRadius);
	
	// If within radius of either, don't move
	if (withinSquadRadius || withinRallyRadius) {
		return;
	}
	
	// Check if unit is already heading to a location within the acceptable radius
	// This prevents repathing when the unit is already on its way
	if (unitDestination.isValid()) {
		FixPoint destToSquadCenter = actualSquadCenter.isValid() ? 
			blockDistance(unitDestination, actualSquadCenter) : FixPt_MAX;
		FixPoint destToRallyPoint = rallyPoint.isValid() ? 
			blockDistance(unitDestination, rallyPoint) : FixPt_MAX;
		
		if (destToSquadCenter <= squadRadius || destToRallyPoint <= squadRadius) {
			return;  // Already heading close enough, keep current path
		}
	}
	
	// CRITICAL: Don't add non-essential rally movements when pathfinding is overloaded
	// If queue is stressed (>300 paths), skip rally repositioning
	// Combat/retreat movements will still happen via other code paths
	if (currentGame != nullptr && currentGame->isPathQueueStressed()) {
		return;  // Queue overloaded, skip non-critical movement
	}
	
	// Unit is outside both radii - move to the closer one
	Coord targetPosition;
	if (distToSquadCenter < distToRallyPoint) {
		targetPosition = actualSquadCenter;
	} else {
		targetPosition = rallyPoint;
	}
	
	// Move to the closer position
	if (targetPosition.isValid()) {
		doMove2Pos(pUnit, targetPosition.x, targetPosition.y, false);
	}
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
        // Safety check: if our house is null (e.g., during game cleanup), don't check units
        if (getHouse() == nullptr) {
            return;
        }

        const QuantBotConfig& config = getQuantBotConfig();
        const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
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
            
            // Log saboteur state for debugging
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
                        //logDebug("MCV: forced: %d  moving: %d  canDeploy: %d",
                        //pMCV->wasForced(), pMCV->isMoving(), pMCV->canDeploy());

                        if (pMCV->canDeploy() && !pMCV->wasForced() && !pMCV->isMoving()) {
                            //logDebug("MCV: Deployed");
                            doDeploy(pMCV);
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
                            moveToOptimalSquadPosition(pUnit, 2);
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
                                moveToOptimalSquadPosition(pUnit, squadRadius);
                            }
                        }
                        else if (pUnit->getAttackMode() == RETREAT) {
                            if (!pUnit->wasForced()) {
                                if (pUnit->getHealth() < pUnit->getMaxHealth()) {
                                    doRepair(pUnit);
                                }
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius + 2);
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

void QuantBot::manageCityBuilding() {
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
                roadsPlaced++;
            }
        }
    }
}
