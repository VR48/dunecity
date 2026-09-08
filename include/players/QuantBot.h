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
#include <players/CombatReward.h>
#include <players/UnitMixPolicy.h>
#include <units/MCV.h>
class Harvester;
#include <players/QuantBotConfig.h>
#include <players/AIDecisionLog.h>

#include <DataTypes.h>
#include <limits>
#include <set>
#include <map>
#include <unordered_map>
#include <array>

class QuantBot : public Player
{
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
    void onHumanUnitOrder(Uint32 id);
    void finishTelemetry() override;
    void onCombatReward(Uint32 attacker, Uint32 target, const CombatReward::Totals& reward) override;

    /// Observational data for the compact end-of-match metaserver summary.
    /// It is not saved or consulted by simulation decisions.
    const std::array<int, 8>& getLastUnitMixBps() const { return lastUnitMixBps; }
    std::string getDifficultyName() const;
    bool permitsPoliceReinforcement(int unitValue) const;

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
    // Legacy squad fields retained for save compatibility; released on first update.
    Uint32 groundSquadPhase = 0, groundSquadStarted = 0, groundSquadNextControl = 0;
    Uint32 groundSquadInitialCount = 0, groundSquadObjective = NONE_ID, groundSquadObjectiveCycle = 0;
    Uint32 groundSquadProgressCycle = 0;
    Coord groundSquadProgressLocation = Coord::Invalid();
    UnitMixPolicy::PerformanceWindow performanceWindow;
    std::set<Uint32> groundSquad;
    std::map<Uint32, Uint32> manualUnitOrders, escortAssignments;
    void launchGroundHunt();
    void releaseLegacyGroundSquad();
    std::map<Uint32,Uint32> defenceResponseCycles;
    bool humanControls(const UnitBase* unit) const;
    Coord squadRallyLocation = Coord::Invalid();
    Uint32 rallySelectedCycle = std::numeric_limits<Uint32>::max();
    Uint32 nonServiceConstructionOrders = 3; // Respond immediately to a new crime emergency.
    Uint32 powerDemandSampleCycle = 0;
    Sint32 powerDemandSample = 0;
    Sint32 projectedPowerDemandGrowth = 0;
    Coord squadRetreatLocation = Coord::Invalid();
    bool supportMode = false;
    Uint32 lastStatsLogCycle = 0;
    Uint32 lastTelemetrySnapshotCycle = 0;
    Uint32 lastCityBuildingSnapshotCycle = 0;
    uint64_t telemetryState = 0; // Runtime only; never part of save/simulation state.
    std::array<int, 8> lastUnitMixBps{};
    // Diagnostic de-duplication only. These must never affect a game decision,
    // save, or lockstep state.
    std::map<Uint32, uint64_t> lastKiteTrace;
    std::map<Uint32, uint64_t> lastHarvesterSafetyTrace;
    std::map<Uint32, std::pair<uint64_t, Uint32>> lastHeavyAllocationTrace;
    std::map<Uint32, AITelemetry::Record> placementScoreDetails;
    std::map<Uint32, uint64_t> zoneDecisionIds;
    std::map<Uint32, Uint32> lastZoneTraceCycle;
    uint64_t traceDecision(const std::string& event, AITelemetry::Record details) const;

    Uint32 ixEligibleSinceCycle = std::numeric_limits<Uint32>::max();
    Uint32 palaceEligibleSinceCycle = std::numeric_limits<Uint32>::max();
    
    std::map<Uint32, int> idleHarvesterCounters; ///< Track idle time for each harvester (objectID -> cycle count)
    std::map<Uint32, int> harvesterMovingCounters; ///< Track continuous movement time (objectID -> cycle count)

    void scrambleUnitsAndDefend(const ObjectBase* pIntruder);


