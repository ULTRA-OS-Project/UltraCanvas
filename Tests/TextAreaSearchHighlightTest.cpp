// Tests/TextAreaSearchHighlightTest.cpp
// Search highlights in a real UltraCanvasTextArea, read back from the window:
//   - HighlightMatches marks every match on screen, not only the current one
//     (only the hex view used to draw them);
//   - the current match - the selection - is drawn over its mark;
//   - the marks follow an edit and go away with ClearHighlights;
//   - the case-sensitive overload honours its flag;
//   - a match below the view puts a tick on the scrollbar;
//   - the hex view marks its matches in the same colour as the text views;
//   - in a document over the defer size the marks follow an edit once typing
//     pauses (a one-shot timer), not on the keystroke itself.
//
// The mark and the selection are painted in pure, opaque colours so a pixel
// says which one covers it. Runs headless under Xvfb; skips when there is no
// display.
// Version: 1.1.0
// Last Modified: 2026-10-18
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <cstdlib>
#include <functional>
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

// Columns right of this are the vertical scrollbar.
const int kTrackLeft = 562;

// ProcessTimers without the native wait RunOnce() would block on.
class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplicationBase::ProcessTimers;
};

struct Counts { int mark = 0; int selection = 0; int trackMark = 0; int trackSelection = 0; };

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
            const bool track = x >= kTrackLeft;
            if (Near(px, kMark)) (track ? n.trackMark : n.mark)++;
            else if (Near(px, kSelection)) (track ? n.trackSelection : n.selection)++;
        }
    }
    std::cerr << "   mark " << n.mark << ", selection " << n.selection
              << " | scrollbar mark " << n.trackMark << ", selection " << n.trackSelection << std::endl;
    return n;
}

// Runs due timers and repaints until `done` says so or two seconds have passed.
bool PumpUntil(TestApplication& app, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        app.ProcessTimers();
        if (done()) return true;
    }
    return done();
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Search Highlight Suite"      << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    TestApplication app;
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

    // ===== SCROLLBAR TICKS =====
    std::cerr << "\n--- A match far below the view ---" << std::endl;
    std::string longText = "top\n";
    for (int i = 0; i < 200; ++i) longText += "filler line\n";
    longText += "cat at the bottom";
    area->SetText(longText);
    area->HighlightMatches("cat", false);
    Counts below = CountPixels(window, area);
    TEST("Nothing in view is marked", unmarked(below));
    TEST("The scrollbar shows a tick for it", below.trackMark >= 4);

    area->SetTextToFind("cat", false);
    area->FindFirst();
    area->ScrollTo(0);
    Counts current = CountPixels(window, area);
    TEST("The current match gets its tick in the selection colour", current.trackSelection >= 4);

    area->ClearHighlights();
    area->ClearSelection();
    Counts noTicks = CountPixels(window, area);
    TEST("ClearHighlights takes the ticks away", noTicks.trackMark == 0 && noTicks.trackSelection == 0);

    // ===== HEX VIEW =====
    std::cerr << "\n--- The hex view ---" << std::endl;
    area->SetText("cat dog cat bird cat");
    area->SetEditingMode(TextAreaEditingMode::Hex);
    area->HighlightMatches("cat", false);
    Counts hexView = CountPixels(window, area);
    TEST("The hex view marks matches in searchHighlightColor", hexView.mark > 100);
    area->ClearHighlights();
    area->SetEditingMode(TextAreaEditingMode::PlainText);

    // ===== LARGE DOCUMENT: MARKS WAIT FOR A PAUSE =====
    std::cerr << "\n--- A document over the defer size ---" << std::endl;
    std::string bigText = "cat dog\ncat\nbird cat\n";
    while (bigText.size() < 320 * 1024) bigText += "filler line without the word\n";
    area->SetText(bigText);
    area->ScrollTo(0);
    area->HighlightMatches("cat", false);
    Counts bigThree = CountPixels(window, area);
    TEST("All three matches are marked at once", bigThree.mark > one.mark * 2);

    TEST("The edit is accepted", area->ReplaceTextRange(0, 1, "b"));   // "bat dog"
    Counts typed = CountPixels(window, area);
    TEST("The keystroke itself does not search again", typed.mark > bigThree.mark * 0.9);
    bool settled = PumpUntil(app, [&] { return CountPixels(window, area).mark < bigThree.mark * 0.85; });
    TEST("Once typing pauses the word that no longer matches loses its mark", settled);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
