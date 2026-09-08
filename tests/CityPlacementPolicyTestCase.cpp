#include <catch2/catch_test_macros.hpp>
#include <Definitions.h>
#include <players/CityPlacementPolicy.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/CityServiceInvestmentPolicy.h>
#include <dunecity/PopulationDensityPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <dunecity/CityConstants.h>
#include <fstream>
#include <cstdlib>

TEST_CASE("Spice opening funds refinery workers before factory infrastructure", "[ai][economy]") {
    using namespace QuantBotBuildPolicy;
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(773600,4,40)) == 3);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(24000,4,40)) == 2);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(0,4,40)) == 1);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(773600,4,1)) == 1);
}

TEST_CASE("Harvester investment follows spice and processing rather than army size", "[ai][economy]") {
    using QuantBotBuildPolicy::fundedSpiceHarvesters;
    REQUIRE(fundedSpiceHarvesters(40,3) == 9);
    REQUIRE(fundedSpiceHarvesters(40,14) == 40);
    REQUIRE(fundedSpiceHarvesters(2,14) == 2);
    REQUIRE(fundedSpiceHarvesters(40,0) == 0);
}

TEST_CASE("Generators compare incremental cost space and protected working capital", "[ai][power]") {
    using QuantBotBuildPolicy::preferNuclearPower;
    REQUIRE_FALSE(preferNuclearPower(100,100,300,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(600,100,300,2000,10,10000,600));
    REQUIRE(preferNuclearPower(700,100,300,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(700,100,300,2000,10,2500,600));
    REQUIRE(preferNuclearPower(300,100,300,2000,2,10000,600));
    REQUIRE_FALSE(preferNuclearPower(700,100,100,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(0,100,300,2000,0,10000,600));
}

TEST_CASE("Power growth forecast escapes repeated small windtrap top-ups", "[ai][power]") {
    using namespace QuantBotBuildPolicy;
    const int growth = projectedPowerGrowth(6000,6200,30,120);
    REQUIRE(growth == 800);
    REQUIRE_FALSE(preferNuclearPower(100,100,300,2000,20,10000,600));
    REQUIRE(preferNuclearPower(100+growth,100,300,2000,20,10000,600));
    REQUIRE(projectedPowerGrowth(6200,6200,30,120) == 0);
    REQUIRE(projectedPowerGrowth(6200,6000,30,120) == 0);
    REQUIRE(projectedPowerGrowth(6000,6200,0,120) == 0);
    REQUIRE(projectedPowerGrowth(0,100,30,120) == 100); // bounded startup/rebuild jump
}

TEST_CASE("Early land-value turrets must repay costs without relying on crime utility", "[city][placement]") {
    CityServiceInvestmentPolicy::Value v;
    v.buildCost=250; v.upkeep=15; v.powerCost=50;
    v.crime=1000; v.defense=1000; v.tax=300;
    REQUIRE_FALSE(v.repaysThroughLandValue());
    v.growthTax=16;
    REQUIRE(v.repaysThroughLandValue());
    v.overlapPenalty=20;
    REQUIRE_FALSE(v.repaysThroughLandValue());
    v.overlapPenalty=0; v.neighbourhoodTax=100; v.crime=0;
    REQUIRE_FALSE(v.landValueTurretEligible());
    v.crime=1;
    REQUIRE(v.landValueTurretEligible());
    auto industrial = v;
    industrial.neighbourhoodTax=0;
    REQUIRE_FALSE(industrial.landValueTurretEligible());
    REQUIRE(v.betterThan(industrial));
}

TEST_CASE("Population density diffuses point sources instead of saturating starter blocks", "[city][crime]") {
    DuneCity::CityMapLayer<uint8_t> density;
    density.init(32,32,2);
    density.set(8,8,128); // Level-one residential source: 16 * 8.
    DuneCity::smoothPopulationDensity(density,32,32);
    REQUIRE(density.get(8,8) == 52); // 13 centre-return walks / 4^3, then doubled.
    REQUIRE(density.get(9,8) == 48);
    REQUIRE(density.get(12,8) == 0);
    density.init(32,32,2);
    DuneCity::smoothPopulationDensity(density,32,32);
    REQUIRE(density.get(8,8) == 0);
}

TEST_CASE("Service investment compares economic return without bypassing emergency crime", "[city][placement]") {
    using namespace CityServiceInvestmentPolicy;
    Value police{600,100,0,0,500,100,10};
    Value turret{100,700,50,100,250,15,10};
    REQUIRE(turret.betterThan(police));
    REQUIRE(turret.useful(true));
    turret.crime = 0;
    REQUIRE_FALSE(turret.useful(false));
    REQUIRE_FALSE(turret.useful(true));
    turret.tax = 0; turret.growthTax = 0; turret.defense = 0;
    REQUIRE_FALSE(turret.useful(false));
    turret.crime = 100;
    REQUIRE(police.betterThan(turret));
    Value expensive = police;
    expensive.upkeep += 500;
    REQUIRE(police.betterThan(expensive));
    REQUIRE(annualTaxGain(1000,7,128,100) == 46);
    REQUIRE(annualTaxGain(1000,7,0,100) == 0);
    REQUIRE(annualTaxGain(1000,7,128,0) == 0);
    REQUIRE(parkContribution(Structure_PoliceStation,10,10,10,10,2) == 0);
    const int stamp = parkContribution(Structure_RocketTurret,10,10,10,10,2);
    const int strength = DuneCity::getParkLandValueBonus(Structure_RocketTurret);
    const int radius = DuneCity::getParkLandValueRadius(Structure_RocketTurret)+1;
    REQUIRE(stamp == strength + 3*DuneCity::falloff(strength,1,radius));
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,11,11,2) == stamp);
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,100,100,2) == 0);
    expensive = police;
    expensive.overlapPenalty = 500;
    REQUIRE(police.betterThan(expensive));
}

