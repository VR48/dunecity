/*
 *  CityStructuresTestCase.cpp - Unit tests for DuneCity structure integration
 *
 *  Validates:
 *  - Structure IDs for zone structures (20-22) remain valid.
 *  - Road (23) is enabled and lives in BuilderBase::itemOrder; PowerLine (24)
 *    is not in the build menu and remains disabled (power flows globally).
 *  - ConstructionYard does not own road routing — placement goes through the
 *    standard build-menu flow, which is special-cased in House::placeStructure.
 *  - Concrete (Slab1/Slab4) and Road are independent tile states.
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CityConstants.h>
#include <fstream>
#include <string>
#include <cstdlib>

static std::string getSourceDir() {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    if (!env) {
        SKIP("DUNE_CITY_SOURCE_DIR not set — skipping source-scan test");
    }
    return std::string(env);
}

static std::ifstream openSourceFile(const std::string& relativePath) {
    return std::ifstream(getSourceDir() + "/" + relativePath);
}

// =============================================================================
// Structure ID Tests — enum values are preserved (no renumbering)
// =============================================================================

TEST_CASE("CityStructures: Zone structure IDs are correct", "[city][phase5]") {
    REQUIRE(Structure_ZoneResidential == 20);
    REQUIRE(Structure_ZoneCommercial == 21);
    REQUIRE(Structure_ZoneIndustrial == 22);
}

TEST_CASE("CityStructures: Road and PowerLine enum IDs still exist", "[city][phase5]") {
    REQUIRE(Structure_Road == 23);
    REQUIRE(Structure_PowerLine == 24);
    REQUIRE(Structure_NuclearPlant == 25);
    // Structure_PoliceStation (26) is the current last structure ID.
    REQUIRE(Structure_PoliceStation == 26);
    REQUIRE(Structure_LastID == 26);
}

TEST_CASE("CityStructures: Road and PowerLine are structures not zones", "[city][phase5]") {
    REQUIRE(isStructure(Structure_Road));
    REQUIRE(isStructure(Structure_PowerLine));
    REQUIRE_FALSE(isZoneStructure(Structure_Road));
    REQUIRE_FALSE(isZoneStructure(Structure_PowerLine));
}

// =============================================================================
// Build Menu Removal Tests
// =============================================================================

TEST_CASE("CityStructures: BuilderBase itemOrder includes Road but not PowerLine",
          "[city][phase5][build]") {
    std::ifstream file = openSourceFile("src/structures/BuilderBase.cpp");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto orderStart = content.find("itemOrder[]");
    REQUIRE(orderStart != std::string::npos);
    auto orderEnd = content.find("};", orderStart);
    REQUIRE(orderEnd != std::string::npos);

    std::string orderBlock = content.substr(orderStart, orderEnd - orderStart);

    INFO("BuilderBase::itemOrder must contain Structure_Road");
    REQUIRE(orderBlock.find("Structure_Road") != std::string::npos);

    INFO("BuilderBase::itemOrder must not contain Structure_PowerLine");
    REQUIRE(orderBlock.find("Structure_PowerLine") == std::string::npos);

    INFO("BuilderBase::itemOrder must still contain zone structures");
    REQUIRE(orderBlock.find("Structure_ZoneResidential") != std::string::npos);
}

TEST_CASE("CityStructures: ConstructionYard no longer routes roads",
          "[city][phase5][build]") {
    std::ifstream file = openSourceFile("src/structures/ConstructionYard.cpp");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    INFO("ConstructionYard.cpp must not reference Structure_Road");
    REQUIRE(content.find("Structure_Road") == std::string::npos);

    INFO("ConstructionYard.cpp must not reference Structure_PowerLine");
    REQUIRE(content.find("Structure_PowerLine") == std::string::npos);

    INFO("ConstructionYard.cpp must not reference CMD_CITY_TOOL");
    REQUIRE(content.find("CMD_CITY_TOOL") == std::string::npos);
}

// =============================================================================
// ObjectData Configuration Tests — Road/PowerLine disabled
// =============================================================================

TEST_CASE("CityStructures: ObjectData Road is enabled", "[city][phase5][config]") {
    std::ifstream file = openSourceFile("config/ObjectData.ini.default");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto roadPos = content.find("[Road]");
    REQUIRE(roadPos != std::string::npos);

    auto nextSection = content.find("[", roadPos + 1);
    std::string roadSection = content.substr(roadPos,
        nextSection != std::string::npos ? nextSection - roadPos : std::string::npos);

    INFO("[Road] section must have Enabled = true");
    REQUIRE(roadSection.find("Enabled = true") != std::string::npos);
}

TEST_CASE("CityStructures: ObjectData PowerLine is disabled", "[city][phase5][config]") {
    std::ifstream file = openSourceFile("config/ObjectData.ini.default");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto plPos = content.find("[Power Line]");
    REQUIRE(plPos != std::string::npos);

    auto nextSection = content.find("[", plPos + 1);
    std::string plSection = content.substr(plPos,
        nextSection != std::string::npos ? nextSection - plPos : std::string::npos);

    INFO("[Power Line] section must have Enabled = false");
    REQUIRE(plSection.find("Enabled = false") != std::string::npos);
}

// =============================================================================
// Concrete-vs-Road separation
//
// In the current design concrete slabs are *not* roads: a slab is plain
// concrete, a road is a separate Structure_Road that mutates an isRoad_
// flag on the tile. Power flows globally — no tile-level conductivity.
// =============================================================================

TEST_CASE("CityStructures: Slab1 placement does not mark tile as road",
          "[city][phase5][road-separation]") {
    std::ifstream file = openSourceFile("src/House.cpp");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto slab1Pos = content.find("case (Structure_Slab1):");
    REQUIRE(slab1Pos != std::string::npos);

    auto nextCase = content.find("} break;", slab1Pos);
    REQUIRE(nextCase != std::string::npos);

    std::string slab1Block = content.substr(slab1Pos, nextCase - slab1Pos);

    INFO("Slab1 placement must not call setRoad or setCityConductive");
    REQUIRE(slab1Block.find("setRoad(true)") == std::string::npos);
    REQUIRE(slab1Block.find("setCityConductive") == std::string::npos);
}

TEST_CASE("CityStructures: Slab4 placement does not mark tile as road",
          "[city][phase5][road-separation]") {
    std::ifstream file = openSourceFile("src/House.cpp");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto slab4Pos = content.find("case (Structure_Slab4):");
    REQUIRE(slab4Pos != std::string::npos);

    auto nextCase = content.find("} break;", slab4Pos);
    REQUIRE(nextCase != std::string::npos);

    std::string slab4Block = content.substr(slab4Pos, nextCase - slab4Pos);

    INFO("Slab4 placement must not call setRoad or setCityConductive");
    REQUIRE(slab4Block.find("setRoad(true)") == std::string::npos);
    REQUIRE(slab4Block.find("setCityConductive") == std::string::npos);
}

TEST_CASE("CityStructures: Road placement marks tile as road",
          "[city][phase5][road-separation]") {
    std::ifstream file = openSourceFile("src/House.cpp");
    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto roadPos = content.find("case (Structure_Road):");
    REQUIRE(roadPos != std::string::npos);

    auto nextCase = content.find("} break;", roadPos);
    REQUIRE(nextCase != std::string::npos);

    std::string roadBlock = content.substr(roadPos, nextCase - roadPos);

    INFO("Structure_Road placement must call setRoad(true)");
    REQUIRE(roadBlock.find("setRoad(true)") != std::string::npos);
}
