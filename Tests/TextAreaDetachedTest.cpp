// Tests/TextAreaDetachedTest.cpp
// A text area that is not in a window yet has no render context, so it has
// nothing to build line layouts with. Scrolling or querying it straight after
// SetText() must not try - the media viewer fills and scrolls its Details
// panel on every file it loads, before the widget is attached, and the
// Markdown layout code dereferenced the missing context (ACCESS_VIOLATION in
// the demo's Media Viewer page).
//
// Then, once attached, the same areas lay out as usual - and an input method's
// composition (TextComposition) is laid out in the caret's line, at the caret,
// without ever entering the text: committing types it, an empty composition
// or losing the focus ends it, composing over a selection replaces it, and a
// read-only area leaves the composing to the input method's own window.
// Runs headless under Xvfb; skips when there is no display.
// Version: 1.1.0 - input method composition
// Version: 1.0.0
// Last Modified: 2026-10-14
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
    using UltraCanvasTextArea::LineColumnToCursorPos;   // where the caret is drawn
};

// The shape of the media viewer's Details text: a heading, a table, sections.
const char* kMarkdown =
    "## Image information\n\n"
    "| Property | Value |\n| --- | --- |\n"
    "| **File** | dice.png |\n"
    "| **Size** | 24.3 KB |\n\n"
    "\n## Metadata\n\n"
    "No embedded metadata.\n";

// What an input method sends while it composes: the whole composing string
// and its caret (a byte offset; -1 is the end). "" ends the composition.
UCEvent CompositionEvent(const std::string& utf8, int cursor = -1) {
    UCEvent e;
    e.type = UCEventType::TextComposition;
    e.text = utf8;
    e.compositionCursor = cursor;
    return e;
}

UCEvent TextEvent(const std::string& utf8) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = UCKeys::Unknown;
    e.text = utf8;
    return e;
}

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

    // ===== INPUT METHOD COMPOSITION =====
    std::cerr << "\n--- Input method composition ---" << std::endl;
    {
        const std::string kana = "\xE3\x81\x8B\xE3\x81\xAA";   // two kana, being composed
        const std::string kanji = "\xE4\xBB\xAE";                // what they convert to
        // Alone in the window, so it is laid out on screen and drawn.
        window->RemoveChild(md);
        window->RemoveChild(plain);
        auto area = std::make_shared<ProbeArea>("Composer", 0, 0, 400, 300);
        window->AddChild(area);
        area->SetText("ab\ncd");
        int changes = 0;
        area->onTextChanged = [&changes](const std::string&) { ++changes; };
        area->SetCursorPosition({0, 1});
        window->UpdateAndRender();
        const Rect2Di before = area->LineColumnToCursorPos(area->GetCursorPosition());

        TEST("An editable area draws compositions itself", area->DrawsTextComposition());
        TEST("A composition is taken", area->OnEvent(CompositionEvent(kana)));
        window->UpdateAndRender();
        const Rect2Di during = area->LineColumnToCursorPos(area->GetCursorPosition());
        TEST("It is the composition text", area->GetCompositionText() == kana);
        TEST("...not in the text", area->GetText() == "ab\ncd");
        TEST("...and reports no change", changes == 0);
        TEST("The caret moves to its end, on the same line",
             during.x > before.x + 5 && during.y == before.y);
        area->OnEvent(CompositionEvent(kana, 3));   // after the first kana
        window->UpdateAndRender();
        const Rect2Di inside = area->LineColumnToCursorPos(area->GetCursorPosition());
        TEST("The caret can sit inside it", inside.x > before.x && inside.x < during.x);

        area->OnEvent(TextEvent(kanji));
        window->UpdateAndRender();
        TEST("The committed text is typed at the caret", area->GetText() == "a" + kanji + "b\ncd");
        TEST("...and the composition is gone", area->GetCompositionText().empty());
        TEST("The commit is the one change", changes == 1);

        area->OnEvent(CompositionEvent(kana));
        area->OnEvent(CompositionEvent(""));
        TEST("An empty composition ends it", area->GetCompositionText().empty());
        TEST("...typing nothing", area->GetText() == "a" + kanji + "b\ncd");

        // A composition leaves no undo step: Undo takes back the commit.
        area->OnEvent(CompositionEvent(kana));
        area->OnEvent(CompositionEvent(""));
        area->Undo();
        TEST("Undo takes back the commit, not the composition", area->GetText() == "ab\ncd");

        // Composing over a selection replaces it, as typing would.
        area->SetText("hello world");
        area->SetSelection(0, 5);
        area->SetCursorPosition({0, 5});
        area->OnEvent(CompositionEvent(kana));
        TEST("Composing over a selection removes the selected text", area->GetText() == " world");
        TEST("...and the selection", !area->HasSelection());
        area->OnEvent(TextEvent(kanji));
        TEST("The commit lands where the selection was", area->GetText() == kanji + " world");

        // A line break in a composition stays in the caret's line.
        area->OnEvent(CompositionEvent("x\ny"));
        TEST("A line break in a composition shows as a space", area->GetCompositionText() == "x y");
        window->UpdateAndRender();
        TEST("...and the area still has one line", area->GetLineCount() == 1);

        // Leaving the area drops the composition, unfinished.
        UCEvent lost;
        lost.type = UCEventType::FocusLost;
        area->OnEvent(lost);
        TEST("Losing the focus ends the composition", area->GetCompositionText().empty());
        TEST("...and types nothing", area->GetText() == kanji + " world");

        area->OnEvent(CompositionEvent(kana));
        area->SetReadOnly(true);
        TEST("Turning read-only drops a composition", area->GetCompositionText().empty());
        TEST("A read-only area does not draw compositions", !area->DrawsTextComposition());
        TEST("...and does not take one", !area->OnEvent(CompositionEvent(kana))
             && area->GetCompositionText().empty());
        area->SetReadOnly(false);
        area->SetEditingMode(TextAreaEditingMode::Hex);
        TEST("Nor does a hex view", !area->DrawsTextComposition());
        area->SetEditingMode(TextAreaEditingMode::PlainText);
        window->RemoveChild(area);
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
