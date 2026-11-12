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
    // A tab shorter than its chart and notes scrolls rather than cutting them.
    ContainerStyle scrolling = tab->GetContainerStyle();
    scrolling.autoShowScrollbars = true;
    tab->SetContainerStyle(scrolling);

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
                             "SocialTitle", Tr("Social media usage by age and gender"),
                             "SocialSubtitle", Tr("Share of UK adults aged 16-40 who use each platform regularly"));

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("SocialMediaDumbbell",
                                                            0, 0, 880, 590);
    chart->SetLegendLabels(Tr("Men"), Tr("Women"));
    chart->SetAxisLabel(Tr("Percentage using the platform regularly (%)"));
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
        chart->AddDataPoint(row.label, row.men, row.women, Tr("Men"), Tr("Women"));
    }

    auto notes = AddChartRow(tab, "SocialRow", chart, "SocialNotes");
    AddNoteHeading(notes, "SocialNotesReadTitle", Tr("Reading the chart"));
    AddNote(notes, "SocialNotesRead",
            Tr("Each row is one platform / age band. The blue dot is the share of men, the pink dot "
            "the share of women, and the grey bar between them is the gender gap."));
    AddNote(notes, "SocialNotesHover",
            Tr("Hover any dot for the exact figures and the difference between the two groups."));
    AddNoteHeading(notes, "SocialNotesDataTitle", Tr("What the data shows"));
    AddNoteBullets(notes, "SocialNotesData", {
            Tr("Women lead on every platform except Facebook 36-40, where the two are level."),
            Tr("The widest gaps are on Twitter/X, where women lead by up to 27 points."),
            Tr("TikTok 16-20 has the highest single reading at 88% of women."),
            Tr("Bluesky is still marginal in every band.")});
    AddNote(notes, "SocialNotesLabels",
            Tr("Value labels are switched off on this tab: with 39 rows squeezed into the plot area the "
            "dots are too small to hold text. The chart detects this automatically when the labels "
            "are on and moves them outside the dots."));

    auto source = DemoKit::AddCaption(tab, "SocialSource",
            Tr("Source: Onward / JL Partners  -  rendered with UltraCanvasDumbbellChart"));
    source->SetFontSize(9.5f);

    return tab;
}

