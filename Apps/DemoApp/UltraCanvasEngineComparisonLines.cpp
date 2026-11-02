// Apps/DemoApp/UltraCanvasEngineComparisonLines.cpp
// "Lines & Points" family of the Chart Engine Comparison page: the legacy
// line, area, cumulative flow, scatter and bubble charts next to their engine
// ports (UltraCanvasEngineLineChart, UltraCanvasEngineScatterChart), each pair
// fed the same data the legacy chart's own demo page uses.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): checkboxes and
//                  buttons as wide as their text, the old width as a minimum
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasEngineComparison.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
#include "Plugins/Charts/UltraCanvasBubbleChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include <random>

namespace UltraCanvas {
namespace EngineComparison {

namespace {

using EngineChart = std::shared_ptr<UltraCanvasChartEngineElement>;

// ---- shared data ------------------------------------------------------------

const std::vector<std::string> kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const std::vector<double> kMonthlySales = {45000, 52000, 48000, 61000, 55000, 67000,
                                           71000, 69000, 58000, 63000, 72000, 78000};
const std::vector<std::string> kQuarters = {"Q1 2023", "Q2 2023", "Q3 2023", "Q4 2023",
                                            "Q1 2024", "Q2 2024", "Q3 2024", "Q4 2024"};
const std::vector<double> kQuarterlyRevenue = {85000, 92000, 78000, 105000,
                                               98000, 112000, 125000, 138000};

std::shared_ptr<ChartDataVector> MakeDataSource(const std::vector<std::string>& labels,
                                                const std::vector<double>& values) {
    auto data = std::make_shared<ChartDataVector>();
    std::vector<ChartDataPoint> points;
    for (size_t i = 0; i < values.size(); ++i) {
        points.emplace_back(static_cast<double>(i + 1), values[i], 0.0, labels[i], values[i]);
    }
    data->LoadFromArray(points);
    return data;
}

// ---- controls ---------------------------------------------------------------
// The strips are siblings of the charts, never their parents, so capturing a
// chart's shared_ptr in a control callback is ownership, not a cycle.

// Controls are as wide as their text, at least the width they were given:
// a longer caption (a translation) widens the control instead of being cut.
void SetMinimumWidth(UltraCanvasUIElement& element, int minWidth) {
    CSSLayout::BoxConstraints limits = element.boxConstraints.value_or(CSSLayout::BoxConstraints{});
    limits.minWidth = CSSLayout::Dimension::Px(static_cast<float>(minWidth));
    element.boxConstraints = limits;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheck(const std::string& id, const std::string& text,
                                               bool checked, int width,
                                               std::function<void(bool)> onChange) {
    auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 24);
    SetMinimumWidth(*box, width);
    box->SetText(text);
    box->SetChecked(checked);
    box->onStateChanged = [onChange](CheckedState, CheckedState state) {
        onChange(state == CheckedState::Checked);
    };
    return box;
}

std::shared_ptr<UltraCanvasDropdown> MakeChoice(const std::string& id,
                                                const std::vector<std::string>& items,
                                                int selected, int width,
                                                std::function<void(int)> onChange) {
    auto dropdown = std::make_shared<UltraCanvasDropdown>(id, width, 24);
    for (const std::string& item : items) dropdown->AddItem(item);
    dropdown->SetSelectedIndex(selected);
    dropdown->onSelectionChanged = [onChange](int index, const DropdownItem&) { onChange(index); };
    return dropdown;
}

// Projection, theme and animation replay - the engine-only options every tab
// offers, appended after the tab's own controls.
void AddEngineControls(UltraCanvasContainer& strip, const std::string& prefix,
                       const EngineChart& engine) {
    AddStripLabel(strip, prefix + "ProjLabel", "Projection:", 70);
    AddToStrip(strip, MakeChoice(prefix + "Projection", {"Vertical", "Horizontal", "Polar"}, 0, 110,
        [engine](int index) {
            engine->SetProjectionKind(index == 1 ? ChartProjectionKind::Horizontal
                                      : index == 2 ? ChartProjectionKind::Polar
                                                   : ChartProjectionKind::Vertical);
        }));

    AddStripLabel(strip, prefix + "ThemeLabel", "Theme:", 46);
    const std::vector<std::string> themes = ChartThemes::Names();
    int current = 0;
    for (size_t i = 0; i < themes.size(); ++i) {
        if (themes[i] == engine->GetTheme().name) current = static_cast<int>(i);
    }
    AddToStrip(strip, MakeChoice(prefix + "Theme", themes, current, 110,
        [engine, themes](int index) {
            if (index >= 0 && static_cast<size_t>(index) < themes.size()) {
                engine->SetTheme(themes[static_cast<size_t>(index)]);
            }
        }));

    auto replay = std::make_shared<UltraCanvasButton>(prefix + "Replay", 0, 0, 0, 24);
    SetMinimumWidth(*replay, 120);
    replay->SetText("Replay animation");
    replay->onClick = [engine]() { engine->StartEngineAnimation(0.8f); };
    AddToStrip(strip, replay);
}

// =============================================================================
// LINE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildLineTab() {
    auto legacy = CreateLineChartElement("CmpLineLegacy", 0, 0, 400, 300);
    legacy->SetDataSource(MakeDataSource(kMonths, kMonthlySales));
    legacy->SetChartTitle("Monthly Sales Trend");
    legacy->SetLineColor(Color(0, 102, 204, 255));
    legacy->SetLineWidth(3.0f);
    legacy->SetShowDataPoints(true);
    legacy->SetPointColor(Color(255, 99, 71, 255));
    legacy->SetPointRadius(5.0f);
    legacy->SetSmoothingEnabled(true);
    legacy->SetEnableTooltips(true);
    legacy->SetShowValueLabels(true);
    legacy->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

