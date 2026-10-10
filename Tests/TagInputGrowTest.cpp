// Tests/TagInputGrowTest.cpp
// A tag field grows to fit its chips inside a layout, not only on its own.
//
// UltraCanvasTagInput wraps its chips onto more rows and grows its height to
// fit them (autoHeight). It used to change only its bounds while painting, so
// in a flex container the next layout pass set them back from size.height -
// the 36 px CreateTagInput gives it - and every row after the first was cut
// off: UltraMail's Settings showed two of three blocked addresses, the third
// invisible. These tests put the field in a flex column between two labels,
// as a settings page does, and check that it grows, that what follows it
// moves down, and that it shrinks again. A field made without a height
// starts as tall as one row of chips, so it does not grow on its first frame.
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, so it stays usable on a bare CI machine.
// Version: 1.1.0 - the field starts at one row's height (OneRowHeight), and
//                  keeps it on its first frame
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasChip.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <cmath>
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

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text) {
    auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 300, 20);
    label->SetText(text);
    label->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return label;
}

// A few frames: the field measures its rows while painting, and the layout
// it then asks for is applied on the frame after.
void Frames(const std::shared_ptr<UltraCanvasWindow>& window,
            const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
    for (int frame = 0; frame < 6; ++frame) DisplayTest::Frame(window, elements);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Tag Input Grows In A Layout Suite"    << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TagInputGrowTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TagInputGrowTest";
    cfg.width = 600;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() draws nothing on an unmapped window

    // A settings page in miniature: a caption, the field, the next caption.
    auto page = std::make_shared<UltraCanvasContainer>("page", 0, 0, 600, 400);
    page->layout.SetFlexColumn().SetFlexGap(8);
    window->AddChild(page);
    auto above = MakeLabel("above", "Blocked:");
    auto tags  = CreateTagInput("tags", -1, -1, 300);   // no height: one row's
    tags->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    tags->size.width = CSSLayout::Dimension::Px(300);
    auto below = MakeLabel("below", "Next setting");
    page->AddChild(above);
    page->AddChild(tags);
    page->AddChild(below);
    const std::vector<std::shared_ptr<UltraCanvasUIElement>> all = { page, above, tags, below };

    // ===== AS MADE =====
    std::cerr << "\n--- As made, before and after its first frame ---" << std::endl;
    const float made = tags->GetHeight();
    std::cerr << "   made: " << made << " px (one row: " << tags->OneRowHeight() << " px)"
              << std::endl;
    TEST("A field made without a height starts as tall as one row of chips",
         made > 0.0f && std::abs(made - tags->OneRowHeight()) <= 0.5f);
    TEST("... in the layout too (size.height)",
         tags->size.height.unit == CSSLayout::DimensionUnit::Pixels &&
         std::abs(tags->size.height.value - made) <= 0.5f);

    tags->SetTags({ "one@example.com" });
    DisplayTest::Frame(window, all);   // one frame: where the jump used to be
    TEST("One chip: the first frame keeps that height (no jump)",
         std::abs(tags->GetHeight() - made) <= 0.5f);
    Frames(window, all);
    const float oneRow = tags->GetHeight();
    std::cerr << "   one row: " << oneRow << " px" << std::endl;
    TEST("One chip fits the field as it was made", std::abs(oneRow - made) <= 0.5f);

    // ===== MORE CHIPS THAN ONE ROW HOLDS =====
    std::cerr << "\n--- Chips on three rows ---" << std::endl;
    tags->SetTags({ "@spam-offers.example", "max@example.com", "orders@ultra.store",
                    "newsletter@shop.example", "someone@else.example" });
    Frames(window, all);
    const float grown = tags->GetHeight();
    std::cerr << "   grown: " << grown << " px (content " << tags->GetContentHeight()
              << " px)" << std::endl;
    TEST("The field grows past one row", grown > oneRow + 10.0f);
    TEST("The field is as tall as its rows", std::abs(grown - tags->GetContentHeight()) <= 0.5f);
    TEST("The layout keeps the height (it is in size.height, not only the bounds)",
         tags->size.height.unit == CSSLayout::DimensionUnit::Pixels &&
         std::abs(tags->size.height.value - grown) <= 0.5f);
    TEST("What follows the field moves down below it",
         below->GetY() >= tags->GetY() + grown - 0.5f);

    // ===== FEWER CHIPS AGAIN =====
    std::cerr << "\n--- Back to one chip ---" << std::endl;
    tags->SetTags({ "one@example.com" });
    Frames(window, all);
    std::cerr << "   shrunk: " << tags->GetHeight() << " px" << std::endl;
    TEST("The field shrinks back to one row", std::abs(tags->GetHeight() - oneRow) <= 0.5f);
    TEST("What follows the field moves back up",
         below->GetY() < tags->GetY() + grown - 0.5f);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed"
              << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
