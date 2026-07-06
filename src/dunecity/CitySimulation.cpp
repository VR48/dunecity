#include <dunecity/CitySimulation.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>

#include <globals.h>
#include <Game.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <SDL2/SDL_log.h>

namespace DuneCity {

CitySimulation::CitySimulation() = default;
CitySimulation::~CitySimulation() = default;

void CitySimulation::setCityTax(int16_t v) {
    if (v < kMinTaxRate) v = kMinTaxRate;
    if (v > kMaxTaxRate) v = kMaxTaxRate;
    cityTax_ = v;
}

void CitySimulation::init(int width, int height) {
    mapWidth_  = width;
    mapHeight_ = height;

    powerGridMap_.init(width, height, 2);
    trafficDensityMap_.init(width, height, 2);
    pollutionDensityMap_.init(width, height, 2);
    landValueMap_.init(width, height, 2);
    crimeRateMap_.init(width, height, 2);
    populationDensityMap_.init(width, height, 2);
    growthRateMap_.init(width, height, 2);

    initialized_ = true;
}

void CitySimulation::load(InputStream& stream) {
    // SAVEGAMEVERSION 9809+ persists the full sim state. Older saves
    // wrote nothing in this slot, so just leave the freshly-initialised
    // defaults in place and let the first effects scan rebuild stats.
    if (!currentGame || currentGame->getLoadedSavegameVersion() < 9809) {
        return;
    }

    // Save format compatibility: load into house 0 (local player's state
    // will be redirected via localHouseID() at runtime).
    auto& hs = houseState_[0];
    hs.resPop = stream.readSint32();
    hs.comPop = stream.readSint32();
    hs.indPop = stream.readSint32();
    hs.prevResPop = hs.resPop;
    hs.prevComPop = hs.comPop;
    hs.prevIndPop = hs.indPop;
    hs.resValve = stream.readSint16();
    hs.comValve = stream.readSint16();
    hs.indValve = stream.readSint16();

    totalFunds_              = stream.readSint32();
    cityTax_                 = stream.readSint16();
    cityYear_                = stream.readSint32();
    cityDay_                 = stream.readSint32();
    lastProcessedDay_        = stream.readUint32();
    lastTaxYear_             = stream.readUint32();
    lastBudgetTick_          = (lastProcessedDay_ * kCyclesPerCityDay) / kCyclesPerBudgetTick;
    economicVictoryThreshold_= stream.readSint32();

    hs.policeFundingPercent = stream.readSint32();
    hs.nominalPoliceCost    = stream.readSint32();
    hs.lastPoliceExpense    = stream.readSint32();

    hs.budget.setLastTaxRevenue(stream.readSint32());
    hs.budget.setRoadPercent(stream.readSint32());
    hs.budget.setPolicePercent(stream.readSint32());
    hs.budget.setFirePercent(stream.readSint32());

    milestoneFirstZoneAchieved_ = stream.readBool();
    for (int i = 0; i < NUM_MILESTONES; ++i) {
        milestones_[i] = stream.readBool();
    }
}

void CitySimulation::save(OutputStream& stream) const {
    // Save format compatibility: save house 0's state (matches old format).
    const auto& hs = houseState_[0];
    stream.writeSint32(hs.resPop);
    stream.writeSint32(hs.comPop);
    stream.writeSint32(hs.indPop);
    stream.writeSint16(hs.resValve);
    stream.writeSint16(hs.comValve);
    stream.writeSint16(hs.indValve);

    stream.writeSint32(totalFunds_);
    stream.writeSint16(cityTax_);
    stream.writeSint32(cityYear_);
    stream.writeSint32(cityDay_);
    stream.writeUint32(lastProcessedDay_);
    stream.writeUint32(lastTaxYear_);
    stream.writeSint32(economicVictoryThreshold_);

    stream.writeSint32(hs.policeFundingPercent);
    stream.writeSint32(hs.nominalPoliceCost);
    stream.writeSint32(hs.lastPoliceExpense);

    stream.writeSint32(hs.budget.getLastTaxRevenue());
    stream.writeSint32(hs.budget.getRoadPercent());
    stream.writeSint32(hs.budget.getPolicePercent());
    stream.writeSint32(hs.budget.getFirePercent());

    stream.writeBool(milestoneFirstZoneAchieved_);
    for (int i = 0; i < NUM_MILESTONES; ++i) {
        stream.writeBool(milestones_[i]);
    }
}

void CitySimulation::advancePhase(uint32_t gameCycleCount) {
    if (!effectsEnabled_ || !initialized_) return;

    // City clock — SimCity Classic-style. A year is 48 day-ticks; each
    // day ticks at gameCycleCount / kCyclesPerCityDay. Day ticks drive
    // effects scans and zone growth; year ticks drive taxes/budget.
    const uint32_t totalDays = gameCycleCount / kCyclesPerCityDay;
    cityYear_ = static_cast<int>(totalDays / kCityDaysPerYear);
    cityDay_  = static_cast<int>(totalDays % kCityDaysPerYear);

    // Budget tick: runs every ~1 second (62 cycles), pays 1/50th of the
    // annual budget. Decoupled from the day-tick so income is smooth.
    const uint32_t budgetTick = gameCycleCount / kCyclesPerBudgetTick;
    if (budgetTick > lastBudgetTick_) {
        lastBudgetTick_ = budgetTick;
        runDailyBudget();
    }

    // Day tick: run effects scans and zone growth on SEPARATE cycles to
    // halve the per-frame spike. Effects scan runs on the day boundary;
    // zone growth runs on the next game cycle via pendingGrowthPhase_.
    if (pendingGrowthPhase_) {
        pendingGrowthPhase_ = false;
        runZoneGrowth();
    }

    if (totalDays > lastProcessedDay_) {
        lastProcessedDay_ = totalDays;
        runEffectsScans();
        pendingGrowthPhase_ = true;

        // Diagnostic snapshot every 8 city days (~1/6 city year). Logs
        // average/max land value, crime, pollution across DEVELOPED
        // blocks only — undeveloped desert always sits at 0 and would
        // dominate the mean. Lets us tell at a glance whether one side
        // of the map is starving on land value or drowning in crime.
        if (cityDay_ % 8 == 0) {
            const int bs = std::max(1, landValueMap_.getBlockSize());
            const int bw = (mapWidth_  + bs - 1) / bs;
            const int bh = (mapHeight_ + bs - 1) / bs;
            int devBlocks = 0;
            long long lvSum = 0, crSum = 0, poSum = 0;
            int lvMax = 0, crMax = 0, poMax = 0;
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    const int lv = landValueMap_.get(bx, by);
                    if (lv <= 0) continue;
                    ++devBlocks;
                    const int cr = crimeRateMap_.get(bx, by);
                    const int po = pollutionDensityMap_.get(bx, by);
                    lvSum += lv; crSum += cr; poSum += po;
                    if (lv > lvMax) lvMax = lv;
                    if (cr > crMax) crMax = cr;
                    if (po > poMax) poMax = po;
                }
            }
            if (devBlocks > 0) {
                SDL_Log("[CitySim] day=%d/%d devBlocks=%d "
                        "lv(avg/max)=%lld/%d crime=%lld/%d pol=%lld/%d "
                        "popR/C/I=%d/%d/%d valves=R%+d C%+d I%+d",
                        cityYear_, cityDay_, devBlocks,
                        lvSum / devBlocks, lvMax,
                        crSum / devBlocks, crMax,
                        poSum / devBlocks, poMax,
                        getResPop(), getComPop(), getIndPop(),
                        getResValve(), getComValve(), getIndValve());
            }
        }
    }
}

