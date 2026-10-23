// Apps/DemoApp/UltraCanvasDumbbellChartExamples.cpp
// Dumbbell (connected dot) chart showcase: a gender comparison recreating the
// Onward / JL Partners social media usage chart, a before/after study with live
// styling controls, a min/max temperature range chart with per-row colours, and
// a gap analysis tab driven by the chart's sorting and click callbacks.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a
//                  card, each tab a flex column whose notes and controls sit in
//                  a card beside the chart and wrap instead of fixed line breaks
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasDumbbellChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"

#include <memory>
#include <vector>
#include <string>
#include <random>

namespace UltraCanvas {

namespace {

constexpr int kTabContentW = 1240;
constexpr int kTabContentH = 700;

const Color kMenColor(70, 130, 190, 255);
const Color kWomenColor(214, 96, 130, 255);
const Color kConnectorColor(200, 200, 200, 255);

// =============================================================================
// TAB 1: SOCIAL MEDIA USAGE BY AGE AND GENDER
// =============================================================================
// Recreation of the Onward / JL Partners chart: percentage of UK adults in each
// age band who say they use each platform regularly, split by gender.

struct SocialRow {
    const char* label;
    double men;
    double women;
};

const SocialRow kSocialData[] = {
    {"Instagram 16-20", 68, 83}, {"Instagram 21-25", 66, 83}, {"Instagram 26-30", 73, 83},
    {"Instagram 31-35", 74, 79}, {"Instagram 36-40", 59, 70},

    {"YouTube 16-20", 69, 75},   {"YouTube 21-25", 69, 75},   {"YouTube 26-30", 70, 83},
    {"YouTube 31-35", 69, 83},   {"YouTube 36-40", 59, 77},

    {"TikTok 16-20", 70, 88},    {"TikTok 21-25", 64, 81},    {"TikTok 26-30", 64, 76},
    {"TikTok 31-35", 58, 65},    {"TikTok 36-40", 44, 51},

    {"Facebook 16-20", 23, 34},  {"Facebook 21-25", 47, 55},  {"Facebook 26-30", 65, 72},
    {"Facebook 31-35", 72, 78},  {"Facebook 36-40", 78, 78},

    {"Twitter/X 16-20", 14, 30}, {"Twitter/X 21-25", 27, 40}, {"Twitter/X 26-30", 33, 54},
    {"Twitter/X 31-35", 32, 59}, {"Twitter/X 36-40", 19, 46},

    {"Reddit 16-20", 11, 19},    {"Reddit 21-25", 19, 24},    {"Reddit 26-30", 21, 29},
    {"Reddit 31-35", 20, 29},    {"Reddit 36-40", 12, 29},

    {"Threads 16-20", 2, 3},     {"Threads 21-25", 5, 7},     {"Threads 26-30", 11, 12},
    {"Threads 31-35", 9, 14},    {"Threads 36-40", 6, 11},

    {"Bluesky 16-20", 0, 3},     {"Bluesky 21-25", 1, 2},     {"Bluesky 26-30", 2, 2},
    {"Bluesky 31-35", 2, 3},
};

// =============================================================================
// TAB LAYOUT HELPERS
// =============================================================================

// A tab: its title and subtitle over the content, a flex column. The tab is
// as big as the tabbed container's content area and scrolls when a longer
// text makes its content taller.
std::shared_ptr<UltraCanvasContainer> CreateTabPage(const std::string& id,
                                                    const std::string& titleId, const std::string& title,
                                                    const std::string& subtitleId, const std::string& subtitle) {
    auto tab = std::make_shared<UltraCanvasContainer>(id, 0, 0, kTabContentW, kTabContentH);
    tab->layout.SetFlexColumn().SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tab->SetPadding(12, 20);

    auto titleLabel = DemoKit::CreateTitle(titleId, title);
    titleLabel->SetFontSize(17);
    DemoKit::Add(tab, titleLabel);
    DemoKit::Add(tab, DemoKit::CreateSubtitle(subtitleId, subtitle));
    return tab;
}

// The chart and, beside it, a card for its notes or controls; the card moves
// under the chart when the tab is too narrow for both.
std::shared_ptr<UltraCanvasContainer> AddChartRow(const std::shared_ptr<UltraCanvasContainer>& tab,
                                                  const std::string& rowId,
                                                  const std::shared_ptr<UltraCanvasUIElement>& chart,
                                                  const std::string& sideId, const std::string& sideTitle = "") {
    auto row = DemoKit::AddRow(tab, rowId);
    DemoKit::AddFixed(row, chart);
    return DemoKit::AddCard(row, sideId, sideTitle, "", 300);
}

// The notes beside a chart: small text in the tab's grey, wrapping.
std::shared_ptr<UltraCanvasLabel> AddNote(const std::shared_ptr<UltraCanvasContainer>& parent,
                                          const std::string& id, const std::string& text,
                                          const Color& color = Color(70, 70, 70, 255)) {
    auto label = DemoKit::AddText(parent, id, text);
    label->SetFontSize(10.5f);
    label->SetTextColor(color);
    return label;
}

std::shared_ptr<UltraCanvasLabel> AddNoteHeading(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::string& text) {
    auto label = DemoKit::AddHeading(parent, id, text);
    label->SetFontSize(11);
    return label;
}

std::shared_ptr<UltraCanvasLabel> AddNoteBullets(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::vector<std::string>& items) {
    auto label = DemoKit::CreateBullets(id, items);
    label->SetFontSize(10.5f);
    label->SetTextColor(Color(70, 70, 70, 255));
    DemoKit::Add(parent, label);
    return label;
}

std::shared_ptr<UltraCanvasUIElement> BuildSocialMediaTab() {
    auto tab = CreateTabPage("DumbbellSocialTab",
                             "SocialTitle", "Social media usage by age and gender",
                             "SocialSubtitle", "Share of UK adults aged 16-40 who use each platform regularly");

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("SocialMediaDumbbell",
                                                            0, 0, 880, 590);
    chart->SetLegendLabels("Men", "Women");
    chart->SetAxisLabel("Percentage using the platform regularly (%)");
    chart->SetDefaultColors(kMenColor, kWomenColor, kConnectorColor);
    chart->SetValueFormat("%.0f%%");
    chart->SetCategoryLabelWidth(120.0f);
    chart->SetCategoryLabelFontSize(9.5f);
    chart->SetDotRadius(5.0f);
    chart->SetLineWidth(2.0f);
    chart->SetValueRange(0.0, 100.0);        // Percentages always read 0-100 here
    chart->SetShowValueLabels(false);        // 39 rows - the dots would be unreadable
    chart->SetAutoFitRows(true);

    for (const auto& row : kSocialData) {
        chart->AddDataPoint(row.label, row.men, row.women, "Men", "Women");
    }

    auto notes = AddChartRow(tab, "SocialRow", chart, "SocialNotes");
    AddNoteHeading(notes, "SocialNotesReadTitle", "Reading the chart");
    AddNote(notes, "SocialNotesRead",
            "Each row is one platform / age band. The blue dot is the share of men, the pink dot "
            "the share of women, and the grey bar between them is the gender gap.");
    AddNote(notes, "SocialNotesHover",
            "Hover any dot for the exact figures and the difference between the two groups.");
    AddNoteHeading(notes, "SocialNotesDataTitle", "What the data shows");
    AddNoteBullets(notes, "SocialNotesData", {
            "Women lead on every platform except Facebook 36-40, where the two are level.",
            "The widest gaps are on Twitter/X, where women lead by up to 27 points.",
            "TikTok 16-20 has the highest single reading at 88% of women.",
            "Bluesky is still marginal in every band."});
    AddNote(notes, "SocialNotesLabels",
            "Value labels are switched off on this tab: with 39 rows squeezed into the plot area the "
            "dots are too small to hold text. The chart detects this automatically when the labels "
            "are on and moves them outside the dots.");

    auto source = DemoKit::AddCaption(tab, "SocialSource",
            "Source: Onward / JL Partners  -  rendered with UltraCanvasDumbbellChart");
    source->SetFontSize(9.5f);

    return tab;
}

// =============================================================================
// TAB 2: BEFORE / AFTER WITH LIVE STYLING CONTROLS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildBeforeAfterTab() {
    auto tab = CreateTabPage("DumbbellBeforeAfterTab",
                             "BATitle", "Before / after: customer satisfaction by region",
                             "BASubtitle", "Satisfaction score before and after the 2026 service programme - use the controls to restyle the chart live");

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("BeforeAfterDumbbell",
                                                            0, 0, 860, 560);
    chart->SetLegendLabels("Before", "After");
    chart->SetAxisLabel("Satisfaction score (0-100)");
    chart->SetDefaultColors(Color(150, 150, 160, 255), Color(60, 160, 120, 255),
                            Color(205, 205, 205, 255));
    chart->SetValueFormat("%.0f");
    chart->SetCategoryLabelWidth(150.0f);
    chart->SetDotRadius(11.0f);
    chart->SetLineWidth(3.0f);
    chart->SetIncludeZeroInRange(false);
    chart->SetShowValueLabels(true);

