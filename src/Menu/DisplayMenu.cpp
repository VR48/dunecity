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
      selectedLayout(validatedStartMenuMode(settings.video.startMenuMode)) {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getRendererWidth(), getRendererHeight());
    setWindowWidget(&content);
    const int left = (getSize().x - 440) / 2;
    const int top = (getSize().y - 376) / 2;
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

    sizeTitle.setText("INTERFACE SIZE");
    sizeTitle.setAlignment(Alignment_HCenter);
    content.addWidget(&sizeTitle, Point(left, top + 94), Point(440, 28));
    const char* labels[] = {"Large - 640 x 480", "Medium - 800 x 600",
                            "Small - 1024 x 768", "Automatic"};
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) {
        choices[i].setText(labels[i]);
        choices[i].setToggleButton(true);
        choices[i].setOnClick([this, height = heights[i]] { select(height); });
        content.addWidget(&choices[i], Point(left, top + 126 + i * 46), Point(440, 40));
    }
#ifdef __ANDROID__
    choices[3].setVisible(false);
    choices[3].setEnabled(false);
#endif
    select(selectedHeight);
    cancelButton.setText("CANCEL");
    cancelButton.setOnClick([this] { quit(); });
    content.addWidget(&cancelButton, Point(left, top + 320), Point(210, 44));
    applyButton.setText("APPLY");
    applyButton.setOnClick([this] { apply(); });
    content.addWidget(&applyButton, Point(left + 230, top + 320), Point(210, 44));
}

void DisplayMenu::select(int height) {
    selectedHeight = height;
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) choices[i].setToggleState(height == heights[i]);
}

void DisplayMenu::selectLayout(int mode) {
    selectedLayout = validatedStartMenuMode(mode);
    for(int i = 0; i < 2; ++i) layoutChoices[i].setToggleState(selectedLayout == i);
}

void DisplayMenu::apply() {
    if(selectedHeight == settings.video.interfaceHeight
       && selectedLayout == settings.video.startMenuMode) { quit(); return; }
    INIFile config(getConfigFilepath());
    config.setIntValue("Video", "Interface Height", selectedHeight);
    config.setIntValue("Video", "Start Menu Mode", selectedLayout);
    if(!config.saveChangesTo(getConfigFilepath())) {
        openWindow(MsgBox::create(_("Could not save display settings.")));
        return;
    }
    settings.video.interfaceHeight = selectedHeight;
    settings.video.startMenuMode = selectedLayout;
    quit(MENU_QUIT_REINITIALIZE);
}
