#ifndef DUNECITY_CITYSIMULATION_H
#define DUNECITY_CITYSIMULATION_H

#include <cstdint>
#include <dunecity/CityConstants.h>
#include <dunecity/CityMapLayer.h>
#include <dunecity/CityBudget.h>

class InputStream;
class OutputStream;

namespace DuneCity {

class CitySimulation {
public:
    CitySimulation();
    ~CitySimulation();

    void init(int width, int height);
    void load(InputStream& stream);
    void save(OutputStream& stream) const;
    void advancePhase(uint32_t gameCycleCount);

    bool isInitialized() const { return initialized_; }

    static void executeCityCommand(int playerID, int commandID,
                                   uint32_t p0, uint32_t p1, uint32_t p2);

    void registerPowerSource(int x, int y, int power);

    int getResPop() const { return resPop_; }
    int getComPop() const { return comPop_; }
    int getIndPop() const { return indPop_; }
    int getTotalPop() const { return resPop_ + comPop_ + indPop_; }

    int16_t getResValve() const { return resValve_; }
    int16_t getComValve() const { return comValve_; }
    int16_t getIndValve() const { return indValve_; }

    int32_t getTotalFunds() const { return totalFunds_; }
    void setTotalFunds(int32_t v) { totalFunds_ = v; }
    int16_t getCityTax() const { return cityTax_; }
    /// Set the tax rate (percentage of population paid annually). Clamped
    /// to [kMinTaxRate, kMaxTaxRate] — outside that band the budget either
    /// stops collecting (0%) or crushes growth (20%+).
    void setCityTax(int16_t v);
    static constexpr int16_t kMinTaxRate = 0;
    static constexpr int16_t kMaxTaxRate = 20;
    int getCityYear() const { return cityYear_; }
    int getCityDay()  const { return cityDay_; }

    /// Police-funding %: 0..100, default 100. Scales coverage from each
    /// police-role structure proportionally and is also the share of
    /// annual police cost actually paid into the budget.
    int getPoliceFundingPercent() const { return policeFundingPercent_; }
    void setPoliceFundingPercent(int v) {
        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        policeFundingPercent_ = v;
    }
    /// Last computed police annual expense (full nominal, before funding%).
    int32_t getNominalPoliceCost() const { return nominalPoliceCost_; }
    /// Last actual amount paid out (nominal * funding%/100).
    int32_t getLastPoliceExpense() const { return lastPoliceExpense_; }

    int getMapWidth() const { return mapWidth_; }
    int getMapHeight() const { return mapHeight_; }

    bool isPowerShortageJustStarted() const { return false; }
    int32_t getUnpoweredZoneCount() const { return 0; }
    int32_t getEconomicVictoryThreshold() const { return economicVictoryThreshold_; }

    /// City Effects feature flag (Phase 6). When false, all effect scans
    /// are skipped and the maps stay at zero. Set from Game options at init.
    void setCityEffectsEnabled(bool enabled) { effectsEnabled_ = enabled; }
    bool areCityEffectsEnabled() const { return effectsEnabled_; }

    const CityMapLayer<uint8_t>& getPowerGridMap() const { return powerGridMap_; }
    const CityMapLayer<uint8_t>& getTrafficDensityMap() const { return trafficDensityMap_; }
    const CityMapLayer<uint8_t>& getPollutionDensityMap() const { return pollutionDensityMap_; }
    const CityMapLayer<uint8_t>& getLandValueMap() const { return landValueMap_; }
    const CityMapLayer<uint8_t>& getCrimeRateMap() const { return crimeRateMap_; }
    const CityMapLayer<uint8_t>& getPopulationDensityMap() const { return populationDensityMap_; }

    CityBudget& getCityBudget() { return budget_; }
    const CityBudget& getCityBudget() const { return budget_; }

    // Milestone stub methods (Phase 9 - declared but not yet implemented)
    bool wasFirstZoneMilestoneTriggered() const { return milestoneFirstZoneAchieved_; }
    void onFirstZoneBuilt() { milestoneFirstZoneAchieved_ = true; }

