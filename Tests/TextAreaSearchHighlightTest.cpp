// Tests/TextAreaSearchHighlightTest.cpp
// Search highlights in a real UltraCanvasTextArea, read back from the window:
//   - HighlightMatches marks every match on screen, not only the current one
//     (only the hex view used to draw them);
//   - the current match - the selection - is drawn over its mark;
//   - the marks follow an edit and go away with ClearHighlights;
//   - the case-sensitive overload honours its flag.
//
// The mark and the selection are painted in pure, opaque colours so a pixel
// says which one covers it. Runs headless under Xvfb; skips when there is no
// display.
// Version: 1.0.0
// Last Modified: 2026-10-18
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>

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

const Color kMark(255, 0, 0);
const Color kSelection(0, 0, 255);

struct Counts { int mark = 0; int selection = 0; };

bool Near(const Color& px, const Color& c) {
    auto close = [](uint8_t a, uint8_t b) { return std::abs(int(a) - int(b)) <= 60; };
    return close(px.r, c.r) && close(px.g, c.g) && close(px.b, c.b);
}

Counts CountPixels(const std::shared_ptr<UltraCanvasWindow>& window,
                   const std::shared_ptr<UltraCanvasTextArea>& area) {
    area->RequestRedraw();
    window->UpdateAndRender();
    Counts n;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 580; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) continue;
            if (Near(px, kMark)) n.mark++;
            else if (Near(px, kSelection)) n.selection++;
        }
    }
    std::cerr << "   mark " << n.mark << ", selection " << n.selection << std::endl;
    return n;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Search Highlight Suite"      << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaSearchHighlightTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TextAreaSearchHighlightTest";
    cfg.width = 600;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Text", 0, 0, 580, 300);
    window->AddChild(area);
    area->SetShowLineNumbers(false);
    area->SetHighlightCurrentLine(false);
    area->SetHighlightSyntax(false);
    area->SetFontSize(28);

    TextAreaStyle& style = area->GetStyle();
    style.searchHighlightColor = kMark;
    style.selectionColor = kSelection;
    style.fontColor = Color(0, 0, 0);

    // Three "cat" on three lines, one "dog": about the same width, so three
    // marks cover well over twice the pixels of one.
    area->SetText("cat dog\ncat\nbird cat");
    // Antialiased glyph edges leave a few pixels near the mark colour.
    Counts none = CountPixels(window, area);
    auto unmarked = [&](const Counts& c) { return c.mark < none.mark + 30; };

    // ===== EVERY MATCH IS MARKED =====
    std::cerr << "\n--- One match ---" << std::endl;
    area->HighlightMatches("dog", false);
    Counts one = CountPixels(window, area);
    TEST("A single match is marked", one.mark > 100);

    std::cerr << "\n--- Three matches ---" << std::endl;
    area->HighlightMatches("cat", false);
    Counts three = CountPixels(window, area);
    TEST("All three matches are marked, not only one", three.mark > one.mark * 2);

    // ===== THE CURRENT MATCH SHOWS OVER ITS MARK =====
    std::cerr << "\n--- After Find Next ---" << std::endl;
    area->SetTextToFind("cat", false);
    area->FindFirst();
    Counts found = CountPixels(window, area);
    TEST("The current match is drawn in the selection colour", found.selection > one.mark / 2);
    TEST("The two other matches keep their marks",
         found.mark > one.mark * 1.5 && found.mark < three.mark);

    // ===== MARKS FOLLOW EDITS =====
    std::cerr << "\n--- After typing a fourth \"cat\" ---" << std::endl;
    area->ClearSelection();
    TEST("The edit is accepted", area->ReplaceTextRange(0, 0, "cat "));
    Counts four = CountPixels(window, area);
    TEST("The new match is marked too", four.mark > three.mark * 1.2);

    std::cerr << "\n--- After breaking the first \"cat\" ---" << std::endl;
    TEST("The edit is accepted", area->ReplaceTextRange(1, 1, "o"));   // "cot "
    Counts broken = CountPixels(window, area);
    TEST("The word that no longer matches loses its mark", broken.mark < four.mark * 0.9);

    // ===== CLEARING AND CASE =====
    std::cerr << "\n--- ClearHighlights ---" << std::endl;
    area->ClearHighlights();
    Counts cleared = CountPixels(window, area);
    TEST("No mark is left", unmarked(cleared));

    std::cerr << "\n--- Case sensitive ---" << std::endl;
    area->HighlightMatches("CAT", true);
    Counts upper = CountPixels(window, area);
    TEST("A case-sensitive \"CAT\" marks nothing", unmarked(upper));
    area->HighlightMatches("CAT", false);
    Counts anyCase = CountPixels(window, area);
    TEST("A case-insensitive \"CAT\" marks the matches", anyCase.mark > one.mark * 2);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
