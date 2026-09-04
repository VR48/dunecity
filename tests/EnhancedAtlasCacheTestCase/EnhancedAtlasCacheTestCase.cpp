#include <catch2/catch_test_macros.hpp>
#include <FileClasses/EnhancedAtlasCache.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/lodepng.h>
#include <misc/EnhancedBuildingGeometry.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <iostream>
#include <algorithm>

namespace {
SDL_Texture* awaitTexture(EnhancedAtlasCache& cache, const std::string& path, int width, int height) {
    const auto start = SDL_GetTicks();
    do {
        if(auto* texture = cache.request(path, width, height)) {
            return texture;
        }
        SDL_Delay(2);
    } while(SDL_GetTicks() - start < 10000);
    return nullptr;
}

struct Fixture {
    sdl2::surface_ptr surface{SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32)};
    sdl2::renderer_ptr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("dune2r-atlas-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Fixture() { std::filesystem::create_directories(root); }
    ~Fixture() { std::filesystem::remove_all(root); }
    std::string png(const char* name, int width, int height) {
        std::vector<unsigned char> rgba(size_t(width) * height * 4, 255);
        const auto path = (root / name).string();
        REQUIRE(lodepng_encode32_file(path.c_str(), rgba.data(), width, height) == 0);
        return path;
    }
};
}

TEST_CASE("Dune2R atlas decode is asynchronous and rectangular", "[dune2r][atlas]") {
    Fixture f;
    REQUIRE(f.renderer);
    EnhancedAtlasCache cache(f.renderer.get());
    const auto path = f.png("rectangle.png", 12, 19);
    REQUIRE(cache.request(path, 12, 19) == nullptr);
    auto* texture = awaitTexture(cache, path, 12, 19);
    REQUIRE(texture);
    int width = 0, height = 0;
    REQUIRE(SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) == 0);
    REQUIRE(width == 12);
    REQUIRE(height == 19);
    REQUIRE(SDL_RenderCopy(f.renderer.get(), texture, nullptr, nullptr) == 0);
    SDL_RenderPresent(f.renderer.get());
    const auto* pixels = static_cast<const Uint8*>(f.surface->pixels);
    REQUIRE(pixels[0] == 255);
    REQUIRE(pixels[3] == 255);
}

TEST_CASE("Dune2R malformed atlas fails once and mount reload retries", "[dune2r][atlas]") {
    Fixture f;
    EnhancedAtlasCache cache(f.renderer.get());
    const auto path = f.png("bad.png", 12, 12);
    REQUIRE(cache.request(path, 12, 19) == nullptr);
    for(int i = 0; i < 100; ++i) {
        SDL_Delay(2);
        REQUIRE(cache.request(path, 12, 19) == nullptr);
    }
    f.png("bad.png", 12, 19);
    REQUIRE(cache.request(path, 12, 19) == nullptr);
    cache.clear();
    REQUIRE(awaitTexture(cache, path, 12, 19));
}

TEST_CASE("Dune2R atlas residency is bounded and survives cache invalidation", "[dune2r][atlas]") {
    Fixture f;
    EnhancedAtlasCache cache(f.renderer.get(), 128);
    const auto a = f.png("a.png", 4, 4);
    const auto b = f.png("b.png", 4, 4);
    const auto c = f.png("c.png", 4, 4);
    REQUIRE(awaitTexture(cache, a, 4, 4));
    REQUIRE(awaitTexture(cache, b, 4, 4));
    REQUIRE(awaitTexture(cache, c, 4, 4));
    REQUIRE(cache.residentBytes() == 128);
    REQUIRE(awaitTexture(cache, a, 4, 4));
    REQUIRE(cache.residentBytes() <= 128);
    cache.clear();
    REQUIRE(cache.residentBytes() == 0);
    cache.request(a, 4, 4);
    cache.clear();
    REQUIRE(awaitTexture(cache, b, 4, 4));
    REQUIRE(cache.residentBytes() == 64);
}