    auto engine = CreateEngineLineChart("CmpLineEngine");
    engine->SetChartTitle("Monthly Sales Trend");
    engine->SetCategories(kMonths);
    engine->SetSeries({{"Sales", kMonthlySales, Color(0, 102, 204, 255)}});
    engine->SetLineWidth(3.0f);
    engine->SetMarkerColor(Color(255, 99, 71, 255));
    engine->SetMarkerRadius(5.0f);
    engine->SetLineShape(ChartLineShape::Curved);
    engine->SetShowValueLabels(true);
    engine->SetValueFormat("$", "", true);
    engine->SetCategoryPadding(0.4);

    auto strip = MakeControlStrip("CmpLine");
    AddStripLabel(*strip, "CmpLineShapeLabel", "Line:", 34);
    AddToStrip(*strip, MakeChoice("CmpLineShape", {"Straight", "Curved", "Step"}, 1, 100,
        [legacy, engine](int index) {
            legacy->SetSmoothingEnabled(index == 1);    // the legacy chart has no steps
            engine->SetLineShape(index == 0 ? ChartLineShape::Straight
                                 : index == 1 ? ChartLineShape::Curved
                                              : ChartLineShape::StepMiddle);
        }));
    AddToStrip(*strip, MakeCheck("CmpLinePoints", "Points", true, 70,
        [legacy, engine](bool on) { legacy->SetShowDataPoints(on); engine->SetShowMarkers(on); }));
    AddToStrip(*strip, MakeCheck("CmpLineLabels", "Value labels", true, 110,
        [legacy, engine](bool on) { legacy->SetShowValueLabels(on); engine->SetShowValueLabels(on); }));
    AddEngineControls(*strip, "CmpLine", engine);

