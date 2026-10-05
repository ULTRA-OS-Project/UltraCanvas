// Apps/DemoApp/UltraCanvasBusyIndicatorExamples.cpp
// Demonstration of UltraCanvasBusyIndicator: the six kinds (Ring, DualRing,
// Dots, Bar, Pulse, DotRing), each at a small and a large size and in a second
// colour, DotRing's three fades side by side, a status line as an app would
// use one, and Start / Stop for all of them.
// Version: 1.1.0
// Last Modified: 2026-10-04
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBusyIndicatorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("BusyIndicatorExamples", 0, 0, 1000, 920);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("BusyTitle", 20, 10, 0, 30);
        title->SetText("Busy Indicator Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("BusySubtitle", 20, 42, 940, 40);
        subtitle->SetText("\"Working on it\" when there is no percentage to show. Six kinds "
                          "(BusyIndicatorKind), all driven by Start() / Stop(); the timer runs "
                          "only while an indicator is turning.");
        subtitle->SetFontSize(12);
        subtitle->SetWrap(TextWrap::WrapWord);
        container->AddChild(subtitle);

        // Every indicator on the page, for Start all / Stop all. Raw pointers:
        // the container owns the elements and the buttons live in it too.
        auto indicators = std::make_shared<std::vector<UltraCanvasBusyIndicator*>>();
        auto add = [&](const std::shared_ptr<UltraCanvasBusyIndicator>& indicator) {
            container->AddChild(indicator);
            indicators->push_back(indicator.get());
            indicator->Start();
        };

        auto columnHeader = [&](const std::string& id, const std::string& text, float x) {
            auto l = CreateLabel(id, x, 92, 180, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            l->SetFontSize(12);
            container->AddChild(l);
        };
        columnHeader("BusyColKind",  "Kind",          20);
        columnHeader("BusyColSmall", "Small",         180);
        columnHeader("BusyColLarge", "Large",         340);
        columnHeader("BusyColColor", "Other colour",  540);
        columnHeader("BusyColNote",  "Box",           740);

        struct Row {
            const char* name;
            BusyIndicatorKind kind;
            float smallW, smallH, largeW, largeH;
            const char* note;
        };
        const Row rows[] = {
            {"Ring",     BusyIndicatorKind::Ring,     16, 16,  40, 40, "square, 16 x 16"},
            {"DualRing", BusyIndicatorKind::DualRing, 20, 20,  48, 48, "square, 20 x 20; two colours"},
            {"Dots",     BusyIndicatorKind::Dots,     36, 10,  90, 24, "wide, 36 x 10"},
            {"Bar",      BusyIndicatorKind::Bar,      120, 4, 180, 8,  "wide, 120 x 4; green: 40 px segment"},
            {"Pulse",    BusyIndicatorKind::Pulse,    14, 14,  40, 40, "square, 14 x 14"},
            {"DotRing",  BusyIndicatorKind::DotRing,  20, 20,  48, 48, "square, 20 x 20"},
        };
        const int rowCount = static_cast<int>(std::size(rows));
        const Color otherColour(230, 120, 20, 255);   // orange

        float y = 120;
        const float rowHeight = 70;
        for (int i = 0; i < rowCount; ++i) {
            const Row& row = rows[i];
            const std::string n = std::to_string(i);
            const float mid = y + rowHeight / 2;

            auto name = CreateLabel("BusyName" + n, 20, mid - 10, 150, 20);
            name->SetText(row.name);
            name->SetFontSize(12);
            container->AddChild(name);

            add(CreateBusyIndicator("BusySmall" + n, 180, mid - row.smallH / 2,
                                    row.smallW, row.smallH, row.kind));
            add(CreateBusyIndicator("BusyLarge" + n, 340, mid - row.largeH / 2,
                                    row.largeW, row.largeH, row.kind));

            auto coloured = CreateBusyIndicator("BusyColour" + n, 540, mid - row.largeH / 2,
                                                row.largeW, row.largeH, row.kind);
            // Orange, with a purple inner ring for DualRing; Bar instead gets a
            // green segment of a fixed 40 px.
            BusyIndicatorStyle style = coloured->GetStyle();
            style.arcColor = otherColour;
            style.trackColor = Color(230, 120, 20, 40);
            if (row.kind == BusyIndicatorKind::DualRing) {
                style.secondArcColor = Color(140, 60, 200, 255);   // purple
            } else if (row.kind == BusyIndicatorKind::Bar) {
                style.arcColor = Color(30, 160, 70, 255);          // green
                style.trackColor = Color(30, 160, 70, 40);
                style.barLength = 40.0f;
            }
            coloured->SetStyle(style);
            add(coloured);

            auto note = CreateLabel("BusyNote" + n, 740, mid - 10, 250, 20);
            note->SetText(row.note);
            note->SetFontSize(11);
            container->AddChild(note);

            y += rowHeight;
        }

        // ===== DOTRING: THE THREE FADES =====
        auto fadeHeader = CreateLabel("BusyFadeHeader", 20, y + 10, 700, 20);
        fadeHeader->SetText("DotRing, three ways (dotRingFade):");
        fadeHeader->SetFontWeight(FontWeight::Bold);
        fadeHeader->SetFontSize(12);
        container->AddChild(fadeHeader);

        struct FadeDemo {
            BusyDotRingFade fade;
            float x, captionWidth;   // the caption stops short of the next column
            const char* caption;
        };
        const FadeDemo fades[] = {
            {BusyDotRingFade::Fade,            180, 150, "Fade: dots fade out behind the head"},
            {BusyDotRingFade::NoFade,          340, 190, "NoFade: solid dots, the head shows by size"},
            {BusyDotRingFade::FadeRandomColor, 540, 250, "FadeRandomColor: a new random colour at every fade-in"},
        };
        for (int i = 0; i < static_cast<int>(std::size(fades)); ++i) {
            const FadeDemo& demo = fades[i];
            const std::string n = std::to_string(i);
            auto ring = CreateBusyIndicator("BusyFade" + n, demo.x, y + 40, 64, 64,
                                            BusyIndicatorKind::DotRing);
            BusyIndicatorStyle style = ring->GetStyle();
            style.dotRingFade = demo.fade;
            ring->SetStyle(style);
            add(ring);

            auto caption = CreateLabel("BusyFadeCaption" + n, demo.x, y + 110, demo.captionWidth, 32);
            caption->SetText(demo.caption);
            caption->SetFontSize(11);
            caption->SetWrap(TextWrap::WrapWord);
            container->AddChild(caption);
        }
        y += 150;

        // ===== A STATUS LINE, AS AN APP USES ONE =====
        auto statusHeader = CreateLabel("BusyStatusHeader", 20, y + 10, 500, 20);
        statusHeader->SetText("In a status line (indicator, then the text):");
        statusHeader->SetFontWeight(FontWeight::Bold);
        statusHeader->SetFontSize(12);
        container->AddChild(statusHeader);
        y += 40;

        const char* statusTexts[] = {"Checking mail…", "Syncing folders…", "Receiving messages… (42)",
                                     "Loading images…", "Contacting the server…", "Indexing attachments…"};
        for (int i = 0; i < rowCount; ++i) {
            const std::string n = std::to_string(i);
            const float x = 20 + (i % 3) * 320.0f;
            const float rowY = y + (i / 3) * 30.0f;
            const Row& row = rows[i];
            // Square kinds at the text height (DotRing a little larger, so its
            // dots stay dots), wide kinds a little wider.
            const float square = row.kind == BusyIndicatorKind::DotRing ? 18.0f : 14.0f;
            const float w = row.smallW > row.smallH ? std::min(row.smallW, 40.0f) : square;
            const float h = row.smallW > row.smallH ? std::min(row.smallH, 8.0f) : square;
            add(CreateBusyIndicator("BusyStatus" + n, x, rowY + 10 - h / 2, w, h, row.kind));
            auto text = CreateLabel("BusyStatusText" + n, x + w + 8, rowY, 260, 20);
            text->SetText(statusTexts[i]);
            text->SetFontSize(12);
            container->AddChild(text);
        }
        y += 70;

        // ===== START / STOP =====
        auto startBtn = std::make_shared<UltraCanvasButton>("BusyStart", 20, y, 100.0f, 30.0f, "Start all");
        startBtn->onClick = [indicators]() { for (auto* b : *indicators) b->Start(); };
        container->AddChild(startBtn);

        auto stopBtn = std::make_shared<UltraCanvasButton>("BusyStop", 130, y, 100.0f, 30.0f, "Stop all");
        stopBtn->onClick = [indicators]() { for (auto* b : *indicators) b->Stop(); };
        container->AddChild(stopBtn);

        auto idleBtn = std::make_shared<UltraCanvasButton>("BusyIdle", 240, y, 190.0f, 30.0f,
                                                           "Show when stopped");
        idleBtn->onClick = [indicators]() {
            for (auto* b : *indicators) {
                BusyIndicatorStyle style = b->GetStyle();
                style.hideWhenStopped = !style.hideWhenStopped;
                b->SetStyle(style);
            }
        };
        container->AddChild(idleBtn);
        y += 44;

        auto instructions = CreateLabel("BusyInstructions", 20, y, 940, 60);
        instructions->SetText(
            "Stop all halts every timer; a stopped indicator draws nothing unless "
            "hideWhenStopped is false (\"Show when stopped\" toggles it), and then rests "
            "where it stopped. Start all resumes from there.");
        instructions->SetFontSize(11);
        instructions->SetWrap(TextWrap::WrapWord);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
