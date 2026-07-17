/*
 *  CityBuildLogicTestCase.cpp - Unit tests for city road placement helpers.
 */

#include <catch2/catch_all.hpp>
#include <Command.h>
#include <dunecity/CityConstants.h>

TEST_CASE("CityBuildLogic: road placement uses the city tool road command", "[city][roads][command]") {
    const auto command = DuneCity::getRoadPlacementCommandDescriptor();

    REQUIRE(command.commandId == CMD_CITY_TOOL);
    REQUIRE(command.parameter == static_cast<uint32_t>(DuneCity::CityTool_Road));
}

TEST_CASE("CityBuildLogic: only roads bypass normal production timing", "[city][roads][concrete]") {
    REQUIRE(DuneCity::usesInstantCityProduction(Structure_Road));
    REQUIRE_FALSE(DuneCity::usesInstantCityProduction(Structure_Slab1));
    REQUIRE_FALSE(DuneCity::usesInstantCityProduction(Structure_Slab4));
}

TEST_CASE("CityBuildLogic: road placement accepts concrete terrain", "[city][roads][concrete]") {
    // Tile::isRock() includes Terrain_Slab, so concrete reaches this helper as
    // supported terrain and the road flag replaces the slab visually.
    const auto concreteState = DuneCity::makeCityTilePlacementState(
        true, false, false, false, false);
    REQUIRE(DuneCity::canPlaceRoad(concreteState));
}

TEST_CASE("CityBuildLogic: valid road placement marks tile as road", "[city][roads][placement]") {
    auto state = DuneCity::makeCityTilePlacementState(
        true,   // isRock
        false,  // isMountain
        false,  // hasGroundObject
        false,  // hasCityZone
        false   // hasRoad
    );

    REQUIRE(DuneCity::canPlaceRoad(state));
    REQUIRE(DuneCity::applyRoadPlacement(state));
    REQUIRE(state.hasRoad);
}

TEST_CASE("CityBuildLogic: invalid road placement is rejected cleanly", "[city][roads][placement]") {
    SECTION("occupied tile") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, true, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("blocked terrain") {
        const auto state = DuneCity::makeCityTilePlacementState(false, false, false, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("mountain terrain") {
        const auto state = DuneCity::makeCityTilePlacementState(true, true, false, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("duplicate road") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, false, false, true);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE(mutableState.hasRoad);
    }

    SECTION("existing city zone") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, false, true, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }
}
