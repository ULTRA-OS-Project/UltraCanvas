// Tests/ColorPickerModelDropdownTest.cpp
// The colour picker's HSV/HSL/RGB selector in Dropdown mode.
//
// It used to be painted by hand: a button and a three-row popup the picker
// drew inside its own bounds and hit-tested itself. It is a real
// UltraCanvasDropdown child now, placed where the button was, with the
// list's tooltips (DropdownItem::tooltip, the list's ToolTipRole) explaining
// each model as the painted popup's did.
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
#include "UltraCanvasDropdown.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <variant>

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
          UCEventType type, float localX, float localY) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    const Point2Di p(static_cast<int>(std::lround(PickerX + localX)),
                     static_cast<int>(std::lround(PickerY + localY)));
    e.pointerWindow = p;
    e.pointer = p;
    app.DispatchEvent(e);
}

std::shared_ptr<UltraCanvasDropdown> ModelDropdownOf(const UltraCanvasColorPicker& picker) {
    for (const auto& child : picker.GetChildren()) {
        auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(child);
        if (dropdown && dropdown->GetIdentifier() == picker.GetIdentifier() + "-model") return dropdown;
    }
    return nullptr;
}

bool Visible(const std::shared_ptr<UltraCanvasDropdown>& dropdown) {
    return dropdown && dropdown->IsVisible();
}

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Colour Picker Model Dropdown Suite"    << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ColorPickerModelDropdownTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ColorPickerModelDropdownTest";
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

    // ----- The tab bar has no dropdown -----
    TEST("the tab bar (default) has no model dropdown", !ModelDropdownOf(*picker));

    picker->SetModeSelector(ColorPickerModeSelector::Dropdown);
    DisplayTest::Frame(window, {picker});
    auto dropdown = ModelDropdownOf(*picker);
    TEST("Dropdown mode shows a real dropdown", Visible(dropdown));
    if (!dropdown) return 1;

    // ----- Its entries, and what they say on hover -----
    TEST("it offers HSV, HSL and RGB",
         dropdown->GetItemCount() == 3 && dropdown->GetItem(0)->text == "HSV" &&
         dropdown->GetItem(1)->text == "HSL" && dropdown->GetItem(2)->text == "RGB");
    TEST("each entry explains its model",
         Contains(dropdown->GetItem(0)->tooltip, "Hue, Saturation, Value") &&
         Contains(dropdown->GetItem(1)->tooltip, "Lightness") &&
         Contains(dropdown->GetItem(2)->tooltip, "Red, Green, Blue"));
    DropdownListModel listModel;
    listModel.SetItems(&dropdown->GetItems());
    const ListDataValue tip = listModel.GetData(ListIndex{2, 0}, ListDataRole::ToolTipRole);
    TEST("the open list shows an entry's tooltip (ToolTipRole)",
         std::holds_alternative<std::string>(tip) &&
         std::get<std::string>(tip) == dropdown->GetItem(2)->tooltip);
    TEST("the button explains the current model", Contains(dropdown->GetTooltip(), "HSV"));
    TEST("the current model is selected", dropdown->GetSelectedIndex() == 0);

    // ----- Where the painted button was: right edge of the preview row -----
    const Rect2Df b = dropdown->GetBounds();
    const float pad = picker->GetStyle().padding;
    TEST("it sits at the right edge, a row high",
         std::fabs(b.x + b.width - (PickerW - pad)) <= 1.0f &&
         std::fabs(b.height - picker->GetStyle().rowHeight) <= 1.0f &&
         b.y > pad && b.y + b.height < PickerH);

    // ----- Choosing a model -----
    const Color before = picker->GetColor();
    const float bx = b.x + b.width * 0.5f, by = b.y + b.height * 0.5f;
    Send(app, window, UCEventType::MouseDown, bx, by);
    Send(app, window, UCEventType::MouseUp, bx, by);
    TEST("a press on it opens its list", dropdown->IsDropdownOpen());
    TEST("and changes nothing else", picker->GetColor() == before &&
                                     picker->GetModel() == ColorPickerModel::HSV);
    dropdown->SetSelectedIndex(2);       // as a click in the list does
    dropdown->CloseDropdown();
    TEST("choosing RGB switches the channel sliders to RGB",
         picker->GetModel() == ColorPickerModel::RGB);
    TEST("the button's tooltip follows", Contains(dropdown->GetTooltip(), "Red, Green, Blue"));
    TEST("the colour is left as it was", picker->GetColor() == before);

    picker->SetModel(ColorPickerModel::HSL);
    TEST("SetModel moves the selection", dropdown->GetSelectedIndex() == 1);

    // ----- Back to the tab bar and again -----
    picker->SetModeSelector(ColorPickerModeSelector::TabBar);
    TEST("the tab bar hides the dropdown", !Visible(dropdown));
    picker->SetModeSelector(ColorPickerModeSelector::Dropdown);
    TEST("Dropdown mode shows it again, on the current model",
         Visible(dropdown) && dropdown->GetSelectedIndex() == 1);

    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
