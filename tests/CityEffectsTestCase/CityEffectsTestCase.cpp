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
    // Starport and Airport have specific city roles
    REQUIRE(getStructureCityRole(Structure_StarPort)         == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Airport)          == CityRole::Commercial);
    // Non-role structures
    REQUIRE(getStructureCityRole(Structure_WindTrap)         == CityRole::None);
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

// --- Zone score / growth-decline gating --------------------------------------

TEST_CASE("computeLocalEval: residential scales with land value minus pollution",
          "[city-effects][zscore]") {
    // evalRes formula: clamp((lv-poll)*32, 0, 6000) → -3000 → /4
    // Good neighborhood: (200-0)*32=6400→6000; (6000-3000)/4 = 750
    CHECK(computeLocalEval(CityRole::Residential, 200, 0, true) == 750);
    // With pollution: (200-100)*32=3200; (3200-3000)/4 = 50
    CHECK(computeLocalEval(CityRole::Residential, 200, 100, true) == 50);
    // No road penalty: 750 - 300 (NoRoad) = 450
    CHECK(computeLocalEval(CityRole::Residential, 200, 0, false) == 450);
    // Bad: (20-100)*32=-2560→0; (0-3000)/4=-750; NoRoad -300 = -1050
    CHECK(computeLocalEval(CityRole::Residential, 20, 100, false) == -1050);
}

TEST_CASE("computeLocalEval: commercial uses land value, crime, pollution",
          "[city-effects][zscore]") {
    CHECK(computeLocalEval(CityRole::Commercial, 200, 0, true)  == 400);
    // pollution > 50 → -50 penalty: 400 - 50 = 350
    CHECK(computeLocalEval(CityRole::Commercial, 200, 100, true) == 350);
    CHECK(computeLocalEval(CityRole::Commercial, 0, 0, true)    == 0);
}

TEST_CASE("computeLocalEval: industrial dominated by traffic",
          "[city-effects][zscore]") {
    CHECK(computeLocalEval(CityRole::Industrial, 250, 0, true) == 0);
    // No road → -200 penalty
    CHECK(computeLocalEval(CityRole::Industrial, 0, 250, false) == -200);
}

TEST_CASE("computeZscore: unpowered always returns -500", "[city-effects][zscore]") {
    CHECK(computeZscore(2000, 1000, false) == -500);
    CHECK(computeZscore(-2000, -1000, false) == -500);
}

TEST_CASE("computeZscore: powered adds valve and local", "[city-effects][zscore]") {
    CHECK(computeZscore(-2000, 1000, true) == -1000);
    CHECK(computeZscore(1000, 640, true) == 1640);
    CHECK(computeZscore(0, 0, true) == 0);
}

TEST_CASE("R=-2000 always blocks residential growth via zscore",
          "[city-effects][zscore][regression]") {
    // Even in the best possible neighborhood (lv=250, poll=0, road=true),
    // R=-2000 must produce a zscore below kZscoreGrowthGate (-350).
    const int localEval = computeLocalEval(CityRole::Residential, 250, 0, true);
    const int zscore = computeZscore(-2000, localEval, true);
    CHECK(zscore < kZscoreGrowthGate);
}

TEST_CASE("shouldZoneDecline: no decline at level 0", "[city-effects][decline]") {
    CHECK_FALSE(shouldZoneDecline(-2000, 0, 0));
}

TEST_CASE("shouldZoneDecline: no decline above threshold", "[city-effects][decline]") {
    CHECK_FALSE(shouldZoneDecline(500, 3, 0));
    CHECK_FALSE(shouldZoneDecline(350, 3, 0));
}

TEST_CASE("shouldZoneDecline: strong negative triggers decline at L3",
          "[city-effects][decline]") {
    // zscore <= -1000: 25% chance (roll < 4)
    CHECK(shouldZoneDecline(-1000, 3, 0));
    CHECK(shouldZoneDecline(-1000, 3, 3));
    CHECK_FALSE(shouldZoneDecline(-1000, 3, 4));
}

TEST_CASE("shouldZoneDecline: moderate negative triggers at lower rate",
          "[city-effects][decline]") {
    // zscore <= -500: 12.5% (roll < 2)
    CHECK(shouldZoneDecline(-500, 3, 0));
    CHECK(shouldZoneDecline(-500, 3, 1));
    CHECK_FALSE(shouldZoneDecline(-500, 3, 2));
}

TEST_CASE("shouldZoneDecline: mild negative triggers at lowest rate",
          "[city-effects][decline]") {
    // zscore < 0: 6.25% (roll == 0)
    CHECK(shouldZoneDecline(-1, 2, 0));
    CHECK_FALSE(shouldZoneDecline(-1, 2, 1));
}

