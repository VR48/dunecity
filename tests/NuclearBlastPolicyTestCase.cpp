#include <fstream>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include <dunecity/NuclearBlastPolicy.h>
#include <FileClasses/INIFile.h>
#include <cstdlib>
#include <string>

using namespace DuneCity::NuclearBlastPolicy;

TEST_CASE("Reactor blast has twice the palace impact area rather than twice its radius", "[city][nuclear]") {
    const double areaInTiles = radiusSquared * 3.141592653589793 / (TILESIZE * TILESIZE);
    REQUIRE(areaInTiles > missileImpactTiles * 1.99);
    REQUIRE(areaInTiles < missileImpactTiles * 2.01);
    REQUIRE(contains(0, 0));
    REQUIRE(contains(3*TILESIZE, 2*TILESIZE));
    REQUIRE_FALSE(contains(3*TILESIZE, 3*TILESIZE));
    REQUIRE_FALSE(contains(4*TILESIZE, 0));
    for (int x = -4*TILESIZE; x <= 4*TILESIZE; ++x)
        for (int y = -4*TILESIZE; y <= 4*TILESIZE; ++y) {
            REQUIRE(contains(x,y) == contains(-x,-y));
            REQUIRE(contains(x,y) == contains(y,x));
        }
}

TEST_CASE("A centered palace strike destroys a Starport-strength three-by-three reactor", "[city][nuclear]") {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    for (const auto* filename : {"config/ObjectData.ini.default", "mods/Tornie/ObjectData.ini"}) {
        INIFile data(std::string(root) + "/" + filename);
        const int hp = data.getIntValue("Nuclear Plant", "HitPoints");
        REQUIRE(hp > 0);
        REQUIRE(hp == data.getIntValue("Starport", "HitPoints"));
        // Nine of the existing missile's 21 impacts land inside a centered 3x3 plant.
        REQUIRE(9 * missileDamagePerTile >= hp);
        REQUIRE(plantBlastDamage >= hp); // Adjacent plants can detonate in turn.
    }
}

// Lifecycle code requires the full game/renderer. Keep a source integration
// guard alongside policy tests; runtime power totals are audited separately.
TEST_CASE("Every generator releases power on noncombat removal without a blast", "[power][regression]") {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    for (const std::string name : {"WindTrap", "NuclearPlant", "AdvancedWindTrap", "Scoutpost"}) {
        std::ifstream input(std::string(root) + "/src/structures/" + name + ".cpp");
        REQUIRE(input.good());
        const std::string code((std::istreambuf_iterator<char>(input)), {});
        const auto begin = code.find(name + "::~" + name + "()");
        REQUIRE(begin != std::string::npos);
        const auto end = code.find("}", begin);
        REQUIRE(end != std::string::npos);
        const auto destructor = code.substr(begin, end - begin);
        REQUIRE(destructor.find("setHealth(0)") != std::string::npos);
        REQUIRE(destructor.find("destroy()") == std::string::npos);
        REQUIRE(destructor.find("handleDamage(") == std::string::npos);
    }
}
