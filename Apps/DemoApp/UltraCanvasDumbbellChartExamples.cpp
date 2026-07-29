// Apps/DemoApp/UltraCanvasDumbbellChartExamples.cpp
// Dumbbell (connected dot) chart showcase: a gender comparison recreating the
// Onward / JL Partners social media usage chart, a before/after study with live
// styling controls, a min/max temperature range chart with per-row colours, and
// a gap analysis tab driven by the chart's sorting and click callbacks.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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

std::shared_ptr<UltraCanvasUIElement> BuildSocialMediaTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("DumbbellSocialTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("SocialTitle", 30, 12, 700, 26);
    title->SetText("Social media usage by age and gender");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("SocialSubtitle", 30, 40, 700, 20);
    subtitle->SetText("Share of UK adults aged 16-40 who use each platform regularly");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("SocialMediaDumbbell",
                                                            20, 66, 880, 590);
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
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("SocialNotes", 920, 90, 300, 460);
    notes->SetText(
        "Reading the chart\n\n"
        "Each row is one platform / age band. The\n"
        "blue dot is the share of men, the pink dot\n"
        "the share of women, and the grey bar\n"
        "between them is the gender gap.\n\n"
        "Hover any dot for the exact figures and\n"
        "the difference between the two groups.\n\n"
        "What the data shows\n\n"
        "  - Women lead on every platform except\n"
        "    Facebook 36-40, where the two are level.\n"
        "  - The widest gaps are on Twitter/X, where\n"
        "    women lead by up to 27 points.\n"
        "  - TikTok 16-20 has the highest single\n"
        "    reading at 88% of women.\n"
        "  - Bluesky is still marginal in every band.\n\n"
        "Value labels are switched off on this tab:\n"
        "with 39 rows squeezed into the plot area the\n"
        "dots are too small to hold text. The chart\n"
        "detects this automatically when the labels\n"
        "are on and moves them outside the dots.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    auto source = std::make_shared<UltraCanvasLabel>("SocialSource", 20, 662, 880, 18);
    source->SetText("Source: Onward / JL Partners  -  rendered with UltraCanvasDumbbellChart");
    source->SetFontSize(9.5f);
    source->SetTextColor(Color(130, 130, 130, 255));
    tab->AddChild(source);

    return tab;
}

// =============================================================================
// TAB 2: BEFORE / AFTER WITH LIVE STYLING CONTROLS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildBeforeAfterTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("DumbbellBeforeAfterTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("BATitle", 30, 12, 700, 26);
    title->SetText("Before / after: customer satisfaction by region");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("BASubtitle", 30, 40, 800, 20);
    subtitle->SetText("Satisfaction score before and after the 2026 service programme - use the controls to restyle the chart live");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("BeforeAfterDumbbell",
                                                            20, 70, 860, 560);
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
    tab->AddChild(chart);

    // ===== CONTROL PANEL =====
    int panelX = 900;
    int y = 78;

    auto controlsTitle = std::make_shared<UltraCanvasLabel>("BAControls", panelX, y, 300, 22);
    controlsTitle->SetText("Chart controls");
    controlsTitle->SetFontSize(13);
    controlsTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(controlsTitle);
    y += 30;

    auto dotLabel = std::make_shared<UltraCanvasLabel>("BADotLabel", panelX, y, 300, 18);
    dotLabel->SetText("Dot radius");
    dotLabel->SetFontSize(11);
    tab->AddChild(dotLabel);
    y += 20;

    auto dotSlider = std::make_shared<UltraCanvasSlider>("BADotSlider", panelX, y, 280, 24);
    dotSlider->SetRange(3.0f, 20.0f);
    dotSlider->SetStep(1.0f);
    dotSlider->SetValue(11.0f);
    dotSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    dotSlider->onValueChanged = [chart](float value) { chart->SetDotRadius(value); };
    tab->AddChild(dotSlider);
    y += 34;

    auto lineLabel = std::make_shared<UltraCanvasLabel>("BALineLabel", panelX, y, 300, 18);
    lineLabel->SetText("Connector width");
    lineLabel->SetFontSize(11);
    tab->AddChild(lineLabel);
    y += 20;

    auto lineSlider = std::make_shared<UltraCanvasSlider>("BALineSlider", panelX, y, 280, 24);
    lineSlider->SetRange(1.0f, 10.0f);
    lineSlider->SetStep(1.0f);
    lineSlider->SetValue(3.0f);
    lineSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    lineSlider->onValueChanged = [chart](float value) { chart->SetLineWidth(value); };
    tab->AddChild(lineSlider);
    y += 34;

    auto labelWidthLabel = std::make_shared<UltraCanvasLabel>("BALabelWidth", panelX, y, 300, 18);
    labelWidthLabel->SetText("Category label column width");
    labelWidthLabel->SetFontSize(11);
    tab->AddChild(labelWidthLabel);
    y += 20;

    auto labelWidthSlider = std::make_shared<UltraCanvasSlider>("BALabelWidthSlider", panelX, y, 280, 24);
    labelWidthSlider->SetRange(60.0f, 260.0f);
    labelWidthSlider->SetStep(5.0f);
    labelWidthSlider->SetValue(150.0f);
    labelWidthSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    labelWidthSlider->onValueChanged = [chart](float value) { chart->SetCategoryLabelWidth(value); };
    tab->AddChild(labelWidthSlider);
    y += 40;

    auto gridCheck = std::make_shared<UltraCanvasCheckbox>("BAGrid", panelX, y, 280, 24, "Show grid");
    gridCheck->SetChecked(true);
    gridCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowGrid(newState == CheckedState::Checked);
    };
    tab->AddChild(gridCheck);
    y += 28;

    auto valueCheck = std::make_shared<UltraCanvasCheckbox>("BAValues", panelX, y, 280, 24, "Show value labels");
    valueCheck->SetChecked(true);
    valueCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowValueLabels(newState == CheckedState::Checked);
    };
    tab->AddChild(valueCheck);
    y += 28;

    auto legendCheck = std::make_shared<UltraCanvasCheckbox>("BALegend", panelX, y, 280, 24, "Show legend");
    legendCheck->SetChecked(true);
    legendCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowLegend(newState == CheckedState::Checked);
    };
    tab->AddChild(legendCheck);
    y += 28;

    auto categoryCheck = std::make_shared<UltraCanvasCheckbox>("BACategories", panelX, y, 280, 24,
                                                               "Show category labels");
    categoryCheck->SetChecked(true);
    categoryCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowCategoryLabels(newState == CheckedState::Checked);
    };
    tab->AddChild(categoryCheck);
    y += 36;

    auto placementLabel = std::make_shared<UltraCanvasLabel>("BAPlacementLabel", panelX, y, 300, 18);
    placementLabel->SetText("Value label placement");
    placementLabel->SetFontSize(11);
    tab->AddChild(placementLabel);
    y += 20;

    auto placementDrop = std::make_shared<UltraCanvasDropdown>("BAPlacement", panelX, y, 280, 26);
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
    tab->AddChild(placementDrop);
    y += 42;

    auto randomBtn = std::make_shared<UltraCanvasButton>("BARandom", panelX, y, 280, 30);
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
    tab->AddChild(randomBtn);
    y += 40;

    auto hint = std::make_shared<UltraCanvasLabel>("BAHint", panelX, y, 300, 150);
    hint->SetText(
        "The axis rescales itself to whatever data\n"
        "is loaded, snapping to round tick values.\n\n"
        "Hover a dot to see the before value, the\n"
        "after value and the change between them.");
    hint->SetFontSize(10.5f);
    hint->SetTextColor(Color(90, 90, 90, 255));
    hint->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(hint);

    return tab;
}

