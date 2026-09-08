#include <players/CombatReward.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <players/UnitMixPolicy.h>
#include <dunecity/PowerRules.h>
#include <dunecity/VanillaEconomy.h>
#include <catch2/catch_test_macros.hpp>
#include <players/QuantBotBuildPolicy.h>

using namespace QuantBotBuildPolicy;

TEST_CASE("QuantBot respects the single-palace option even in a large city", "[quantbot][production]") {
    REQUIRE(palaceTarget(true, true, 60000) == 1);
    REQUIRE(palaceTarget(false, false, 60000) == 1);
    REQUIRE(palaceTarget(false, true, 29999) == 1);
    REQUIRE(palaceTarget(false, true, 30000) == 2);
    REQUIRE(palaceTarget(false, true, 60000) == 3);
}

TEST_CASE("QuantBot strategic savings leave excess funds available for tanks", "[quantbot][production]") {
    REQUIRE(spendableCredits(59044, 2000) == 57044);
    REQUIRE(spendableCredits(1000, 2000) == 0);
    REQUIRE(spendableCredits(1000, 0) == 1000);
    REQUIRE(spendableCredits(-100, 2000) == 0);
}

TEST_CASE("QuantBot repairs an over-residential economy even at saturated demand", "[quantbot][city]") {
    const auto zones = rankZones(60, 4, 9, 2000, 1500, 1500, false);
    REQUIRE(zones[0] == Structure_ZoneCommercial);
    REQUIRE(zones[1] == Structure_ZoneIndustrial);
    REQUIRE(zones[2] == Structure_ZoneResidential);
    // The second yard sees the first yard's accepted order in its counts.
    const auto first = rankZones(30, 10, 10, 2000, 1500, 1500, false);
    REQUIRE(first[0] == Structure_ZoneResidential);
    const auto second = rankZones(33, 10, 10, 2000, 1500, 1500, false);
    REQUIRE(second[0] == Structure_ZoneIndustrial);
}

TEST_CASE("QuantBot does not build residential as a fallback against demand", "[quantbot][city]") {
    const auto zones = rankZones(60, 4, 9, -286, 1500, 1500, false);
    REQUIRE(zones[0] == Structure_ZoneCommercial);
    REQUIRE(zones[1] == Structure_ZoneIndustrial);
    REQUIRE(zones[2] == NONE_ID);
    for(auto item : rankZones(3, 1, 1, 0, -100, -100, false)) REQUIRE(item == NONE_ID);
}

TEST_CASE("QuantBot seeds missing jobs before relying on positive demand", "[quantbot][city]") {
    REQUIRE(rankZones(3, 0, 0, 2000, -1500, -1500, true)[0] == Structure_ZoneIndustrial);
    REQUIRE(rankZones(3, 0, 1, 2000, -1500, -1500, true)[0] == Structure_ZoneCommercial);
}

TEST_CASE("QuantBot expands tank production with surplus cash without unbounded factory growth", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true, 50, 59044) == 23);
    REQUIRE(desiredHeavyFactories(true, 150, 3000) == 4);
    REQUIRE(desiredHeavyFactories(true, 0, 2000) == 1);
    REQUIRE(desiredHeavyFactories(true, 10000, 1000000) == 24);
    REQUIRE(desiredHeavyFactories(false, 0, 12000) == 4);
    REQUIRE(desiredHeavyFactories(false, 0, 1000000) == 24);
    REQUIRE(desiredHeavyFactories(true, 0, 59499) == 23);
    REQUIRE(desiredHeavyFactories(true, 0, 59500) == 24);
    // Live Harkonnen treasury must expand beyond the old eight-factory cap.
    REQUIRE(desiredHeavyFactories(true, 0, 149408) == 24);
}

TEST_CASE("QuantBot converts the observed city cash surplus into troop capacity", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true, 0, 4499) == 1);
    REQUIRE(desiredHeavyFactories(true, 0, 4500) == 2);
    REQUIRE(desiredHeavyFactories(true, 0, 11926) == 4);
    REQUIRE(desiredHeavyFactories(true, 0, 13873) == 5);
    REQUIRE(desiredHeavyFactories(true, 0, 17922) == 7);
    REQUIRE(desiredHeavyFactories(true, 0, -100) == 1);
}

TEST_CASE("QuantBot repair expansion follows load and heavy production capacity", "[quantbot][production]") {
    // Current-game regressions: four/six yards with just two factories.
    REQUIRE_FALSE(needsExtraRepairYard(3, 3, 2, 19520));
    REQUIRE_FALSE(needsExtraRepairYard(5, 5, 2, 30200));
    REQUIRE_FALSE(needsExtraRepairYard(1, 1, 2, 19520));
    // Productive army with a busy repair yard can add a second.
    REQUIRE(needsExtraRepairYard(1, 1, 3, 19520));
    REQUIRE_FALSE(needsExtraRepairYard(1, 0, 3, 19520));
    // One busy yard plus another queued must not trigger a third order.
    REQUIRE_FALSE(needsExtraRepairYard(2, 1, 6, 30000));
    REQUIRE(needsExtraRepairYard(2, 2, 6, 30000));
    REQUIRE_FALSE(needsExtraRepairYard(2, 2, 6, 12000));
    REQUIRE_FALSE(needsExtraRepairYard(0, 0, 0, 30000));
    REQUIRE_FALSE(needsExtraRepairYard(4, 4, 20, 80000));
}

