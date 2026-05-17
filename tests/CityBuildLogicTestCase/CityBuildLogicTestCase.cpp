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
