#ifndef QUANTBOT_BUILD_POLICY_H
#define QUANTBOT_BUILD_POLICY_H

#include <data.h>
#include <SDL_stdinc.h>
#include <Definitions.h>
#include <algorithm>
#include <array>

namespace QuantBotBuildPolicy {

// Zero disables the reservation; otherwise one service order per N orders.
inline unsigned crimeServiceOrderInterval(int developed, int dangerous) {
    if (developed <= 0) return 0;
    if (int64_t(dangerous) * 2 > developed) return 2;
    return int64_t(dangerous) * 4 >= developed ? 4 : 0;
}
inline bool crimeServiceOrderDue(unsigned interval, unsigned nonServiceOrders) {
    return interval > 0 && nonServiceOrders >= interval - 1;
}

inline bool viableMainWave(int units, int value) { return units >= 6 && value >= 3000; }
inline bool replaceHarvestAnchor(bool safe, int currentWorkers, int bestWorkers,
                                 Uint32 elapsed, Uint32 dwell) {
    return !safe || currentWorkers == 0
        || (elapsed >= dwell && bestWorkers >= currentWorkers + std::max(1, (currentWorkers + 3) / 4));
}
struct AllocationCandidate { int price, committedValue, targetBps; bool available; };
inline bool militaryItem(Uint32 item) {
    return isUnit(item) && item != Unit_Carryall && item != Unit_Harvester
        && item != Unit_MCV && item != Unit_Sandworm;
}
inline int fundedArmyTarget(int committed, int limit, int spendable) {
    return std::min(limit, committed + std::max(0, spendable));
}
template<size_t N>
int fundedDeficit(const std::array<AllocationCandidate,N>& candidates, int committed,
                  int money, int limit, int target) {
    int selected = -1;
    int64_t best = 0;
    for (size_t i=0;i<N;++i) {
        const auto& c=candidates[i];
        if (!c.available || c.price<=0 || c.price>money || committed+c.price>limit) continue;
        const int64_t deficit=int64_t(target)*c.targetBps-int64_t(c.committedValue)*10000;
        if (deficit>best) { best=deficit; selected=static_cast<int>(i); }
    }
    return selected;
}
// Shares guide composition; they must not strand funded army capacity when
// another factory family cannot keep up. Choose the least overrepresented
// available type relative to its learned share, including this next unit.
template<size_t N>
int capacityFill(const std::array<AllocationCandidate,N>& candidates, int committed,
                 int money, int limit) {
    int selected=-1;
    for (size_t i=0;i<N;++i) {
        const auto& c=candidates[i];
        if (!c.available || c.targetBps<=0 || c.price<=0 || c.price>money || committed+c.price>limit) continue;
        if (selected<0 || int64_t(c.committedValue+c.price)*candidates[selected].targetBps
                < int64_t(candidates[selected].committedValue+candidates[selected].price)*c.targetBps)
            selected=static_cast<int>(i);
    }
    return selected;
}

template<size_t N>
int allocationHorizon(const std::array<AllocationCandidate, N>& candidates,
                      int armyValue, int money, int armyLimit) {
    // Plan one funded unit ahead, allowing an empty army to start growing.
    int increment = 0;
    for (const auto& c : candidates)
        if (c.available && c.price > 0 && c.price <= money && armyValue + c.price <= armyLimit)
            increment = std::max(increment, c.price);
    return std::min(armyLimit, armyValue + increment);
}
template<size_t N>
int largestAffordableDeficit(const std::array<AllocationCandidate, N>& candidates,
                             int armyValue, int money, int armyLimit, int minimumHorizon = 0) {
    const int horizon = std::max(allocationHorizon(candidates,armyValue,money,armyLimit),
                                 std::clamp(minimumHorizon, 0, armyLimit));
    int selected = -1;
    int64_t best = 0;
    for (size_t i = 0; i < N; ++i) {
        const auto& c = candidates[i];
        if (!c.available || c.price <= 0 || c.price > money || armyValue + c.price > armyLimit) continue;
        const int64_t deficit = int64_t(horizon) * c.targetBps - int64_t(c.committedValue) * 10000;
        if (deficit > best) { best = deficit; selected = static_cast<int>(i); }
    }
    return selected;
}

// When a small army already matches its percentages, the one-unit horizon above
// has no deficit and leaves every factory idle.  Grow the target in measured
// stages instead of treating a proportionally balanced 8k army as an 80k army.
inline int expansionAllocationHorizon(int armyValue, int armyLimit) {
    return std::min(std::max(0, armyLimit), std::max(armyValue + 1, armyValue * 2));
}


// Stateless decision randomness: identical inputs on every lockstep peer and after load.
inline int attackCommitmentPercent(Uint32 seed, Uint32 cycle, Uint32 house, Uint32 player) {
    Uint32 value = seed ^ (cycle * 0x9e3779b9u) ^ (house * 0x85ebca6bu) ^ (player * 0xc2b2ae35u);
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return 20 + static_cast<int>(value % 81u);
}

// Retain the 20..100 range, but harder vanilla bots favour committed waves.
// Extra stateless samples do not consume the simulation RNG stream.
inline int difficultyAttackCommitment(Uint32 seed, Uint32 cycle, Uint32 house, Uint32 player, int difficulty) {
    int percent = attackCommitmentPercent(seed, cycle, house, player);
    if (difficulty >= 2) percent = std::max(percent, attackCommitmentPercent(seed ^ 0xa511e9b3u, cycle, house, player));
    if (difficulty >= 3) percent = std::max(percent, attackCommitmentPercent(seed ^ 0x63d83595u, cycle, house, player));
    return percent;
}

// A harvester strike is a deliberate alternative to a normal hunt wave.  This
// remains stateless so it has the same result on every multiplayer peer and
// after loading a saved game.  The 20..45 part of the 20..100 sample gives an
// exposed harvester field roughly one third of eligible attack windows.
inline bool shouldUseMainHarvesterStrike(Uint32 seed, Uint32 cycle, Uint32 house, Uint32 player) {
    return attackCommitmentPercent(seed ^ 0x4f1bbcdcu, cycle, house, player) <= 45;
}

inline int attackForceBudget(int availableValue, int commitmentPercent, bool mainHarvesterStrike) {
    return mainHarvesterStrike ? availableValue
        : static_cast<int>(static_cast<Sint64>(availableValue) * commitmentPercent / 100);
}

inline bool easyReactorStrike(int nearbyAircraft, int readyAircraft, int antiAir) {
    return readyAircraft > 0 && nearbyAircraft * 2 >= readyAircraft && antiAir == 0;
}

inline int palaceTarget(bool onlyOnePalace, bool citySim, int displayedPopulation) {
    return onlyOnePalace || !citySim ? 1 : 1 + std::max(0, displayedPopulation) / 30000;
}

inline int spendableCredits(int credits, int strategicCost) {
    return std::max(0, credits - std::max(0, strategicCost));
}

inline int cityConstructionYardTarget(int credits, int residentialDemand,
                                      int commercialDemand, int industrialDemand) {
    const int activeDemand = (residentialDemand > 0) + (commercialDemand > 0) + (industrialDemand > 0);
    int target = 1 + activeDemand; // One builder per live R/C/I demand, plus the original yard.
    // Wealth funds parallel military, power and civic construction even when
    // the zone demand valves are zero or negative.
    if (credits >= 20000) target = std::max(target, 5);
    if (credits >= 50000) target = std::max(target, 6);
    if (credits >= 100000) target = 8;
    return std::clamp(target, 1, 8);
}

// City build capacity is funded from spare cash. Queued MCVs
// count toward the target, so parallel heavy factories can bootstrap a city
// quickly without continuously ordering MCVs after capacity is reached.
inline bool canFundCityYard(int credits, int mcvPrice, int constructionCapacity,
                            int targetCapacity, int workingReserve) {
    return constructionCapacity < targetCapacity && mcvPrice > 0
        && credits >= mcvPrice && credits - mcvPrice >= std::max(1000, workingReserve);
}
inline int baseDefenderTarget(int combatUnits) {
    return combatUnits > 0 ? std::max(1, combatUnits / 10) : 0;
}

// Demand valves have different maxima: R=2000, C/I=1500. Compare
// their fractions of maximum using integers; counts only break demand ties.
inline int normalizedZoneDemand(Uint32 item, int demand) {
    return std::clamp(demand, 0, item == Structure_ZoneResidential ? 2000 : 1500)
        * (item == Structure_ZoneResidential ? 3 : 4);
}

inline std::array<Uint32, 3> rankZones(int residential, int commercial, int industrial,
                                      int resDemand, int comDemand, int indDemand,
                                      bool bootstrap) {
    struct Candidate { Uint32 item; int count; int demand; int weight; };
    std::array<Candidate, 3> candidates{{
        {Structure_ZoneResidential, residential, resDemand, 3},
        {Structure_ZoneIndustrial, industrial, indDemand, 1},
        {Structure_ZoneCommercial, commercial, comDemand, 1}
    }};
    std::stable_sort(candidates.begin(), candidates.end(), [bootstrap](const auto& a, const auto& b) {
        const bool missingA = bootstrap && a.count == 0;
        const bool missingB = bootstrap && b.count == 0;
        if(missingA != missingB) return missingA;
        const int demandA = normalizedZoneDemand(a.item, a.demand);
        const int demandB = normalizedZoneDemand(b.item, b.demand);
        if(demandA != demandB) return demandA > demandB;
        return a.count * b.weight < b.count * a.weight;
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

// Divide the remaining map spice between active houses before investing.
inline int desiredSpiceHarvesters(int spice, int competitors, int limit) {
    return std::clamp(std::max(0, spice) / std::max(1, competitors) / 3000, 0, std::max(0, limit));
}
// A refinery without workers is sunk capital. Once a player has a meaningful
// share of the field, maintain enough harvesters to keep each refinery busy
// while preserving the map-share cap above.
inline int refineryThroughputHarvesterTarget(int spiceShare, int spiceTarget,
                                             int refineries, int limit) {
    if (spiceShare < 3000) return std::clamp(spiceTarget, 0, std::max(0, limit));
    const int throughputTarget = (std::max(0, refineries) * 3 + 1) / 2;
    return std::clamp(std::max(spiceTarget, throughputTarget), 0, std::max(0, limit));
}
inline int desiredSpiceRefineries(int harvesterTarget, int harvestersIncludingQueued) {
    return std::max(1, (std::min(harvesterTarget, harvestersIncludingQueued + 3) + 2) / 3);
}
// Refineries supply their own initial worker, unlike later throughput expansion.
// Establish up to three income lanes before spending on vehicle prerequisites.
inline int openingSpiceRefineries(int sustainableHarvesters) {
    return std::clamp(sustainableHarvesters, 1, 3);
}
inline int fundedSpiceHarvesters(int sustainable, int refineries) {
    return std::max(0, std::min(sustainable, std::max(0, refineries) * 3));
}
// Compare the capacity actually needed, not the nominal cost per power of
// a reactor that would sit mostly idle. A shortage of legal wind sites can
// justify paying for compact generation. Cash for other work stays protected.
inline bool preferNuclearPower(int need, int windOutput, int windPrice, int nuclearPrice,
                              int availableWindSites, int cash, int protectedCash) {
    if (need <= 0 || windOutput <= 0 || cash < nuclearPrice + std::max(0,protectedCash)) return false;
    const int windCount = (need + windOutput - 1) / windOutput;
    return availableWindSites < windCount || int64_t(windCount) * windPrice > nuclearPrice;
}
inline int projectedPowerGrowth(int previous, int current, unsigned elapsed, unsigned horizon) {
    if (!elapsed || current <= previous) return 0;
    return int(std::min<int64_t>(std::max(0,current), int64_t(current-previous)*horizon/elapsed));
}
// Scale reserve with demand and cover one generator loss where practical.
// Limit the single-generator allowance to half of demand for small bases.
inline int cityPowerReserve(int required, int largestGenerator) {
    required = std::max(0, required);
    const int growthReserve = required / 4 + (required % 4 != 0);
    return std::max(growthReserve, std::min(std::max(0, largestGenerator), required / 2));
}

inline int desiredHeavyFactories(bool citySim, int creditsPerSecond, int credits,
                                 int actual = 0, int busy = 0, int recentLosses = 0) {
    // Income sustains normal expansion; a large unspent treasury can fund
    // additional capacity. Bound expansion so a busy queue cannot grow it forever.
    const int incomeTarget = 1 + std::max(0, creditsPerSecond) / 50;
    // Keep working capital for units/power, then add a city production lane
    // per 2500 surplus credits; leave 2000 available for units and power.
    const int cashTarget = 1 + (citySim ? spendableCredits(credits, 2000) / 2500
                                      : std::max(0, credits) / 4000);
    int target = citySim ? std::max(incomeTarget, cashTarget) : cashTarget;
    // With working capital, replace recent losses and add two production lanes
    // when at least 75% of existing factories are busy. Queued factories are
    // compared against this target by the caller; they do not raise it again.
    if (credits >= 8000 && (recentLosses > 0 || (actual > 0 && busy * 4 >= actual * 3)))
        target = std::max(target, actual + std::clamp(recentLosses + 2, 2, 4));
    return std::clamp(target, 1, 24);
}

// Expand production for a funded backlog, counting queued factories as spare
// capacity. Busy queues alone say nothing about demand for that factory's units.
inline bool needsProductionLane(int actual, int committed, int busy, int deficit,
                                int credits, int reserve, int factoryPrice) {
    return actual > 0 && committed == actual && busy * 4 >= actual * 3
        && deficit >= factoryPrice && credits >= reserve + factoryPrice + 1000;
}

inline bool allAirFactoriesBuildingOrnithopters(int actual, int committed, int producing) {
    return actual > 0 && committed == actual && producing == actual;
}

inline bool needsKiting(int distance, int weaponRange, bool easy, bool groundTarget) {
    return !easy && groundTarget && weaponRange > 2 && distance <= weaponRange - 2;
}

inline bool isLightRaider(Uint32 item) {
    return item == Unit_Trike || item == Unit_RaiderTrike || item == Unit_Quad
        || item == Unit_RocketTrike || item == Unit_SonicTrike;
}

inline bool isArmoredTank(Uint32 item) {
    return item == Unit_Tank || item == Unit_SiegeTank || item == Unit_Devastator
        || item == Unit_SonicTank || item == Unit_FlameTank || item == Unit_EliteSiegeTank
        || item == Unit_ChemicalSiegeTank;
}

inline bool isLightRaiderPreferredTarget(Uint32 item) {
    return item == Unit_Launcher || item == Unit_EliteLauncher
        || item == Unit_Harvester || item == Unit_RebelHarvester
        || item == Unit_Trike || item == Unit_RaiderTrike || item == Unit_Quad
        || item == Unit_RocketTrike || item == Unit_SonicTrike
        || item == Unit_Infantry || item == Unit_Soldier || item == Unit_Trooper
        || item == Unit_Troopers;
}

inline int repairYardCap(int heavyFactories) {
    // Repair is support capacity: at most one yard per two heavy factories.
    return std::clamp((heavyFactories + 1) / 2, 1, 4);
}

inline bool needsExtraRepairYard(int yardsIncludingQueued, int busyYards,
                                int heavyFactories, int militaryValue) {
    // A queued yard counts as spare capacity, preventing multiple CYs from
    // expanding repair simultaneously. The first yard has its own tech rule.
    return yardsIncludingQueued > 0
        && busyYards >= yardsIncludingQueued
        && yardsIncludingQueued < repairYardCap(heavyFactories)
        && militaryValue > yardsIncludingQueued * 6000;
}

} // namespace QuantBotBuildPolicy
#endif
