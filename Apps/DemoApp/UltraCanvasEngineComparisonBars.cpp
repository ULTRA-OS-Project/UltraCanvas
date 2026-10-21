// Apps/DemoApp/UltraCanvasEngineComparisonBars.cpp
// "Bars & Flows" tabs of the Chart Engine Comparison page: the waterfall,
// diverging (Likert, tornado / opposing bars), population pyramid and dumbbell
// charts, each legacy chart beside its engine port on the same data.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasEngineComparison.h"
#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "Plugins/Charts/UltraCanvasPopulationChart.h"
#include "Plugins/Charts/UltraCanvasDumbbellChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineWaterfallChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDivergingBarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDumbbellChart.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasButton.h"
#include <map>

namespace UltraCanvas {
namespace EngineComparison {

namespace {

using EngineCharts = std::vector<std::shared_ptr<UltraCanvasChartEngineElement>>;

// ---- shared controls --------------------------------------------------------

std::shared_ptr<UltraCanvasDropdown> MakeDropdown(const std::string& id, int width,
                                                  const std::vector<std::string>& items,
                                                  int selected) {
    auto dropdown = std::make_shared<UltraCanvasDropdown>(id, width, 24);
    for (const std::string& item : items) dropdown->AddItem(item);
    dropdown->SetSelectedIndex(selected);
    return dropdown;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id, int width,
                                                  const std::string& text, bool checked,
                                                  std::function<void(bool)> onChange) {
    auto checkbox = std::make_shared<UltraCanvasCheckbox>(id, width, 24, text);
    checkbox->SetChecked(checked);
    checkbox->onStateChanged = [onChange](CheckedState, CheckedState state) {
        onChange(state == CheckedState::Checked);
    };
    return checkbox;
}

// Theme dropdown + replay button for the engine charts: the legacy charts
// have no themes and no replayable animation, which is part of the point.
void AddEngineExtras(UltraCanvasContainer& strip, const std::string& idPrefix,
                     const EngineCharts& charts) {
    AddStripLabel(strip, idPrefix + "ThemeLabel", "Engine theme:", 90);
    const std::vector<std::string> names = ChartThemes::Names();
    auto theme = MakeDropdown(idPrefix + "Theme", 120, names, 0);
    theme->onSelectionChanged = [charts, names](int index, const DropdownItem&) {
        if (index < 0 || static_cast<size_t>(index) >= names.size()) return;
        for (const auto& chart : charts) chart->SetTheme(names[static_cast<size_t>(index)]);
    };
    AddToStrip(strip, theme);

    auto replay = std::make_shared<UltraCanvasButton>(idPrefix + "Replay", 0, 0, 120, 24);
    replay->SetText("Replay animation");
    replay->onClick = [charts]() {
        for (const auto& chart : charts) chart->StartEngineAnimation(0.7f);
    };
    AddToStrip(strip, replay);
}

void AddOrientation(UltraCanvasContainer& strip, const std::string& idPrefix,
                    const EngineCharts& charts, bool horizontalFirst) {
    AddStripLabel(strip, idPrefix + "OrientLabel", "Engine projection:", 115);
    auto orientation = MakeDropdown(idPrefix + "Orientation", 110,
                                    horizontalFirst
                                        ? std::vector<std::string>{"Horizontal", "Vertical"}
                                        : std::vector<std::string>{"Vertical", "Horizontal"},
                                    0);
    orientation->onSelectionChanged = [charts, horizontalFirst](int index, const DropdownItem&) {
        const bool horizontal = (index == 0) == horizontalFirst;
        for (const auto& chart : charts) {
            chart->SetProjectionKind(horizontal ? ChartProjectionKind::Horizontal
                                                : ChartProjectionKind::Vertical);
        }
    };
    AddToStrip(strip, orientation);
}

// =============================================================================
// WATERFALL
// =============================================================================

struct WaterfallDataset {
    std::string title;
    struct Step { const char* label; double value; bool subtotal; bool total; };
    std::vector<Step> steps;
};

const std::vector<WaterfallDataset>& WaterfallDatasets() {
    static const std::vector<WaterfallDataset> datasets = {
        {"Revenue Flow ($K)",
         {{"Start", 500, false, false}, {"Q1", 150, false, false}, {"Q2", 120, false, false},
          {"Q3", 180, false, false}, {"Q4", 90, false, false}, {"Return", -25, false, false},
          {"Discount", -40, false, false}, {"Total", 0, true, false}}},
        {"Cash Flow ($K)",
         {{"Start", 100, false, false}, {"Revenue", 1200, false, false},
          {"Costs", -800, false, false}, {"Q1", 0, true, false},
          {"Marketing", -150, false, false}, {"R&D", -120, false, false},
          {"Q2", 0, true, false}, {"Equipment", -80, false, false},
          {"Final", 0, false, true}}},
        {"Performance Impact",
         {{"Start", 100, false, false}, {"Training", 25, false, false},
          {"Tools", 15, false, false}, {"Process", 12, false, false},
          {"Turnover", -18, false, false}, {"Downtime", -8, false, false},
          {"Final", 0, false, true}}},
    };
    return datasets;
}

void LoadWaterfall(const WaterfallDataset& data,
                   const std::shared_ptr<UltraCanvasWaterfallChartElement>& legacy,
                   const std::shared_ptr<UltraCanvasEngineWaterfallChart>& engine) {
    auto source = std::make_shared<WaterfallChartDataVector>();
    std::vector<WaterfallStep> steps;
    for (const WaterfallDataset::Step& s : data.steps) {
        source->AddWaterfallPoint(s.label, s.value, s.subtotal, s.total);
        steps.emplace_back(s.label, s.value,
                           s.total ? WaterfallStepKind::Total
                                   : s.subtotal ? WaterfallStepKind::Subtotal
                                                : WaterfallStepKind::Change);
    }
    legacy->SetWaterfallDataSource(source);
    legacy->SetChartTitle(data.title);
    engine->SetSteps(steps);
    engine->SetChartTitle(data.title);
}

std::shared_ptr<UltraCanvasUIElement> BuildWaterfallTab() {
    auto legacy = CreateWaterfallChartElement("CmpWaterfallLegacy", 0, 0, 400, 300);
    legacy->SetPositiveBarColor(Color(76, 175, 80, 255));
    legacy->SetNegativeBarColor(Color(244, 67, 54, 255));
    legacy->SetSubtotalBarColor(Color(255, 193, 7, 255));
    legacy->SetTotalBarColor(Color(33, 150, 243, 255));
    legacy->SetConnectionLineStyle(UltraCanvasWaterfallChartElement::ConnectionStyle::Solid);
    legacy->SetShowValueLabels(true);

    auto engine = CreateEngineWaterfallChart("CmpWaterfallEngine");
    engine->SetIncreaseColor(Color(76, 175, 80, 255));
    engine->SetDecreaseColor(Color(244, 67, 54, 255));
    engine->SetSubtotalColor(Color(255, 193, 7, 255));
    engine->SetTotalColor(Color(33, 150, 243, 255));
    engine->SetConnectorStyle(WaterfallConnectorStyle::Solid);
    engine->SetShowValueLabels(true);
    engine->SetValueFormat("$", "K");

    LoadWaterfall(WaterfallDatasets()[0], legacy, engine);

    auto strip = MakeControlStrip("CmpWaterfall");
    AddStripLabel(*strip, "CmpWaterfallDataLabel", "Data:", 36);
    std::vector<std::string> titles;
    for (const WaterfallDataset& d : WaterfallDatasets()) titles.push_back(d.title);
    auto dataset = MakeDropdown("CmpWaterfallData", 150, titles, 0);
    dataset->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        if (index >= 0 && static_cast<size_t>(index) < WaterfallDatasets().size()) {
            LoadWaterfall(WaterfallDatasets()[static_cast<size_t>(index)], legacy, engine);
        }
    };
    AddToStrip(*strip, dataset);

