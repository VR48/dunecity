#include <catch2/catch_test_macros.hpp>
#include <misc/EnhancedBuildingGeometry.h>
#include <mmath.h>

extern int currentZoomlevel;

namespace {
struct RestoreZoom {
    int zoom = currentZoomlevel;
    ~RestoreZoom() { currentZoomlevel = zoom; }
};
}

TEST_CASE("Dune2R building width matches the world footprint at every zoom", "[dune2r][geometry]") {
    RestoreZoom restore;
    const int expectedWidths[] = {48, 96, 144};
    for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
        INFO(zoom);
        currentZoomlevel = static_cast<int>(zoom);
        const SDL_Point anchor{world2zoomedWorld(11 * TILESIZE + 3 * TILESIZE / 2),
                               world2zoomedWorld(7 * TILESIZE + 2 * TILESIZE)};
        const auto rect = calcEnhancedBuildingDrawingRect(3, zoom, {576, 603}, {288, 603}, anchor);
        CHECK(rect.w == expectedWidths[zoom]);
        CHECK(rect.w == world2zoomedWorld(3 * TILESIZE));
        CHECK(rect.x == world2zoomedWorld(11 * TILESIZE));
        CHECK(rect.x + rect.w / 2 == anchor.x);
        CHECK(rect.y + rect.h == anchor.y);
        // Chimneys may rise above the ground footprint without stretching the image.
        CHECK(rect.h == static_cast<int>(std::lround(603.0 * rect.w / 576)));
    }
}

TEST_CASE("Dune2R building placement is independent of source resolution", "[dune2r][geometry]") {
    for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
        const SDL_Point anchor{300, 400};
        const auto frame = calcEnhancedBuildingDrawingRect(3, zoom, {576, 603}, {288, 603}, anchor);
        const auto doubled = calcEnhancedBuildingDrawingRect(3, zoom, {1152, 1206}, {576, 1206}, anchor);
        const auto still = calcEnhancedBuildingDrawingRect(3, zoom, {576, 576}, {288, 576}, anchor);
        CHECK(frame.x == doubled.x);
        CHECK(frame.y == doubled.y);
        CHECK(frame.w == doubled.w);
        CHECK(frame.h == doubled.h);
        CHECK(frame.x == still.x);
        CHECK(frame.w == still.w);
        CHECK(frame.y + frame.h == still.y + still.h);
    }
}

TEST_CASE("Dune2R building geometry preserves custom anchors and rejects invalid sizes", "[dune2r][geometry]") {
    const auto rect = calcEnhancedBuildingDrawingRect(2, 1, {384, 576}, {96, 480}, {300, 400});
    CHECK(rect.x == 284);
    CHECK(rect.y == 320);
    CHECK(rect.w == 64);
    CHECK(rect.h == 96);
    CHECK(calcEnhancedBuildingDrawingRect(0, 0, {576, 603}, {}, {}).w == 0);
    CHECK(calcEnhancedBuildingDrawingRect(3, NUM_ZOOMLEVEL, {576, 603}, {}, {}).w == 0);
    CHECK(calcEnhancedBuildingDrawingRect(3, 0, {0, 603}, {}, {}).w == 0);
    CHECK(calcEnhancedBuildingDrawingRect(3, 0, {576, 0}, {}, {}).h == 0);
}
