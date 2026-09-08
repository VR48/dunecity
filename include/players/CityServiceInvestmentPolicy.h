#ifndef CITY_SERVICE_INVESTMENT_POLICY_H
#define CITY_SERVICE_INVESTMENT_POLICY_H
#include <algorithm>
#include <cstdint>
#include <dunecity/CityEffects.h>

namespace CityServiceInvestmentPolicy {
inline int crimeHarm(int crime, int item, int population) {
    int growthPenalty = 0;
    if (item == Structure_ZoneResidential) growthPenalty = crime > 150 ? 200 : crime > 100 ? 100 : 0;
    if (item == Structure_ZoneCommercial) growthPenalty = crime > 120 ? 150 : crime > 80 ? 75 : 0;
    return std::max(0, crime - 63) / 4 + growthPenalty * std::max(1, population) / 4;
}
// Match stampFalloff: every world tile adds into its coarse land-value cell.
inline int parkContribution(int item, int cx, int cy, int px, int py, int blockSize) {
    int value = 0;
    for (int y = (py/blockSize)*blockSize; y < (py/blockSize+1)*blockSize; ++y)
        for (int x = (px/blockSize)*blockSize; x < (px/blockSize+1)*blockSize; ++x)
            value += DuneCity::falloff(DuneCity::getParkLandValueBonus(item),
                std::max(std::abs(cx-x),std::abs(cy-y)),DuneCity::getParkLandValueRadius(item)+1);
    return value;
}
// One game-year horizon. Crime is a civic utility weight, not tax income:
// weighted by actual growth thresholds and severe-crime relief.
struct Value {
    int crime = 0, tax = 0, growthTax = 0, defense = 0;
    int buildCost = 0, upkeep = 0, powerCost = 0, overlapPenalty = 0;
    int neighbourhoodTax = 0; // R/C share of tax gain; preference, not extra income.
    int crimeUtility = 0, dangerousRelief = 0;
    int cost() const { return std::max(1, buildCost + upkeep + powerCost + overlapPenalty); }
    int64_t benefit() const { return int64_t(crimeUtility) + tax + growthTax + defense; }
    bool repaysThroughLandValue() const { return int64_t(tax) + growthTax > cost(); }
    bool landValueTurretEligible() const {
        return crime > 0 && neighbourhoodTax > 0 && repaysThroughLandValue();
    }
    bool useful(bool emergency) const {
        return crime > 0 && (benefit() > cost() || (emergency && dangerousRelief >= 32));
    }
    bool betterThan(const Value& other) const {
        // Modest placement preference for improving homes and businesses.
        const int64_t left = (benefit()+neighbourhoodTax/2) * other.cost();
        const int64_t right = (other.benefit()+other.neighbourhoodTax/2) * cost();
        return left != right ? left > right : crime > other.crime;
    }
};
inline int annualTaxGain(int population, int taxPercent, int valueGainSum, int sampledBuildings) {
    if (sampledBuildings <= 0) return 0;
    return int(int64_t(population) * 200 * taxPercent * valueGainSum
        / (int64_t(100) * 3 * 128 * sampledBuildings));
}
}
#endif
