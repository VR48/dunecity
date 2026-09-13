#ifndef DUNECITY_FEEDBACK_WINDOW_H
#define DUNECITY_FEEDBACK_WINDOW_H

#include <GUI/GUIStyle.h>
#include <GUI/Window.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextBox.h>
#include <GUI/TextView.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>

// Small multiline editor for feedback. Text stays local until Open GitHub is clicked.
class FeedbackEditor : public TextView {
public:
    const std::string& value() const { return contents; }
    bool handleMouseLeft(Sint32 x, Sint32 y, bool pressed) override;
    bool handleKeyPress(SDL_KeyboardEvent& key) override;
    bool handleTextInput(SDL_TextInputEvent& event) override;
    void setActive() override;
    void setInactive() override;
protected:
    void setActive(bool active) override;
private:
    void insert(const std::string& text);
    void refresh();
    std::string contents;
    size_t cursor = 0;
    bool selectAll = false;
};

class FeedbackWindow final : public Window {
public:
    FeedbackWindow();
    ~FeedbackWindow() override;
    bool handleKeyPress(SDL_KeyboardEvent& key) override;
private:
    void openGitHub();
    StaticContainer layout;
    Label heading, summaryLabel, detailsLabel, helpLabel, statusLabel;
    TextBox summary;
    FeedbackEditor details;
    TextView contextView;
    TextButton openButton, closeButton;
    std::string context;
    bool textInputWasActive = false;
};
#endif
