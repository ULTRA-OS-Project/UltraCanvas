// Apps/DemoApp/UltraCanvasSpinnerExamples.cpp
// Demonstration of the UltraCanvasSpinner control: integer / decimal / list
// value modes, vertical up-down and horizontal stepper layouts, wrapping,
// prefixes / suffixes and custom formatting.
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  spinner, each beside its read-out, every text sized to fit
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSpinnerExamples() {
        auto container = DemoKit::CreatePage("SpinnerExamples", "Spinner / SpinBox Examples",
                "Select a value with the up/down (or left/right) arrow buttons, the "
                "keyboard arrow keys, the mouse wheel, or by typing into the field.");

        // All spinners share one width so they line up; each value label is
        // as tall as the spinner beside it and vertically centred.
        const int spinW = 160;
        const int spinH = 28;

        auto cards = DemoKit::AddRow(container, "SpinnerCards");
        // One card per spinner: its title, then the spinner beside its read-out.
        auto addSection = [&cards](const std::string& id, const std::string& title) {
            auto card = DemoKit::AddCard(cards, id, title);
            return DemoKit::AddRow(card, id + "Row", 12);
        };

        // Helper: a read-out label, vertically centred against the spinner on
        // its row (VerticalAlignment::Middle) rather than top-aligned.
        auto makeValueLabel = [](const std::shared_ptr<UltraCanvasContainer>& row,
                                 const std::string& id, const std::string& text) {
            auto lbl = DemoKit::CreateText(id, text);
            lbl->SetBackgroundColor(Color(240, 240, 240));
            lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            lbl->SetPadding(3);
            DemoKit::Add(row, lbl);
            return lbl;
        };

        // ===== 1. INTEGER SPINNER (vertical up/down buttons) =====
        auto intRow = addSection("IntSpinLabel", "Integer (0–100, step 5):");

        auto intSpin = CreateIntSpinner("IntSpinner", 0, 0, spinW, spinH, 0, 100, 20, 5);
        DemoKit::AddFixed(intRow, intSpin);

        auto intValue = makeValueLabel(intRow, "IntSpinValue", "Value: 20");
        intSpin->onValueChanged = [intValue](double v) {
            intValue->SetText("Value: " + std::to_string(static_cast<int>(v)));
        };

        // ===== 2. DECIMAL SPINNER with suffix =====
        auto decRow = addSection("DecSpinLabel", "Decimal (0.0–5.0, step 0.25, 2 dp):");

        auto decSpin = CreateDecimalSpinner("DecSpinner", 0, 0, spinW, spinH, 0.0, 5.0, 1.5, 0.25, 2);
        decSpin->SetSuffix(" s");
        DemoKit::AddFixed(decRow, decSpin);

        auto decValue = makeValueLabel(decRow, "DecSpinValue", "Duration: 1.50 s");
        decSpin->onValueChanged = [decValue](double v) {
            std::ostringstream oss;
            oss << "Duration: " << std::fixed << std::setprecision(2) << v << " s";
            decValue->SetText(oss.str());
        };

        // ===== 3. LIST SPINNER (cycle arbitrary values) =====
        auto listRow = addSection("ListSpinLabel", "List (cycle string values, wraps):");

        auto listSpin = CreateListSpinner("ListSpinner", 0, 0, spinW, spinH,
                                          {"Small", "Medium", "Large", "X-Large"});
        listSpin->SetWrap(true);
        listSpin->SetTextAlignment(TextAlignment::Center);
        // Combobox-style: clicking the field opens a dropdown of every size.
        listSpin->SetDropdownEnabled(true);
        DemoKit::AddFixed(listRow, listSpin);

        auto listValue = makeValueLabel(listRow, "ListSpinValue", "Size: Small");
        listSpin->onSelectionChanged = [listValue](int, const std::string& text) {
            listValue->SetText("Size: " + text);
        };

        // ===== 4. HORIZONTAL STEPPER  [−] value [+] =====
        auto stepRow = addSection("StepSpinLabel", "Horizontal stepper (quantity 1–99):");

        auto stepper = CreateStepper("QtyStepper", 0, 0, spinW, spinH, 1, 99, 1, 1);
        DemoKit::AddFixed(stepRow, stepper);

        auto stepValue = makeValueLabel(stepRow, "StepSpinValue", "Quantity: 1");
        stepper->onValueChanged = [stepValue](double v) {
            stepValue->SetText("Quantity: " + std::to_string(static_cast<int>(v)));
        };

        // ===== 5. CHEVRON GLYPH + prefix, wrapping angle =====
        auto angleRow = addSection("AngleSpinLabel", "Angle (0–359°, wraps, chevron glyph):");

        auto angleSpin = CreateIntSpinner("AngleSpinner", 0, 0, spinW, spinH, 0, 359, 90, 15);
        angleSpin->SetWrap(true);
        angleSpin->SetSuffix("\xC2\xB0");   // degree sign (UTF-8)
        angleSpin->SetButtonGlyph(SpinnerButtonGlyph::Chevron);
        DemoKit::AddFixed(angleRow, angleSpin);

        auto angleValue = makeValueLabel(angleRow, "AngleSpinValue", "Angle: 90\xC2\xB0");
        angleSpin->onValueChanged = [angleValue](double v) {
            angleValue->SetText("Angle: " + std::to_string(static_cast<int>(v)) + "\xC2\xB0");
        };

        // ===== 6. CUSTOM FORMATTER (months) =====
        auto monthRow = addSection("MonthSpinLabel", "Custom formatter (month names):");

        auto monthSpin = CreateIntSpinner("MonthSpinner", 0, 0, spinW, spinH, 1, 12, 7, 1);
        monthSpin->SetWrap(true);
        monthSpin->SetEditable(false);
        monthSpin->SetTextAlignment(TextAlignment::Center);
        // Non-editable, so offer the dropdown as the way to pick a month.
        monthSpin->SetDropdownEnabled(true);
        monthSpin->SetFormatter([](double v) {
            static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            int idx = static_cast<int>(v) - 1;
            if (idx < 0 || idx > 11) return std::string("--");
            return std::string(names[idx]);
        });
        DemoKit::AddFixed(monthRow, monthSpin);

        auto monthValue = makeValueLabel(monthRow, "MonthSpinValue", "Month #7");
        monthSpin->onValueChanged = [monthValue](double v) {
            monthValue->SetText("Month #" + std::to_string(static_cast<int>(v)));
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(container, "SpinnerInstructions", "Interaction", {
                "Click the arrow buttons, or press Up/Down (Left/Right) to step",
                "PageUp / PageDown step by the page amount; Home / End jump to min / max",
                "Scroll the mouse wheel over a spinner to change its value",
                "Type directly into an editable field (the current value is selected, so "
                "typing replaces it), then press Enter to commit (Esc to cancel)",
                "The Size and Month spinners have a value dropdown \xE2\x80\x93 click the "
                "field to pick from the full list"});

        return container;
    }

} // namespace UltraCanvas
