#include <Menu/DisplayMenu.h>
#include <Menu/OptionsMenu.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <globals.h>
#include <main.h>
#include <GUI/MsgBox.h>

DisplayMenu::DisplayMenu() : selectedHeight(settings.video.interfaceHeight) {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getRendererWidth(), getRendererHeight());
    setWindowWidget(&content);
    const int left = (getSize().x - 440) / 2;
    const int top = (getSize().y - 360) / 2;
    title.setText("DISPLAY / INTERFACE SIZE");
    title.setTextFontSize(22);
    content.addWidget(&title, Point(left, top), Point(440, 40));
    const char* labels[] = {"Large - 640 x 480", "Medium - 800 x 600",
                            "Small - 1024 x 768", "Automatic"};
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) {
        choices[i].setText(labels[i]);
        choices[i].setToggleButton(true);
        choices[i].setOnClick([this, height = heights[i]] { select(height); });
        content.addWidget(&choices[i], Point(left, top + 52 + i * 54), Point(440, 44));
    }
#ifdef __ANDROID__
    choices[3].setVisible(false);
    choices[3].setEnabled(false);
#endif
    select(selectedHeight);
    cancelButton.setText("CANCEL");
    cancelButton.setOnClick([this] { quit(); });
    content.addWidget(&cancelButton, Point(left, top + 304), Point(210, 44));
    applyButton.setText("APPLY");
    applyButton.setOnClick([this] { apply(); });
    content.addWidget(&applyButton, Point(left + 230, top + 304), Point(210, 44));
}

void DisplayMenu::select(int height) {
    selectedHeight = height;
    const int heights[] = {480, 600, 768, 0};
    for(int i = 0; i < 4; ++i) choices[i].setToggleState(height == heights[i]);
}

void DisplayMenu::apply() {
    if(selectedHeight == settings.video.interfaceHeight) { quit(); return; }
    INIFile config(getConfigFilepath());
    config.setIntValue("Video", "Interface Height", selectedHeight);
    if(!config.saveChangesTo(getConfigFilepath())) {
        openWindow(MsgBox::create(_("Could not save display settings.")));
        return;
    }
    settings.video.interfaceHeight = selectedHeight;
    quit(MENU_QUIT_REINITIALIZE);
}
