// Tests/TextInputFontTest.cpp
// A text field shows a new font as soon as it is set.
//
// UltraCanvasTextInput::SetFontSize and SetStyle only stored the new font:
// nothing invalidated the layout or asked for a redraw, so the field went on
// drawing the old size until something else repainted it, and a parent that
// sized itself from the field kept the old measure. They now go through
// FontChanged (scroll re-clamped, layout invalidated, redraw), as the Label
// and Button font setters always did.
//
// The field's line box - the height of its text, selection and caret - is
// the font's line height unrounded. It was GetTextLineHeight("H"), which
// truncates to whole pixels (17.94 became 17), so the caret stopped a pixel
// short of the descenders. IRenderContext::GetSingleLineHeight gives the
// exact figure; TextMetricsScreenshotTest checks the caret on screen.
//
// Runs headless: a probe subclass counts the redraw requests and the layout
// invalidations; the field is measured without a window, and the line height
// in an offscreen context.
// Version: 1.1.0 - the font's line height, unrounded
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextInput.h"

#include <cmath>
#include <iostream>
#include <memory>

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

namespace {

// Counts what the field asks of the framework.
class ProbeInput : public UltraCanvasTextInput {
public:
    using UltraCanvasTextInput::UltraCanvasTextInput;
    int redraws = 0;
    int invalidations = 0;
    void InvalidateRect(const Rect2Df& localRect) override {
        ++redraws;
        UltraCanvasTextInput::InvalidateRect(localRect);
    }
    void InvalidateLayout() override {
        ++invalidations;
        UltraCanvasTextInput::InvalidateLayout();
    }
};

// Lays the parent and the field out, so both hold a valid measure.
void LayOut(UltraCanvasContainer& parent) {
    CSSLayout::LayoutContext ctx;
    CSSLayout::MeasureConstraints c{ { CSSLayout::ConstraintMode::Exact, 400.0f },
                                     { CSSLayout::ConstraintMode::Exact, 100.0f } };
    parent.Measure(c, ctx);
    parent.Arrange(Rect2Df{ 0, 0, 400, 100 }, ctx);
}

} // namespace

int main() {
    auto parent = std::make_shared<UltraCanvasContainer>("parent", 0, 0, 400, 100);
    parent->layout.SetFlexColumn();
    auto input = std::make_shared<ProbeInput>("field", 0, 0, 240, 28);
    parent->AddChild(input);

    LayOut(*parent);
    TEST("laid out: the field holds a valid measure", input->measured.valid);
    TEST("laid out: so does its parent", parent->measured.valid);

    input->redraws = 0;
    input->invalidations = 0;
    input->SetFontSize(22.0f);
    TEST("SetFontSize stores the size", input->GetStyle().fontStyle.fontSize == 22.0f);
    TEST("SetFontSize invalidates the field's layout", input->invalidations > 0 && !input->measured.valid);
    TEST("and its parent's", !parent->measured.valid);
    TEST("SetFontSize asks for a redraw", input->redraws > 0);

    LayOut(*parent);
    input->redraws = 0;
    input->invalidations = 0;
    TextInputStyle style = input->GetStyle();
    style.fontStyle.fontSize = 9.0f;
    input->SetStyle(style);
    TEST("SetStyle stores the style", input->GetStyle().fontStyle.fontSize == 9.0f);
    TEST("SetStyle invalidates the layout", input->invalidations > 0 && !parent->measured.valid);
    TEST("SetStyle asks for a redraw", input->redraws > 0);

    // Without a window nothing can be measured or scrolled; it must not crash.
    auto loose = std::make_shared<UltraCanvasTextInput>("loose", 0, 0, 100, 24);
    loose->SetText("some text to scroll");
    loose->SetFontSize(30.0f);
    TEST("a field outside any window takes a new font", loose->GetStyle().fontStyle.fontSize == 30.0f);

    // The line height a field's caret and selection are sized from.
    if (auto ctx = CreateRenderContext(Size2Di(300, 60), nullptr)) {
        const FontStyle font = TextInputStyle().fontStyle;
        auto line = ctx->CreateTextLayout("H", false);
        line->SetFontStyle(font);
        const double exact = line->GetLayoutHeight();
        const double single = ctx->GetSingleLineHeight(font);
        TEST("GetSingleLineHeight is the line's height, unrounded", std::abs(single - exact) < 1e-9);
        TEST("GetTextLineHeight is the same height in whole pixels, never more",
             ctx->GetTextLineHeight("H") <= single && single - ctx->GetTextLineHeight("H") < 1.0);
        auto descenders = ctx->CreateTextLayout("gjpqy", false);
        descenders->SetFontStyle(font);
        const UCLayoutExtents e = descenders->GetLayoutExtents();
        TEST("descenders end inside the unrounded line height", e.ink.y + e.ink.height <= single + 1e-6);
        TEST("the second ask comes from the cache", ctx->GetSingleLineHeight(font) == single);
    } else {
        TEST("an offscreen context to measure in", false);
    }

    std::cerr << "\nTextInputFontTest: " << testCount << " checks, " << failCount << " failures" << std::endl;
    return failCount == 0 ? 0 : 1;
}
