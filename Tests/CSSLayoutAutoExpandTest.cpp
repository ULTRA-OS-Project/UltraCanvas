// Tests/CSSLayoutAutoExpandTest.cpp
// Auto-expanding, centred card layout on the real CSSLayout engine.
//
// Pins the behaviour the UltraMail account tile relies on (see
// Apps/UltraMail/ui/UltraMailAccountBar.cpp and Docs/CSSLayout.md):
// a flex-column card with an auto width grows as an inner row of auto-sized
// counters gets wider, keeps every row centred on the card's centre line, and
// never shrinks below its minimum width. A card with an explicit width does
// NOT grow — the case the tile used to hit, where the counter row spilled past
// the frame and the container clipped it.
//
// No UI stack: the layout engine is pure geometry, so every assertion is about
// numbers, not pixels. The leaf stand-ins below report the same intrinsic
// sizes UltraCanvasBadge / UltraCanvasLabel publish at runtime, without the
// render context those need to measure real text.
//
// Version: 1.0.0
// Last Modified: 2026-09-04
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

// ===== LEAF STAND-INS =====

// UltraCanvasBadge: auto-sizes to its text, with a minWidth floor that keeps
// short counts square, and a fixed height.
struct CounterStub : Element {
    std::string text;
    float height = 34.0f, minWidth = 34.0f, paddingH = 10.0f, charWidth = 9.0f;

    float NaturalWidth() const {
        return std::max(minWidth, static_cast<float>(text.size()) * charWidth + 2 * paddingH);
    }
    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override {
        return Size2Df(NaturalWidth(), height);
    }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth  = intrinsic.maxContentWidth  = NaturalWidth();
        intrinsic.minContentHeight = intrinsic.maxContentHeight = height;
    }
};

// UltraCanvasLabel: a single unwrapped line.
struct LabelStub : Element {
    std::string text;
    float fontSize = 15.0f;

    float NaturalWidth() const { return static_cast<float>(text.size()) * fontSize * 0.6f; }
    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override {
        return Size2Df(NaturalWidth(), fontSize * 1.3f);
    }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth  = intrinsic.maxContentWidth  = NaturalWidth();
        intrinsic.minContentHeight = intrinsic.maxContentHeight = fontSize * 1.3f;
    }
};

// ===== THE CARD UNDER TEST =====

static constexpr float kSide = 176.0f;   // the design's square baseline

enum class Sizing {
    AutoWithMinimum,   // what the account tile does now
    FixedWidth         // what it used to do
};

struct Card {
    std::shared_ptr<Element> bar;    // the flex row the card lives in
    std::shared_ptr<Element> root;
    std::shared_ptr<Element> counters;
    std::shared_ptr<Element> name;
    std::shared_ptr<CounterStub> first;
};

static Card MakeCard(Sizing sizing, const char* a, const char* b, const char* c) {
    Card card;
    card.root = std::make_shared<Element>();
    card.root->id = "card";
    card.root->box.boxSizing = BoxSizing::BorderBox;
    card.root->box.padding = { Dimension::Px(10), Dimension::Px(10),
                               Dimension::Px(10), Dimension::Px(10) };
    card.root->box.border  = { Dimension::Px(2), Dimension::Px(2),
                               Dimension::Px(2), Dimension::Px(2) };
    card.root->size.height = Dimension::Px(kSide);
    if (sizing == Sizing::FixedWidth) {
        card.root->size.width = Dimension::Px(kSide);
    } else {
        BoxConstraints limits;
        limits.minWidth = Dimension::Px(kSide);
        card.root->boxConstraints = limits;
    }
    card.root->layout.SetFlexColumn()
                     .SetFlexGap(10)
                     .SetFlexJustifyContent(JustifyContent::Center)
                     .SetFlexAlignItems(AlignItems::Center);

    auto letter = std::make_shared<LabelStub>();
    letter->id = "letter"; letter->text = "G"; letter->fontSize = 40.0f;
    card.root->AddChild(letter);

    card.name = std::make_shared<LabelStub>();
    card.name->id = "name";
    std::static_pointer_cast<LabelStub>(card.name)->text = "testmail23@";
    card.root->AddChild(card.name);

    card.counters = std::make_shared<Element>();
    card.counters->id = "counters";
    card.counters->layout.SetFlexRow()
                         .SetFlexGap(6)
                         .SetFlexJustifyContent(JustifyContent::Center)
                         .SetFlexAlignItems(AlignItems::Center);
    const char* texts[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) {
        auto counter = std::make_shared<CounterStub>();
        counter->id = std::string("counter") + std::to_string(i);
        counter->text = texts[i];
        card.counters->AddChild(counter);
        if (i == 0) card.first = counter;
    }
    card.root->AddChild(card.counters);
    return card;
}