void CitySimulation::registerPowerSource(int /*x*/, int /*y*/, int /*power*/) {
    // Stub — power grid registration not yet implemented
}

void CitySimulation::executeCityCommand(int /*playerID*/, int commandID,
                                        uint32_t p0, uint32_t p1, uint32_t p2) {
    if(!currentGameMap) return;

    switch(commandID) {
        case CMD_CITY_TOOL: {
            int x = static_cast<int>(p0);
            int y = static_cast<int>(p1);
            uint32_t toolType = p2;

            if(!currentGameMap->tileExists(x, y)) return;

            Tile* tile = currentGameMap->getTile(x, y);

            switch(toolType) {
                case CityTool_Road: {
                    auto placementState = makeCityTilePlacementState(
                        tile->isRock(),
                        tile->isMountain(),
                        tile->hasAGroundObject(),
                        tile->hasCityZone(),
                        tile->isRoad());

                    if (!applyRoadPlacement(placementState)) {
                        return;
                    }

                    tile->setRoad(placementState.hasRoad);
                    tile->setDestroyedStructureTile(DestroyedStructure_None);
                    SDL_Log("CityTool: Road placed at (%d, %d)", x, y);
                } break;

                case CityTool_Bulldoze:
                case CityTool_PowerLine:
                default:
                    break;
            }
        } break;

        case CMD_CITY_PLACE_ZONE: {
            int x = static_cast<int>(p0);
            int y = static_cast<int>(p1);
            auto zoneType = static_cast<ZoneType>(p2);

            if(!currentGameMap->tileExists(x, y)) return;

            Tile* tile = currentGameMap->getTile(x, y);
            tile->setCityZoneType(zoneType);
            // Start at density 0 (empty lot). The first growth scan will
            // bump this to 1 once basic conditions are met.
            tile->setCityZoneDensity(0);
        } break;

        case CMD_CITY_SET_TAX_RATE: {
            // p0 reserved (was houseID in the legacy form); p1 = new rate
            // (0-20). Routed through the command system so multiplayer
            // stays deterministic.
            auto* sim = currentGame ? currentGame->getCitySimulation() : nullptr;
            if (sim) {
                sim->setCityTax(static_cast<int16_t>(p1));
            }
        } break;

        case CMD_CITY_SET_BUDGET: {
            // p0 = police funding %; p1/p2 reserved (no roads/fire in
            // the DuneCity budget model). Routed through the command
            // system so multiplayer stays deterministic.
            auto* sim = currentGame ? currentGame->getCitySimulation() : nullptr;
            if (sim) {
                sim->setPoliceFundingPercent(static_cast<int>(p0));
            }
        } break;

        default:
            break;
    }
}

