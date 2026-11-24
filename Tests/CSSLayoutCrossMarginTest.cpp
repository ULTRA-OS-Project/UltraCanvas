// Tests/CSSLayoutCrossMarginTest.cpp
// A flex item is measured at the cross extent it will be arranged at: the
// container's, less the item's own cross margins.
//
// The demo's Group Box page puts captions with a 2 px margin in a stretching
// column. The column measured each caption's height at its own full width and
// then arranged it 4 px narrower. In English every caption fits either way;
// in German one caption ("Die Beschriftung sitzt im oberen Rahmen.") fits on
// one line at the full width and needs two at the narrower one, so it was
// given one line's height and its second line was cut off.
//
// Pure geometry: the text stand-in wraps as a label does, a line for every
// `width` of its natural width.
//
// Usage: CSSLayoutCrossMarginTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-24
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

static void CheckNear(float actual, float expected, const char* what, float tol = 0.01f) {
    if (std::fabs(actual - expected) > tol) {
        std::printf("  FAIL: %s = %.2f (expected %.2f)\n", what, actual, expected);
        ++g_failures;
    } else {
        std::printf("  ok:   %s = %.2f\n", what, actual);
    }
}

// A label that wraps: `natural` wide on one line, a 16 px line for every
// width it is given.
struct WrappingText : Element {
    float natural = 298.0f;
    static constexpr float kLine = 16.0f;

    Size2Df MeasureOwnContent(std::optional<float> width, const LayoutContext&) override {
        if (!width.has_value()) return Size2Df(natural, kLine);
        const float w = std::max(1.0f, *width);
        return Size2Df(std::min(natural, w), kLine * std::ceil(natural / w - 1e-4f));
    }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth = 40.0f;
        intrinsic.maxContentWidth = natural;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = kLine;
    }
};

struct Column {
    std::shared_ptr<Element> column;
    std::shared_ptr<WrappingText> text;
};

// A stretching column with one text in it, `margin` px either side of it.
static Column Build(float natural, float margin) {
    Column c;
    c.column = std::make_shared<Element>();
    c.column->id = "column";
    c.column->layout.SetFlexColumn().SetFlexAlignItems(AlignItems::Stretch);
    c.text = std::make_shared<WrappingText>();
    c.text->id = "text";
    c.text->natural = natural;
    c.text->box.margin.left = Dimension::Px(margin);
    c.text->box.margin.right = Dimension::Px(margin);
    c.text->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    c.column->AddChild(c.text);
    return c;
}

static void LayOut(Column& c, ConstraintMode widthMode, float width) {
    LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 800.0f;
    MeasureConstraints mc{ { widthMode, width }, { ConstraintMode::Unbounded, INFINITY } };
    c.column->Measure(mc, ctx);
    c.column->Arrange(Rect2Df{ 0, 0, width, c.column->measured.measuredHeight }, ctx);
}

int main() {
    std::printf("CSSLayout cross-margin tests\n");

    // 298 wide: one line at the column's 300, two at the 296 the margins leave.
    std::printf("\n-- a column of exactly 300: the text gets 296 and two lines --\n");
    {
        Column c = Build(298.0f, 2.0f);
        LayOut(c, ConstraintMode::Exact, 300.0f);
        CheckNear(c.text->finalBounds.width, 296.0f, "the text's width");
        CheckNear(c.text->finalBounds.height, 32.0f, "the text's height (two lines)");
        CheckNear(c.column->measured.measuredHeight, 32.0f, "the column's height");
    }

    std::printf("\n-- a column measured at most 300 wide, as a card measures it --\n");
    {
        Column c = Build(298.0f, 2.0f);
        LayOut(c, ConstraintMode::AtMost, 300.0f);
        CheckNear(c.text->finalBounds.height, 32.0f, "the text's height (two lines)");
        CheckNear(c.column->measured.measuredHeight, 32.0f, "the column's height");
    }

    std::printf("\n-- no margins: one line, as before --\n");
    {
        Column c = Build(298.0f, 0.0f);
        LayOut(c, ConstraintMode::Exact, 300.0f);
        CheckNear(c.text->finalBounds.width, 300.0f, "the text's width");
        CheckNear(c.text->finalBounds.height, 16.0f, "the text's height (one line)");
    }

    std::printf("\n-- a text that fits even with its margins: one line --\n");
    {
        Column c = Build(250.0f, 2.0f);
        LayOut(c, ConstraintMode::Exact, 300.0f);
        CheckNear(c.text->finalBounds.height, 16.0f, "the text's height (one line)");
    }

    std::printf("\n%d failed\n", g_failures);
    return g_failures;
}
