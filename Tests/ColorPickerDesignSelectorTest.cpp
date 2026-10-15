// Tests/ColorPickerDesignSelectorTest.cpp
// The colour picker's optional design selector.
//
// SetShowWheelStyleSelector(true) puts a dropdown in the picker's top-right
// corner offering the three designs - colour circle (Ring), colour slider +
// square (Bar) and colour rectangle (HueLightnessField) - each with its icon.
// On the rectangular designs it covers the corner of the colour area, so it
// hides while a drag on the area or a colour bar runs and comes back on
// release; beside the circle it stays. The button shows only the icon
// (DropdownStyle::buttonShowsText = false).
//
// Every press goes through the application's own dispatch, at window
// coordinates. Opens a real window, so it runs under Xvfb (xvfb-run -a) and
// skips itself without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImage.h"
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

constexpr float PickerX = 20.0f, PickerY = 20.0f, PickerW = 290.0f, PickerH = 470.0f;

void Send(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
          UCEventType type, float localX, float localY,
          UCMouseButton button = UCMouseButton::Left) {
    UCEvent e;
    e.type = type;
    e.button = (type == UCEventType::MouseMove) ? UCMouseButton::NoneButton : button;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    const Point2Di p(static_cast<int>(std::lround(PickerX + localX)),
                     static_cast<int>(std::lround(PickerY + localY)));
    e.pointerWindow = p;
    e.pointer = p;
    app.DispatchEvent(e);
}

std::shared_ptr<UltraCanvasDropdown> SelectorOf(const UltraCanvasColorPicker& picker) {
    for (const auto& child : picker.GetChildren()) {
        if (auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(child)) return dropdown;
    }
    return nullptr;
}

