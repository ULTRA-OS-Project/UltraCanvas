// Tests/InputDialogPromptTest.cpp
// An input dialog shows its whole prompt.
//
// UltraCanvasDialogManager::ShowInputDialog() opened a window of a fixed
// 400 x 150 and put the prompt in a one-line label above the field. A prompt
// longer than the window was wide ended in an ellipsis ("The repository and
// the GGUF file in it, as "organis…"), and the label's width squeezed the
// severity icon to a sliver beside it. The prompt is the message area's now:
// it wraps, and the dialog grows to the height the lines and the field need,
// up to most of the monitor. This pins that: the window grows, the prompt's
// area is as tall as its text, the field stays inside the window, the icon
// keeps its size, and a short prompt leaves the dialog as it was.
//
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-11-15
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

// The element with this identifier anywhere under `root`.
std::shared_ptr<UltraCanvasUIElement> Find(const std::shared_ptr<UltraCanvasUIElement>& root,
                                           const std::string& id) {
    if (!root) return nullptr;
    if (root->GetIdentifier() == id) return root;
    auto container = std::dynamic_pointer_cast<UltraCanvasContainer>(root);
    if (!container) return nullptr;
    for (const auto& child : container->GetChildren()) {
        if (auto found = Find(child, id)) return found;
    }
    return nullptr;
}

std::shared_ptr<UltraCanvasInputDialog> Show(const std::string& prompt, const std::string& message = {}) {
    InputDialogConfig config;
    config.title      = "Input";
    config.inputLabel = prompt;
    config.message    = message;
    auto dialog = std::make_shared<UltraCanvasInputDialog>();
    dialog->CreateInputDialog(config);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
    dialog->UpdateAndRender();      // one frame: everything laid out at the final size
    return dialog;
}

// The bottom edge of an element, in the window.
float Bottom(const std::shared_ptr<UltraCanvasUIElement>& element) {
    const Rect2Df bounds = element->GetBoundsInWindow();
    return bounds.y + bounds.height;
}

std::string Words(int count) {
    std::string text;
    for (int i = 0; i < count; ++i) text += (i ? " " : "") + std::string("repository");
    return text;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Input Dialog Prompt Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("InputDialogPromptTest")) SKIP_ALL("application would not initialise");
    UltraCanvasDialogManager::SetUseNativeDialogs(false);
    const InputDialogConfig defaults;

    std::cerr << "\n--- A short prompt ---" << std::endl;
    {
        auto dialog = Show("New name:");
        auto area  = std::dynamic_pointer_cast<UltraCanvasTextArea>(Find(dialog, "MessageArea"));
        auto field = Find(dialog, "InputField");
        auto footer = Find(dialog, "FooterSection");
        TEST("The dialog keeps its configured height", static_cast<int>(dialog->GetHeight()) == defaults.height);
        TEST("The prompt is in the message area", area && area->GetText() == "New name:");
        TEST("and nowhere a second time", Find(dialog, "InputLabel") == nullptr);
        TEST("Its area is as tall as its text", area && area->GetHeight() + 0.5f >= area->MeasureContentHeight());
        TEST("The field is above the buttons",
             field && footer && Bottom(field) <= footer->GetBoundsInWindow().y + 0.5f);
        dialog->CloseDialog(DialogResult::Cancel);
    }

    std::cerr << "\n--- A prompt of several lines ---" << std::endl;
    {
        // The prompt of UltraAI's "Add from Hugging Face", which was cut off.
        auto dialog = Show("The repository and the GGUF file in it, as \"organisation/repository:file.gguf\". "
                           "Add \"embeddings\" after a space for an embedding model. A whisper.cpp model "
                           "(\"ggerganov/whisper.cpp:ggml-medium.bin\") is a speech-to-text model.");
        auto area  = std::dynamic_pointer_cast<UltraCanvasTextArea>(Find(dialog, "MessageArea"));
        auto field = Find(dialog, "InputField");
        auto footer = Find(dialog, "FooterSection");
        auto icon  = Find(dialog, "IconContainer");
        TEST("The dialog grew", static_cast<int>(dialog->GetHeight()) > defaults.height);
        TEST("to the height of the whole prompt",
             area && area->MeasureContentHeight() > 40.0f && area->GetHeight() + 0.5f >= area->MeasureContentHeight());
        TEST("The field is inside the window, above the buttons",
             field && footer && field->GetHeight() >= 20.0f && Bottom(field) <= footer->GetBoundsInWindow().y + 0.5f);
        TEST("and below the prompt", field && area && field->GetBoundsInWindow().y + 0.5f >= Bottom(area));
        TEST("The buttons are inside the window", footer && Bottom(footer) <= dialog->GetHeight() + 0.5f);
        TEST("The icon is not squeezed", icon && icon->GetWidth() >= 47.5f);
        dialog->CloseDialog(DialogResult::Cancel);
    }

    std::cerr << "\n--- A prompt longer than the monitor ---" << std::endl;
    {
        auto dialog = Show(Words(1500));
        auto field = Find(dialog, "InputField");
        auto footer = Find(dialog, "FooterSection");
        int screenWidth = 0, screenHeight = 0;
        dialog->GetScreenSize(screenWidth, screenHeight);
        TEST("The dialog stops at most of the monitor's height",
             screenHeight > 0 && dialog->GetHeight() <= static_cast<float>(screenHeight) * 0.85f + 1.0f);
        TEST("and the field is still there, above the buttons",
             field && footer && field->GetHeight() >= 20.0f && Bottom(field) <= footer->GetBoundsInWindow().y + 0.5f);
        dialog->CloseDialog(DialogResult::Cancel);
    }

    std::cerr << "\n--- A message and a caption for the field ---" << std::endl;
    {
        auto dialog = Show("Name:", "This sheet is used by two charts; they follow the new name.");
        auto area  = std::dynamic_pointer_cast<UltraCanvasTextArea>(Find(dialog, "MessageArea"));
        auto label = Find(dialog, "InputLabel");
        auto field = Find(dialog, "InputField");
        TEST("The message is the prompt", area && area->GetText().find("two charts") != std::string::npos);
        TEST("The input label is the field's caption", label != nullptr);
        TEST("which sits between the message and the field",
             label && area && field && label->GetBoundsInWindow().y + 0.5f >= Bottom(area) &&
             field->GetBoundsInWindow().y + 0.5f >= Bottom(label));
        dialog->CloseDialog(DialogResult::Cancel);
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
