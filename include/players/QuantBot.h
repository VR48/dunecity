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

#ifndef QuantBot_H
#define QuantBot_H

#include <players/Player.h>
#include <units/MCV.h>
#include <players/QuantBotConfig.h>
#include <players/QuantBotBuildContext.h>

#include <DataTypes.h>
#include <set>
#include <map>
#include <unordered_map>
#include <vector>

struct MentatBuildStep;

class QuantBot : public Player
{
    friend struct MentatBuildStep;  // Build order lambdas need access to private members

public:
    enum class Difficulty {
        Easy = 0,
        Medium = 1,
        Hard = 2,
        Brutal = 3,
        Defend = 4
    };

    enum class GameMode {
        Custom = 4,
        Campaign = 5
    };

    QuantBot(House* associatedHouse, const std::string& playername, Difficulty difficulty, bool supportModeEnabled = false);
    QuantBot(InputStream& stream, House* associatedHouse);
    void init();
    ~QuantBot();
    void save(OutputStream& stream) const override;

    void update() override;

    void onObjectWasBuilt(const ObjectBase* pObject) override;
    void onDecrementStructures(int itemID, const Coord& location) override;
    void onDecrementUnits(int itemID) override;
    void onIncrementUnitKills(int itemID) override;
    void onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) override;

private:

    struct OrnithopterStrikeTeam {
        int minMembers = 0;
        Uint32 targetId = 0;
        std::set<Uint32> memberIds;

        bool isActive() const {
            return targetId != 0 && !memberIds.empty();
        }

        void reset() {
            minMembers = 0;
            targetId = 0;
            memberIds.clear();
        }

        void setTarget(Uint32 newTargetId, int requiredMembers) {
            targetId = newTargetId;
            minMembers = requiredMembers;
        }
    };

    Difficulty difficulty;  ///< difficulty level
    GameMode  gameMode;     ///< game mode (custom or campaign)
    Sint32  buildTimer;     ///< When to build the next structure/unit
    Sint32  attackTimer;    ///< When to build the next structure/unit
    Sint32  retreatTimer;   ///< When you last retreated>

    int initialItemCount[Num_ItemID];
    int initialMilitaryValue = 0;
    int militaryValueLimit = 0;
    int harvesterLimit = 4;
    int lastCalculatedSpice = 0;
    bool campaignAIAttackFlag = false;
    Coord squadRallyLocation = Coord::Invalid();
    Coord squadRetreatLocation = Coord::Invalid();
    bool supportMode = false;
    Uint32 lastStatsLogCycle = 0;
    
    std::map<Uint32, int> idleHarvesterCounters; ///< Track idle time for each harvester (objectID -> cycle count)
    std::map<Uint32, int> harvesterMovingCounters; ///< Track continuous movement time (objectID -> cycle count)

    void scrambleUnitsAndDefend(const ObjectBase* pIntruder, int numUnits = std::numeric_limits<int>::max());


    Coord findMcvPlaceLocation(const MCV* pMCV);
    Coord findPlaceLocation(Uint32 itemID);
    Coord findPlaceLocationSimple(Uint32 itemID);
    Coord findSlabPlaceLocation(Uint32 itemID);
    Coord findTurretPlaceLocation(Uint32 itemID);
    Coord findCityTurretPlaceLocation(Uint32 itemID);
    Coord findEffectiveTurretPlaceLocation(Uint32 itemID);
    Coord findSquadCenter(int houseID);
    Coord findBaseCentre(int houseID);
    Coord findBestDeathHandTarget(int enemyHouseID);
    double getProductionBuildingMultiplier(int itemID) const;
    Coord findSquadRallyLocation();
    Coord findSquadRetreatLocation();
    void moveToOptimalSquadPosition(const UnitBase* pUnit, FixPoint squadRadius);
    void kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange);

    bool tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                    const QuantBotConfig& config);

    std::list<Coord> placeLocations;    ///< Where to place structures
    OrnithopterStrikeTeam ornithopterStrikeTeam;
    std::unordered_map<Uint32, Coord> placementCache; ///< Per-build-cycle cache for findPlaceLocation results

    void checkAllUnits();
    void retreatAllUnits();
    void build(int militaryValue);
    void attack(int militaryValue);
    void manageCityBuilding();

    // --- Mentat refactor: CY build order vector ---
    static const std::vector<MentatBuildStep>& getCYBuildOrder();

    // --- Mentat refactor: per-builder production handlers ---
    void handleCYProduction(const BuilderBase* pBuilder, const StructureBase* pStructure, QuantBotBuildContext& ctx, bool emitStatsLog);
    void handleHeavyFactory(const BuilderBase* pBuilder, QuantBotBuildContext& ctx, bool emitStatsLog,
                            FixPoint tankPercent, FixPoint siegePercent, FixPoint specialPercent,
                            FixPoint launcherPercent, FixPoint ornithopterPercent);
    void handleHighTechFactory(const BuilderBase* pBuilder, QuantBotBuildContext& ctx,
                               FixPoint ornithopterPercent);
    void handleLightFactory(const BuilderBase* pBuilder, QuantBotBuildContext& ctx);
    void handleStarPort(const BuilderBase* pBuilder, QuantBotBuildContext& ctx);
    void handleRepairs(const StructureBase* pStructure, QuantBotBuildContext& ctx);
    void handleSpecialWeapon(const StructureBase* pStructure, QuantBotBuildContext& ctx);
    void handleStructurePlacement(const ConstructionYard* pConstYard, const BuilderBase* pBuilder, QuantBotBuildContext& ctx);

    // Unit ratio calculation (extracted from build())
    void calculateUnitRatios(QuantBotBuildContext& ctx,
                             FixPoint& tankPercent, FixPoint& siegePercent,
                             FixPoint& specialPercent, FixPoint& launcherPercent,
                             FixPoint& ornithopterPercent,
                             bool emitStatsLog);

    // Build context snapshot
    QuantBotBuildContext buildContextSnapshot(int militaryValue);

    Sint32 cityBuildTimer = 0;
};

#endif //QuantBot_H
