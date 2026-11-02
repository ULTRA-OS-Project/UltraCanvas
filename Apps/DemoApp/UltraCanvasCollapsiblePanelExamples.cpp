// Apps/DemoApp/UltraCanvasCollapsiblePanelExamples.cpp
// Demonstration of UltraCanvasCollapsiblePanel: a panel that folds to its
// header strip, header buttons that stay buttons, and a bottom-docked panel
// whose top edge resizes it.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasCollapsiblePanel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCollapsiblePanelExamples() {
        auto page = DemoKit::CreatePage("CollapsiblePanelExamples", "Collapsible Panel Examples",
                "A header strip with a chevron, a title, a detail text and real buttons over a body; "
                "collapsed, only the header shows.");

        // ===== 1. FOLD / UNFOLD =====
        auto foldCard = DemoKit::AddCard(page, "CpL1", "Fold and unfold",
                                         "Click the header strip. The body is as tall as its content when no expanded height is set.");
        auto fold = CreateCollapsiblePanel("CpFold", "Problems");
        fold->SetDetail("3 warnings");
        for (int i = 0; i < 3; ++i) {
            auto line = CreateLabel("CpFoldLine" + std::to_string(i), 0, 0, 0, 24,
                                    i == 0 ? "  main.cpp:12  unused variable 'count'"
                                           : (i == 1 ? "  main.cpp:40  comparison of integers of different signs"
                                                     : "  app.cpp:7    implicit conversion loses precision"));
            fold->GetBody()->AddChild(line);
        }
        fold->GetBody()->SetPadding(6);
        auto foldOut = DemoKit::CreateText("CpFoldOut", "Expanded");
        foldOut->SetBackgroundColor(Color(240, 240, 240));
        foldOut->SetPadding(3);
        fold->onExpandedChanged = [foldOut](bool e) { foldOut->SetText(e ? "Expanded" : "Collapsed"); };
        DemoKit::Add(foldCard, fold);
        DemoKit::Add(foldCard, foldOut);

        // ===== 2. HEADER BUTTONS =====
        auto buttonsCard = DemoKit::AddCard(page, "CpL2", "Header buttons",
                                            "Real buttons at the header's right: a press on them is theirs, not a fold.");
        auto queue = CreateCollapsiblePanel("CpQueue", "Downloads");
        queue->SetDetail("2 running, 1 finished");
        auto queueBody = queue->GetBody();
        queueBody->SetPadding(6);
        queueBody->layout.SetFlexGap(4);
        for (const char* text : {"runtime-0.9.410-linux-x86_64.7z   104.9 MB   62 %",
                                 "ultravideo-0.2.1-linux-x86_64.7z    2.9 MB   waiting",
                                 "videofx-0.12.0-linux-x86_64.7z      7.6 MB   done"}) {
            queueBody->AddChild(CreateLabel(std::string("CpQ") + std::to_string(queueBody->GetChildCount()), 0, 0, 0, 22, text));
        }
        auto queueOut = DemoKit::CreateText("CpQueueOut", "");
        queueOut->SetBackgroundColor(Color(240, 240, 240));
        queueOut->SetPadding(3);
        queue->AddHeaderButton("CpQueuePause", "Pause all", [queueOut]() { queueOut->SetText("Pause all pressed - the panel did not fold"); });
        queue->AddHeaderButton("CpQueueClear", "Clear finished", [queueOut, queueBody]() {
            if (queueBody->GetChildCount() > 2) queueBody->RemoveChild(queueBody->GetChildren().back());
            queueOut->SetText("Clear finished pressed");
        });
        DemoKit::Add(buttonsCard, queue);
        DemoKit::Add(buttonsCard, queueOut);

        // ===== 3. BOTTOM DOCK, RESIZABLE =====
        auto dockCard = DemoKit::AddCard(page, "CpL3", "Docked at the bottom, resizable",
                                         "A column with a page above and the panel below at its expanded height; drag the panel's top edge.");
        auto dock = CreateContainer("CpDock", 0, 0, 0, 320);
        dock->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        dock->SetBorders(1.0f, Color(224, 227, 235, 255), 6.0f);
        auto above = CreateLabel("CpAbove", 0, 0, 0, 0, "The window's page: whatever the application shows. It takes the room the panel leaves.");
        above->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        above->SetWrap(TextWrap::WrapWord);
        above->SetPadding(12);
        above->layoutItem.SetFlexGrow(1);
        above->SetBackgroundColor(Color(250, 250, 252, 255));
        dock->AddChild(above);
        auto bottom = CreateCollapsiblePanel("CpBottom", "Log");
        bottom->SetDetail("drag the top edge");
        bottom->SetExpandedHeight(140);
        bottom->GetStyle().cornerRadius = 0.0f;
        bottom->SetStyle(bottom->GetStyle());
        bottom->GetBody()->SetPadding(6);
        for (int i = 0; i < 4; ++i) {
            bottom->GetBody()->AddChild(CreateLabel("CpLog" + std::to_string(i), 0, 0, 0, 20,
                                                    "14:5" + std::to_string(i) + "  catalog fetched, sequence 42"));
        }
        auto dockOut = DemoKit::CreateText("CpDockOut", "Expanded height: 140");
        dockOut->SetBackgroundColor(Color(240, 240, 240));
        dockOut->SetPadding(3);
        bottom->onExpandedHeightChanged = [dockOut](float h) { dockOut->SetText("Expanded height: " + std::to_string(static_cast<int>(h))); };
        dock->AddChild(bottom);
        DemoKit::AddStretched(dockCard, dock);
        DemoKit::Add(dockCard, dockOut);

        DemoKit::AddNotes(page, "CpNotes", "Implementation notes", {
            "UltraCanvasCollapsiblePanel is a container: a header row (label, detail label, an actions row) over a body container.",
            "Collapsed, the panel is headerHeight tall; expanded, the expanded height or as tall as the body.",
            "SetCollapsible(false) makes the header a plain strip; SetResizable controls the grip along the top edge.",
            "onExpandedChanged and onExpandedHeightChanged let a window remember the state.",
            "UltraCanvasGroupBox::SetCollapsible folds a framed section under its caption; this is a dock with buttons and a resizable height.",
        });
        return page;
    }

} // namespace UltraCanvas