    chart->AddDataPoint("North America", 62, 78, "Before", "After");
    chart->AddDataPoint("Latin America", 55, 71, "Before", "After");
    chart->AddDataPoint("United Kingdom", 68, 74, "Before", "After");
    chart->AddDataPoint("Western Europe", 71, 83, "Before", "After");
    chart->AddDataPoint("Eastern Europe", 49, 66, "Before", "After");
    chart->AddDataPoint("Middle East", 58, 61, "Before", "After");
    chart->AddDataPoint("Africa", 44, 63, "Before", "After");
    chart->AddDataPoint("South Asia", 52, 69, "Before", "After");
    chart->AddDataPoint("East Asia", 74, 81, "Before", "After");
    chart->AddDataPoint("Oceania", 66, 72, "Before", "After");

    // ===== CONTROL PANEL =====
    auto panel = AddChartRow(tab, "BARow", chart, "BAControls", "Chart controls");

    DemoKit::AddCaption(panel, "BADotLabel", "Dot radius");

    auto dotSlider = std::make_shared<UltraCanvasSlider>("BADotSlider", 0, 0, 280, 24);
    dotSlider->SetRange(3.0f, 20.0f);
    dotSlider->SetStep(1.0f);
    dotSlider->SetValue(11.0f);
    dotSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    dotSlider->onValueChanged = [chart](float value) { chart->SetDotRadius(value); };
    DemoKit::AddStretched(panel, dotSlider);

