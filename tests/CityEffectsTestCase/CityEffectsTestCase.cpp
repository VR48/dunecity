/*
 *  CityEffectsTestCase.cpp
 *
 *  Locks the spec for the city effects pipeline:
 *  pollution emission, supply contributions, police coverage,
 *  park-bonus mappings, distance falloff, and demand thresholds.
 *
 *  All city-role structures (zones AND existing Dune buildings that map
 *  to SC-Classic roles) share a uniform `level` parameter 0..maxLevel.
 *  Level 0 = vacant: no supply, no pollution, no population.
 */

#include <catch2/catch_all.hpp>
#include <dunecity/CityEffects.h>
#include <data.h>

using namespace DuneCity;

// --- Roles & max level -------------------------------------------------------

TEST_CASE("getStructureCityRole categorises mapped buildings", "[city-effects][role]") {
    REQUIRE(getStructureCityRole(Structure_ZoneResidential)  == CityRole::Residential);
    REQUIRE(getStructureCityRole(Structure_ZoneCommercial)   == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_ZoneIndustrial)   == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Silo)             == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_Radar)            == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_HighTechFactory)  == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_IX)               == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_LightFactory)     == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_HeavyFactory)     == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_RepairYard)       == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Refinery)         == CityRole::Industrial);
    // Non-role structures
    REQUIRE(getStructureCityRole(Structure_WindTrap)         == CityRole::None);
    REQUIRE(getStructureCityRole(Structure_StarPort)         == CityRole::None);
    REQUIRE(getStructureCityRole(Structure_Wall)             == CityRole::None);
}

TEST_CASE("getStructureMaxLevel matches structure tier", "[city-effects][role]") {
    REQUIRE(getStructureMaxLevel(Structure_ZoneResidential)  == 3);
    REQUIRE(getStructureMaxLevel(Structure_ZoneCommercial)   == 3);
    REQUIRE(getStructureMaxLevel(Structure_ZoneIndustrial)   == 3);
    REQUIRE(getStructureMaxLevel(Structure_Silo)             == 1);  // C-low
    REQUIRE(getStructureMaxLevel(Structure_Radar)            == 2);  // C-medium
    REQUIRE(getStructureMaxLevel(Structure_HighTechFactory)  == 3);  // C-high
    REQUIRE(getStructureMaxLevel(Structure_IX)               == 3);  // C-high
    REQUIRE(getStructureMaxLevel(Structure_LightFactory)     == 2);  // I-medium
    REQUIRE(getStructureMaxLevel(Structure_HeavyFactory)     == 3);  // I-high
    REQUIRE(getStructureMaxLevel(Structure_RepairYard)       == 3);  // I-high
    REQUIRE(getStructureMaxLevel(Structure_Refinery)         == 1);  // I-low (small factory)
    REQUIRE(getStructureMaxLevel(Structure_WindTrap)         == 0);  // not a role
}

// --- Pollution ---------------------------------------------------------------

TEST_CASE("Pollution: WindTrap and Nuclear are clean", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_WindTrap, 3)     == 0);
    REQUIRE(getPollutionEmission(Structure_NuclearPlant, 3) == 0);
}

TEST_CASE("Pollution: Starport is clean (per spec override)", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_StarPort, 3) == 0);
}

TEST_CASE("Pollution: civic / commercial / defensive structures are clean", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ConstructionYard, 3) == 0);
    REQUIRE(getPollutionEmission(Structure_Palace, 3)           == 0);
    REQUIRE(getPollutionEmission(Structure_Radar, 2)            == 0);
    REQUIRE(getPollutionEmission(Structure_Barracks, 3)         == 0);
    REQUIRE(getPollutionEmission(Structure_WOR, 3)              == 0);
    REQUIRE(getPollutionEmission(Structure_GunTurret, 3)        == 0);
    REQUIRE(getPollutionEmission(Structure_RocketTurret, 3)     == 0);
    REQUIRE(getPollutionEmission(Structure_Wall, 3)             == 0);
    REQUIRE(getPollutionEmission(Structure_Silo, 1)             == 0);
    REQUIRE(getPollutionEmission(Structure_HighTechFactory, 3)  == 0);
    REQUIRE(getPollutionEmission(Structure_IX, 3)               == 0);
    REQUIRE(getPollutionEmission(Structure_ZoneResidential, 3)  == 0);
    REQUIRE(getPollutionEmission(Structure_ZoneCommercial, 3)   == 0);
}

TEST_CASE("Pollution: industrial sources scale uniformly with level", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 1) == 10);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 2) == 25);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 3) == 50);
    // Non-zone industrial buildings emit on the same scale:
    REQUIRE(getPollutionEmission(Structure_LightFactory, 2)   == 25);
    REQUIRE(getPollutionEmission(Structure_HeavyFactory, 3)   == 50);
    REQUIRE(getPollutionEmission(Structure_RepairYard, 3)     == 50);
    // Refinery is I-low: only ever runs at level 1 in the sim.
    REQUIRE(getPollutionEmission(Structure_Refinery, 1)       == 10);
}

