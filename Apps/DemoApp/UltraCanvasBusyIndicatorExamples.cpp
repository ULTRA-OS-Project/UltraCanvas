// Apps/DemoApp/UltraCanvasBusyIndicatorExamples.cpp
// Demonstration of UltraCanvasBusyIndicator: the six kinds (Ring, DualRing,
// Dots, Bar, Pulse, DotRing), each at a small and a large size and in a second
// colour, DotRing's three fades side by side, a status line as an app would
// use one, and Start / Stop for all of them.
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the kinds as a
//                  table of rows in a card, fades / status line / buttons in
//                  cards, every text sized to fit, buttons as wide as their text
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBusyIndicatorExamples() {
        auto page = DemoKit::CreatePage("BusyIndicatorExamples", "Busy Indicator Examples",
                "\"Working on it\" when there is no percentage to show. Six kinds "
                "(BusyIndicatorKind), all driven by Start() / Stop(); the timer runs "
                "only while an indicator is turning.");

        // Every indicator on the page, for Start all / Stop all. Raw pointers:
        // the page owns the elements and the buttons live in it too.
        auto indicators = std::make_shared<std::vector<UltraCanvasBusyIndicator*>>();
        auto add = [&](const std::shared_ptr<UltraCanvasContainer>& parent,
                       const std::shared_ptr<UltraCanvasBusyIndicator>& indicator) {
            DemoKit::AddFixed(parent, indicator);
            indicators->push_back(indicator.get());
            indicator->Start();
        };

        // A table column: a fixed width so the columns line up from row to
        // row, and the height of its content (or `height`, which centres an
        // indicator in its row). Text in it wraps instead of widening it.
        auto cell = [](const std::string& id, float width, float height = 0) {
            auto c = DemoKit::CreateColumn(id, 2);
            c->SetElementSize(CSSLayout::Dimension::Px(width),
                              height > 0 ? CSSLayout::Dimension::Px(height) : CSSLayout::Dimension::Auto());
            c->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center)
                     .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
            return c;
        };
        // An indicator centred in its cell (AddFixed puts it at the start).
        auto addCentred = [&](const std::shared_ptr<UltraCanvasContainer>& parent,
                              const std::shared_ptr<UltraCanvasBusyIndicator>& indicator) {
            add(parent, indicator);
            indicator->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        };
        const float smallColumn = 130, largeColumn = 190, rowHeight = 56;

        // ===== THE SIX KINDS =====
        auto kindsCard = DemoKit::AddCard(page, "BusyKindsCard", "The six kinds",
                "Each at a small and a large size and in a second colour");

        auto headerRow = DemoKit::AddRow(kindsCard, "BusyHeaderRow");
        auto columnHeader = [&](const std::shared_ptr<UltraCanvasContainer>& parent,
                                const std::string& id, const std::string& text, bool centred) {
            auto l = DemoKit::CreateText(id, text);
            l->SetFontWeight(FontWeight::Bold);
            // Over an indicator column centred, as the indicators are.
            if (centred) l->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            DemoKit::Add(parent, l);
        };
        auto headSmall = cell("BusyColSmallCell", smallColumn);
        auto headLarge = cell("BusyColLargeCell", largeColumn);
        auto headColor = cell("BusyColColorCell", largeColumn);
        columnHeader(headSmall, "BusyColSmall", "Small", true);
        columnHeader(headLarge, "BusyColLarge", "Large", true);
        columnHeader(headColor, "BusyColColor", "Other colour", true);
        DemoKit::AddFixed(headerRow, headSmall);
        DemoKit::AddFixed(headerRow, headLarge);
        DemoKit::AddFixed(headerRow, headColor);
        auto headText = DemoKit::CreateColumn("BusyColText", 2);
        columnHeader(headText, "BusyColKind", "Kind", false);
        columnHeader(headText, "BusyColNote", "Box", false);
        DemoKit::Add(headerRow, headText, 1);

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

        for (int i = 0; i < rowCount; ++i) {
            const Row& row = rows[i];
            const std::string n = std::to_string(i);
            auto tableRow = DemoKit::AddRow(kindsCard, "BusyRow" + n);

            auto smallCell = cell("BusySmallCell" + n, smallColumn, rowHeight);
            addCentred(smallCell, CreateBusyIndicator("BusySmall" + n, 0, 0,
                                                      row.smallW, row.smallH, row.kind));
            DemoKit::AddFixed(tableRow, smallCell);

            auto largeCell = cell("BusyLargeCell" + n, largeColumn, rowHeight);
            addCentred(largeCell, CreateBusyIndicator("BusyLarge" + n, 0, 0,
                                                      row.largeW, row.largeH, row.kind));
            DemoKit::AddFixed(tableRow, largeCell);

            auto coloured = CreateBusyIndicator("BusyColour" + n, 0, 0,
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
            auto colourCell = cell("BusyColourCell" + n, largeColumn, rowHeight);
            addCentred(colourCell, coloured);
            DemoKit::AddFixed(tableRow, colourCell);

            // The kind's name over the box it is drawn in.
            auto text = DemoKit::CreateColumn("BusyText" + n, 2);
            text->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
            DemoKit::AddText(text, "BusyName" + n, row.name);
            DemoKit::AddCaption(text, "BusyNote" + n, row.note);
            DemoKit::Add(tableRow, text, 1);
        }

        // ===== DOTRING: THE THREE FADES =====
        auto fadeCard = DemoKit::AddCard(page, "BusyFadeCard", "DotRing, three ways (dotRingFade):");
        auto fadeRow = DemoKit::AddRow(fadeCard, "BusyFadeRow", 24);

        struct FadeDemo {
            BusyDotRingFade fade;
            const char* caption;
        };
        const FadeDemo fades[] = {
            {BusyDotRingFade::Fade,            "Fade: dots fade out behind the head"},
            {BusyDotRingFade::NoFade,          "NoFade: solid dots, the head shows by size"},
            {BusyDotRingFade::FadeRandomColor, "FadeRandomColor: a new random colour at every fade-in"},
        };
        for (int i = 0; i < static_cast<int>(std::size(fades)); ++i) {
            const FadeDemo& demo = fades[i];
            const std::string n = std::to_string(i);
            // The three share the row's width; the caption wraps under its ring.
            auto column = DemoKit::CreateColumn("BusyFadeColumn" + n, 8);
            DemoKit::Add(fadeRow, column, 1);
            column->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(180));

            auto ring = CreateBusyIndicator("BusyFade" + n, 0, 0, 64, 64,
                                            BusyIndicatorKind::DotRing);
            BusyIndicatorStyle style = ring->GetStyle();
            style.dotRingFade = demo.fade;
            ring->SetStyle(style);
            add(column, ring);

            DemoKit::AddCaption(column, "BusyFadeCaption" + n, demo.caption);
        }

        // ===== A STATUS LINE, AS AN APP USES ONE =====
        auto statusCard = DemoKit::AddCard(page, "BusyStatusCard",
                                           "In a status line (indicator, then the text):");
        auto statusRow = DemoKit::AddRow(statusCard, "BusyStatusRow", 24);
        statusRow->layout.SetFlexGap(10, 32);

        const char* statusTexts[] = {"Checking mail…", "Syncing folders…", "Receiving messages… (42)",
                                     "Loading images…", "Contacting the server…", "Indexing attachments…"};
        for (int i = 0; i < rowCount; ++i) {
            const std::string n = std::to_string(i);
            const Row& row = rows[i];
            // Square kinds at the text height (DotRing a little larger, so its
            // dots stay dots), wide kinds a little wider.
            const float square = row.kind == BusyIndicatorKind::DotRing ? 18.0f : 14.0f;
            const float w = row.smallW > row.smallH ? std::min(row.smallW, 40.0f) : square;
            const float h = row.smallW > row.smallH ? std::min(row.smallH, 8.0f) : square;

            auto line = DemoKit::CreateRow("BusyStatusLine" + n, 8, false);
            line->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            DemoKit::Add(statusRow, line);
            auto indicator = CreateBusyIndicator("BusyStatus" + n, 0, 0, w, h, row.kind);
            add(line, indicator);
            indicator->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
            DemoKit::AddText(line, "BusyStatusText" + n, statusTexts[i]);
        }

        // ===== START / STOP =====
        auto controlCard = DemoKit::AddCard(page, "BusyControlCard", "Start / Stop");
        auto buttonRow = DemoKit::AddRow(controlCard, "BusyButtonRow", 10);

        // Buttons as wide as their text.
        auto startBtn = std::make_shared<UltraCanvasButton>("BusyStart", 0, 0, 0, 30.0f, "Start all");
        startBtn->onClick = [indicators]() { for (auto* b : *indicators) b->Start(); };
        DemoKit::Add(buttonRow, startBtn);

        auto stopBtn = std::make_shared<UltraCanvasButton>("BusyStop", 0, 0, 0, 30.0f, "Stop all");
        stopBtn->onClick = [indicators]() { for (auto* b : *indicators) b->Stop(); };
        DemoKit::Add(buttonRow, stopBtn);

        auto idleBtn = std::make_shared<UltraCanvasButton>("BusyIdle", 0, 0, 0, 30.0f,
                                                           "Show when stopped");
        idleBtn->onClick = [indicators]() {
            for (auto* b : *indicators) {
                BusyIndicatorStyle style = b->GetStyle();
                style.hideWhenStopped = !style.hideWhenStopped;
                b->SetStyle(style);
            }
        };
        DemoKit::Add(buttonRow, idleBtn);

        DemoKit::AddCaption(controlCard, "BusyInstructions",
            "Stop all halts every timer; a stopped indicator draws nothing unless "
            "hideWhenStopped is false (\"Show when stopped\" toggles it), and then rests "
            "where it stopped. Start all resumes from there.");

        return page;
    }

} // namespace UltraCanvas