TEST_CASE("Dune2R packaged Refinery pages decode and draw", "[dune2r][atlas][payload]") {
    const char* directory = std::getenv("DUNE2R_ATLAS_TEST_ROOT");
    if(!directory) {
        SKIP("Set DUNE2R_ATLAS_TEST_ROOT to test a packaged Refinery");
    }
    Fixture f;
    EnhancedAtlasCache cache(f.renderer.get(), 32ull * 1024 * 1024);
    const auto root = std::filesystem::path(directory);
    INIFile manifest((root / "building.ini").string());
    REQUIRE(manifest.getIntValue("Building", "HouseID", -1) == 1);
    for(const auto* state : {"Placement", "Construction", "Idle", "Working", "Damaged", "Repair", "Destroyed"}) {
        const auto section = std::string("State.") + state;
        const int width = manifest.getIntValue(section, "FrameWidth", 0);
        const int height = manifest.getIntValue(section, "FrameHeight", 0);
        const int count = manifest.getIntValue(section, "AtlasCount", 0);
        REQUIRE(count > 0);
        for(int i = 0; i < count; ++i) {
            const auto suffix = std::to_string(i);
            const auto path = root / manifest.getStringValue(section, "Atlas." + suffix, "");
            const int cols = manifest.getIntValue(section, "Columns." + suffix, 0);
            const int rows = manifest.getIntValue(section, "Rows." + suffix, 0);
            auto* texture = awaitTexture(cache, path.string(), cols * width, rows * height);
            INFO(path.string());
            REQUIRE(texture);
            SDL_Rect frame{0, 0, width, height};
            REQUIRE(SDL_RenderCopy(f.renderer.get(), texture, &frame, nullptr) == 0);
            SDL_RenderPresent(f.renderer.get());
            REQUIRE(cache.residentBytes() <= 32ull * 1024 * 1024);
        }
        const auto path = root / manifest.getStringValue(section, "Still", "");
        REQUIRE(awaitTexture(cache, path.string(), manifest.getIntValue(section, "StillWidth", 0),
                             manifest.getIntValue(section, "StillHeight", 0)));
    }
}