TEST_CASE("Dangerous developed districts reserve a share of construction", "[city][placement]") {
    using namespace QuantBotBuildPolicy;
    REQUIRE(crimeServiceOrderInterval(0, 0) == 0);
    REQUIRE(crimeServiceOrderInterval(100, 24) == 0);
    REQUIRE(crimeServiceOrderInterval(100, 25) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 50) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 51) == 2);
    REQUIRE_FALSE(crimeServiceOrderDue(0, 3));
    for (unsigned interval : {2u, 4u}) {
        unsigned progress = 3, services = 0;
        for (unsigned order = 0; order < 12; ++order) {
            if (crimeServiceOrderDue(interval, progress)) {
                ++services;
                progress = 0;
            } else ++progress;
        }
        REQUIRE(services == 12 / interval);
    }
    REQUIRE_FALSE(crimeServiceOrderDue(4, 1));
    REQUIRE(crimeServiceOrderDue(2, 1)); // Worsening crime accelerates response.
}

TEST_CASE("City turret placement preserves cross T and corner road arms", "[city][placement]") {
    const auto cross = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return x==0 || y==0;});
    const auto tee = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return y==0 || (x==0 && y<0);});
    const auto corner = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return (x==0 && y<=0)||(y==0 && x>=0);});
    REQUIRE(cross.preservesConnections);
    REQUIRE(tee.preservesConnections);
    REQUIRE(corner.preservesConnections);
    REQUIRE(cross.junctionBonus > tee.junctionBonus);
    REQUIRE(tee.junctionBonus > corner.junctionBonus);
    REQUIRE(corner.junctionBonus > 0);
}

TEST_CASE("City placement does not cut through a straight road", "[city][placement]") {
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int,int y){return y==0;}).preservesConnections);
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,3,2,false,[](int,int y){return y==0;}).preservesConnections);
    REQUIRE(CityPlacementPolicy::assessRoads(0,1,3,2,false,[](int,int y){return y==0;}).preservesConnections);
    // Existing perimeter loop provides an intact local detour around the factory.
    REQUIRE(CityPlacementPolicy::assessRoads(0,0,3,2,false,[](int x,int y){
        return y==-1 || y==2 || x==-1 || x==3 || y==0;
    }).preservesConnections);
}

