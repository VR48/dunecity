#include <catch2/catch_test_macros.hpp>
#include <misc/MenuLayout.h>
#include <misc/MenuPalette.h>
#include <FileClasses/INIFile.h>

TEST_CASE("Start menus retain readable targets and clear artwork at supported resolutions", "[menu][display]") {
    for(const auto size : {SDL_Point{640,480}, SDL_Point{800,600}, SDL_Point{1024,768},
                           SDL_Point{960,540}, SDL_Point{1280,720}, SDL_Point{1920,1080}}) {
        for(int rows : {3,5}) {
            const StartMenuLayout layout{size.x, size.y, rows};
            REQUIRE(layout.artBounds().h >= 100);
            for(int i = 0; i < rows * 2; ++i) {
                const auto button = layout.button(i);
                REQUIRE(button.w >= 240);
                REQUIRE(button.h >= 40);
                REQUIRE(button.x >= 24);
                REQUIRE(button.x + button.w <= size.x - 24);
                REQUIRE(button.y >= layout.artBounds().y + layout.artBounds().h);
                REQUIRE(button.y + button.h <= size.y - 44);
                for(int j = 0; j < i; ++j) {
                    const auto other = layout.button(j);
                    REQUIRE_FALSE(SDL_HasIntersection(&button, &other));
                }
            }
        }
    }
}

TEST_CASE("Android preserves chosen interface size and repairs old native resolution values", "[menu][display]") {
    for(int size : {480,600,768}) {
        REQUIRE(validatedInterfaceHeight(size, true) == size);
        REQUIRE(validatedInterfaceHeight(size, false) == size);
    }
    for(int old : {0,-1,2000,2800}) {
        REQUIRE(validatedInterfaceHeight(old, true) == 480);
        REQUIRE(validatedInterfaceHeight(old, false) == 0);
    }
}

TEST_CASE("Desert menu colors default safely and high contrast remains opt in", "[menu][accessibility]") {
    const auto desert = menuPalette(0);
    REQUIRE(desert.foreground != COLOR_BLACK);
    REQUIRE(desert.shadow == COLOR_DESERTSAND);
    const auto contrast = menuPalette(1);
    REQUIRE(contrast.foreground == COLOR_BLACK);
    REQUIRE(contrast.shadow == COLOR_TRANSPARENT);
    for(int invalid : {-99, -1, 2, 100}) {
        REQUIRE(validatedMenuPalette(invalid) == 0);
        REQUIRE(menuPalette(invalid).foreground == desert.foreground);
    }
    INIFile config(true, "Menu color preference test");
    REQUIRE(validatedMenuPalette(config.getIntValue("Video", "Menu Palette", 0)) == 0);
    for(int selected : {1, 0}) {
        config.setIntValue("Video", "Menu Palette", selected);
        REQUIRE(validatedMenuPalette(config.getIntValue("Video", "Menu Palette", 0)) == selected);
    }
}

TEST_CASE("Classic start menus default safely and enlarged remains opt in", "[menu][accessibility]") {
    for(int invalid : {-99, -1, 2, 100}) {
        REQUIRE(validatedStartMenuMode(invalid) == 0);
    }
    REQUIRE(validatedStartMenuMode(0) == 0);
    REQUIRE(validatedStartMenuMode(1) == 1);

    INIFile config(true, "Start menu mode preference test");
    REQUIRE(validatedStartMenuMode(config.getIntValue("Video", "Start Menu Mode", 0)) == 0);
    for(int selected : {1, 0}) {
        config.setIntValue("Video", "Start Menu Mode", selected);
        REQUIRE(validatedStartMenuMode(config.getIntValue("Video", "Start Menu Mode", 0)) == selected);
    }
}
