#include <catch2/catch_test_macros.hpp>

#include <mod/Dune2RAssetManager.h>
#include <Network/ENetHttp.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>

TEST_CASE("Dune2R asset paths reject traversal", "[Dune2RAssets]") {
    CHECK(Dune2RAssetManager::isSafeRelativeAssetPath("atlases/idle/east.png"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("../unit.ini"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("/unit.ini"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("atlases\\east.png"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("atlases/east file.png"));
}

TEST_CASE("Dune2R asset SHA-256 matches the standard vector", "[Dune2RAssets]") {
    const auto path = std::filesystem::temp_directory_path() / "dunecity-sha256-test.txt";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    CHECK(Dune2RAssetManager::sha256File(path.string())
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("Dune2R source catalog loads immutable packs", "[Dune2RAssets]") {
    const char* source = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(source != nullptr);
    const auto modPath = std::filesystem::path(source) / "mods" / "Dune2R";
    Dune2RAssetManager manager(modPath.string());
    REQUIRE(manager.getRevision().size() == 40);
    REQUIRE(manager.getRevision().find_first_not_of("0123456789abcdef") == std::string::npos);
    for(const auto* id : {"gravel", "refinery", "harkonnendevastator", "ordostank"}) {
        const auto& packs = manager.getPacks();
        const auto found = std::find_if(packs.begin(), packs.end(), [&](const auto& pack) { return pack.id == id; });
        REQUIRE(found != packs.end());
        CHECK(found->variant == "remastered");
        CHECK(found->totalBytes() > 0);
        if(found->id == "refinery") {
            CHECK(found->displayName == "Atreides Refinery Remastered");
        }
    }
}

TEST_CASE("Dune2R downloader resumes and verifies a live asset", "[Dune2RAssets][network]") {
    const char* enabled = std::getenv("DUNE2R_NETWORK_TEST");
    if(enabled == nullptr || std::string(enabled) != "1") {
        SKIP("Set DUNE2R_NETWORK_TEST=1 to run the live GitHub download check");
    }

    const char* source = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(source != nullptr);
    const std::string revision = "1275ed1036828b8f3365395c66ca4499cd2d266a";
    const std::string baseURL = "https://raw.githubusercontent.com/VR48/dunecity/" + revision
                                + "/mods/Dune2R/graphics_hd/units";
    const std::string remoteData = loadFromHttp(
        baseURL + "/harkonnendevastator/unit.ini");
    REQUIRE(remoteData.size() > 64);

    const auto remoteCopy = std::filesystem::temp_directory_path()
                            / "dunecity-dune2r-network-source.ini";
    {
        std::ofstream output(remoteCopy, std::ios::binary | std::ios::trunc);
        output.write(remoteData.data(), static_cast<std::streamsize>(remoteData.size()));
    }
    const uint64_t sourceSize = remoteData.size();
    const std::string sourceHash = Dune2RAssetManager::sha256File(remoteCopy.string());

    const auto root = std::filesystem::temp_directory_path() / "dunecity-dune2r-network-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream catalog(root / "asset-catalog.ini", std::ios::trunc);
        catalog << "[Catalog]\nSchema=1\nRevision=" << revision << "\n"
                << "BaseURL=" << baseURL << "\nPackCount=1\n\n"
                << "[Pack.0]\nID=smoke\nDisplayName=Network Smoke Test\n"
                << "Variant=remastered\nUnit=harkonnendevastator\nFileCount=1\n"
                << "File.0=unit.ini|" << sourceSize << "|" << sourceHash << "\n";
    }

    const auto partial = root / "graphics_hd" / "units"
                         / "harkonnendevastator.download" / "unit.ini.part";
    std::filesystem::create_directories(partial.parent_path());
    {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        output.write(remoteData.data(), 64);
    }

    Dune2RAssetManager manager(root.string());
    const auto result = manager.install({"smoke"});
    INFO(result.message);
    REQUIRE(result.success);
    REQUIRE(result.changed);
    REQUIRE(manager.isPackInstalled(manager.getPacks().front()));
    std::filesystem::remove_all(root);
    std::filesystem::remove(remoteCopy);
}
