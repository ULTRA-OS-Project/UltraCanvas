// Apps/DemoApp/UltraCanvasTimePickerExamples.cpp
// Demonstration of UltraCanvasTimePicker: 24-hour and 12-hour AM/PM fields,
// optional seconds, minute stepping, min/max constraints and the two popup
// styles (spinner panel and circular clock-face dial).
// Version: 1.1.0
// Last Modified: 2026-07-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTimePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimePickerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TimePickerExamples", 0, 0, 1000, 760);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("TimePickerTitle", 20, 10, 0, 30);
        title->SetText("Time Picker Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("TimePickerSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Click the clock button (or press Up/Down) to open the popup. "
                          "You can also type a time directly, or scroll the wheel over a field.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        const int fieldX = 20;
        const int fieldW = 190;      // all picker fields share the same width
        const int labelX = 260;
        int y = 100;

        // Section captions auto-size (width 0) so the full text is always visible.
        auto addCaption = [&](const std::string& id, const std::string& text) {
            auto lbl = CreateLabel(id, fieldX, y - 22, 0, 20);
            lbl->SetText(text);
            lbl->SetFontWeight(FontWeight::Bold);
            container->AddChild(lbl);
            return lbl;
        };

        auto addReadout = [&](const std::string& id, const std::string& initial) {
            auto lbl = CreateLabel(id, labelX, y + 3, 320, 22);
            lbl->SetText(initial);
            lbl->SetBackgroundColor(Color(240, 240, 240));
            lbl->SetPadding(3);
            container->AddChild(lbl);
            return lbl;
        };

        // ===== 1. 24-HOUR =====
        addCaption("TP1Label", "24-hour (HH:mm)");

        auto tp1 = CreateTimePicker24h("TP1", fieldX, y, fieldW, 28, false);
        tp1->SetTime(UCTime(9, 30));
        container->AddChild(tp1);
        auto r1 = addReadout("TP1Value", "Selected: 09:30");
        tp1->onTimeChanged = [r1](const UCTime& t) {
            r1->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm"));
        };
        y += 60;

        // ===== 2. 12-HOUR AM/PM =====
        addCaption("TP2Label", "12-hour (hh:mm AM/PM)");

        auto tp2 = CreateTimePicker12h("TP2", fieldX, y, fieldW, 28, false);
        tp2->SetTime(UCTime(14, 15));   // 2:15 PM
        container->AddChild(tp2);
        auto r2 = addReadout("TP2Value", "Selected: 02:15 PM");
        tp2->onTimeChanged = [r2](const UCTime& t) {
            r2->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("hh:mm tt"));
        };
        y += 60;

        // ===== 3. WITH SECONDS =====
        addCaption("TP3Label", "24-hour with seconds (HH:mm:ss)");

        auto tp3 = CreateTimePicker24h("TP3", fieldX, y, fieldW, 28, true);
        tp3->SetTime(UCTime(23, 59, 45));
        container->AddChild(tp3);
        auto r3 = addReadout("TP3Value", "Selected: 23:59:45");
        tp3->onTimeChanged = [r3](const UCTime& t) {
            r3->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm:ss"));
        };
        y += 60;

        // ===== 4. MINUTE STEP + CONSTRAINTS =====
        addCaption("TP4Label", "15-min step, business hours 09:00\xE2\x80\x93" "17:00");

        auto tp4 = CreateTimePicker24h("TP4", fieldX, y, fieldW, 28, false);
        tp4->SetMinuteStep(15);
        tp4->SetMinTime(UCTime(9, 0));
        tp4->SetMaxTime(UCTime(17, 0));
        tp4->SetTime(UCTime(12, 0));
        container->AddChild(tp4);
        auto r4 = addReadout("TP4Value", "Meeting at: 12:00");
        tp4->onTimeChanged = [r4](const UCTime& t) {
            r4->SetText(t.IsEmpty() ? "Meeting at: (none)" : "Meeting at: " + t.Format("HH:mm"));
        };
        y += 60;

        // ===== 5. CLOCK DIAL, 24-HOUR =====
        addCaption("TP5Label", "Clock dial popup, 24-hour");

        auto tp5 = CreateClockTimePicker("TP5", fieldX, y, fieldW, 28, /*use24h*/ true);
        tp5->SetTime(UCTime(5, 12));
        container->AddChild(tp5);
        auto r5 = addReadout("TP5Value", "Selected: 05:12");
        tp5->onTimeChanged = [r5](const UCTime& t) {
            r5->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm"));
        };
        y += 60;

        // ===== 6. CLOCK DIAL, 12-HOUR AM/PM =====
        addCaption("TP6Label", "Clock dial popup, 12-hour AM/PM");

        auto tp6 = CreateClockTimePicker("TP6", fieldX, y, fieldW, 28, /*use24h*/ false);
        tp6->SetTime(UCTime(19, 40));   // 7:40 PM
        container->AddChild(tp6);
        auto r6 = addReadout("TP6Value", "Selected: 07:40 PM");
        tp6->onTimeChanged = [r6](const UCTime& t) {
            r6->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("hh:mm tt"));
        };
        y += 70;

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("TimePickerInstructions", 20, y, 940, 110);
        instructions->SetText(
                "Interaction:\n"
                "\xE2\x80\xA2 Click the clock button (or press Up/Down) to open the popup \xE2\x80\x94 spinners or the round clock dial\n"
                "\xE2\x80\xA2 Clock dial: pick the hour on the dial (outer ring 00/13\xE2\x80\x93""23, inner 12/01\xE2\x80\x93""11), then the minutes; "
                "click the header to switch between hour / minute / AM/PM\n"
                "\xE2\x80\xA2 Type a time directly into the field (e.g. \"9:30\", \"2:15 pm\") and press Enter \xE2\x80\x94 "
                "click inside the text to place the cursor, use Left/Right/Home/End/Del to edit\n"
                "\xE2\x80\xA2 Scroll the mouse wheel over a field to nudge the time by the minute step");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
