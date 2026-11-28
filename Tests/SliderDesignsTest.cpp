// Tests/SliderDesignsTest.cpp
// The parts of the slider's designs that are not drawing: where the ticks
// fall and what they are labelled, named stops, the fill origin's reset, and
// that a knob or a ruler moves by dragging from where it is instead of
// jumping to where the press lands.
//
// Also how the slider sizes itself: a minimum across the bar for what it
// shows, a size given too small for that growing to it in a layout, and a
// plain slider or an application's own minimum left as they were.
//
// Runs headless: the slider is driven with events and laid out in a
// container, no window.
// Version: 1.1.0 - sizing: GetMinimumSize, grow to fit, intrinsic size
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasSlider.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        testCount++;                                                          \
        if (!passed) failCount++;                                             \
    } while (0)

static bool Near(float a, float b) { return std::abs(a - b) < 1e-3f; }

static UCEvent Mouse(UCEventType type, int x, int y) {
    UCEvent e;
    e.type = type;
    e.pointer = Point2Di(x, y);
    e.button = (type == UCEventType::MouseMove) ? UCMouseButton::NoneButton : UCMouseButton::Left;
    return e;
}

static void TestTickValues() {
    auto slider = CreateSlider("Ticks", 0, 0, 300, 40);
    slider->SetRange(0.0f, 100.0f);
    slider->SetStep(10.0f);

    // No interval set: the step, when that gives at most 20 ticks
    auto ticks = slider->GetTickValues();
    TEST("derived interval: one tick per step, both ends included", ticks.size() == 11 &&
         Near(ticks.front().first, 0.0f) && Near(ticks.back().first, 100.0f));

    // Major and minor
    slider->SetTickInterval(25.0f, 5.0f);
    ticks = slider->GetTickValues();
    int majors = 0;
    for (const auto& t : ticks) if (t.second) majors++;
    TEST("25 / 5 gives 21 ticks, 5 of them major", ticks.size() == 21 && majors == 5);
    TEST("the major ones fall on multiples of 25", ticks[5].second && Near(ticks[5].first, 25.0f) && !ticks[4].second);

    // Chosen values win over the interval, and stay inside the range
    slider->SetTickMarks({-10.0f, 0.0f, 50.0f, 100.0f, 120.0f});
    ticks = slider->GetTickValues();
    TEST("tick marks: only the values inside the range", ticks.size() == 3 && Near(ticks[1].first, 50.0f));

    // A wide continuous range: a tenth of it
    auto wide = CreateSlider("Wide", 0, 0, 300, 40);
    wide->SetRange(0.0f, 1000.0f);
    wide->SetStep(0.0f);
    TEST("continuous slider: a tick every tenth of the range", wide->GetTickValues().size() == 11);

    // A ruler's scale reads in round numbers, not from its minimum
    auto ruler = CreateSlider("Ruler", 0, 0, 300, 60);
    ruler->SetSliderStyle(SliderStyle::Ruler);
    ruler->SetRange(-45.0f, 45.0f);
    ruler->SetTickInterval(10.0f, 2.0f);
    ticks = ruler->GetTickValues();
    bool zeroIsMajor = false, fiveIsMajor = false;
    for (const auto& t : ticks) {
        if (Near(t.first, 0.0f)) zeroIsMajor = t.second;
        if (Near(t.first, 5.0f)) fiveIsMajor = true;
    }
    TEST("ruler: ticks on multiples of the interval, 0 major", zeroIsMajor && !fiveIsMajor &&
         Near(ticks.front().first, -44.0f) && Near(ticks.back().first, 44.0f));
}

