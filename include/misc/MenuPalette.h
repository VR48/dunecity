#ifndef DUNECITY_MENU_PALETTE_H
#define DUNECITY_MENU_PALETTE_H

#include <Colors.h>

inline constexpr int validatedMenuPalette(int value) {
    return value == 1 ? 1 : 0;
}

struct MenuPalette {
    Uint32 foreground;
    Uint32 shadow;
};

inline constexpr MenuPalette menuPalette(int value) {
    return validatedMenuPalette(value) == 1
        ? MenuPalette{COLOR_BLACK, COLOR_TRANSPARENT}
        : MenuPalette{COLOR_RGB(83, 37, 6), COLOR_DESERTSAND};
}

#endif
