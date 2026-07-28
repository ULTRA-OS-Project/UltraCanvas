// Apps/DemoApp/UltraCanvasSWOTDiagramExamples.cpp
// SWOT diagram examples: corner badges, matrix, cards, diamond, rows, columns
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasSWOTDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace UltraCanvas {

    static const char* kSWOTQuadrantNames[4] = {
            "Strengths", "Weaknesses", "Opportunities", "Threats"};

    // Short phrases used by the "Add Random Item" button
    static std::string RandomSWOTItemText(SWOTQuadrant q) {
        static const char* strengths[] = {
                "Skilled workforce", "Solid cash reserves", "Modern tooling", "Fast delivery"};
        static const char* weaknesses[] = {
                "Slow release cycle", "High staff turnover", "Weak online presence", "Manual processes"};
        static const char* opportunities[] = {
                "New export markets", "Automation potential", "Industry consolidation", "Green energy demand"};
        static const char* threats[] = {
                "Currency volatility", "New market entrants", "Talent shortage", "Cyber attacks"};

        int pick = rand() % 4;
        switch (q) {
            case SWOTQuadrant::Strengths: return strengths[pick];
            case SWOTQuadrant::Weaknesses: return weaknesses[pick];
            case SWOTQuadrant::Opportunities: return opportunities[pick];
            case SWOTQuadrant::Threats:
            default: return threats[pick];
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSWOTDiagramExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // Quadrant/Sankey/Venn demos use - so absolutely-positioned children
        // are not clipped by the container's content area.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("swotMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("swotHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("swotMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("SWOT Diagram Infographic");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("swotSubtitle", 30, 52, 900, 30);
        subtitle->SetText("Classic four-panel SWOT analysis with six design presets, light/dark themes and interactive items");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("swotContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("swotLeftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;   // usable child width inside the 200px sidebar

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("swotControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto themeLabel = std::make_shared<UltraCanvasLabel>("swotThemeLabel", PAD, 55, SIDEBAR_W, 20);
        themeLabel->SetText("Theme");
        themeLabel->SetFontSize(10);
        themeLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(themeLabel);

        auto themeSelector = std::make_shared<UltraCanvasDropdown>("swotThemeSelector", PAD, 78, SIDEBAR_W, 28);
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(themeSelector);

        auto badgesLabel = std::make_shared<UltraCanvasLabel>("swotBadgesLabel", PAD, 118, SIDEBAR_W, 20);
        badgesLabel->SetText("Letter Badges");
        badgesLabel->SetFontSize(10);
        badgesLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(badgesLabel);

        auto badgesSelector = std::make_shared<UltraCanvasDropdown>("swotBadgesSelector", PAD, 141, SIDEBAR_W, 28);
        badgesSelector->AddItem("Shown", "on");
        badgesSelector->AddItem("Hidden", "off");
        badgesSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(badgesSelector);

        auto centerLabel = std::make_shared<UltraCanvasLabel>("swotCenterLabel", PAD, 181, SIDEBAR_W, 20);
        centerLabel->SetText("Center Element");
        centerLabel->SetFontSize(10);
        centerLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(centerLabel);

        auto centerSelector = std::make_shared<UltraCanvasDropdown>("swotCenterSelector", PAD, 204, SIDEBAR_W, 28);
        centerSelector->AddItem("Shown", "on");
        centerSelector->AddItem("Hidden", "off");
        centerSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(centerSelector);

        auto bulletsLabel = std::make_shared<UltraCanvasLabel>("swotBulletsLabel", PAD, 244, SIDEBAR_W, 20);
        bulletsLabel->SetText("Item Bullets");
        bulletsLabel->SetFontSize(10);
        bulletsLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(bulletsLabel);

        auto bulletsSelector = std::make_shared<UltraCanvasDropdown>("swotBulletsSelector", PAD, 267, SIDEBAR_W, 28);
        bulletsSelector->AddItem("Shown", "on");
        bulletsSelector->AddItem("Hidden", "off");
        bulletsSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(bulletsSelector);

        auto btnAddItem = std::make_shared<UltraCanvasButton>("swotBtnAddItem", PAD, 313, SIDEBAR_W, 32);
        btnAddItem->SetText("Add Random Item");
        btnAddItem->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnAddItem);

        auto btnRemove = std::make_shared<UltraCanvasButton>("swotBtnRemove", PAD, 350, SIDEBAR_W, 32);
        btnRemove->SetText("Remove Selected");
        btnRemove->SetBackgroundColor(Color(220, 53, 69, 255));
        leftSidebar->AddChild(btnRemove);

        auto btnReload = std::make_shared<UltraCanvasButton>("swotBtnReload", PAD, 387, SIDEBAR_W, 32);
        btnReload->SetText("Reload Sample Data");
        btnReload->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnReload);

        auto btnReset = std::make_shared<UltraCanvasButton>("swotBtnReset", PAD, 424, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        auto helpBox = std::make_shared<UltraCanvasLabel>("swotHelpBox", PAD, 475, SIDEBAR_W, 120);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Click an item to select\n"
                         "- Click empty space to\n"
                         "  clear the selection\n"
                         "- Hover for full text\n"
                         "- Double-click an item");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("swotTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR (stats + about) =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("swotRightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("swotStatsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("swotStatsPanel", PAD, 48, SIDEBAR_W, 200);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("swotAboutTitle", PAD, 268, SIDEBAR_W, 24);
        aboutTitle->SetText("ABOUT THIS DESIGN");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(aboutTitle);

        auto aboutPanel = std::make_shared<UltraCanvasLabel>("swotAboutPanel", PAD, 300, SIDEBAR_W, 220);
        aboutPanel->SetFontSize(9);
        aboutPanel->SetAlignment(TextAlignment::Left);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);
        rightSidebar->AddChild(aboutPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("swotStatusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - click items to select them, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        // Diagram geometry inside each tab
        const int DIAG_X = 20, DIAG_Y = 15, DIAG_W = 620, DIAG_H = 510;

        struct TabSpec {
            std::string tabTitle;
            SWOTDesign design;
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Corner Badges", SWOTDesign::CornerBadges,
                 "Corner Badges\n\nVivid filled panels with\nS/W/O/T letter badges\nat the outer corners and\na central SWOT circle -\nthe classic infographic\npresentation."},
                {"Matrix", SWOTDesign::Matrix,
                 "Classic Matrix\n\nContiguous 2x2 grid,\nthe textbook SWOT form.\n\nAxis captions (helpful/\nharmful, internal/\nexternal) are enabled\non this tab."},
                {"Cards", SWOTDesign::Cards,
                 "Cards\n\nFour separated rounded\ncards with colored\nheader bars - reads as\nfour independent tiles."},
                {"Diamond", SWOTDesign::CenterDiamond,
                 "Center Diamond\n\nA central rotated square\nof four letter triangles\nwith clean text blocks\nat the surrounding\ncorners."},
                {"Rows", SWOTDesign::Rows,
                 "Rows\n\nFour stacked horizontal\nbands with big letter\nblocks - the list-style\npresentation that suits\nlonger item texts."},
                {"Columns", SWOTDesign::Columns,
                 "Columns\n\nFour side-by-side\ncolumns with header\nchips - good for\nportrait layouts."}};

        std::vector<std::shared_ptr<UltraCanvasSWOTDiagram>> allDiagrams;
        for (const auto& spec : tabSpecs) {
            auto diagram = CreateSWOTDiagramElement(
                    "swotDiagram" + std::to_string(allDiagrams.size()),
                    DIAG_X, DIAG_Y, DIAG_W, DIAG_H, spec.design);
            diagram->SetTitle("SWOT Analysis");
            diagram->LoadSampleData();
            if (spec.design == SWOTDesign::Matrix) diagram->SetShowAxisCaptions(true);

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    diagram->GetIdentifier() + "Tab", 0, 0, 660, 542);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(diagram);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allDiagrams.push_back(diagram);
        }

        // Returns the diagram belonging to the currently active tab
        auto activeDiagram = [tabbedContainer, allDiagrams]() -> std::shared_ptr<UltraCanvasSWOTDiagram> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allDiagrams.size())) return nullptr;
            return allDiagrams[index];
        };

        // Refreshes the statistics panel from the active diagram
        auto updateStats = [activeDiagram, statsPanel]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            std::ostringstream oss;
            oss << "Total Items: " << diagram->CountAllItems() << "\n\n";
            for (int q = 0; q < 4; ++q) {
                oss << kSWOTQuadrantNames[q] << ": "
                    << diagram->CountItems(static_cast<SWOTQuadrant>(q)) << "\n";
            }
            auto sel = diagram->GetSelectedItem();
            oss << "\nSelected: ";
            if (sel.IsValid()) {
                oss << kSWOTQuadrantNames[sel.quadrant] << " #" << (sel.item + 1);
            } else {
                oss << "none";
            }
            statsPanel->SetText(oss.str());
        };

        // Refreshes the "about" panel from the active tab
        std::vector<std::string> aboutTexts;
        for (const auto& spec : tabSpecs) aboutTexts.push_back(spec.about);
        auto updateAbout = [aboutTexts, tabbedContainer, aboutPanel]() {
            int index = tabbedContainer->GetActiveTab();
            if (index >= 0 && index < static_cast<int>(aboutTexts.size())) {
                aboutPanel->SetText(aboutTexts[index]);
            }
        };

        // ===== DIAGRAM CALLBACKS =====
        for (auto& diagram : allDiagrams) {
            diagram->onItemSelect = [statusBar, updateStats](SWOTQuadrant q, size_t, const SWOTItem& item) {
                statusBar->SetText("Selected [" +
                        std::string(kSWOTQuadrantNames[static_cast<int>(q)]) + "] " + item.text);
                updateStats();
            };
            diagram->onItemHover = [statusBar](SWOTQuadrant q, size_t, const SWOTItem& item) {
                statusBar->SetText("Hovering [" +
                        std::string(kSWOTQuadrantNames[static_cast<int>(q)]) + "] " + item.text);
            };
            diagram->onItemDoubleClick = [statusBar](SWOTQuadrant q, size_t, const SWOTItem& item) {
                statusBar->SetText("Double-clicked [" +
                        std::string(kSWOTQuadrantNames[static_cast<int>(q)]) + "] " + item.text);
            };
            diagram->onSelectionChange = updateStats;
        }

        // ===== CONTROL CALLBACKS =====
        tabbedContainer->onTabChange = [updateStats, updateAbout](int, int) {
            updateStats();
            updateAbout();
        };

        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            bool dark = (item.value == "dark");
            for (auto& diagram : allDiagrams) diagram->SetDarkTheme(dark);
        };

        badgesSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            bool show = (item.value == "on");
            for (auto& diagram : allDiagrams) diagram->SetShowBadges(show);
        };

        centerSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            bool show = (item.value == "on");
            for (auto& diagram : allDiagrams) diagram->SetShowCenterElement(show);
        };

        bulletsSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            bool show = (item.value == "on");
            for (auto& diagram : allDiagrams) diagram->SetShowItemBullets(show);
        };

        // Adds a random item, cycling through the quadrants
        auto nextQuadrant = std::make_shared<int>(0);
        btnAddItem->onClick = [activeDiagram, nextQuadrant, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            SWOTQuadrant q = static_cast<SWOTQuadrant>(*nextQuadrant % 4);
            *nextQuadrant = (*nextQuadrant + 1) % 4;

            std::string text = RandomSWOTItemText(q);
            diagram->AddItem(q, text);
            statusBar->SetText("Added [" +
                    std::string(kSWOTQuadrantNames[static_cast<int>(q)]) + "] " + text);
            updateStats();
        };

        btnRemove->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;

            auto sel = diagram->GetSelectedItem();
            if (!sel.IsValid()) {
                statusBar->SetText("Nothing selected - click an item first");
                return;
            }
            diagram->RemoveItem(static_cast<SWOTQuadrant>(sel.quadrant),
                                static_cast<size_t>(sel.item));
            statusBar->SetText("Removed the selected item");
            updateStats();
        };

        btnReload->onClick = [activeDiagram, statusBar, updateStats]() {
            auto diagram = activeDiagram();
            if (!diagram) return;
            diagram->LoadSampleData();
            statusBar->SetText("Sample data reloaded for the active diagram");
            updateStats();
        };

        btnReset->onClick = [themeSelector, badgesSelector, centerSelector, bulletsSelector,
                             allDiagrams, statusBar, updateStats]() {
            themeSelector->SetSelectedIndex(0);
            badgesSelector->SetSelectedIndex(0);
            centerSelector->SetSelectedIndex(0);
            bulletsSelector->SetSelectedIndex(0);

            for (auto& diagram : allDiagrams) {
                diagram->SetDarkTheme(false);
                diagram->SetShowBadges(true);
                diagram->SetShowCenterElement(true);
                diagram->SetShowItemBullets(true);
                diagram->ClearSelection();
                diagram->LoadSampleData();
            }

            statusBar->SetText("View reset - all diagrams restored to their defaults");
            updateStats();
        };

        // Initial panel contents
        updateStats();
        updateAbout();

        return mainContainer;
    }

} // namespace UltraCanvas
