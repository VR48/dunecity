#include <Menu/DisplayMenu.h>
#include <Menu/OptionsMenu.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <globals.h>
#include <main.h>
#include <GUI/MsgBox.h>
#include <misc/MenuLayout.h>

DisplayMenu::DisplayMenu()
    : selectedHeight(settings.video.interfaceHeight),
      selectedWidescreen(
          settings.video.interfaceHeight > 0
          && settings.video.width == interfaceWidthForHeight(settings.video.interfaceHeight, true)),
      selectedLayout(validatedStartMenuMode(settings.video.startMenuMode)) {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getRendererWidth(), getRendererHeight());
    setWindowWidget(&content);
    const int left = (getSize().x - 440) / 2;
    const int top = (getSize().y - 430) / 2;
    title.setText("DISPLAY");
    title.setTextFontSize(22);
    title.setAlignment(Alignment_HCenter);
    content.addWidget(&title, Point(left, top), Point(440, 34));

    const char* layoutLabels[] = {"CLASSIC", "ENLARGED"};
    for(int i = 0; i < 2; ++i) {
        layoutChoices[i].setText(layoutLabels[i]);
        layoutChoices[i].setToggleButton(true);
        layoutChoices[i].setOnClick([this, i] { selectLayout(i); });
        content.addWidget(&layoutChoices[i], Point(left + i * 224, top + 42), Point(216, 44));
    }
    selectLayout(selectedLayout);

    aspectTitle.setText("SCREEN SHAPE");
    aspectTitle.setAlignment(Alignment_HCenter);
    content.addWidget(&aspectTitle, Point(left, top + 94), Point(440, 24));
    const char* aspectLabels[] = {"STANDARD 4:3", "WIDESCREEN 16:9"};
    for(int i = 0; i < 2; ++i) {
        aspectChoices[i].setText(aspectLabels[i]);
        aspectChoices[i].setToggleButton(true);
        aspectChoices[i].setOnClick([this, i] { selectAspect(i == 1); });
        content.addWidget(&aspectChoices[i], Point(left + i * 224, top + 122), Point(216, 40));
    }
    selectAspect(selectedWidescreen);

    sizeTitle.setText("INTERFACE SIZE");
    sizeTitle.setAlignment(Alignment_HCenter);
    content.addWidget(&sizeTitle, Point(left, top + 170), Point(440, 24));
    const char* labels[] = {"LARGE", "MEDIUM", "SMALL", "AUTOMATIC"};
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) {
        choices[i].setText(labels[i]);
        choices[i].setToggleButton(true);
        choices[i].setOnClick([this, height = heights[i]] { select(height); });
        if(i < 3) {
            content.addWidget(&choices[i], Point(left, top + 198 + i * 44), Point(440, 38));
        } else {
            content.addWidget(&choices[i], Point(left, top + 332), Point(440, 38));
        }
    }
#ifdef __ANDROID__
    choices[3].setVisible(false);
    choices[3].setEnabled(false);
#endif
    select(selectedHeight);
    cancelButton.setText("CANCEL");
    cancelButton.setOnClick([this] { quit(); });
    content.addWidget(&cancelButton, Point(left, top + 378), Point(210, 40));
    applyButton.setText("APPLY");
    applyButton.setOnClick([this] { apply(); });
    content.addWidget(&applyButton, Point(left + 230, top + 378), Point(210, 40));
}

void DisplayMenu::select(int height) {
    selectedHeight = height;
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) choices[i].setToggleState(height == heights[i]);
}

void DisplayMenu::selectAspect(bool widescreen) {
    selectedWidescreen = widescreen;
    for(int i = 0; i < 2; ++i) aspectChoices[i].setToggleState(selectedWidescreen == (i == 1));
}

void DisplayMenu::selectLayout(int mode) {
    selectedLayout = validatedStartMenuMode(mode);
    for(int i = 0; i < 2; ++i) layoutChoices[i].setToggleState(selectedLayout == i);
}

void DisplayMenu::apply() {
    const int selectedWidth = selectedHeight > 0
        ? interfaceWidthForHeight(selectedHeight, selectedWidescreen)
        : settings.video.width;
    if(selectedHeight == settings.video.interfaceHeight
       && (selectedHeight == 0 || selectedWidth == settings.video.width)
       && selectedLayout == settings.video.startMenuMode) { quit(); return; }
    INIFile config(getConfigFilepath());
    config.setIntValue("Video", "Interface Height", selectedHeight);
    if(selectedHeight > 0) {
        config.setIntValue("Video", "Width", selectedWidth);
        config.setIntValue("Video", "Height", selectedHeight);
    }
    config.setIntValue("Video", "Start Menu Mode", selectedLayout);
    if(!config.saveChangesTo(getConfigFilepath())) {
        openWindow(MsgBox::create(_("Could not save display settings.")));
        return;
    }
    settings.video.interfaceHeight = selectedHeight;
    if(selectedHeight > 0) {
        settings.video.width = selectedWidth;
        settings.video.height = selectedHeight;
    }
    settings.video.startMenuMode = selectedLayout;
    quit(MENU_QUIT_REINITIALIZE);
}