TEST_CASE("shouldZoneDecline: L1 protected unless extreme",
          "[city-effects][decline]") {
    // zscore -1000, level 1: protected (zscore > -1500)
    CHECK_FALSE(shouldZoneDecline(-1000, 1, 0));
    // zscore -1500, level 1: extreme enough to decline
    CHECK(shouldZoneDecline(-1500, 1, 0));
    // zscore -1600, level 1: extreme, roll < 4 (25%)
    CHECK(shouldZoneDecline(-1600, 1, 3));
    CHECK_FALSE(shouldZoneDecline(-1600, 1, 4));
}

TEST_CASE("shouldZoneDecline: buffer zone between 0 and kZscoreDeclineGate",
          "[city-effects][decline]") {
    // zscore in [0, 350): no decline even at roll 0
    CHECK_FALSE(shouldZoneDecline(0, 3, 0));
    CHECK_FALSE(shouldZoneDecline(200, 3, 0));
    CHECK_FALSE(shouldZoneDecline(349, 3, 0));
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

// --- Traffic-aware local evaluation -----------------------------------------

TEST_CASE("computeLocalEval (traffic): residential penalised by NoDestination",
          "[city-effects][zscore][traffic]") {
    // Connected traffic, good area
    int connected = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                     TrafficResult::Connected);
    // NoDestination: large penalty
    int noDest = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                  TrafficResult::NoDestination);
    // NoRoad: moderate penalty
    int noRoad = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(connected > noDest);
    CHECK(noDest >= noRoad - 300);  // NoRoad has -300, NoDest has -600
    CHECK(noRoad < connected);
}

TEST_CASE("computeLocalEval (traffic): commercial penalised by traffic failure",
          "[city-effects][zscore][traffic]") {
    int ok = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                              TrafficResult::Connected);
    int noDest = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                                  TrafficResult::NoDestination);
    int noRoad = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(ok > noDest);
    CHECK(ok > noRoad);
}

TEST_CASE("computeLocalEval (traffic): industrial penalised by traffic failure",
          "[city-effects][zscore][traffic]") {
    int ok = computeLocalEval(CityRole::Industrial, 0, 0, 0,
                              TrafficResult::Connected);
    int noRoad = computeLocalEval(CityRole::Industrial, 0, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(ok == 0);
    CHECK(noRoad < 0);
}

TEST_CASE("computeLocalEval (traffic): residential crime penalty",
          "[city-effects][zscore][traffic]") {
    int lowCrime = computeLocalEval(CityRole::Residential, 200, 0, 50,
                                    TrafficResult::Connected);
    int highCrime = computeLocalEval(CityRole::Residential, 200, 0, 200,
                                     TrafficResult::Connected);
    CHECK(lowCrime > highCrime);
}

TEST_CASE("computeLocalEval (traffic): commercial crime penalty",
          "[city-effects][zscore][traffic]") {
    int lowCrime = computeLocalEval(CityRole::Commercial, 200, 0, 50,
                                    TrafficResult::Connected);
    int highCrime = computeLocalEval(CityRole::Commercial, 200, 0, 200,
                                     TrafficResult::Connected);
    CHECK(lowCrime > highCrime);
}

// --- Micropolis-shaped demand valves ----------------------------------------

TEST_CASE("computeDemandValves: empty city produces positive R demand",
          "[city-effects][valves]") {
    // With no residents and no jobs, residential demand should be positive
    // (people want to move in).
    ValveInputs vi;
    vi.resPop = 0;
    vi.comPop = 0;
    vi.indPop = 0;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve >= 0);
}

TEST_CASE("computeDemandValves: excess residents produce negative R demand",
          "[city-effects][valves]") {
    // Many residents, no jobs: residential demand must be negative.
    ValveInputs vi;
    vi.resPop = 5000;
    vi.comPop = 0;
    vi.indPop = 0;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve < 0);
}

TEST_CASE("computeDemandValves: balanced city has moderate demand",
          "[city-effects][valves]") {
    ValveInputs vi;
    vi.resPop = 1000;
    vi.comPop = 500;
    vi.indPop = 500;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    // All valves should be in reasonable range
    CHECK(vo.resValve > -2000);
    CHECK(vo.resValve < 2000);
    CHECK(vo.comValve > -2000);
    CHECK(vo.comValve < 2000);
    CHECK(vo.indValve > -2000);
    CHECK(vo.indValve < 2000);
}

TEST_CASE("computeDemandValves: high tax suppresses all demand",
          "[city-effects][valves]") {
    ValveInputs base;
    base.resPop = 1000;
    base.comPop = 500;
    base.indPop = 500;
    base.taxRate = 7;
    const auto low = computeDemandValves(base);

    base.taxRate = 20;  // maximum tax
    const auto high = computeDemandValves(base);

    CHECK(high.resValve < low.resValve);
    CHECK(high.comValve < low.comValve);
    CHECK(high.indValve < low.indValve);
}