    return MakeComparisonTab(
        "CmpLine",
        "Monthly sales as a smoothed line with points and value labels. The port draws the curve as a monotone cubic that never\n"
        "overshoots a month, declutters the value labels through the label plan, and follows the projection - Polar is a polar line chart.",
        "UltraCanvasLineChartElement", legacy, "UltraCanvasEngineLineChart", engine, strip);
}

// =============================================================================
// AREA
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildAreaTab() {
    const Color teal(0, 150, 136, 255);
    auto legacy = CreateAreaChartElement("CmpAreaLegacy", 0, 0, 400, 300);
    legacy->SetDataSource(MakeDataSource(kQuarters, kQuarterlyRevenue));
    legacy->SetChartTitle("Quarterly Revenue Growth");
    legacy->SetFillColor(Color(0, 150, 136, 120));
    legacy->SetLineColor(teal);
    legacy->SetLineWidth(3.0f);
    legacy->SetShowDataPoints(true);
    legacy->SetPointColor(Color(255, 87, 34, 255));
    legacy->SetPointRadius(4.0f);
    legacy->SetFillGradientEnabled(true);
    legacy->SetGradientColors(Color(0, 150, 136, 180), Color(0, 150, 136, 40));
    legacy->SetSmoothingEnabled(true);
    legacy->SetShowValueLabels(true);
    legacy->SetEnableTooltips(true);
    legacy->SetXAxisLabelMode(XAxisLabelMode::DataLabel);

    auto engine = CreateEngineLineChart("CmpAreaEngine");
    engine->SetChartTitle("Quarterly Revenue Growth");
    engine->SetMode(EngineLineChartMode::Area);
    engine->SetCategories(kQuarters);
    engine->SetSeries({{"Revenue", kQuarterlyRevenue, teal}});
    engine->SetLineWidth(3.0f);
    engine->SetMarkerColor(Color(255, 87, 34, 255));
    engine->SetGradientFill(true);
    engine->SetAreaOpacity(0.55f);
    engine->SetLineShape(ChartLineShape::Curved);
    engine->SetShowValueLabels(true);
    engine->SetValueFormat("$", "", true);

    auto strip = MakeControlStrip("CmpArea");
    AddToStrip(*strip, MakeCheck("CmpAreaSmooth", "Smooth", true, 76,
        [legacy, engine](bool on) {
            legacy->SetSmoothingEnabled(on);
            engine->SetLineShape(on ? ChartLineShape::Curved : ChartLineShape::Straight);
        }));
    AddToStrip(*strip, MakeCheck("CmpAreaGradient", "Gradient", true, 86,
        [legacy, engine](bool on) { legacy->SetFillGradientEnabled(on); engine->SetGradientFill(on); }));
    AddToStrip(*strip, MakeCheck("CmpAreaPoints", "Points", true, 70,
        [legacy, engine](bool on) { legacy->SetShowDataPoints(on); engine->SetShowMarkers(on); }));
    AddToStrip(*strip, MakeCheck("CmpAreaLabels", "Value labels", true, 110,
        [legacy, engine](bool on) { legacy->SetShowValueLabels(on); engine->SetShowValueLabels(on); }));
    AddEngineControls(*strip, "CmpArea", engine);

    return MakeComparisonTab(
        "CmpArea",
        "Quarterly revenue as a gradient-filled area. The port fills from the zero line through BuildAreaOutlines, so the same\n"
        "area becomes a filled band under Horizontal and a filled polar area under Polar; hover a quarter for its tooltip.",
        "UltraCanvasAreaChartElement", legacy, "UltraCanvasEngineLineChart (Area)", engine, strip);
}

// =============================================================================
// CUMULATIVE FLOW
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildCumulativeFlowTab() {
    const std::vector<std::string> months = {"Month 1", "Month 2", "Month 3", "Month 4", "Month 5",
                                             "Month 6", "Month 7", "Month 8", "Month 9"};
    const std::vector<std::pair<std::string, std::vector<double>>> stages = {
        {"To do", {17, 15, 12, 12, 10, 5, 2, 1, 1}},
        {"Doing", {3, 3, 3, 3, 3, 5, 5, 5, 4}},
        {"QA",    {0, 2, 3, 2, 2, 3, 3, 3, 2}},
        {"Done",  {0, 0, 2, 3, 5, 7, 10, 11, 13}}};

    const CumulativeFlowChartStyle olive = CumulativeFlowStyles::CreateOlive();
    auto legacy = CreateCumulativeFlowChartElement("CmpFlowLegacy", 0, 0, 400, 300);
    legacy->SetStyle(olive);
    for (const auto& stage : stages) legacy->AddStage(stage.first, stage.second);
    legacy->SetPeriodLabels(months);
    legacy->AddLeadTimeAnnotation();
    legacy->AddCycleTimeAnnotation();
    legacy->AddWipAnnotation();

    // The same stages, first stage on top - the CFD convention - and the
    // olive report palette, so the two read the same.
    std::vector<EngineLineSeries> series;
    for (size_t i = 0; i < stages.size(); ++i) {
        EngineLineSeries s;
        s.name = stages[i].first;
        s.values = stages[i].second;
        if (!olive.palette.empty()) s.color = olive.palette[i % olive.palette.size()];
        series.push_back(s);
    }
    auto engine = CreateEngineLineChart("CmpFlowEngine");
    engine->SetChartTitle("Cumulative flow");
    engine->SetMode(EngineLineChartMode::StackedArea);
    engine->SetReverseStackOrder(true);
    engine->SetCategories(months);
    engine->SetSeries(series);
    engine->SetAreaOpacity(1.0f);
    engine->SetLineWidth(2.2f);
    engine->SetMarkerRadius(3.4f);
    engine->SetMarkerColor(Color(255, 255, 255, 255));
    engine->SetLegendPosition(ChartLegendPosition::RightCenter);
    engine->AddCumulativeFlowAnnotations();

    auto strip = MakeControlStrip("CmpFlow");
    AddToStrip(*strip, MakeCheck("CmpFlowSpans", "Lead / cycle / WIP", true, 150,
        [legacy, engine](bool on) {
            legacy->ClearAnnotations();
            engine->ClearSeriesSpans();
            if (on) {
                legacy->AddLeadTimeAnnotation();
                legacy->AddCycleTimeAnnotation();
                legacy->AddWipAnnotation();
                engine->AddCumulativeFlowAnnotations();
            }
        }));
    AddToStrip(*strip, MakeCheck("CmpFlowMarkers", "Markers", true, 80,
        [legacy, engine](bool on) {
            legacy->EditStyle().showMarkers = on;
            legacy->StyleChanged();
            engine->SetShowMarkers(on);
        }));
    AddToStrip(*strip, MakeCheck("CmpFlowPercent", "100% (engine)", false, 116,
        [engine](bool on) {
            engine->SetMode(on ? EngineLineChartMode::PercentStackedArea
                               : EngineLineChartMode::StackedArea);
        }));
    AddEngineControls(*strip, "CmpFlow", engine);

    return MakeComparisonTab(
        "CmpFlow",
        "A cumulative flow diagram: per-stage bands stacked over the months, first stage on top. The port is the line chart's\n"
        "StackedArea mode with SetReverseStackOrder; lead time, cycle time and WIP are engine span annotations the value labels steer around.",
        "UltraCanvasCumulativeFlowChartElement", legacy, "UltraCanvasEngineLineChart (StackedArea)",
        engine, strip);
}

// =============================================================================
// SCATTER
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildScatterTab() {
    // Marketing spend vs sales with noise - seeded, so both charts get the
    // very same 50 points on every run.
    std::mt19937 gen(20261020u);
    std::uniform_real_distribution<> xDist(1000, 10000);
    std::uniform_real_distribution<> noise(-5000, 5000);
    auto data = std::make_shared<ChartDataVector>();
    EngineScatterGroup group;
    group.name = "Campaigns";
    group.color = Color(255, 140, 0, 255);
    std::vector<ChartDataPoint> points;
    for (int i = 0; i < 50; ++i) {
        const double spend = xDist(gen);
        const double sales = spend * 3.2 + 15000 + noise(gen);
        points.emplace_back(spend, sales, 0.0, "Point " + std::to_string(i + 1), sales);
        group.points.emplace_back(spend, sales, std::nan(""), std::nan(""),
                                  "Point " + std::to_string(i + 1));
    }
    data->LoadFromArray(points);

    auto legacy = CreateScatterPlotElement("CmpScatterLegacy", 0, 0, 400, 300);
    legacy->SetDataSource(data);
    legacy->SetChartTitle("Marketing Spend vs Sales");
    legacy->SetPointColor(Color(255, 140, 0, 255));
    legacy->SetPointSize(8.0f);
    legacy->SetPointShape(UltraCanvasScatterPlotElement::PointShape::Circle);
    legacy->SetEnableTooltips(true);
    legacy->SetShowTrendLine(true);
    legacy->SetShowCorrelationInfo(true);

    auto engine = CreateEngineScatterChart("CmpScatterEngine");
    engine->SetChartTitle("Marketing Spend vs Sales");
    engine->SetGroups({group});
    engine->SetMarkerRadius(4.5f);
    engine->SetAxisTitles("Marketing spend", "Sales");
    engine->SetXFormat("$", "", true);
    engine->SetYFormat("$", "", true);
    engine->SetShowTrendLine(true);
    engine->SetShowCorrelationInfo(true);

    auto strip = MakeControlStrip("CmpScatter");
    AddStripLabel(*strip, "CmpScatterShapeLabel", "Shape:", 42);
    AddToStrip(*strip, MakeChoice("CmpScatterShape", {"Circle", "Square", "Triangle", "Diamond"}, 0, 100,
        [legacy, engine, group](int index) {
            using Legacy = UltraCanvasScatterPlotElement::PointShape;
            static const Legacy legacyShapes[] = {Legacy::Circle, Legacy::Square,
                                                  Legacy::Triangle, Legacy::Diamond};
            static const ChartMarkerShape engineShapes[] = {
                ChartMarkerShape::Circle, ChartMarkerShape::Square,
                ChartMarkerShape::TriangleUp, ChartMarkerShape::Diamond};
            const size_t i = static_cast<size_t>(std::clamp(index, 0, 3));
            legacy->SetPointShape(legacyShapes[i]);
            EngineScatterGroup reshaped = group;
            reshaped.marker = engineShapes[i];
            engine->SetGroups({reshaped});
        }));
    AddToStrip(*strip, MakeCheck("CmpScatterTrend", "Trend line", true, 96,
        [legacy, engine](bool on) {
            legacy->SetShowTrendLine(on);
            legacy->SetShowCorrelationInfo(on);
            engine->SetShowTrendLine(on);
        }));
    AddToStrip(*strip, MakeCheck("CmpScatterEllipse", "95% ellipse (engine)", false, 160,
        [engine](bool on) { engine->SetShowConfidenceEllipses(on, 0.95); }));
    AddEngineControls(*strip, "CmpScatter", engine);

    return MakeComparisonTab(
        "CmpScatter",
        "Fifty campaigns with a least-squares trend line and its y = ax + b / r / r\xC2\xB2 readout. The port rules both directions from\n"
        "the axis ticks, places the readout through the label plan and can wrap the group in an engine confidence-ellipse highlight.",
        "UltraCanvasScatterPlotElement", legacy, "UltraCanvasEngineScatterChart", engine, strip);
}

// =============================================================================
// BUBBLE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildBubbleTab() {
    struct Row { double cost, safety, health, food; const char* name; };
    const Row rows[] = {{9, 79, 46.1, 82, "Mexico"},   {44, 78, 41.0, 76, "Egypt"},
                        {52, 46, 42.5, 71, "China"},   {60, 15, 10.5, 22, "France"},
                        {57, 11, 8.0, 16, "Australia"}, {52, 7, 5.1, 10, "Great Britain"}};
    const std::vector<Color> ramp = {Color(201, 218, 240, 255), Color(148, 160, 173, 255),
                                     Color(128, 128, 128, 255)};