// spendCityFunds is implemented in CityEffectsRuntime.cpp so it can
// access pLocalHouse directly. The test build provides its own stub
// in CityEffectsRuntime_test_stub.cpp that operates on totalFunds_.

void DuneCity::CitySimulation::checkAndTriggerMilestones(int32_t totalPop, int32_t totalFunds,
                                                         int32_t resPop, int32_t comPop, int32_t indPop,
                                                         bool hasRoads) {
    if (totalPop >= 100) milestones_[MILESTONE_POPULATION_100] = true;
    if (totalPop >= 500) milestones_[MILESTONE_POPULATION_500] = true;
    if (totalPop >= 1000) milestones_[MILESTONE_POPULATION_1000] = true;
    if (totalPop >= 5000) milestones_[MILESTONE_POPULATION_5000] = true;
    if (totalFunds >= 1000) milestones_[MILESTONE_ECONOMY_1000] = true;
    if (totalFunds >= 5000) milestones_[MILESTONE_ECONOMY_5000] = true;
    if (milestoneFirstZoneAchieved_) milestones_[MILESTONE_FIRST_ZONE] = true;
    if (hasRoads && resPop > 0 && comPop > 0 && indPop > 0) milestones_[MILESTONE_ALL_ZONES] = true;
}

} // namespace DuneCity
