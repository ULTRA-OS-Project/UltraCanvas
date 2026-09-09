// Tests/DialogIconLayoutTest.cpp
// The dialog content row with and without the severity icon.
//
// Pins what DialogConfig::showIcon buys (see core/UltraCanvasModalDialog.cpp
// and Docs/UltraCanvas/UltraCanvasAlert.md): the icon lives in its own column
// left of the message, so an element the caller centres in the message column
// - the progress dialog's ring - is centred on that column, not on the window.
// Hiding the icon must remove the column AND the flex gap that follows it, so
// the message column spans the full content width and the ring lands on the
// window's centre line. Merely making the icon invisible would leave the offset
// in place, which is the bug this pins.
//
// No UI stack: the same flex tree the dialog builds, on the real layout engine,
// so every assertion is about numbers rather than pixels.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

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

// ===== THE DIALOG'S CONTENT ROW =====
// The numbers are ModalDialogStyle's defaults and UltraCanvasProgressDialog's
// window width: padding 16, icon 48 square, icon/message gap 12, ring 132
// square, window 460 wide.
static constexpr float kWindowWidth = 460.0f;
static constexpr float kPadding     = 16.0f;
static constexpr float kIconSize    = 48.0f;
static constexpr float kIconGap     = 12.0f;
static constexpr float kRingSize    = 132.0f;
static constexpr float kMessageHeight = 40.0f;

// A leaf that reports a fixed intrinsic size, standing in for the message area
// (whose height needs a render context) and for the ring chart.
struct BoxStub : Element {
    float width = 0.0f, height = 0.0f;

    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override {
        return Size2Df(width, height);
    }
    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.minContentWidth  = intrinsic.maxContentWidth  = width;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = height;
    }
};

struct Content {
    std::shared_ptr<Element> section;   // ContentSection: the icon + message row
    std::shared_ptr<Element> icon;      // IconContainer
    std::shared_ptr<Element> message;   // MessageContainer
    std::shared_ptr<BoxStub> ring;      // what AddDialogElement() put in it
};

// The tree UltraCanvasModalDialog::CreateContentSection() builds.
static Content MakeContent(bool showIcon) {
    Content c;

    c.section = std::make_shared<Element>();
    c.section->id = "ContentSection";
    c.section->box.boxSizing = BoxSizing::BorderBox;
    c.section->box.padding = { Dimension::Px(kPadding), Dimension::Px(kPadding),
                               Dimension::Px(kPadding), Dimension::Px(kPadding) };
    c.section->layout.SetFlexRow()
                     .SetFlexGap(kIconGap)
                     .SetFlexAlignItems(AlignItems::Stretch);

    c.icon = std::make_shared<Element>();
    c.icon->id = "IconContainer";
    c.icon->size.width  = Dimension::Px(kIconSize);
    c.icon->size.height = Dimension::Px(kIconSize);
    c.icon->layoutItem.SetAlignSelf(AlignSelf::Start);
    // SetVisible(false) is layout.Hide(): display:none, out of the flow.
    if (!showIcon) c.icon->layout.Hide();
    c.section->AddChild(c.icon);

    c.message = std::make_shared<Element>();
    c.message->id = "MessageContainer";
    c.message->layout.SetFlexColumn()
                     .SetFlexGap(6)
                     .SetFlexAlignItems(AlignItems::Stretch);
    c.message->layoutItem.SetFlexGrow(1);

    auto text = std::make_shared<BoxStub>();
    text->id = "MessageArea";
    text->width = 100.0f;               // wraps: takes whatever width it is given
    text->height = kMessageHeight;
    text->layoutItem.SetFlexGrow(1);
    c.message->AddChild(text);

    c.ring = std::make_shared<BoxStub>();
    c.ring->id = "ProgressRing";
    c.ring->width = c.ring->height = kRingSize;
    c.ring->size.width  = Dimension::Px(kRingSize);
    c.ring->size.height = Dimension::Px(kRingSize);
    c.ring->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(AlignSelf::Center);
    c.message->AddChild(c.ring);

    c.section->AddChild(c.message);
    return c;
}

static void LayOut(Content& c, float windowWidth = kWindowWidth) {
    LayoutContext ctx;
    ctx.viewportWidth  = windowWidth;
    ctx.viewportHeight = 320.0f;
    MeasureConstraints mc{ { ConstraintMode::Exact, windowWidth },
                           { ConstraintMode::AtMost, 320.0f } };
    c.section->Measure(mc, ctx);
    c.section->Arrange(Rect2Df{ 0, 0, windowWidth,
                                c.section->measured.measuredHeight }, ctx);
}

// The ring's centre in the window's coordinates: the section is the window's
// full-width child, so its own origin is 0.
static float RingCentreX(const Content& c) {
    return c.section->finalBounds.x + c.message->finalBounds.x +
           c.ring->finalBounds.x + c.ring->finalBounds.width / 2.0f;
}

int main() {
    std::printf("Dialog icon layout tests\n");

    const float windowCentre = kWindowWidth / 2.0f;
    const float contentWidth = kWindowWidth - 2 * kPadding;

    std::printf("\n-- with the icon: the message column starts right of it --\n");
    {
        Content c = MakeContent(/*showIcon=*/true);
        LayOut(c);
        CheckNear(c.icon->finalBounds.width, kIconSize, "icon column width");
        CheckNear(c.message->finalBounds.x, kPadding + kIconSize + kIconGap,
                  "message column left edge");
        CheckNear(c.message->finalBounds.width,
                  contentWidth - kIconSize - kIconGap, "message column width");
        // Centred in its own column, which is 60px right of the window's.
        CheckNear(RingCentreX(c),
                  windowCentre + (kIconSize + kIconGap) / 2.0f,
                  "ring centre (offset by the icon column)");
        CHECK(RingCentreX(c) - windowCentre > 20.0f,
              "ring sits visibly right of the window centre");
    }

    std::printf("\n-- without the icon: the message column takes the full width --\n");
    {
        Content c = MakeContent(/*showIcon=*/false);
        LayOut(c);
        CheckNear(c.icon->finalBounds.width, 0.0f, "hidden icon takes no width");
        CheckNear(c.message->finalBounds.x, kPadding,
                  "message column starts at the content padding");
        // The gap belongs to the icon: an out-of-flow item must not leave one.
        CheckNear(c.message->finalBounds.width, contentWidth,
                  "message column width (no icon column, no gap)");
        CheckNear(RingCentreX(c), windowCentre, "ring centre = window centre");
    }

    std::printf("\n-- the ring stays centred as the window is resized --\n");
    for (float width : { 360.0f, 460.0f, 640.0f }) {
        Content c = MakeContent(/*showIcon=*/false);
        LayOut(c, width);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "ring centred at window width %.0f", width);
        CHECK(std::fabs(RingCentreX(c) - width / 2.0f) < 0.01f, msg);
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