TEST_CASE("QuantBot prioritizes the live jobs demand seen in the current game", "[quantbot][city]") {
    const auto lowResidential = rankZones(191, 56, 65, 319, 1500, 1500, false);
    REQUIRE(lowResidential[0] == Structure_ZoneCommercial);
    REQUIRE(lowResidential[1] == Structure_ZoneIndustrial);
    REQUIRE(lowResidential[2] == Structure_ZoneResidential);
    REQUIRE(rankZones(179, 60, 61, 708, 1500, 1500, false)[0] == Structure_ZoneCommercial);
    REQUIRE(rankZones(219, 72, 73, 800, 1500, 1500, false)[0] == Structure_ZoneCommercial);
    // Normalize different valve ranges: R1000/2000 < C1000/1500.
    REQUIRE(rankZones(0, 10, 10, 1000, 1000, 0, false)[0] == Structure_ZoneCommercial);
    // When jobs demand falls, residential can win again.
    REQUIRE(rankZones(191, 56, 65, 1600, 500, 400, false)[0] == Structure_ZoneResidential);
}

TEST_CASE("QuantBot invests in spice without counting the whole map for every house", "[quantbot][economy]") {
    REQUIRE(desiredSpiceHarvesters(600000, 4, 40) == 40);
    REQUIRE(desiredSpiceHarvesters(60000, 4, 40) == 5);
    REQUIRE(desiredSpiceHarvesters(0, 4, 40) == 0);
    REQUIRE(desiredSpiceHarvesters(600000, 4, 10) == 10);
    REQUIRE(desiredSpiceRefineries(40, 21) == 8);
    REQUIRE(desiredSpiceRefineries(5, 21) == 2);
}

TEST_CASE("City power reserve covers growth and generator losses without doubling small bases", "[quantbot][power]") {
    REQUIRE(cityPowerReserve(0, 1000) == 0);
    REQUIRE(cityPowerReserve(100, 0) == 25);
    REQUIRE(cityPowerReserve(101, 0) == 26);
    REQUIRE(cityPowerReserve(1000, 1000) == 500);
    REQUIRE(cityPowerReserve(2000, 1000) == 1000);
    REQUIRE(cityPowerReserve(6000, 1000) == 1500);
    REQUIRE(cityPowerReserve(14000, 1000) == 3500);
    REQUIRE(cityPowerReserve(6000, 2000) == 2000);
    REQUIRE(cityPowerReserve(-100, 1000) == 0);
}

TEST_CASE("Funded Harkonnen develops its city while expanding heavy production", "[quantbot][city][production]") {
    // Live match: 26,298 credits, 15/5/9 zones, all valves saturated, idle CY.
    REQUIRE(desiredHeavyFactories(true, 0, 23000) == 9);
    REQUIRE(desiredHeavyFactories(true, 0, 26298) == 10);
    // Consecutive accepted orders balance city types under equal normalized demand.
    int r = 15, c = 5, i = 9;
    int builtR = 0, builtC = 0, builtI = 0;
    for (int n = 0; n < 50; ++n) {
        const auto zone = rankZones(r, c, i, 2000, 1500, 1500, false)[0];
        REQUIRE(zone != NONE_ID);
        if (zone == Structure_ZoneResidential) { ++r; ++builtR; }
        if (zone == Structure_ZoneCommercial) { ++c; ++builtC; }
        if (zone == Structure_ZoneIndustrial) { ++i; ++builtI; }
    }
    REQUIRE(builtR > 0);
    REQUIRE(builtC > 0);
    REQUIRE(builtI > 0);
    REQUIRE(std::abs(c - i) <= 1);
    REQUIRE(std::abs(r - 3 * c) <= 3);
}

TEST_CASE("Factory expansion reacts to busy production and recent losses without spending the reserve", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true,0,8000,8,6,0) == 10);
    REQUIRE(desiredHeavyFactories(true,0,8000,8,5,0) == 3);
    REQUIRE(desiredHeavyFactories(true,0,8000,8,0,2) == 12);
    REQUIRE(desiredHeavyFactories(true,0,7999,8,8,4) == 3);
    REQUIRE(desiredHeavyFactories(true,0,10000,23,23,4) == 24);
    REQUIRE(desiredHeavyFactories(false,0,8000,8,6,0) == 10);
}

