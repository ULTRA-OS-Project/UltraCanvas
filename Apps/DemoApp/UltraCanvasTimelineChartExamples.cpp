// Apps/DemoApp/UltraCanvasTimelineChartExamples.cpp
// Chronological timeline examples: date axis, spans, milestones, zoom and pan
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasTimelineChart.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimelineChartExamples() {
        // Children placed directly inside a sidebar/panel container are inset
        // explicitly instead of using SetPadding() - the same pattern the
        // SWOT/Gantt/Venn demos use.
        const int PAD = 15;

        auto mainContainer = std::make_shared<UltraCanvasContainer>("tcMainContainer", 0, 0, 1140, 800);
        mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

        auto headerContainer = std::make_shared<UltraCanvasContainer>("tcHeader", 0, 0, 1140, 90);
        headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(headerContainer);

        auto mainTitle = std::make_shared<UltraCanvasLabel>("tcMainTitle", 30, 15, 1080, 35);
        mainTitle->SetText("Timeline Chart");
        mainTitle->SetFontSize(22);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainTitle->SetAlignment(TextAlignment::Left);
        mainTitle->SetTextColor(Color(33, 37, 41, 255));
        headerContainer->AddChild(mainTitle);

        auto subtitleLabel = std::make_shared<UltraCanvasLabel>("tcSubtitle", 30, 52, 1040, 30);
        subtitleLabel->SetText("Milestones and spans placed to scale on a real date axis - zoom with the wheel, drag to pan, double-click to reset");
        subtitleLabel->SetFontSize(13);
        subtitleLabel->SetAlignment(TextAlignment::Left);
        subtitleLabel->SetTextColor(Color(108, 117, 125, 255));
        headerContainer->AddChild(subtitleLabel);

        auto contentContainer = std::make_shared<UltraCanvasContainer>("tcContent", 0, 90, 1140, 710);
        mainContainer->AddChild(contentContainer);

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = std::make_shared<UltraCanvasContainer>("tcLeftSidebar", 20, 20, 200, 670);
        leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(leftSidebar);

        const int SIDEBAR_W = 170;

        auto controlsTitle = std::make_shared<UltraCanvasLabel>("tcControlsTitle", PAD, PAD, SIDEBAR_W, 22);
        controlsTitle->SetText("CONTROLS");
        controlsTitle->SetFontSize(11);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(73, 80, 87, 255));
        controlsTitle->SetAlignment(TextAlignment::Left);
        leftSidebar->AddChild(controlsTitle);

        auto themeLabel = std::make_shared<UltraCanvasLabel>("tcThemeLabel", PAD, 50, SIDEBAR_W, 18);
        themeLabel->SetText("Theme");
        themeLabel->SetFontSize(10);
        themeLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(themeLabel);

        auto themeSelector = std::make_shared<UltraCanvasDropdown>("tcThemeSelector", PAD, 70, SIDEBAR_W, 28);
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(themeSelector);

        auto axisLabel = std::make_shared<UltraCanvasLabel>("tcAxisLabel", PAD, 106, SIDEBAR_W, 18);
        axisLabel->SetText("Axis Position");
        axisLabel->SetFontSize(10);
        axisLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(axisLabel);

        auto axisSelector = std::make_shared<UltraCanvasDropdown>("tcAxisSelector", PAD, 126, SIDEBAR_W, 28);
        axisSelector->AddItem("Center", "center");
        axisSelector->AddItem("Top", "top");
        axisSelector->AddItem("Bottom", "bottom");
        axisSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(axisSelector);

        auto scaleLabel = std::make_shared<UltraCanvasLabel>("tcScaleLabel", PAD, 162, SIDEBAR_W, 18);
        scaleLabel->SetText("Time Scale");
        scaleLabel->SetFontSize(10);
        scaleLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(scaleLabel);

        auto scaleSelector = std::make_shared<UltraCanvasDropdown>("tcScaleSelector", PAD, 182, SIDEBAR_W, 28);
        scaleSelector->AddItem("Auto", "auto");
        scaleSelector->AddItem("Days", "days");
        scaleSelector->AddItem("Weeks", "weeks");
        scaleSelector->AddItem("Months", "months");
        scaleSelector->AddItem("Quarters", "quarters");
        scaleSelector->AddItem("Years", "years");
        scaleSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(scaleSelector);

        auto paletteLabel = std::make_shared<UltraCanvasLabel>("tcPaletteLabel", PAD, 218, SIDEBAR_W, 18);
        paletteLabel->SetText("Palette");
        paletteLabel->SetFontSize(10);
        paletteLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(paletteLabel);

        auto paletteSelector = std::make_shared<UltraCanvasDropdown>("tcPaletteSelector", PAD, 238, SIDEBAR_W, 28);
        paletteSelector->AddItem("Corporate Blue", "corporate");
        paletteSelector->AddItem("Vibrant", "vibrant");
        paletteSelector->AddItem("Ocean", "ocean");
        paletteSelector->AddItem("Sunset", "sunset");
        paletteSelector->AddItem("Forest", "forest");
        paletteSelector->AddItem("Slate", "slate");
        paletteSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(paletteSelector);

        auto labelsLabel = std::make_shared<UltraCanvasLabel>("tcLabelsLabel", PAD, 274, SIDEBAR_W, 18);
        labelsLabel->SetText("Labels");
        labelsLabel->SetFontSize(10);
        labelsLabel->SetFontWeight(FontWeight::Bold);
        leftSidebar->AddChild(labelsLabel);

        auto labelsSelector = std::make_shared<UltraCanvasDropdown>("tcLabelsSelector", PAD, 294, SIDEBAR_W, 28);
        labelsSelector->AddItem("Auto place", "auto");
        labelsSelector->AddItem("Always callout", "callout");
        labelsSelector->AddItem("Inside bars", "inside");
        labelsSelector->AddItem("Hidden", "hidden");
        labelsSelector->SetSelectedIndex(0);
        leftSidebar->AddChild(labelsSelector);

        auto btnZoomIn = std::make_shared<UltraCanvasButton>("tcBtnZoomIn", PAD, 336, SIDEBAR_W, 30);
        btnZoomIn->SetText("Zoom In");
        btnZoomIn->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnZoomIn);

        auto btnZoomOut = std::make_shared<UltraCanvasButton>("tcBtnZoomOut", PAD, 371, SIDEBAR_W, 30);
        btnZoomOut->SetText("Zoom Out");
        btnZoomOut->SetBackgroundColor(Color(13, 110, 253, 255));
        leftSidebar->AddChild(btnZoomOut);

        auto btnPanBack = std::make_shared<UltraCanvasButton>("tcBtnPanBack", PAD, 406, SIDEBAR_W, 30);
        btnPanBack->SetText("Pan Back 30 Days");
        btnPanBack->SetBackgroundColor(Color(111, 66, 193, 255));
        leftSidebar->AddChild(btnPanBack);

        auto btnFit = std::make_shared<UltraCanvasButton>("tcBtnFit", PAD, 441, SIDEBAR_W, 30);
        btnFit->SetText("Fit To Data");
        btnFit->SetBackgroundColor(Color(25, 135, 84, 255));
        leftSidebar->AddChild(btnFit);

        auto btnAddMilestone = std::make_shared<UltraCanvasButton>("tcBtnAdd", PAD, 476, SIDEBAR_W, 30);
        btnAddMilestone->SetText("Add Milestone");
        btnAddMilestone->SetBackgroundColor(Color(108, 117, 125, 255));
        leftSidebar->AddChild(btnAddMilestone);

        auto helpBox = std::make_shared<UltraCanvasLabel>("tcHelpBox", PAD, 520, SIDEBAR_W, 130);
        helpBox->SetText("INTERACTION TIPS\n\n"
                         "- Wheel zooms about the\n  cursor date\n"
                         "- Drag to pan the range\n"
                         "- Click an entry to select\n"
                         "- Double-click empty space\n  to fit the data again");
        helpBox->SetFontSize(9);
        helpBox->SetAlignment(TextAlignment::Left);
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);
        leftSidebar->AddChild(helpBox);

        // ===== TABS =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("tcTabs", 240, 20, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        contentContainer->AddChild(tabbedContainer);

        // ===== RIGHT SIDEBAR =====
        auto rightSidebar = std::make_shared<UltraCanvasContainer>("tcRightSidebar", 920, 20, 200, 580);
        rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
        contentContainer->AddChild(rightSidebar);

        auto statsTitle = std::make_shared<UltraCanvasLabel>("tcStatsTitle", PAD, PAD, SIDEBAR_W, 24);
        statsTitle->SetText("VIEW");
        statsTitle->SetFontSize(11);
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetTextColor(Color(73, 80, 87, 255));
        statsTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(statsTitle);

        auto statsPanel = std::make_shared<UltraCanvasLabel>("tcStatsPanel", PAD, 48, SIDEBAR_W, 180);
        statsPanel->SetFontSize(10);
        statsPanel->SetAlignment(TextAlignment::Left);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);
        rightSidebar->AddChild(statsPanel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("tcAboutTitle", PAD, 240, SIDEBAR_W, 24);
        aboutTitle->SetText("ABOUT THIS TAB");
        aboutTitle->SetFontSize(11);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(73, 80, 87, 255));
        aboutTitle->SetAlignment(TextAlignment::Left);
        rightSidebar->AddChild(aboutTitle);

        auto aboutPanel = std::make_shared<UltraCanvasLabel>("tcAboutPanel", PAD, 272, SIDEBAR_W, 250);
        aboutPanel->SetFontSize(9);
        aboutPanel->SetAlignment(TextAlignment::Left);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);
        rightSidebar->AddChild(aboutPanel);

        auto statusBar = std::make_shared<UltraCanvasLabel>("tcStatusBar", 240, 610, 660, 60);
        statusBar->SetText("Ready - wheel to zoom, drag to pan, click an entry to select it");
        statusBar->SetFontSize(11);
        statusBar->SetAlignment(TextAlignment::Left);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));
        contentContainer->AddChild(statusBar);

        const int CHART_X = 20, CHART_Y = 15, CHART_W = 620, CHART_H = 510;

        struct TabSpec {
            std::string tabTitle;
            TimelineChartDesign design;
            bool history;         // Company history instead of the project plan
            std::string title;
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Modern", TimelineChartDesign::Modern, false, "Partner Development 2026",
                 "Modern\n\nRounded phase bars below\nthe axis, milestone\nmarkers above it, project\nbookends at both ends.\n\nSpans and milestone\ncallouts share one\npacker, so a label never\nlands on a bar."},
                {"Classic", TimelineChartDesign::Classic, false, "Partner Development 2026",
                 "Classic\n\nFlat bars and a thin axis\n- the print-friendly\nvariant. Era bands are\noff by default here."},
                {"Minimal", TimelineChartDesign::Minimal, false, "Partner Development 2026",
                 "Minimal\n\nThin line bars, hairline\ngrid, small markers, axis\nat the top so everything\nhangs below it."},
                {"Roadmap", TimelineChartDesign::Roadmap, true, "Company History",
                 "Roadmap\n\nBig circular markers and\nera bands across five\ndecades. The axis scale\nresolves itself from the\nzoom level - decades\nhere, months once you\nzoom in."},
                {"Dark", TimelineChartDesign::Dark, false, "Partner Development 2026",
                 "Dark\n\nThe dark-theme variant of\nModern; every colour is\nrecoloured coherently,\nthe palette is untouched."}};

        std::vector<std::shared_ptr<UltraCanvasTimelineChart>> allCharts;
        for (const auto& spec : tabSpecs) {
            auto chart = CreateTimelineChartWithData(
                    "tcChart" + std::to_string(allCharts.size()),
                    CHART_X, CHART_Y, CHART_W, CHART_H,
                    spec.history ? TimelineChartSamples::CompanyHistory()
                                 : TimelineChartSamples::DevelopmentTimeline(2026),
                    spec.design);
            chart->SetTitle(spec.title);
            chart->SetSubtitle(spec.history ? "Five decades of milestones"
                                            : "Phases, milestones and hand-over");
            if (spec.design == TimelineChartDesign::Minimal) {
                chart->SetAxisPosition(TimelineAxisPosition::Top);
            }
            if (!spec.history) chart->SetNow(2026, 8, 20);

            auto tabContent = std::make_shared<UltraCanvasContainer>(
                    chart->GetIdentifier() + "Tab", 0, 0, 660, 542);
            tabContent->SetBackgroundColor(Color(255, 255, 255, 255));
            tabContent->AddChild(chart);
            tabbedContainer->AddTab(spec.tabTitle, tabContent);
            allCharts.push_back(chart);
        }

        auto activeChart = [tabbedContainer, allCharts]() -> std::shared_ptr<UltraCanvasTimelineChart> {
            int index = tabbedContainer->GetActiveTab();
            if (index < 0 || index >= static_cast<int>(allCharts.size())) return nullptr;
            return allCharts[index];
        };

        auto updateStats = [activeChart, statsPanel]() {
            auto chart = activeChart();
            if (!chart) return;

            double lo = 0.0, hi = 0.0;
            chart->GetDateRange(lo, hi);

            std::ostringstream oss;
            oss << "From: " << TimeAxis::FormatDate(lo) << "\n";
            oss << "To:   " << TimeAxis::FormatDate(hi) << "\n";
            oss << "Span: " << static_cast<long>(hi - lo) << " days\n\n";
            oss << "Entries: " << chart->GetData()->Count() << "\n\n";

            const int selected = chart->GetSelectedIndex();
            oss << "Selected: ";
            if (selected >= 0) {
                oss << "\n" << chart->GetData()->GetEntries()[selected].name;
            } else {
                oss << "none";
            }
            statsPanel->SetText(oss.str());
        };

        std::vector<std::string> aboutTexts;
        for (const auto& spec : tabSpecs) aboutTexts.push_back(spec.about);
        auto updateAbout = [aboutTexts, tabbedContainer, aboutPanel]() {
            int index = tabbedContainer->GetActiveTab();
            if (index >= 0 && index < static_cast<int>(aboutTexts.size())) {
                aboutPanel->SetText(aboutTexts[index]);
            }
        };

        // ===== CHART CALLBACKS =====
        for (auto& chart : allCharts) {
            chart->onEntrySelect = [statusBar, updateStats](size_t, const TimelineChartEntry& entry) {
                statusBar->SetText("Selected: " + entry.name + " (" +
                                   TimeAxis::FormatDate(entry.start) + ")");
                updateStats();
            };
            chart->onEntryHover = [statusBar](size_t, const TimelineChartEntry& entry) {
                statusBar->SetText("Hovering: " + entry.name);
            };
            chart->onEntryDoubleClick = [statusBar](size_t, const TimelineChartEntry& entry) {
                statusBar->SetText("Double-clicked: " + entry.name);
            };
            chart->onSelectionChange = updateStats;
            chart->onViewChanged = updateStats;
        }

        // ===== CONTROL CALLBACKS =====
        tabbedContainer->onTabChange = [updateStats, updateAbout](int, int) {
            updateStats();
            updateAbout();
        };

        themeSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            const bool dark = (item.value == "dark");
            for (auto& chart : allCharts) chart->SetDarkTheme(dark);
        };

        axisSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            TimelineAxisPosition position = TimelineAxisPosition::Center;
            if (item.value == "top") position = TimelineAxisPosition::Top;
            else if (item.value == "bottom") position = TimelineAxisPosition::Bottom;
            for (auto& chart : allCharts) chart->SetAxisPosition(position);
        };

        scaleSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            TimelineScale scale = TimelineScale::Auto;
            if (item.value == "days") scale = TimelineScale::Days;
            else if (item.value == "weeks") scale = TimelineScale::Weeks;
            else if (item.value == "months") scale = TimelineScale::Months;
            else if (item.value == "quarters") scale = TimelineScale::Quarters;
            else if (item.value == "years") scale = TimelineScale::Years;
            for (auto& chart : allCharts) chart->SetScale(scale);
        };

        paletteSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            TimelineChartPalette palette = TimelineChartPalette::CorporateBlue;
            if (item.value == "vibrant") palette = TimelineChartPalette::Vibrant;
            else if (item.value == "ocean") palette = TimelineChartPalette::Ocean;
            else if (item.value == "sunset") palette = TimelineChartPalette::Sunset;
            else if (item.value == "forest") palette = TimelineChartPalette::Forest;
            else if (item.value == "slate") palette = TimelineChartPalette::Slate;
            for (auto& chart : allCharts) chart->SetPalette(palette);
        };

        labelsSelector->onSelectionChanged = [allCharts](int, const DropdownItem& item) {
            TimelineLabelPlacement placement = TimelineLabelPlacement::AutoPlace;
            if (item.value == "callout") placement = TimelineLabelPlacement::Callout;
            else if (item.value == "inside") placement = TimelineLabelPlacement::InsideBar;
            else if (item.value == "hidden") placement = TimelineLabelPlacement::Hidden;
            for (auto& chart : allCharts) {
                chart->EditStyle().labelPlacement = placement;
                chart->StyleChanged();
            }
        };

        btnZoomIn->onClick = [activeChart, statusBar, updateStats]() {
            if (auto chart = activeChart()) {
                chart->ZoomBy(0.7);
                statusBar->SetText("Zoomed in");
                updateStats();
            }
        };
        btnZoomOut->onClick = [activeChart, statusBar, updateStats]() {
            if (auto chart = activeChart()) {
                chart->ZoomBy(1.0 / 0.7);
                statusBar->SetText("Zoomed out");
                updateStats();
            }
        };
        btnPanBack->onClick = [activeChart, statusBar, updateStats]() {
            if (auto chart = activeChart()) {
                chart->PanDays(-30.0);
                statusBar->SetText("Panned back 30 days");
                updateStats();
            }
        };
        btnFit->onClick = [activeChart, statusBar, updateStats]() {
            if (auto chart = activeChart()) {
                chart->FitToData();
                statusBar->SetText("View fitted to the data");
                updateStats();
            }
        };

        auto milestoneCounter = std::make_shared<int>(0);
        btnAddMilestone->onClick = [activeChart, milestoneCounter, statusBar, updateStats]() {
            auto chart = activeChart();
            if (!chart) return;

            double lo = 0.0, hi = 0.0;
            chart->GetDateRange(lo, hi);
            const double when = lo + (hi - lo) * 0.5;
            const std::string name = "New milestone " + std::to_string(++(*milestoneCounter));
            const int id = chart->GetData()->AddMilestone(name, when);
            chart->GetData()->SetEntryMarker(id, TimelineMarkerStyle::Star);
            chart->GetData()->SetEntryImportance(id, 1.4f);
            chart->DataChanged();
            statusBar->SetText("Added " + name + " on " + TimeAxis::FormatDate(when));
            updateStats();
        };

        updateStats();
        updateAbout();
        return mainContainer;
    }

} // namespace UltraCanvas