    DemoKit::AddCaption(panel, "BALineLabel", "Connector width");

    auto lineSlider = std::make_shared<UltraCanvasSlider>("BALineSlider", 0, 0, 280, 24);
    lineSlider->SetRange(1.0f, 10.0f);
    lineSlider->SetStep(1.0f);
    lineSlider->SetValue(3.0f);
    lineSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    lineSlider->onValueChanged = [chart](float value) { chart->SetLineWidth(value); };
    DemoKit::AddStretched(panel, lineSlider);

    DemoKit::AddCaption(panel, "BALabelWidth", "Category label column width");

    auto labelWidthSlider = std::make_shared<UltraCanvasSlider>("BALabelWidthSlider", 0, 0, 280, 24);
    labelWidthSlider->SetRange(60.0f, 260.0f);
    labelWidthSlider->SetStep(5.0f);
    labelWidthSlider->SetValue(150.0f);
    labelWidthSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    labelWidthSlider->onValueChanged = [chart](float value) { chart->SetCategoryLabelWidth(value); };
    DemoKit::AddStretched(panel, labelWidthSlider);

    auto gridCheck = std::make_shared<UltraCanvasCheckbox>("BAGrid", 0, 0, 280, 24, "Show grid");
    gridCheck->SetChecked(true);
    gridCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowGrid(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, gridCheck);