TEST_CASE("Rocket turrets are traffic connectors but factories and units are not", "[city][traffic]") {
    REQUIRE(DuneCity::isTrafficConnector(false, Structure_RocketTurret));
    REQUIRE(DuneCity::isTrafficConnector(true, NONE_ID));
    REQUIRE_FALSE(DuneCity::isTrafficConnector(false, Structure_HeavyFactory));
    REQUIRE_FALSE(DuneCity::isTrafficConnector(false, Unit_Tank));
}

static std::string placementSource(const std::string& file) {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    std::ifstream input(std::string(root) + "/" + file);
    REQUIRE(input.good());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

TEST_CASE("Road restoration clears damage and traffic uses the shared connector", "[city][road][regression]") {
    const auto tile = placementSource("include/Tile.h");
    const auto start = tile.find("void setRoad(bool");
    const auto block = tile.substr(start, tile.find("bool isRoadConnection", start)-start);
    REQUIRE(block.find("DestroyedStructure_None") != std::string::npos);
    REQUIRE(block.find("damage.clear()") != std::string::npos);
    REQUIRE(block.find("deadUnits.clear()") != std::string::npos);
    REQUIRE(placementSource("src/dunecity/TrafficSimulation.cpp").find("->isRoadConnection()") != std::string::npos);
}

TEST_CASE("Completed factory placement retries without cancelling or requiring concrete", "[quantbot][placement][regression]") {
    const auto source = placementSource("src/players/QuantBot.cpp");
    const auto start = source.find("if (pBuilder->isWaitingToPlace())");
    const auto block = source.substr(start, source.find("void QuantBot::scrambleUnitsAndDefend",start)-start);
    REQUIRE(block.find("itemsize.y, false, getHouse(), false, itemToBePlaced") != std::string::npos);
    REQUIRE(block.find("placement_replan") != std::string::npos);
    REQUIRE(block.find("placement_deferred") != std::string::npos);
    // Only obsolete concrete is cancelled; completed buildings stay available.
    const auto cancel = block.find("doCancelItem(pConstYard, itemToBePlaced)");
    REQUIRE(cancel != std::string::npos);
    REQUIRE(block.find("doCancelItem(pConstYard, itemToBePlaced)", cancel+1) == std::string::npos);
}

TEST_CASE("Ordinary buildings cannot rely on diagonal-only traffic detours", "[city][placement]") {
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,1,1,false,[](int x,int y){return x==0 || y==0;}).preservesConnections);
}


TEST_CASE("Construction reservations reject overlapping footprints and permit adjacent lots", "[quantbot][placement]") {
    // A heavy factory reservation protects every covered tile against a second yard.
    REQUIRE(CityPlacementPolicy::overlaps(10,10,3,2,12,11,2,2));
    REQUIRE(CityPlacementPolicy::overlaps(12,11,2,2,10,10,3,2));
    REQUIRE(CityPlacementPolicy::overlaps(10,10,3,2,10,10,3,2));
    REQUIRE_FALSE(CityPlacementPolicy::overlaps(10,10,3,2,13,10,2,2));
    REQUIRE_FALSE(CityPlacementPolicy::overlaps(10,10,3,2,10,12,2,2));
}

TEST_CASE("Economic rebuilding avoids recent destruction but releases the site after cooldown", "[quantbot][placement]") {
    REQUIRE(CityPlacementPolicy::recentLossBlocks(10,10,2,2,10,10,2,2,30,60));
    REQUIRE(CityPlacementPolicy::recentLossBlocks(12,10,2,2,10,10,2,2,30,60));
    REQUIRE_FALSE(CityPlacementPolicy::recentLossBlocks(14,10,2,2,10,10,2,2,30,60));
    REQUIRE_FALSE(CityPlacementPolicy::recentLossBlocks(10,10,2,2,10,10,2,2,60,60));
}

