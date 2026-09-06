#ifndef QUANTBOT_BUILD_POLICY_H
#define QUANTBOT_BUILD_POLICY_H

#include <data.h>
#include <SDL_stdinc.h>
#include <Definitions.h>
#include <algorithm>
#include <array>

namespace QuantBotBuildPolicy {

inline int palaceTarget(bool onlyOnePalace, bool citySim, int displayedPopulation) {
    return onlyOnePalace || !citySim ? 1 : 1 + std::max(0, displayedPopulation) / 30000;
}

inline int spendableCredits(int credits, int strategicCost) {
    return std::max(0, credits - std::max(0, strategicCost));
}

// Rank live demand against the existing 3R:1I:1C target. Callers try each
// candidate in order so an unavailable/landlocked zone cannot stall the yard.
inline std::array<Uint32, 3> rankZones(int residential, int commercial, int industrial,
                                      int resDemand, int comDemand, int indDemand,
                                      bool bootstrap) {
    struct Candidate { Uint32 item; int count; int demand; int gap; };
    std::array<Candidate, 3> candidates{{
        {Structure_ZoneResidential, residential, resDemand,
         std::max(commercial, industrial) * 3 + 3 - residential},
        {Structure_ZoneIndustrial, industrial, indDemand, std::max(residential / 3, 1) - industrial},
        {Structure_ZoneCommercial, commercial, comDemand, std::max(residential / 3, 1) - commercial}
    }};
    std::stable_sort(candidates.begin(), candidates.end(), [bootstrap](const auto& a, const auto& b) {
        const bool missingA = bootstrap && a.count == 0;
        const bool missingB = bootstrap && b.count == 0;
        if(missingA != missingB) return missingA;
        return a.gap > b.gap;
    });
    std::array<Uint32, 3> result{{NONE_ID, NONE_ID, NONE_ID}};
    int index = 0;
    for(const auto& candidate : candidates) {
        if(candidate.demand > 0 || (bootstrap && candidate.count == 0)) {
            result[index++] = candidate.item;
        }
    }
    return result;
}

inline int desiredHeavyFactories(bool citySim, int creditsPerSecond, int credits) {
    // Income sustains normal expansion; a large unspent treasury can fund
    // additional capacity. Bound expansion so a busy queue cannot grow it forever.
    const int incomeTarget = 1 + std::max(0, creditsPerSecond) / 50;
    const int cashTarget = 1 + std::max(0, credits) / (citySim ? 10000 : 4000);
    return std::clamp(citySim ? std::max(incomeTarget, cashTarget) : cashTarget, 1, 8);
}

} // namespace QuantBotBuildPolicy
#endif