// =============================================================================
// TAB 2: BEFORE / AFTER WITH LIVE STYLING CONTROLS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildBeforeAfterTab() {
    auto tab = CreateTabPage("DumbbellBeforeAfterTab",
                             "BATitle", Tr("Before / after: customer satisfaction by region"),
                             "BASubtitle", Tr("Satisfaction score before and after the 2026 service programme - use the controls to restyle the chart live"));

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("BeforeAfterDumbbell",
                                                            0, 0, 860, 560);
    chart->SetLegendLabels(Tr("Before"), Tr("After"));
    chart->SetAxisLabel(Tr("Satisfaction score (0-100)"));
    chart->SetDefaultColors(Color(150, 150, 160, 255), Color(60, 160, 120, 255),
                            Color(205, 205, 205, 255));
    chart->SetValueFormat("%.0f");
    chart->SetCategoryLabelWidth(150.0f);
    chart->SetDotRadius(11.0f);
    chart->SetLineWidth(3.0f);
    chart->SetIncludeZeroInRange(false);
    chart->SetShowValueLabels(true);

    chart->AddDataPoint(Tr("North America"), 62, 78, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Latin America"), 55, 71, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("United Kingdom"), 68, 74, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Western Europe"), 71, 83, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Eastern Europe"), 49, 66, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Middle East"), 58, 61, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Africa"), 44, 63, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("South Asia"), 52, 69, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("East Asia"), 74, 81, Tr("Before"), Tr("After"));
    chart->AddDataPoint(Tr("Oceania"), 66, 72, Tr("Before"), Tr("After"));

    // ===== CONTROL PANEL =====
    auto panel = AddChartRow(tab, "BARow", chart, "BAControls", Tr("Chart controls"));

    DemoKit::AddCaption(panel, "BADotLabel", Tr("Dot radius"));

    auto dotSlider = std::make_shared<UltraCanvasSlider>("BADotSlider", 0, 0, 280, 24);
    dotSlider->SetRange(3.0f, 20.0f);
    dotSlider->SetStep(1.0f);
    dotSlider->SetValue(11.0f);
    dotSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    dotSlider->onValueChanged = [chart](float value) { chart->SetDotRadius(value); };
    DemoKit::AddStretched(panel, dotSlider);

    DemoKit::AddCaption(panel, "BALineLabel", Tr("Connector width"));

    auto lineSlider = std::make_shared<UltraCanvasSlider>("BALineSlider", 0, 0, 280, 24);
    lineSlider->SetRange(1.0f, 10.0f);
    lineSlider->SetStep(1.0f);
    lineSlider->SetValue(3.0f);
    lineSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    lineSlider->onValueChanged = [chart](float value) { chart->SetLineWidth(value); };
    DemoKit::AddStretched(panel, lineSlider);

    DemoKit::AddCaption(panel, "BALabelWidth", Tr("Category label column width"));

    auto labelWidthSlider = std::make_shared<UltraCanvasSlider>("BALabelWidthSlider", 0, 0, 280, 24);
    labelWidthSlider->SetRange(60.0f, 260.0f);
    labelWidthSlider->SetStep(5.0f);
    labelWidthSlider->SetValue(150.0f);
    labelWidthSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    labelWidthSlider->onValueChanged = [chart](float value) { chart->SetCategoryLabelWidth(value); };
    DemoKit::AddStretched(panel, labelWidthSlider);

    auto gridCheck = std::make_shared<UltraCanvasCheckbox>("BAGrid", 0, 0, 280, 24, Tr("Show grid"));
    gridCheck->SetChecked(true);
    gridCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowGrid(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, gridCheck);

    auto valueCheck = std::make_shared<UltraCanvasCheckbox>("BAValues", 0, 0, 280, 24, Tr("Show value labels"));
    valueCheck->SetChecked(true);
    valueCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowValueLabels(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, valueCheck);

    auto legendCheck = std::make_shared<UltraCanvasCheckbox>("BALegend", 0, 0, 280, 24, Tr("Show legend"));
    legendCheck->SetChecked(true);
    legendCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowLegend(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, legendCheck);

    auto categoryCheck = std::make_shared<UltraCanvasCheckbox>("BACategories", 0, 0, 280, 24,
                                                               Tr("Show category labels"));
    categoryCheck->SetChecked(true);
    categoryCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowCategoryLabels(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, categoryCheck);

    DemoKit::AddCaption(panel, "BAPlacementLabel", Tr("Value label placement"));

    auto placementDrop = std::make_shared<UltraCanvasDropdown>("BAPlacement", 0, 0, 280, 26);
    placementDrop->AddItem(Tr("Auto (inside when they fit)"));
    placementDrop->AddItem(Tr("Always inside the dots"));
    placementDrop->AddItem(Tr("Always outside the dots"));
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
    randomBtn->SetText(Tr("Generate random data"));
    randomBtn->SetFontSize(11);
    randomBtn->onClick = [chart]() {
        const std::string regions[] = {
            Tr("North America"), Tr("Latin America"), Tr("United Kingdom"), Tr("Western Europe"),
            Tr("Eastern Europe"), Tr("Middle East"), Tr("Africa"), Tr("South Asia"), Tr("East Asia"), Tr("Oceania")
        };

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> before(35.0, 75.0);
        std::uniform_real_distribution<double> delta(-8.0, 25.0);

        chart->ClearData();
        for (const std::string& region : regions) {
            double b = before(gen);
            double a = std::max(0.0, std::min(100.0, b + delta(gen)));
            chart->AddDataPoint(region, b, a, Tr("Before"), Tr("After"));
        }
    };
    DemoKit::AddStretched(panel, randomBtn);

    AddNote(panel, "BAHint",
            Tr("The axis rescales itself to whatever data is loaded, snapping to round tick values."),
            Color(90, 90, 90, 255));
    AddNote(panel, "BAHint2",
            Tr("Hover a dot to see the before value, the after value and the change between them."),
            Color(90, 90, 90, 255));

    return tab;
}

