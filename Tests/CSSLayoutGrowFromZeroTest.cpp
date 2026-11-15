// Tests/CSSLayoutGrowFromZeroTest.cpp
// A row measured "at most this wide" gives an item that grows from a zero
// basis the width it will be arranged at.
//
// The demo's Text Rendering page puts each sample in a row - a size caption,
// then the sample text with flex-grow 1 and flex-basis 0, so the text takes
// the rest of the line and wraps. The card (a column) measures the row for
// its height before any width is final, at "at most" the card's width, and
// the row measured the text at its base size: zero. A wrapping text measured
// zero wide is one word, or one letter, per line, so every row came out many
// times too tall - the card showed its first sample and then a blank area
// the height of the window.
//
// Pure geometry: the text stand-in wraps as a label does, a line for every
// `width` of its natural width.
//
// Usage: CSSLayoutGrowFromZeroTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

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
// width it is given, a line per 8 px "letter" when it is given none.
struct WrappingText : Element {
    float natural = 400.0f;
    static constexpr float kLine = 16.0f;
    static constexpr float kLetter = 8.0f;

    Size2Df MeasureOwnContent(std::optional<float> width, const LayoutContext&) override {
        if (!width.has_value()) return Size2Df(natural, kLine);
        const float w = std::max(kLetter, *width);
        return Size2Df(std::min(natural, w), kLine * std::ceil(natural / w));
    }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth = kLetter;
        intrinsic.maxContentWidth = natural;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = kLine;
    }
};

struct Fixed : Element {
    float width = 40.0f, height = 14.0f;
    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override { return Size2Df(width, height); }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth = intrinsic.maxContentWidth = width;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = height;
    }
};

struct Page {
    std::shared_ptr<Element> card, row;
    std::shared_ptr<WrappingText> text;
};

// A card (a stretching column) with one row: a 40 px caption, a 10 px gap,
// then the text, growing from `basis`.
static Page Build(float natural, Dimension basis, float grow) {
    Page p;
    p.card = std::make_shared<Element>();
    p.card->id = "card";
    p.card->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(AlignItems::Stretch);
    p.row = std::make_shared<Element>();
    p.row->id = "row";
    p.row->layout.SetFlexRow().SetFlexWrap(FlexWrap::NoWrap).SetFlexGap(10).SetFlexAlignItems(AlignItems::Stretch);
    p.row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(AlignSelf::Stretch);
    auto caption = std::make_shared<Fixed>();
    caption->id = "caption";
    caption->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    p.row->AddChild(caption);
    p.text = std::make_shared<WrappingText>();
    p.text->id = "text";
    p.text->natural = natural;
    p.text->layoutItem.SetFlexGrow(grow).SetFlexShrink(1).SetFlexBasis(basis);
    p.row->AddChild(p.text);
    p.card->AddChild(p.row);
    return p;
}

static void LayOut(Page& p, float width) {
    LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 800.0f;
    MeasureConstraints mc{ { ConstraintMode::Exact, width }, { ConstraintMode::Unbounded, INFINITY } };
    p.card->Measure(mc, ctx);
    p.card->Arrange(Rect2Df{ 0, 0, width, p.card->measured.measuredHeight }, ctx);
}

int main() {
    std::printf("CSSLayout grow-from-zero tests\n");

    std::printf("\n-- a text that fits the line: one line high --\n");
    {
        Page p = Build(400.0f, Dimension::Px(0), 1.0f);
        LayOut(p, 600.0f);
        CheckNear(p.row->finalBounds.height, 16.0f, "the row's height");
        CheckNear(p.card->measured.measuredHeight, 16.0f, "the card's height");
        CheckNear(p.text->finalBounds.width, 550.0f, "the text takes the rest of the line");
    }

    std::printf("\n-- a text longer than the line: the lines it wraps to there --\n");
    {
        Page p = Build(1200.0f, Dimension::Px(0), 1.0f);
        LayOut(p, 600.0f);
        // 1200 at 550 wide: three lines.
        CheckNear(p.row->finalBounds.height, 48.0f, "the row's height");
        CheckNear(p.card->measured.measuredHeight, 48.0f, "the card's height");
        CheckNear(p.text->finalBounds.width, 550.0f, "the text's width");
    }

    std::printf("\n-- an auto basis, as before --\n");
    {
        Page p = Build(1200.0f, Dimension::Auto(), 1.0f);
        LayOut(p, 600.0f);
        CheckNear(p.row->finalBounds.height, 48.0f, "the row's height");
    }

    std::printf("\n-- no grow: a zero basis stays zero wide --\n");
    {
        Page p = Build(400.0f, Dimension::Px(0), 0.0f);
        LayOut(p, 600.0f);
        CheckNear(p.text->finalBounds.width, 0.0f, "the text's width");
    }

    std::printf("\n-- the row measured alone, at most 600 wide: as wide as its content --\n");
    {
        Page p = Build(400.0f, Dimension::Px(0), 1.0f);
        LayoutContext ctx;
        MeasureConstraints mc{ { ConstraintMode::AtMost, 600.0f }, { ConstraintMode::Unbounded, INFINITY } };
        p.row->Measure(mc, ctx);
        CheckNear(p.row->measured.measuredWidth, 450.0f, "caption, gap and the text's own width");
        CheckNear(p.row->measured.measuredHeight, 16.0f, "one line high");
    }

    std::printf("\n%d failed\n", g_failures);
    return g_failures;
}