TEST_CASE("Refinery throughput raises the worker target only for a viable spice field", "[quantbot][economy]") {
    REQUIRE(refineryThroughputHarvesterTarget(2000, 4, 10, 40) == 4);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 6, 8, 40) == 12);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 18, 8, 40) == 18);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 6, 40, 20) == 20);
}

TEST_CASE("Attack commitment is reproducible without mutable random state", "[quantbot][attack]") {
    std::array<bool, 101> seen{};
    for (Uint32 cycle = 0; cycle < 10000; ++cycle) {
        const int firstPeer = attackCommitmentPercent(1859147109u, cycle, 2, 3);
        // Other decisions cannot perturb this peer's result or a replay/load.
        attackCommitmentPercent(5, cycle + 1, 6, 7);
        REQUIRE(firstPeer == attackCommitmentPercent(1859147109u, cycle, 2, 3));
        REQUIRE(firstPeer >= 20);
        REQUIRE(firstPeer <= 100);
        seen[firstPeer] = true;
    }
    REQUIRE(seen[20]);
    REQUIRE(seen[100]);
}

TEST_CASE("Main harvester strikes are deterministic and use the entire available force", "[quantbot][attack][multiplayer]") {
    bool sawStrikeWindow = false;
    bool sawHuntWindow = false;
    for (Uint32 cycle = 0; cycle < 10000; ++cycle) {
        const bool strike = shouldUseMainHarvesterStrike(1859147109u, cycle, 2, 3);
        REQUIRE(strike == shouldUseMainHarvesterStrike(1859147109u, cycle, 2, 3));
        sawStrikeWindow |= strike;
        sawHuntWindow |= !strike;
    }
    REQUIRE(sawStrikeWindow);
    REQUIRE(sawHuntWindow);
    REQUIRE(attackForceBudget(10000, 40, false) == 4000);
    REQUIRE(attackForceBudget(10000, 40, true) == 10000);
}

TEST_CASE("Opportunistic reactor strikes require a nearby wing and clear approach", "[quantbot][attack]") {
    REQUIRE(easyReactorStrike(3, 6, 0));
    REQUIRE_FALSE(easyReactorStrike(2, 6, 0));
    REQUIRE_FALSE(easyReactorStrike(3, 6, 1));
    REQUIRE_FALSE(easyReactorStrike(0, 0, 0));
}

TEST_CASE("Wealth funds city yards regardless of zone demand while preserving working cash", "[quantbot][city]") {
    REQUIRE(cityConstructionYardTarget(1500, 0, 0, 0) == 1);
    REQUIRE(cityConstructionYardTarget(1500, 2000, 1500, 1500) == 4);
    REQUIRE(cityConstructionYardTarget(20000, 2000, 1500, 0) == 5);
    REQUIRE(cityConstructionYardTarget(50000, 2000, 1500, 1500) == 6);
    REQUIRE(cityConstructionYardTarget(20000, 0, 0, 0) == 5);
    REQUIRE(cityConstructionYardTarget(50000, 0, 0, 1500) == 6);
    REQUIRE(cityConstructionYardTarget(100000, -1000, -500, -500) == 8);
    REQUIRE(cityConstructionYardTarget(1000000, 2000, 1500, 1500) == 8);
    REQUIRE(canFundCityYard(2000, 1000, 1, 4, 0));
    REQUIRE_FALSE(canFundCityYard(1999, 1000, 1, 4, 0));
    REQUIRE_FALSE(canFundCityYard(10000, 1000, 4, 4, 1000));
    REQUIRE_FALSE(canFundCityYard(4000, 1500, 2, 6, 3000));
    REQUIRE(canFundCityYard(4500, 1500, 2, 6, 3000));
    REQUIRE_FALSE(canFundCityYard(10000, 0, 1, 4, 1000));
    // Existing yards plus all queued/live MCVs satisfy capacity: no duplicate orders.
    REQUIRE_FALSE(canFundCityYard(100000, 1500, 2 + 6, 8, 3000));
}

TEST_CASE("Small armies retain a base defender while large armies reserve ten percent", "[quantbot][defence]") {
    REQUIRE(baseDefenderTarget(0) == 0);
    REQUIRE(baseDefenderTarget(1) == 1);
    REQUIRE(baseDefenderTarget(9) == 1);
    REQUIRE(baseDefenderTarget(20) == 2);
    REQUIRE(baseDefenderTarget(100) == 10);
}

