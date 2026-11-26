// Tests/TextAreaMarkdownSelectionTest.cpp
// A selection over rendered Markdown, read back from the window. Texter showed
// a selection only on the raw line under the caret: a table row has no text
// layout of its own (each cell has one), so the selection background had
// nowhere to go, and a rule, a picture or a display formula has no text at
// all. Checked here:
//   - selecting a table's rows paints the selection inside its cells;
//   - selecting one word of a cell paints that word, not the whole row;
//   - a search match inside a cell is marked too;
//   - a selected horizontal rule is tinted across its row.
//
// The selection and the mark are painted in pure, opaque colours so a pixel
// says which one covers it. Runs headless under Xvfb; skips when there is no
// display.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"
#include "DisplayRequired.h"

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
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

const Color kMark(255, 0, 0);
const Color kSelection(0, 0, 255);

// Columns right of this are the vertical scrollbar.
const int kTrackLeft = 562;

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
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < kTrackLeft; ++x) {
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
    std::cerr << "   TextArea Markdown Selection Suite"     << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaMarkdownSelectionTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TextAreaMarkdownSelectionTest";
    cfg.width = 600;
    cfg.height = 420;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Markdown", 0, 0, 580, 400);
    window->AddChild(area);
    area->SetShowLineNumbers(false);
    area->SetHighlightCurrentLine(false);
    area->SetFontSize(24);

    TextAreaStyle& style = area->GetStyle();
    style.searchHighlightColor = kMark;
    style.selectionColor = kSelection;
    style.fontColor = Color(0, 0, 0);

    // The caret stays on the last line, which is the one shown raw: every
    // line the selection covers is rendered.
    area->SetText("| Name | Pet |\n"
                  "|------|-----|\n"
                  "| Ann  | cat |\n"
                  "| Bob  | dog |\n"
                  "\n"
                  "---\n"
                  "\n"
                  "end");
    area->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    const LineColumnIndex caret{7, 3};
    area->SetCursorPosition(caret);

    Counts none = CountPixels(window, area);

    // ===== TABLE ROWS =====
    std::cerr << "\n--- The whole table ---" << std::endl;
    area->SetSelection(LineColumnIndex{0, 0}, LineColumnIndex{4, 0});
    Counts table = CountPixels(window, area);
    TEST("Selecting a table's rows paints the selection in its cells",
         table.selection > none.selection + 400);

    std::cerr << "\n--- One word of one cell ---" << std::endl;
    // "| Ann  | cat |": "cat" is columns 9-12.
    area->SetSelection(LineColumnIndex{2, 9}, LineColumnIndex{2, 12});
    Counts word = CountPixels(window, area);
    TEST("Selecting a word in a cell paints it", word.selection > none.selection + 60);
    TEST("...and not the whole table", word.selection < table.selection / 2);

    // ===== SEARCH MARKS IN CELLS =====
    std::cerr << "\n--- A search match in a cell ---" << std::endl;
    area->SetSelection(LineColumnIndex::INVALID, LineColumnIndex::INVALID);
    area->HighlightMatches("dog", false);
    Counts mark = CountPixels(window, area);
    TEST("A match inside a table cell is marked", mark.mark > none.mark + 60);
    area->ClearHighlights();

    // ===== A RULE =====
    std::cerr << "\n--- A horizontal rule ---" << std::endl;
    area->SetSelection(LineColumnIndex{5, 0}, LineColumnIndex{6, 0});
    Counts rule = CountPixels(window, area);
    TEST("A selected rule is tinted across its row", rule.selection > none.selection + 2000);

    area->SetSelection(LineColumnIndex::INVALID, LineColumnIndex::INVALID);
    Counts cleared = CountPixels(window, area);
    TEST("Clearing the selection takes the tint away", cleared.selection < none.selection + 30);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