// =============================================================================
// TAB 3: MIN / MAX RANGE WITH PER-ROW COLOURS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildRangeTab() {
    auto tab = CreateTabPage("DumbbellRangeTab",
                             "RangeTitle", Tr("Monthly temperature range - Reykjavik"),
                             "RangeSubtitle", Tr("Average daily low and high, with each row coloured by how cold the month gets"));

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("RangeDumbbell", 0, 0, 860, 560);
    chart->SetLegendLabels(Tr("Average low"), Tr("Average high"));
    chart->SetAxisLabel(Tr("Temperature (degrees Celsius)"));
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

    struct MonthRange { std::string month; double low; double high; };
    const MonthRange months[] = {
        {Tr("January"), -3, 2},  {Tr("February"), -2, 3},  {Tr("March"), -2, 4},
        {Tr("April"), 1, 6},     {Tr("May"), 4, 10},       {Tr("June"), 7, 13},
        {Tr("July"), 9, 14},     {Tr("August"), 8, 14},    {Tr("September"), 6, 11},
        {Tr("October"), 2, 7},   {Tr("November"), -1, 4},  {Tr("December"), -2, 3},
    };

    for (const auto& m : months) {
        DumbbellDataPoint point(m.month, m.low, m.high, Tr("Low"), Tr("High"));
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
    AddNoteHeading(notes, "RangeNotesColoursTitle", Tr("Per-row colours"));
    AddNote(notes, "RangeNotesColours",
            Tr("Every DumbbellDataPoint carries its own color1, color2 and lineColor. Leave them "
            "transparent and the chart falls back to the colours set with SetDefaultColors()."));
    AddNote(notes, "RangeNotesHeat",
            Tr("Here each month is tinted by its average high, so the chart doubles as a heat scale."));
    AddNoteHeading(notes, "RangeNotesPlacementTitle", Tr("Value label placement"));
    AddNote(notes, "RangeNotesPlacement",
            Tr("This tab pins the labels outside the dots. Each label sits beyond the outer edge of "
            "its own dot, so they never overlap however narrow the range gets."));
    AddNoteHeading(notes, "RangeNotesZeroTitle", Tr("Zero on the axis"));
    AddNote(notes, "RangeNotesZero",
            Tr("SetIncludeZeroInRange(true) keeps the freezing point inside the plotted range even "
            "though no month averages exactly zero."));

    return tab;
}

