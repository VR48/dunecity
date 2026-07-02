/*
 *  NeutralHouseTestCase.cpp - Tests for the Neutral house (v1.0.111-120)
 *
 *  Validates: house enum, palette color, radar color, mentat animation
 *  registration, voice enum, skirmish availability (source scan), and
 *  ObjectData.ini prerequisites for Neutral-specific entries.
 *
 *  Source-scan tests skip gracefully when DUNE_CITY_SOURCE_DIR is not set.
 *  ObjectData tests skip gracefully when DUNE_CITY_SOURCE_DIR is not set
 *  (config lives in the repo root, not the data dir).
 */

#include <catch2/catch_all.hpp>
#include <DataTypes.h>
#include <Definitions.h>
#include <Colors.h>
#include <globals.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <fstream>
#include <string>
#include <cstdlib>

// ── source-scan helper ────────────────────────────────────────────────────────

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
// House enum
// =============================================================================

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL exists in the house enum",
          "[neutral][enum]") {
    REQUIRE(HOUSE_NEUTRAL == 6);
    // NUM_HOUSES must include Neutral + Rebels (0..7 → 8 total as of v1.0.228)
    REQUIRE(NUM_HOUSES == 8);
}

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL is after HOUSE_MERCENARY",
          "[neutral][enum]") {
    REQUIRE(HOUSE_NEUTRAL == HOUSE_MERCENARY + 1);
}

TEST_CASE("NeutralHouse: savegame version includes HOUSE_NEUTRAL",
          "[neutral][savegame]") {
    // SAVEGAMEVERSION 9817 added House::cityCredits (9816 added Unit_EliteSiegeTank=55, 9813 added HOUSE_NEUTRAL).
    REQUIRE(SAVEGAMEVERSION == 9817);
}

// =============================================================================
// Palette / radar color
// =============================================================================

TEST_CASE("NeutralHouse: PALCOLOR_NEUTRAL is 128",
          "[neutral][color]") {
    REQUIRE(PALCOLOR_NEUTRAL == 128);
}

TEST_CASE("NeutralHouse: houseToPaletteIndex maps HOUSE_NEUTRAL to PALCOLOR_NEUTRAL",
          "[neutral][color]") {
    REQUIRE(houseToPaletteIndex[HOUSE_NEUTRAL] == PALCOLOR_NEUTRAL);
}

// =============================================================================
// Mentat animation enum registration
// =============================================================================

TEST_CASE("NeutralHouse: Neutral mentat animation enum values are registered",
          "[neutral][mentat]") {
    // These enums must exist so MentatMenu can construct the Neutral mentat.
    // Verify they compile and are valid (> 0) in the animation enum.
    REQUIRE(Anim_NeutralEyes   >= 0);
    REQUIRE(Anim_NeutralMouth  >= 0);
    REQUIRE(Anim_NeutralShoulder >= 0);
    REQUIRE(Anim_NeutralRing   >= 0);
}

TEST_CASE("NeutralHouse: Neutral mentat animations are distinct from each other",
          "[neutral][mentat]") {
    REQUIRE(Anim_NeutralEyes   != Anim_NeutralMouth);
    REQUIRE(Anim_NeutralEyes   != Anim_NeutralShoulder);
    REQUIRE(Anim_NeutralMouth  != Anim_NeutralRing);
}

// =============================================================================
// Voice file mapping (source scan)
// =============================================================================

TEST_CASE("NeutralHouse: HouseNeutral voice enum is registered in SFXManager",
          "[neutral][voice]") {
    // Voice enum must include HouseNeutral before NUM_VOICE.
    REQUIRE(HouseNeutral < NUM_VOICE);
    // It follows HouseOrdos in the enum.
    REQUIRE(HouseNeutral == HouseOrdos + 1);
}

TEST_CASE("NeutralHouse: ANEU.VOC is the primary voice file for Neutral house announcement",
          "[neutral][voice][source-scan]") {
    std::string src = readSourceFile("src/FileClasses/SFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    // The Atreides/Fremen/Neutral fallback branch must use ANEU.VOC as primary
    // (replaced MNEU.VOC as primary in v1.0.120).
    auto pos = src.find("ANEU.VOC");
    INFO("SFXManager.cpp must reference ANEU.VOC for Neutral house name");
    REQUIRE(pos != std::string::npos);
}

TEST_CASE("NeutralHouse: HNEU.VOC and ONEU.VOC are referenced for Harkonnen/Ordos branches",
          "[neutral][voice][source-scan]") {
    std::string src = readSourceFile("src/FileClasses/SFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    INFO("SFXManager.cpp must reference HNEU.VOC for Harkonnen/Sardaukar branch");
    REQUIRE(src.find("HNEU.VOC") != std::string::npos);

    INFO("SFXManager.cpp must reference ONEU.VOC for Ordos/Mercenary branch");
    REQUIRE(src.find("ONEU.VOC") != std::string::npos);
}

// =============================================================================
// Skirmish availability (source scan)
// =============================================================================

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL is included in skirmish houseOrder",
          "[neutral][skirmish][source-scan]") {
    std::string src = readSourceFile("src/Menu/SinglePlayerSkirmishMenu.cpp");
    REQUIRE_FALSE(src.empty());

    auto pos = src.find("houseOrder[]");
    INFO("SinglePlayerSkirmishMenu.cpp must declare houseOrder");
    REQUIRE(pos != std::string::npos);

    // Extract houseOrder array contents
    auto end = src.find("}", pos);
    REQUIRE(end != std::string::npos);
    std::string orderBlock = src.substr(pos, end - pos);

    INFO("houseOrder must include HOUSE_NEUTRAL");
    REQUIRE(orderBlock.find("HOUSE_NEUTRAL") != std::string::npos);
}

// =============================================================================
// ObjectData.ini prerequisites (source scan — config/ is under source dir)
// =============================================================================

TEST_CASE("NeutralHouse: ObjectData.ini has Neutral-specific Launcher prerequisite",
          "[neutral][objectdata][source-scan]") {
    std::string ini = readSourceFile("config/ObjectData.ini.default");
    REQUIRE_FALSE(ini.empty());

    // [Launcher] must have Prerequisite(N) = House IX
    auto launcherPos = ini.find("[Launcher]");
    INFO("ObjectData.ini must have a [Launcher] section");
    REQUIRE(launcherPos != std::string::npos);

    // Find next section after [Launcher]
    auto nextSection = ini.find("[", launcherPos + 1);
    std::string launcherBlock = ini.substr(launcherPos,
        nextSection != std::string::npos ? nextSection - launcherPos : std::string::npos);

    INFO("Launcher section must have Prerequisite(N) = House IX for Neutral");
    REQUIRE(launcherBlock.find("Prerequisite(N) = House IX") != std::string::npos);
}