    AddStripLabel(*strip, "CmpWaterfallStyleLabel", "Bars:", 34);
    auto barStyle = MakeDropdown("CmpWaterfallStyle", 100, {"Standard", "Rounded", "Gradient"}, 0);
    barStyle->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        using Legacy = UltraCanvasWaterfallChartElement::BarStyle;
        const Legacy l = index == 1 ? Legacy::Rounded : index == 2 ? Legacy::Gradient
                                                                   : Legacy::Standard;
        legacy->SetBarStyle(l);
        engine->SetBarStyle(index == 1 ? WaterfallBarStyle::Rounded
                                       : index == 2 ? WaterfallBarStyle::Gradient
                                                    : WaterfallBarStyle::Standard);
    };
    AddToStrip(*strip, barStyle);

    AddStripLabel(*strip, "CmpWaterfallConnLabel", "Connectors:", 72);
    auto connectors = MakeDropdown("CmpWaterfallConn", 90, {"Solid", "Dashed", "Dotted", "Off"}, 0);
    connectors->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        using Legacy = UltraCanvasWaterfallChartElement::ConnectionStyle;
        const Legacy l = index == 1 ? Legacy::Dashed : index == 2 ? Legacy::Dotted
                         : index == 3 ? Legacy::NoneStyle : Legacy::Solid;
        legacy->SetConnectionLineStyle(l);
        engine->SetConnectorStyle(index == 1 ? WaterfallConnectorStyle::Dashed
                                  : index == 2 ? WaterfallConnectorStyle::Dotted
                                  : index == 3 ? WaterfallConnectorStyle::Hidden
                                               : WaterfallConnectorStyle::Solid);
    };
    AddToStrip(*strip, connectors);

