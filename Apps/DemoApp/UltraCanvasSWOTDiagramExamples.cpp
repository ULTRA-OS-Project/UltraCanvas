// Apps/DemoApp/UltraCanvasSWOTDiagramExamples.cpp
// SWOT diagram examples: corner badges, matrix, cards, diamond, rows, columns
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): controls, tabs
//                  and statistics in cards that wrap, every text sized to fit,
//                  buttons as wide as their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
        const int SIDEBAR_W = 170;
        // A sidebar card's width before it wraps under the tabs: room for the
        // 170 px controls and the card's padding.
        const float SIDEBAR_CARD_W = SIDEBAR_W + 2 * DemoKit::kCardPadding + 2;

        auto mainContainer = DemoKit::CreatePage("swotMainContainer", "SWOT Diagram Infographic",
                "Classic four-panel SWOT analysis with six design presets, light/dark themes and interactive items");

        // Controls | diagram tabs and status | statistics. The sidebars keep
        // their width; on a narrow window the cards wrap under each other.
        auto contentContainer = DemoKit::AddRow(mainContainer, "swotContent");

        // The small upper-case heading of a panel.
        auto makeSectionTitle = [](const std::string& id, const std::string& text) {
            auto title = DemoKit::CreateText(id, text);
            title->SetFontSize(11);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(73, 80, 87, 255));
            return title;
        };

        // ===== LEFT SIDEBAR (controls) =====
        // As wide as its widest button text, at least the old sidebar width.
        auto leftSidebar = DemoKit::AddCard(contentContainer, "swotLeftSidebar", "", "", SIDEBAR_CARD_W);
        leftSidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                               .SetFlexBasis(CSSLayout::Dimension::Auto());
        CSSLayout::BoxConstraints sidebarLimits;
        sidebarLimits.minWidth = CSSLayout::Dimension::Px(SIDEBAR_CARD_W);
        leftSidebar->boxConstraints = sidebarLimits;

        DemoKit::Add(leftSidebar, makeSectionTitle("swotControlsTitle", "CONTROLS"));

        auto addDropdown = [&](const std::string& id, const std::string& labelId,
                               const std::string& caption) {
            auto label = DemoKit::CreateText(labelId, caption);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
            DemoKit::Add(leftSidebar, label);

            auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, SIDEBAR_W, 28);
            DemoKit::AddStretched(leftSidebar, dropdown);
            return dropdown;
        };

        auto themeSelector = addDropdown("swotThemeSelector", "swotThemeLabel", "Theme");
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto badgesSelector = addDropdown("swotBadgesSelector", "swotBadgesLabel", "Letter Badges");
        badgesSelector->AddItem("Shown", "on");
        badgesSelector->AddItem("Hidden", "off");
        badgesSelector->SetSelectedIndex(0);

        auto centerSelector = addDropdown("swotCenterSelector", "swotCenterLabel", "Center Element");
        centerSelector->AddItem("Shown", "on");
        centerSelector->AddItem("Hidden", "off");
        centerSelector->SetSelectedIndex(0);

        auto bulletsSelector = addDropdown("swotBulletsSelector", "swotBulletsLabel", "Item Bullets");
        bulletsSelector->AddItem("Shown", "on");
        bulletsSelector->AddItem("Hidden", "off");
        bulletsSelector->SetSelectedIndex(0);

        // The action buttons: across the sidebar, as tall as before; a longer
        // text makes the sidebar wider rather than being cut.
        auto addButton = [&](const std::string& id, const std::string& text, const Color& color) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
            button->SetBackgroundColor(color);
            DemoKit::Add(leftSidebar, button);
            return button;
        };

        auto btnAddItem = addButton("swotBtnAddItem", "Add Random Item", Color(13, 110, 253, 255));
        auto btnRemove = addButton("swotBtnRemove", "Remove Selected", Color(220, 53, 69, 255));
        auto btnReload = addButton("swotBtnReload", "Reload Sample Data", Color(25, 135, 84, 255));
        auto btnReset = addButton("swotBtnReset", "Reset View", Color(108, 117, 125, 255));

        // Short lines, so the tips do not widen the sidebar; they wrap if a
        // translation is longer.
        auto helpBox = DemoKit::CreateText("swotHelpBox",
                "INTERACTION TIPS\n\n"
                "- Click an item to select\n"
                "- Click empty space to\n"
                "  clear the selection\n"
                "- Hover for full text\n"
                "- Double-click an item");
        helpBox->SetFontSize(9);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        DemoKit::Add(leftSidebar, helpBox);

        // ===== TABS =====
        // The diagrams have a fixed size; the card is at least as wide as the
        // tabs and takes the width the sidebars leave.
        auto centerCard = DemoKit::AddCard(contentContainer, "swotCenter", "", "",
                                           660 + 2 * DemoKit::kCardPadding + 2);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("swotTabs", 0, 0, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(centerCard, tabbedContainer);

        auto statusBar = DemoKit::CreateText("swotStatusBar",
                "Ready - click items to select them, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        DemoKit::Add(centerCard, statusBar);

        // ===== RIGHT SIDEBAR (stats + about) =====
        auto rightSidebar = DemoKit::AddCard(contentContainer, "swotRightSidebar", "", "", SIDEBAR_CARD_W);
        rightSidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        DemoKit::Add(rightSidebar, makeSectionTitle("swotStatsTitle", "STATISTICS"));

        auto statsPanel = DemoKit::CreateText("swotStatsPanel", "");
        statsPanel->SetFontSize(10);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        DemoKit::Add(rightSidebar, statsPanel);

        DemoKit::Add(rightSidebar, makeSectionTitle("swotAboutTitle", "ABOUT THIS DESIGN"));

        auto aboutPanel = DemoKit::CreateText("swotAboutPanel", "");
        aboutPanel->SetFontSize(9);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);
        DemoKit::Add(rightSidebar, aboutPanel);

        // Diagram geometry inside each tab
        const int DIAG_X = 20, DIAG_Y = 15, DIAG_W = 620, DIAG_H = 510;

        struct TabSpec {
            std::string tabTitle;
            SWOTDesign design;
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Corner Badges", SWOTDesign::CornerBadges,
                 "Corner Badges\n\nVivid filled panels with S/W/O/T letter badges at the outer corners "
                 "and a central SWOT circle - the classic infographic presentation."},
                {"Matrix", SWOTDesign::Matrix,
                 "Classic Matrix\n\nContiguous 2x2 grid, the textbook SWOT form.\n\n"
                 "Axis captions (helpful/harmful, internal/external) are enabled on this tab."},
                {"Cards", SWOTDesign::Cards,
                 "Cards\n\nFour separated rounded cards with colored header bars - reads as "
                 "four independent tiles."},
                {"Diamond", SWOTDesign::CenterDiamond,
                 "Center Diamond\n\nA central rotated square of four letter triangles with clean "
                 "text blocks at the surrounding corners."},
                {"Rows", SWOTDesign::Rows,
                 "Rows\n\nFour stacked horizontal bands with big letter blocks - the list-style "
                 "presentation that suits longer item texts."},
                {"Columns", SWOTDesign::Columns,
                 "Columns\n\nFour side-by-side columns with header chips - good for portrait "
                 "layouts."}};

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