TEST_CASE("Vanilla ignores power but city and other mods retain their rules", "[vanilla][power]") {
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false, "vanilla"));
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false, ""));
    REQUIRE(DuneCity::powerRulesEnabled(true, "vanilla"));
    REQUIRE(DuneCity::powerRulesEnabled(true, "dunecity"));
    REQUIRE(DuneCity::powerRulesEnabled(false, "Tornie"));
}
TEST_CASE("Spice-rich vanilla funds a larger fleet and preserves low-cash factory limits", "[vanilla][economy]") {
    REQUIRE(DuneCity::vanillaHarvesterCapacity(40) == 60);
    REQUIRE(DuneCity::vanillaHarvesterCapacity(0) == 0);
    REQUIRE(DuneCity::vanillaHarvesterTarget(640916, 5, 60) == 60);
    REQUIRE(DuneCity::vanillaHarvesterTarget(200000, 5, 60) == 20);
    REQUIRE(DuneCity::vanillaHarvesterTarget(640916, 5, 10) == 10);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 8000) == 2);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 40, 8000) == 13);
    REQUIRE(desiredSpiceRefineries(60, 42) == 15); // build the next refinery before the fleet stalls at 42
    REQUIRE(desiredSpiceRefineries(60, 60) == 20);
}

TEST_CASE("Vanilla cash reserves unlock yard expansion before the harvester target", "[quantbot][vanilla]") {
    REQUIRE(DuneCity::vanillaYardTarget(100000, 1) == 8);
    REQUIRE(DuneCity::vanillaYardTarget(67782, 6) == 7);
    REQUIRE(DuneCity::vanillaYardTarget(50000, 16) == 6);
    REQUIRE(DuneCity::vanillaYardTarget(100000, 60) == 8);
    REQUIRE(DuneCity::vanillaYardTarget(1000, 60) == 1);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 3) == 24000);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 2) == 28000);
    REQUIRE(DuneCity::vanillaAttackThreshold(8000, 3) == 8000);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 1) == 32000);
}

TEST_CASE("Vanilla Brutal attack commitment remains reproducible and favours larger waves", "[quantbot][multiplayer]") {
    int originalTotal=0, brutalTotal=0;
    for (Uint32 cycle=0; cycle<10000; cycle+=17) {
        const int original=attackCommitmentPercent(753675852u,cycle,1,18);
        const int brutal=difficultyAttackCommitment(753675852u,cycle,1,18,3);
        REQUIRE(brutal == difficultyAttackCommitment(753675852u,cycle,1,18,3));
        REQUIRE(brutal >= original);
        REQUIRE(brutal >= 20);
        REQUIRE(brutal <= 100);
        REQUIRE(difficultyAttackCommitment(753675852u,cycle,1,18,1) == original);
        originalTotal+=original; brutalTotal+=brutal;
    }
    REQUIRE(brutalTotal > originalTotal);
}

TEST_CASE("Vanilla prioritises parallel affordable MCVs while preserving recovery cash", "[quantbot][vanilla]") {
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 0, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 1, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 6, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 7, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 6, 2, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 8, 0, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(1500, 40, 0, 0, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(5000, 2, 1, 0, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(10000, 2, 1, 0, 900));
}

TEST_CASE("Vanilla learning preserves ground production when aircraft dominate damage scores", "[quantbot][vanilla]") {
    const auto mix = DuneCity::balancedVanillaUnitMix({71,721,450,911,7847},{500,1000,3500,3500,1500});
    REQUIRE(mix[4] == 2500);
    REQUIRE(mix[2] >= 2500);
    REQUIRE(mix[3] >= 2500);
    int total=0;
    for (const int weight : mix) { REQUIRE(weight >= 0); total+=weight; }
    REQUIRE(total == 10000);
    const auto stable = DuneCity::balancedVanillaUnitMix({500,1000,3500,3500,1500},{500,1000,3500,3500,1500});
    REQUIRE(stable == std::array<int,5>{500,1000,3500,3500,1500});
    const auto empty = DuneCity::balancedVanillaUnitMix({0,0,0,0,0},{0,0,0,0,0});
    REQUIRE(empty == std::array<int,5>{2500,2500,2500,2500,0});
}

TEST_CASE("Wealthy vanilla expands factories without waiting for a full harvester fleet", "[quantbot][vanilla]") {
    // Recorded six-minute failure: 94k cash, sixteen harvesters, one factory.
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 16, 94000) == 22);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 2, 100000) == 23);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 50000) == 11);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 10000) == 2);
    REQUIRE(DuneCity::vanillaFactoryTarget(4, 60, 100000) == 4);
    REQUIRE(DuneCity::vanillaFactoryTarget(99, 99, 1000000) == 24);
    REQUIRE(DuneCity::prioritizeVanillaFactory(94000, 1, 1, 22));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 2, 1, 22)); // advance tech next
    REQUIRE(DuneCity::prioritizeVanillaFactory(94000, 2, 2, 22)); // a new yard adds production lanes
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 4, 2, 22)); // queued factories count
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(19000, 1, 2, 3));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 2, 2, 2));
}