    AddToStrip(*strip, MakeCheckbox("CmpWaterfallLabels", 110, "Value labels", true,
        [legacy, engine](bool on) {
            legacy->SetShowValueLabels(on);
            engine->SetShowValueLabels(on);
        }));
    AddToStrip(*strip, MakeCheckbox("CmpWaterfallRunning", 120, "Running totals", false,
        [legacy, engine](bool on) {
            legacy->SetShowCumulativeLabels(on);
            engine->SetShowCumulativeLabels(on);
        }));
    AddOrientation(*strip, "CmpWaterfall", {engine}, false);
    AddEngineExtras(*strip, "CmpWaterfall", {engine});

    return MakeComparisonTab(
        "CmpWaterfall",
        "Floating bars walk the running total; subtotal and total bars stand on zero. The port adds a measured value axis\n"
        "with the format, solved (decluttered) value labels, a legend of the bar kinds, tooltips, themes and the Horizontal projection.",
        "UltraCanvasWaterfallChartElement", legacy,
        "UltraCanvasEngineWaterfallChart", engine, strip);
}

// =============================================================================
// DIVERGING: LIKERT
// =============================================================================

struct LikertData {
    std::vector<std::string> rows;
    std::vector<std::vector<double>> values;    // SD, D, N, A, SA per row
};

const LikertData& SurveyData() {
    static const LikertData data = {
        {"Question 1", "Question 2", "Question 3", "Question 4", "Question 5"},
        {{12, 18, 10, 34, 26}, {30, 25, 15, 20, 10}, {8, 14, 22, 30, 26},
         {20, 30, 12, 24, 14}, {5, 10, 18, 37, 30}}};
    return data;
}

const std::vector<std::string>& LikertNames() {
    static const std::vector<std::string> names = {
        "Strongly Disagree", "Disagree", "Neutral", "Agree", "Strongly Agree"};
    return names;
}

const std::vector<Color>& LikertColors() {
    static const std::vector<Color> colors = {
        Color(0xff, 0x50, 0x33, 255), Color(244, 165, 130, 255), Color(220, 220, 220, 255),
        Color(4, 0xd1, 0xd6, 255), Color(33, 102, 172, 255)};
    return colors;
}

std::shared_ptr<UltraCanvasDivergingBarChart> MakeLegacyDiverging(const std::string& id,
                                                                  DivergingChartStyle style,
                                                                  const std::string& title) {
    auto chart = std::make_shared<UltraCanvasDivergingBarChart>(id, 0, 0, 400, 300);
    chart->SetChartTitle(title);
    chart->SetChartStyle(style);
    chart->SetBarHeight(0.85f);
    chart->SetCenterGap(5.0f);
    chart->SetShowGrid(true);
    chart->SetShowCenterLine(true);
    chart->SetShowRowLabels(true);
    std::vector<DivergingCategory> categories;
    for (size_t c = 0; c < LikertNames().size(); ++c) {
        categories.emplace_back(LikertNames()[c], LikertColors()[c], c >= 3);
    }
    chart->SetCategories(categories);
    const LikertData& data = SurveyData();
    for (size_t r = 0; r < data.rows.size(); ++r) {
        std::map<std::string, float> values;
        for (size_t c = 0; c < LikertNames().size(); ++c) {
            values[LikertNames()[c]] = static_cast<float>(data.values[r][c]);
        }
        chart->AddDataRow(data.rows[r], values);
    }
    return chart;
}