    Coord findMcvPlaceLocation(const MCV* pMCV);
    Coord findPlaceLocation(Uint32 itemID);
    Coord findRedevelopmentSite(Uint32 itemID);
    bool redevelopmentZones(Uint32 itemID, Coord pos, std::vector<Uint32>& zones) const;
    Coord findPlaceLocationSimple(Uint32 itemID);
    Coord findSlabPlaceLocation(Uint32 itemID);
    Coord findTurretPlaceLocation(Uint32 itemID);
    bool selectCityServiceInvestment(const BuilderBase* builder, int money, bool emergency,
                                    Uint32& item, Coord& site, bool landValueOnly = false);
    Coord findCityTurretPlaceLocation(Uint32 itemID, int* defenseScore = nullptr, int* amenityScore = nullptr,
                                      int* crimeBenefit = nullptr, int* crimeHotspot = nullptr);
    Coord findCityCrimeServicePlaceLocation(Uint32 itemID, int* crimeBenefit = nullptr,
                                            int* crimeHotspot = nullptr);
    Coord findEffectiveTurretPlaceLocation(Uint32 itemID);
    Coord findSquadCenter(int houseID);
    Coord findBaseCentre(int houseID);
    Coord findBestDeathHandTarget(int enemyHouseID);
    const UnitBase* findLightRaiderTarget(const UnitBase* raider) const;
    const UnitBase* findThreateningTank(const UnitBase* raider) const;
    double getProductionBuildingMultiplier(int itemID) const;
    // Runtime-only observations. Never consulted by tactical/production decisions.
    struct HarvesterStrikeMemberTrace { Uint32 id, item; int price; CombatReward::Totals reward; };
    struct HarvesterStrikeTrace {
        uint64_t id; Uint32 target, start, sampled, logged, lastVisible;
        Uint32 transitCycles = 0, engagementCycles = 0;
        bool targetKilledByStrike = false;
        std::vector<HarvesterStrikeMemberTrace> members;
    };
    std::vector<HarvesterStrikeTrace> harvesterStrikeTraces;
    void updateHarvesterStrikeTelemetry(bool final = false);
    Coord findSquadRallyLocation();
    Coord findSquadRetreatLocation();
    void moveToOptimalSquadPosition(const UnitBase* pUnit, FixPoint squadRadius, int* orderBudget = nullptr);
    void kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange);

    bool tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                    const QuantBotConfig& config);

    std::list<Coord> placeLocations;    ///< Where to place structures
    // Runtime-only plans; the legacy list above remains in the save layout.
    // After loading, each yard safely finds positions for its own queued items.
    std::map<Uint32, std::list<Coord>> builderPlaceLocations;
    struct PlannedStructure { Uint32 item; Coord location; };
    std::map<Uint32, PlannedStructure> reservedStructures;
    struct RecentStructureLoss { Coord location; Coord size; Uint32 cycle; Uint32 item; };
    std::vector<int> tacticalDanger, harvesterDanger, lossDanger;
    std::vector<Coord> visibleEnemyBases;
    Uint32 dangerUpdated = std::numeric_limits<Uint32>::max();
    Uint32 lastSafetyTrace = std::numeric_limits<Uint32>::max();
    struct HarvesterSafety { Uint32 nextCheck = 0, retreatUntil = 0; Coord lastLocation = Coord::Invalid(), plannedDestination = Coord::Invalid(); bool controlled = false; };
    std::map<Uint32, HarvesterSafety> harvesterSafety;
    struct UnsafeField { Coord location; Uint32 cycle; };
    std::vector<UnsafeField> unsafeFields;
    void refreshTacticalDanger();
    int dangerAt(Coord pos, Coord size = Coord(1, 1), bool losses = false) const;
    bool reactorClearance(Uint32 item, Coord pos) const;
    int rearScore(Coord pos, Coord base) const;
    int recentFactoryLossCount() const;
    bool manageHarvesterSafety(const Harvester* harvester);

    std::vector<RecentStructureLoss> recentStructureLosses;
    bool nearRecentStructureLoss(int x, int y, int width, int height) const;
    Uint32 planningBuilder = NONE_ID;
    bool overlapsReservedStructure(int x, int y, int width, int height) const;
    OrnithopterStrikeTeam ornithopterStrikeTeam;
    std::unordered_map<Uint32, Coord> placementCache; ///< Per-build-cycle cache for findPlaceLocation results

    void checkAllUnits();
    void retreatAllUnits();
    void build(int militaryValue);
    void attack(int militaryValue);
    void manageCityBuilding();

    Sint32 cityBuildTimer = 0;
};

#endif //QuantBot_H
