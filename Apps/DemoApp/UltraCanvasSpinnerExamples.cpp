// Apps/DemoApp/UltraCanvasSpinnerExamples.cpp
// Demonstration of the UltraCanvasSpinner control: integer / decimal / list
// value modes, vertical up-down and horizontal stepper layouts, wrapping,
// prefixes / suffixes and custom formatting.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSpinnerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("SpinnerExamples", 0, 0, 1000, 900);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("SpinnerTitle", 20, 10, 0, 30);
        title->SetText("Spinner / SpinBox Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("SpinnerSubtitle", 20, 40, 900, 40);
        subtitle->SetText("Select a value with the up/down (or left/right) arrow buttons, the "
                          "keyboard arrow keys, the mouse wheel, or by typing into the field.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        int y = 100;
        const int labelX = 360;

        // ===== 1. INTEGER SPINNER (vertical up/down buttons) =====
        auto intLabel = CreateLabel("IntSpinLabel", 20, y - 22, 320, 20);
        intLabel->SetText("Integer (0–100, step 5):");
        intLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(intLabel);

        auto intSpin = CreateIntSpinner("IntSpinner", 20, y, 120, 28, 0, 100, 20, 5);
        container->AddChild(intSpin);

        auto intValue = CreateLabel("IntSpinValue", labelX, y + 4, 260, 20);
        intValue->SetText("Value: 20");
        intValue->SetBackgroundColor(Color(240, 240, 240));
        intValue->SetPadding(3);
        container->AddChild(intValue);
        intSpin->onValueChanged = [intValue](double v) {
            intValue->SetText("Value: " + std::to_string(static_cast<int>(v)));
        };
        y += 55;

        // ===== 2. DECIMAL SPINNER with suffix =====
        auto decLabel = CreateLabel("DecSpinLabel", 20, y - 22, 320, 20);
        decLabel->SetText("Decimal (0.0–5.0, step 0.25, 2 dp):");
        decLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(decLabel);

        auto decSpin = CreateDecimalSpinner("DecSpinner", 20, y, 140, 28, 0.0, 5.0, 1.5, 0.25, 2);
        decSpin->SetSuffix(" s");
        container->AddChild(decSpin);

        auto decValue = CreateLabel("DecSpinValue", labelX, y + 4, 260, 20);
        decValue->SetText("Duration: 1.50 s");
        decValue->SetBackgroundColor(Color(240, 240, 240));
        decValue->SetPadding(3);
        container->AddChild(decValue);
        decSpin->onValueChanged = [decValue](double v) {
            std::ostringstream oss;
            oss << "Duration: " << std::fixed << std::setprecision(2) << v << " s";
            decValue->SetText(oss.str());
        };
        y += 55;

        // ===== 3. LIST SPINNER (cycle arbitrary values) =====
        auto listLabel = CreateLabel("ListSpinLabel", 20, y - 22, 320, 20);
        listLabel->SetText("List (cycle string values, wraps):");
        listLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(listLabel);

        auto listSpin = CreateListSpinner("ListSpinner", 20, y, 160, 28,
                                          {"Small", "Medium", "Large", "X-Large"});
        listSpin->SetWrap(true);
        listSpin->SetTextAlignment(TextAlignment::Center);
        container->AddChild(listSpin);

        auto listValue = CreateLabel("ListSpinValue", labelX, y + 4, 260, 20);
        listValue->SetText("Size: Small");
        listValue->SetBackgroundColor(Color(240, 240, 240));
        listValue->SetPadding(3);
        container->AddChild(listValue);
        listSpin->onSelectionChanged = [listValue](int, const std::string& text) {
            listValue->SetText("Size: " + text);
        };
        y += 55;

        // ===== 4. HORIZONTAL STEPPER  [−] value [+] =====
        auto stepLabel = CreateLabel("StepSpinLabel", 20, y - 22, 320, 20);
        stepLabel->SetText("Horizontal stepper (quantity 1–99):");
        stepLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(stepLabel);

        auto stepper = CreateStepper("QtyStepper", 20, y, 140, 28, 1, 99, 1, 1);
        container->AddChild(stepper);

        auto stepValue = CreateLabel("StepSpinValue", labelX, y + 4, 260, 20);
        stepValue->SetText("Quantity: 1");
        stepValue->SetBackgroundColor(Color(240, 240, 240));
        stepValue->SetPadding(3);
        container->AddChild(stepValue);
        stepper->onValueChanged = [stepValue](double v) {
            stepValue->SetText("Quantity: " + std::to_string(static_cast<int>(v)));
        };
        y += 55;

        // ===== 5. CHEVRON GLYPH + prefix, wrapping angle =====
        auto angleLabel = CreateLabel("AngleSpinLabel", 20, y - 22, 320, 20);
        angleLabel->SetText("Angle (0–359°, wraps, chevron glyph):");
        angleLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(angleLabel);

        auto angleSpin = CreateIntSpinner("AngleSpinner", 20, y, 130, 28, 0, 359, 90, 15);
        angleSpin->SetWrap(true);
        angleSpin->SetSuffix("\xC2\xB0");   // degree sign (UTF-8)
        angleSpin->SetButtonGlyph(SpinnerButtonGlyph::Chevron);
        container->AddChild(angleSpin);

        auto angleValue = CreateLabel("AngleSpinValue", labelX, y + 4, 260, 20);
        angleValue->SetText("Angle: 90\xC2\xB0");
        angleValue->SetBackgroundColor(Color(240, 240, 240));
        angleValue->SetPadding(3);
        container->AddChild(angleValue);
        angleSpin->onValueChanged = [angleValue](double v) {
            angleValue->SetText("Angle: " + std::to_string(static_cast<int>(v)) + "\xC2\xB0");
        };
        y += 55;

        // ===== 6. CUSTOM FORMATTER (months) =====
        auto monthLabel = CreateLabel("MonthSpinLabel", 20, y - 22, 320, 20);
        monthLabel->SetText("Custom formatter (month names):");
        monthLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(monthLabel);

        auto monthSpin = CreateIntSpinner("MonthSpinner", 20, y, 150, 28, 1, 12, 7, 1);
        monthSpin->SetWrap(true);
        monthSpin->SetEditable(false);
        monthSpin->SetTextAlignment(TextAlignment::Center);
        monthSpin->SetFormatter([](double v) {
            static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            int idx = static_cast<int>(v) - 1;
            if (idx < 0 || idx > 11) return std::string("--");
            return std::string(names[idx]);
        });
        container->AddChild(monthSpin);

        auto monthValue = CreateLabel("MonthSpinValue", labelX, y + 4, 260, 20);
        monthValue->SetText("Month #7");
        monthValue->SetBackgroundColor(Color(240, 240, 240));
        monthValue->SetPadding(3);
        container->AddChild(monthValue);
        monthSpin->onValueChanged = [monthValue](double v) {
            monthValue->SetText("Month #" + std::to_string(static_cast<int>(v)));
        };
        y += 70;

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("SpinnerInstructions", 20, y, 900, 90);
        instructions->SetText(
                "Interaction:\n"
                "\xE2\x80\xA2 Click the arrow buttons, or press Up/Down (Left/Right) to step\n"
                "\xE2\x80\xA2 PageUp / PageDown step by the page amount; Home / End jump to min / max\n"
                "\xE2\x80\xA2 Scroll the mouse wheel over a spinner to change its value\n"
                "\xE2\x80\xA2 Type directly into an editable field, then press Enter to commit (Esc to cancel)");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