std::shared_ptr<UltraCanvasEngineDivergingBarChart> MakeEngineDiverging(
        const std::string& id, DivergingBarStyle style, const std::string& title) {
    auto chart = CreateEngineDivergingBarChart(id);
    chart->SetChartTitle(title);
    std::vector<DivergingBarCategory> categories;
    for (size_t c = 0; c < LikertNames().size(); ++c) {
        // The legacy chart has no neutral side: it files Neutral as negative.
        categories.emplace_back(LikertNames()[c], LikertColors()[c],
                                c == 2 ? DivergingSide::Neutral
                                       : c < 2 ? DivergingSide::Negative : DivergingSide::Positive);
    }
    chart->SetCategories(categories);
    const LikertData& data = SurveyData();
    std::vector<DivergingBarRow> rows;
    for (size_t r = 0; r < data.rows.size(); ++r) rows.emplace_back(data.rows[r], data.values[r]);
    chart->SetRows(rows);
    chart->SetStyle(style);
    return chart;
}

std::shared_ptr<UltraCanvasUIElement> BuildLikertTab() {
    auto legacy = MakeLegacyDiverging("CmpLikertLegacy", DivergingChartStyle::LikertScale,
                                      "Likert Scale Survey Responses");
    auto engine = MakeEngineDiverging("CmpLikertEngine", DivergingBarStyle::Likert,
                                      "Likert Scale Survey Responses");
    engine->SetShowValueLabels(true);

    auto strip = MakeControlStrip("CmpLikert");
    AddToStrip(*strip, MakeCheckbox("CmpLikertSplit", 150, "Split neutral at 0", true,
        [engine](bool on) { engine->SetSplitNeutral(on); }));
    AddToStrip(*strip, MakeCheckbox("CmpLikertPercent", 130, "Share of row", true,
        [engine](bool on) { engine->SetPercentOfRow(on); }));
    AddToStrip(*strip, MakeCheckbox("CmpLikertLabels", 110, "Value labels", true,
        [legacy, engine](bool on) {
            legacy->SetShowValueLabels(on);
            engine->SetShowValueLabels(on);
        }));
    AddToStrip(*strip, MakeCheckbox("CmpLikertGrid", 70, "Grid", true,
        [legacy, engine](bool on) {
            legacy->SetShowGrid(on);
            engine->SetGridAxis(on ? 0 : static_cast<size_t>(-1));
        }));
    AddOrientation(*strip, "CmpLikert", {engine}, true);
    AddEngineExtras(*strip, "CmpLikert", {engine});

    return MakeComparisonTab(
        "CmpLikert",
        "Answer shares per question, disagreement left and agreement right. The legacy chart scales each side to 100% of its own half;\n"
        "the port puts both sides on one percent axis, splits the neutral share across zero, labels the segments and toggles answers from the legend.",
        "UltraCanvasDivergingBarChart (LikertScale)", legacy,
        "UltraCanvasEngineDivergingBarChart (Likert)", engine, strip);
}

