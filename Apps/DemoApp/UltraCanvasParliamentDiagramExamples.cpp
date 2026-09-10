// Apps/DemoApp/UltraCanvasParliamentDiagramExamples.cpp
// Parliament diagram examples: hemicycle, horseshoe, circle, Westminster
// benches, seat grid and an interactive coalition builder
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {
        std::string PercentText(int seats, int total) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f%%", total > 0 ? 100.0 * seats / total : 0.0);
            return buf;
        }

        // Text for the right-hand stats panel: one party, or the chamber summary
        std::string ChamberSummary(const UltraCanvasParliamentDiagram& diagram) {
            int total = diagram.GetTotalSeats();
            int gov = diagram.GetGovernmentSeats();
            std::string text = "Total seats: " + std::to_string(total) +
                               "\nMajority: " + std::to_string(diagram.GetMajorityThreshold()) +
                               "\nParties: " + std::to_string(diagram.GetPartyCount());
            if (gov > 0) {
                text += "\n\nGovernment: " + std::to_string(gov) + " (" + PercentText(gov, total) + ")";
                text += diagram.GovernmentHasMajority() ? "\nHas a majority" : "\nShort of a majority";
            }
            text += "\n\nHover a seat or legend\nentry for its party.";
            return text;
        }

        std::string PartySummary(const UltraCanvasParliamentDiagram& diagram, const ParliamentParty& party) {
            int total = diagram.GetTotalSeats();
            std::string text = party.name;
            if (!party.abbreviation.empty() && party.abbreviation != party.name) {
                text += "\n(" + party.abbreviation + ")";
            }
            text += "\n\nSeats: " + std::to_string(party.seats) +
                    "\nShare: " + PercentText(party.seats, total);
            text += party.government ? "\nGovernment side" : "\nOpposition / other";
            if (party.vacant) text += "\nVacant seats";
            return text;
        }

        std::string CoalitionSummary(const UltraCanvasParliamentDiagram& diagram) {
            int total = diagram.GetTotalSeats();
            int gov = diagram.GetGovernmentSeats();
            int needed = diagram.GetMajorityThreshold();
            std::string members;
            for (const auto& p : diagram.GetParties()) {
                if (!p.government) continue;
                if (!members.empty()) members += " + ";
                members += p.LegendName();
            }
            if (members.empty()) {
                return "Coalition builder: click parties to add them to a coalition. " +
                       std::to_string(needed) + " of " + std::to_string(total) + " seats are needed.";
            }
            std::string text = "Coalition: " + members + " = " + std::to_string(gov) + " seats. ";
            if (gov >= needed) {
                text += "Majority reached (" + std::to_string(gov - needed + 1) + " seats to spare).";
            } else {
                text += std::to_string(needed - gov) + " more seats needed for a majority of " +
                        std::to_string(needed) + ".";
            }
            return text;
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateParliamentDiagramExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // SWOT/Venn/Sankey demos use - so absolutely-positioned children are
        // not clipped by the container's content area.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("parlMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("parlHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("parlMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Parliament Diagram");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("parlSubtitle", 30, 52, 1000, 30);
        subtitle->SetText("Seat charts of legislatures: hemicycle, horseshoe, circle, Westminster benches and grid layouts with a majority marker, legend and coalition building");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("parlContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("parlLeftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;   // usable child width inside the 200px sidebar

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("parlControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        int y = 55;
        auto addCaption = [&](const std::string& id, const std::string& text) {
            auto label = std::make_shared<UltraCanvasLabel>(id, PAD, y, SIDEBAR_W, 20);
            label->SetText(text);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
            leftSidebar->AddChild(label);
            y += 23;
        };
        auto addDropdown = [&](const std::string& id) {
            auto dropdown = std::make_shared<UltraCanvasDropdown>(id, PAD, y, SIDEBAR_W, 28);
            leftSidebar->AddChild(dropdown);
            y += 40;
            return dropdown;
        };

        addCaption("parlLayoutLabel", "Layout");
        auto layoutSelector = addDropdown("parlLayoutSelector");
        layoutSelector->AddItem("Hemicycle (180 deg)", "hemicycle");
        layoutSelector->AddItem("Horseshoe (240 deg)", "horseshoe");
        layoutSelector->AddItem("Circle", "circle");
        layoutSelector->AddItem("Westminster", "westminster");
        layoutSelector->AddItem("Grid", "grid");
        layoutSelector->SetSelectedIndex(0);

        addCaption("parlShapeLabel", "Seat Shape");
        auto shapeSelector = addDropdown("parlShapeSelector");
        shapeSelector->AddItem("Circle", "circle");
        shapeSelector->AddItem("Square", "square");
        shapeSelector->AddItem("Rounded Square", "rounded");
        shapeSelector->SetSelectedIndex(0);

        addCaption("parlRowsLabel", "Rows / Benches");
        auto rowsSelector = addDropdown("parlRowsSelector");
        rowsSelector->AddItem("Automatic", "0");
        rowsSelector->AddItem("4", "4");
        rowsSelector->AddItem("6", "6");
        rowsSelector->AddItem("8", "8");
        rowsSelector->AddItem("12", "12");
        rowsSelector->SetSelectedIndex(0);

        addCaption("parlLegendLabel", "Legend");
        auto legendSelector = addDropdown("parlLegendSelector");
        legendSelector->AddItem("Bottom", "bottom");
        legendSelector->AddItem("Right", "right");
        legendSelector->AddItem("Hidden", "hidden");
        legendSelector->SetSelectedIndex(0);

        addCaption("parlMajorityLabel", "Majority Marker");
        auto majoritySelector = addDropdown("parlMajoritySelector");
        majoritySelector->AddItem("Shown", "on");
        majoritySelector->AddItem("Hidden", "off");
        majoritySelector->SetSelectedIndex(0);

        addCaption("parlGovLabel", "Government Highlight");
        auto govSelector = addDropdown("parlGovSelector");
        govSelector->AddItem("Off", "off");
        govSelector->AddItem("Fade opposition", "on");
        govSelector->SetSelectedIndex(0);

        addCaption("parlThemeLabel", "Theme");
        auto themeSelector = addDropdown("parlThemeSelector");
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        y += 6;
        auto btnReset = std::make_shared<UltraCanvasButton>("parlBtnReset", PAD, y, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);
        y += 37;

        auto btnReload = std::make_shared<UltraCanvasButton>("parlBtnReload", PAD, y, SIDEBAR_W, 32);
        btnReload->SetText("Reload Sample Data");
        btnReload->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnReload);
        y += 45;

        auto helpBox = std::make_shared<UltraCanvasLabel>("parlHelpBox", PAD, y, SIDEBAR_W, 660 - y - PAD);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Hover a seat or legend\n  entry for its party\n"
                         "- Click to select a party\n"
                         "- Coalition tab: click\n  parties to add them");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("parlTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR (stats) =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("parlRightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("parlStatsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("CHAMBER");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("parlStatsPanel", PAD, 48, SIDEBAR_W, 210);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("parlAboutTitle", PAD, 280, SIDEBAR_W, 24);
        aboutTitle->SetText("ABOUT THE LAYOUTS");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(aboutTitle);

        auto aboutPanel = std::make_shared<UltraCanvasLabel>("parlAboutPanel", PAD, 312, SIDEBAR_W, 250);
        aboutPanel->SetText("Hemicycle: parties left to\nright in concentric arcs,\nseats per arc in proportion\nto its length.\n\n"
                            "Westminster: governing\nparties on the left benches,\nthe rest opposite, Speaker\nat the head of the aisle.\n\n"
                            "Grid: a plain waffle of\nseats in party order.");
        aboutPanel->SetFontSize(9);
        aboutPanel->SetAlignment(TextAlignment::Left);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);
        rightSidebar->AddChild(aboutPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("parlStatusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - hover the seats, or open the Coalition Builder tab and click parties");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        // Every tab: a title label above the diagram
        const int DIAGRAM_X = 20, DIAGRAM_Y = 56, DIAGRAM_W = 620, DIAGRAM_H = 476;

        auto makeTab = [&](const std::string& id, const std::string& title,
                           std::shared_ptr<UltraCanvasContainer>& tab) {
            tab = std::make_shared<UltraCanvasContainer>(id + "Tab", 0, 0, 660, 542);
            tab->SetBackgroundColor(Color(255, 255, 255, 255));

            auto tabTitle = std::make_shared<UltraCanvasLabel>(id + "Title", 20, 16, 620, 32);
            tabTitle->SetText(title);
            tabTitle->SetFontSize(17);
            tabTitle->SetFontWeight(FontWeight::Bold);
            tabTitle->SetAlignment(TextAlignment::Center);
            tab->AddChild(tabTitle);

            auto diagram = std::make_shared<UltraCanvasParliamentDiagram>(id + "Diagram", DIAGRAM_X, DIAGRAM_Y, DIAGRAM_W, DIAGRAM_H);
            tab->AddChild(diagram);
            return diagram;
        };

        // TAB 1: BUNDESTAG - classic hemicycle
        std::shared_ptr<UltraCanvasContainer> bundestagTab;
        auto bundestag = makeTab("parlBundestag", "German Bundestag, 2021 election", bundestagTab);
        bundestag->SetParties(ParliamentDiagramSamples::Bundestag2021());
        bundestag->SetChartTitle("Traffic-light coalition: SPD, Greens and FDP");
        bundestag->SetHighlightGovernment(false);

        // TAB 2: EUROPEAN PARLIAMENT - wide horseshoe, legend on the right
        std::shared_ptr<UltraCanvasContainer> europeTab;
        auto europe = makeTab("parlEurope", "European Parliament, 2024 election", europeTab);
        europe->SetParties(ParliamentDiagramSamples::EuropeanParliament2024());
        europe->SetChartTitle("Political groups, 720 seats");
        europe->SetArcSpan(200.0);
        europe->SetLegendPosition(ParliamentLegendPosition::Right);

        // TAB 3: HOUSE OF COMMONS - Westminster benches
        std::shared_ptr<UltraCanvasContainer> commonsTab;
        auto commons = makeTab("parlCommons", "UK House of Commons, 2024 general election", commonsTab);
        commons->SetParties(ParliamentDiagramSamples::HouseOfCommons2024());
        commons->SetChartTitle("Government benches left, opposition right");
        commons->SetLayout(ParliamentLayout::Westminster);
        commons->SetSeatShape(ParliamentSeatShape::RoundedSquare);

        // TAB 4: COALITION BUILDER - click parties to form a majority
        std::shared_ptr<UltraCanvasContainer> coalitionTab;
        auto coalition = makeTab("parlCoalition", "Coalition Builder", coalitionTab);
        {
            auto list = ParliamentDiagramSamples::Bundestag2021();
            for (auto& p : list) p.government = false;
            coalition->SetParties(list);
        }
        coalition->SetChartTitle("Click parties to add them to a coalition");
        coalition->SetHighlightGovernment(true);
        coalition->SetEnableSelection(false);

        tabbedContainer->AddTab("Bundestag", bundestagTab);
        tabbedContainer->AddTab("European Parliament", europeTab);
        tabbedContainer->AddTab("House of Commons", commonsTab);
        tabbedContainer->AddTab("Coalition Builder", coalitionTab);

        std::vector<std::shared_ptr<UltraCanvasParliamentDiagram>> allDiagrams = {
            bundestag, europe, commons, coalition
        };

        statsPanel->SetText(ChamberSummary(*bundestag));

        // ===== CONTROL WIRING =====
        auto applyDefaults = [bundestag, europe, commons, coalition]() {
            for (auto& d : {bundestag, europe, commons, coalition}) {
                d->SetLayout(ParliamentLayout::Hemicycle);
                d->SetArcSpan(180.0);
                d->SetSeatShape(ParliamentSeatShape::Circle);
                d->SetRowCount(0);
                d->SetLegendPosition(ParliamentLegendPosition::Bottom);
                d->SetShowMajorityMarker(true);
                d->SetHighlightGovernment(false);
                d->SetDarkTheme(false);
                d->ClearSelection();
            }
            europe->SetArcSpan(200.0);
            europe->SetLegendPosition(ParliamentLegendPosition::Right);
            commons->SetLayout(ParliamentLayout::Westminster);
            commons->SetSeatShape(ParliamentSeatShape::RoundedSquare);
            coalition->SetHighlightGovernment(true);
        };

        layoutSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            for (auto& d : allDiagrams) {
                if (item.value == "hemicycle") {
                    d->SetLayout(ParliamentLayout::Hemicycle);
                    d->SetArcSpan(180.0);
                } else if (item.value == "horseshoe") {
                    d->SetLayout(ParliamentLayout::Hemicycle);
                    d->SetArcSpan(240.0);
                } else if (item.value == "circle") {
                    d->SetLayout(ParliamentLayout::Circle);
                } else if (item.value == "westminster") {
                    d->SetLayout(ParliamentLayout::Westminster);
                } else if (item.value == "grid") {
                    d->SetLayout(ParliamentLayout::Grid);
                }
            }
        };

        shapeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            ParliamentSeatShape shape = ParliamentSeatShape::Circle;
            if (item.value == "square") shape = ParliamentSeatShape::Square;
            else if (item.value == "rounded") shape = ParliamentSeatShape::RoundedSquare;
            for (auto& d : allDiagrams) d->SetSeatShape(shape);
        };

        rowsSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            int rows = std::atoi(item.value.c_str());
            for (auto& d : allDiagrams) d->SetRowCount(rows);
        };

        legendSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            ParliamentLegendPosition pos = ParliamentLegendPosition::Bottom;
            if (item.value == "right") pos = ParliamentLegendPosition::Right;
            else if (item.value == "hidden") pos = ParliamentLegendPosition::Hidden;
            for (auto& d : allDiagrams) d->SetLegendPosition(pos);
        };

        majoritySelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            for (auto& d : allDiagrams) d->SetShowMajorityMarker(item.value == "on");
        };

        govSelector->onSelectionChanged = [allDiagrams, coalition](int, const DropdownItem& item) {
            for (auto& d : allDiagrams) d->SetHighlightGovernment(item.value == "on");
            // The coalition tab always fades the parties outside the coalition
            coalition->SetHighlightGovernment(true);
        };

        themeSelector->onSelectionChanged = [allDiagrams](int, const DropdownItem& item) {
            for (auto& d : allDiagrams) d->SetDarkTheme(item.value == "dark");
        };

        btnReset->onClick = [applyDefaults, layoutSelector, shapeSelector, rowsSelector, legendSelector,
                             majoritySelector, govSelector, themeSelector, statusBar]() {
            layoutSelector->SetSelectedIndex(0);
            shapeSelector->SetSelectedIndex(0);
            rowsSelector->SetSelectedIndex(0);
            legendSelector->SetSelectedIndex(0);
            majoritySelector->SetSelectedIndex(0);
            govSelector->SetSelectedIndex(0);
            themeSelector->SetSelectedIndex(0);
            applyDefaults();
            statusBar->SetText("View reset to the defaults of each tab");
        };

        btnReload->onClick = [bundestag, europe, commons, coalition, statsPanel, statusBar]() {
            bundestag->SetParties(ParliamentDiagramSamples::Bundestag2021());
            europe->SetParties(ParliamentDiagramSamples::EuropeanParliament2024());
            commons->SetParties(ParliamentDiagramSamples::HouseOfCommons2024());
            auto list = ParliamentDiagramSamples::Bundestag2021();
            for (auto& p : list) p.government = false;
            coalition->SetParties(list);
            statsPanel->SetText(ChamberSummary(*bundestag));
            statusBar->SetText("Sample data reloaded");
        };

        // Hover reports the party into the stats panel; clicks into the status bar
        for (auto& d : allDiagrams) {
            std::weak_ptr<UltraCanvasParliamentDiagram> weak = d;
            d->onPartyHover = [weak, statsPanel](size_t, const ParliamentParty& party) {
                if (auto diagram = weak.lock()) statsPanel->SetText(PartySummary(*diagram, party));
            };
        }

        for (auto& d : {bundestag, europe, commons}) {
            std::weak_ptr<UltraCanvasParliamentDiagram> weak = d;
            d->onPartyClick = [weak, statusBar](size_t, const ParliamentParty& party) {
                if (auto diagram = weak.lock()) {
                    statusBar->SetText(party.name + ": " + std::to_string(party.seats) + " of " +
                                       std::to_string(diagram->GetTotalSeats()) + " seats (" +
                                       PercentText(party.seats, diagram->GetTotalSeats()) + ")");
                }
            };
        }

        {
            std::weak_ptr<UltraCanvasParliamentDiagram> weak = coalition;
            coalition->onPartyClick = [weak, statusBar, statsPanel](size_t index, const ParliamentParty& party) {
                auto diagram = weak.lock();
                if (!diagram) return;
                diagram->SetPartyGovernment(index, !party.government);
                statusBar->SetText(CoalitionSummary(*diagram));
                statsPanel->SetText(ChamberSummary(*diagram));
            };
        }

        return mainContainer;
    }

} // namespace UltraCanvas
