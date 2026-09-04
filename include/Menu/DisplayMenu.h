#ifndef DUNECITY_DISPLAY_MENU_H
#define DUNECITY_DISPLAY_MENU_H

#include <Menu/MenuBase.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <array>

class DisplayMenu final : public MenuBase {
public:
    DisplayMenu();
private:
    void select(int height);
    void apply();
    StaticContainer content;
    Label title;
    std::array<TextButton, 4> choices;
    TextButton cancelButton;
    TextButton applyButton;
    int selectedHeight;
};

#endif
