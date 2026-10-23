// Apps/DemoApp/UltraCanvasTimePickerExamples.cpp
// Demonstration of UltraCanvasTimePicker: 24-hour and 12-hour AM/PM fields,
// optional seconds, minute stepping, min/max constraints and the two popup
// styles (spinner panel and circular clock-face dial).
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  picker in a wrapping row, every text sized to fit, the
//                  instructions as a notes card
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasTimePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimePickerExamples() {
        auto container = DemoKit::CreatePage("TimePickerExamples", "Time Picker Examples",
                "Click the clock button (or press Up/Down) to open the popup. "
                "You can also type a time directly, or scroll the wheel over a field.");

        const int fieldW = 190;      // all picker fields share the same width

        // The pickers share the width and wrap under each other when narrow.
        auto examples = DemoKit::AddRow(container, "TimePickerCards");

        // One example: a card titled with the caption, the picker and its
        // readout side by side (the readout under it when narrow).
        std::shared_ptr<UltraCanvasContainer> line;
        auto addCaption = [&](const std::string& id, const std::string& text) {
            auto card = DemoKit::AddCard(examples, id, text);
            line = DemoKit::AddRow(card, id + "Row", 12);
            line->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        };

        auto addReadout = [&](const std::string& id, const std::string& initial) {
            auto lbl = DemoKit::AddText(line, id, initial);
            lbl->SetBackgroundColor(Color(240, 240, 240));
            lbl->SetPadding(3);
            lbl->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            return lbl;
        };

        // ===== 1. 24-HOUR =====
        addCaption("TP1Label", "24-hour (HH:mm)");

        auto tp1 = CreateTimePicker24h("TP1", 0, 0, fieldW, 28, false);
        tp1->SetTime(UCTime(9, 30));
        DemoKit::AddFixed(line, tp1);
        auto r1 = addReadout("TP1Value", "Selected: 09:30");
        tp1->onTimeChanged = [r1](const UCTime& t) {
            r1->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm"));
        };

        // ===== 2. 12-HOUR AM/PM =====
        addCaption("TP2Label", "12-hour (hh:mm AM/PM)");

        auto tp2 = CreateTimePicker12h("TP2", 0, 0, fieldW, 28, false);
        tp2->SetTime(UCTime(14, 15));   // 2:15 PM
        DemoKit::AddFixed(line, tp2);
        auto r2 = addReadout("TP2Value", "Selected: 02:15 PM");
        tp2->onTimeChanged = [r2](const UCTime& t) {
            r2->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("hh:mm tt"));
        };

        // ===== 3. WITH SECONDS =====
        addCaption("TP3Label", "24-hour with seconds (HH:mm:ss)");

        auto tp3 = CreateTimePicker24h("TP3", 0, 0, fieldW, 28, true);
        tp3->SetTime(UCTime(23, 59, 45));
        DemoKit::AddFixed(line, tp3);
        auto r3 = addReadout("TP3Value", "Selected: 23:59:45");
        tp3->onTimeChanged = [r3](const UCTime& t) {
            r3->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm:ss"));
        };

        // ===== 4. MINUTE STEP + CONSTRAINTS =====
        addCaption("TP4Label", "15-min step, business hours 09:00\xE2\x80\x93" "17:00");

        auto tp4 = CreateTimePicker24h("TP4", 0, 0, fieldW, 28, false);
        tp4->SetMinuteStep(15);
        tp4->SetMinTime(UCTime(9, 0));
        tp4->SetMaxTime(UCTime(17, 0));
        tp4->SetTime(UCTime(12, 0));
        DemoKit::AddFixed(line, tp4);
        auto r4 = addReadout("TP4Value", "Meeting at: 12:00");
        tp4->onTimeChanged = [r4](const UCTime& t) {
            r4->SetText(t.IsEmpty() ? "Meeting at: (none)" : "Meeting at: " + t.Format("HH:mm"));
        };

        // ===== 5. CLOCK DIAL, 24-HOUR =====
        addCaption("TP5Label", "Clock dial popup, 24-hour");

        auto tp5 = CreateClockTimePicker("TP5", 0, 0, fieldW, 28, /*use24h*/ true);
        tp5->SetTime(UCTime(5, 12));
        DemoKit::AddFixed(line, tp5);
        auto r5 = addReadout("TP5Value", "Selected: 05:12");
        tp5->onTimeChanged = [r5](const UCTime& t) {
            r5->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("HH:mm"));
        };

        // ===== 6. CLOCK DIAL, 12-HOUR AM/PM =====
        addCaption("TP6Label", "Clock dial popup, 12-hour AM/PM");

        auto tp6 = CreateClockTimePicker("TP6", 0, 0, fieldW, 28, /*use24h*/ false);
        tp6->SetTime(UCTime(19, 40));   // 7:40 PM
        DemoKit::AddFixed(line, tp6);
        auto r6 = addReadout("TP6Value", "Selected: 07:40 PM");
        tp6->onTimeChanged = [r6](const UCTime& t) {
            r6->SetText(t.IsEmpty() ? "Selected: (none)" : "Selected: " + t.Format("hh:mm tt"));
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(container, "TimePickerInstructions", "Interaction", {
                "Click the clock button (or press Up/Down) to open the popup \xE2\x80\x94 spinners or the round clock dial",
                "Clock dial: pick the hour on the dial (outer ring 00/13\xE2\x80\x93""23, inner 12/01\xE2\x80\x93""11), then the minutes; "
                "click the header to switch between hour / minute / AM/PM",
                "Type a time directly into the field (e.g. \"9:30\", \"2:15 pm\") and press Enter \xE2\x80\x94 "
                "click inside the text to place the cursor, use Left/Right/Home/End/Del to edit",
                "Scroll the mouse wheel over a field to nudge the time by the minute step"});

        return container;
    }

} // namespace UltraCanvas