static void TestLabels() {
    auto slider = CreateSlider("Labels", 0, 0, 300, 50);
    slider->SetRange(0.0f, 1.0f);
    TEST("a fractional label loses its trailing zeros", slider->GetTickLabel(0.5f).find("00") == std::string::npos);
    TEST("a whole number has no decimals", slider->GetTickLabel(1.0f) == "1");
    slider->tickLabelFormatter = [](float v) { return std::to_string(static_cast<int>(v * 100)) + " %"; };
    TEST("the formatter makes the label", slider->GetTickLabel(0.25f) == "25 %");

    auto sizes = CreateSlider("Sizes", 0, 0, 300, 50);
    sizes->SetRange(0.0f, 6.0f);
    sizes->SetStep(1.0f);
    sizes->SetStopLabels({"Off", "XS", "S", "M", "L", "XL", "XXL"});
    sizes->SetTickPosition(SliderTickPosition::OnTrack);
    TEST("stop labels: one major tick per stop", sizes->GetTickValues().size() == 7);
    TEST("stop labels: a stop is named", sizes->GetTickLabel(3.0f) == "M" && sizes->GetTickLabel(6.0f) == "XXL");
}

static void TestResetAndOrigin() {
    auto balance = CreateSlider("Balance", 0, 0, 300, 40);
    balance->SetRange(-100.0f, 100.0f);
    balance->SetValue(-40.0f);
    balance->SetFillOrigin(0.0f);
    balance->SetResetValue(0.0f);
    TEST("fill origin is kept", balance->HasFillOrigin() && Near(balance->GetFillOrigin(), 0.0f));
    balance->OnEvent(Mouse(UCEventType::MouseDoubleClick, 50, 20));
    TEST("a double-click returns to the reset value", Near(balance->GetValue(), 0.0f));

    auto plain = CreateSlider("Plain", 0, 0, 300, 40);
    plain->SetValue(30.0f);
    plain->OnEvent(Mouse(UCEventType::MouseDoubleClick, 50, 20));
    TEST("without a reset value a double-click changes nothing", Near(plain->GetValue(), 30.0f));
}

static void TestRelativeDrag() {
    // A press on a linear slider sets the value where it lands
    auto linear = CreateSlider("Linear", 0, 0, 216, 40);
    linear->SetRange(0.0f, 100.0f);
    linear->SetValue(10.0f);
    linear->OnEvent(Mouse(UCEventType::MouseDown, 208, 20));
    TEST("linear: a press at the end jumps to the maximum", Near(linear->GetValue(), 100.0f));
    linear->OnEvent(Mouse(UCEventType::MouseUp, 208, 20));

    // A knob does not jump: it turns as the pointer moves up
    auto knob = CreateSlider("Knob", 0, 0, 120, 120);
    knob->SetSliderStyle(SliderStyle::Knob);
    knob->SetRange(0.0f, 100.0f);
    knob->SetValue(50.0f);
    knob->OnEvent(Mouse(UCEventType::MouseDown, 110, 110));
    TEST("knob: the press leaves the value alone", Near(knob->GetValue(), 50.0f));
    knob->OnEvent(Mouse(UCEventType::MouseMove, 110, 70));    // 40 px up: a fifth of the range
    TEST("knob: dragging up 40 px turns it up a fifth", Near(knob->GetValue(), 70.0f));
    knob->OnEvent(Mouse(UCEventType::MouseUp, 110, 70));

    // A ruler's scale follows the pointer, so the value moves the other way
    auto ruler = CreateSlider("Ruler", 0, 0, 300, 60);
    ruler->SetSliderStyle(SliderStyle::Ruler);
    ruler->SetRange(-45.0f, 45.0f);
    ruler->SetStep(0.5f);
    ruler->SetPixelsPerUnit(6.0f);
    ruler->SetValue(0.0f);
    ruler->OnEvent(Mouse(UCEventType::MouseDown, 150, 30));
    ruler->OnEvent(Mouse(UCEventType::MouseMove, 120, 30));   // scale dragged 30 px left
    TEST("ruler: dragging the scale left by 30 px adds 5", Near(ruler->GetValue(), 5.0f));
    ruler->OnEvent(Mouse(UCEventType::MouseUp, 120, 30));
}

// Lays a column out at w x h, the slider in it, and returns the slider's
// bounds after the layout.
static Rect2Df LayOut(const std::shared_ptr<UltraCanvasSlider>& slider, float w, float h) {
    using namespace CSSLayout;
    auto column = std::make_shared<UltraCanvasContainer>("Column", 0, 0, w, h);
    column->layout.SetFlexColumn().SetFlexAlignItems(AlignItems::Start);
    column->AddChild(slider);
    LayoutContext ctx;
    MeasureConstraints mc{ { ConstraintMode::Exact, w }, { ConstraintMode::Exact, h } };
    column->Measure(mc, ctx);
    column->Arrange(Rect2Df{ 0, 0, w, h }, ctx);
    // The slider keeps a pointer to its parent: the column stays alive
    static std::vector<std::shared_ptr<UltraCanvasContainer>> columns;
    columns.push_back(column);
    return slider->GetBounds();
}

