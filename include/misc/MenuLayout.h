#ifndef DUNECITY_MENU_LAYOUT_H
#define DUNECITY_MENU_LAYOUT_H

#include <SDL.h>
#include <algorithm>

inline int interfaceWidthForHeight(int height, bool widescreen) {
    if(!widescreen) return height * 4 / 3;
    switch(height) {
        case 480: return 854;
        case 600: return 1067;
        case 768: return 1366;
        default: return height * 16 / 9;
    }
}

inline bool isSupportedInterfaceResolution(int width, int height) {
    return (height == 480 || height == 600 || height == 768)
        && (width == interfaceWidthForHeight(height, false)
            || width == interfaceWidthForHeight(height, true));
}

inline int validatedInterfaceWidth(int width, int height) {
    if(isSupportedInterfaceResolution(width, height)) return width;
    return interfaceWidthForHeight(height, false);
}

inline constexpr int validatedStartMenuMode(int value) {
    return value == 1 ? 1 : 0;
}

// Enlarged start screens retain the original framed, single-column composition.
// Dimensions scale with the logical interface height so controls have a stable
// physical size across the 480, 600 and 768 presets.
struct StartMenuLayout {
    int width;
    int height;
    int buttonCount;

    int buttonHeight() const { return std::clamp(height * 28 / 480, 28, 46); }
    int gap() const { return std::clamp(height / 150, 4, 6); }
    int buttonWidth() const { return std::clamp(height * 220 / 480, 220, 360); }
    int bottomMargin() const { return std::clamp(height / 24, 20, 32); }
    int listHeight() const { return buttonCount * buttonHeight() + (buttonCount - 1) * gap(); }
    int top() const { return height - bottomMargin() - listHeight(); }
    SDL_Rect button(int index) const {
        return {(width - buttonWidth()) / 2,
                top() + index * (buttonHeight() + gap()), buttonWidth(), buttonHeight()};
    }
    SDL_Rect borderBounds() const {
        const auto first = button(0);
        return {first.x - 16, first.y - 12, first.w + 32, listHeight() + 24};
    }
    SDL_Rect artBounds() const {
        const int artWidth = std::min({height * 320 / 480, width - 80, 460});
        return {(width - artWidth) / 2, 16, artWidth, std::max(80, top() - 42)};
    }
    SDL_Rect planetBounds() const {
        auto bounds = artBounds();
        bounds.h -= std::clamp(height / 16, 28, 42);
        return bounds;
    }
    SDL_Rect logoBounds() const {
        const auto bounds = artBounds();
        const int logoHeight = std::clamp(height / 18, 26, 40);
        return {bounds.x + bounds.w / 8, bounds.y + bounds.h - logoHeight,
                bounds.w * 3 / 4, logoHeight};
    }
};

inline int validatedInterfaceHeight(int height, bool android) {
    if(height == 480 || height == 600 || height == 768) return height;
    return android ? 480 : 0;
}

#endif
