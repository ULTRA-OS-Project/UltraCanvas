// Tests/CSSLayoutSafeAlignTest.cpp
// Safe alignment: content that does not fit its line (flex) or its grid area
// stays reachable.
//
// The case this pins is the demo's "LaTeX Documents" page. Its rendered-output
// pane is a flex row with justify-content/align-items centre, and a tall
// formula is taller than the pane, so the pane scrolls. Centring an item that
// does not fit gives it a NEGATIVE offset - half the overflow lands above the
// pane's content origin, and nothing there can be scrolled to, because a
// container clips to its content box and its scrollbar starts at that edge.
// The top of the formula was simply gone, with the scrollbar already at the
// top. Aligning an overflowing item to the start instead (CSS Box Alignment's
// "safe" fallback) puts the whole overflow at the end, where the scrollbar
// reaches it.
//
// No UI stack: the layout engine is pure geometry, so every assertion is about
// numbers rather than pixels. The scroll-range arithmetic mirrors
// UltraCanvasContainer::UpdateScrollability, which measures each child's extent
// from the container's content-box origin.
//
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static void CheckNear(float actual, float expected, const char* what, float tol = 0.01f) {
    if (std::fabs(actual - expected) > tol) {
        std::printf("  FAIL: %s = %.2f (expected %.2f)\n", what, actual, expected);
        ++g_failures;
    } else {
        std::printf("  ok:   %s = %.2f\n", what, actual);
    }
}

// A leaf of a fixed intrinsic size: a typeset formula, whose size comes from
// the LaTeX engine and neither grows nor wraps.
struct FormulaStub : Element {
    float w = 0.0f, h = 0.0f;

    FormulaStub(float width, float height) : w(width), h(height) {}

    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth  = intrinsic.maxContentWidth  = w;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = h;
    }
    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override {
        return { w, h };
    }
};

static std::shared_ptr<FormulaStub> Formula(float w, float h) {
    return std::make_shared<FormulaStub>(w, h);
}

static void LayOut(const std::shared_ptr<Element>& root, float w, float h) {
    LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    MeasureConstraints mc{ { ConstraintMode::Exact, w }, { ConstraintMode::Exact, h } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, w, h }, ctx);
}

// The rendered-output pane: a centring flex row with the given padding.
static std::shared_ptr<Element> Pane(FlexDirection dir, float padding = 0.0f) {
    auto pane = std::make_shared<Element>();
    pane->id = "pane";
    pane->box.boxSizing = BoxSizing::BorderBox;
    if (padding > 0.0f) {
        pane->box.padding = { Dimension::Px(padding), Dimension::Px(padding),
                              Dimension::Px(padding), Dimension::Px(padding) };
    }
    pane->layout.SetFlexDirection(dir)
                .SetFlexJustifyContent(JustifyContent::Center)
                .SetFlexAlignItems(AlignItems::Center);
    return pane;
}

// What UltraCanvasContainer::UpdateScrollability computes: the content extent
// below the content-box origin, and how far the scrollbar can therefore travel.
static float ScrollRange(const Element& child, float originY, float viewportH) {
    const float contentBottom = (child.finalBounds.y - originY) + child.finalBounds.height;
    return std::max(0.0f, contentBottom - viewportH);
}

