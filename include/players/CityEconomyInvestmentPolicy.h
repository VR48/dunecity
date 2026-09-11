#ifndef CITY_ECONOMY_INVESTMENT_POLICY_H
#define CITY_ECONOMY_INVESTMENT_POLICY_H

#include <players/QuantBotBuildPolicy.h>
#include <dunecity/CityEffects.h>

namespace CityEconomyInvestmentPolicy {
// Compare both investments over four simulated minutes, including the time
// before their first income. These are forecasts, not measured cash flows.
constexpr int horizonCycles = 4 * DuneCity::kCyclesPerCityYear;
struct Investment {
    int cost = 0;
    int annualIncome = 0;
    int annualUpkeep = 0;
    int delayCycles = 0;
    int confidence = 1000;
    int projectedProceeds = -1; // Optional delivery-cycle forecast, before confidence.
    int proceeds() const {
        if (projectedProceeds >= 0)
            return int(int64_t(projectedProceeds) * std::clamp(confidence,0,1000) / 1000);
        return static_cast<int>(int64_t(std::max(0,annualIncome-annualUpkeep))
            * std::max(0,horizonCycles-delayCycles) * std::clamp(confidence,0,1000)
            / (DuneCity::kCyclesPerCityYear * 1000));
    }
};
// Refinery capacity must follow the fleet, never cap factory production.
inline int factoryHarvesterTarget(int sustainableWorkers, int mapLimit) {
    return std::max(0, std::min(sustainableWorkers, mapLimit));
}
// This is production priority, not a worker cap. While the army is short,
// keep about twice the current harvester capital in military strength. Rebuild
// a collapsed workforce first; once army needs are met, expand to the spice target.
inline bool preferFactoryHarvester(int workers, int target, int armyValue,
                                   int armyTarget, int workerPrice, bool canBuildMilitary) {
    if (workers >= target) return false;
    if (workers < 2 || !canBuildMilitary || armyValue >= armyTarget) return true;
    return int64_t(armyValue) >= std::min<int64_t>(armyTarget,
        int64_t(std::max(0,workers))*std::max(0,workerPrice)*2);
}
inline bool processingCapacityNeeded(int refineries, int committedWorkers,
                                    int workerAnnualIncome, int bayAnnualCapacity) {
    return int64_t(std::max(0,committedWorkers)) * std::max(0,workerAnnualIncome)
        > int64_t(std::max(0,refineries)) * std::max(0,bayAnnualCapacity);
}
inline bool considerRefinery(bool processingNeeded, bool wantedIncludedWorker,
                             bool factoryCanSupply, bool workerRecovery = false) {
    // Military production is temporary. Do not buy a permanent unused bay just
    // because that factory is busy this pass. Recover a collapsed fleet first.
    return processingNeeded || (wantedIncludedWorker && (!factoryCanSupply || workerRecovery));
}
// Grow a permanent tax base alongside spice, rather than planting one token R.
// Count developing/queued lots conservatively so multiple yards do not duplicate
// the hedge. Aim for tax >= one third of spice (25% of combined income).
inline bool taxHedgeNeeded(int taxIncome, int developingIncome, int spiceIncome) {
    return int64_t(std::max(0,taxIncome) + std::max(0,developingIncome)) * 3
        < std::max(0,spiceIncome);
}
inline bool preferRefinery(const Investment& refinery, const Investment& zone,
                           bool refineryUseful, bool residentialHedge, bool processingNeeded = false) {
    if (!refineryUseful || refinery.cost <= 0 || refinery.proceeds() <= refinery.cost) return false;
    if (processingNeeded) return true; // Release an economically worthwhile unloading bottleneck first.
    if (residentialHedge) return false;
    if (zone.cost <= 0) return true;
    // Return per credit accounts for the four 100-credit plots that can be
    // bought instead of a 400-credit refinery. Ties favour permanent tax income.
    return int64_t(refinery.proceeds()) * zone.cost > int64_t(zone.proceeds()) * refinery.cost;
}
inline int marginalSpiceIncome(int workers, int refineries, bool freeWorker,
                              int workerAnnualIncome, int refineryAnnualCapacity) {
    const int before = std::min(workers*workerAnnualIncome,refineries*refineryAnnualCapacity);
    const int after = std::min((workers+int(freeWorker))*workerAnnualIncome,
                              (refineries+1)*refineryAnnualCapacity);
    return std::max(0,after-before);
}
// Existing/queued workers are common to both yard choices. Credit only bay
// relief plus actual full deliveries from the refinery's one included worker.
// A first load is a receipt, not an extra full-trip delay before steady income.
inline int refineryProceeds(int workers, int refineries, bool freeWorker,
                            int workerIncome, int bayIncome, int buildCycles,
                            int roundTripCycles, int unloadCycles, int load, int upkeep) {
    const int operating = std::max(0,horizonCycles-buildCycles);
    const int relief = marginalSpiceIncome(workers,refineries,false,workerIncome,bayIncome);
    const int addedWorker = marginalSpiceIncome(workers,refineries,freeWorker,workerIncome,bayIncome)-relief;
    const int deliveries = operating/std::max(1,roundTripCycles);
    const int64_t receipts = int64_t(deliveries)*load*addedWorker/std::max(1,workerIncome);
    const int64_t released = int64_t(relief)*std::max(0,operating-unloadCycles)/DuneCity::kCyclesPerCityYear;
    const int64_t bills = int64_t(upkeep)*operating/DuneCity::kCyclesPerCityYear;
    return int(std::max<int64_t>(0,receipts+released-bills));
}
inline int zoneConfidence(int demand, int maximum, int pollution, int crime, int unfinished) {
    if (demand <= 0) return 0;
    const int demandConfidence = std::clamp(demand*1000/std::max(1,maximum),250,1000);
    const int environment = pollution >= DuneCity::kPollutionGrowthBlock ? 0
        : pollution > DuneCity::kPollutionGrowthThreshold ? 500 : 1000;
    return demandConfidence * environment / 1000 * (crime>=192 ? 500 : 1000) / 1000
        / (1+std::max(0,unfinished));
}
}
#endif