bool Visible(const std::shared_ptr<UltraCanvasDropdown>& selector) {
    return selector && selector->IsVisible();
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Colour Picker Design Selector Suite"   << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ColorPickerDesignSelectorTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ColorPickerDesignSelectorTest";
    cfg.width = 340;
    cfg.height = 520;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();
    DisplayTest::ActivateWindow(app, window);

    auto picker = CreateColorPicker("picker", Color(0x85, 0xFF, 0xFB, 0xFF), PickerX, PickerY,
                                    PickerW, PickerH);
    window->AddChild(picker);
    DisplayTest::Frame(window, {picker});

    // ----- Off by default -----
    TEST("the selector is off by default", !picker->GetShowWheelStyleSelector() && !SelectorOf(*picker));

    std::vector<ColorPickerWheelStyle> chosen;
    picker->onWheelStyleChanged = [&chosen](ColorPickerWheelStyle s) { chosen.push_back(s); };

    picker->SetShowWheelStyleSelector(true);
    DisplayTest::Frame(window, {picker});
    auto selector = SelectorOf(*picker);
    TEST("showing it adds a dropdown child", selector != nullptr && Visible(selector));
    if (!selector) return 1;

    // ----- Three designs, each with an icon that loads -----
    TEST("it offers three designs", selector->GetItemCount() == 3);
    bool iconsLoad = selector->GetItemCount() == 3;
    for (const auto& item : selector->GetItems()) {
        auto image = UCImage::Get(item.iconPath);
        if (item.iconPath.empty() || !image || !image->IsValid()) {
            std::cerr << "  icon does not load: '" << item.iconPath << "'" << std::endl;
            iconsLoad = false;
        }
    }
    TEST("every design's icon loads", iconsLoad);
    TEST("the button shows the icon alone", !selector->GetStyle().buttonShowsText);

    // ----- Top-right corner -----
    const Rect2Df b = selector->GetBounds();
    const float pad = picker->GetStyle().padding;
    TEST("it sits in the top-right corner, inside the padding",
         std::fabs(b.x + b.width - (PickerW - pad)) <= 1.0f && std::fabs(b.y - pad) <= 1.0f &&
         b.width > 0 && b.height > 0);

    // ----- The selection follows the design -----
    TEST("the colour circle is selected for the Ring style", selector->GetSelectedIndex() == 0);
    picker->SetWheelStyle(ColorPickerWheelStyle::Bar);
    TEST("SetWheelStyle moves the selection", selector->GetSelectedIndex() == 1);
    picker->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
    TEST("the two-sliders style selects none of the three", selector->GetSelectedIndex() == -1);
    TEST("SetWheelStyle does not report a choice", chosen.empty());

    // ----- Choosing a design -----
    selector->SetSelectedIndex(2);       // as a click in the list does
    TEST("choosing the colour rectangle switches the design",
         picker->GetWheelStyle() == ColorPickerWheelStyle::HueLightnessField);
    TEST("the choice is reported once",
         chosen.size() == 1 && chosen[0] == ColorPickerWheelStyle::HueLightnessField);
    selector->SetSelectedIndex(1);
    TEST("choosing the colour slider + square switches the design",
         picker->GetWheelStyle() == ColorPickerWheelStyle::Bar && chosen.size() == 2);
    DisplayTest::Frame(window, {picker});

    // ----- Rectangular design: a drag on the area hides it -----
    // Bar style: the SV area is the full inner width from the top padding.
    Send(app, window, UCEventType::MouseDown, 100, 120);
    TEST("a press on the area of a rectangular design picks there", app.GetCapturedElement() == picker.get());
    TEST("a drag on a rectangular design's area hides the selector", !Visible(selector));
    Send(app, window, UCEventType::MouseMove, 140, 90);
    TEST("it stays hidden while the drag runs", !Visible(selector));
    Send(app, window, UCEventType::MouseUp, 140, 90);
    TEST("the release brings it back", Visible(selector));

    // A channel slider is below the area: dragging it leaves the selector.
    // The alpha row is the last one: H - 2 gaps - row height, from x = 34.
    const uint8_t alphaBefore = picker->GetAlpha();
    Send(app, window, UCEventType::MouseDown, 80, PickerH - 12.0f - 11.0f);
    TEST("a press on the alpha slider changes the alpha", picker->GetAlpha() != alphaBefore);
    TEST("a channel slider drag leaves the selector shown", Visible(selector));
    Send(app, window, UCEventType::MouseUp, 80, PickerH - 12.0f - 11.0f);
    picker->SetAlpha(255, false);

    // ----- A press on the selector is the selector's -----
    const Color before = picker->GetColor();
    const float sx = b.x + b.width * 0.5f, sy = b.y + b.height * 0.5f;
    Send(app, window, UCEventType::MouseDown, sx, sy);
    Send(app, window, UCEventType::MouseUp, sx, sy);
    TEST("a press on the selector opens its list", selector->IsDropdownOpen());
    TEST("and does not pick the colour under it", picker->GetColor() == before);
    selector->CloseDropdown();
    const Color bgBefore = picker->GetBackgroundColor();
    Send(app, window, UCEventType::MouseDown, sx, sy, UCMouseButton::Right);
    Send(app, window, UCEventType::MouseUp, sx, sy, UCMouseButton::Right);
    TEST("a right press on it does not edit the background under it",
         picker->GetBackgroundColor() == bgBefore && picker->GetColor() == before);
    if (selector->IsDropdownOpen()) selector->CloseDropdown();

    // ----- The circle: the corner is free, the selector stays -----
    picker->SetWheelStyle(ColorPickerWheelStyle::Ring);
    DisplayTest::Frame(window, {picker});
    Send(app, window, UCEventType::MouseDown, PickerW * 0.5f, 140);   // in the SV square
    TEST("a drag on the circle's square starts", app.GetCapturedElement() == picker.get());
    TEST("beside the circle the selector stays during a drag", Visible(selector));
    Send(app, window, UCEventType::MouseUp, PickerW * 0.5f, 140);

    // ----- Hidden with the wheel, and when switched off -----
    picker->SetShowColorWheel(false);
    TEST("the compact (no wheel) picker hides it", !Visible(selector));
    picker->SetShowColorWheel(true);
    TEST("showing the wheel again shows it", Visible(selector));
    picker->SetShowWheelStyleSelector(false);
    TEST("switching it off hides it", !Visible(selector));

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