TEST_CASE("computeDemandValves: civic caps limit positive demand",
          "[city-effects][valves][civic-caps]") {
    ValveInputs vi;
    vi.resPop = 3000;  // above popThreshold
    vi.comPop = 2000;
    vi.indPop = 2000;
    vi.taxRate = 5;     // low tax for strong positive demand
    vi.popThreshold = 2000;

    // Without civics: demand is capped
    vi.hasStadium = false;
    vi.hasPalace  = false;
    vi.hasAirport = false;
    vi.hasStarport = false;
    const auto noCivic = computeDemandValves(vi);

    // With civics: demand uncapped
    vi.hasStadium  = true;
    vi.hasPalace   = true;
    vi.hasAirport  = true;
    vi.hasStarport = true;
    const auto withCivic = computeDemandValves(vi);

    // Civic-capped demand should be <= uncapped demand (may be equal if
    // demand is already below the cap ceiling).
    CHECK(noCivic.resValve <= withCivic.resValve);
    CHECK(noCivic.comValve <= withCivic.comValve);
    CHECK(noCivic.indValve <= withCivic.indValve);
}

TEST_CASE("computeDemandValves: R valve is negative when residents far exceed jobs",
          "[city-effects][valves][regression]") {
    // The core invariant: if residents >> jobs, R valve must be negative,
    // preventing residential from growing further.
    ValveInputs vi;
    vi.resPop = 10000;
    vi.comPop = 100;
    vi.indPop = 100;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve < 0);
    // C/I demand should be positive (need more jobs for all those residents).
    CHECK(vo.comValve > 0);
    CHECK(vo.indValve > 0);
}

TEST_CASE("computeDemandValves: C/I positive when R negative does not force R growth",
          "[city-effects][valves][regression]") {
    // This is the original bug: C/I positive shouldn't drag R along.
    // R valve being negative + local eval must produce zscore below growth gate.
    ValveInputs vi;
    vi.resPop = 10000;
    vi.comPop = 100;
    vi.indPop = 100;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve < kZscoreGrowthGate);
    // Even with maximum local eval boost, R still can't grow
    const int bestLocalEval = computeLocalEval(CityRole::Residential, 250, 0, 0,
                                               TrafficResult::Connected);
    const int zscore = computeZscore(vo.resValve, bestLocalEval, true);
    CHECK(zscore < kZscoreGrowthGate);
}

// --- Tax table ---------------------------------------------------------------

TEST_CASE("getTaxTableEntry: low tax is positive, high tax is strongly negative",
          "[city-effects][valves]") {
    CHECK(getTaxTableEntry(0) == 200);
    CHECK(getTaxTableEntry(7) == 0);
    CHECK(getTaxTableEntry(20) == -600);
}

// --- Palace role and population ----------------------------------------------

TEST_CASE("Palace is residential role with high population",
          "[city-effects][role][palace]") {
    REQUIRE(getStructureCityRole(Structure_Palace) == CityRole::Residential);
    REQUIRE(getStructureMaxLevel(Structure_Palace) == 3);
    REQUIRE(getZonePopulation(Structure_Palace, 1) == 250);
    REQUIRE(getZonePopulation(Structure_Palace, 2) == 500);
    REQUIRE(getZonePopulation(Structure_Palace, 3) == 1000);
}

// --- Starport role -----------------------------------------------------------

TEST_CASE("Starport and Airport have correct city roles",
          "[city-effects][role]") {
    REQUIRE(getStructureCityRole(Structure_StarPort) == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Airport)  == CityRole::Commercial);
    REQUIRE(getStructureMaxLevel(Structure_StarPort) == 3);
    REQUIRE(getStructureMaxLevel(Structure_Airport)  == 3);
}

// --- Traffic pollution -------------------------------------------------------

TEST_CASE("getTrafficPollution: no pollution below light threshold",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(0)  == 0);
    CHECK(getTrafficPollution(63) == 0);
}

TEST_CASE("getTrafficPollution: light traffic contributes small pollution",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(64)  == kTrafficPollutionLight);
    CHECK(getTrafficPollution(100) == kTrafficPollutionLight);
    CHECK(getTrafficPollution(149) == kTrafficPollutionLight);
}

TEST_CASE("getTrafficPollution: heavy traffic contributes more pollution",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(150) == kTrafficPollutionHeavy);
    CHECK(getTrafficPollution(240) == kTrafficPollutionHeavy);
}

// --- Commercial desirability (computeCommercialRate) -------------------------

TEST_CASE("computeCommercialRate: more nearby supply increases score",
          "[city-effects][commercial-rate]") {
    int noSupply = computeCommercialRate(0, 0, false);
    int withRes  = computeCommercialRate(100, 0, false);
    int withBoth = computeCommercialRate(100, 100, false);
    CHECK(withRes > noSupply);
    CHECK(withBoth > withRes);
}

