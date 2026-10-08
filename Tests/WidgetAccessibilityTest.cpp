// Tests/WidgetAccessibilityTest.cpp
// What the common widgets tell assistive technology about themselves: role,
// name, description, checked state, value range, value text and the default
// action - and that a change is announced. The platform bridges (AT-SPI,
// UI Automation, NSAccessibility) only translate these answers, so this is
// what makes a whole window navigable with a screen reader, not just the
// rich text editor.
//
// Runs headless: only the elements' answers are checked, no window or bridge.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasAccessibility.h"
#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"

#include <cmath>
#include <iostream>
#include <memory>
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

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Widget Accessibility Suite"            << std::endl;
    std::cerr << "========================================" << std::endl;

    // Every announcement, for the "is it announced" checks.
    std::vector<std::pair<AccessibilityEventType, UltraCanvasUIElement*>> events;
    const int listener = UltraCanvasAccessibility::AddListener([&](const AccessibilityEvent& e) {
        events.emplace_back(e.type, e.element);
    });
    const auto announced = [&](AccessibilityEventType type, UltraCanvasUIElement* element) {
        for (const auto& [t, e] : events) {
            if (t == type && e == element) return true;
        }
        return false;
    };

    std::cerr << "\n--- Buttons ---" << std::endl;
    {
        auto button = std::make_shared<UltraCanvasButton>("save", 0, 0, 80, 24, "Save");
        int clicks = 0;
        button->onClick = [&]() { clicks++; };
        TEST("A button is a button", button->GetAccessibleRole() == AccessibleRole::Button);
        TEST("...named by its text", button->GetAccessibleName() == "Save");
        TEST("...with its tooltip as description", (button->SetTooltip("Save the file"), button->GetAccessibleDescription() == "Save the file"));
        TEST("...not a toggle", button->GetAccessibleToggleState() == AccessibleToggleState::NotToggleable);
        TEST("...whose action is press", button->GetAccessibleActionName() == "press");
        TEST("...which clicks it", button->DoAccessibleAction() && clicks == 1);
        button->SetAccessibleName("Save document");
        TEST("SetAccessibleName replaces the text as name", button->GetAccessibleName() == "Save document");

        auto icon = std::make_shared<UltraCanvasButton>("bold", 0, 0, 24, 24, "");
        icon->SetTooltip("Bold");
        TEST("An icon button is named by its tooltip", icon->GetAccessibleName() == "Bold");
        TEST("...which is then not repeated as its description", icon->GetAccessibleDescription().empty());

        auto toggle = std::make_shared<UltraCanvasButton>("pin", 0, 0, 24, 24, "Pin");
        toggle->SetCanToggled(true);
        bool toggledTo = false;
        toggle->onToggle = [&](bool on) { toggledTo = on; };
        TEST("A toggle button reports its state", toggle->GetAccessibleToggleState() == AccessibleToggleState::Off);
        events.clear();
        TEST("...its action toggles it", toggle->DoAccessibleAction() && toggledTo &&
                                         toggle->GetAccessibleToggleState() == AccessibleToggleState::On);
        TEST("...and announces the change", announced(AccessibilityEventType::StateChanged, toggle.get()));
        toggle->SetDisabled(true);
        TEST("A disabled button refuses the action", !toggle->DoAccessibleAction());
    }

    std::cerr << "\n--- Checkboxes, radio buttons, switches ---" << std::endl;
    {
        auto box = std::make_shared<UltraCanvasCheckbox>("wrap", 0, 0, 120, 24, "Wrap lines");
        TEST("A checkbox is a checkbox named by its label",
             box->GetAccessibleRole() == AccessibleRole::CheckBox && box->GetAccessibleName() == "Wrap lines");
        TEST("...unchecked", box->GetAccessibleToggleState() == AccessibleToggleState::Off);
        events.clear();
        TEST("...its action checks it", box->DoAccessibleAction() && box->IsChecked() &&
                                        box->GetAccessibleToggleState() == AccessibleToggleState::On);
        TEST("...and the change is announced", announced(AccessibilityEventType::StateChanged, box.get()));
        box->SetCheckState(CheckedState::Indeterminate);
        TEST("An indeterminate checkbox is mixed", box->GetAccessibleToggleState() == AccessibleToggleState::Mixed);
        events.clear();
        box->SetText("Wrap long lines");
        TEST("Renaming the label is announced", announced(AccessibilityEventType::NameChanged, box.get()) &&
                                                box->GetAccessibleName() == "Wrap long lines");

        auto radio = std::make_shared<UltraCanvasRadio>("left", 0, 0, 120, 24, "Left");
        TEST("A radio button is one", radio->GetAccessibleRole() == AccessibleRole::RadioButton &&
                                      radio->GetAccessibleActionName() == "select");
        TEST("...its action selects it", radio->DoAccessibleAction() &&
                                         radio->GetAccessibleToggleState() == AccessibleToggleState::On);
        TEST("...and selecting it again keeps it selected", radio->DoAccessibleAction() && radio->IsChecked());

        auto toggleSwitch = std::make_shared<UltraCanvasSwitch>("wifi", 0, 0, 120, 24, "Wi-Fi");
        TEST("A switch is a switch", toggleSwitch->GetAccessibleRole() == AccessibleRole::Switch &&
                                     toggleSwitch->GetAccessibleName() == "Wi-Fi");
        TEST("...its action flips it", toggleSwitch->DoAccessibleAction() &&
                                       toggleSwitch->GetAccessibleToggleState() == AccessibleToggleState::On &&
                                       toggleSwitch->DoAccessibleAction() &&
                                       toggleSwitch->GetAccessibleToggleState() == AccessibleToggleState::Off);
    }

    std::cerr << "\n--- Sliders and spin buttons ---" << std::endl;
    {
        auto slider = std::make_shared<UltraCanvasSlider>("volume", 0, 0, 200, 24);
        slider->SetRange(0, 100);
        slider->SetStep(5);
        slider->SetValue(40);
        slider->SetAccessibleName("Volume");
        AccessibleRange range;
        TEST("A slider is a slider with its range", slider->GetAccessibleRole() == AccessibleRole::Slider &&
             slider->GetAccessibleRange(range) && range.value == 40 && range.minimum == 0 &&
             range.maximum == 100 && range.step == 5);
        float heard = -1;
        slider->onValueChanged = [&](float v) { heard = v; };
        events.clear();
        TEST("A screen reader can set it, snapped to the step", slider->SetAccessibleValue(62) &&
             std::abs(slider->GetValue() - 60.0f) < 0.01f && std::abs(heard - 60.0f) < 0.01f);
        TEST("...and the change is announced", announced(AccessibilityEventType::ValueChanged, slider.get()));
        slider->SetRangeMode(true);
        TEST("A two-handle range slider reports no single value", !slider->GetAccessibleRange(range));

        auto spinner = std::make_shared<UltraCanvasSpinner>("copies", 0, 0, 100, 24);
        spinner->SetRange(1, 10);
        spinner->SetValue(2);
        TEST("A spinner is a spin button with its range", spinner->GetAccessibleRole() == AccessibleRole::SpinButton &&
             spinner->GetAccessibleRange(range) && range.value == 2 && range.maximum == 10);
        events.clear();
        TEST("...which a screen reader can set", spinner->SetAccessibleValue(7) && spinner->GetValue() == 7 &&
                                                 announced(AccessibilityEventType::ValueChanged, spinner.get()));
        TEST("...with its shown text as value text", spinner->GetAccessibleValueText() == spinner->GetDisplayText());
    }

    std::cerr << "\n--- Labels, fields, combo boxes, groups ---" << std::endl;
    {
        auto label = std::make_shared<UltraCanvasLabel>("status", 0, 0, 200, 20, "Ready");
        TEST("A label is a label named by its text", label->GetAccessibleRole() == AccessibleRole::Label &&
                                                     label->GetAccessibleName() == "Ready");
        events.clear();
        label->SetText("Saving");
        TEST("...a new text is announced as a new name", announced(AccessibilityEventType::NameChanged, label.get()) &&
                                                          label->GetAccessibleName() == "Saving");

        auto field = std::make_shared<UltraCanvasTextInput>("email", 0, 0, 200, 24);
        field->SetPlaceholder("E-mail address");
        field->SetText("a@b.c");
        TEST("A text field is named by its placeholder", field->GetAccessibleName() == "E-mail address");
        TEST("...its value is its text", field->GetAccessibleValueText() == "a@b.c");
        TEST("...which the bridges read as text",
             AccessibilityBridge::TextInterface(field.get()) &&
             AccessibilityBridge::TextInterface(field.get())->GetAccessibleText() == "a@b.c");
        events.clear();
        TEST("A screen reader can replace the text", field->SetAccessibleValueText("x@y.z") && field->GetText() == "x@y.z" &&
                                                     announced(AccessibilityEventType::ValueChanged, field.get()));
        auto secret = std::make_shared<UltraCanvasTextInput>("pin", 0, 0, 200, 24);
        secret->SetInputType(TextInputType::Password);
        secret->SetText("1234");
        TEST("A password field gives away no value", secret->GetAccessibleValueText().empty() &&
                                                     AccessibilityBridge::TextInterface(secret.get()) == nullptr);

        auto combo = std::make_shared<UltraCanvasDropdown>("size", 0, 0, 120, 24);
        combo->AddItem("Small");
        combo->AddItem("Large");
        combo->SetSelectedIndex(0);
        TEST("A dropdown is a combo box showing its item", combo->GetAccessibleRole() == AccessibleRole::ComboBox &&
                                                          combo->GetAccessibleValueText() == "Small");
        events.clear();
        combo->SetSelectedIndex(1);
        TEST("...a new selection is announced", announced(AccessibilityEventType::ValueChanged, combo.get()) &&
                                                combo->GetAccessibleValueText() == "Large");

        auto group = std::make_shared<UltraCanvasGroupBox>("options", 0, 0, 200, 100, "Options");
        TEST("A group box is a group named by its title", group->GetAccessibleRole() == AccessibleRole::Group &&
                                                         group->GetAccessibleName() == "Options");
    }

    UltraCanvasAccessibility::RemoveListener(listener);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