TEST_CASE("Rocket turrets protect reactors before factories and useful city junctions", "[city][placement]") {
    using namespace RocketTurretPolicy;
    REQUIRE(defenseWeight(Structure_NuclearPlant) == 2 * defenseWeight(Structure_HeavyFactory));
    REQUIRE(defenseWeight(Structure_RepairYard) == defenseWeight(Structure_HeavyFactory));
    REQUIRE(defenseWeight(Structure_ZoneResidential) == 0);
    Score reactor{2, 120, 0, 1};
    Score factory{1, 240, 100, 10};
    Score city{0, 240, 1000, 0};
    REQUIRE(reactor.betterThan(factory));
    REQUIRE(factory.betterThan(city));
    REQUIRE(city.useful());
    REQUIRE_FALSE((Score{0, 0, 100, 0}).useful()); // amenities require a junction/corner
    REQUIRE_FALSE((Score{0, 240, 0, 0}).useful()); // no endless unused turrets
    REQUIRE((Score{1, 240, 0, 1}).betterThan(Score{1, 0, 0, 10}));
}

TEST_CASE("Rocket amenities have park strength and expanded useful coverage", "[city][placement]") {
    using namespace RocketTurretPolicy;
    REQUIRE(DuneCity::getPoliceCoverage(Structure_RocketTurret) == 150);
    REQUIRE(DuneCity::getParkLandValueBonus(Structure_RocketTurret) == 15);
    REQUIRE(amenityBenefit(100, false, 0) == 15);
    REQUIRE(amenityBenefit(100, false, 2) == 10);
    REQUIRE(amenityBenefit(245, false, 0) == 5);
    REQUIRE(amenityBenefit(250, false, 0) == 0);
    REQUIRE(amenityBenefit(100, true, 0) == 0);
    REQUIRE(amenityBenefit(100, false, DuneCity::kRocketTurretParkBonusRadius + 1) == 0);
}

TEST_CASE("Polluting factories and R/C keep a footprint-aware buffer", "[city][placement]") {
    using namespace CityPlacementPolicy;
    REQUIRE(footprintDistance(0,0,3,2,3,0,2,2) == 1);
    REQUIRE(footprintDistance(3,0,2,2,0,0,3,2) == 1);
    REQUIRE(pollutionSeparationScore(6) > pollutionSeparationScore(1));
    REQUIRE(pollutionSeparationScore(6) > pollutionSeparationScore(20));
    REQUIRE(pollutionSeparationScore(5) < 0);
    REQUIRE(residentialCommercialEnvironmentScore(0,100,0)
          > residentialCommercialEnvironmentScore(100,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,150,0)
          > residentialCommercialEnvironmentScore(0,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,100,8)
          > residentialCommercialEnvironmentScore(0,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,100,0,100)
          > residentialCommercialEnvironmentScore(0,100,0,240));
    // Sand and value should not outweigh severe industrial pollution.
    REQUIRE(residentialCommercialEnvironmentScore(0,80,0)
          > residentialCommercialEnvironmentScore(200,250,12));
}

TEST_CASE("Industry prefers a clean local commute over adjacent or distant lots", "[city][placement]") {
    using namespace CityPlacementPolicy;
    REQUIRE(cityPlacementTier(true, 6) > cityPlacementTier(true, 1));
    REQUIRE(cityPlacementTier(true, 6) > cityPlacementTier(false, 20));
    REQUIRE(cityPlacementTier(true, 1) > cityPlacementTier(false, 20));
    REQUIRE(cityPlacementTier(true, 5) < cityPlacementTier(true, 6));
    REQUIRE(sensitivePlacementTier(true, 8, 40, 120)
          > sensitivePlacementTier(true, 8, 180, 240));
}

