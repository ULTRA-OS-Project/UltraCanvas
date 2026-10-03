// Tests/TextAreaDetachedTest.cpp
// A text area that is not in a window yet has no render context, so it has
// nothing to build line layouts with. Scrolling or querying it straight after
// SetText() must not try - the media viewer fills and scrolls its Details
// panel on every file it loads, before the widget is attached, and the
// Markdown layout code dereferenced the missing context (ACCESS_VIOLATION in
// the demo's Media Viewer page).
//
// Then, once attached, the same areas lay out as usual.
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

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

// Reads the laid-out content height, which the area keeps to itself.
struct ProbeArea : UltraCanvasTextArea {
    using UltraCanvasTextArea::UltraCanvasTextArea;
    using UltraCanvasTextArea::GetContentHeight;
};

// The shape of the media viewer's Details text: a heading, a table, sections.
const char* kMarkdown =
    "## Image information\n\n"
    "| Property | Value |\n| --- | --- |\n"
    "| **File** | dice.png |\n"
    "| **Size** | 24.3 KB |\n\n"
    "\n## Metadata\n\n"
    "No embedded metadata.\n";

std::string LongText() {
    std::string text;
    for (int i = 0; i < 200; ++i) text += "Line " + std::to_string(i) + "\n";
    return text;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Detached Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaDetachedTest")) SKIP_ALL("application would not initialise");

    // ===== NOT IN A WINDOW =====
    std::cerr << "\n--- Markdown, not in a window ---" << std::endl;
    auto md = std::make_shared<ProbeArea>("Details", 0, 0, 400, 300);
    md->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    md->SetDisplayOnly(true);
    md->SetText(kMarkdown);
    md->ScrollTo(0);   // crashed here
    TEST("ScrollTo on a detached Markdown area returns", true);
    md->SetText(kMarkdown);
    md->ScrollTo(3);
    TEST("A second SetText + ScrollTo returns too", true);

    std::cerr << "\n--- Plain text, not in a window ---" << std::endl;
    auto plain = std::make_shared<ProbeArea>("Plain", 0, 0, 400, 300);
    plain->SetText(LongText());
    plain->ScrollTo(50);
    TEST("ScrollTo on a detached plain area returns", true);

    // ===== ATTACHED =====
    WindowConfig cfg;
    cfg.title = "TextAreaDetachedTest";
    cfg.width = 420;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    std::cerr << "\n--- The same areas once attached ---" << std::endl;
    window->AddChild(md);
    window->AddChild(plain);
    window->UpdateAndRender();
    TEST("The Markdown area lays out once it is in a window", md->GetContentHeight() > 0);

    plain->ScrollTo(50);
    window->UpdateAndRender();
    TEST("The plain area lays out all its lines once it is in a window",
         plain->GetContentHeight() > 300);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