    auto valueCheck = std::make_shared<UltraCanvasCheckbox>("BAValues", 0, 0, 280, 24, "Show value labels");
    valueCheck->SetChecked(true);
    valueCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowValueLabels(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, valueCheck);

    auto legendCheck = std::make_shared<UltraCanvasCheckbox>("BALegend", 0, 0, 280, 24, "Show legend");
    legendCheck->SetChecked(true);
    legendCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowLegend(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, legendCheck);

    auto categoryCheck = std::make_shared<UltraCanvasCheckbox>("BACategories", 0, 0, 280, 24,
                                                               "Show category labels");
    categoryCheck->SetChecked(true);
    categoryCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowCategoryLabels(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, categoryCheck);

    DemoKit::AddCaption(panel, "BAPlacementLabel", "Value label placement");

    auto placementDrop = std::make_shared<UltraCanvasDropdown>("BAPlacement", 0, 0, 280, 26);
    placementDrop->AddItem("Auto (inside when they fit)");
    placementDrop->AddItem("Always inside the dots");
    placementDrop->AddItem("Always outside the dots");
    placementDrop->SetSelectedIndex(0, false);
    placementDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1:  chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Inside); break;
            case 2:  chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Outside); break;
            default: chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Auto); break;
        }
    };
    DemoKit::AddStretched(panel, placementDrop);

    auto randomBtn = std::make_shared<UltraCanvasButton>("BARandom", 0, 0, 280, 30);
    randomBtn->SetText("Generate random data");
    randomBtn->SetFontSize(11);
    randomBtn->onClick = [chart]() {
        static const char* regions[] = {
            "North America", "Latin America", "United Kingdom", "Western Europe",
            "Eastern Europe", "Middle East", "Africa", "South Asia", "East Asia", "Oceania"
        };

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> before(35.0, 75.0);
        std::uniform_real_distribution<double> delta(-8.0, 25.0);

        chart->ClearData();
        for (const char* region : regions) {
            double b = before(gen);
            double a = std::max(0.0, std::min(100.0, b + delta(gen)));
            chart->AddDataPoint(region, b, a, "Before", "After");
        }
    };
    DemoKit::AddStretched(panel, randomBtn);

    AddNote(panel, "BAHint",
            "The axis rescales itself to whatever data is loaded, snapping to round tick values.",
            Color(90, 90, 90, 255));
    AddNote(panel, "BAHint2",
            "Hover a dot to see the before value, the after value and the change between them.",
            Color(90, 90, 90, 255));

    return tab;
}

