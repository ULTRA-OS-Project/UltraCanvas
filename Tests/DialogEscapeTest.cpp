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
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
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

// A custom dialog like UltraMail's and UltraPassword's: no built-in buttons,
// its own fields in a column. `field` goes into the column.
Shown ShowCustomDialog(const std::shared_ptr<UltraCanvasUIElement>& field) {
    DialogConfig config;
    config.title      = "Escape test";
    config.width      = 360;
    config.height     = 220;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

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

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
