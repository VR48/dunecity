#include <catch2/catch_test_macros.hpp>
#include <misc/DrawingRectHelper.h>
#include <cstdlib>

TEST_CASE("Renderer dimensions follow the active target and restore the window layout", "[rendering]") {
    sdl2::surface_ptr surface{SDL_CreateRGBSurfaceWithFormat(0, 1280, 800, 32, SDL_PIXELFORMAT_RGBA32)};
    REQUIRE(surface);
    // Opt into the native HiDPI backend locally; software rendering also runs
    // on headless CI, but does not reproduce sdl2-compat's output-size bug.
    const bool native = std::getenv("DUNECITY_RENDERER_TEST_GPU") != nullptr;
    struct VideoSubsystem {
        bool active;
        ~VideoSubsystem() { if(active) SDL_QuitSubSystem(SDL_INIT_VIDEO); }
    } video{native};
    if(native) REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        native ? SDL_CreateWindow("Renderer size regression", 0, 0, 1280, 800,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI) : nullptr,
        SDL_DestroyWindow};
    if(native) REQUIRE(window);
    sdl2::renderer_ptr testRenderer{native
        ? SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE)
        : SDL_CreateSoftwareRenderer(surface.get())};
    REQUIRE(testRenderer);
    struct RestoreRenderer {
        SDL_Renderer* previous = renderer;
        ~RestoreRenderer() { renderer = previous; }
    } restoreRenderer;
    renderer = testRenderer.get();

    int outputWidth = 0, outputHeight = 0;
    REQUIRE(SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) == 0);
    REQUIRE(getRendererWidth() == outputWidth);
    REQUIRE(getRendererHeight() == outputHeight);
    REQUIRE(SDL_RenderSetLogicalSize(renderer, 960, 600) == 0);
    REQUIRE(getRendererWidth() == 960);
    REQUIRE(getRendererHeight() == 600);

    sdl2::texture_ptr target{SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_TARGET, 960, 600)};
    REQUIRE(target);
    REQUIRE(SDL_SetRenderTarget(renderer, target.get()) == 0);
    REQUIRE(getRendererWidth() == 960);
    REQUIRE(getRendererHeight() == 600);
    // The rightmost credits glyph must remain inside the sidebar and target.
    const int creditsRight = getRendererWidth() - 144 + 49 + 5 * 10 + 8;
    REQUIRE(creditsRight == 923);
    REQUIRE(SDL_SetRenderTarget(renderer, nullptr) == 0);
    REQUIRE(getRendererWidth() == 960);
    REQUIRE(getRendererHeight() == 600);
}