TEST_CASE("Parallel MCV orders account for earlier factories and subsequent deployment", "[quantbot][vanilla]") {
    int credits = 98000, pending = 0;
    for (int factory=0; factory<12; ++factory) {
        if (DuneCity::prioritizeVanillaMcv(credits, 2, 1, pending, 900)) {
            ++pending;
            credits -= 900;
        }
    }
    REQUIRE(pending == 7);
    REQUIRE(DuneCity::vanillaMcvShortfall(credits, 2, 1, pending) == 0);
    // Deployment converts one pending MCV to a yard, without creating extra demand.
    REQUIRE(DuneCity::vanillaMcvShortfall(credits, 2, 2, pending-1) == 0);
    REQUIRE(DuneCity::vanillaMcvShortfall(50000, 2, 1, 2) == 3);
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(10000, 2, 1, 1, 900));
}

TEST_CASE("Light vehicle combat returns compete with heavy units by replacement cost", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    Weights scores{performanceScore(3000,600,300),performanceScore(3000,1200,600),
        performanceScore(3000,900,450),performanceScore(3000,1400,700),0,
        performanceScore(3000,300,150),0,performanceScore(3000,400,200)};
    // Provide combat evidence: without losses the opening mix is intentionally
    // retained, so performance must not steer production yet.
    const auto successful = allocate(scores,baseline,true,true,10000,10000);
    REQUIRE(successful[5] > successful[0]);
    REQUIRE(successful[5] > successful[7]);
    REQUIRE(successful[5]+successful[7] > 1200);
    scores[5] = performanceScore(3000,3000,150);
    const auto costly = allocate(scores,baseline,true,true,10000,10000);
    REQUIRE(costly[5] < successful[5]);
    REQUIRE(costly[7] > costly[5]);
    REQUIRE(costly[6] == 0);
}
TEST_CASE("Eight-type mix keeps defaults before combat and handles zero damage safely", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    const auto expected = normalize(baseline);
    REQUIRE(allocate({},baseline,true,true) == expected);
    REQUIRE(allocate({0,0,0,0,0,100,0,0},baseline,false,true) == expected);
    REQUIRE(performanceScore(-100,0,0) == 0);
    REQUIRE(performanceScore(500,0,0) == 500000000);
    REQUIRE(performanceScore(2000000000,2000000000,150) > 0);
}
TEST_CASE("Adaptive eight-type mix preserves limits and deterministic peer results", "[quantbot][unitmix][multiplayer]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    for (bool vanilla : {false,true}) {
        for (size_t strongest=0; strongest<8; ++strongest) {
            Weights scores{}; scores[strongest]=10000000;
            const auto first=allocate(scores,baseline,true,vanilla);
            allocate({4,3,2,1,0,4,3,2},baseline,true,vanilla);
            REQUIRE(allocate(scores,baseline,true,vanilla) == first);
            int total=0;
            for (int share : first) { REQUIRE(share>=0); REQUIRE(share<=8000); total+=share; }
            REQUIRE(total == 10000);
            if (vanilla) REQUIRE(first[4]<=2500);
        }
    }
}
TEST_CASE("Vanilla unit mix replaces its opening prior as combat evidence accumulates", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{5000,5000,0,0,0,0,0,0};
    const Weights scores{1000000,0,0,0,0,0,0,0};
    const auto early = allocate(scores, baseline, true, true, 0, 10000);
    const auto battleTested = allocate(scores, baseline, true, true, 90000, 10000);
    REQUIRE(evidenceConfidenceBps(0, 10000) == 0);
    REQUIRE(evidenceConfidenceBps(90000, 10000) == 9000);
    REQUIRE(early[0] == 5000);
    // The performance signal is 90%, then the universal 80% single-unit cap
    // keeps the mix from collapsing onto one type.
    REQUIRE(battleTested[0] == 8000);
    REQUIRE(battleTested[1] == 2000);
}
TEST_CASE("Light vehicle selection fills value deficits and counts queued units", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    REQUIRE(deficit(1000,10000,1,150) > deficit(500,10000,1,200));
    REQUIRE(deficit(1000,10000,7,150) < 0);
    REQUIRE(deficit(0,10000,0,150) == 0);
}

TEST_CASE("Opening light shares shrink with tech and unavailable units receive no allocation", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights configured{500,1000,3500,3500,1500,0,0,0};
    const std::array<bool,8> full{true,true,true,true,true,true,false,true};
    for (int tech=4; tech<=8; ++tech) {
        const auto mix = openingMix(tech,configured,full);
        REQUIRE(mix[5]+mix[6]+mix[7] == (tech>=7 ? 400 : tech>=5 ? 800 : 1500));
        REQUIRE(mix[6] == 0);
        REQUIRE(mix[7] > mix[5]);
        int total=0; for (int value : mix) total+=value;
        REQUIRE(total == 10000);
    }
    const auto tankOnly = openingMix(4,configured,{true,false,false,false,false,true,false,true});
    REQUIRE(tankOnly[0] == 8500);
    REQUIRE(tankOnly[3] == 0);
    REQUIRE(tankOnly[4] == 0);
    const auto lightsOnly = openingMix(3,configured,{false,false,false,false,false,true,false,true});
    REQUIRE(lightsOnly[5]+lightsOnly[7] == 10000);
    const auto noFactory = openingMix(8,configured,{});
    REQUIRE(noFactory == Mix{});
}
TEST_CASE("Opening allocation follows upgrades and missing producer recovery", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights configured{500,1000,3500,3500,1500,0,0,0};
    const auto before = openingMix(8,configured,{true,false,false,false,false,true,false,true});
    const auto after = openingMix(8,configured,{true,true,true,true,true,true,false,true});
    REQUIRE(before[0] == 9600);
    REQUIRE(after[0] < before[0]);
    REQUIRE(after[3] > 0);
    REQUIRE(after[4] > 0);
    REQUIRE(after[5]+after[7] == 400);
    REQUIRE(openingMix(8,{}, {true,false,false,false,false,false,false,false})[0] == 10000);
}