// =============================================================================
// TAB 3: MIN / MAX RANGE WITH PER-ROW COLOURS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildRangeTab() {
    auto tab = CreateTabPage("DumbbellRangeTab",
                             "RangeTitle", "Monthly temperature range - Reykjavik",
                             "RangeSubtitle", "Average daily low and high, with each row coloured by how cold the month gets");

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("RangeDumbbell", 0, 0, 860, 560);
    chart->SetLegendLabels("Average low", "Average high");
    chart->SetAxisLabel("Temperature (degrees Celsius)");
    // Every row overrides its own colours below; these only drive the legend swatches
    chart->SetDefaultColors(Color(125, 125, 135, 255), Color(205, 130, 55, 255),
                            Color(215, 215, 215, 255));
    chart->SetValueFormat("%.0f");
    chart->SetCategoryLabelWidth(110.0f);
    chart->SetDotRadius(10.0f);
    chart->SetLineWidth(6.0f);
    chart->SetIncludeZeroInRange(true);   // Keep the freezing point on the axis
    chart->SetShowValueLabels(true);
    chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Outside);

    struct MonthRange { const char* month; double low; double high; };
    const MonthRange months[] = {
        {"January", -3, 2},  {"February", -2, 3},  {"March", -2, 4},
        {"April", 1, 6},     {"May", 4, 10},       {"June", 7, 13},
        {"July", 9, 14},     {"August", 8, 14},    {"September", 6, 11},
        {"October", 2, 7},   {"November", -1, 4},  {"December", -2, 3},
    };

    for (const auto& m : months) {
        DumbbellDataPoint point(m.month, m.low, m.high, "Low", "High");
        // Colder months run blue, warmer months run amber
        double warmth = (m.high + 5.0) / 20.0;
        warmth = std::max(0.0, std::min(1.0, warmth));
        auto lerp = [warmth](int coldC, int warmC) {
            return static_cast<uint8_t>(coldC + (warmC - coldC) * warmth);
        };
        point.color1 = Color(lerp(60, 190), lerp(110, 140), lerp(200, 70), 255);
        point.color2 = Color(lerp(120, 230), lerp(170, 120), lerp(230, 40), 255);
        point.lineColor = Color(215, 215, 215, 255);
        chart->AddDataPoint(point);
    }

    auto notes = AddChartRow(tab, "RangeRow", chart, "RangeNotes");
    AddNoteHeading(notes, "RangeNotesColoursTitle", "Per-row colours");
    AddNote(notes, "RangeNotesColours",
            "Every DumbbellDataPoint carries its own color1, color2 and lineColor. Leave them "
            "transparent and the chart falls back to the colours set with SetDefaultColors().");
    AddNote(notes, "RangeNotesHeat",
            "Here each month is tinted by its average high, so the chart doubles as a heat scale.");
    AddNoteHeading(notes, "RangeNotesPlacementTitle", "Value label placement");
    AddNote(notes, "RangeNotesPlacement",
            "This tab pins the labels outside the dots. Each label sits beyond the outer edge of "
            "its own dot, so they never overlap however narrow the range gets.");
    AddNoteHeading(notes, "RangeNotesZeroTitle", "Zero on the axis");
    AddNote(notes, "RangeNotesZero",
            "SetIncludeZeroInRange(true) keeps the freezing point inside the plotted range even "
            "though no month averages exactly zero.");

    return tab;
}