// =============================================================================
// DIVERGING: TORNADO AND OPPOSING BARS
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildTornadoTab() {
    auto legacy = MakeLegacyDiverging("CmpTornadoLegacy", DivergingChartStyle::TornadoChart,
                                      "Tornado Chart Analysis");
    auto engine = MakeEngineDiverging("CmpTornadoEngine", DivergingBarStyle::Tornado,
                                      "Tornado Chart Analysis");
    engine->SetShowValueLabels(true);

    auto strip = MakeControlStrip("CmpTornado");
    AddStripLabel(*strip, "CmpTornadoStyleLabel", "Style:", 40);
    auto styleDropdown = MakeDropdown("CmpTornadoStyle", 130, {"Tornado (net)", "Opposing bars"}, 0);
    styleDropdown->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        legacy->SetChartStyle(index == 1 ? DivergingChartStyle::OpposingBars
                                         : DivergingChartStyle::TornadoChart);
        engine->SetStyle(index == 1 ? DivergingBarStyle::OpposingBars : DivergingBarStyle::Tornado);
    };
    AddToStrip(*strip, styleDropdown);
    AddToStrip(*strip, MakeCheckbox("CmpTornadoSort", 160, "Sort by magnitude", false,
        [engine](bool on) { engine->SetSortByMagnitude(on); }));
    AddToStrip(*strip, MakeCheckbox("CmpTornadoLabels", 110, "Value labels", true,
        [engine](bool on) { engine->SetShowValueLabels(on); }));
    AddOrientation(*strip, "CmpTornado", {engine}, true);
    AddEngineExtras(*strip, "CmpTornado", {engine});

    return MakeComparisonTab(
        "CmpTornado",
        "The same survey reduced to one net bar per question (agree minus disagree), or to the two side totals as opposing half-height bars.\n"
        "The port reads magnitudes on both sides of a symmetric axis, can sort the rows by size, and labels the bar ends through the label plan.",
        "UltraCanvasDivergingBarChart (Tornado)", legacy,
        "UltraCanvasEngineDivergingBarChart (Tornado)", engine, strip);
}

// =============================================================================
// POPULATION PYRAMID
// =============================================================================

struct PopulationDataset {
    std::string name;
    double legacyAxisMax;
    std::vector<DivergingPopulationGroup> groups;
};

const std::vector<PopulationDataset>& PopulationDatasets() {
    static const std::vector<PopulationDataset> datasets = {
        {"Germany", 4.0,
         {{"0-4", 2.0, 1.9}, {"5-9", 2.1, 2.0}, {"10-14", 2.1, 2.0}, {"15-19", 2.0, 1.9},
          {"20-24", 2.3, 2.2}, {"25-29", 2.8, 2.7}, {"30-34", 3.0, 2.9}, {"35-39", 3.2, 3.1},
          {"40-44", 2.9, 2.8}, {"45-49", 2.8, 2.7}, {"50-54", 3.4, 3.3}, {"55-59", 3.6, 3.5},
          {"60-64", 3.0, 3.0}, {"65-69", 2.7, 2.8}, {"70-74", 2.2, 2.4}, {"75-79", 1.8, 2.1},
          {"80-84", 1.2, 1.7}, {"85-89", 0.7, 1.2}, {"90-94", 0.2, 0.5}, {"95-99", 0.05, 0.15},
          {"100+", 0.01, 0.04}}},
        {"Russia", 7.0,
         {{"0-4", 3.8, 3.6}, {"5-9", 4.2, 4.0}, {"10-14", 4.0, 3.8}, {"15-19", 3.5, 3.3},
          {"20-24", 3.8, 3.6}, {"25-29", 5.5, 5.3}, {"30-34", 6.2, 6.1}, {"35-39", 6.3, 6.5},
          {"40-44", 5.2, 5.8}, {"45-49", 4.5, 5.3}, {"50-54", 4.8, 6.0}, {"55-59", 5.0, 6.5},
          {"60-64", 4.2, 5.8}, {"65-69", 3.5, 5.2}, {"70-74", 2.8, 4.8}, {"75-79", 1.8, 3.9},
          {"80-84", 0.9, 2.5}, {"85-89", 0.4, 1.4}, {"90-94", 0.1, 0.5}, {"95-99", 0.03, 0.15},
          {"100+", 0.01, 0.05}}},
    };
    return datasets;
}

const Color kMaleColor(100, 150, 200, 255);
const Color kFemaleColor(220, 120, 140, 255);

void LoadPopulation(const PopulationDataset& data,
                    const std::shared_ptr<UltraCanvasPopulationChart>& legacy,
                    const std::shared_ptr<UltraCanvasEngineDivergingBarChart>& engine) {
    legacy->ClearAgeGroups();
    for (const DivergingPopulationGroup& g : data.groups) {
        legacy->AddAgeGroup(g.label, g.left, g.right);
    }
    legacy->SetTitle(data.name);
    legacy->SetMaxAxisValue(data.legacyAxisMax);

    engine->SetPopulationData(data.groups, "Males", kMaleColor, "Females", kFemaleColor);
    engine->SetChartTitle(data.name + " 2024");
}