#include <players/TacticalSafetyPolicy.h>
#include <dunecity/NuclearBlastPolicy.h>
TEST_CASE("Reactor spacing clears blast geometry for neighbouring factory footprints", "[city][safety]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE_FALSE(blastClearance(0,0,3,3,6,0,3,2));
    REQUIRE(blastClearance(0,0,3,3,7,0,3,2));
    REQUIRE(blastClearance(0,0,3,3,0,7,3,2));
    REQUIRE(blastClearance(0,0,3,3,7,7,3,2));
    REQUIRE_FALSE(DuneCity::NuclearBlastPolicy::contains(5*TILESIZE,0));
    REQUIRE(blastClearance(7,0,3,2,0,0,3,3));
}
TEST_CASE("Recent loss influence fades and overlapping losses accumulate", "[city][safety]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(lossStrength(0,300) > lossStrength(150,300));
    REQUIRE(lossStrength(299,300) > 0);
    REQUIRE(lossStrength(300,300) == 0);
    REQUIRE(lossStrength(1000,300) == 0);
    REQUIRE(lossStrength(0,0) == 0);
    REQUIRE(lossStrength(10,300)*2 > lossStrength(10,300));
}
TEST_CASE("Harvester corridor assessment spots fire between safe endpoints", "[quantbot][harvester]") {
    using namespace TacticalSafetyPolicy;
    auto danger = [](int x,int y) { return x==5 && y==5 ? 100 : 0; };
    REQUIRE(corridorDanger(0,0,10,10,danger) == 100);
    REQUIRE(corridorDanger(0,0,10,0,danger) == 0);
    REQUIRE(corridorDanger(10,10,0,0,danger) == 100);
    REQUIRE(corridorDanger(5,5,5,5,danger) == 0);
    REQUIRE(corridorDanger(5,5,10,5,danger) == 0);
}

TEST_CASE("Harvester escape permits leaving danger but rejects crossing another threat", "[tactical][harvester]") {
    using TacticalSafetyPolicy::escapeCorridor;
    REQUIRE(escapeCorridor(0,0,6,0,[](int x,int) { return x<3 ? 100:0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,6,0,[](int x,int) { return x<2 || x==5 ? 100:0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,6,0,[](int x,int) { return x==3 ? 100:0; }));
    REQUIRE(escapeCorridor(0,0,6,0,[](int,int) { return 0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,0,0,[](int,int) { return 100; }));
}

#include <players/RedevelopmentPolicy.h>
TEST_CASE("Redevelopment compares normalized demand and displacement", "[city][placement]") {
    using RedevelopmentPolicy::displacementCost;
    REQUIRE(displacementCost(1,30,2000,2000)==displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(1,30,-500,1500)<displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(0,30,1500,1500)<displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(1,5,0,1500)<displacementCost(1,60,0,1500));
}

TEST_CASE("Four-zone preference uses four 2x2 lots and an outer road gap", "[city][placement]") {
    using CityPlacementPolicy::fourZoneGridSlot;
    REQUIRE(fourZoneGridSlot(10,10,10,10));
    REQUIRE(fourZoneGridSlot(12,10,10,10));
    REQUIRE(fourZoneGridSlot(10,12,10,10));
    REQUIRE(fourZoneGridSlot(12,12,10,10));
    REQUIRE_FALSE(fourZoneGridSlot(14,12,10,10));
    REQUIRE(fourZoneGridSlot(15,15,10,10));
}
TEST_CASE("Planned automatic perimeter roads may replace an internal road", "[city][roads]") {
    auto road=[](int x,int y) { return x==0 && y>=-1 && y<=2; };
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,2,2,false,road).preservesConnections);
    REQUIRE(CityPlacementPolicy::assessRoads(0,0,2,2,false,road,[](int,int) { return true; }).preservesConnections);
}

TEST_CASE("Road paving assessment never reads beyond map edges", "[city][placement][regression]") {
    constexpr int mapWidth = 128, mapHeight = 128;
    int reads = 0;
    auto tile = [&](int x, int y) {
        REQUIRE(x >= 0);
        REQUIRE(y >= 0);
        REQUIRE(x < mapWidth);
        REQUIRE(y < mapHeight);
        ++reads;
        return true;
    };
    // Actual crash: factory near x=93, y=0 evaluated perimeter (92,-1).
    for (const auto& origin : std::vector<std::pair<int,int>>{
            {93,0}, {0,40}, {125,40}, {40,126}, {0,0}, {125,126}}) {
        const auto impact = CityPlacementPolicy::assessRoadsOnMap(
            mapWidth,mapHeight,origin.first,origin.second,3,2,false,tile,tile);
        REQUIRE(impact.roadsCovered == 6);
    }
    // The separate turret-arm lookup also must stay bounded.
    for (const auto& origin : std::vector<std::pair<int,int>>{{0,0},{127,0},{0,127},{127,127}})
        REQUIRE(CityPlacementPolicy::assessRoadsOnMap(
            mapWidth,mapHeight,origin.first,origin.second,1,1,true,tile,tile).preservesConnections);
    REQUIRE(reads > 0);
}

TEST_CASE("Four-zone blocks reject off-map neighbours and perimeter roads", "[city][placement][regression]") {
    constexpr int width=128, height=128;
    for (int y=0; y<height-1; ++y) for (int x=0; x<width-1; ++x)
        for (int oy : {0,2}) for (int ox : {0,2}) {
            const int bx=x-ox, by=y-oy;
            if (!CityPlacementPolicy::fourZoneBlockFits(width,height,bx,by)) continue;
            for (int dy=-1; dy<=4; ++dy) for (int dx=-1; dx<=4; ++dx) {
                REQUIRE(bx+dx >= 0);
                REQUIRE(by+dy >= 0);
                REQUIRE(bx+dx < width);
                REQUIRE(by+dy < height);
            }
        }
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,-1,20));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,20,0));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,124,20));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,20,124));
    REQUIRE(CityPlacementPolicy::fourZoneBlockFits(128,128,1,1));
    REQUIRE(CityPlacementPolicy::fourZoneBlockFits(128,128,123,123));
}

