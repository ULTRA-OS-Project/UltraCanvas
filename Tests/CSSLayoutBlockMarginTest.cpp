// Tests/CSSLayoutBlockMarginTest.cpp
// Block layout honours the margins of its in-flow children.
//
// Block layout (the default display) used to stack children at their
// border-box height and nothing else, so UltraCanvasUIElement::SetMargin() had
// no effect inside an ordinary container or group box. The demo's Group Box
// page set a 2px margin on every label, and its TreeView page had to switch its
// group boxes to a flex column to get any space between a tree and the options
// below it.
//
// What is pinned here: a margin offsets the child and adds to the stack;
// facing margins of two siblings add up (the engine does not collapse
// margins); horizontal margins narrow an auto-width child and resolve a
// percentage against the content width; an auto-sized parent grows to include
// its children's margins.
//
// No UI stack: the layout engine is pure geometry.
//
// Version: 1.0.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

static int g_failures = 0;

static void CheckNear(float actual, float expected, const char* what, float tol = 0.01f) {
    if (std::fabs(actual - expected) > tol) {
        std::printf("  FAIL: %s = %.2f (expected %.2f)\n", what, actual, expected);
        ++g_failures;
    } else {
        std::printf("  ok:   %s = %.2f\n", what, actual);
    }
}

static EdgeSizes Margins(float top, float right, float bottom, float left) {
    return { Dimension::Px(top), Dimension::Px(right), Dimension::Px(bottom), Dimension::Px(left) };
}

// A child with an explicit border-box size, like a widget built as (id, w, h).
static std::shared_ptr<Element> Box(float w, float h) {
    auto e = std::make_shared<Element>();
    e->size.width = Dimension::Px(w);
    e->size.height = Dimension::Px(h);
    return e;
}

static void LayOut(const std::shared_ptr<Element>& root, MeasureConstraints mc, float w, float h) {
    LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, root->measured.measuredWidth, root->measured.measuredHeight }, ctx);
}

// Two fixed-size children in a 300px column, parent height auto.
static void StackedSiblings() {
    std::printf("Stacked siblings\n");
    auto root = std::make_shared<Element>();
    root->box.padding = Margins(8, 8, 8, 8);
    auto first = Box(100, 20);
    first->box.margin = Margins(5, 0, 3, 0);
    auto second = Box(100, 20);
    second->box.margin = Margins(4, 0, 6, 10);
    root->AddChild(first);
    root->AddChild(second);

    LayOut(root, { { ConstraintMode::Exact, 316 }, { ConstraintMode::Unbounded, INFINITY } }, 316, 600);

    CheckNear(first->finalBounds.x, 8, "first x (padding, no left margin)");
    CheckNear(first->finalBounds.y, 8 + 5, "first y (padding + top margin)");
    CheckNear(second->finalBounds.x, 8 + 10, "second x (padding + left margin)");
    // 3px below the first and 4px above the second add up: no collapsing.
    CheckNear(second->finalBounds.y, 8 + 5 + 20 + 3 + 4, "second y (margins add)");
    CheckNear(second->finalBounds.width, 100, "second keeps its explicit width");
    CheckNear(root->measured.measuredHeight, 8 + 5 + 20 + 3 + 4 + 20 + 6 + 8,
              "parent auto height includes every margin");
}

// Auto-width children stretch to the content width less their margins.
static void HorizontalMargins() {
    std::printf("Horizontal margins\n");
    auto root = std::make_shared<Element>();
    auto px = std::make_shared<Element>();
    px->size.height = Dimension::Px(20);
    px->box.margin = Margins(0, 15, 0, 10);
    auto pct = std::make_shared<Element>();
    pct->size.height = Dimension::Px(20);
    pct->box.margin = { Dimension::Px(0), Dimension::Pct(10), Dimension::Px(0), Dimension::Pct(10) };
    root->AddChild(px);
    root->AddChild(pct);

    LayOut(root, { { ConstraintMode::Exact, 300 }, { ConstraintMode::Unbounded, INFINITY } }, 300, 600);

    CheckNear(px->finalBounds.x, 10, "px child x");
    CheckNear(px->finalBounds.width, 300 - 10 - 15, "px child narrowed by its margins");
    CheckNear(pct->finalBounds.x, 30, "10% left margin of a 300px content box");
    CheckNear(pct->finalBounds.width, 240, "pct child narrowed by 10% on each side");
}

// A parent whose width is not fixed grows to its widest child plus margins.
static void ShrinkToFitParent() {
    std::printf("Auto-width parent\n");
    auto root = std::make_shared<Element>();
    auto child = Box(100, 20);
    child->box.margin = Margins(0, 12, 0, 12);
    root->AddChild(child);

    LayOut(root, { { ConstraintMode::AtMost, 500 }, { ConstraintMode::Unbounded, INFINITY } }, 500, 600);

    CheckNear(root->measured.measuredWidth, 124, "parent width = child + both margins");
    CheckNear(child->finalBounds.x, 12, "child x");
}

int main() {
    StackedSiblings();
    HorizontalMargins();
    ShrinkToFitParent();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All checks passed\n");
    return 0;
}
