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

#include <players/MentatBuildStep.h>
#include <players/QuantBot.h>

#include <Game.h>
#include <GameInitSettings.h>
#include <Map.h>
#include <House.h>
#include <sand.h>

#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

#include <limits>
#include <algorithm>

const std::vector<MentatBuildStep>& QuantBot::getCYBuildOrder() {
    static const std::vector<MentatBuildStep> ORDER = {

    // ═══════════════════════════════════════════════════════════════════
    //  CRITICAL DEFENSE — Counter ornithopters ASAP
    // ═══════════════════════════════════════════════════════════════════

    {"COUNTER-ORNITHOPTER", anyMode(),
        // check: enemy has ornithopters AND we need more turrets
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (!currentGame) return false;
            int maxEnemyOrnis = 0;
            int totalEnemyOrnis = 0;
            for (int i = 0; i < NUM_HOUSES; i++) {
                const House* pH = currentGame->getHouse(i);
                if (pH && pH->getTeamID() != bot->getHouse()->getTeamID()) {
                    int n = pH->getNumItems(Unit_Ornithopter);
                    totalEnemyOrnis += n;
                    if (n > maxEnemyOrnis) maxEnemyOrnis = n;
                }
            }
            int requiredTurrets = std::max(maxEnemyOrnis * 2, totalEnemyOrnis);
            return maxEnemyOrnis > 0
                && ctx.itemCount[Structure_RocketTurret] < requiredTurrets;
        },
        // run: prep prerequisites (CY upgrade, windtrap, radar, power) then build turret
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            // Recompute enemy orni count (same as check)
            int maxEnemyOrnis = 0;
            if (currentGame) {
                for (int i = 0; i < NUM_HOUSES; i++) {
                    const House* pH = currentGame->getHouse(i);
                    if (pH && pH->getTeamID() != bot->getHouse()->getTeamID()) {
                        int n = pH->getNumItems(Unit_Ornithopter);
                        if (n > maxEnemyOrnis) maxEnemyOrnis = n;
                    }
                }
            }

            bool hasWindtrap = ctx.itemCount[Structure_WindTrap] > 0;
            bool hasRadar = ctx.itemCount[Structure_Radar] > 0;

            // Power buffer check for rocket turrets
            auto hasPowerBufferForTurret = [&]() {
                if (!bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower) return true;
                return (ctx.powerProduced - ctx.powerRequired) >= 225;
            };

            if (b->getCurrentUpgradeLevel() < 2) {
                if (b->getHealth() < b->getMaxHealth() && !b->isRepairing()) {
                    bot->doRepair(b);
                    bot->logDebug("COUNTER-ORNITHOPTER: Repairing CY before upgrade (level %d)", b->getCurrentUpgradeLevel());
                    return {NONE_ID, true};
                } else if (!b->isUpgrading() && b->getHealth() >= b->getMaxHealth()) {
                    bot->doUpgrade(b);
                    bot->logDebug("COUNTER-ORNITHOPTER: Upgrading CY (level %d -> %d)", b->getCurrentUpgradeLevel(), b->getCurrentUpgradeLevel() + 1);
                    return {NONE_ID, true};
                }
                return {NONE_ID, true};
            } else if (!hasWindtrap && b->isAvailableToBuild(Structure_WindTrap)) {
                bot->logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", maxEnemyOrnis);
                return {Structure_WindTrap, false};
            } else if (!hasRadar && b->isAvailableToBuild(Structure_Radar) && bot->getHouse()->hasPower()) {
                bot->logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", maxEnemyOrnis);
                return {Structure_Radar, false};
            } else if (!hasPowerBufferForTurret()) {
                int powerExcess = ctx.powerProduced - ctx.powerRequired;
                if (ctx.isCitySim && b->isAvailableToBuild(Structure_NuclearPlant)
                    && bot->findPlaceLocation(Structure_NuclearPlant).isValid()) {
                    bot->logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
                    return {Structure_NuclearPlant, false};
                } else if (b->isAvailableToBuild(Structure_WindTrap)
                    && bot->findPlaceLocation(Structure_WindTrap).isValid()) {
                    bot->logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
                    return {Structure_WindTrap, false};
                }
                return {NONE_ID, true};
            } else if (b->isAvailableToBuild(Structure_RocketTurret)
                && bot->findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()
                && hasPowerBufferForTurret()) {
                int totalEnemyOrnis2 = 0;
                for (int i = 0; i < NUM_HOUSES; i++) {
                    const House* pH = currentGame->getHouse(i);
                    if (pH && pH->getTeamID() != bot->getHouse()->getTeamID())
                        totalEnemyOrnis2 += pH->getNumItems(Unit_Ornithopter);
                }
                int requiredTurrets = std::max(maxEnemyOrnis * 2, totalEnemyOrnis2);
                bot->logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", maxEnemyOrnis, requiredTurrets);
                return {Structure_RocketTurret, false};
            }
            return {NONE_ID, true};
        }},

    // ═══════════════════════════════════════════════════════════════════
    //  ESSENTIAL INFRASTRUCTURE
    // ═══════════════════════════════════════════════════════════════════

    // 1. First windtrap
    {"WINDTRAP-0", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.itemCount[Structure_WindTrap] == 0
                && b->isAvailableToBuild(Structure_WindTrap);
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_WindTrap, false};
        }},

    // 1b. Power deficit recovery
    {"POWER-RECOVERY", anyMode(),
        [](QuantBot*, const BuilderBase*, const QuantBotBuildContext& ctx) {
            return ctx.powerProduced < ctx.powerRequired;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            int powerDeficit = ctx.powerRequired - ctx.powerProduced;
            if (ctx.isCitySim && b->isAvailableToBuild(Structure_NuclearPlant)
                && bot->findPlaceLocation(Structure_NuclearPlant).isValid()) {
                bot->logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
                return {Structure_NuclearPlant, false};
            } else if (b->isAvailableToBuild(Structure_WindTrap)
                && bot->findPlaceLocation(Structure_WindTrap).isValid()) {
                bot->logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
                return {Structure_WindTrap, false};
            }
            return {NONE_ID, false};
        }},

    // 1c. City-mode proportional power buffer
    {"CITY-POWER-BUFFER", cityOnly(),
        [](QuantBot*, const BuilderBase*, const QuantBotBuildContext& ctx) {
            if (!currentGame) return false;
            const int buffer = ctx.powerProduced - ctx.powerRequired;
            const int targetBuffer = ctx.powerRequired / 10;
            return ctx.powerRequired > 0 && buffer < targetBuffer;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            const int buffer = ctx.powerProduced - ctx.powerRequired;
            const int targetBuffer = ctx.powerRequired / 10;
            if (b->isAvailableToBuild(Structure_NuclearPlant)
                && bot->findPlaceLocation(Structure_NuclearPlant).isValid()) {
                bot->logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
                    buffer, targetBuffer, ctx.powerRequired);
                return {Structure_NuclearPlant, false};
            } else if (b->isAvailableToBuild(Structure_WindTrap)
                && bot->findPlaceLocation(Structure_WindTrap).isValid()) {
                bot->logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d)",
                    buffer, targetBuffer);
                return {Structure_WindTrap, false};
            }
            return {NONE_ID, false};
        }},

    // ═══════════════════════════════════════════════════════════════════
    //  CITY-SIM ECONOMIC BACKBONE (step 2-CITY)
    // ═══════════════════════════════════════════════════════════════════

    {"CITY-ECON", cityOnly(),
        [](QuantBot*, const BuilderBase*, const QuantBotBuildContext&) {
            return true; // Always check; run() decides
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            int resCount = ctx.itemCount[Structure_ZoneResidential];
            int comCount = ctx.itemCount[Structure_ZoneCommercial];
            int indCount = ctx.itemCount[Structure_ZoneIndustrial];
            int zoneCount = resCount + comCount + indCount;
            int refCount = ctx.itemCount[Structure_Refinery];

            constexpr int kCityBootstrapZoneSeed = 6;
            constexpr int kCityRefineryCap = 4;
            constexpr int kZonesPerRefinery = 3;

            const bool lowSpiceEconomy = (bot->lastCalculatedSpice < 500);

            const bool firstRefineryNeeded = refCount == 0
                && b->isAvailableToBuild(Structure_Refinery)
                && bot->findPlaceLocation(Structure_Refinery).isValid();

            const bool refineryDue = !lowSpiceEconomy
                && refCount < kCityRefineryCap
                && zoneCount >= refCount * kZonesPerRefinery
                && b->isAvailableToBuild(Structure_Refinery)
                && bot->findPlaceLocation(Structure_Refinery).isValid();

            if (firstRefineryNeeded || refineryDue) {
                if (ctx.itemCount[Unit_Harvester] < ctx.harvesterLimit) {
                    ctx.itemCount[Unit_Harvester]++;
                }
                bot->logDebug("CITY-ECON: Building Refinery (zones=%d ref=%d, %s)",
                    zoneCount, refCount,
                    firstRefineryNeeded ? "tech prerequisite" : "alternation");
                return {Structure_Refinery, false};
            } else if (zoneCount < kCityBootstrapZoneSeed) {
                Uint32 zoneID = NONE_ID;
                if (resCount == 0) {
                    zoneID = Structure_ZoneResidential;
                } else if (indCount == 0) {
                    zoneID = Structure_ZoneIndustrial;
                } else if (comCount == 0) {
                    zoneID = Structure_ZoneCommercial;
                } else {
                    const int expR = std::max(comCount, indCount) * 3 + 3;
                    const int expI = std::max(resCount / 3, 1);
                    const int expC = std::max(resCount / 3, 1);
                    const int rGap = expR - resCount;
                    const int iGap = expI - indCount;
                    const int cGap = expC - comCount;
                    int bestGap = std::numeric_limits<int>::min();
                    if (ctx.ownResValve > 0 && rGap > bestGap) {
                        bestGap = rGap; zoneID = Structure_ZoneResidential;
                    }
                    if (ctx.ownIndValve > 0 && iGap > bestGap) {
                        bestGap = iGap; zoneID = Structure_ZoneIndustrial;
                    }
                    if (ctx.ownComValve > 0 && cGap > bestGap) {
                        bestGap = cGap; zoneID = Structure_ZoneCommercial;
                    }
                    if (zoneID == NONE_ID) zoneID = Structure_ZoneResidential;
                }

                if (zoneID != NONE_ID
                    && ctx.money > 200
                    && ctx.itemCount[Structure_WindTrap] > 0
                    && b->isAvailableToBuild(zoneID)
                    && bot->findPlaceLocation(zoneID).isValid()) {
                    bot->logDebug("CITY-ECON: Building %s (R:%d C:%d I:%d zoneCount=%d valves=R%+d C%+d I%+d)",
                        getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
                        zoneCount, ctx.ownResValve, ctx.ownComValve, ctx.ownIndValve);
                    return {zoneID, false};
                }
            }
            return {NONE_ID, false};
        }},

    // ═══════════════════════════════════════════════════════════════════
    //  VANILLA ECONOMY
    // ═══════════════════════════════════════════════════════════════════

    // 2. Refinery (if 0) — vanilla only
    {"REFINERY-0", vanillaOnly(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.itemCount[Structure_Refinery] == 0
                && b->isAvailableToBuild(Structure_Refinery);
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            if (ctx.itemCount[Unit_Harvester] < ctx.harvesterLimit) {
                ctx.itemCount[Unit_Harvester]++;
            }
            return {Structure_Refinery, false};
        }},

    // 3. Refinery ratio (1 per 3 harvesters) — vanilla only
    {"REFINERY-RATIO", vanillaOnly(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (bot->lastCalculatedSpice < 500) return false;
            if (ctx.itemCount[Structure_Refinery] >= ctx.itemCount[Unit_Harvester] / 3) return false;
            if (!b->isAvailableToBuild(Structure_Refinery)) return false;
            if (ctx.isCampaign && ctx.itemCount[Structure_Refinery] >= 2
                && ctx.itemCount[Structure_RepairYard] == 0
                && currentGame && currentGame->techLevel >= 5) return false;
            return true;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            if (ctx.itemCount[Unit_Harvester] < ctx.harvesterLimit) {
                ctx.itemCount[Unit_Harvester]++;
            }
            return {Structure_Refinery, false};
        }},

    // 4. Refinery (< 4, money < 2000) — vanilla, custom only
    {"REFINERY-EARLY", vanillaCustomOnly(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (bot->lastCalculatedSpice < 500) return false;
            return ctx.itemCount[Structure_Refinery] < 4
                && b->isAvailableToBuild(Structure_Refinery)
                && ctx.money < 2000;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            if (ctx.itemCount[Unit_Harvester] < ctx.harvesterLimit) {
                ctx.itemCount[Unit_Harvester]++;
            }
            return {Structure_Refinery, false};
        }},

    // ═══════════════════════════════════════════════════════════════════
    //  MILITARY INFRASTRUCTURE
    // ═══════════════════════════════════════════════════════════════════

    // 5. StarPort
    {"STARPORT", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            // City income gate
            constexpr int kCityIncomeReadyZones = 3;
            const int zoneCount = ctx.itemCount[Structure_ZoneResidential]
                + ctx.itemCount[Structure_ZoneCommercial]
                + ctx.itemCount[Structure_ZoneIndustrial];
            if (ctx.isCitySim && zoneCount < kCityIncomeReadyZones) return false;

            if (ctx.itemCount[Structure_StarPort] != 0) return false;
            if (!b->isAvailableToBuild(Structure_StarPort)) return false;
            if (!bot->findPlaceLocation(Structure_StarPort).isValid()) return false;
            if (ctx.isCampaign && ctx.money <= 1000) return false;

            if (!currentGame) return false;
            const auto& objData = currentGame->objectData.data;
            int houseID = ctx.houseID;
            bool hasUsefulStarportUnits =
                (objData[Unit_Tank][houseID].enabled && bot->getHouse()->getChoam().getNumAvailable(Unit_Tank) > 0) ||
                (objData[Unit_SiegeTank][houseID].enabled && bot->getHouse()->getChoam().getNumAvailable(Unit_SiegeTank) > 0) ||
                (objData[Unit_Launcher][houseID].enabled && bot->getHouse()->getChoam().getNumAvailable(Unit_Launcher) > 0) ||
                (objData[Unit_Harvester][houseID].enabled && bot->getHouse()->getChoam().getNumAvailable(Unit_Harvester) > 0) ||
                (objData[Unit_Carryall][houseID].enabled && bot->getHouse()->getChoam().getNumAvailable(Unit_Carryall) > 0);
            return ctx.itemCount[Structure_HeavyFactory] > 0 || hasUsefulStarportUnits;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_StarPort, false};
        }},

    // 6. Radar
    {"RADAR", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            constexpr int kCityIncomeReadyZones = 3;
            const int zoneCount = ctx.itemCount[Structure_ZoneResidential]
                + ctx.itemCount[Structure_ZoneCommercial]
                + ctx.itemCount[Structure_ZoneIndustrial];
            if (ctx.isCitySim && zoneCount < kCityIncomeReadyZones) return false;
            return ctx.itemCount[Structure_Radar] == 0
                && b->isAvailableToBuild(Structure_Radar)
                && ctx.money > 500;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_Radar, false};
        }},

    // 7. Light Factory
    {"LIGHT-FACTORY", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            constexpr int kCityIncomeReadyZones = 3;
            const int zoneCount = ctx.itemCount[Structure_ZoneResidential]
                + ctx.itemCount[Structure_ZoneCommercial]
                + ctx.itemCount[Structure_ZoneIndustrial];
            if (ctx.isCitySim && zoneCount < kCityIncomeReadyZones) return false;
            return ctx.itemCount[Structure_LightFactory] == 0
                && b->isAvailableToBuild(Structure_LightFactory)
                && ctx.money > 500;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_LightFactory, false};
        }},

    // 8. Repair Yard
    {"REPAIR-YARD", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.itemCount[Structure_RepairYard] == 0
                && (ctx.itemCount[Structure_StarPort] > 0 || ctx.itemCount[Structure_HeavyFactory] > 0)
                && b->isAvailableToBuild(Structure_RepairYard);
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("Build Repair Yard... money: %d", ctx.money);
            return {Structure_RepairYard, false};
        }},

    // 8a. Upgrade CY to level 2 for rocket turrets
    {"TURRET-PREP-UPGRADE", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (b->getCurrentUpgradeLevel() >= 2) return false;
            if (b->isUpgrading()) return false;
            if (currentGame && ctx.isCitySim && ctx.money > 500) return true;
            return ctx.itemCount[Structure_RepairYard] > 0
                && (ctx.itemCount[Structure_StarPort] > 0 || ctx.itemCount[Structure_HeavyFactory] > 0)
                && ctx.itemCount[Structure_RocketTurret] < 2;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            if (b->getHealth() < b->getMaxHealth() && !b->isRepairing()) {
                bot->doRepair(b);
                bot->logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", b->getCurrentUpgradeLevel());
                return {NONE_ID, true};
            } else if (b->getHealth() >= b->getMaxHealth()) {
                bot->doUpgrade(b);
                bot->logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", b->getCurrentUpgradeLevel() + 1);
                return {NONE_ID, true};
            }
            return {NONE_ID, true};
        }},

    // 8b-pre-repair. Repair damaged windtraps before building turrets
    {"TURRET-WINDTRAP-REPAIR", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (!bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower) return false;
            if (ctx.itemCount[Structure_RepairYard] == 0) return false;
            if (ctx.itemCount[Structure_StarPort] == 0 && ctx.itemCount[Structure_HeavyFactory] == 0) return false;
            if (b->getCurrentUpgradeLevel() < 2) return false;
            if (!b->isAvailableToBuild(Structure_RocketTurret)) return false;
            // Check if we lack power buffer
            return (ctx.powerProduced - ctx.powerRequired) < 225;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            for (const StructureBase* pStruct : bot->getStructureList()) {
                if (pStruct->getOwner() == bot->getHouse()
                    && pStruct->getItemID() == Structure_WindTrap
                    && pStruct->getHealth() < pStruct->getMaxHealth()
                    && !pStruct->isRepairing()) {
                    bot->doRepair(pStruct);
                    bot->logDebug("TURRET-POWER: Repairing damaged windtrap for max power generation");
                    // Note: original code did not set handled=true or pick an item here,
                    // it just called repairDamagedWindtraps() and fell through to 8b-pre.
                    // We replicate that by returning NONE_ID, false to continue.
                    break;
                }
            }
            return {NONE_ID, false};
        }},

    // 8b-pre. Build power for turret buffer
    {"TURRET-POWER-BUFFER", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (!bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower) return false;
            if (ctx.itemCount[Structure_RepairYard] == 0) return false;
            if (ctx.itemCount[Structure_StarPort] == 0 && ctx.itemCount[Structure_HeavyFactory] == 0) return false;
            if (b->getCurrentUpgradeLevel() < 2) return false;
            if (!b->isAvailableToBuild(Structure_RocketTurret)) return false;
            return (ctx.powerProduced - ctx.powerRequired) < 225;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            int powerExcess = ctx.powerProduced - ctx.powerRequired;
            if (ctx.isCitySim && b->isAvailableToBuild(Structure_NuclearPlant)
                && bot->findPlaceLocation(Structure_NuclearPlant).isValid()) {
                bot->logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: 225)", powerExcess);
                return {Structure_NuclearPlant, false};
            } else if (b->isAvailableToBuild(Structure_WindTrap)
                && bot->findPlaceLocation(Structure_WindTrap).isValid()) {
                bot->logDebug("TURRET-POWER: Windtrap for turret buffer (excess: %d, need: 225)", powerExcess);
                return {Structure_WindTrap, false};
            }
            return {NONE_ID, false};
        }},

    // 8b. Two baseline rocket turrets
    {"BASELINE-TURRETS", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (ctx.itemCount[Structure_RepairYard] == 0) return false;
            if (ctx.itemCount[Structure_RocketTurret] >= 2) return false;
            if (ctx.itemCount[Structure_StarPort] == 0 && ctx.itemCount[Structure_HeavyFactory] == 0) return false;
            // Power buffer check
            if (bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower
                && (ctx.powerProduced - ctx.powerRequired) < 225) return false;
            if (b->getCurrentUpgradeLevel() < 2) return false;
            if (!b->isAvailableToBuild(Structure_RocketTurret)) return false;
            return bot->findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid();
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", ctx.itemCount[Structure_RocketTurret] + 1);
            return {Structure_RocketTurret, false};
        }},

    // 8c. Counter enemy ornithopters (post-infrastructure)
    {"COUNTER-ORNI-TURRETS", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (ctx.itemCount[Structure_RepairYard] == 0) return false;
            if (ctx.itemCount[Structure_StarPort] == 0 && ctx.itemCount[Structure_HeavyFactory] == 0) return false;
            if (bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower
                && (ctx.powerProduced - ctx.powerRequired) < 225) return false;
            if (b->getCurrentUpgradeLevel() < 2) return false;
            if (!b->isAvailableToBuild(Structure_RocketTurret)) return false;
            if (!bot->findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) return false;

            int maxEnemyOrnis = 0;
            if (currentGame) {
                for (int i = 0; i < NUM_HOUSES; i++) {
                    const House* pH = currentGame->getHouse(i);
                    if (pH && pH->getTeamID() != bot->getHouse()->getTeamID()) {
                        int n = pH->getNumItems(Unit_Ornithopter);
                        if (n > maxEnemyOrnis) maxEnemyOrnis = n;
                    }
                }
            }
            return maxEnemyOrnis > 0 && ctx.itemCount[Structure_RocketTurret] < maxEnemyOrnis * 2;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            bot->logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
            return {Structure_RocketTurret, false};
        }},

    // 9. Heavy Factory
    {"HEAVY-FACTORY-0", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            constexpr int kCityIncomeReadyZones = 3;
            const int zoneCount = ctx.itemCount[Structure_ZoneResidential]
                + ctx.itemCount[Structure_ZoneCommercial]
                + ctx.itemCount[Structure_ZoneIndustrial];
            if (ctx.isCitySim && zoneCount < kCityIncomeReadyZones) return false;
            return ctx.itemCount[Structure_HeavyFactory] == 0
                && b->isAvailableToBuild(Structure_HeavyFactory)
                && ctx.money > 500;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("Build first Heavy Factory... money: %d", ctx.money);
            return {Structure_HeavyFactory, false};
        }},

    // 10. High Tech Factory
    {"HTF-0", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            constexpr int kCityIncomeReadyZones = 3;
            const int zoneCount = ctx.itemCount[Structure_ZoneResidential]
                + ctx.itemCount[Structure_ZoneCommercial]
                + ctx.itemCount[Structure_ZoneIndustrial];
            if (ctx.isCitySim && zoneCount < kCityIncomeReadyZones) return false;
            return ctx.itemCount[Structure_HighTechFactory] == 0
                && ctx.itemCount[Structure_HeavyFactory] > 0
                && b->isAvailableToBuild(Structure_HighTechFactory)
                && ctx.money > 1000;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("Build first High Tech Factory... money: %d", ctx.money);
            return {Structure_HighTechFactory, false};
        }},

    // 11. House IX
    {"IX", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.itemCount[Structure_IX] == 0
                && ctx.itemCount[Structure_HeavyFactory] > 0
                && ctx.itemCount[Structure_HighTechFactory] > 0
                && ctx.itemCount[Structure_RepairYard] > 0
                && b->isAvailableToBuild(Structure_IX)
                && ctx.money > 1000;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("Build IX... money: %d", ctx.money);
            return {Structure_IX, false};
        }},

    // 12. Additional Heavy Factories
    {"HEAVY-FACTORY-EXTRA", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.money > 2000 && b->isAvailableToBuild(Structure_HeavyFactory);
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            int desiredHFs = 1;
            if (ctx.isCitySim) {
                auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
                int tax = citySim ? citySim->getCityTax() : 7;
                int32_t annual = DuneCity::computeAnnualTaxRevenue(ctx.ownTotalPop, tax, ctx.ownAvgLandValue);
                int creditsPerSec = annual / 60;
                desiredHFs = 1 + creditsPerSec / 50;
            } else {
                desiredHFs = 1 + ctx.money / 4000;
            }

            const bool needMore = (ctx.activeHeavyFactoryCount >= ctx.itemCount[Structure_HeavyFactory])
                || (ctx.itemCount[Structure_HeavyFactory] < desiredHFs);

            if (!needMore) return {NONE_ID, false};

            int techLevel = currentGame ? currentGame->techLevel : 8;
            bool prerequisitesMet = false;
            if (techLevel <= 4) {
                prerequisitesMet = true;
            } else if (techLevel <= 6) {
                prerequisitesMet = (ctx.itemCount[Structure_RepairYard] >= 1);
            } else {
                prerequisitesMet = (ctx.itemCount[Structure_RepairYard] >= 1 && ctx.itemCount[Structure_IX] >= 1);
            }

            if (prerequisitesMet) {
                bot->logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  desired: %d  tech: %d",
                    ctx.activeHeavyFactoryCount, bot->getHouse()->getNumItems(Structure_HeavyFactory), ctx.money, desiredHFs, techLevel);
                return {Structure_HeavyFactory, false};
            }
            return {NONE_ID, false};
        }},

    // 13. Refineries for harvester ratio
    {"REFINERY-RATIO-LATE", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (bot->lastCalculatedSpice < 500) return false;
            if (!((ctx.itemCount[Structure_Refinery] * 3.5_fix < ctx.itemCount[Unit_Harvester])
                || (currentGame && currentGame->techLevel < 4))) return false;
            if (!b->isAvailableToBuild(Structure_Refinery)) return false;
            if (ctx.isCampaign && ctx.itemCount[Structure_Refinery] >= 2
                && ctx.itemCount[Structure_RepairYard] == 0
                && currentGame && currentGame->techLevel >= 5) return false;
            return true;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            if (ctx.itemCount[Unit_Harvester] < ctx.harvesterLimit) {
                ctx.itemCount[Unit_Harvester]++;
            }
            return {Structure_Refinery, false};
        }},

    // 14. Additional Repair Yards
    {"REPAIR-YARD-EXTRA", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return b->isAvailableToBuild(Structure_RepairYard) && ctx.money > 2000
                && ctx.itemCount[Structure_RepairYard] * 6000 < ctx.militaryValue;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            bot->logDebug("Build Repair Yard: have %d, need %d (military: %d)", ctx.itemCount[Structure_RepairYard], (ctx.militaryValue / 6000) + 1, ctx.militaryValue);
            return {Structure_RepairYard, false};
        }},

    // 15. Additional High Tech Factories
    {"HTF-EXTRA", anyMode(),
        [](QuantBot*, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.money > 3000 && b->isAvailableToBuild(Structure_HighTechFactory)
                && ctx.itemCount[Structure_HighTechFactory] > 0
                && ctx.activeHighTechFactoryCount >= ctx.itemCount[Structure_HighTechFactory];
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_HighTechFactory, false};
        }},

    // 16. Silos
    {"SILOS", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            return ctx.itemCount[Structure_HeavyFactory] > 0
                && bot->getHouse()->getStoredCredits() > bot->getHouse()->getCapacity() * 0.80_fix
                && b->isAvailableToBuild(Structure_Silo);
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            bot->logDebug("Build Silo - storage at %d/%d", bot->getHouse()->getStoredCredits().lround(), bot->getHouse()->getCapacity());
            return {Structure_Silo, false};
        }},

    // 17. City protection turrets
    {"CITY-TURRET", cityOnly(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (!currentGame) return false;
            if (ctx.money <= 500) return false;
            // Power buffer check
            if (bot->getGameInitSettings().getGameOptions().rocketTurretsNeedPower
                && (ctx.powerProduced - ctx.powerRequired) < 225) return false;
            if (!b->isAvailableToBuild(Structure_RocketTurret)) return false;

            int maxOwnCrime = 0;
            if (auto* citySim = currentGame->getCitySimulation()) {
                const auto& crimeMap = citySim->getCrimeRateMap();
                for (const StructureBase* pStruct : bot->getStructureList()) {
                    if (!pStruct || pStruct->getOwner() != bot->getHouse())
                        continue;
                    Coord pos = pStruct->getLocation();
                    Coord sz  = pStruct->getStructureSize();
                    for (int dy = 0; dy < sz.y; dy++) {
                        for (int dx = 0; dx < sz.x; dx++) {
                            int c = crimeMap.worldGet(pos.x + dx, pos.y + dy);
                            if (c > maxOwnCrime) maxOwnCrime = c;
                        }
                    }
                }
            }
            return maxOwnCrime > 2;
        },
        [](QuantBot* bot, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            Coord loc = bot->findCityTurretPlaceLocation(Structure_RocketTurret);
            if (loc.isValid()) {
                // We can't pass the location through, but findPlaceLocation in the
                // finalisation block will re-find it. The check confirmed it exists.
                bot->logDebug("CITY-TURRET: Building rocket turret for crime suppression");
                return {Structure_RocketTurret, false};
            }
            return {NONE_ID, false};
        }},

    // 17b. Palace
    {"PALACE", anyMode(),
        [](QuantBot* bot, const BuilderBase* b, const QuantBotBuildContext& ctx) {
            if (ctx.money <= 5000) return false;
            if (!b->isAvailableToBuild(Structure_Palace)) return false;
            if (ctx.itemCount[Structure_HeavyFactory] == 0) return false;
            if (ctx.itemCount[Structure_LightFactory] == 0) return false;

            bool palaceAllowed;
            if (currentGame && ctx.isCitySim) {
                palaceAllowed = (ctx.itemCount[Structure_Palace] < 1 + ctx.ownTotalPop / 25);
            } else {
                palaceAllowed = (ctx.itemCount[Structure_Palace] == 0
                    || !bot->getGameInitSettings().getGameOptions().onlyOnePalace);
            }
            return palaceAllowed;
        },
        [](QuantBot*, const BuilderBase*, QuantBotBuildContext&) -> std::pair<Uint32, bool> {
            return {Structure_Palace, false};
        }},

    // ═══════════════════════════════════════════════════════════════════
    //  CITY ZONES & CIVIC BUILDINGS
    // ═══════════════════════════════════════════════════════════════════

    // 18b. Civic buildings: Stadium and Airport
    {"CITY-CIVIC", cityOnly(),
        [](QuantBot*, const BuilderBase*, const QuantBotBuildContext& ctx) {
            if (!currentGame) return false;
            return ctx.money > 500;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            if (!ctx.ownHasStadium
                && ctx.ownResPop > 500
                && b->isAvailableToBuild(Structure_Stadium)
                && bot->findPlaceLocation(Structure_Stadium).isValid()) {
                bot->logDebug("CITY-CIVIC: Building Stadium (ownResPop=%d > 500, no stadium)", ctx.ownResPop);
                return {Structure_Stadium, false};
            }
            else if (!ctx.ownHasAirport
                && ctx.ownComPop > 20
                && b->isAvailableToBuild(Structure_Airport)
                && bot->findPlaceLocation(Structure_Airport).isValid()) {
                bot->logDebug("CITY-CIVIC: Building Airport (ownComPop=%d > 20, no airport)", ctx.ownComPop);
                return {Structure_Airport, false};
            }
            return {NONE_ID, false};
        }},

    // 18. City zone expansion (power headroom + demand valves)
    {"CITY-ZONE", cityOnly(),
        [](QuantBot*, const BuilderBase*, const QuantBotBuildContext& ctx) {
            if (!currentGame) return false;
            return ctx.money > 200 && ctx.itemCount[Structure_WindTrap] > 0;
        },
        [](QuantBot* bot, const BuilderBase* b, QuantBotBuildContext& ctx) -> std::pair<Uint32, bool> {
            constexpr int kZonePowerHeadroom = 24;
            const int powerSurplus = ctx.powerProduced - ctx.powerRequired;
            if (powerSurplus < kZonePowerHeadroom) {
                if (b->isAvailableToBuild(Structure_NuclearPlant)
                    && bot->findPlaceLocation(Structure_NuclearPlant).isValid()) {
                    bot->logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
                        powerSurplus, kZonePowerHeadroom);
                    return {Structure_NuclearPlant, false};
                } else if (b->isAvailableToBuild(Structure_WindTrap)
                    && bot->findPlaceLocation(Structure_WindTrap).isValid()) {
                    bot->logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
                        powerSurplus, kZonePowerHeadroom);
                    return {Structure_WindTrap, false};
                }
                return {NONE_ID, false};
            }

            const int resCount = ctx.itemCount[Structure_ZoneResidential];
            const int comCount = ctx.itemCount[Structure_ZoneCommercial];
            const int indCount = ctx.itemCount[Structure_ZoneIndustrial];
            const int expR = std::max(comCount, indCount) * 3 + 3;
            const int expI = std::max(resCount / 3, 1);
            const int expC = std::max(resCount / 3, 1);
            const int rGap = expR - resCount;
            const int iGap = expI - indCount;
            const int cGap = expC - comCount;

            Uint32 zoneID = NONE_ID;
            int bestGap = std::numeric_limits<int>::min();
            if (ctx.ownResValve > 0 && rGap > bestGap) {
                bestGap = rGap; zoneID = Structure_ZoneResidential;
            }
            if (ctx.ownIndValve > 0 && iGap > bestGap) {
                bestGap = iGap; zoneID = Structure_ZoneIndustrial;
            }
            if (ctx.ownComValve > 0 && cGap > bestGap) {
                bestGap = cGap; zoneID = Structure_ZoneCommercial;
            }

            if (zoneID != NONE_ID && b->isAvailableToBuild(zoneID)
                && bot->findPlaceLocation(zoneID).isValid()) {
                bot->logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d gap=%d valves=R%+d C%+d I%+d surplus=%d)",
                    getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
                    bestGap, ctx.ownResValve, ctx.ownComValve, ctx.ownIndValve, powerSurplus);
                return {zoneID, false};
            }
            return {NONE_ID, false};
        }},

    }; // end ORDER

    return ORDER;
}