TEST_CASE("computeCommercialRate: airport provides flat bonus",
          "[city-effects][commercial-rate]") {
    int noAirport   = computeCommercialRate(100, 50, false);
    int withAirport  = computeCommercialRate(100, 50, true);
    CHECK(withAirport == noAirport + 100);
}

TEST_CASE("computeCommercialRate: residential supply capped at 200",
          "[city-effects][commercial-rate]") {
    int at200  = computeCommercialRate(200, 0, false);
    int at500  = computeCommercialRate(500, 0, false);
    CHECK(at200 == at500);  // capped
}

// --- Regression scenario tests (spec Slice 6) --------------------------------

TEST_CASE("Regression: R=-2000 with C/I positive — residential must shrink",
          "[city-effects][regression][scenario]") {
    // With R=-2000, even best local eval, zscore must be below growth gate.
    const int bestEval = computeLocalEval(CityRole::Residential, kMaxLandValue, 0, 0,
                                          TrafficResult::Connected);
    const int zscore = computeZscore(-2000, bestEval, true);
    CHECK(zscore < kZscoreGrowthGate);
    // And it should be negative enough to decline.
    CHECK(shouldZoneDecline(zscore, 3, 0));
}

TEST_CASE("Regression: connected residential + jobs + low pollution can grow",
          "[city-effects][regression][scenario]") {
    // Positive valve, good area, powered, connected → should be above growth gate.
    const int goodEval = computeLocalEval(CityRole::Residential, 150, 10, 20,
                                           TrafficResult::Connected);
    const int zscore = computeZscore(500, goodEval, true);
    CHECK(zscore > kZscoreGrowthGate);
}

TEST_CASE("Regression: high pollution > 128 blocks residential immigration",
          "[city-effects][regression][scenario]") {
    // Pollution >= kPollutionGrowthBlock (160) blocks R/C growth.
    CHECK(isPollutionBlockingGrowth(160, CityRole::Residential, 2));
    CHECK(isPollutionBlockingGrowth(200, CityRole::Residential, 1));
    // But industrial is immune.
    CHECK_FALSE(isPollutionBlockingGrowth(200, CityRole::Industrial, 3));
}

TEST_CASE("Regression: high crime tanks land value and growth stalls",
          "[city-effects][regression][scenario]") {
    // Crime > 150 causes -200 penalty on residential eval.
    int lowCrimeEval = computeLocalEval(CityRole::Residential, 150, 0, 50,
                                         TrafficResult::Connected);
    int highCrimeEval = computeLocalEval(CityRole::Residential, 150, 0, 200,
                                          TrafficResult::Connected);
    CHECK(highCrimeEval < lowCrimeEval - 100);
}

TEST_CASE("Regression: Airport raises commercial demand/cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 3000;
    vi.comPop = 2000;
    vi.indPop = 1000;
    vi.taxRate = 5;
    vi.popThreshold = 2000;

    vi.hasAirport = false;
    const auto noAirport = computeDemandValves(vi);
    vi.hasAirport = true;
    const auto withAirport = computeDemandValves(vi);

    CHECK(withAirport.comValve >= noAirport.comValve);
}

TEST_CASE("Regression: Starport raises industrial demand/cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 3000;
    vi.comPop = 1000;
    vi.indPop = 2000;
    vi.taxRate = 5;
    vi.popThreshold = 2000;

    vi.hasStarport = false;
    const auto noStarport = computeDemandValves(vi);
    vi.hasStarport = true;
    const auto withStarport = computeDemandValves(vi);

    CHECK(withStarport.indValve >= noStarport.indValve);
}

TEST_CASE("Regression: Stadium/Palace raises residential cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 3000;
    vi.comPop = 2000;
    vi.indPop = 2000;
    vi.taxRate = 5;
    vi.popThreshold = 2000;

    vi.hasStadium = false;
    vi.hasPalace = false;
    const auto noCivic = computeDemandValves(vi);
    vi.hasStadium = true;
    vi.hasPalace = true;
    const auto withCivic = computeDemandValves(vi);

    CHECK(withCivic.resValve >= noCivic.resValve);
}

TEST_CASE("Regression: Palace provides residential population per spec",
          "[city-effects][regression][scenario]") {
    // Palace is residential-role, high-capacity.
    REQUIRE(getStructureCityRole(Structure_Palace) == CityRole::Residential);
    REQUIRE(getZonePopulation(Structure_Palace, 1) == 250);
    REQUIRE(getZonePopulation(Structure_Palace, 3) == 1000);
    // Palace provides a park land-value bonus.
    REQUIRE(getParkLandValueBonus(Structure_Palace) == kParkLandValueBonus);
}