TEST_CASE("Police purchases value actual harm instead of repeatedly polishing safe streets", "[city][placement]") {
    using namespace CityServiceInvestmentPolicy;
    Value polish{600,0,0,0,500,100,10};
    REQUIRE_FALSE(polish.useful(false));
    REQUIRE_FALSE(polish.useful(true)); // raw crime points no longer bypass purchase value
    polish.dangerousRelief=40;
    REQUIRE(polish.useful(true));
    REQUIRE_FALSE(polish.useful(false));
    REQUIRE(crimeHarm(63,Structure_ZoneResidential,20)==0);
    REQUIRE(crimeHarm(101,Structure_ZoneResidential,20)>crimeHarm(100,Structure_ZoneResidential,20));
    REQUIRE(crimeHarm(121,Structure_ZoneCommercial,10)>crimeHarm(120,Structure_ZoneCommercial,10));
}

TEST_CASE("Harvesters avoid launcher approach range before actual weapon reach", "[tactical][harvester]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(harvesterThreatRadius(Unit_Launcher,9)==12);
    REQUIRE(harvesterThreatRadius(Unit_Tank,4)==4);
    REQUIRE(harvesterThreatRadius(Unit_Trooper,5)==-1);
    auto danger=[](int x,int) { return std::abs(x-20)<=harvesterThreatRadius(Unit_Launcher,9) ? 100 : 0; };
    REQUIRE_FALSE(escapeCorridor(0,0,9,0,danger)); // Destination outside weapon range but too close.
    REQUIRE_FALSE(escapeCorridor(0,0,40,0,danger)); // Safe endpoints cannot justify crossing the launcher.
    REQUIRE(escapeCorridor(9,0,0,0,danger)); // Allow escape out of the warning margin.
}

TEST_CASE("Empty harvesters do not repeat refuge trips because their old field is unsafe", "[ai][harvester]") {
    using TacticalSafetyPolicy::needsRefineryRefuge;
    // Escape first; after unloading at a safe site, find a safe job or hold.
    REQUIRE(needsRefineryRefuge(true,true,false,false));
    REQUIRE_FALSE(needsRefineryRefuge(false,true,false,false));
    REQUIRE_FALSE(needsRefineryRefuge(false,true,true,false));
    // Carrying spice still warrants safe unloading, even before being hit.
    REQUIRE(needsRefineryRefuge(false,true,false,true));
    REQUIRE(needsRefineryRefuge(false,false,true,true));
    REQUIRE_FALSE(needsRefineryRefuge(false,false,false,true));
}