// =============================================================================
// TAB 4: GAP ANALYSIS - SORTING AND CLICK CALLBACKS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildGapAnalysisTab() {
    auto tab = CreateTabPage("DumbbellGapTab",
                             "GapTitle", Tr("Gap analysis: median hourly pay by sector"),
                             "GapSubtitle", Tr("Sort the rows to bring the widest gaps to the top, and click a row to inspect it"));

    auto chart = std::make_shared<UltraCanvasDumbbellChart>("GapDumbbell", 0, 0, 860, 540);
    chart->SetLegendLabels(Tr("Women"), Tr("Men"));
    chart->SetAxisLabel(Tr("Median hourly pay (GBP)"));
    chart->SetDefaultColors(Color(214, 96, 130, 255), Color(70, 130, 190, 255),
                            Color(198, 198, 198, 255));
    chart->SetValueFormat("%.2f");
    chart->SetCategoryLabelWidth(190.0f);
    chart->SetDotRadius(9.0f);
    chart->SetLineWidth(3.0f);
    chart->SetShowValueLabels(true);
    chart->SetValueLabelPlacement(DumbbellValueLabelPlacement::Outside);
    chart->SetSortOrder(DumbbellSortOrder::RangeDescending);

    chart->AddDataPoint(Tr("Finance & insurance"), 21.40, 29.85, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Information & comms"), 22.10, 26.90, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Construction"), 16.05, 19.60, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Manufacturing"), 15.20, 18.75, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Professional services"), 19.30, 25.10, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Education"), 17.85, 20.40, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Health & social work"), 16.90, 18.20, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Retail & wholesale"), 12.40, 14.05, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Transport & storage"), 13.75, 15.30, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Hospitality"), 11.20, 11.95, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Public administration"), 18.60, 19.45, Tr("Women"), Tr("Men"));
    chart->AddDataPoint(Tr("Arts & recreation"), 14.30, 16.85, Tr("Women"), Tr("Men"));

    auto panel = AddChartRow(tab, "GapRow", chart, "GapPanel");

    DemoKit::AddCaption(panel, "GapSortLabel", Tr("Row order"));

    auto sortDrop = std::make_shared<UltraCanvasDropdown>("GapSort", 0, 0, 300, 26);
    sortDrop->AddItem(Tr("Widest gap first"));
    sortDrop->AddItem(Tr("Narrowest gap first"));
    sortDrop->AddItem(Tr("Highest men's pay first"));
    sortDrop->AddItem(Tr("Highest women's pay first"));
    sortDrop->AddItem(Tr("Sector name (A-Z)"));
    sortDrop->AddItem(Tr("As entered"));
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

    auto selectionTitle = DemoKit::AddHeading(panel, "GapSelTitle", Tr("Selected row"));
    selectionTitle->SetFontSize(12);

    auto selection = AddNote(panel, "GapSelection",
                             Tr("Click any row in the chart to read its values here."));
    selection->SetFontSize(11);

    // onRowClick reports the index into the data source, unaffected by sorting
    auto source = chart->GetDumbbellDataSource();
    chart->onRowClick = [selection, source](size_t rowIndex) {
        if (!source || rowIndex >= source->GetPointCount()) return;
        const auto& point = source->GetDumbbellPoint(rowIndex);

        auto number = [](double value, const char* format) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), format, value);
            return std::string(buffer);
        };
        selection->SetText(TrF("{0}\n\n{1}: {2}\n{3}: {4}\nGap: {5} ({6}%)",
                               point.categoryLabel,
                               point.label1, number(point.value1, "%.2f"),
                               point.label2, number(point.value2, "%.2f"),
                               number(point.GetRange(), "%.2f"),
                               number(point.value2 != 0.0 ? point.GetRange() / point.value2 * 100.0 : 0.0,
                                      "%.1f")));
    };

    const Color notesColor(90, 90, 90, 255);
    AddNoteHeading(panel, "GapNotesSortTitle", Tr("Sorting"));
    AddNote(panel, "GapNotes",
            Tr("SetSortOrder() reorders the drawn rows without touching the data source, so the "
            "indices handed to the callbacks always refer to the original rows."), notesColor);
    AddNoteHeading(panel, "GapNotesCallbacksTitle", Tr("Callbacks"));
    AddNote(panel, "GapNotesCallbacks",
            Tr("onRowClick fires for a click anywhere in a row; onDotClick fires as well when the "
            "click lands on one of the two dots and reports which one."), notesColor);
    AddNote(panel, "GapNotesFigures", Tr("Figures are illustrative."), notesColor);

    return tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDumbbellChartExamples() {
    auto container = DemoKit::CreatePage("DumbbellChartExamples", Tr("Dumbbell Chart Examples"),
        Tr("Connected dot charts: a gender comparison, a before/after study with live styling controls, "
        "a min/max range with per-row colours and a gap analysis with sorting and click callbacks"));

    auto card = DemoKit::AddCard(container, "DumbbellChartCard");
    card->SetPadding(4);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "DumbbellChartTabs", 0, 0, kTabContentW + 10, kTabContentH + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab(Tr("Social Media by Age & Gender"), BuildSocialMediaTab());
    tabs->AddTab(Tr("Before / After"), BuildBeforeAfterTab());
    tabs->AddTab(Tr("Min / Max Range"), BuildRangeTab());
    tabs->AddTab(Tr("Gap Analysis & Sorting"), BuildGapAnalysisTab());
    tabs->SetActiveTab(0);

    DemoKit::AddFixed(card, tabs);
    return container;
}

} // namespace UltraCanvas
