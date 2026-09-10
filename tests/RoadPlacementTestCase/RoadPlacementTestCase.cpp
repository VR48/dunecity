/*
 *  RoadPlacementTestCase.cpp - Regression tests for the Road structure
 *
 *  Roads are a build-menu structure (like concrete slabs): they mutate
 *  tile state on placement, render with an auto-tile sprite overlay, are
 *  not selectable. Concrete and Road are now separate tile states — a
 *  slab is just concrete, a road is just a road. Power flows globally,
 *  so road placement no longer carries any "conductivity" semantics.
 *
 *  Validates:
 *  - Slab1 and Slab4 placement paths do NOT set the road flag (concrete
 *    stays concrete).
 *  - Structure_Road is registered in BuilderBase::itemOrder.
 *  - Structure_PowerLine is NOT in itemOrder (no power lines in DuneCity).
 *  - Enum IDs preserved (no renumbering).
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CityConstants.h>
#include <dunecity/RoadMaintenancePolicy.h>
#include <fstream>
#include <string>
#include <cstdlib>

static std::string readSourceFile(const std::string& relativePath) {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    if (!env) {
        SKIP("DUNE_CITY_SOURCE_DIR not set — skipping source-scan test");
    }
    std::ifstream f(std::string(env) + "/" + relativePath);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// =============================================================================
// Enum ID stability (no renumbering happened)
// =============================================================================

TEST_CASE("RoadPlacement: Road and PowerLine enum IDs are preserved", "[road][ids]") {
    REQUIRE(Structure_Road == 23);
    REQUIRE(Structure_PowerLine == 24);
    REQUIRE(Structure_Slab1 == 14);
    REQUIRE(Structure_Slab4 == 15);
}

// =============================================================================
// Concrete and Road are independent tile states
// =============================================================================

TEST_CASE("RoadPlacement: Slab1 placement does NOT set the road flag", "[road][concrete-separation]") {
    std::string src = readSourceFile("src/House.cpp");
    REQUIRE_FALSE(src.empty());

    auto slab1Pos = src.find("case (Structure_Slab1):");
    REQUIRE(slab1Pos != std::string::npos);

    auto blockEnd = src.find("} break;", slab1Pos);
    REQUIRE(blockEnd != std::string::npos);

    std::string block = src.substr(slab1Pos, blockEnd - slab1Pos);
    REQUIRE(block.find("setRoad(true)") == std::string::npos);
    REQUIRE(block.find("setCityConductive") == std::string::npos);
}

TEST_CASE("RoadPlacement: Slab4 placement does NOT set the road flag", "[road][concrete-separation]") {
    std::string src = readSourceFile("src/House.cpp");
    REQUIRE_FALSE(src.empty());

    auto slab4Pos = src.find("case (Structure_Slab4):");
    REQUIRE(slab4Pos != std::string::npos);

    auto blockEnd = src.find("} break;", slab4Pos);
    REQUIRE(blockEnd != std::string::npos);

    std::string block = src.substr(slab4Pos, blockEnd - slab4Pos);
    REQUIRE(block.find("setRoad(true)") == std::string::npos);
    REQUIRE(block.find("setCityConductive") == std::string::npos);
}

TEST_CASE("RoadPlacement: Structure_Road placement sets the road flag", "[road][effect]") {
    std::string src = readSourceFile("src/House.cpp");
    REQUIRE_FALSE(src.empty());

    auto roadPos = src.find("case (Structure_Road):");
    REQUIRE(roadPos != std::string::npos);

    auto blockEnd = src.find("} break;", roadPos);
    REQUIRE(blockEnd != std::string::npos);

    std::string block = src.substr(roadPos, blockEnd - roadPos);
    REQUIRE(block.find("setRoad(true)") != std::string::npos);
}

// =============================================================================
// Slab placement clears road flag (slabs overwrite roads)
// =============================================================================

TEST_CASE("RoadPlacement: Slab1 clears road flag when placed over road", "[road][slab]") {
    std::string src = readSourceFile("src/House.cpp");
    REQUIRE_FALSE(src.empty());

    auto slab1Pos = src.find("case (Structure_Slab1):");
    REQUIRE(slab1Pos != std::string::npos);

    auto blockEnd = src.find("} break;", slab1Pos);
    REQUIRE(blockEnd != std::string::npos);

    std::string block = src.substr(slab1Pos, blockEnd - slab1Pos);
    // Slab1 must clear the road flag so the tile becomes pure concrete
    REQUIRE(block.find("setRoad(false)") != std::string::npos);
}

TEST_CASE("RoadPlacement: Slab4 clears road flag when placed over road", "[road][slab]") {
    std::string src = readSourceFile("src/House.cpp");
    REQUIRE_FALSE(src.empty());

    auto slab4Pos = src.find("case (Structure_Slab4):");
    REQUIRE(slab4Pos != std::string::npos);

    auto blockEnd = src.find("} break;", slab4Pos);
    REQUIRE(blockEnd != std::string::npos);

    std::string block = src.substr(slab4Pos, blockEnd - slab4Pos);
    // Slab4 must clear the road flag so each tile becomes pure concrete
    REQUIRE(block.find("setRoad(false)") != std::string::npos);
}

TEST_CASE("RoadPlacement: Slab placement over road tiles is not blocked by cursor validation", "[road][slab]") {
    // The cursor validation in Game.cpp uses isRock() to determine tile validity
    // for slab placement. Road tiles have Terrain_Rock type, so isRock() returns
    // true and the cursor correctly shows valid placement. This test ensures the
    // cursor validation does not add an isRoad() exclusion for slabs.
    std::string src = readSourceFile("src/Game.cpp");
    REQUIRE_FALSE(src.empty());

    // Find the cursor tile validation block
    auto tileValidPos = src.find("tileValid = pTile->isRock()");
    REQUIRE(tileValidPos != std::string::npos);

    // Get the full line/expression
    auto lineEnd = src.find(";", tileValidPos);
    REQUIRE(lineEnd != std::string::npos);
    std::string expr = src.substr(tileValidPos, lineEnd - tileValidPos);

    // The expression must NOT exclude road tiles for slab placement
    REQUIRE(expr.find("isRoad") == std::string::npos);
}

// =============================================================================
// Build menu wiring
// =============================================================================

TEST_CASE("RoadPlacement: Road is in the build menu; PowerLine is not", "[road][build]") {
    std::string src = readSourceFile("src/structures/BuilderBase.cpp");
    REQUIRE_FALSE(src.empty());

    auto orderStart = src.find("itemOrder[]");
    REQUIRE(orderStart != std::string::npos);
    auto orderEnd = src.find("};", orderStart);
    std::string orderBlock = src.substr(orderStart, orderEnd - orderStart);

    REQUIRE(orderBlock.find("Structure_Road") != std::string::npos);
    REQUIRE(orderBlock.find("Structure_PowerLine") == std::string::npos);
}

TEST_CASE("Road foundation is captured before placement clears the road flag", "[road][foundation][regression]") {
    const auto src=readSourceFile("src/structures/StructureBase.cpp");
    const auto start=src.find("void StructureBase::assignToMap");
    REQUIRE(start!=std::string::npos);
    const auto capture=src.find("const bool preparedFoundation = pTile->hasPreparedFoundation();",start);
    const auto clear=src.find("pTile->setRoad(false)",start);
    const auto damage=src.find("if(!preparedFoundation &&",start);
    REQUIRE(capture!=std::string::npos);REQUIRE(clear!=std::string::npos);REQUIRE(damage!=std::string::npos);
    REQUIRE(capture<clear);REQUIRE(clear<damage);
    const auto tile=readSourceFile("include/Tile.h");
    REQUIRE(tile.find("return isConcrete() || isRoad();")!=std::string::npos);
}

TEST_CASE("City construction can reuse enemy and abandoned roads without taking their upkeep", "[road][placement][regression]") {
    for (int owner : {-1, 0, 1, 4, 7}) {
        REQUIRE(DuneCity::isConstructionAnchor(true, true, owner, 1));
        REQUIRE(DuneCity::isConstructionAnchor(false, true, owner, 1) == (owner == 1));
        REQUIRE(DuneCity::isConstructionAnchor(true, false, owner, 1) == (owner == 1));
    }
    // Public access and responsibility for existing roads remain separate.
    REQUIRE(DuneCity::roadOwnerAfterPlacement(true, 4, 1) == 4);
    REQUIRE(DuneCity::roadOwnerAfterPlacement(false, 4, 1) == 1);
}

TEST_CASE("Road reuse keeps footprint occupancy and the bounded build-range check", "[road][placement][regression]") {
    const auto map = readSourceFile("src/Map.cpp");
    const auto begin = map.find("bool Map::okayToPlaceStructure(");
    const auto end = map.find("bool Map::isWithinBuildRange(", begin);
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto checks = map.substr(begin, end-begin);
    REQUIRE(checks.find("!pTile->hasPreparedFoundation()") != std::string::npos);
    REQUIRE(checks.find("pTile->hasCityZone()") != std::string::npos);
    REQUIRE(checks.find("pTile->isBlocked()") != std::string::npos);
    const auto range = map.substr(end, map.find("return false;",end)-end);
    REQUIRE(range.find("x - BUILDRANGE") != std::string::npos);
    REQUIRE(range.find("x + BUILDRANGE") != std::string::npos);
    REQUIRE(range.find("DuneCity::isConstructionAnchor") != std::string::npos);
}
