// Tests/TextAreaMarkdownCaretLineTest.cpp
// The caret's line in Markdown view, read back from the window. It is shown as
// its source, so it can be edited character by character, and with
// MarkdownHybridStyle::styleCaretLine (the default) it is formatted like the
// rendered lines around it:
//   - a heading is drawn at the heading's size, its "#" in the markup colour;
//   - "**" and "`" are drawn in the markup colour, code in the code colour;
//   - the line still shows a selection;
//   - a table row's "|" stands on its column's divider, as in the rendered
//     rows below it;
//   - with styleCaretLine off the line is plain source again.
//
// Text, markup, code, selection and table borders are each painted in a pure,
// opaque colour so a pixel says which one it is. Runs headless under Xvfb;
// skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"
#include "DisplayRequired.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
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
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

const Color kText(0, 0, 0);
const Color kMarkup(255, 0, 0);
const Color kCode(0, 0, 255);
const Color kSelection(255, 0, 255);
const Color kBorder(0, 200, 0);
const Color kWhite(255, 255, 255);

// Columns right of this are the vertical scrollbar.
const int kTrackLeft = 562;
const int kHeight = 300;

bool Near(const Color& px, const Color& c) {
    auto close = [](uint8_t a, uint8_t b) { return std::abs(int(a) - int(b)) <= 60; };
    return close(px.r, c.r) && close(px.g, c.g) && close(px.b, c.b);
}

struct Shot {
    std::vector<Color> px;
    Color At(int x, int y) const { return px[y * kTrackLeft + x]; }
    int Count(const Color& c) const {
        int n = 0;
        for (const auto& p : px) if (Near(p, c)) n++;
        return n;
    }
};

Shot Grab(const std::shared_ptr<UltraCanvasWindow>& window,
          const std::shared_ptr<UltraCanvasTextArea>& area) {
    area->RequestRedraw();
    window->UpdateAndRender();
    Shot s;
    s.px.resize(kTrackLeft * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kTrackLeft; ++x)
            window->GetPixelColor(x, y, s.px[y * kTrackLeft + x]);
    return s;
}

void SetCaretStyling(const std::shared_ptr<UltraCanvasTextArea>& area, bool on) {
    MarkdownHybridStyle md = area->GetMarkdownStyle();
    md.styleCaretLine = on;
    area->SetMarkdownStyle(md);
}

