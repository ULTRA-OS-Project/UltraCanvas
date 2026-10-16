// Tests/ColorPickerAutoHeightTest.cpp
// The colour picker's channel section folds away, and the widget can follow
// its content's height.
//
// A collapsible picker hides its colour definitions behind a disclosure row.
// The HSV / HSL / RGB tab bar used to stay out when the sliders under it
// folded away; it belongs to the section now and folds with them, and the
// disclosure row comes first.
//
// The widget used to keep the size it was given: closing the section handed
// the space to the colour area, and the colour + brightness sliders - whose
// bars have a height of their own - left it empty at the bottom. With
// SetAutoHeight(true) the widget takes the height its content needs instead,
// in the layout too, so in a flex column (a toolbox panel) what is below it
// moves up when the section folds and down when it opens or a larger design
// is chosen. Without it the widget keeps its size, as before.
//
// Every press goes through the application's own dispatch. Opens a real
// window, so it runs under Xvfb (xvfb-run -a) and skips itself without a
// DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
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
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

constexpr float PickerW = 290.0f;

void Press(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           const UltraCanvasUIElement& in, float localX, float localY) {
    const Rect2Df b = in.GetBoundsInWindow();
    for (UCEventType type : {UCEventType::MouseDown, UCEventType::MouseUp}) {
        UCEvent e;
        e.type = type;
        e.button = UCMouseButton::Left;
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        e.pointerWindow = Point2Di(static_cast<int>(std::lround(b.x + localX)),
                                   static_cast<int>(std::lround(b.y + localY)));
        e.pointer = e.pointerWindow;
        app.DispatchEvent(e);
    }
}

// A few frames: a height change is laid out on the frame after it is made.
void Frames(const std::shared_ptr<UltraCanvasWindow>& window,
            const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
    for (int frame = 0; frame < 4; ++frame) DisplayTest::Frame(window, elements);
}

