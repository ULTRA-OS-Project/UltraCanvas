// Apps/DemoApp/UltraCanvasSequenceDiagramExamples.cpp
// Sequence diagram examples: one tab per sample interaction, with live controls
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // SWOT/Venn/Timeline demos use - so absolutely-positioned children are
        // not clipped by the container's content area.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("seqMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("seqHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("seqMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("UML Sequence Diagram");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("seqSubtitle", 30, 52, 1000, 30);
        subtitle->SetText("Lifelines, execution bars, combined fragments and notes - "
                          "drag to pan, wheel to zoom, click to inspect");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("seqContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("seqSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("seqControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto themeLabel = std::make_shared<UltraCanvasLabel>("seqThemeLabel", PAD, 55, SIDEBAR_W, 20);
        themeLabel->SetText("Theme");
        themeLabel->SetFontSize(10);
        themeLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(themeLabel);

        auto themeSelector = std::make_shared<UltraCanvasDropdown>("seqThemeSelector", PAD, 78, SIDEBAR_W, 28);
        themeSelector->AddItem("Default", "default");
        themeSelector->AddItem("Classic", "classic");
        themeSelector->AddItem("Professional", "professional");
        themeSelector->AddItem("Colorful", "colorful");
        themeSelector->AddItem("Minimal", "minimal");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(themeSelector);

        auto numberingLabel = std::make_shared<UltraCanvasLabel>("seqNumberingLabel", PAD, 118, SIDEBAR_W, 20);
        numberingLabel->SetText("Message Numbering");
        numberingLabel->SetFontSize(10);
        numberingLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(numberingLabel);

        auto numberingSelector = std::make_shared<UltraCanvasDropdown>("seqNumberingSelector", PAD, 141, SIDEBAR_W, 28);
        numberingSelector->AddItem("Hidden", "hidden");
        numberingSelector->AddItem("Sequential (1, 2, 3)", "sequential");
        numberingSelector->AddItem("Hierarchical (1.1.2)", "hierarchical");
        numberingSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(numberingSelector);

        auto btnFrame = std::make_shared<UltraCanvasButton>("seqBtnFrame", PAD, 185, SIDEBAR_W, 32);
        btnFrame->SetText("Toggle sd Frame");
        btnFrame->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnFrame);

        auto btnFeet = std::make_shared<UltraCanvasButton>("seqBtnFeet", PAD, 222, SIDEBAR_W, 32);
        btnFeet->SetText("Toggle Foot Boxes");
        btnFeet->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnFeet);

        auto btnFit = std::make_shared<UltraCanvasButton>("seqBtnFit", PAD, 259, SIDEBAR_W, 32);
        btnFit->SetText("Fit View");
        btnFit->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnFit);

        auto btnReset = std::make_shared<UltraCanvasButton>("seqBtnReset", PAD, 296, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        auto btnExport = std::make_shared<UltraCanvasButton>("seqBtnExport", PAD, 333, SIDEBAR_W, 32);
        btnExport->SetText("Print PlantUML");
        btnExport->SetBackgroundColor(Color(111, 66, 193, 255));
        leftSidebar->AddChild(btnExport);

        auto helpBox = std::make_shared<UltraCanvasLabel>("seqHelpBox", PAD, 385, SIDEBAR_W, 130);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Wheel zooms at cursor\n"
                         "- Drag empty canvas to pan\n"
                         "- Click arrows or heads\n"
                         "- Event Flow tab shows the\n"
                         "  framework's own pipeline");
        helpBox->SetFontSize(9);
        helpBox->SetTextColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(helpBox);

        // ===== TABS (one sample interaction each) =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("seqTabs", 240, 20, 880, 600);

        struct SampleTab {
            const char* tabId;
            const char* title;
            UltraCanvasSequenceModel model;
        };
        std::vector<SampleTab> samples;
        samples.push_back({"seqCardGame", "Card Game", SequenceDiagramSamples::CardGame()});
        samples.push_back({"seqLogin", "User Login", SequenceDiagramSamples::UserLogin()});
        samples.push_back({"seqAuth", "Authentication", SequenceDiagramSamples::Authentication()});
        samples.push_back({"seqShop", "Online Shop", SequenceDiagramSamples::OnlineShop()});
        samples.push_back({"seqLifecycle", "Lifecycle", SequenceDiagramSamples::ObjectLifecycle()});
        samples.push_back({"seqEventFlow", "Event Flow", SequenceDiagramSamples::UltraCanvasEventFlow()});

        auto statusBar = std::make_shared<UltraCanvasLabel>("seqStatus", 240, 630, 880, 24);
        statusBar->SetText("Click a message or lifeline to inspect it");
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
                statusBar->SetText("Message " + std::to_string(index) + ": \"" +
                                   message.label + "\"");
            };
            diagram->onLifelineClick = [statusBar, diagram](const std::string& id) {
                const SequenceLifeline* lifeline = diagram->Model().GetLifeline(id);
                if (lifeline) statusBar->SetText("Lifeline: " + lifeline->name);
            };
            diagram->onFragmentClick = [statusBar, diagram](const std::string& id) {
                const SequenceFragment* fragment = diagram->Model().GetFragment(id);
                if (fragment) {
                    statusBar->SetText(std::string("Fragment: ") + fragment->Keyword());
                }
            };

            tabbedContainer->AddTab(sample.title, tab);
        }

        // The framework's own pipeline reads best with call numbering on.
        allDiagrams.back()->SetNumbering(SequenceNumbering::Hierarchical);
        allDiagrams.back()->SetShowFrame(true);

        contentContainer->AddChild(tabbedContainer);
        contentContainer->AddChild(statusBar);

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
            statusBar->SetText("PlantUML for all tabs printed to the console");
        };

        return mainContainer;
    }

} // namespace UltraCanvas