// Centres of the runs of columns holding a pixel of `c` in rows [y0, y1).
std::vector<int> ColumnClusters(const Shot& s, const Color& c, int y0, int y1) {
    std::vector<int> centres;
    int runStart = -1;
    for (int x = 0; x <= kTrackLeft; ++x) {
        bool hit = false;
        for (int y = y0; x < kTrackLeft && y < y1 && !hit; ++y) hit = Near(s.At(x, y), c);
        if (hit && runStart < 0) runStart = x;
        if (!hit && runStart >= 0) { centres.push_back((runStart + x - 1) / 2); runStart = -1; }
    }
    return centres;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Markdown Caret Line Suite"    << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaMarkdownCaretLineTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TextAreaMarkdownCaretLineTest";
    cfg.width = 600;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Markdown", 0, 0, 580, kHeight);
    window->AddChild(area);
    area->SetShowLineNumbers(false);
    area->SetHighlightCurrentLine(false);
    area->SetHighlightSyntax(false);
    area->SetFontSize(20);

    TextAreaStyle& style = area->GetStyle();
    style.selectionColor = kSelection;
    style.fontColor = kText;
    style.backgroundColor = kWhite;
    style.cursorColor = kWhite;   // the caret is not what is measured

    MarkdownHybridStyle md = area->GetMarkdownStyle();
    md.markupColor = kMarkup;
    md.codeTextColor = kCode;
    md.codeBackgroundColor = kWhite;
    md.tableBorderColor = kBorder;
    md.tableHeaderBackground = kWhite;
    md.headerColors.fill(kText);
    area->SetMarkdownStyle(md);
    area->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);

    // ===== A HEADING =====
    std::cerr << "\n--- A heading under the caret ---" << std::endl;
    area->SetText("# Title");
    area->SetCursorPosition(LineColumnIndex{0, 7});
    SetCaretStyling(area, false);
    Shot plainHeading = Grab(window, area);
    SetCaretStyling(area, true);
    Shot styledHeading = Grab(window, area);
    std::cerr << "   text pixels plain " << plainHeading.Count(kText)
              << ", styled " << styledHeading.Count(kText)
              << "; markup " << styledHeading.Count(kMarkup) << std::endl;
    TEST("The heading is drawn larger than plain source",
         styledHeading.Count(kText) > plainHeading.Count(kText) * 3 / 2);
    TEST("Its # is in the markup colour", styledHeading.Count(kMarkup) > 40);
    TEST("Plain source has no markup colour", plainHeading.Count(kMarkup) == 0);
    TEST("The source text is unchanged", area->GetText() == "# Title");

    // ===== INLINE MARKUP =====
    std::cerr << "\n--- Bold and code under the caret ---" << std::endl;
    area->SetText("**bold** and `code`");
    area->SetCursorPosition(LineColumnIndex{0, 3});
    Shot inlineShot = Grab(window, area);
    std::cerr << "   markup " << inlineShot.Count(kMarkup) << ", code " << inlineShot.Count(kCode) << std::endl;
    TEST("** and ` are drawn in the markup colour", inlineShot.Count(kMarkup) > 40);
    TEST("The code is drawn in the code colour", inlineShot.Count(kCode) > 40);

    std::cerr << "\n--- A selection on the formatted line ---" << std::endl;
    area->SetSelection(LineColumnIndex{0, 2}, LineColumnIndex{0, 6});
    Shot selected = Grab(window, area);
    TEST("The selection shows over the formatting", selected.Count(kSelection) > 100);
    area->SetSelection(LineColumnIndex::INVALID, LineColumnIndex::INVALID);

    // ===== A TABLE ROW =====
    std::cerr << "\n--- A table row under the caret ---" << std::endl;
    area->SetText("| A | B |\n"
                  "|---|---|\n"
                  "| a much longer first cell | x |\n"
                  "| more | y |");
    area->SetCursorPosition(LineColumnIndex{0, 1});
    Shot table = Grab(window, area);

    // The rendered rows' divider between the columns: the column with the
    // most border pixels (the rules across the table give one or two each).
    std::map<int, int> borderPerColumn;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kTrackLeft; ++x)
            if (Near(table.At(x, y), kBorder)) borderPerColumn[x]++;
    int divider = -1, most = 0;
    for (auto [x, count] : borderPerColumn)
        if (count > most) { most = count; divider = x; }
    // Every pixel in the markup colour is the caret's line: its "|" bars.
    int top = kHeight, bottom = 0;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kTrackLeft; ++x)
            if (Near(table.At(x, y), kMarkup)) { top = std::min(top, y); bottom = std::max(bottom, y + 1); }
    std::vector<int> bars = top < bottom ? ColumnClusters(table, kMarkup, top, bottom) : std::vector<int>{};
    std::cerr << "   divider at x=" << divider << "; bars at";
    for (int b : bars) std::cerr << " " << b;
    std::cerr << std::endl;
    bool barOnDivider = false;
    for (int b : bars) if (std::abs(b - divider) <= 3) barOnDivider = true;
    TEST("The rendered rows have a divider", divider > 100);
    TEST("The caret row's | stands on the divider", barOnDivider);
    TEST("The table's source is unchanged",
         area->GetText().rfind("| A | B |\n|---|---|\n", 0) == 0);

    std::cerr << "\n--- The same row with styleCaretLine off ---" << std::endl;
    SetCaretStyling(area, false);
    Shot plainTable = Grab(window, area);
    TEST("Off, the row is plain source (no markup colour)", plainTable.Count(kMarkup) == 0);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
