/*
 *  CityBudgetIntegrationTestCase.cpp - Tests for city budget integration with construction
 *
 *  Validates:
 *  - spendCityFunds deducts correctly when sufficient funds exist
 *  - spendCityFunds returns false and deducts nothing when insufficient funds
 *  - Road and PowerLine structure prices are defined correctly
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

// =============================================================================
// spendCityFunds: Sufficient Funds Tests
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds succeeds with exact funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    REQUIRE(sim.getTotalFunds() == 0); // Starts at 0
    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    // Exact match should succeed
    REQUIRE(sim.spendCityFunds(100) == true);
    REQUIRE(sim.getTotalFunds() == 0);
}

TEST_CASE("CityBudget: spendCityFunds succeeds with surplus funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(1000);

    REQUIRE(sim.spendCityFunds(25) == true);
    REQUIRE(sim.getTotalFunds() == 975);
}

TEST_CASE("CityBudget: spendCityFunds succeeds for small amounts", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(50);

    REQUIRE(sim.spendCityFunds(1) == true);
    REQUIRE(sim.getTotalFunds() == 49);
}

TEST_CASE("CityBudget: multiple spendCityFunds calls accumulate", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);

    REQUIRE(sim.spendCityFunds(25) == true);   // Road cost
    REQUIRE(sim.getTotalFunds() == 75);
    REQUIRE(sim.spendCityFunds(15) == true);   // Power line cost
    REQUIRE(sim.getTotalFunds() == 60);
    REQUIRE(sim.spendCityFunds(25) == true);   // Another road
    REQUIRE(sim.getTotalFunds() == 35);
}

// =============================================================================
// spendCityFunds: Insufficient Funds Tests
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds fails with zero funds", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(0);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 0);  // No deduction on failure
}

TEST_CASE("CityBudget: spendCityFunds fails when one credit short", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // No deduction on failure
}

TEST_CASE("CityBudget: spendCityFunds fails on negative balance request", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(50);

    REQUIRE(sim.spendCityFunds(0) == true);   // Zero spend should succeed (no-op)
    REQUIRE(sim.spendCityFunds(51) == false); // Overdraft should fail
    REQUIRE(sim.getTotalFunds() == 50);        // Unchanged
}

TEST_CASE("CityBudget: spendCityFunds fails when exact amount would zero out", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // Unchanged
}

// =============================================================================
// Structure ID Stability
// =============================================================================

TEST_CASE("CityBudget: Road and PowerLine enum IDs are preserved", "[citybudget][ids]") {
    // Enum values remain stable (no renumbering)
    REQUIRE(Structure_Road == 23);
    REQUIRE(Structure_PowerLine == 24);
    REQUIRE(Structure_LastID == 26);  // PoliceStation (26) is now the last structure
}

// =============================================================================
// City Budget Deduction Flow Tests
// =============================================================================

TEST_CASE("CityBudget: spending 25 credits deducted from city funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    REQUIRE(sim.spendCityFunds(25) == true);
    REQUIRE(sim.getTotalFunds() == 75);
}

TEST_CASE("CityBudget: insufficient funds blocks spending", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // Funds unchanged
}

TEST_CASE("CityBudget: spending 15 credits deducted from city funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    REQUIRE(sim.spendCityFunds(15) == true);
    REQUIRE(sim.getTotalFunds() == 85);
}

TEST_CASE("CityBudget: insufficient funds blocks small spending", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(14);

    REQUIRE(sim.spendCityFunds(15) == false);
    REQUIRE(sim.getTotalFunds() == 14);  // Funds unchanged
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds does not go negative", "[citybudget][edge]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(10);
    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 10);  // Still 10, never negative
}

TEST_CASE("CityBudget: setTotalFunds changes balance", "[citybudget][funds]") {
    DuneCity::CitySimulation sim;

    REQUIRE(sim.getTotalFunds() == 0);
    sim.setTotalFunds(5000);
    REQUIRE(sim.getTotalFunds() == 5000);
}

TEST_CASE("CityBudget: Large deduction succeeds with sufficient funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100000);
    REQUIRE(sim.spendCityFunds(99975) == true);
    REQUIRE(sim.getTotalFunds() == 25);
}

TEST_CASE("CityBudget: Multiple roads drain city budget progressively", "[citybudget][road]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);

    // Place 4 roads (4 x 25 = 100)
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 1: 100 -> 75
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 2: 75 -> 50
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 3: 50 -> 25
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 4: 25 -> 0
    REQUIRE(sim.getTotalFunds() == 0);

    // Fifth road blocked
    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 0);
}

TEST_CASE("CityBudget: Alternating roads and power lines correct total", "[citybudget][road][powerline]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(200);

    // Road (25) + PowerLine (15) = 40 per pair
    REQUIRE(sim.spendCityFunds(25) == true);  // 200 -> 175
    REQUIRE(sim.spendCityFunds(15) == true);  // 175 -> 160
    REQUIRE(sim.spendCityFunds(25) == true);  // 160 -> 135
    REQUIRE(sim.spendCityFunds(15) == true);  // 135 -> 120
    REQUIRE(sim.getTotalFunds() == 120);
}


TEST_CASE("Road upkeep follows Micropolis weighting and aggregate rounding", "[citybudget][roads]") {
    DuneCity::RoadMaintenanceCensus roads;
    REQUIRE(roads.annualCost(2000) == 0);
    for (int i = 0; i < 80; ++i) roads.add(true, 191);
    for (int i = 0; i < 20; ++i) roads.add(true, 192);
    // 100 physical road tiles + 20 extra heavy weights, at 0.7/year.
    REQUIRE(roads.tiles == 100);
    REQUIRE(roads.heavyTiles == 20);
    REQUIRE(roads.annualCost(2000) == 84);
    roads.add(false, 255); // Concrete, destroyed roads and building footprints are free.
    REQUIRE(roads.annualCost(2000) == 84);
    roads = {};
    roads.add(true, 0);
    REQUIRE(roads.annualCost(2000) == 0); // Truncate the annual total, not individual tiles.
    roads.add(true, 64);
    REQUIRE(roads.annualCost(2000) == 1);
}

TEST_CASE("Road placement preserves existing owners and assigns new roads", "[citybudget][roads]") {
    REQUIRE(DuneCity::roadOwnerAfterPlacement(false, -1, 2) == 2);
    REQUIRE(DuneCity::roadOwnerAfterPlacement(false, 1, 2) == 2);
    REQUIRE(DuneCity::roadOwnerAfterPlacement(true, 1, 2) == 1);
    REQUIRE(DuneCity::roadOwnerAfterPlacement(true, -1, 2) == 2);
}

TEST_CASE("Legacy roads inherit only adjacent structure ownership", "[citybudget][roads]") {
    auto ownerAt = [](int x, int y) {
        if (x == 9 && y == 9) return 0; // Diagonal loses to direct neighbors.
        if (x == 10 && y == 9) return 2;
        if (x == 11 && y == 10) return 1; // Stable house-ID tie break.
        return -1;
    };
    REQUIRE(DuneCity::legacyRoadOwner(-1, 10, 10, ownerAt) == 1);
    REQUIRE(DuneCity::legacyRoadOwner(3, 10, 10, ownerAt) == 3);
    REQUIRE(DuneCity::legacyRoadOwner(-1, 50, 50, ownerAt) == -1);
}

TEST_CASE("Road census is per house and fractional upkeep accumulates over a year", "[citybudget][roads]") {
    DuneCity::HouseCityState houses[2];
    for (int i = 0; i < 100; ++i) {
        houses[0].roads.add(true, 0);
        houses[1].roads.add(true, 240);
    }
    REQUIRE(houses[0].roads.annualCost(2000) == 70);
    REQUIRE(houses[1].roads.annualCost(2000) == 140);
    const FixPoint tick = FixPoint(houses[0].roads.annualCost(2000)) / DuneCity::kBudgetTicksPerYear;
    FixPoint charged = 0;
    for (int i = 0; i < DuneCity::kBudgetTicksPerYear; ++i) charged += tick;
    REQUIRE(charged.toDouble() == Catch::Approx(70.0).margin(0.001));
    houses[0].roads = {}; // Removed roads disappear in the next census.
    REQUIRE(houses[0].roads.annualCost(2000) == 0);
    REQUIRE(houses[1].roads.annualCost(2000) == 140);
}


TEST_CASE("Road upkeep starts at 2000 displayed population and stops below it", "[citybudget][roads]") {
    DuneCity::RoadMaintenanceCensus roads;
    for (int i = 0; i < 100; ++i) roads.add(true, 240);
    REQUIRE(roads.annualCost(0) == 0);
    REQUIRE(roads.annualCost(1999) == 0);
    REQUIRE(roads.annualCost(2000) == 140);
    REQUIRE(roads.annualCost(2001) == 140);
    REQUIRE(roads.annualCost(1999) == 0); // No permanent unlock after crossing the threshold.
    // The threshold uses each house's population, not the sum for the map.
    DuneCity::HouseCityState houses[2];
    houses[0].resPop = 99;
    houses[1].resPop = 100;
    const int scale = DuneCity::CitySimulation::kPopDisplayMultiplier;
    REQUIRE(roads.annualCost(houses[0].getTotalPop() * scale) == 0);
    REQUIRE(roads.annualCost(houses[1].getTotalPop() * scale) == 140);
}
