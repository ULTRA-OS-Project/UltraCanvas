// Apps/DemoApp/UltraCanvasTimePickerExamples.cpp
// Demonstration of UltraCanvasTimePicker: 24-hour and 12-hour AM/PM fields,
// optional seconds, minute stepping and min/max constraints. The popup reuses
// UltraCanvasSpinner for the hour / minute / second fields.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTimePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimePickerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TimePickerExamples", 0, 0, 1000, 700);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("TimePickerTitle", 20, 10, 0, 30);
        title->SetText("Time Picker Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("TimePickerSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Click the clock button (or press Up/Down) to open the hour / minute spinners. "
                          "You can also type a time directly, or scroll the wheel over a field.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        const int fieldX = 20;
        const int labelX = 260;
        int y = 100;

        auto addReadout = [&](const std::string& id, const std::string& initial) {
            auto lbl = CreateLabel(id, labelX, y + 3, 320, 22);
            lbl->SetText(initial);
            lbl->SetBackgroundColor(Color(240, 240, 240));
            lbl->SetPadding(3);
            container->AddChild(lbl);
            return lbl;
        };

        // ===== 1. 24-HOUR =====
        auto l1 = CreateLabel("TP1Label", fieldX, y - 22, 220, 20);
        l1->SetText("24-hour (HH:mm):");
        l1->SetFontWeight(FontWeight::Bold);
        container->AddChild(l1);

        auto tp1 = CreateTimePicker24h("TP1", fieldX, y, 160, 28, false);
        tp1->SetTime(UCTime(9, 30));
        container->AddChild(tp1);
        auto r1 = addReadout("TP1Value", "Selected: 09:30");
        tp1->onTimeChanged = [r1](const UCTime& t) {
            r1->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm"));
        };
        y += 60;

        // ===== 2. 12-HOUR AM/PM =====
        auto l2 = CreateLabel("TP2Label", fieldX, y - 22, 220, 20);
        l2->SetText("12-hour (hh:mm AM/PM):");
        l2->SetFontWeight(FontWeight::Bold);
        container->AddChild(l2);

        auto tp2 = CreateTimePicker12h("TP2", fieldX, y, 180, 28, false);
        tp2->SetTime(UCTime(14, 15));   // 2:15 PM
        container->AddChild(tp2);
        auto r2 = addReadout("TP2Value", "Selected: 02:15 PM");
        tp2->onTimeChanged = [r2](const UCTime& t) {
            r2->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("hh:mm tt"));
        };
        y += 60;

        // ===== 3. WITH SECONDS =====
        auto l3 = CreateLabel("TP3Label", fieldX, y - 22, 220, 20);
        l3->SetText("24-hour with seconds (HH:mm:ss):");
        l3->SetFontWeight(FontWeight::Bold);
        container->AddChild(l3);

        auto tp3 = CreateTimePicker24h("TP3", fieldX, y, 190, 28, true);
        tp3->SetTime(UCTime(23, 59, 45));
        container->AddChild(tp3);
        auto r3 = addReadout("TP3Value", "Selected: 23:59:45");
        tp3->onTimeChanged = [r3](const UCTime& t) {
            r3->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm:ss"));
        };
        y += 60;

        // ===== 4. MINUTE STEP + CONSTRAINTS =====
        auto l4 = CreateLabel("TP4Label", fieldX, y - 22, 320, 20);
        l4->SetText("15-min step, business hours 09:00–17:00:");
        l4->SetFontWeight(FontWeight::Bold);
        container->AddChild(l4);

        auto tp4 = CreateTimePicker24h("TP4", fieldX, y, 160, 28, false);
        tp4->SetMinuteStep(15);
        tp4->SetMinTime(UCTime(9, 0));
        tp4->SetMaxTime(UCTime(17, 0));
        tp4->SetTime(UCTime(12, 0));
        container->AddChild(tp4);
        auto r4 = addReadout("TP4Value", "Meeting at: 12:00");
        tp4->onTimeChanged = [r4](const UCTime& t) {
            r4->SetText(t.IsEmpty() ? "Meeting at: (none)" : "Meeting at: " + t.Format("HH:mm"));
        };
        y += 70;

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("TimePickerInstructions", 20, y, 940, 90);
        instructions->SetText(
                "Interaction:\n"
                "\xE2\x80\xA2 Click the clock button (or press Up/Down) to open the spinner popup\n"
                "\xE2\x80\xA2 Adjust hours / minutes with the spinner arrows, wheel or keyboard; \"Now\" sets the current time\n"
                "\xE2\x80\xA2 Type a time directly into the field (e.g. \"9:30\", \"2:15 pm\") and press Enter\n"
                "\xE2\x80\xA2 Scroll the mouse wheel over a field to nudge the time by the minute step");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