TEST_CASE("Damage reward values actual HP removed and gives the killer twenty percent", "[quantbot][reward]") {
    const auto partial = CombatReward::hit(600,300000,300000,200000,true,true);
    REQUIRE(partial.damageMilli == 200000);
    REQUIRE(partial.killBonusMilli == 0);
    const auto kill = CombatReward::hit(600,300000,20000,0,true,true);
    REQUIRE(kill.damageMilli == 40000);
    REQUIRE(kill.hpRemovedMilli == 20000);
    REQUIRE(kill.killBonusMilli == 120000);
    REQUIRE(kill.total() == 160000);
    REQUIRE(kill.kills == 1);
    REQUIRE(CombatReward::hit(600,300000,0,0,true,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,0,false,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,30000,true,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,0,true,false).killBonusMilli == 0);
    REQUIRE(CombatReward::hit(600,300000,1000,0,true,true).damageMilli == 2000);
}
TEST_CASE("Killing blow raises unit efficiency and reward counters survive save load", "[quantbot][reward][save-compat]") {
    const auto reward = CombatReward::hit(600,300000,300000,0,true,true);
    REQUIRE(reward.total() == 720000);
    REQUIRE(UnitMixPolicy::performanceScore(reward.total(),300000,300000)
        > UnitMixPolicy::performanceScore(reward.damageMilli,300000,300000));
    OMemoryStream out; reward.save(out); out.writeUint32(0x12345678);
    IMemoryStream in(out.getData(),static_cast<int>(out.getDataLength()));
    CombatReward::Totals loaded; loaded.load(in);
    REQUIRE(loaded.total() == reward.total());
    REQUIRE(loaded.damageMilli == reward.damageMilli);
    REQUIRE(loaded.killBonusMilli == reward.killBonusMilli);
    REQUIRE(loaded.kills == 1);
    REQUIRE(loaded.hits == 1);
    REQUIRE(loaded.hpRemovedMilli == 300000);
    REQUIRE(in.readUint32() == 0x12345678);
}

TEST_CASE("Factory priorities require funded demand and no spare lane", "[quantbot][production]") {
    REQUIRE(needsProductionLane(2,2,2,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,3,2,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,1,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,2,0,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,2,4000,3000,2000,600));
    REQUIRE(needsProductionLane(4,4,4,4000,6000,2000,500)); // no arbitrary air cap
}
TEST_CASE("Launcher spacing triggers inside its safe range", "[quantbot][combat]") {
    REQUIRE(needsKiting(7, 9, false, true));
    REQUIRE(needsKiting(1, 9, false, true));
    REQUIRE_FALSE(needsKiting(8, 9, false, true));
    REQUIRE_FALSE(needsKiting(1, 9, true, true));
    REQUIRE_FALSE(needsKiting(1, 9, false, false));
}

TEST_CASE("Light raiders evade tanks and prefer vulnerable mobile prey", "[quantbot][combat]") {
    REQUIRE(isLightRaider(Unit_Trike));
    REQUIRE(isLightRaider(Unit_Quad));
    REQUIRE_FALSE(isLightRaider(Unit_Launcher));
    REQUIRE(isArmoredTank(Unit_Tank));
    REQUIRE(isArmoredTank(Unit_Devastator));
    REQUIRE(isArmoredTank(Unit_EliteSiegeTank));
    REQUIRE_FALSE(isArmoredTank(Unit_Launcher));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Launcher));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Harvester));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Trike));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Quad));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Troopers));
    REQUIRE_FALSE(isLightRaiderPreferredTarget(Unit_Tank));
    REQUIRE_FALSE(isLightRaiderPreferredTarget(Structure_Refinery));
}

TEST_CASE("Only ornithopter saturation justifies extra air production", "[quantbot][production]") {
    REQUIRE(allAirFactoriesBuildingOrnithopters(3,3,3));
    REQUIRE_FALSE(allAirFactoriesBuildingOrnithopters(3,3,2)); // one carryall, idle or held
    REQUIRE_FALSE(allAirFactoriesBuildingOrnithopters(3,4,3)); // incoming capacity
    REQUIRE_FALSE(allAirFactoriesBuildingOrnithopters(0,0,0));
}