    auto legacy = CreateBubbleChartElement("CmpBubbleLegacy", 0, 0, 400, 300);
    EngineScatterGroup group;
    group.name = "Countries";
    for (const Row& r : rows) {
        legacy->AddBubble(r.cost, r.safety, r.health, r.food, r.name);
        group.points.emplace_back(r.cost, r.safety, r.health, r.food, r.name);
    }
    legacy->SetChartTitle("Concerns when traveling");
    legacy->SetSizeScale(BubbleSizeScale::Area);
    legacy->SetRadiusRange(8.0f, 34.0f);
    legacy->SetColorMode(BubbleColorMode::ColormapByValue);
    legacy->SetCustomColormap(ramp);
    legacy->SetNameLabelMode(BubbleNameLabelMode::Auto);
    legacy->SetNameLabelColor(Color(130, 150, 175, 255));
    legacy->SetValueLabelMode(BubbleValueLabelMode::Hidden);
    legacy->SetShowSizeLegend(true);
    legacy->SetSizeLegendTitle("Health");
    legacy->SetSizeLegendValues({46.1, 20.5, 5.1});
    legacy->SetValueDecimals(1);
    legacy->SetFootnote("Darkness relates to Food concerns");
    legacy->SetEnableTooltips(true);

    auto engine = CreateEngineScatterChart("CmpBubbleEngine");
    engine->SetChartTitle("Concerns when traveling");
    engine->SetGroups({group});
    engine->SetRadiusRange(8.0f, 34.0f);
    engine->SetSizeScale(EngineBubbleScale::Area);
    engine->SetCustomColormap(ramp);
    engine->SetMarkerOpacity(1.0f);
    engine->SetMarkerOutline(Color(255, 255, 255, 255), 1.0f);
    engine->SetAxisTitles("Cost", "Safety");
    engine->SetPointLabelMode(EngineScatterLabelMode::Inside);
    engine->SetShowSizeLegend(true, "Health", {46.1, 20.5, 5.1});

