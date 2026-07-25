// Apps/DemoApp/UltraCanvasQuadrantChartExamples.cpp
// Quadrant chart examples: SWOT, BCG, Eisenhower, risk, priority and custom matrices
// Version: 1.0.0
// Last Modified: 2026-07-25
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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
            QuadrantDataPoint("R&D Projects", 0.3f, 0.8f, Color(212, 167, 44, 255)),
            QuadrantDataPoint("Core Products", 0.8f, 0.8f, Color(46, 160, 67, 255)),
            QuadrantDataPoint("Proof of Concepts", 0.2f, 0.3f, Color(9, 105, 218, 255)),
            QuadrantDataPoint("Maintenance", 0.7f, 0.2f, Color(219, 112, 147, 255)),
            QuadrantDataPoint("New Features", 0.6f, 0.7f, Color(46, 160, 67, 255))
        };
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateQuadrantChartExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // Sankey/Venn demos use - so absolutely-positioned children are not
        // clipped by the container's content area.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("quadrantMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("quadrantHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("quadrantMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Quadrant Chart Analysis Suite");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitle = std::make_shared<UltraCanvasLabel>("quadrantSubtitle", 30, 52, 900, 30);
        subtitle->SetText("Strategic 2x2 matrices: SWOT, BCG, Eisenhower, risk, priority and custom quadrants");
        subtitle->SetFontSize(13);
        subtitle->SetAlignment(TextAlignment::Left);
        subtitle->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitle);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("quadrantContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("quadrantLeftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;   // usable child width inside the 200px sidebar

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("quadrantControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto styleLabel = std::make_shared<UltraCanvasLabel>("quadrantStyleLabel", PAD, 55, SIDEBAR_W, 20);
        styleLabel->SetText("Visual Style");
        styleLabel->SetFontSize(10);
        styleLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(styleLabel);

        auto styleSelector = std::make_shared<UltraCanvasDropdown>("quadrantStyleSelector", PAD, 78, SIDEBAR_W, 28);
        styleSelector->AddItem("Business", "business");
        styleSelector->AddItem("Clean", "clean");
        styleSelector->AddItem("Scientific", "scientific");
        styleSelector->AddItem("Modern", "modern");
        styleSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(styleSelector);

        auto colorsLabel = std::make_shared<UltraCanvasLabel>("quadrantColorsLabel", PAD, 118, SIDEBAR_W, 20);
        colorsLabel->SetText("Quadrant Colors");
        colorsLabel->SetFontSize(10);
        colorsLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(colorsLabel);

        auto colorsSelector = std::make_shared<UltraCanvasDropdown>("quadrantColorsSelector", PAD, 141, SIDEBAR_W, 28);
        colorsSelector->AddItem("Shown", "on");
        colorsSelector->AddItem("Hidden", "off");
        colorsSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(colorsSelector);

        auto pointLabelsLabel = std::make_shared<UltraCanvasLabel>("quadrantPointLabelsLabel", PAD, 181, SIDEBAR_W, 20);
        pointLabelsLabel->SetText("Point Labels");
        pointLabelsLabel->SetFontSize(10);
        pointLabelsLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(pointLabelsLabel);

        auto pointLabelsSelector = std::make_shared<UltraCanvasDropdown>("quadrantPointLabelsSelector", PAD, 204, SIDEBAR_W, 28);
        pointLabelsSelector->AddItem("Shown", "on");
        pointLabelsSelector->AddItem("Hidden", "off");
        pointLabelsSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(pointLabelsSelector);

        auto btnAddPoint = std::make_shared<UltraCanvasButton>("quadrantBtnAddPoint", PAD, 250, SIDEBAR_W, 32);
        btnAddPoint->SetText("Add Random Point");
        btnAddPoint->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnAddPoint);

        auto btnClear = std::make_shared<UltraCanvasButton>("quadrantBtnClear", PAD, 287, SIDEBAR_W, 32);
        btnClear->SetText("Clear Points");
        btnClear->SetBackgroundColor(Color(220, 53, 69, 255));
        leftSidebar->AddChild(btnClear);

        auto btnReload = std::make_shared<UltraCanvasButton>("quadrantBtnReload", PAD, 324, SIDEBAR_W, 32);
        btnReload->SetText("Reload Sample Data");
        btnReload->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnReload);

        auto btnReset = std::make_shared<UltraCanvasButton>("quadrantBtnReset", PAD, 361, SIDEBAR_W, 32);
        btnReset->SetText("Reset View");
        btnReset->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnReset);

        auto helpBox = std::make_shared<UltraCanvasLabel>("quadrantHelpBox", PAD, 412, SIDEBAR_W, 120);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Click a point to select\n"
                         "- Click empty space to\n"
                         "  clear the selection\n"
                         "- Hover for details\n"
                         "- Double-click a point");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("quadrantTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR (stats) =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("quadrantRightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("quadrantStatsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("STATISTICS");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("quadrantStatsPanel", PAD, 48, SIDEBAR_W, 220);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto legendTitle = std::make_shared<UltraCanvasLabel>("quadrantLegendTitle", PAD, 290, SIDEBAR_W, 24);
        legendTitle->SetText("ABOUT THIS CHART");
        legendTitle->SetFontSize(11);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(73, 80, 87, 255));
        legendTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(legendTitle);

        auto legendPanel = std::make_shared<UltraCanvasLabel>("quadrantLegendPanel", PAD, 322, SIDEBAR_W, 200);
        legendPanel->SetFontSize(9);
        legendPanel->SetAlignment(TextAlignment::Left);
        legendPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        legendPanel->SetPadding(12);
        rightSidebar->AddChild(legendPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("quadrantStatusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - click data points to select them, hover for tooltips");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

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
            tabs.push_back({"SWOT", chart,
                            "SWOT Analysis\n\nInternal vs external\nfactors against positive\nvs negative impact.\n\nStrengths, Weaknesses,\nOpportunities, Threats."});
        }

        // TAB 2: BCG MATRIX
        {
            auto chart = CreateBCGMatrixChart("bcgChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({"BCG Matrix", chart,
                            "BCG Matrix\n\nMarket share vs market\ngrowth. Bubble size\nencodes revenue.\n\nStars, Cash Cows,\nQuestion Marks, Dogs."});
        }

        // TAB 3: EISENHOWER MATRIX
        {
            auto chart = CreateEisenhowerMatrixChart("eisenhowerChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({"Eisenhower", chart,
                            "Eisenhower Matrix\n\nUrgency vs importance\nfor task prioritization.\n\nDo First, Schedule,\nDelegate, Eliminate."});
        }

        // TAB 4: RISK MATRIX
        {
            auto chart = CreateRiskMatrixChart("riskChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({"Risk Matrix", chart,
                            "Risk Matrix\n\nProbability of occurrence\nvs impact severity.\n\nTop-right quadrant =\ncritical risks needing\nmitigation first."});
        }

        // TAB 5: PRIORITY MATRIX
        {
            auto chart = CreatePriorityMatrixChart("priorityChart", CHART_X, CHART_Y, CHART_W, CHART_H);
            chart->LoadSampleData();
            chart->SetShowDataPointLabels(true);
            tabs.push_back({"Priority", chart,
                            "Priority Matrix\n\nImpact/value vs effort\nrequired.\n\nQuick Wins, Major\nProjects, Fill-ins,\nThankless Tasks."});
        }

        // TAB 6: CUSTOM QUADRANTS
        {
            QuadrantDefinition customDef;
            customDef.topLeftLabel = "Innovation\nZone";
            customDef.topRightLabel = "Success\nZone";
            customDef.bottomLeftLabel = "Learning\nZone";
            customDef.bottomRightLabel = "Comfort\nZone";
            customDef.topLeftColor = Color(255, 215, 0, 100);
            customDef.topRightColor = Color(144, 238, 144, 100);
            customDef.bottomLeftColor = Color(173, 216, 230, 100);
            customDef.bottomRightColor = Color(255, 182, 193, 100);

            auto chart = CreateCustomQuadrantChart("customQuadrantChart", CHART_X, CHART_Y, CHART_W, CHART_H,
                                                   "Innovation vs Execution Matrix", customDef);

            QuadrantAxisConfiguration customAxis;
            customAxis.xAxisLabel = "Execution Capability";
            customAxis.yAxisLabel = "Innovation Level";
            customAxis.xAxisLeftLabel = "Low Execution";
            customAxis.xAxisRightLabel = "High Execution";
            customAxis.yAxisBottomLabel = "Low Innovation";
            customAxis.yAxisTopLabel = "High Innovation";
            chart->SetAxisConfiguration(customAxis);

            chart->SetDataPoints(BuildInnovationMatrixData());
            chart->SetShowDataPointLabels(true);
            tabs.push_back({"Custom", chart,
                            "Custom Quadrants\n\nUser-defined quadrant\nlabels, colors and axis\ncaptions via\nQuadrantDefinition and\nQuadrantAxisConfiguration."});
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
            oss << "Total Points: " << chart->GetDataPoints().size() << "\n";
            oss << "Selected: " << chart->GetSelectedPoints().size() << "\n\n";
            oss << "Per quadrant:\n";
            oss << "Top Left: " << chart->CountPointsInQuadrant(QuadrantPosition::TopLeft) << "\n";
            oss << "Top Right: " << chart->CountPointsInQuadrant(QuadrantPosition::TopRight) << "\n";
            oss << "Bottom Left: " << chart->CountPointsInQuadrant(QuadrantPosition::BottomLeft) << "\n";
            oss << "Bottom Right: " << chart->CountPointsInQuadrant(QuadrantPosition::BottomRight);
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
                std::ostringstream oss;
                oss << "Selected: " << point.label
                    << " (" << point.xValue << ", " << point.yValue << ")";
                statusBar->SetText(oss.str());
                updateStats();
            };
            chart->onPointDeselect = [statusBar, updateStats](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText("Deselected: " + point.label);
                updateStats();
            };
            chart->onPointHover = [statusBar](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText("Hovering: " + point.label);
            };
            chart->onPointDoubleClick = [statusBar](size_t, const QuadrantDataPoint& point) {
                statusBar->SetText("Double-clicked: " + point.label);
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
            std::string label = "Point " + std::to_string(rand() % 1000);
            Color color(rand() % 156 + 100, rand() % 156 + 100, rand() % 156 + 100, 220);

            chart->AddDataPoint(label, x, y, color);
            statusBar->SetText("Added " + label);
            updateStats();
        };

        btnClear->onClick = [activeChart, statusBar, updateStats]() {
            auto chart = activeChart();
            if (!chart) return;
            chart->ClearDataPoints();
            statusBar->SetText("Cleared all data points of the active chart");
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
            statusBar->SetText("Sample data reloaded for the active chart");
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

            statusBar->SetText("View reset - all charts restored to their defaults");
            updateStats();
        };

        // Initial panel contents
        updateStats();
        updateAbout();

        return mainContainer;
    }

} // namespace UltraCanvas