TEST_CASE("Rocket turret power setting is independent of vanilla power bypass", "[quantbot][power]") {
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false,"vanilla"));
    REQUIRE(DuneCity::rocketTurretPowered(false,100,1000));
    REQUIRE_FALSE(DuneCity::rocketTurretPowered(true,100,1000));
    REQUIRE(DuneCity::rocketTurretPowered(true,1000,1000));
    REQUIRE(DuneCity::rocketTurretPowered(true,1100,1000));
}

TEST_CASE("Windtrap damage keeps output until destruction while city reactors scale", "[power]") {
    using DuneCity::generatorOutput;
    REQUIRE(generatorOutput(100, 100, 100, false) == 100);
    REQUIRE(generatorOutput(100, 1, 100, false) == 100);
    REQUIRE(generatorOutput(300, 25, 100, false) == 300);
    REQUIRE(generatorOutput(100, 0, 100, false) == 0);
    REQUIRE(generatorOutput(1000, 25, 100, true) == 250);
    REQUIRE(generatorOutput(1000, 25, 100, false) == 1000);
    REQUIRE(generatorOutput(1000, 0, 100, true) == 0);
    const int before = generatorOutput(100,25,100,false);
    REQUIRE(100 - before + generatorOutput(100,0,100,false) == 0);
    REQUIRE(0 - generatorOutput(100,0,100,false) == 0); // destructor after lethal damage
}

TEST_CASE("Main waves require both actual numbers and value", "[quantbot][attack]") {
    REQUIRE_FALSE(viableMainWave(1,600));
    REQUIRE_FALSE(viableMainWave(5,10000));
    REQUIRE_FALSE(viableMainWave(20,2999));
    REQUIRE(viableMainWave(6,3000));
}

TEST_CASE("Harvest anchors resist churn but leave danger and depleted fields", "[quantbot][rally]") {
    REQUIRE_FALSE(replaceHarvestAnchor(true,8,12,29999,30000));
    REQUIRE_FALSE(replaceHarvestAnchor(true,8,9,30000,30000));
    REQUIRE(replaceHarvestAnchor(true,8,10,30000,30000));
    REQUIRE(replaceHarvestAnchor(false,8,1,0,30000));
    REQUIRE(replaceHarvestAnchor(true,0,1,0,30000));
    REQUIRE_FALSE(replaceHarvestAnchor(true,9,11,90000,30000));
    REQUIRE(replaceHarvestAnchor(true,9,12,90000,30000));
}

TEST_CASE("Heavy allocation picks the largest funded deficit, not a fixed priority", "[quantbot][allocation]") {
    std::array<AllocationCandidate,3> c = {{{300,3000,1000,true},{600,600,5000,true},{450,450,4000,true}}};
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==1);
    REQUIRE(largestAffordableDeficit(c,10000,500,20000)==2); // Largest deficit cannot be afforded.
    c[1].available=false;
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==2);
    c[2].committedValue=10000; // Includes queued launchers; don't keep ordering them.
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==-1); // No tank fallback.
}

TEST_CASE("Heavy allocation bootstraps and respects cash and army limits", "[quantbot][allocation]") {
    std::array<AllocationCandidate,2> c = {{{300,0,5000,true},{600,0,5000,true}}};
    REQUIRE(largestAffordableDeficit(c,0,600,1000)==0); // Stable tie, no RNG.
    REQUIRE(largestAffordableDeficit(c,0,299,1000)==-1);
    REQUIRE(largestAffordableDeficit(c,0,1000,299)==-1);
    c[0].targetBps=0;
    REQUIRE(largestAffordableDeficit(c,0,600,1000)==1);
    REQUIRE(largestAffordableDeficit(c,800,1000,1000)==-1);
}

TEST_CASE("Balanced small armies still fill idle heavy-factory lanes", "[quantbot][allocation]") {
    // A damaged/queued force can have more committed category value than its
    // current military total, which makes the normal one-unit horizon look full.
    std::array<AllocationCandidate,2> c = {{{300,4500,5000,true},{600,4500,5000,true}}};
    REQUIRE(largestAffordableDeficit(c,8000,1000,80000) == -1);
    const int growthHorizon = expansionAllocationHorizon(8000,80000);
    REQUIRE(growthHorizon == 16000);
    REQUIRE(largestAffordableDeficit(c,8000,1000,80000,growthHorizon) == 0);
    REQUIRE(expansionAllocationHorizon(50000,80000) == 80000);
}

