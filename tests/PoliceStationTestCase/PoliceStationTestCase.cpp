/*
 *  PoliceStationTestCase.cpp
 *
 *  Regression tests for the DuneCity Police Station — a 2x2 city-mode
 *  building that replaced Barracks/WOR as the source of police coverage.
 *  Price matches SimCity Classic's TOOL_POLICESTATION (gCostOf[4] = 500
 *  in MicropolisEngine/src/tool.cpp).
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CityEffects.h>

using namespace DuneCity;

TEST_CASE("PoliceStation: assigned the expected ItemID (last structure ID)",
          "[police-station][ids][regression]") {
    REQUIRE(Structure_PoliceStation == 26);
    REQUIRE(Structure_LastID == 26);
}

TEST_CASE("PoliceStation: isStructure recognises it", "[police-station][helpers]") {
    REQUIRE(isStructure(Structure_PoliceStation));
    REQUIRE_FALSE(isUnit(Structure_PoliceStation));
    // Not a R/C/I zone — that helper is reserved for those three.
    REQUIRE_FALSE(isZoneStructure(Structure_PoliceStation));
}

TEST_CASE("PoliceStation: not a city-role structure (R/C/I)",
          "[police-station][role]") {
    // PoliceStation provides police coverage but is NOT a city-role
    // building — it doesn't contribute residential/commercial/industrial
    // supply or population, and it doesn't grow via runZoneGrowth.
    REQUIRE(getStructureCityRole(Structure_PoliceStation) == CityRole::None);
    REQUIRE(getStructureMaxLevel(Structure_PoliceStation) == 0);
}

TEST_CASE("PoliceStation: provides full police coverage",
          "[police-station][police][regression]") {
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == kPoliceCoverageFull);
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == 100);
}

TEST_CASE("PoliceStation: annual upkeep is 500 (matches SC TOOL_POLICESTATION)",
          "[police-station][police][regression]") {
    // SC source: MicropolisCore/MicropolisEngine/src/tool.cpp line 218,
    // gCostOf[TOOL_POLICESTATION] = 500.
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == kPoliceCostPoliceStation);
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == 500);
}

TEST_CASE("PoliceStation: replaces Barracks/WOR as the police source",
          "[police-station][police][regression]") {
    // The whole point of the PoliceStation: Barracks/WOR no longer count
    // toward crime reduction. Verify the absence so a future refactor
    // that re-adds them is noisy.
    REQUIRE(getPoliceCoverage(Structure_Barracks)   == 0);
    REQUIRE(getPoliceCoverage(Structure_WOR)        == 0);
    REQUIRE(getPoliceAnnualCost(Structure_Barracks) == 0);
    REQUIRE(getPoliceAnnualCost(Structure_WOR)      == 0);
}

TEST_CASE("PoliceStation: turrets keep their fractional coverage",
          "[police-station][police]") {
    // The user asked to remove police from Barracks/WOR only — turrets
    // retain their 1/4 and 1/10 partial coverage as garrison adjacency.
    REQUIRE(getPoliceCoverage(Structure_GunTurret)    == kPoliceCoverageGunTurret);
    REQUIRE(getPoliceCoverage(Structure_RocketTurret) == kPoliceCoverageRocketTurret);
}