std::shared_ptr<UltraCanvasUIElement> BuildPopulationTab() {
    auto legacy = std::make_shared<UltraCanvasPopulationChart>("CmpPopulationLegacy", 0, 0, 400, 500);
    legacy->SetSubtitle("2024");
    legacy->SetAxisLabel("Population (millions)");
    legacy->SetMaleColor(kMaleColor);
    legacy->SetFemaleColor(kFemaleColor);
    legacy->SetMaleSurplusColor(Color(70, 110, 160, 255));
    legacy->SetFemaleSurplusColor(Color(190, 90, 110, 255));
    legacy->EnableGrid(true);
    legacy->EnableAxisLabels(true);
    legacy->EnableCenterLine(true);
    legacy->SetBarSpacing(2);
    legacy->EnableAverageAgeLine(true);
    legacy->SetAverageAgeLineColors(Color(30, 80, 160, 255), Color(170, 40, 80, 255));
    legacy->AddLegendItem("Males", kMaleColor);
    legacy->AddLegendItem("Male surplus", Color(70, 110, 160, 255));
    legacy->AddLegendItem("Females", kFemaleColor);
    legacy->AddLegendItem("Female surplus", Color(190, 90, 110, 255));
    legacy->AddLegendLineItem("Avg age males", Color(30, 80, 160, 255));
    legacy->AddLegendLineItem("Avg age females", Color(170, 40, 80, 255));
    legacy->EnableLegend(true);
    legacy->SetLegendPosition("top-right");

    auto engine = CreateEngineDivergingBarChart("CmpPopulationEngine");
    engine->SetValueFormat("", "M", false, 1);
    engine->SetValueAxisTitle("Population (millions)");
    engine->SetBarFill(0.9);

    LoadPopulation(PopulationDatasets()[0], legacy, engine);

    auto strip = MakeControlStrip("CmpPopulation");
    AddStripLabel(*strip, "CmpPopulationDataLabel", "Country:", 55);
    auto country = MakeDropdown("CmpPopulationData", 100, {"Germany", "Russia"}, 0);
    country->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        if (index >= 0 && static_cast<size_t>(index) < PopulationDatasets().size()) {
            LoadPopulation(PopulationDatasets()[static_cast<size_t>(index)], legacy, engine);
        }
    };
    AddToStrip(*strip, country);
    AddToStrip(*strip, MakeCheckbox("CmpPopulationSurplus", 130, "Surplus shading", true,
        [engine](bool on) { engine->SetHighlightSurplus(on); }));
    AddToStrip(*strip, MakeCheckbox("CmpPopulationAverage", 120, "Average ages", true,
        [legacy, engine](bool on) {
            legacy->EnableAverageAgeLine(on);
            engine->SetShowAverageLines(on);
        }));
    AddToStrip(*strip, MakeCheckbox("CmpPopulationLabels", 110, "Value labels", false,
        [engine](bool on) { engine->SetShowValueLabels(on); }));
    AddEngineExtras(*strip, "CmpPopulation", {engine});

    return MakeComparisonTab(
        "CmpPopulation",
        "Age groups bottom-up, males left and females right. The legacy chart draws the surplus beyond the full bar (overstating it);\n"
        "the port keeps each bar at its true length, shades the surplus inside it, and draws the average ages as engine limiters on the age axis.",
        "UltraCanvasPopulationChart", legacy,
        "UltraCanvasEngineDivergingBarChart (PopulationPyramid)", engine, strip);
}

// =============================================================================
// DUMBBELL
// =============================================================================

struct DumbbellSample { const char* region; double before; double after; };

const DumbbellSample kSatisfaction[] = {
    {"North America", 62, 78}, {"Latin America", 55, 71}, {"United Kingdom", 68, 74},
    {"Western Europe", 71, 83}, {"Eastern Europe", 49, 66}, {"Middle East", 58, 61},
    {"Africa", 44, 63}, {"South Asia", 52, 69}, {"East Asia", 74, 81}, {"Oceania", 66, 72},
};

