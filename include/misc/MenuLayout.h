#ifndef DUNECITY_MENU_LAYOUT_H
#define DUNECITY_MENU_LAYOUT_H

#include <SDL.h>
#include <algorithm>

inline constexpr int validatedStartMenuMode(int value) {
    return value == 1 ? 1 : 0;
}

// Start-screen geometry is independent of the display's physical pixel density.
struct StartMenuLayout {
    int width;
    int height;
    int rows;

    int buttonHeight() const { return std::clamp(height / 12, 40, 60); }
    int gap() const { return 8; }
    int columnWidth() const { return std::min((width - 80 - gap()) / 2, 340); }
    int top() const { return height - 44 - rows * (buttonHeight() + gap()); }
    SDL_Rect button(int index) const {
        const int left = (width - 2 * columnWidth() - gap()) / 2;
        return {left + (index % 2) * (columnWidth() + gap()),
                top() + (index / 2) * (buttonHeight() + gap()), columnWidth(), buttonHeight()};
    }
    SDL_Rect artBounds() const { return {(width - 300) / 2, 16, 300, top() - 30}; }
    SDL_Rect planetBounds() const {
        auto bounds = artBounds();
        bounds.h -= 34;
        return bounds;
    }
    SDL_Rect logoBounds() const {
        const auto bounds = artBounds();
        return {bounds.x + 40, bounds.y + bounds.h - 28, bounds.w - 80, 26};
    }
};

inline int validatedInterfaceHeight(int height, bool android) {
    if(height == 480 || height == 600 || height == 768) return height;
    return android ? 480 : 0;
}

#endif
