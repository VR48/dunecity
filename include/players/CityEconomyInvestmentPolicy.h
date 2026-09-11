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
    int proceeds() const {
        return static_cast<int>(int64_t(std::max(0,annualIncome-annualUpkeep))
            * std::max(0,horizonCycles-delayCycles) * std::clamp(confidence,0,1000)
            / (DuneCity::kCyclesPerCityYear * 1000));
    }
};
inline bool refineryCapacityNeeded(int refineries, int workers, int sustainableWorkers) {
    return refineries < QuantBotBuildPolicy::desiredSpiceRefineries(sustainableWorkers,workers);
}
inline bool preferRefinery(const Investment& refinery, const Investment& zone,
                           bool capacityNeeded, bool residentialHedge) {
    if (!capacityNeeded || residentialHedge || refinery.cost <= 0 || refinery.proceeds() <= refinery.cost) return false;
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
