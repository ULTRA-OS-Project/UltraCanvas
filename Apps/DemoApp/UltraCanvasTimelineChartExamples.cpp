// Apps/DemoApp/UltraCanvasTimelineChartExamples.cpp
// Chronological timeline examples: date axis, spans, milestones, zoom and pan
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): controls,
//                  chart and view in cards that wrap, every text sized to fit,
//                  the about texts wrap instead of carrying line breaks
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

    namespace {
        // A button in the controls card: stretched across it, and as wide as
        // its text when that is wider.
        std::shared_ptr<UltraCanvasButton> AddTimelineButton(const std::shared_ptr<UltraCanvasContainer>& card,
                                                             const std::string& id, const std::string& text,
                                                             const Color& background) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 30, text);
            button->SetBackgroundColor(background);
            DemoKit::Add(card, button);
            return button;
        }

        // A caption over a dropdown, the dropdown stretched across the card.
        std::shared_ptr<UltraCanvasDropdown> AddTimelineSelector(const std::shared_ptr<UltraCanvasContainer>& card,
                                                                 const std::string& id, const std::string& labelId,
                                                                 const std::string& label) {
            auto caption = DemoKit::AddCaption(card, labelId, label);
            caption->SetFontWeight(FontWeight::Bold);
            auto selector = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 170, 28);
            DemoKit::AddStretched(card, selector);
            return selector;
        }

        // A side card keeps its width (the sidebar's 200 px) and lets the
        // chart card take the rest of the row.
        std::shared_ptr<UltraCanvasContainer> AddTimelineSideCard(const std::shared_ptr<UltraCanvasContainer>& row,
                                                                  const std::string& id, const std::string& titleId,
                                                                  const std::string& title) {
            auto card = DemoKit::AddCard(row, id, "", "", 200);
            card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            DemoKit::AddHeading(card, titleId, title);
            return card;
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTimelineChartExamples() {
        auto mainContainer = DemoKit::CreatePage("tcMainContainer", "Timeline Chart",
                "Milestones and spans placed to scale on a real date axis - zoom with the wheel, drag to pan, double-click to reset");

        // Controls | tabs with the status line | view and about. The side cards
        // wrap under the chart in a narrow window.
        auto contentContainer = DemoKit::AddRow(mainContainer, "tcContent");

        // ===== LEFT SIDEBAR (controls) =====
        auto leftSidebar = AddTimelineSideCard(contentContainer, "tcLeftSidebar", "tcControlsTitle", "CONTROLS");

        auto themeSelector = AddTimelineSelector(leftSidebar, "tcThemeSelector", "tcThemeLabel", "Theme");
        themeSelector->AddItem("Light", "light");
        themeSelector->AddItem("Dark", "dark");
        themeSelector->SetSelectedIndex(0);

        auto axisSelector = AddTimelineSelector(leftSidebar, "tcAxisSelector", "tcAxisLabel", "Axis Position");
        axisSelector->AddItem("Center", "center");
        axisSelector->AddItem("Top", "top");
        axisSelector->AddItem("Bottom", "bottom");
        axisSelector->SetSelectedIndex(0);

        auto laneSelector = AddTimelineSelector(leftSidebar, "tcLaneSelector", "tcLaneLabel", "Row Grouping");
        laneSelector->AddItem("Per tab", "tab");
        laneSelector->AddItem("Packed", "packed");
        laneSelector->AddItem("Swimlanes", "swimlanes");
        laneSelector->SetSelectedIndex(0);

        auto scaleSelector = AddTimelineSelector(leftSidebar, "tcScaleSelector", "tcScaleLabel", "Time Scale");
        scaleSelector->AddItem("Auto", "auto");
        scaleSelector->AddItem("Days", "days");
        scaleSelector->AddItem("Weeks", "weeks");
        scaleSelector->AddItem("Months", "months");
        scaleSelector->AddItem("Quarters", "quarters");
        scaleSelector->AddItem("Years", "years");
        scaleSelector->SetSelectedIndex(0);

        auto paletteSelector = AddTimelineSelector(leftSidebar, "tcPaletteSelector", "tcPaletteLabel", "Palette");
        paletteSelector->AddItem("Corporate Blue", "corporate");
        paletteSelector->AddItem("Vibrant", "vibrant");
        paletteSelector->AddItem("Ocean", "ocean");
        paletteSelector->AddItem("Sunset", "sunset");
        paletteSelector->AddItem("Forest", "forest");
        paletteSelector->AddItem("Slate", "slate");
        paletteSelector->SetSelectedIndex(0);

        auto labelsSelector = AddTimelineSelector(leftSidebar, "tcLabelsSelector", "tcLabelsLabel", "Labels");
        labelsSelector->AddItem("Auto place", "auto");
        labelsSelector->AddItem("Always callout", "callout");
        labelsSelector->AddItem("Inside bars", "inside");
        labelsSelector->AddItem("Hidden", "hidden");
        labelsSelector->SetSelectedIndex(0);

        auto btnZoomIn = AddTimelineButton(leftSidebar, "tcBtnZoomIn", "Zoom In", Color(13, 110, 253, 255));
        auto btnZoomOut = AddTimelineButton(leftSidebar, "tcBtnZoomOut", "Zoom Out", Color(13, 110, 253, 255));
        auto btnPanBack = AddTimelineButton(leftSidebar, "tcBtnPanBack", "Pan Back 30 Days", Color(111, 66, 193, 255));
        auto btnFit = AddTimelineButton(leftSidebar, "tcBtnFit", "Fit To Data", Color(25, 135, 84, 255));
        auto btnAddMilestone = AddTimelineButton(leftSidebar, "tcBtnAdd", "Add Milestone", Color(108, 117, 125, 255));

        auto helpBox = DemoKit::AddCaption(leftSidebar, "tcHelpBox",
                "INTERACTION TIPS\n\n"
                "- Wheel zooms about the cursor date\n"
                "- Drag to pan the range\n"
                "- Click an entry to select");
        helpBox->SetTextColor(DemoKit::BodyText());
        helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
        helpBox->SetPadding(10);

        // ===== TABS =====
        auto chartCard = DemoKit::AddCard(contentContainer, "tcChartCard", "", "", 690);
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("tcTabs", 0, 0, 660, 580);
        tabbedContainer->SetTabPosition(TabPosition::Top);
        DemoKit::AddFixed(chartCard, tabbedContainer);

        auto statusBar = DemoKit::AddText(chartCard, "tcStatusBar",
                "Ready - wheel to zoom, drag to pan, click an entry to select it");
        statusBar->SetFontSize(11);
        statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
        statusBar->SetPadding(15);
        statusBar->SetTextColor(Color(73, 80, 87, 255));

        // ===== RIGHT SIDEBAR =====
        auto rightSidebar = AddTimelineSideCard(contentContainer, "tcRightSidebar", "tcStatsTitle", "VIEW");

        auto statsPanel = DemoKit::AddText(rightSidebar, "tcStatsPanel", "");
        statsPanel->SetFontSize(10);
        statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        statsPanel->SetPadding(12);

        DemoKit::AddHeading(rightSidebar, "tcAboutTitle", "ABOUT THIS TAB");

        auto aboutPanel = DemoKit::AddText(rightSidebar, "tcAboutPanel", "");
        aboutPanel->SetFontSize(9);
        aboutPanel->SetBackgroundColor(Color(248, 249, 250, 255));
        aboutPanel->SetPadding(12);

        const int CHART_X = 20, CHART_Y = 15, CHART_W = 620, CHART_H = 510;

        enum class TabData { Program, History, Swimlane };
        struct TabSpec {
            std::string tabTitle;
            TimelineChartDesign design;
            TabData dataset;
            std::string title;
            std::string about;
        };
        const std::vector<TabSpec> tabSpecs = {
                {"Modern", TimelineChartDesign::Modern, TabData::Program, "Partner Development 2026",
                 "Modern\n\nRounded phase bars below the axis, milestone markers above it, project bookends at both ends.\n\nSpans and milestone callouts share one packer, so a label never lands on a bar."},
                {"Classic", TimelineChartDesign::Classic, TabData::Program, "Partner Development 2026",
                 "Classic\n\nFlat bars and a thin axis - the print-friendly variant. Era bands are off by default here."},
                {"Minimal", TimelineChartDesign::Minimal, TabData::Program, "Partner Development 2026",
                 "Minimal\n\nThin line bars, hairline grid, small markers, axis at the top so everything hangs below it."},
                {"Roadmap", TimelineChartDesign::Roadmap, TabData::History, "Company History",
                 "Roadmap\n\nBig circular markers and era bands across five decades. The axis scale resolves itself from the zoom level - decades here, months once you zoom in."},
                {"Dark", TimelineChartDesign::Dark, TabData::Program, "Partner Development 2026",
                 "Dark\n\nThe dark-theme variant of Modern; every colour is recoloured coherently, the palette is untouched."},
                {"Swimlanes", TimelineChartDesign::Modern, TabData::Swimlane,
                 "Programme 2026",
                 "Swimlanes\n\nSame date axis, same entries - only the row assignment changes: one named band per workstream instead of auto-packing.\n\nBands sub-pack inside themselves and share one height budget, so a busy row can take three sub-rows while quiet rows use one."}};

        std::vector<std::shared_ptr<UltraCanvasTimelineChart>> allCharts;
        for (const auto& spec : tabSpecs) {
            std::shared_ptr<TimelineChartDataSource> source;
            switch (spec.dataset) {
                case TabData::History:  source = TimelineChartSamples::CompanyHistory(); break;
                case TabData::Swimlane: source = TimelineChartSamples::SwimlaneProgram(2026); break;
                case TabData::Program:
                default:                source = TimelineChartSamples::DevelopmentTimeline(2026); break;
            }
            auto chart = CreateTimelineChartWithData(
                    "tcChart" + std::to_string(allCharts.size()),
                    CHART_X, CHART_Y, CHART_W, CHART_H, source, spec.design);
            chart->SetTitle(spec.title);
            switch (spec.dataset) {
                case TabData::History:
                    chart->SetSubtitle("Five decades of milestones");
                    break;
                case TabData::Swimlane:
                    chart->SetSubtitle("Four workstreams on one date axis");
                    chart->SetLaneMode(TimelineLaneMode::Swimlanes);
                    chart->SetSwimlanes(TimelineChartSamples::ProgramSwimlanes());
                    chart->SetNow(2026, 6, 15);
                    break;
                case TabData::Program:
                default:
                    chart->SetSubtitle("Phases, milestones and hand-over");
                    chart->SetNow(2026, 8, 20);
                    break;
            }
            if (spec.design == TimelineChartDesign::Minimal) {
                chart->SetAxisPosition(TimelineAxisPosition::Top);
            }

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

        // "Per tab" restores each tab's own grouping; the other two force one
        std::vector<TimelineLaneMode> tabModes;
        for (auto& chart : allCharts) tabModes.push_back(chart->GetLaneMode());
        laneSelector->onSelectionChanged =
                [allCharts, tabModes](int, const DropdownItem& item) {
            for (size_t i = 0; i < allCharts.size(); ++i) {
                TimelineLaneMode mode = tabModes[i];
                if (item.value == "packed") mode = TimelineLaneMode::Packed;
                else if (item.value == "swimlanes") mode = TimelineLaneMode::Swimlanes;
                allCharts[i]->SetLaneMode(mode);
            }
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
