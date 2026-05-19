/*
 *  ConstructionYardResolverTestCase.cpp - Regression tests for DuneCity
 *  construction-yard item resolver coverage.
 *
 *  Validates that every DuneCity structure ID (20-24) has entries in:
 *  - resolveItemPicture() switch
 *  - resolveItemName() switch
 *  - getStructureSize() switch
 *  - getItemNameByID() switch (internal name resolver)
 *
 *  These are source-scan tests: they read sand.cpp and verify the switch
 *  statements contain explicit cases for each DuneCity item.  This catches
 *  the class of bug where a new item ID is added to data.h but one or more
 *  resolver functions are not updated, causing a crash when the
 *  construction-yard UI hovers/clicks/draws the item.
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <fstream>
#include <string>
#include <cstdlib>
#include <vector>

// ── helpers ─────────────────────────────────────────────────────────────────

static std::string getSourceDir() {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    return env ? std::string(env) : std::string();
}

static std::string readFile(const std::string& relativePath) {
    std::ifstream f(getSourceDir() + "/" + relativePath);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// Extract a switch-case block from source text that starts with a function
// signature containing `funcName`.  We look for the opening '{' after the
// signature and collect until the matching '}'.
static std::string extractSwitchBlock(const std::string& src,
                                       const std::string& funcSignature) {
    auto pos = src.find(funcSignature);
    if (pos == std::string::npos) return {};

    // Find the opening brace of the function body
    auto bracePos = src.find('{', pos);
    if (bracePos == std::string::npos) return {};

    int depth = 1;
    auto i = bracePos + 1;
    while (i < src.size() && depth > 0) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') --depth;
        ++i;
    }
    return src.substr(pos, i - pos);
}

// All DuneCity structure item IDs and their enum-name strings.
struct DuneCityItem {
    int         id;
    const char* enumName;   // e.g. "Structure_Road"
};

static const std::vector<DuneCityItem>& duneCityItems() {
    static const std::vector<DuneCityItem> items = {
        { Structure_ZoneResidential, "Structure_ZoneResidential" },
        { Structure_ZoneCommercial,  "Structure_ZoneCommercial"  },
        { Structure_ZoneIndustrial,  "Structure_ZoneIndustrial"  },
        { Structure_Road,            "Structure_Road"            },
        { Structure_PowerLine,       "Structure_PowerLine"       },
    };
    return items;
}

// ── data.h sanity ───────────────────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: DuneCity item IDs are valid structures",
          "[city][resolver][regression]") {
    for (const auto& item : duneCityItems()) {
        INFO("Checking " << item.enumName << " (ID " << item.id << ")");
        REQUIRE(isStructure(item.id));
        REQUIRE_FALSE(isUnit(item.id));
    }
}

// ── resolveItemPicture ──────────────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: resolveItemPicture handles all DuneCity items",
          "[city][resolver][regression]") {
    std::string sand = readFile("src/sand.cpp");
    REQUIRE_FALSE(sand.empty());

    std::string block = extractSwitchBlock(sand, "resolveItemPicture(");
    REQUIRE_FALSE(block.empty());

    for (const auto& item : duneCityItems()) {
        INFO("resolveItemPicture() must have case " << item.enumName);
        REQUIRE(block.find(item.enumName) != std::string::npos);
    }
}

// ── resolveItemName ─────────────────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: resolveItemName handles all DuneCity items",
          "[city][resolver][regression]") {
    std::string sand = readFile("src/sand.cpp");
    REQUIRE_FALSE(sand.empty());

    std::string block = extractSwitchBlock(sand, "resolveItemName(");
    REQUIRE_FALSE(block.empty());

    for (const auto& item : duneCityItems()) {
        INFO("resolveItemName() must have case " << item.enumName);
        REQUIRE(block.find(item.enumName) != std::string::npos);
    }
}

// ── getStructureSize ────────────────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: getStructureSize handles all DuneCity items",
          "[city][resolver][regression]") {
    std::string sand = readFile("src/sand.cpp");
    REQUIRE_FALSE(sand.empty());

    std::string block = extractSwitchBlock(sand, "getStructureSize(");
    REQUIRE_FALSE(block.empty());

    for (const auto& item : duneCityItems()) {
        INFO("getStructureSize() must have case " << item.enumName);
        REQUIRE(block.find(item.enumName) != std::string::npos);
    }
}

// ── getItemNameByID ─────────────────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: getItemNameByID handles all DuneCity items",
          "[city][resolver][regression]") {
    std::string sand = readFile("src/sand.cpp");
    REQUIRE_FALSE(sand.empty());

    std::string block = extractSwitchBlock(sand, "getItemNameByID(");
    REQUIRE_FALSE(block.empty());

    for (const auto& item : duneCityItems()) {
        INFO("getItemNameByID() must have case " << item.enumName);
        REQUIRE(block.find(item.enumName) != std::string::npos);
    }
}

// ── BuilderList uses resolvers ──────────────────────────────────────────────

TEST_CASE("ConstructionYardResolver: BuilderList calls resolveItemPicture and resolveItemName",
          "[city][resolver][regression]") {
    std::string src = readFile("src/GUI/dune/BuilderList.cpp");
    REQUIRE_FALSE(src.empty());

    INFO("BuilderList must call resolveItemPicture for item drawing");
    REQUIRE(src.find("resolveItemPicture") != std::string::npos);

    INFO("BuilderList must call resolveItemName for tooltip/overlay");
    REQUIRE(src.find("resolveItemName") != std::string::npos);
}

// ── Completeness guard: Structure_LastID ────────────────────────────────────

TEST_CASE("ConstructionYardResolver: All structure IDs up to Structure_LastID are handled",
          "[city][resolver][regression]") {
    // Every ID from Structure_FirstID to Structure_LastID should appear as a
    // case in resolveItemName.  This test uses data.h constants so it will
    // fail if someone adds a new Structure_* without updating the resolver.
    std::string sand = readFile("src/sand.cpp");
    REQUIRE_FALSE(sand.empty());

    std::string nameBlock = extractSwitchBlock(sand, "resolveItemName(");
    REQUIRE_FALSE(nameBlock.empty());

    // Build a list of every Structure_ enum name from data.h source
    std::string dataH = readFile("include/data.h");
    REQUIRE_FALSE(dataH.empty());

    // Parse Structure_ enum entries from data.h
    std::vector<std::string> structureEnums;
    std::string::size_type pos = 0;
    while ((pos = dataH.find("Structure_", pos)) != std::string::npos) {
        // Skip Structure_FirstID, Structure_LastID — these are range markers
        auto end = dataH.find_first_of(" \t=,\n\r();/", pos);
        std::string name = dataH.substr(pos, end - pos);
        if (name != "Structure_FirstID" && name != "Structure_LastID" && name != "Structure_ExtLastID") {
            structureEnums.push_back(name);
        }
        pos = end;
    }

    // De-duplicate (the enum name appears in both typedef and inline helpers)
    std::sort(structureEnums.begin(), structureEnums.end());
    structureEnums.erase(std::unique(structureEnums.begin(), structureEnums.end()),
                         structureEnums.end());

    REQUIRE(structureEnums.size() >= 5);  // at minimum the DuneCity items

    for (const auto& name : structureEnums) {
        INFO("resolveItemName() must handle " << name);
        REQUIRE(nameBlock.find(name) != std::string::npos);
    }
}
