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
    REQUIRE(desiredHeavyFactories(true, 50, 59044) == 6);
    REQUIRE(desiredHeavyFactories(true, 150, 3000) == 4);
    REQUIRE(desiredHeavyFactories(true, 0, 2000) == 1);
    REQUIRE(desiredHeavyFactories(true, 10000, 1000000) == 8);
    REQUIRE(desiredHeavyFactories(false, 0, 12000) == 4);
}
