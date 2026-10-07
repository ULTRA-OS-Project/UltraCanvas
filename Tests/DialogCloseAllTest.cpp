// Tests/DialogCloseAllTest.cpp
// UltraCanvasDialogManager::CloseAllDialogs() cancels every open dialog.
//
// It walked activeDialogs while each CloseDialog() erased that dialog from the
// same vector (PerformClose unregisters it): each erase moved the next dialog
// under the loop's iterator, so every second one stayed open, and the loop went
// on to read the vacated slots past the vector's end. The clear() after it then
// unregistered a dialog a result callback had opened meanwhile, leaving it on
// screen where no later CloseAllDialogs() could reach it. Apps call it to lock
// or reset (UltraPassword's Lock()); it went unnoticed because none of them did
// so with more than one dialog open. Without the fix three checks here fail.
//
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

struct Shown {
    std::shared_ptr<UltraCanvasModalDialog> dialog;
    std::shared_ptr<DialogResult> result;
};

// A custom dialog like UltraPassword's: no built-in buttons, one field.
// `onClosed` runs from its result callback, after the result is recorded.
Shown ShowCustomDialog(const std::string& name, std::function<void()> onClosed = nullptr) {
    DialogConfig config;
    config.title      = name;
    config.width      = 320;
    config.height     = 160;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    Shown shown;
    shown.result = std::make_shared<DialogResult>(DialogResult::NoResult);
    shown.dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto column = CreateContainer(name + "Column", 0, 0, 0, 0);
    column->layout.SetFlexColumn();
    column->AddChild(CreateTextInput(name + "Field", 0, 0, 200, 24));
    shown.dialog->AddChild(column);
    auto result = shown.result;
    UltraCanvasDialogManager::ShowDialog(
        shown.dialog,
        [result, onClosed](DialogResult r) {
            *result = r;
            if (onClosed) onClosed();
        },
        nullptr);
    return shown;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Dialog CloseAll Suite"                  << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("DialogCloseAllTest")) SKIP_ALL("application would not initialise");
    UltraCanvasDialogManager::SetUseNativeDialogs(false);

    std::cerr << "\n--- Nothing open ---" << std::endl;
    UltraCanvasDialogManager::CloseAllDialogs();
    TEST("Closing no dialogs is harmless", UltraCanvasDialogManager::GetActiveDialogCount() == 0);

    std::cerr << "\n--- One dialog ---" << std::endl;
    {
        Shown one = ShowCustomDialog("one");
        TEST("It is registered", UltraCanvasDialogManager::GetActiveDialogCount() == 1);
        UltraCanvasDialogManager::CloseAllDialogs();
        TEST("It is cancelled", *one.result == DialogResult::Cancel);
        TEST("and none is left", UltraCanvasDialogManager::GetActiveDialogCount() == 0);
    }

    std::cerr << "\n--- Three dialogs ---" << std::endl;
    {
        std::vector<Shown> shown = {ShowCustomDialog("first"), ShowCustomDialog("second"),
                                    ShowCustomDialog("third")};
        TEST("All three are registered", UltraCanvasDialogManager::GetActiveDialogCount() == 3);
        UltraCanvasDialogManager::CloseAllDialogs();
        TEST("The first is cancelled", *shown[0].result == DialogResult::Cancel);
        TEST("the second too (it used to be skipped)", *shown[1].result == DialogResult::Cancel);
        TEST("and the third", *shown[2].result == DialogResult::Cancel);
        TEST("none is left", UltraCanvasDialogManager::GetActiveDialogCount() == 0);
    }

    std::cerr << "\n--- A result callback opens a dialog ---" << std::endl;
    {
        Shown followUp;
        Shown opener = ShowCustomDialog("opener", [&followUp]() {
            followUp = ShowCustomDialog("followUp");
        });
        UltraCanvasDialogManager::CloseAllDialogs();
        TEST("The open dialog is cancelled", *opener.result == DialogResult::Cancel);
        TEST("the one its callback opened is still open",
             followUp.dialog && *followUp.result == DialogResult::NoResult);
        TEST("and still registered", UltraCanvasDialogManager::GetActiveDialogCount() == 1);
        UltraCanvasDialogManager::CloseAllDialogs();
        TEST("A second call cancels it", *followUp.result == DialogResult::Cancel);
        TEST("leaving none", UltraCanvasDialogManager::GetActiveDialogCount() == 0);
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