TEST_CASE("Pollution: vacant (level 0) emits nothing", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 0) == 0);
    REQUIRE(getPollutionEmission(Structure_HeavyFactory, 0)   == 0);
    REQUIRE(getPollutionEmission(Structure_Refinery, 0)       == 0);
}

// --- Supply ------------------------------------------------------------------

TEST_CASE("Commercial supply scales by level for any commercial-role structure",
          "[city-effects][supply]") {
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 1) == 10);
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 2) == 25);
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 3) == 50);
    // Non-zone commercial buildings, evaluated at their respective max level:
    REQUIRE(getCommercialSupply(Structure_Silo, 1)             == 10);  // C-low max
    REQUIRE(getCommercialSupply(Structure_Radar, 2)            == 25);  // C-medium max
    REQUIRE(getCommercialSupply(Structure_HighTechFactory, 3)  == 50);  // C-high max
    REQUIRE(getCommercialSupply(Structure_IX, 3)               == 50);
    // Industrial structures don't contribute commercial supply.
    REQUIRE(getCommercialSupply(Structure_HeavyFactory, 3) == 0);
    REQUIRE(getCommercialSupply(Structure_Refinery, 1)     == 0);
}

TEST_CASE("Industrial supply scales by level for any industrial-role structure",
          "[city-effects][supply]") {
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 1) == 10);
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 2) == 25);
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 3) == 50);
    REQUIRE(getIndustrialSupply(Structure_LightFactory, 2)   == 25);
    REQUIRE(getIndustrialSupply(Structure_HeavyFactory, 3)   == 50);
    REQUIRE(getIndustrialSupply(Structure_RepairYard, 3)     == 50);
    // Refinery is I-low: capped at level 1 = 10 supply.
    REQUIRE(getIndustrialSupply(Structure_Refinery, 1)       == 10);
}

TEST_CASE("Residential supply only comes from R zones",
          "[city-effects][supply]") {
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 1) == 10);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 2) == 25);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 3) == 50);
    REQUIRE(getResidentialSupply(Structure_Silo, 1)            == 0);
    REQUIRE(getResidentialSupply(Structure_HeavyFactory, 3)    == 0);
}

TEST_CASE("All city-role structures contribute zero supply at level 0",
          "[city-effects][supply]") {
    REQUIRE(getCommercialSupply(Structure_Silo, 0)        == 0);
    REQUIRE(getCommercialSupply(Structure_HighTechFactory, 0) == 0);
    REQUIRE(getIndustrialSupply(Structure_Refinery, 0)    == 0);
    REQUIRE(getIndustrialSupply(Structure_HeavyFactory, 0) == 0);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 0) == 0);
}

// --- Police coverage ---------------------------------------------------------

TEST_CASE("Police coverage: PoliceStation full, gun turrets quarter, rocket turrets 10%",
          "[city-effects][police]") {
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == 100);
    REQUIRE(getPoliceCoverage(Structure_GunTurret)     == 25);
    REQUIRE(getPoliceCoverage(Structure_RocketTurret)  == 10);
    REQUIRE(getPoliceCoverage(Structure_Wall)          == 0);
    REQUIRE(getPoliceCoverage(Structure_HeavyFactory)  == 0);
}

TEST_CASE("Police coverage: Barracks and WOR no longer count as police (regression)",
          "[city-effects][police][regression]") {
    // Barracks and WOR are infantry-production buildings only. Adding
    // them used to grant full police coverage, which was a design bug:
    // putting a barracks down would magically turn slums luxury. Police
    // Station is now the sole full-coverage source.
    REQUIRE(getPoliceCoverage(Structure_Barracks)  == 0);
    REQUIRE(getPoliceCoverage(Structure_WOR)       == 0);
    REQUIRE(getPoliceAnnualCost(Structure_Barracks) == 0);
    REQUIRE(getPoliceAnnualCost(Structure_WOR)      == 0);
}

TEST_CASE("Police annual cost mirrors coverage; PoliceStation costs 500 (SC TOOL_POLICESTATION)",
          "[city-effects][police]") {
    // SC source: MicropolisEngine/src/tool.cpp gCostOf[4] = 500.
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == 500);
    // GunTurret and RocketTurret keep their fractional police coverage
    // as a garrison adjacency effect, but contribute zero to the police
    // bill — base defenses shouldn't drain the city budget.
    REQUIRE(getPoliceAnnualCost(Structure_GunTurret)     == 0);
    REQUIRE(getPoliceAnnualCost(Structure_RocketTurret)  == 0);
    REQUIRE(getPoliceAnnualCost(Structure_Wall)          == 0);
    REQUIRE(getPoliceAnnualCost(Structure_HeavyFactory)  == 0);
}