std::shared_ptr<UltraCanvasUIElement> BuildDumbbellTab() {
    const Color beforeColor(150, 150, 160, 255);
    const Color afterColor(60, 160, 120, 255);
    const Color connector(205, 205, 205, 255);

    auto legacy = std::make_shared<UltraCanvasDumbbellChart>("CmpDumbbellLegacy", 0, 0, 400, 400);
    legacy->SetChartTitle("Customer satisfaction by region");
    legacy->SetLegendLabels("Before", "After");
    legacy->SetAxisLabel("Satisfaction score (0-100)");
    legacy->SetDefaultColors(beforeColor, afterColor, connector);
    legacy->SetValueFormat("%.0f");
    legacy->SetCategoryLabelWidth(110.0f);
    legacy->SetDotRadius(9.0f);
    legacy->SetLineWidth(3.0f);
    legacy->SetIncludeZeroInRange(false);
    legacy->SetShowValueLabels(true);

    auto engine = CreateEngineDumbbellChart("CmpDumbbellEngine");
    engine->SetChartTitle("Customer satisfaction by region");
    engine->SetGroups({DumbbellGroup("Before", beforeColor), DumbbellGroup("After", afterColor)});
    engine->SetConnectorColor(connector);
    engine->SetValueAxisTitle("Satisfaction score (0-100)");
    engine->SetValueFormat("", "", false, 0);
    engine->SetDotRadius(9.0f);
    engine->SetConnectorWidth(3.0f);
    engine->SetShowValueLabels(true);

    for (const DumbbellSample& s : kSatisfaction) {
        legacy->AddDataPoint(s.region, s.before, s.after, "Before", "After");
        engine->AddRow(s.region, s.before, s.after);
    }

    auto strip = MakeControlStrip("CmpDumbbell");
    AddStripLabel(*strip, "CmpDumbbellSortLabel", "Sort:", 34);
    // The two enums list the same orders in the same sequence.
    auto sort = MakeDropdown("CmpDumbbellSort", 150,
                             {"As added", "Label A-Z", "Label Z-A", "Before ascending",
                              "Before descending", "After ascending", "After descending",
                              "Gap ascending", "Gap descending"}, 0);
    sort->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        if (index < 0 || index > 8) return;
        legacy->SetSortOrder(static_cast<DumbbellSortOrder>(index));
        engine->SetSortOrder(static_cast<DumbbellRowSort>(index));
    };
    AddToStrip(*strip, sort);
    AddToStrip(*strip, MakeCheckbox("CmpDumbbellLabels", 110, "Value labels", true,
        [legacy, engine](bool on) {
            legacy->SetShowValueLabels(on);
            engine->SetShowValueLabels(on);
        }));
    AddToStrip(*strip, MakeCheckbox("CmpDumbbellDiff", 110, "Differences", false,
        [engine](bool on) { engine->SetShowDifferenceLabels(on); }));
    AddToStrip(*strip, MakeCheckbox("CmpDumbbellZero", 110, "Include zero", false,
        [legacy, engine](bool on) {
            legacy->SetIncludeZeroInRange(on);
            engine->SetIncludeZero(on);
        }));
    AddOrientation(*strip, "CmpDumbbell", {engine}, true);
    AddEngineExtras(*strip, "CmpDumbbell", {engine});

    return MakeComparisonTab(
        "CmpDumbbell",
        "Before and after per region, joined by the gap. The port takes any number of groups per row, labels the outer dots on their\n"
        "outward side through the label plan, can show the gap itself, toggles groups from the legend and turns the rows into columns.",
        "UltraCanvasDumbbellChart", legacy,
        "UltraCanvasEngineDumbbellChart", engine, strip);
}

} // namespace

std::vector<ComparisonTab> BarFamilyTabs() {
    return {
        {"Waterfall", BuildWaterfallTab},
        {"Diverging (Likert)", BuildLikertTab},
        {"Tornado", BuildTornadoTab},
        {"Population Pyramid", BuildPopulationTab},
        {"Dumbbell", BuildDumbbellTab},
    };
}

} // namespace EngineComparison
} // namespace UltraCanvas
