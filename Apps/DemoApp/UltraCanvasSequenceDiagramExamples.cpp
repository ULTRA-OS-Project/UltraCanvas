// Apps/DemoApp/UltraCanvasSequenceDiagramExamples.cpp
// Sequence diagram examples: one tab per sample interaction, with live controls
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls
//                  and the tabs in cards side by side, every label and button
//                  sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasSequenceDiagram.h"
#include "Plugins/Diagrams/UltraCanvasSequenceTextExport.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSequenceDiagramExamples() {
        auto mainContainer = DemoKit::CreatePage("seqMainContainer", Tr("UML Sequence Diagram"),
                Tr("Lifelines, execution bars, combined fragments and notes - "
                "drag to pan, wheel to zoom, click to inspect"));

        // The controls beside the tabs; under them when the window is narrow.
        auto contentContainer = DemoKit::AddRow(mainContainer, "seqContent");

        // ===== LEFT SIDEBAR (controls) =====
        const int SIDEBAR_W = 170;
        auto leftSidebar = DemoKit::AddCard(contentContainer, "seqSidebar", "", "",
                                            SIDEBAR_W + 2 * DemoKit::kCardPadding + 2);
        leftSidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        auto controlsTitle = DemoKit::AddText(leftSidebar, "seqControlsTitle", Tr("CONTROLS"));
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));

        // A bold caption over a dropdown that takes the sidebar's width.
        auto addCaption = [&](const std::string& id, const std::string& text) {
            auto label = DemoKit::AddText(leftSidebar, id, text);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
        };
        auto addDropdown = [&](const std::string& id) {
            auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, SIDEBAR_W, 28);
            DemoKit::AddStretched(leftSidebar, dropdown);
            return dropdown;
        };
        // A button as wide as its text, at least the sidebar's width.
        auto addButton = [&](const std::string& id, const std::string& text, const Color& color) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(SIDEBAR_W);
            button->boxConstraints = limits;
            button->SetBackgroundColor(color);
            DemoKit::Add(leftSidebar, button);
            return button;
        };

        addCaption("seqThemeLabel", Tr("Theme"));
        auto themeSelector = addDropdown("seqThemeSelector");
        themeSelector->AddItem(Tr("Default"), "default");
        themeSelector->AddItem(Tr("Classic"), "classic");
        themeSelector->AddItem(Tr("Professional"), "professional");
        themeSelector->AddItem(Tr("Colorful"), "colorful");
        themeSelector->AddItem(Tr("Minimal"), "minimal");
        themeSelector->AddItem(Tr("Dark"), "dark");
        themeSelector->SetSelectedIndex(0);

        addCaption("seqNumberingLabel", Tr("Message Numbering"));
        auto numberingSelector = addDropdown("seqNumberingSelector");
        numberingSelector->AddItem(Tr("Hidden"), "hidden");
        numberingSelector->AddItem(Tr("Sequential (1, 2, 3)"), "sequential");
        numberingSelector->AddItem(Tr("Hierarchical (1.1.2)"), "hierarchical");
        numberingSelector->SetSelectedIndex(0);

        auto btnFrame = addButton("seqBtnFrame", Tr("Toggle sd Frame"), Color(13, 110, 253, 255));
        auto btnFeet = addButton("seqBtnFeet", Tr("Toggle Foot Boxes"), Color(13, 110, 253, 255));
        auto btnFit = addButton("seqBtnFit", Tr("Fit View"), Color(25, 135, 84, 255));
        auto btnReset = addButton("seqBtnReset", Tr("Reset View"), Color(108, 117, 125, 255));
        auto btnExport = addButton("seqBtnExport", Tr("Print PlantUML"), Color(111, 66, 193, 255));

        auto helpBox = DemoKit::AddCaption(leftSidebar, "seqHelpBox",
                         Tr("INTERACTION TIPS\n\n"
                         "- Wheel zooms at cursor\n"
                         "- Drag empty canvas to pan\n"
                         "- Click arrows or heads\n"
                         "- Event Flow tab shows the framework's own pipeline"));
        helpBox->SetFontSize(9);
        helpBox->SetTextColor(Color(108, 117, 125, 255));

        // ===== TABS (one sample interaction each) =====
        // The diagrams have a fixed size; the card is at least as wide as the
        // tabs and takes the width the controls leave.
        auto centerCard = DemoKit::AddCard(contentContainer, "seqCenter", "", "",
                                           880 + 2 * DemoKit::kCardPadding + 2);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("seqTabs", 0, 0, 880, 600);

        struct SampleTab {
            const char* tabId;
            std::string title;
            UltraCanvasSequenceModel model;
        };
        std::vector<SampleTab> samples;
        samples.push_back({"seqCardGame", Tr("Card Game"), SequenceDiagramSamples::CardGame()});
        samples.push_back({"seqLogin", Tr("User Login"), SequenceDiagramSamples::UserLogin()});
        samples.push_back({"seqAuth", Tr("Authentication"), SequenceDiagramSamples::Authentication()});
        samples.push_back({"seqShop", Tr("Online Shop"), SequenceDiagramSamples::OnlineShop()});
        samples.push_back({"seqLifecycle", Tr("Lifecycle"), SequenceDiagramSamples::ObjectLifecycle()});
        samples.push_back({"seqEventFlow", Tr("Event Flow"), SequenceDiagramSamples::UltraCanvasEventFlow()});

        auto statusBar = DemoKit::CreateText("seqStatus", Tr("Click a message or lifeline to inspect it"));
        statusBar->SetFontSize(11);
        statusBar->SetTextColor(Color(90, 98, 106, 255));

        std::vector<std::shared_ptr<UltraCanvasSequenceDiagram>> allDiagrams;
        for (auto& sample : samples) {
            auto tab = std::make_shared<UltraCanvasContainer>(
                std::string(sample.tabId) + "Tab", 0, 0, 876, 560);
            tab->SetBackgroundColor(Color(252, 252, 253, 255));

            auto diagram = CreateSequenceDiagramWithModel(sample.tabId, 0, 0, 876, 560,
                                                          sample.model);
            diagram->FitView();
            tab->AddChild(diagram);
            allDiagrams.push_back(diagram);

            diagram->onMessageClick = [statusBar](size_t index, const SequenceMessage& message) {
                statusBar->SetText(TrF("Message {0}: \"{1}\"", index, message.label));
            };
            diagram->onLifelineClick = [statusBar, diagram = diagram.get()](const std::string& id) {
                const SequenceLifeline* lifeline = diagram->Model().GetLifeline(id);
                if (lifeline) statusBar->SetText(TrF("Lifeline: {0}", lifeline->name));
            };
            diagram->onFragmentClick = [statusBar, diagram = diagram.get()](const std::string& id) {
                const SequenceFragment* fragment = diagram->Model().GetFragment(id);
                if (fragment) {
                    statusBar->SetText(TrF("Fragment: {0}", fragment->Keyword()));
                }
            };

            tabbedContainer->AddTab(sample.title, tab);
        }

        // The framework's own pipeline reads best with call numbering on.
        allDiagrams.back()->SetNumbering(SequenceNumbering::Hierarchical);
        allDiagrams.back()->SetShowFrame(true);

        DemoKit::AddFixed(centerCard, tabbedContainer);
        DemoKit::Add(centerCard, statusBar);

        // ===== CONTROL WIRING =====
        themeSelector->onSelectionChanged =
            [allDiagrams](int, const DropdownItem& item) {
                SequenceDiagramTheme theme = SequenceDiagramTheme::Default;
                if (item.value == "classic") theme = SequenceDiagramTheme::Classic;
                else if (item.value == "professional") theme = SequenceDiagramTheme::Professional;
                else if (item.value == "colorful") theme = SequenceDiagramTheme::Colorful;
                else if (item.value == "minimal") theme = SequenceDiagramTheme::Minimal;
                else if (item.value == "dark") theme = SequenceDiagramTheme::Dark;
                for (auto& diagram : allDiagrams) diagram->SetTheme(theme);
            };

        numberingSelector->onSelectionChanged =
            [allDiagrams](int, const DropdownItem& item) {
                SequenceNumbering numbering = SequenceNumbering::Hidden;
                if (item.value == "sequential") numbering = SequenceNumbering::Sequential;
                else if (item.value == "hierarchical") numbering = SequenceNumbering::Hierarchical;
                for (auto& diagram : allDiagrams) diagram->SetNumbering(numbering);
            };

        btnFrame->onClick = [allDiagrams]() {
            for (auto& diagram : allDiagrams) diagram->SetShowFrame(!diagram->GetShowFrame());
        };
        btnFeet->onClick = [allDiagrams]() {
            for (auto& diagram : allDiagrams) {
                diagram->SetShowFootBoxes(!diagram->GetShowFootBoxes());
            }
        };
        btnFit->onClick = [allDiagrams]() {
            for (auto& diagram : allDiagrams) diagram->FitView();
        };
        btnReset->onClick = [allDiagrams]() {
            for (auto& diagram : allDiagrams) diagram->ResetView();
        };
        btnExport->onClick = [allDiagrams, statusBar]() {
            // The demo has no file dialog wired here; the console output is
            // paste-ready for plantuml.com or any Markdown renderer.
            for (auto& diagram : allDiagrams) {
                std::printf("%s\n", SequenceTextExport::ToPlantUML(diagram->Model()).c_str());
            }
            statusBar->SetText(Tr("PlantUML for all tabs printed to the console"));
        };

        return mainContainer;
    }

} // namespace UltraCanvas
