// Tests/DemoScrollTextTest.cpp
// The demo's scrolling text block (Apps/DemoApp/UltraCanvasDemoScrollText.h).
//
// The 3D Graphics demo pages fill fixed-size information panels with text
// whose length depends on the file being described, so the panels have to
// cope with more text than they have room for. Before this block they did
// not: a label is centred in its box by default, so an overlong "What the
// reader found" lost its first line off the top and its last off the bottom,
// with nothing on screen to say so.
//
// These tests run the real layout engine over the real widgets (an offscreen
// render context stands in for a window, which is all UltraCanvasLabel needs
// to measure text) and assert what the fix rests on: the label is measured
// against the whole text rather than clipped to the viewport, the container
// notices and shows its vertical scrollbar, text that fits shows no
// scrollbar at all, a line wider than the box wraps instead of being
// ellipsized, and the first line sits at the top of the box either way.
//
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasDemoScrollText.h"
#include "UltraCanvasRenderContext.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  %s %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

// A page stand-in that owns a render context, the way a window does: an
// element reaches one through its parent containers, and a label with no
// render context cannot measure its text at all.
struct PageRoot : UltraCanvasContainer {
    PageRoot(float w, float h) : UltraCanvasContainer("Page", 0, 0, w, h) {
        renderContext = CreateRenderContext(Size2Di(static_cast<int>(w),
                                                    static_cast<int>(h)), nullptr);
    }
    bool HasContext() const { return renderContext != nullptr; }
};

// One "What the reader found" panel: the page, and the block inside it.
struct Panel {
    std::shared_ptr<PageRoot> page;
    demoui::ScrollingText block;
};

constexpr float kPageW = 400.0f;
constexpr float kPageH = 300.0f;
constexpr float kBlockW = 300.0f;
constexpr float kBlockH = 120.0f;

Panel MakePanel(const std::string& text) {
    Panel panel;
    panel.page = std::make_shared<PageRoot>(kPageW, kPageH);
    panel.block = demoui::MakeScrollingText("Stats", 10.0f, 10.0f, kBlockW, kBlockH);
    panel.page->AddChild(panel.block.View);
    panel.block.SetText(text);
    return panel;
}

void LayOut(Panel& panel) {
    LayoutContext ctx;
    ctx.viewportWidth = kPageW;
    ctx.viewportHeight = kPageH;
    MeasureConstraints mc{ { ConstraintMode::Exact, kPageW },
                           { ConstraintMode::Exact, kPageH } };
    panel.page->Measure(mc, ctx);
    panel.page->Arrange(Rect2Df{ 0, 0, kPageW, kPageH }, ctx);
}

// Roughly what a stats panel holds for a scene format: far more lines than a
// 120px box can show at 11px.
std::string LongText() {
    std::string text;
    for (int line = 0; line < 40; ++line) {
        text += "Nodes      " + std::to_string(line) + "   Meshes 2\n";
    }
    return text;
}

void TestOverflowScrolls() {
    std::printf("Text longer than the box:\n");
    Panel panel = MakePanel(LongText());
    Check(panel.page->HasContext(), "offscreen render context created");
    LayOut(panel);

    const float textHeight = panel.block.Text->GetHeight();
    Check(textHeight > kBlockH,
          "label measured against the whole text (" + std::to_string((int)textHeight) +
          "px > " + std::to_string((int)kBlockH) + "px box)");
    Check(panel.block.View->GetVerticalScrollBar().IsVisible(),
          "vertical scrollbar shown");
    Check(panel.block.View->GetVerticalScrollBar().GetMaxScrollPosition() > 0,
          "there is something to scroll to");
    Check(!panel.block.View->GetHorizontalScrollBar().IsVisible(),
          "no horizontal scrollbar");
    Check(panel.block.Text->GetY() <= 1.0f,
          "first line sits at the top of the box");
}

void TestFittingTextHasNoScrollbar() {
    std::printf("Text that fits:\n");
    Panel panel = MakePanel("Format      STL (binary)\nTriangles   12\n");
    LayOut(panel);

    Check(panel.block.Text->GetHeight() <= kBlockH,
          "label fits the box");
    Check(!panel.block.View->GetVerticalScrollBar().IsVisible(),
          "no scrollbar when none is needed");
    Check(panel.block.Text->GetY() <= 1.0f,
          "short text starts at the top, not centred");
}

// A line wider than the panel is wrapped, not ellipsized: the part that would
// be cut off ("Autodesk FBX 6.x and 7.x, binary and ASCII") is the part worth
// reading, and the box cannot get any wider.
void TestLongLineWraps() {
    std::printf("A line wider than the box:\n");
    Panel oneWord = MakePanel("Format");
    LayOut(oneWord);
    const float lineHeight = oneWord.block.Text->GetHeight();

    Panel panel = MakePanel("Format      Autodesk FBX 6.x and 7.x, binary and ASCII, "
                            "with the header's version, encoding and application");
    LayOut(panel);
    Check(lineHeight > 0.0f, "one line measured");
    Check(panel.block.Text->GetHeight() > lineHeight * 1.5f,
          "the long line wrapped onto further lines");
}

// Switching samples replaces the text; a panel scrolled to the bottom for one
// sample must not open the next one halfway down.
void TestSetTextReturnsToTop() {
    std::printf("Switching samples:\n");
    Panel panel = MakePanel(LongText());
    LayOut(panel);

    panel.block.View->ScrollByVertical(200);
    Check(panel.block.View->GetVerticalScrollPosition() > 0, "scrolled down");

    panel.block.SetText(LongText() + "and one more line\n");
    LayOut(panel);
    Check(panel.block.View->GetVerticalScrollPosition() == 0,
          "new text starts at the top again");
}

} // namespace

int main() {
    std::printf("=== Demo scrolling text block ===\n");
    TestOverflowScrolls();
    TestFittingTextHasNoScrollbar();
    TestLongLineWraps();
    TestSetTextReturnsToTop();

    if (g_failures == 0) {
        std::printf("All checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) failed.\n", g_failures);
    return 1;
}