// --- Park bonus --------------------------------------------------------------

TEST_CASE("Park land-value bonus: Wall and Turrets contribute, garrisons do not",
          "[city-effects][park]") {
    REQUIRE(getParkLandValueBonus(Structure_Wall)         == kParkLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_GunTurret)    == kParkLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_RocketTurret) == kParkLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_Barracks)     == 0);
    REQUIRE(getParkLandValueBonus(Structure_WOR)          == 0);
    REQUIRE(getParkLandValueBonus(Structure_HeavyFactory) == 0);
}

// --- Falloff helper ----------------------------------------------------------

TEST_CASE("falloff: zero distance returns full strength", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 0, 5) == 100);
}

TEST_CASE("falloff: distance >= radius returns zero", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 5, 5) == 0);
    REQUIRE(falloff(100, 6, 5) == 0);
    REQUIRE(falloff(100, 99, 3) == 0);
}

TEST_CASE("falloff: linear interpolation between 0 and radius", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 1, 5) == 80);  // (5-1)/5 * 100 = 80
    REQUIRE(falloff(100, 2, 5) == 60);
    REQUIRE(falloff(100, 3, 5) == 40);
    REQUIRE(falloff(100, 4, 5) == 20);
}

TEST_CASE("falloff: degenerate radius returns zero", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 0, 0) == 0);
    REQUIRE(falloff(100, 0, -1) == 0);
}

// --- Population --------------------------------------------------------------

TEST_CASE("Zone population is zero when level is zero", "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 0) == 0);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 0)  == 0);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 0)  == 0);
}

TEST_CASE("Non-zone city-role buildings ALSO contribute population at their level",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_HeavyFactory, 3)   == 500);
    REQUIRE(getZonePopulation(Structure_HighTechFactory, 3) == 500);
    REQUIRE(getZonePopulation(Structure_IX, 3)             == 500);
    REQUIRE(getZonePopulation(Structure_Silo, 1)           == 60);
    REQUIRE(getZonePopulation(Structure_Radar, 2)          == 200);
    // Refinery is I-low: caps at level 1 = 60 jobs.
    REQUIRE(getZonePopulation(Structure_Refinery, 1)       == 60);
    // Vacant — contributes nothing
    REQUIRE(getZonePopulation(Structure_Refinery, 0)       == 0);
    // Non-role structure — never contributes
    REQUIRE(getZonePopulation(Structure_WindTrap, 3)       == 0);
}

TEST_CASE("Residential zones contribute people, scaled with level",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 1) == 100);
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 2) == 300);
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 3) == 800);
}

TEST_CASE("Commercial and industrial zones contribute jobs",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 1) == 60);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 2) == 200);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 3) == 500);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 1) == 60);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 2) == 200);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 3) == 500);
}

TEST_CASE("Population at level >3 clamps to level-3 value",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 5)  == 800);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 99)  == 500);
}

// --- Tax ---------------------------------------------------------------------

TEST_CASE("Annual tax is zero for empty city or zero rate",
          "[city-effects][tax]") {
    REQUIRE(computeAnnualTaxRevenue(0, 7)    == 0);
    REQUIRE(computeAnnualTaxRevenue(1000, 0) == 0);
    REQUIRE(computeAnnualTaxRevenue(-5, 7)   == 0);
    REQUIRE(computeAnnualTaxRevenue(1000, -3) == 0);
}

TEST_CASE("Annual tax scales linearly with population and rate",
          "[city-effects][tax]") {
    REQUIRE(computeAnnualTaxRevenue(1000, 7)  == 70);
    REQUIRE(computeAnnualTaxRevenue(2000, 7)  == 140);
    REQUIRE(computeAnnualTaxRevenue(1000, 14) == 140);
    REQUIRE(computeAnnualTaxRevenue(500, 20)  == 100);
}

TEST_CASE("Demand thresholds increase with level", "[city-effects][demand]") {
    REQUIRE(getDemandResidentialThreshold(1) <  getDemandResidentialThreshold(2));
    REQUIRE(getDemandResidentialThreshold(2) <  getDemandResidentialThreshold(3));
    REQUIRE(getDemandJobsThreshold(1)        <  getDemandJobsThreshold(2));
    REQUIRE(getDemandJobsThreshold(2)        <  getDemandJobsThreshold(3));
    // Land-value floors are zeroed across every level: density is gated by
    // local supply only, while value tier handles "rich vs poor" visuals.
    REQUIRE(getDemandLandValueFloor(1)       == 0);
    REQUIRE(getDemandLandValueFloor(2)       == 0);
    REQUIRE(getDemandLandValueFloor(3)       == 0);
}