    auto strip = MakeControlStrip("CmpBubble");
    AddStripLabel(*strip, "CmpBubbleScaleLabel", "Size:", 32);
    AddToStrip(*strip, MakeChoice("CmpBubbleScale", {"Area", "Diameter"}, 0, 96,
        [legacy, engine](int index) {
            legacy->SetSizeScale(index == 1 ? BubbleSizeScale::Diameter : BubbleSizeScale::Area);
            engine->SetSizeScale(index == 1 ? EngineBubbleScale::Diameter : EngineBubbleScale::Area);
        }));
    AddStripLabel(*strip, "CmpBubbleNamesLabel", "Names:", 44);
    AddToStrip(*strip, MakeChoice("CmpBubbleNames", {"Auto", "Hidden", "Outside"}, 0, 96,
        [legacy, engine](int index) {
            legacy->SetNameLabelMode(index == 1 ? BubbleNameLabelMode::Hidden
                                     : index == 2 ? BubbleNameLabelMode::BelowBubble
                                                  : BubbleNameLabelMode::Auto);
            engine->SetPointLabelMode(index == 1 ? EngineScatterLabelMode::Hidden
                                      : index == 2 ? EngineScatterLabelMode::Outside
                                                   : EngineScatterLabelMode::Inside);
        }));
    AddStripLabel(*strip, "CmpBubbleKeyLabel", "Key (engine):", 80);
    AddToStrip(*strip, MakeChoice("CmpBubbleKey", {"Size", "Food colour bar"}, 0, 130,
        [engine](int index) {
            engine->SetShowSizeLegend(index == 0, "Health", {46.1, 20.5, 5.1});
            engine->SetShowColorBar(index == 1, "Food");
        }));
    AddEngineControls(*strip, "CmpBubble", engine);

    return MakeComparisonTab(
        "CmpBubble",
        "Four dimensions per country: cost (x), safety (y), health (bubble area) and food (darkness). The port keys the size with the\n"
        "engine's area-scaled size legend (or the food colour bar), puts names inside bubbles that fit and solves the rest outside.",
        "UltraCanvasBubbleChartElement", legacy, "UltraCanvasEngineScatterChart (bubbles)", engine, strip);
}

} // namespace

std::vector<ComparisonTab> LineFamilyTabs() {
    return {{"Line", BuildLineTab},
            {"Area", BuildAreaTab},
            {"Cumulative Flow", BuildCumulativeFlowTab},
            {"Scatter", BuildScatterTab},
            {"Bubble", BuildBubbleTab}};
}

} // namespace EngineComparison
} // namespace UltraCanvas
