// Tests/DialogEscapeTest.cpp
// Escape cancels a dialog whatever field the caret is in.
//
// UltraCanvasModalDialog maps Escape to Cancel in its own OnEvent, which a
// key reaches only after the focused element and its parents have declined
// it. UltraCanvasTextArea did not decline: it took Escape and did nothing with
// it, so every custom dialog with a multi-line field - UltraMail's signature
// editor, UltraPassword's notes - stayed open on Escape while the caret was in
// that field, and closed on it from any other field.
//
// The key is routed the way the application routes a real one
// (HandleEventWithBubbling from the focused element), so the test covers the
// whole path, not just the text area's return value.
//
// Checkboxes, radios and switches take the keyboard focus now, so the same
// path decides what Space and Enter do on a focused one: Space activates it,
// and Enter reaches the dialog, whose default button it presses.
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.1.0 - Space and Enter on a focused checkbox in a dialog
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

UCEvent EscapeKey() {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = UCKeys::Escape;
    // X11 delivers Escape with its control character as text; it must not be
    // typed into the field on its way past.
    e.text = "\x1b";
    e.character = 0x1b;
    return e;
}

struct Shown {
    std::shared_ptr<UltraCanvasModalDialog> dialog;
    std::shared_ptr<DialogResult> result;
};

UCEvent Key(UCKeys key, const char* text) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.text = text;
    e.character = static_cast<unsigned char>(text[0]);
    return e;
}

// A custom dialog like UltraMail's and UltraPassword's: its own fields in a
// column and no built-in buttons. With `buttons` it is a Question dialog
// instead, whose button row carries them (a Custom one builds none). `field`
// goes into the column.
Shown ShowCustomDialog(const std::shared_ptr<UltraCanvasUIElement>& field,
                       DialogButtons buttons = DialogButtons::NoButtons) {
    DialogConfig config;
    config.title      = "Escape test";
    config.width      = 360;
    config.height     = 220;
    config.dialogType = buttons == DialogButtons::NoButtons ? DialogType::Custom : DialogType::Question;
    config.buttons    = buttons;

    Shown shown;
    shown.result = std::make_shared<DialogResult>(DialogResult::NoResult);
    shown.dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto column = CreateContainer("column", 0, 0, 0, 0);
    column->layout.SetFlexColumn();
    column->AddChild(field);
    shown.dialog->AddChild(column);
    auto result = shown.result;
    UltraCanvasDialogManager::ShowDialog(
        shown.dialog, [result](DialogResult r) { *result = r; }, nullptr);
    shown.dialog->SetFocusedElement(field.get());
    return shown;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Dialog Escape Suite"                    << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("DialogEscapeTest")) SKIP_ALL("application would not initialise");
    UltraCanvasDialogManager::SetUseNativeDialogs(false);

    std::cerr << "\n--- Caret in a multi-line field ---" << std::endl;
    {
        auto notes = std::make_shared<UltraCanvasTextArea>("notes", 0, 0, 300, 80);
        notes->SetText("keep me", false);
        TEST("The text area declines Escape", !notes->OnEvent(EscapeKey()));
        TEST("and types nothing for it", notes->GetText() == "keep me");

        Shown shown = ShowCustomDialog(notes);
        app.HandleEventWithBubbling(notes.get(), EscapeKey());
        TEST("Escape from the text area cancels the dialog",
             *shown.result == DialogResult::Cancel);
        TEST("and leaves its text alone", notes->GetText() == "keep me");
    }

    std::cerr << "\n--- Caret in a single-line field (unchanged) ---" << std::endl;
    {
        auto title = CreateTextInput("title", 0, 0, 300, 24);
        title->SetText("keep me too");
        Shown shown = ShowCustomDialog(title);
        app.HandleEventWithBubbling(title.get(), EscapeKey());
        TEST("Escape from a text input cancels the dialog",
             *shown.result == DialogResult::Cancel);
        TEST("and leaves its text alone", title->GetText() == "keep me too");
    }

    std::cerr << "\n--- A focused checkbox: Space toggles, Enter is the dialog's ---" << std::endl;
    {
        auto remember = std::make_shared<UltraCanvasCheckbox>("remember", 0, 0, 200, 24, "Remember me");
        Shown shown = ShowCustomDialog(remember, DialogButtons::OKCancel);
        TEST("The checkbox can take the dialog's focus",
             shown.dialog->GetFocusedElement() == remember.get());
        if (!remember->IsFocused()) {
            // IsFocused() also needs the dialog to be the focused window,
            // which the window manager decides; give it the focus as a real
            // FocusIn would.
            UCEvent focus;
            focus.type = UCEventType::WindowFocus;
            focus.targetWindow = shown.dialog;
            app.DispatchEvent(focus);
        }
        TEST("and is focused", remember->IsFocused());
        app.HandleEventWithBubbling(remember.get(), Key(UCKeys::Space, " "));
        TEST("Space checks it", remember->IsChecked());
        TEST("and leaves the dialog open", *shown.result == DialogResult::NoResult);
        app.HandleEventWithBubbling(remember.get(), Key(UCKeys::Return, "\r"));
        TEST("Enter presses the dialog's default button", *shown.result == DialogResult::OK);
        TEST("and does not toggle the checkbox", remember->IsChecked());
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