int main() {
    std::printf("=== CSSLayout safe alignment ===\n");

    constexpr float kPaneW = 400.0f, kPaneH = 200.0f;

    std::printf("\n-- a flex row centres an item that fits --\n");
    {
        auto pane = Pane(FlexDirection::Row);
        auto formula = Formula(300.0f, 120.0f);
        pane->AddChild(formula);
        LayOut(pane, kPaneW, kPaneH);

        CheckNear(formula->finalBounds.x, (kPaneW - 300.0f) / 2.0f, "fitting item centred on x");
        CheckNear(formula->finalBounds.y, (kPaneH - 120.0f) / 2.0f, "fitting item centred on y");
        CheckNear(ScrollRange(*formula, 0.0f, kPaneH), 0.0f, "nothing to scroll");
    }

    std::printf("\n-- an item taller than the pane starts at the top, not above it --\n");
    {
        auto pane = Pane(FlexDirection::Row);
        auto formula = Formula(300.0f, 500.0f);   // the tall formula
        pane->AddChild(formula);
        LayOut(pane, kPaneW, kPaneH);

        CheckNear(formula->finalBounds.y, 0.0f, "overflowing item top at the content origin");
        CHECK(formula->finalBounds.y >= 0.0f,
              "no part of the item sits above the pane (it would be unreachable)");
        // The whole overflow is below the viewport, so the scrollbar reaches
        // every last pixel of it. Unsafe centring halved this range and lost
        // the other half off the top.
        CheckNear(ScrollRange(*formula, 0.0f, kPaneH), 500.0f - kPaneH,
                  "scroll range covers the whole overflow");
        CheckNear(formula->finalBounds.x, (kPaneW - 300.0f) / 2.0f,
                  "the axis that does fit is still centred");
    }

    std::printf("\n-- align-self: end is safe too --\n");
    {
        auto pane = Pane(FlexDirection::Row);
        auto formula = Formula(300.0f, 500.0f);
        formula->layoutItem.SetAlignSelf(AlignSelf::End);
        pane->AddChild(formula);
        LayOut(pane, kPaneW, kPaneH);

        CheckNear(formula->finalBounds.y, 0.0f, "end-aligned overflowing item starts at the origin");
    }

    std::printf("\n-- the cross axis of a flex column is the horizontal one --\n");
    {
        auto pane = Pane(FlexDirection::Column);
        auto formula = Formula(900.0f, 100.0f);   // wider than the pane
        pane->AddChild(formula);
        LayOut(pane, kPaneW, kPaneH);

        CheckNear(formula->finalBounds.x, 0.0f, "too-wide item starts at the left content edge");
    }

    std::printf("\n-- the pane's padding is the origin, not zero --\n");
    {
        auto pane = Pane(FlexDirection::Row, 8.0f);
        auto formula = Formula(300.0f, 500.0f);
        pane->AddChild(formula);
        LayOut(pane, kPaneW, kPaneH);

        CheckNear(formula->finalBounds.y, 8.0f, "overflowing item starts at the padding edge");
        CheckNear(ScrollRange(*formula, 8.0f, kPaneH - 16.0f), 500.0f - (kPaneH - 16.0f),
                  "scroll range covers the whole overflow");
    }

    std::printf("\n-- the LaTeX page's shape: header, growing render pane, source --\n");
    {
        // Apps/DemoApp/UltraCanvasLaTeXExamples.cpp builds exactly this: a flex
        // column page, a fixed header label, a growing centred render area with
        // the live formula in it, and a source view below.
        auto page = std::make_shared<Element>();
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(AlignItems::Stretch);

        auto header = Formula(200.0f, 20.0f);
        header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(header);

        auto renderArea = Pane(FlexDirection::Row);
        renderArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(AlignSelf::Stretch);
        auto formula = Formula(600.0f, 700.0f);
        formula->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(AlignSelf::Center);
        renderArea->AddChild(formula);
        page->AddChild(renderArea);

        auto source = Formula(200.0f, 150.0f);
        source->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(AlignSelf::Stretch);
        page->AddChild(source);

        LayOut(page, 800.0f, 600.0f);

        const float paneH = renderArea->finalBounds.height;
        CHECK(paneH > 0.0f && paneH < 700.0f, "the render pane is shorter than the formula");
        CheckNear(formula->finalBounds.y, 0.0f,
                  "the formula's first line is inside the pane at scroll position 0");
        CheckNear(ScrollRange(*formula, 0.0f, paneH), 700.0f - paneH,
                  "every line below the fold is reachable by scrolling");
    }

    std::printf("\n-- the grid engine needs no such fallback: it clamps to the area --\n");
    {
        // ArrangeGrid sizes a non-stretch item to min(track, natural), so an
        // item can never be larger than the area it is centred in and its
        // offset can never go negative. Pinned here so that the day that
        // clamp changes, the safe-alignment rule is applied there too.
        auto grid = std::make_shared<Element>();
        grid->layout.SetGrid();
        GridTrackSize fixed;
        fixed.kind = GridTrackSizeKind::Fixed;
        fixed.value = Dimension::Px(100);
        grid->layout.SetGridColumns({fixed, fixed});
        GridTrackSize row;
        row.kind = GridTrackSizeKind::Fixed;
        row.value = Dimension::Px(80);
        grid->layout.SetGridRows({row});

        auto small = Formula(40.0f, 20.0f);
        small->layoutItem.SetJustifySelf(JustifySelf::Center).SetGridAlignSelf(AlignSelf::Center);
        grid->AddChild(small);

        auto big = Formula(260.0f, 300.0f);      // larger than its 100x80 area
        big->layoutItem.SetJustifySelf(JustifySelf::Center).SetGridAlignSelf(AlignSelf::Center);
        grid->AddChild(big);

        LayOut(grid, 200.0f, 80.0f);

        CheckNear(small->finalBounds.x, (100.0f - 40.0f) / 2.0f, "fitting item centred in its area");
        CheckNear(small->finalBounds.y, (80.0f - 20.0f) / 2.0f, "fitting item centred vertically");
        CheckNear(big->finalBounds.x, 100.0f, "oversized item starts at its area's left edge");
        CheckNear(big->finalBounds.y, 0.0f, "oversized item starts at its area's top edge");
        CheckNear(big->finalBounds.width, 100.0f, "oversized item is clamped to its area's width");
        CheckNear(big->finalBounds.height, 80.0f, "oversized item is clamped to its area's height");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