// =============================================================================
// TAB 3: MIN / MAX RANGE WITH PER-ROW COLOURS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildRangeTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("DumbbellRangeTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("RangeTitle", 30, 12, 700, 26);
    title->SetText("Monthly temperature range - Reykjavik");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("RangeSubtitle", 30, 40, 800, 20);
    subtitle->SetText("Average daily low and high, with each row coloured by how cold the month gets");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("RangeDumbbell", 20, 70, 860, 560);
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
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("RangeNotes", 900, 90, 320, 380);
    notes->SetText(
        "Per-row colours\n\n"
        "Every DumbbellDataPoint carries its own\n"
        "color1, color2 and lineColor. Leave them\n"
        "transparent and the chart falls back to the\n"
        "colours set with SetDefaultColors().\n\n"
        "Here each month is tinted by its average\n"
        "high, so the chart doubles as a heat scale.\n\n"
        "Value label placement\n\n"
        "This tab pins the labels outside the dots.\n"
        "Each label sits beyond the outer edge of\n"
        "its own dot, so they never overlap however\n"
        "narrow the range gets.\n\n"
        "Zero on the axis\n\n"
        "SetIncludeZeroInRange(true) keeps the\n"
        "freezing point inside the plotted range even\n"
        "though no month averages exactly zero.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 4: GAP ANALYSIS - SORTING AND CLICK CALLBACKS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildGapAnalysisTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("DumbbellGapTab", 0, 0,
                                                      kTabContentW, kTabContentH);

    auto title = std::make_shared<UltraCanvasLabel>("GapTitle", 30, 12, 700, 26);
    title->SetText("Gap analysis: median hourly pay by sector");
    title->SetFontSize(17);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("GapSubtitle", 30, 40, 800, 20);
    subtitle->SetText("Sort the rows to bring the widest gaps to the top, and click a row to inspect it");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitle);

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("GapDumbbell", 20, 70, 860, 540);
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
    tab->AddChild(chart);

    int panelX = 900;
    int y = 78;

    auto sortLabel = std::make_shared<UltraCanvasLabel>("GapSortLabel", panelX, y, 300, 18);
    sortLabel->SetText("Row order");
    sortLabel->SetFontSize(11);
    tab->AddChild(sortLabel);
    y += 20;

    auto sortDrop = std::make_shared<UltraCanvasDropdown>("GapSort", panelX, y, 300, 26);
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
    tab->AddChild(sortDrop);
    y += 46;

    auto selectionTitle = std::make_shared<UltraCanvasLabel>("GapSelTitle", panelX, y, 300, 20);
    selectionTitle->SetText("Selected row");
    selectionTitle->SetFontSize(12);
    selectionTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(selectionTitle);
    y += 24;

    auto selection = std::make_shared<UltraCanvasLabel>("GapSelection", panelX, y, 310, 110);
    selection->SetText("Click any row in the chart to read\nits values here.");
    selection->SetFontSize(11);
    selection->SetTextColor(Color(70, 70, 70, 255));
    selection->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(selection);
    y += 124;

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

    auto notes = std::make_shared<UltraCanvasLabel>("GapNotes", panelX, y, 320, 300);
    notes->SetText(
        "Sorting\n\n"
        "SetSortOrder() reorders the drawn rows\n"
        "without touching the data source, so the\n"
        "indices handed to the callbacks always\n"
        "refer to the original rows.\n\n"
        "Callbacks\n\n"
        "onRowClick fires for a click anywhere in a\n"
        "row; onDotClick fires as well when the\n"
        "click lands on one of the two dots and\n"
        "reports which one.\n\n"
        "Figures are illustrative.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(90, 90, 90, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDumbbellChartExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "DumbbellChartExamples", 0, 0, kTabContentW + 20, kTabContentH + 60);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "DumbbellChartTabs", 0, 10, kTabContentW + 10, kTabContentH + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("Social Media by Age & Gender", BuildSocialMediaTab());
    tabs->AddTab("Before / After", BuildBeforeAfterTab());
    tabs->AddTab("Min / Max Range", BuildRangeTab());
    tabs->AddTab("Gap Analysis & Sorting", BuildGapAnalysisTab());
    tabs->SetActiveTab(0);

    container->AddChild(tabs);
    return container;
}

} // namespace UltraCanvas