// =============================================================================
// TAB 4: GAP ANALYSIS - SORTING AND CLICK CALLBACKS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildGapAnalysisTab() {
    auto tab = CreateTabPage("DumbbellGapTab",
                             "GapTitle", "Gap analysis: median hourly pay by sector",
                             "GapSubtitle", "Sort the rows to bring the widest gaps to the top, and click a row to inspect it");

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("GapDumbbell", 0, 0, 860, 540);
    chart->SetLegendLabels("Women", "Men");
    chart->SetAxisLabel("Median hourly pay (GBP)");
    chart->SetDefaultColors(Color(214, 96, 130, 255), Color(70, 130, 190, 255),
                            Color(198, 198, 198, 255));
    chart->SetValueFormat("%.2f");
    chart->SetCategoryLabelWidth(190.0f);
    chart->SetDotRadius(9.0f);
    chart->SetLineWidth(3.0f);
    chart->SetShowValueLabels(true);
    chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Outside);
    chart->SetSortOrder(DumbbellSortOrder::RangeDescending);

    chart->AddDataPoint("Finance & insurance", 21.40, 29.85, "Women", "Men");
    chart->AddDataPoint("Information & comms", 22.10, 26.90, "Women", "Men");
    chart->AddDataPoint("Construction", 16.05, 19.60, "Women", "Men");
    chart->AddDataPoint("Manufacturing", 15.20, 18.75, "Women", "Men");
    chart->AddDataPoint("Professional services", 19.30, 25.10, "Women", "Men");
    chart->AddDataPoint("Education", 17.85, 20.40, "Women", "Men");
    chart->AddDataPoint("Health & social work", 16.90, 18.20, "Women", "Men");
    chart->AddDataPoint("Retail & wholesale", 12.40, 14.05, "Women", "Men");
    chart->AddDataPoint("Transport & storage", 13.75, 15.30, "Women", "Men");
    chart->AddDataPoint("Hospitality", 11.20, 11.95, "Women", "Men");
    chart->AddDataPoint("Public administration", 18.60, 19.45, "Women", "Men");
    chart->AddDataPoint("Arts & recreation", 14.30, 16.85, "Women", "Men");

    auto panel = AddChartRow(tab, "GapRow", chart, "GapPanel");

    DemoKit::AddCaption(panel, "GapSortLabel", "Row order");

    auto sortDrop = std::make_shared<UltraCanvasDropdown>("GapSort", 0, 0, 300, 26);
    sortDrop->AddItem("Widest gap first");
    sortDrop->AddItem("Narrowest gap first");
    sortDrop->AddItem("Highest men's pay first");
    sortDrop->AddItem("Highest women's pay first");
    sortDrop->AddItem("Sector name (A-Z)");
    sortDrop->AddItem("As entered");
    sortDrop->SetSelectedIndex(0, false);
    sortDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 0: chart->SetSortOrder(DumbbellSortOrder::RangeDescending); break;
            case 1: chart->SetSortOrder(DumbbellSortOrder::RangeAscending); break;
            case 2: chart->SetSortOrder(DumbbellSortOrder::Value2Descending); break;
            case 3: chart->SetSortOrder(DumbbellSortOrder::Value1Descending); break;
            case 4: chart->SetSortOrder(DumbbellSortOrder::CategoryAscending); break;
            default: chart->SetSortOrder(DumbbellSortOrder::InsertionOrder); break;
        }
    };
    DemoKit::AddStretched(panel, sortDrop);

    auto selectionTitle = DemoKit::AddHeading(panel, "GapSelTitle", "Selected row");
    selectionTitle->SetFontSize(12);

    auto selection = AddNote(panel, "GapSelection",
                             "Click any row in the chart to read its values here.");
    selection->SetFontSize(11);

    // onRowClick reports the index into the data source, unaffected by sorting
    auto source = chart->GetDumbbellDataSource();
    chart->onRowClick = [selection, source](size_t rowIndex) {
        if (!source || rowIndex >= source->GetPointCount()) return;
        const auto& point = source->GetDumbbellPoint(rowIndex);

        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      "%s\n\n%s: %.2f\n%s: %.2f\nGap: %.2f (%.1f%%)",
                      point.categoryLabel.c_str(),
                      point.label1.c_str(), point.value1,
                      point.label2.c_str(), point.value2,
                      point.GetRange(),
                      point.value2 != 0.0 ? point.GetRange() / point.value2 * 100.0 : 0.0);
        selection->SetText(buffer);
    };

    const Color notesColor(90, 90, 90, 255);
    AddNoteHeading(panel, "GapNotesSortTitle", "Sorting");
    AddNote(panel, "GapNotes",
            "SetSortOrder() reorders the drawn rows without touching the data source, so the "
            "indices handed to the callbacks always refer to the original rows.", notesColor);
    AddNoteHeading(panel, "GapNotesCallbacksTitle", "Callbacks");
    AddNote(panel, "GapNotesCallbacks",
            "onRowClick fires for a click anywhere in a row; onDotClick fires as well when the "
            "click lands on one of the two dots and reports which one.", notesColor);
    AddNote(panel, "GapNotesFigures", "Figures are illustrative.", notesColor);

    return tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDumbbellChartExamples() {
    auto container = DemoKit::CreatePage("DumbbellChartExamples", "Dumbbell Chart Examples",
        "Connected dot charts: a gender comparison, a before/after study with live styling controls, "
        "a min/max range with per-row colours and a gap analysis with sorting and click callbacks");

    auto card = DemoKit::AddCard(container, "DumbbellChartCard");
    card->SetPadding(4);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "DumbbellChartTabs", 0, 0, kTabContentW + 10, kTabContentH + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("Social Media by Age & Gender", BuildSocialMediaTab());
    tabs->AddTab("Before / After", BuildBeforeAfterTab());
    tabs->AddTab("Min / Max Range", BuildRangeTab());
    tabs->AddTab("Gap Analysis & Sorting", BuildGapAnalysisTab());
    tabs->SetActiveTab(0);

    DemoKit::AddFixed(card, tabs);
    return container;
}

} // namespace UltraCanvas