bool Near(float a, float b) { return std::fabs(a - b) <= 0.5f; }

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Colour Picker Auto Height Suite"       << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ColorPickerAutoHeightTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ColorPickerAutoHeightTest";
    cfg.width = 680;
    cfg.height = 720;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();
    DisplayTest::ActivateWindow(app, window);

    const ColorPickerStyle st;
    const float gap = st.rowGap, rh = st.rowHeight;
    const float sectionH = st.tabHeight + gap + 4 * (rh + gap);   // tabs + H S V A

    // ===== The tabs fold with the sliders (a picker of fixed size) =====
    auto fixedPicker = CreateColorPicker("fixed", Color(0xFF, 0x80, 0x00, 0xFF), 20, 20, PickerW, 470);
    fixedPicker->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
    window->AddChild(fixedPicker);
    const float openH = fixedPicker->PreferredHeightForWidth(PickerW);
    fixedPicker->SetSlidersCollapsible(true, false);
    const float foldedH = fixedPicker->PreferredHeightForWidth(PickerW);
    TEST("folding takes the tab bar away with the sliders",
         Near(openH + (rh + gap) - foldedH, sectionH));
    DisplayTest::Frame(window, {fixedPicker});

    // The two bars, then the swatches / hex block, then the disclosure row;
    // the tabs follow it when the section is open.
    const float pad = st.padding;
    const float headerY = pad + 2 * st.hueBarHeight + gap + pad + (2 * rh + gap) + gap;
    const float tabsY = headerY + rh + gap;
    Press(app, window, *fixedPicker, PickerW * 0.5f, tabsY + st.tabHeight * 0.5f);
    TEST("where the HSL tab is when open, a press does nothing while folded",
         fixedPicker->GetModel() == ColorPickerModel::HSV);
    Press(app, window, *fixedPicker, 40, headerY + rh * 0.5f);
    TEST("a press on the disclosure row opens the section", fixedPicker->GetSlidersExpanded());
    DisplayTest::Frame(window, {fixedPicker});
    Press(app, window, *fixedPicker, PickerW * 0.5f, tabsY + st.tabHeight * 0.5f);
    TEST("open, the tab bar sits under the disclosure row",
         fixedPicker->GetModel() == ColorPickerModel::HSL);
    TEST("without auto height the widget keeps the size it was given",
         Near(fixedPicker->GetHeight(), 470));
    fixedPicker->SetSlidersExpanded(false);
    TEST("... folded too", Near(fixedPicker->GetHeight(), 470));
    window->RemoveChild(fixedPicker);

    // ===== Auto height in a toolbox column =====
    auto toolbox = std::make_shared<UltraCanvasContainer>("toolbox", 0, 0, 320, 700);
    toolbox->layout.SetFlexColumn().SetFlexGap(6);
    window->AddChild(toolbox);
    auto picker = CreateColorPicker("picker", Color(0xFF, 0x80, 0x00, 0xFF), 0, 0, PickerW, 470);
    picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    picker->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
    picker->SetSlidersCollapsible(true, false);
    toolbox->AddChild(picker);
    auto below = std::make_shared<UltraCanvasLabel>("below", 0, 0, 290, 20);
    below->SetText("Palette");
    below->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toolbox->AddChild(below);
    const std::vector<std::shared_ptr<UltraCanvasUIElement>> all = {toolbox, picker, below};
    Frames(window, all);

    picker->SetAutoHeight(true);
    Frames(window, all);
    const float folded = picker->GetHeight();
    TEST("auto height takes the height the content needs",
         Near(folded, picker->PreferredHeightForWidth(PickerW)) && folded < 200);
    TEST("in the layout too (size.height)",
         picker->size.height.unit == CSSLayout::DimensionUnit::Pixels &&
         Near(picker->size.height.value, folded));
    TEST("what is below sits under it", Near(below->GetY(), picker->GetY() + folded + 6));

    picker->SetSlidersExpanded(true);
    TEST("opening the section grows the widget at once", Near(picker->GetHeight(), folded + sectionH));
    Frames(window, all);
    TEST("and what is below moves down with it",
         Near(below->GetY(), picker->GetY() + folded + sectionH + 6));

    picker->SetWheelStyle(ColorPickerWheelStyle::Ring);
    Frames(window, all);
    TEST("choosing the circle grows it to the full circle",
         Near(picker->GetHeight(), picker->PreferredHeightForWidth(PickerW)) &&
         picker->GetHeight() > folded + sectionH + 200);
    TEST("and what is below follows", Near(below->GetY(), picker->GetY() + picker->GetHeight() + 6));

    picker->SetSlidersExpanded(false);
    Frames(window, all);
    TEST("folding shrinks it by the section",
         Near(picker->GetHeight(), picker->PreferredHeightForWidth(PickerW)));
    TEST("and what is below moves up", Near(below->GetY(), picker->GetY() + picker->GetHeight() + 6));

    // A PixelExact area: PreferredHeightForWidth keeps room for the folded
    // sliders, so opening them cannot shrink the area; auto height grows the
    // widget for them instead and keeps no room.
    picker->SetWheelStyle(ColorPickerWheelStyle::Bar);
    picker->SetSVAreaShape(ColorPickerSVAreaShape::PixelExact);
    Frames(window, all);
    const float exactFolded = picker->GetHeight();
    TEST("a folded PixelExact picker keeps no room for the sliders",
         Near(exactFolded + 4 * (rh + gap), picker->PreferredHeightForWidth(PickerW)));
    picker->SetSlidersExpanded(true);
    TEST("and grows for them when they open",
         Near(picker->GetHeight(), picker->PreferredHeightForWidth(PickerW)));

    picker->SetAutoHeight(false);
    const float kept = picker->GetHeight();
    picker->SetSlidersExpanded(false);
    Frames(window, all);
    TEST("auto height off: the widget keeps its size again", Near(picker->GetHeight(), kept));

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