    // Milestone tracking
    enum MilestoneType {
        MILESTONE_POPULATION_100 = 0,
        MILESTONE_POPULATION_500,
        MILESTONE_POPULATION_1000,
        MILESTONE_POPULATION_5000,
        MILESTONE_FIRST_ZONE,
        MILESTONE_ECONOMY_1000,
        MILESTONE_ECONOMY_5000,
        MILESTONE_ALL_ZONES,
        NUM_MILESTONES
    };
    bool hasMilestoneBeenAchieved(int milestone) const {
        return milestone >= 0 && milestone < NUM_MILESTONES && milestones_[milestone];
    }

    // Phase/milestone methods used by Game.cpp
    int getPhaseCycle() const { return 9; } // Always return 9 so milestone check runs
    void checkAndTriggerMilestones(int32_t totalPop, int32_t totalFunds, int32_t resPop, int32_t comPop, int32_t indPop, bool hasRoads);
    bool spendCityFunds(int32_t amount);

private:
    bool initialized_ = false;
    bool milestones_[NUM_MILESTONES] = {};
    bool milestoneFirstZoneAchieved_ = false;
    int mapWidth_  = 0;
    int mapHeight_ = 0;

    /// Default tax rate used at game start. 7% sits between SC Classic's
    /// suggested 6% and the punitive 10%+ band — keeps growth healthy
    /// while making the budget meaningful. Player can adjust at runtime
    /// via setCityTax() / the budget window.
    static constexpr int16_t kDefaultTaxRate = 7;

    int resPop_ = 0;
    int comPop_ = 0;
    int indPop_ = 0;
    int16_t resValve_ = 0;
    int16_t comValve_ = 0;
    int16_t indValve_ = 0;
    int32_t totalFunds_ = 0;
    int16_t cityTax_ = kDefaultTaxRate;
    int cityYear_ = 0;
    int32_t economicVictoryThreshold_ = 500;

    int     policeFundingPercent_ = 100;  ///< 0..100; scales police coverage AND expense
    int32_t nominalPoliceCost_    = 0;    ///< Sum of getPoliceAnnualCost() across police-role structures
    int32_t lastPoliceExpense_    = 0;    ///< Actually-paid-out portion (nominal * funding/100)

    CityMapLayer<uint8_t> powerGridMap_;
    CityMapLayer<uint8_t> trafficDensityMap_;
    CityMapLayer<uint8_t> pollutionDensityMap_;
    CityMapLayer<uint8_t> landValueMap_;
    CityMapLayer<uint8_t> crimeRateMap_;
    CityMapLayer<uint8_t> populationDensityMap_;

    CityBudget budget_;

    bool      effectsEnabled_ = true;   ///< City effects feature flag
    uint32_t  lastProcessedDay_ = 0;    ///< Last city-day number that ran a scan + growth roll
    uint32_t  lastTaxYear_ = 0;         ///< Last year tax was collected (cityYear_ at collection)
    int       cityDay_ = 0;             ///< Current city day (resets within the year, monotonic across years)

    /// Full-map scan: rebuilds pollution, land value, crime maps and runs
    /// zone growth from current map contents. Called from advancePhase()
    /// at a slow cadence (every kEffectsScanCycles). Pure read of game
    /// state, deterministic — safe in multiplayer.
    void runEffectsScans();
    void runZoneGrowth();

    /// Year-tick budget pass: walks all structures on the map, aggregates
    /// population and police cost per house, then applies (tax revenue -
    /// funded police bill) to each house's credit pool. Every owner with
    /// city-role structures pays their own bill — not just the local
    /// player. Lives in CityEffectsRuntime.cpp so the test build doesn't
    /// have to link House/pLocalHouse/StructureBase.
    void runAnnualBudget();
};

} // namespace DuneCity

#endif // DUNECITY_CITYSIMULATION_H
