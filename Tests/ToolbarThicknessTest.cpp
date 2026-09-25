// Tests/ToolbarThicknessTest.cpp
// A toolbar is as thick as the items in it.
//
// The bug this pins: a host constructs `UltraCanvasToolbar(..., 0, 0, 0, 38)`
// because 38 px looked like a toolbar, the toolbar puts 32 px buttons inside
// 5 px of padding and a 1 px border - 44 px of content - and the layout
// engine obeys the explicit height, so the bottom 6 px of every icon is cut
// off. Nothing scrolls, nothing wraps, nothing warns; the icons are simply
// clipped, which is what UltraPaint's main toolbar shipped.
//
// The fix is that the constructed thickness is a FLOOR, not a size, so the
// two rules below hold for every host: the items always fit, and a host that
// asks for a taller bar than its items need still gets it.
//
// No UI stack: the children are plain sized elements, so this is the layout
// engine and the toolbar's own box, with no render context needed to measure
// text.
//
// A second bug, in the builder: it makes every toolbar 800 x 48 before it
// knows the shape, so a toolbar turned vertical kept the 800 px width as its
// floor, and SetDimensions() never replaced it. Texter's markdown side bar
// asked for 40 px and got 800, half the window.
// Version: 1.1.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

// An item of a known size and nothing else — a stand-in for a toolbar button,
// which is exactly that plus an icon.
std::shared_ptr<UltraCanvasUIElement> Item(const std::string& id, float w, float h) {
    auto item = std::make_shared<UltraCanvasUIElement>(id, 0, 0, w, h);
    item->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return item;
}

// Lays the toolbar out the way a window's flex column does: the cross axis is
// given to it, the main axis is its own business.
void LayOut(const std::shared_ptr<UltraCanvasToolbar>& toolbar, float width, float height) {
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = height;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::AtMost, width },
                                      { CSSLayout::ConstraintMode::AtMost, height } };
    toolbar->Measure(mc, ctx);
    toolbar->Arrange(Rect2Df{ 0, 0, toolbar->measured.measuredWidth,
                              toolbar->measured.measuredHeight }, ctx);
}

float Bottom(const std::shared_ptr<UltraCanvasUIElement>& item) {
    return item->finalBounds.y + item->finalBounds.height;
}

float Right(const std::shared_ptr<UltraCanvasUIElement>& item) {
    return item->finalBounds.x + item->finalBounds.width;
}

void TestHorizontalTooShort() {
    std::printf("A horizontal toolbar asked for less than its items need\n");

    // UltraPaint's numbers exactly: 38 px asked for, 32 px buttons.
    auto toolbar = std::make_shared<UltraCanvasToolbar>("tb", 0, 0, 0, 38);
    auto a = Item("a", 30, 32), b = Item("b", 30, 32), c = Item("c", 30, 32);
    toolbar->AddChild(a);
    toolbar->AddChild(b);
    toolbar->AddChild(c);
    LayOut(toolbar, 400, 300);

    const float height = toolbar->finalBounds.height;
    Check(height >= 32.0f + 10.0f + 2.0f,
          "it grows to fit them - 32 px item, 5 px padding either side, 1 px border (got " +
                  std::to_string(height) + " px)");
    Check(Bottom(a) <= height + 0.01f && Bottom(b) <= height + 0.01f &&
                  Bottom(c) <= height + 0.01f,
          "so no item hangs past the bottom edge");
    Check(a->finalBounds.height == 32.0f,
          "and no item was squashed to make it fit");
}

void TestHorizontalGenerous() {
    std::printf("A horizontal toolbar asked for more than its items need\n");

    auto toolbar = std::make_shared<UltraCanvasToolbar>("tb-tall", 0, 0, 0, 60);
    auto a = Item("a", 30, 24);
    toolbar->AddChild(a);
    LayOut(toolbar, 400, 300);

    Check(std::fabs(toolbar->finalBounds.height - 60.0f) < 0.01f,
          "the thickness it was given is kept, so a host can still design a "
          "taller bar (got " + std::to_string(toolbar->finalBounds.height) + " px)");
}

void TestVerticalTooNarrow() {
    std::printf("A vertical toolbar asked for less than its items need\n");

    // A palette: constructed with a width, then turned vertical.
    auto palette = std::make_shared<UltraCanvasToolbar>("palette", 0, 0, 46, 0);
    palette->SetOrientation(ToolbarOrientation::Vertical);
    auto a = Item("a", 44, 30), b = Item("b", 44, 30);
    palette->AddChild(a);
    palette->AddChild(b);
    LayOut(palette, 300, 400);

    const float width = palette->finalBounds.width;
    Check(width >= 44.0f + 10.0f + 2.0f,
          "it widens to fit them (got " + std::to_string(width) + " px)");
    Check(Right(a) <= width + 0.01f && Right(b) <= width + 0.01f,
          "so no item hangs past the right edge");
    Check(palette->finalBounds.height >= 60.0f,
          "and turning it vertical did not leave its height pinned to the "
          "floor set for the horizontal shape it was constructed as (got " +
                  std::to_string(palette->finalBounds.height) + " px)");
}

void TestBuilderVerticalTakesItsWidth() {
    std::printf("A vertical toolbar from the builder, given a width\n");

    // Texter's markdown toolbar, as it is built.
    auto side = UltraCanvasToolbarBuilder("side")
            .SetOrientation(ToolbarOrientation::Vertical)
            .SetAppearance(ToolbarAppearance::Flat())
            .SetDimensions(0, 0, 40, 400)
            .Build();
    auto a = Item("a", 32, 32), b = Item("b", 32, 32);
    side->AddChild(a);
    side->AddChild(b);
    LayOut(side, 1600, 700);

    const float width = side->finalBounds.width;
    Check(width >= 32.0f && width <= 48.0f,
          "it is as wide as it was asked or its items need, not the builder's "
          "800 px default (got " + std::to_string(width) + " px)");

    // Same numbers, the calls the other way round.
    auto other = UltraCanvasToolbarBuilder("side-2")
            .SetDimensions(0, 0, 40, 400)
            .SetOrientation(ToolbarOrientation::Vertical)
            .Build();
    auto c = Item("c", 32, 32);
    other->AddChild(c);
    LayOut(other, 1600, 700);
    Check(other->finalBounds.width <= 48.0f,
          "and so it is when the dimensions come before the orientation (got " +
                  std::to_string(other->finalBounds.width) + " px)");
}

void TestBuilderHorizontalTakesItsHeight() {
    std::printf("A horizontal toolbar from the builder, given a thin height\n");

    // A status bar: 24 px asked for, 10 px items - 10 + 5 + 5 padding + 1 + 1
    // border = 22 px of content, so the 24 px floor is what decides.
    auto status = UltraCanvasToolbarBuilder("status")
            .SetOrientation(ToolbarOrientation::Horizontal)
            .SetDimensions(0, 0, 1024, 24)
            .Build();
    auto a = Item("a", 60, 10);
    status->AddChild(a);
    LayOut(status, 1024, 300);

    Check(std::fabs(status->finalBounds.height - 24.0f) < 0.01f,
          "it is the height it was given, not the builder's default 48 px (got " +
                  std::to_string(status->finalBounds.height) + " px)");
}

} // namespace

int main() {
    std::printf("===== UltraCanvas toolbar thickness =====\n");
    TestHorizontalTooShort();
    TestHorizontalGenerous();
    TestVerticalTooNarrow();
    TestBuilderVerticalTakesItsWidth();
    TestBuilderHorizontalTakesItsHeight();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