TEST_CASE("Dune2R Refinery playback stays responsive on the native renderer", "[dune2r][atlas][gpu]") {
    const char* directory = std::getenv("DUNE2R_ATLAS_TEST_ROOT");
    if(!directory || !std::getenv("DUNE2R_GPU_TEST")) {
        SKIP("Set DUNE2R_ATLAS_TEST_ROOT and DUNE2R_GPU_TEST for hidden-window GPU playback");
    }
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>{
        SDL_CreateWindow("Refinery render regression", 0, 0, 576, 640, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
    REQUIRE(window);
    auto renderer = sdl2::renderer_ptr{SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED)};
    REQUIRE(renderer);
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(renderer.get(), &info);
    EnhancedAtlasCache cache(renderer.get());
    const auto root = std::filesystem::path(directory);
    INIFile manifest((root / "building.ini").string());
    const std::string section = "State.Idle";
    const int width = manifest.getIntValue(section, "FrameWidth", 0);
    const int height = manifest.getIntValue(section, "FrameHeight", 0);
    const int count = manifest.getIntValue(section, "AtlasCount", 0);
    const int frames = manifest.getIntValue(section, "Frames", 0);
    const int frameMs = manifest.getIntValue(section, "FrameMs", 42);
    const int footprintWidth = manifest.getIntValue("Building", "FootprintWidth", 0);
    const int footprintHeight = manifest.getIntValue("Building", "FootprintHeight", 0);
    const SDL_Point imageAnchor{manifest.getIntValue(section, "AnchorX", 0),
                                manifest.getIntValue(section, "AnchorY", 0)};
    constexpr unsigned int zoom = 2;
    constexpr int tilePixels = D2_TILESIZE * (zoom + 1);
    const SDL_Point screenAnchor{288 + tilePixels / 2, 384};
    const SDL_Rect footprint{screenAnchor.x - footprintWidth * tilePixels / 2,
                             screenAnchor.y - footprintHeight * tilePixels,
                             footprintWidth * tilePixels, footprintHeight * tilePixels};
    struct Page { std::string path; int first; int frames; int cols; int rows; };
    std::vector<Page> pages;
    for(int i = 0; i < count; ++i) {
        const auto suffix = std::to_string(i);
        pages.push_back({(root / manifest.getStringValue(section, "Atlas." + suffix, "")).string(),
            manifest.getIntValue(section, "FirstFrame." + suffix, 0),
            manifest.getIntValue(section, "ChunkFrames." + suffix, 0),
            manifest.getIntValue(section, "Columns." + suffix, 0),
            manifest.getIntValue(section, "Rows." + suffix, 0)});
    }
    double worstRequestMs = 0;
    int animated = 0, missesSecondLoop = 0;
    uint64_t firstHash = 0;
    bool changed = false;
    auto canvas = sdl2::surface_ptr{SDL_CreateRGBSurfaceWithFormat(0, 576, 640, 32, SDL_PIXELFORMAT_RGBA32)};
    REQUIRE(canvas);
    for(int tick = 0; tick < frames * 2; ++tick) {
        const Uint32 start = SDL_GetTicks();
        const int frame = tick % frames;
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto& p) {
            return frame >= p.first && frame < p.first + p.frames;
        });
        REQUIRE(found != pages.end());
        const auto requestStart = SDL_GetPerformanceCounter();
        auto* texture = cache.request(found->path, found->cols * width, found->rows * height);
        worstRequestMs = std::max(worstRequestMs, 1000.0 * (SDL_GetPerformanceCounter() - requestStart) / SDL_GetPerformanceFrequency());
        SDL_SetRenderDrawColor(renderer.get(), 30, 30, 30, 255);
        SDL_RenderClear(renderer.get());
        SDL_SetRenderDrawColor(renderer.get(), 65, 65, 65, 255);
        for(int x = 0; x < 576; x += tilePixels) SDL_RenderDrawLine(renderer.get(), x, 0, x, 639);
        for(int y = 0; y < 640; y += tilePixels) SDL_RenderDrawLine(renderer.get(), 0, y, 575, y);
        if(texture) {
            SDL_Rect source{((frame - found->first) % found->cols) * width,
                            ((frame - found->first) / found->cols) * height, width, height};
            const SDL_Rect dest = calcEnhancedBuildingDrawingRect(
                footprintWidth, zoom, {width, height}, imageAnchor, screenAnchor);
            REQUIRE(dest.w == footprint.w);
            REQUIRE(dest.x == footprint.x);
            REQUIRE(dest.y + dest.h == footprint.y + footprint.h);
            REQUIRE(SDL_RenderCopy(renderer.get(), texture, &source, &dest) == 0);
            SDL_SetRenderDrawColor(renderer.get(), 220, 190, 70, 255);
            SDL_RenderDrawRect(renderer.get(), &footprint);
            ++animated;
            if(frame % 10 == 0) {
                REQUIRE(SDL_RenderReadPixels(renderer.get(), nullptr, SDL_PIXELFORMAT_RGBA32, canvas->pixels, canvas->pitch) == 0);
                uint64_t hash = 14695981039346656037ull;
                const auto* pixels = static_cast<const Uint8*>(canvas->pixels);
                for(int i = 0; i < canvas->pitch * canvas->h; ++i) { hash ^= pixels[i]; hash *= 1099511628211ull; }
                if(firstHash == 0) firstHash = hash;
                else changed |= firstHash != hash;
                if(const auto* capture = std::getenv("DUNE2R_ATLAS_CAPTURE")) {
                    REQUIRE(SDL_SaveBMP(canvas.get(), capture) == 0);
                }
            }
            const auto& next = pages[(std::distance(pages.begin(), found) + 1) % pages.size()];
            cache.request(next.path, next.cols * width, next.rows * height);
        } else if(tick >= frames) {
            ++missesSecondLoop;
        }
        SDL_RenderPresent(renderer.get());
        const auto elapsed = SDL_GetTicks() - start;
        if(elapsed < static_cast<Uint32>(frameMs)) SDL_Delay(frameMs - elapsed);
    }
    std::cout << "Refinery GPU playback: renderer=" << info.name << " animated=" << animated << "/" << frames * 2
              << " second-loop-misses=" << missesSecondLoop << " worst-request-ms=" << worstRequestMs
              << " resident-MiB=" << cache.residentBytes() / (1024.0 * 1024.0) << '\n';
    CHECK(animated > frames);
    CHECK(missesSecondLoop == 0);
    CHECK(changed);
    CHECK(worstRequestMs < 500.0);
}