static void TestSizing() {
    // A plain slider: its band is its minimum, and a given size is kept
    auto plain = CreateSlider("PlainSize", 0, 0, 300, 20);
    TEST("plain slider: minimum across is the handle", Near(plain->GetMinimumSize().height, 16.0f));
    Rect2Df plainBox = LayOut(plain, 400, 200);
    TEST("plain slider: the given 300 x 20 stays", Near(plainBox.width, 300.0f) && Near(plainBox.height, 20.0f));
    TEST("plain slider: no constraint of its own", !plain->boxConstraints.has_value());

    // Ticks and labels need more than the 20 px given: it grows to them
    auto labelled = CreateSlider("LabelledSize", 0, 0, 300, 20);
    labelled->SetTickPosition(SliderTickPosition::After);
    labelled->SetTickInterval(25.0f);
    labelled->SetTickLabels(true);
    float needed = labelled->GetMinimumSize().height;
    TEST("labelled slider: needs more than its band", needed > 16.0f + 6.0f);
    Rect2Df labelledBox = LayOut(labelled, 400, 200);
    TEST("labelled slider: grows from 20 to what its labels need",
         labelledBox.height >= needed - 0.5f && Near(labelledBox.width, 300.0f));

    // Switched off, the given size stays
    auto fixed = CreateSlider("FixedSize", 0, 0, 300, 20);
    fixed->SetTickPosition(SliderTickPosition::After);
    fixed->SetTickLabels(true);
    fixed->SetGrowToFit(false);
    TEST("grow to fit off: the given height stays", Near(LayOut(fixed, 400, 200).height, 20.0f));

    // An application's own min-height is left alone
    auto own = CreateSlider("OwnMinimum", 0, 0, 300, 20);
    own->SetTickPosition(SliderTickPosition::After);
    own->SetTickLabels(true);
    CSSLayout::BoxConstraints limits;
    limits.minHeight = CSSLayout::Dimension::Px(70.0f);
    own->boxConstraints = limits;
    LayOut(own, 400, 200);
    TEST("an application's min-height is kept", Near(own->boxConstraints->minHeight.value, 70.0f));

    // Labels off again: the slider drops the minimum it set
    labelled->SetTickPosition(SliderTickPosition::NoTicks);
    labelled->SetTickLabels(false);
    LayOut(labelled, 400, 200);
    TEST("decorations gone: its own minimum is released",
         labelled->boxConstraints->minHeight.unit == CSSLayout::DimensionUnit::Auto);

    // A vertical slider grows across too: in width
    auto vertical = CreateSlider("VerticalSize", 0, 0, 20, 200);
    vertical->SetOrientation(SliderOrientation::Vertical);
    vertical->SetTickPosition(SliderTickPosition::Before);
    vertical->SetTickLabels(true);
    float neededWide = vertical->GetMinimumSize().width;
    Rect2Df verticalBox = LayOut(vertical, 400, 300);
    TEST("vertical slider: grows in width, keeps its height",
         verticalBox.width >= neededWide - 0.5f && neededWide > 20.0f && Near(verticalBox.height, 200.0f));

    // No size given: the layout sizes it from its own measure
    auto sized = std::make_shared<UltraCanvasSlider>("Unsized");
    sized->SetTickPosition(SliderTickPosition::After);
    sized->SetTickLabels(true);
    Rect2Df sizedBox = LayOut(sized, 400, 200);
    TEST("no size given: preferred length, the height its labels need",
         Near(sizedBox.width, sized->GetPreferredLength()) && sizedBox.height >= sized->GetMinimumSize().height - 0.5f);
}

int main() {
    TestTickValues();
    TestLabels();
    TestResetAndOrigin();
    TestRelativeDrag();
    TestSizing();
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
