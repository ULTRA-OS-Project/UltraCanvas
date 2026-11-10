// Apps/DemoApp/UltraCanvasQuadrantChartExamples.cpp
// Quadrant chart examples: SWOT, BCG, Eisenhower, risk, priority and custom matrices
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): controls, tabs and
//                  statistics as cards in a wrapping row, every text sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasQuadrantChart.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace UltraCanvas {

    // Builds the data set used by the "Custom" tab: an innovation-vs-execution
    // matrix with user-defined quadrant labels and colors.
    static std::vector<QuadrantDataPoint> BuildInnovationMatrixData() {
        return {
            QuadrantDataPoint(Tr("R&D Projects"), 0.3f, 0.8f, Color(212, 167, 44, 255)),
            QuadrantDataPoint(Tr("Core Products"), 0.8f, 0.8f, Color(46, 160, 67, 255)),
            QuadrantDataPoint(Tr("Proof of Concepts"), 0.2f, 0.3f, Color(9, 105, 218, 255)),
            QuadrantDataPoint(Tr("Maintenance"), 0.7f, 0.2f, Color(219, 112, 147, 255)),
            QuadrantDataPoint(Tr("New Features"), 0.6f, 0.7f, Color(46, 160, 67, 255))
        };
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateQuadrantChartExamples() {
        auto page = DemoKit::CreatePage("quadrantMainContainer", Tr("Quadrant Chart Analysis Suite"),
                Tr("Strategic 2x2 matrices: SWOT, BCG, Eisenhower, risk, priority and custom quadrants"));

        // Controls, the tabbed charts and the statistics side by side; the
        // side cards wrap under the charts when the window is narrow.
        auto contentContainer = DemoKit::AddRow(page, "quadrantContent");
        constexpr float sideCardWidth = 200;
        // The tabbed container keeps its 660 px.
        constexpr float tabsCardWidth = 660 + 2 * DemoKit::kCardPadding + 2;

        // A small bold caption over a control.
        auto addFieldLabel = [](const std::shared_ptr<UltraCanvasContainer>& parent,
                                const std::string& id, const std::string& text) {
            auto label = DemoKit::AddText(parent, id, text);
            label->SetFontSize(10);
            label->SetFontWeight(FontWeight::Bold);
            return label;
        };

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = DemoKit::AddCard(contentContainer, "quadrantLeftSidebar", Tr("CONTROLS"), "", sideCardWidth);

        addFieldLabel(leftSidebar, "quadrantStyleLabel", Tr("Visual Style"));
        auto styleSelector = std::make_shared<UltraCanvasDropdown>("quadrantStyleSelector", 0, 0, 170, 28);
        styleSelector->AddItem(Tr("Business"), "business");
        styleSelector->AddItem(Tr("Clean"), "clean");
        styleSelector->AddItem(Tr("Scientific"), "scientific");
        styleSelector->AddItem(Tr("Modern"), "modern");
        styleSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(leftSidebar, styleSelector);

        addFieldLabel(leftSidebar, "quadrantColorsLabel", Tr("Quadrant Colors"));
        auto colorsSelector = std::make_shared<UltraCanvasDropdown>("quadrantColorsSelector", 0, 0, 170, 28);
        colorsSelector->AddItem(Tr("Shown"), "on");
        colorsSelector->AddItem(Tr("Hidden"), "off");
        colorsSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(leftSidebar, colorsSelector);

        addFieldLabel(leftSidebar, "quadrantPointLabelsLabel", Tr("Point Labels"));
        auto pointLabelsSelector = std::make_shared<UltraCanvasDropdown>("quadrantPointLabelsSelector", 0, 0, 170, 28);
        pointLabelsSelector->AddItem(Tr("Shown"), "on");
        pointLabelsSelector->AddItem(Tr("Hidden"), "off");
        pointLabelsSelector->SetSelectedIndex(0);
        DemoKit::AddStretched(leftSidebar, pointLabelsSelector);

        // The buttons are stretched across the card and as tall as their text.
        auto buttons = DemoKit::CreateColumn("quadrantButtons", 5);
        buttons->SetMargin(10, 0, 0, 0);
        DemoKit::Add(leftSidebar, buttons);

        auto btnAddPoint = std::make_shared<UltraCanvasButton>("quadrantBtnAddPoint", Tr("Add Random Point"));
        btnAddPoint->SetBackgroundColor(Color(13, 110, 253, 255));
        DemoKit::Add(buttons, btnAddPoint);

        auto btnClear = std::make_shared<UltraCanvasButton>("quadrantBtnClear", Tr("Clear Points"));
        btnClear->SetBackgroundColor(Color(220, 53, 69, 255));
        DemoKit::Add(buttons, btnClear);

        auto btnReload = std::make_shared<UltraCanvasButton>("quadrantBtnReload", Tr("Reload Sample Data"));
        btnReload->SetBackgroundColor(Color(25, 135, 84, 255));
        DemoKit::Add(buttons, btnReload);

        auto btnReset = std::make_shared<UltraCanvasButton>("quadrantBtnReset", Tr("Reset View"));
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        DemoKit::Add(buttons, btnReset);

        auto helpBox = DemoKit::CreateText("quadrantHelpBox",
                Tr("INTERACTION TIPS\n\n"
                "- Click a point to select\n"
                "- Click empty space to clear the selection\n"
                "- Hover for details\n"
                "- Double-click a point"));
        helpBox->SetFontSize(9);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        helpBox->SetMargin(10, 0, 0, 0);
        DemoKit::Add(leftSidebar, helpBox);

        // ===== TABS =====
        auto tabsCard = DemoKit::AddCard(contentContainer, "quadrantTabsCard", "", "", tabsCardWidth);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("quadrantTabs", 0, 0, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(tabsCard, tabbedContainer);

        auto statusBar = DemoKit::CreateText("quadrantStatusBar",
                Tr("Ready - click data points to select them, hover for tooltips"));
        statusBar->SetFontSize(11);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        DemoKit::Add(tabsCard, statusBar);

        // ===== RIGHT SIDEBAR (stats) =====
        auto rightSidebar = DemoKit::AddCard(contentContainer, "quadrantRightSidebar", Tr("STATISTICS"), "", sideCardWidth);

        // Filled by updateStats / updateAbout below.
        auto statsPanel = DemoKit::CreateText("quadrantStatsPanel", "");
        statsPanel->SetFontSize(10);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        DemoKit::Add(rightSidebar, statsPanel);

        auto legendTitle = DemoKit::AddHeading(rightSidebar, "quadrantLegendTitle", Tr("ABOUT THIS CHART"));
        legendTitle->SetMargin(10, 0, 0, 0);

        auto legendPanel = DemoKit::CreateText("quadrantLegendPanel", "");
        legendPanel->SetFontSize(9);
        legendPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        legendPanel->SetPadding(12);
        DemoKit::Add(rightSidebar, legendPanel);

        // Chart geometry inside each tab
        const int CHART_X = 20, CHART_Y = 15, CHART_W = 620, CHART_H = 510;

        struct TabSpec {
            std::string tabTitle;
            std::shared_ptr<UltraCanvasQuadrantChart> chart;
            std::string about;
        };
        std::vector<TabSpec> tabs;

        // TAB 1: SWOT ANALYSIS
        {
            auto chart = CreateSWOTAnalysisChart("swotChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("SWOT"), chart,
                            Tr("SWOT Analysis\n\nInternal vs external factors against positive vs negative impact.\n\n"
                            "Strengths, Weaknesses, Opportunities, Threats.")});
        }

        // TAB 2: BCG MATRIX
        {
            auto chart = CreateBCGMatrixChart("bcgChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("BCG Matrix"), chart,
                            Tr("BCG Matrix\n\nMarket share vs market growth. Bubble size encodes revenue.\n\n"
                            "Stars, Cash Cows, Question Marks, Dogs.")});
        }

        // TAB 3: EISENHOWER MATRIX
        {
            auto chart = CreateEisenhowerMatrixChart("eisenhowerChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("Eisenhower"), chart,
                            Tr("Eisenhower Matrix\n\nUrgency vs importance for task prioritization.\n\n"
                            "Do First, Schedule, Delegate, Eliminate.")});
        }

        // TAB 4: RISK MATRIX
        {
            auto chart = CreateRiskMatrixChart("riskChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("Risk Matrix"), chart,
                            Tr("Risk Matrix\n\nProbability of occurrence vs impact severity.\n\n"
                            "Top-right quadrant = critical risks needing mitigation first.")});
        }

        // TAB 5: PRIORITY MATRIX
        {
            auto chart = CreatePriorityMatrixChart("priorityChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("Priority"), chart,
                            Tr("Priority Matrix\n\nImpact/value vs effort required.\n\n"
                            "Quick Wins, Major Projects, Fill-ins, Thankless Tasks.")});
        }

        // TAB 6: CUSTOM QUADRANTS
        {
            QuadrantDefinition customDef;
            customDef.topLeftLabel = Tr("Innovation\nZone");
            customDef.topRightLabel = Tr("Success\nZone");
            customDef.bottomLeftLabel = Tr("Learning\nZone");
            customDef.bottomRightLabel = Tr("Comfort\nZone");
            customDef.topLeftColor = Color(255, 215, 0, 100);
            customDef.topRightColor = Color(144, 238, 144, 100);
            customDef.bottomLeftColor = Color(173, 216, 230, 100);
            customDef.bottomRightColor = Color(255, 182, 193, 100);

            auto chart = CreateCustomQuadrantChart("customQuadrantChart", CHART_X, CHART_Y, CHART_W, CHART_H,
                                                   Tr("Innovation vs Execution Matrix"), customDef);

            QuadrantAxisConfiguration customAxis;
            customAxis.xAxisLabel = Tr("Execution Capability");
            customAxis.yAxisLabel = Tr("Innovation Level");
            customAxis.xAxisLeftLabel = Tr("Low Execution");
            customAxis.xAxisRightLabel = Tr("High Execution");
            customAxis.yAxisBottomLabel = Tr("Low Innovation");
            customAxis.yAxisTopLabel = Tr("High Innovation");
            chart->SetAxisConfiguration(customAxis);

            chart->SetDataPoints(BuildInnovationMatrixData());
            chart->SetShowDataPointLabels(true);
            tabs.push_back({Tr("Custom"), chart,
                            Tr("Custom Quadrants\n\nUser-defined quadrant labels, colors and axis captions via "
                            "QuadrantDefinition and QuadrantAxisConfiguration.")});
        }

        std::vector<std::shared_ptr<UltraCanvasQuadrantChart>> allCharts;
        for (auto& spec : tabs) {
            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    spec.chart->GetIdentifier() + "Tab", 0, 0, 660, 542);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(spec.chart);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allCharts.push_back(spec.chart);
        }

        // Returns the chart belonging to the currently active tab
        auto activeChart = [tabbedContainer, allCharts]() -> std::shared_ptr<UltraCanvasQuadrantChart> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allCharts.size())) return nullptr;
            return allCharts[index];
        };

        // Refreshes the statistics panel from the active chart
        auto updateStats = [activeChart, statsPanel]() {
            auto chart = activeChart();
            if (!chart) return;

            std::ostringstream oss;
            oss << TrF("Total Points: {0}", chart->GetDataPoints().size()) << "\n";
            oss << TrF("Selected: {0}", chart->GetSelectedPoints().size()) << "\n\n";
            oss << Tr("Per quadrant:") << "\n";
            oss << TrF("Top Left: {0}", chart->CountPointsInQuadrant(QuadrantPosition::TopLeft)) << "\n";
            oss << TrF("Top Right: {0}", chart->CountPointsInQuadrant(QuadrantPosition::TopRight)) << "\n";
            oss << TrF("Bottom Left: {0}", chart->CountPointsInQuadrant(QuadrantPosition::BottomLeft)) << "\n";
            oss << TrF("Bottom Right: {0}", chart->CountPointsInQuadrant(QuadrantPosition::BottomRight));
            statsPanel->SetText(oss.str());
        };

        // Refreshes the "about" panel from the active tab
        std::vector<std::string> aboutTexts;
        for (auto& spec : tabs) aboutTexts.push_back(spec.about);
        auto updateAbout = [aboutTexts, tabbedContainer, legendPanel]() {
            int index = tabbedContainer->GetActiveTab();
            if (index >= 0 && index < static_cast<int>(aboutTexts.size())) {
                legendPanel->SetText(aboutTexts[index]);
            }
        };

        // ===== CHART CALLBACKS =====
        for (auto& chart : allCharts) {
            chart->onPointSelect = [statusBar, updateStats](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText(TrF("Selected: {0} ({1}, {2})", point.label, point.xValue, point.yValue));
                updateStats();
            };
            chart->onPointDeselect = [statusBar, updateStats](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText(TrF("Deselected: {0}", point.label));
                updateStats();
            };
            chart->onPointHover = [statusBar](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText(TrF("Hovering: {0}", point.label));
            };
            chart->onPointDoubleClick = [statusBar](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText(TrF("Double-clicked: {0}", point.label));
            };
            chart->onSelectionChange = updateStats;
        }

        // ===== CONTROL CALLBACKS =====
        tabbedContainer->onTabChange = [updateStats, updateAbout](int, int) {
            updateStats();
            updateAbout();
        };

        styleSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            QuadrantStyle style = QuadrantStyle::Business;
            if (item.value == "clean") style = QuadrantStyle::Clean;
            else if (item.value == "scientific") style = QuadrantStyle::Scientific;
            else if (item.value == "modern") style = QuadrantStyle::Modern;
            for (auto& chart : allCharts) chart->SetQuadrantStyle(style);
        };

        colorsSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            bool show = (item.value == "on");
            for (auto& chart : allCharts) chart->SetShowQuadrantColors(show);
        };

        pointLabelsSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            bool show = (item.value == "on");
            for (auto& chart : allCharts) chart->SetShowDataPointLabels(show);
        };

        btnAddPoint->onClick = [activeChart, statusBar, updateStats]() {
            auto chart = activeChart();
            if (!chart) return;

            float x = static_cast<float>(rand()) / RAND_MAX;
            float y = static_cast<float>(rand()) / RAND_MAX;
            std::string label = TrF("Point {0}", rand() % 1000);
            Color color(rand() % 156 + 100, rand() % 156 + 100, rand() % 156 + 100, 220);

            chart->AddDataPoint(label, x, y, color);
            statusBar->SetText(TrF("Added {0}", label));
            updateStats();
        };

        btnClear->onClick = [activeChart, statusBar, updateStats]() {
            auto chart = activeChart();
            if (!chart) return;
            chart->ClearDataPoints();
            statusBar->SetText(Tr("Cleared all data points of the active chart"));
            updateStats();
        };

        auto reloadActiveChart = [activeChart, tabbedContainer, allCharts]() {
            auto chart = activeChart();
            if (!chart) return;
            if (chart->GetQuadrantType() == QuadrantType::Custom) {
                chart->SetDataPoints(BuildInnovationMatrixData());
            } else {
                chart->LoadSampleData();
            }
        };

        btnReload->onClick = [reloadActiveChart, statusBar, updateStats]() {
            reloadActiveChart();
            statusBar->SetText(Tr("Sample data reloaded for the active chart"));
            updateStats();
        };

        btnReset->onClick = [styleSelector, colorsSelector, pointLabelsSelector,
                             allCharts, statusBar, updateStats]() {
            styleSelector->SetSelectedIndex(0);
            colorsSelector->SetSelectedIndex(0);
            pointLabelsSelector->SetSelectedIndex(0);

            for (auto& chart : allCharts) {
                chart->SetQuadrantStyle(QuadrantStyle::Business);
                chart->SetShowQuadrantColors(true);
                chart->SetShowDataPointLabels(true);
                chart->ClearSelection();
                if (chart->GetQuadrantType() == QuadrantType::Custom) {
                    chart->SetDataPoints(BuildInnovationMatrixData());
                } else {
                    chart->LoadSampleData();
                }
            }

            statusBar->SetText(Tr("View reset - all charts restored to their defaults"));
            updateStats();
        };

        // Initial panel contents
        updateStats();
        updateAbout();

        return page;
    }

} // namespace UltraCanvas
