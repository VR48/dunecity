#include <catch2/catch_all.hpp>

#include <INIMap/MapPlayerSectionUtils.h>
#include <mod/CustomHouseConfig.h>
#include <mod/ModMentatConfig.h>

#include <array>
#include <string>

TEST_CASE("CustomHouse config: valid numeric fields parse without throwing",
          "[custom-house][config]") {
    int value = -1;
    REQUIRE(CustomHouseConfig::parseInteger("144", value));
    REQUIRE(value == 144);

    REQUIRE(CustomHouseConfig::parseInteger("0", value));
    REQUIRE(value == 0);
}

TEST_CASE("CustomHouse config: malformed numeric fields are rejected without mutation",
          "[custom-house][config][regression]") {
    const std::array<std::string, 5> malformedValues = {
        "",
        "not-a-number",
        "144trailing",
        "144 ",
        "999999999999999999999999999999"
    };

    for(const std::string& malformedValue : malformedValues) {
        CAPTURE(malformedValue);
        int destination = 73;
        REQUIRE_FALSE(CustomHouseConfig::parseInteger(malformedValue, destination));
        REQUIRE(destination == 73);
    }
}

TEST_CASE("PlayerN-only map scans the fixed available-house capacity",
          "[map][players][regression]") {
    constexpr int availableHouseCapacity = 8;
    int sectionsScanned = 0;

    const int numberedPlayerCount = MapPlayerSectionUtils::countNumberedPlayerSections(
        availableHouseCapacity,
        [&sectionsScanned](int playerNumber) {
            ++sectionsScanned;
            return playerNumber == 1 || playerNumber == 8;
        });

    REQUIRE(sectionsScanned == availableHouseCapacity);
    REQUIRE(numberedPlayerCount == 2);
    REQUIRE(0 + numberedPlayerCount == 2);
}

TEST_CASE("Mod Mentat numeric parsing is nonthrowing and rejects malformed values",
          "[mod][mentat][config]") {
    double frameRate = 0.5;
    REQUIRE(ModMentatConfig::parseDouble("5.25", frameRate));
    REQUIRE(frameRate == Catch::Approx(5.25));

    const std::array<std::string, 5> malformedValues = {
        "", "not-a-number", "5.0trailing", "nan", "1e9999"
    };
    for(const std::string& malformedValue : malformedValues) {
        CAPTURE(malformedValue);
        double destination = 3.0;
        REQUIRE_FALSE(ModMentatConfig::parseDouble(malformedValue, destination));
        REQUIRE(destination == Catch::Approx(3.0));
    }
}

TEST_CASE("Mod Mentat assets must use portable relative paths",
          "[mod][mentat][config][security]") {
    REQUIRE(ModMentatConfig::isSafeAssetPath("mentat/custom/background.png"));
    REQUIRE(ModMentatConfig::isSafeAssetPath("MentatEyes.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("../other-mod/asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("mentat/../asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("C:/local/asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("mentat\\asset.png"));
}

TEST_CASE("Invalid optional Mentat fields disable the override safely",
          "[mod][mentat][config][fallback]") {
    ModMentatInfo valid;
    valid.enabled = true;
    valid.identityHouse = 1;
    valid.backgroundAsset = "mentat/background.png";
    valid.eyesAsset = "mentat/eyes.png";
    valid.mouthAsset = "mentat/mouth.png";
    REQUIRE(ModMentatConfig::isValid(valid));

    ModMentatInfo invalidIdentity = valid;
    invalidIdentity.identityHouse = 8;
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidIdentity));

    ModMentatInfo invalidFrames = valid;
    invalidFrames.eyesFrames = 0;
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidFrames));

    ModMentatInfo invalidPath = valid;
    invalidPath.backgroundAsset = "../leaked.png";
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidPath));
}