// Lay the card out where it actually lives: as an item of the account bar, a
// flex row that stretches its items. The parent matters — a flex container
// applies an item's boxConstraints on the main axis (here, the width), and it
// is the parent that pins an explicitly-sized item to its own width.
static void LayOut(Card& card, float barWidth = 1000.0f) {
    if (!card.root->Parent()) {
        card.bar = std::make_shared<Element>();
        card.bar->id = "bar";
        card.bar->layout.SetFlexRow()
                        .SetFlexGap(20)
                        .SetFlexAlignItems(AlignItems::Stretch);
        card.bar->AddChild(card.root);
    }
    LayoutContext ctx;
    ctx.viewportWidth = barWidth;
    ctx.viewportHeight = 600.0f;
    MeasureConstraints mc{ { ConstraintMode::Exact, barWidth },
                           { ConstraintMode::AtMost, 400.0f } };
    card.bar->Measure(mc, ctx);
    card.bar->Arrange(Rect2Df{ 0, 0, barWidth, card.bar->measured.measuredHeight }, ctx);
}

static float CentreX(const Element& e) { return e.finalBounds.x + e.finalBounds.width / 2.0f; }

// Every row's centre must sit on the card's centre line.
static void CheckCentred(const Card& card, const char* what) {
    const float cardCentre = card.root->finalBounds.width / 2.0f;
    char msg[160];
    std::snprintf(msg, sizeof(msg), "%s: counters centred on the card", what);
    CHECK(std::fabs(CentreX(*card.counters) - cardCentre) < 0.01f, msg);
    std::snprintf(msg, sizeof(msg), "%s: name centred on the card", what);
    CHECK(std::fabs(CentreX(*card.name) - cardCentre) < 0.01f, msg);
}

int main() {
    std::printf("CSSLayout auto-expand tests\n");

    // Counter widths: max(minWidth 34, digits * 9 + 2 * paddingH 10).
    // "12" and "36" are 38 wide, the single "5" stays at the 34 floor.
    const float shortRow = 38.0f + 6.0f + 38.0f + 6.0f + 34.0f;
    // Four digits: 4*9 + 20 = 56; five digits: 5*9 + 20 = 65.
    const float longRow = 56.0f + 6.0f + 65.0f + 6.0f + 34.0f;
    const float inset = (10.0f + 2.0f) * 2;   // padding + border, both sides

    std::printf("\n-- auto width, short counts: stays at the design's square --\n");
    {
        Card card = MakeCard(Sizing::AutoWithMinimum, "12", "36", "5");
        LayOut(card);
        CheckNear(card.root->finalBounds.width, kSide, "card width");
        CheckNear(card.root->finalBounds.height, kSide, "card height");
        CheckNear(card.counters->finalBounds.width, shortRow, "counter row width");
        CheckCentred(card, "short counts");
    }

    std::printf("\n-- auto width, long counts: the card grows to fit --\n");
    {
        Card card = MakeCard(Sizing::AutoWithMinimum, "1234", "98765", "7");
        LayOut(card);
        CheckNear(card.counters->finalBounds.width, longRow, "counter row width");
        CheckNear(card.root->finalBounds.width, longRow + inset, "card width");
        CHECK(card.root->finalBounds.width > kSide, "card grew past its minimum");
        CheckNear(card.root->finalBounds.height, kSide, "card height unchanged");
        // The row must sit inside the frame, not spill into border + padding.
        CHECK(card.counters->finalBounds.x >= 11.99f,
              "counter row stays inside the frame's padding");
        CheckCentred(card, "long counts");
    }

    std::printf("\n-- a count that grows at runtime re-measures and re-centres --\n");
    {
        Card card = MakeCard(Sizing::AutoWithMinimum, "12", "36", "5");
        LayOut(card);
        const float before = card.root->finalBounds.width;

        // What UltraCanvasBadge::SetCount does: change the content, then
        // InvalidateLayout() so the engine drops the cached measurement.
        card.first->text = "123456";
        card.first->InvalidateLayout();
        LayOut(card);

        CHECK(card.root->finalBounds.width > before, "card widened after the count grew");
        CheckNear(card.first->finalBounds.width, 6 * 9.0f + 20.0f, "grown counter width");
        CheckCentred(card, "after the count grew");
    }

    std::printf("\n-- an explicit width does NOT grow (the old fixed tile) --\n");
    {
        Card card = MakeCard(Sizing::FixedWidth, "1234", "98765", "7");
        LayOut(card);
        CheckNear(card.root->finalBounds.width, kSide, "card width pinned to the explicit size");
        CHECK(card.counters->finalBounds.width > kSide - inset,
              "counter row is wider than the frame's content area");
        CHECK(card.counters->finalBounds.x < 12.0f,
              "counter row spills into the frame (clipped at render time)");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
