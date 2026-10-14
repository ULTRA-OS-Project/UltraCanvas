// Tests/UIElementBoxSizingTest.cpp
// A widget's width and height are its whole box - padding and border
// included - whichever UltraCanvasUIElement constructor built it. The
// size-only constructor (id, w, h) used to keep the layout engine's
// content-box default while the full-rect one (id, x, y, w, h) set
// border-box, so the same tree view with the same size and its 1px border
// came out 2px larger when built without a position.
//
// Headless: elements laid out by the CSSLayout engine, no window.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "CSSLayout/CSSLayout.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void CheckNear(float actual, float expected, const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= 0.01f;
    std::printf("  %s %s = %.2f (expected %.2f)\n", ok ? "ok:  " : "FAIL:", what.c_str(),
                actual, expected);
    if (!ok) ++g_failures;
}

// The border box an element gets inside a 500 x 500 block.
Rect2Df LayOut(const std::shared_ptr<UltraCanvasUIElement>& element) {
    auto root = std::make_shared<UltraCanvasContainer>("root", 0, 0, 500, 500);
    root->AddChild(element);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = 500;
    ctx.viewportHeight = 500;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 500 },
                                      { CSSLayout::ConstraintMode::Exact, 500 } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, 500, 500 }, ctx);
    return element->finalBounds;
}

}  // namespace

int main() {
    std::printf("every constructor is border-box\n");

    auto sized = std::make_shared<UltraCanvasUIElement>("sized", 120.f, 80.f);
    CheckNear(static_cast<float>(sized->box.boxSizing == CSSLayout::BoxSizing::BorderBox), 1.f,
              "the size-only constructor sets border-box");
    auto plain = std::make_shared<UltraCanvasUIElement>("plain");
    CheckNear(static_cast<float>(plain->box.boxSizing == CSSLayout::BoxSizing::BorderBox), 1.f,
              "and so does the default one");

    // 120 x 80 with 10px padding and a 2px border: the box stays 120 x 80.
    sized->SetPadding(10.f);
    sized->SetBorders(2.f);
    const Rect2Df a = LayOut(sized);
    CheckNear(a.width, 120.f, "size-only: width includes padding and border");
    CheckNear(a.height, 80.f, "size-only: height includes padding and border");

    auto placed = std::make_shared<UltraCanvasUIElement>("placed", 0.f, 0.f, 120.f, 80.f);
    placed->SetPadding(10.f);
    placed->SetBorders(2.f);
    const Rect2Df b = LayOut(placed);
    CheckNear(b.width, a.width, "full-rect: the same width");
    CheckNear(b.height, a.height, "full-rect: the same height");

    // The case that showed it: a tree view, which draws a 1px border.
    auto treeA = std::make_shared<UltraCanvasTreeView>("treeA", 200.f, 150.f);
    auto treeB = std::make_shared<UltraCanvasTreeView>("treeB", 0, 0, 200, 150);
    const Rect2Df ta = LayOut(treeA);
    const Rect2Df tb = LayOut(treeB);
    CheckNear(ta.width, 200.f, "tree view (id, w, h): 200 wide");
    CheckNear(ta.width, tb.width, "the same as built with a position");
    CheckNear(ta.height, tb.height, "and as tall");

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