#include <players/GroundSquadPolicy.h>
TEST_CASE("Coordinated waves gather a large core and never launch an isolated packet", "[quantbot][squad]") {
    using namespace GroundSquadPolicy;
    REQUIRE(committedCount(200)==160);
    REQUIRE(assembly(30,160,160,false)==Assembly::Wait);
    REQUIRE(assembly(30,160,160,true)==Assembly::Abort);
    REQUIRE(assembly(135,160,160,false)==Assembly::Wait);
    REQUIRE(assembly(136,160,160,false)==Assembly::Launch);
    REQUIRE(assembly(112,160,160,true)==Assembly::Launch);
    REQUIRE(assembly(111,160,160,true)==Assembly::Abort);
    REQUIRE(assembly(50,50,160,true)==Assembly::Abort); // losses cannot redefine a tiny wave as ready
}
TEST_CASE("Front-runners wait for the army but do not stop fighting nearby enemies", "[quantbot][squad]") {
    REQUIRE(GroundSquadPolicy::waitForBody(10,20,10,false));
    REQUIRE_FALSE(GroundSquadPolicy::waitForBody(18,20,10,false));
    REQUIRE_FALSE(GroundSquadPolicy::waitForBody(10,20,10,true));
}
TEST_CASE("All factory classes fill the same funded live plus queued army plan", "[quantbot][production]") {
    // Heavy shares are already filled. The remaining money must fund light/air shares
    // against 100k, not fractions of the existing 80k army.
    const int target=fundedArmyTarget(80000,100000,999999);
    REQUIRE(target==100000);
    const std::array<AllocationCandidate,2> lightAir={{{300,1500,400,true},{900,3600,1000,true}}};
    REQUIRE(fundedDeficit(lightAir,80000,999999,100000,target)==1);
    const std::array<AllocationCandidate,2> heavy={{{450,15000,1500,true},{450,44000,4400,true}}};
    REQUIRE(fundedDeficit(heavy,80000,999999,100000,target)==-1);
    REQUIRE(fundedArmyTarget(80000,100000,1200)==81200);
    REQUIRE(fundedDeficit(lightAir,99900,999999,100000,target)==-1); // queued military consumes the cap
    REQUIRE_FALSE(militaryItem(Unit_Carryall));
    REQUIRE_FALSE(militaryItem(Unit_Harvester));
    REQUIRE_FALSE(militaryItem(Unit_MCV));
    REQUIRE(militaryItem(Unit_Ornithopter));
}
TEST_CASE("Exploration fades independently for each unit's evidence", "[quantbot][allocation]") {
    using namespace UnitMixPolicy;
    Weights scores{},rewards{},losses{},prices{};
    std::array<bool,8> available{}; available[0]=available[1]=available[3]=true;
    scores[0]=2000000; rewards[0]=100000000; losses[0]=50000000;
    scores[1]=100000; rewards[1]=100000000; losses[1]=1000000000;
    prices.fill(700000);
    const auto first=exploredScores(scores,rewards,losses,prices,available);
    REQUIRE(first[3]>0); // never tried: eligible for a real combat sample
    REQUIRE(first[1]<first[3]); // many losses: no named-unit floor rescuing poor performance
    REQUIRE(first[4]==0); // unavailable aircraft get no speculative allocation
    scores[3]=100000; rewards[3]=10000000; losses[3]=100000000;
    const auto tested=exploredScores(scores,rewards,losses,prices,available);
    REQUIRE(tested[3]<first[3]);
}

#include <players/TacticalSafetyPolicy.h>
TEST_CASE("Reactor spacing protects production and expensive facilities while allowing RCI", "[quantbot][placement]") {
    using TacticalSafetyPolicy::protectedReactorNeighbour;
    for (const int item:{Structure_ConstructionYard,Structure_HeavyFactory,Structure_HighTechFactory,
            Structure_Refinery,Structure_IX,Structure_StarPort,Structure_Palace,Structure_NuclearPlant})
        REQUIRE(protectedReactorNeighbour(item));
    for (const int item:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial})
        REQUIRE_FALSE(protectedReactorNeighbour(item));
}

TEST_CASE("Recent unit evidence fades and resumes identically after save and load", "[quantbot][allocation][save-compat]") {
    UnitMixPolicy::PerformanceWindow original;
    UnitMixPolicy::Weights reward{},loss{};
    reward[4]=800000; loss[4]=1600000;
    original.update(100,30,reward,loss);
    original.update(130,30,reward,loss);
    REQUIRE(original.reward[4]==700000);
    REQUIRE(original.loss[4]==1400000);
    OMemoryStream output; original.save(output);
    IMemoryStream input(output.getData(),output.getDataLength());
    UnitMixPolicy::PerformanceWindow restored; restored.load(input);
    reward[4]+=500000; loss[4]+=100000;
    original.update(160,30,reward,loss); restored.update(160,30,reward,loss);
    REQUIRE(restored.reward==original.reward);
    REQUIRE(restored.loss==original.loss);
    REQUIRE(restored.sampled==original.sampled);
    REQUIRE(original.reward[4]==1112500);
    REQUIRE(original.loss[4]==1325000);
}
