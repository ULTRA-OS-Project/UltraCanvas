// Apps/DemoApp/UltraCanvasEngineComparisonPolar.cpp
// "Polar" family of the Chart Engine Comparison page: the legacy radar, radial
// bar, pie and polar charts next to their engine ports
// (UltraCanvasEngineRadarChart, UltraCanvasEngineRadialBarChart,
// UltraCanvasEnginePieChart, UltraCanvasEnginePolarChart). Every example of
// the legacy chart's own demo page is offered; picking one rebuilds BOTH
// charts from the same data, so the two sides always show the same thing.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasEngineComparison.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePolarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadialBarChart.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "Plugins/Charts/UltraCanvasPolarChart.h"
#include "Plugins/Charts/UltraCanvasRadarChartElement.h"
#include "Plugins/Charts/UltraCanvasRadialBarChart.h"
#include "Plugins/Charts/UltraCanvasPieChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "CSSLayout/CSSLayout.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace UltraCanvas {
namespace EngineComparison {

namespace {

// =============================================================================
// SHARED WIDGETRY
// =============================================================================

// A slot an example dropdown swaps a chart into, so a new example replaces
// both sides' charts instead of reconfiguring them field by field.
std::shared_ptr<UltraCanvasContainer> MakeHolder(const std::string& id) {
    auto holder = std::make_shared<UltraCanvasContainer>(id);
    holder->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    return holder;
}

void PutInHolder(UltraCanvasContainer& holder, std::shared_ptr<UltraCanvasUIElement> chart) {
    holder.ClearChildren();
    chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    holder.AddChild(chart);
}

std::shared_ptr<UltraCanvasDropdown> MakeDropdown(const std::string& id, int width,
                                                  const std::vector<std::string>& items,
                                                  int selected = 0) {
    auto dropdown = std::make_shared<UltraCanvasDropdown>(id, width, 24);
    for (const std::string& item : items) dropdown->AddItem(item);
    dropdown->SetSelectedIndex(selected);
    return dropdown;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id, const std::string& text,
                                                  bool checked, int width) {
    auto checkbox = std::make_shared<UltraCanvasCheckbox>(id, width, 24);
    checkbox->SetText(text);
    checkbox->SetChecked(checked);
    return checkbox;
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text,
                                              int width) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 24);
    button->SetText(text);
    return button;
}

// "Legacy colours" keeps each series' own colour (the comparison default);
// a theme name re-styles the engine port's furniture and series together.
// `apply` receives the theme name and whether the palette should win.
std::shared_ptr<UltraCanvasDropdown> MakeThemeDropdown(
        const std::string& id, std::function<void(const std::string&, bool)> apply) {
    std::vector<std::string> items = {"Legacy colours"};
    for (const std::string& name : ChartThemes::Names()) items.push_back(name);
    auto dropdown = MakeDropdown(id, 130, items);
    dropdown->onSelectionChanged = [apply, items](int index, const DropdownItem&) {
        if (index <= 0) apply("Light", false);
        else apply(items[static_cast<size_t>(index)], true);
    };
    return dropdown;
}

bool Checked(CheckedState state) { return state == CheckedState::Checked; }

// Display-only number formatting for the value formatters below, through the
// engine's axis formatting so both charts print the same text.
std::function<std::string(double)> UnitFormatter(int decimals, const std::string& suffix) {
    return [decimals, suffix](double v) {
        ChartAxis format;
        format.decimals = decimals;
        format.unitSuffix = suffix;
        return format.FormatValue(v);
    };
}

// =============================================================================
// RADAR
// =============================================================================

struct RadarSpec {
    std::string title;
    std::vector<EngineRadarAxis> axes;
    struct Series { std::string name; std::vector<double> values; Color fill, stroke; float width; };
    std::vector<Series> series;
    bool valueLabels = false;
};

std::vector<RadarSpec> RadarExamples() {
    std::vector<RadarSpec> specs;
    specs.push_back({"Performance Metrics: Current vs Target",
                     {{"Speed", 0, 100}, {"Quality", 0, 100}, {"Efficiency", 0, 100},
                      {"Innovation", 0, 100}, {"Collaboration", 0, 100}, {"Leadership", 0, 100}},
                     {{"Current", {85, 92, 78, 65, 88, 72}, Color(70, 130, 180, 100), Color(70, 130, 180, 255), 2.5f},
                      {"Target", {95, 95, 90, 85, 90, 85}, Color(255, 140, 0, 80), Color(255, 140, 0, 255), 2.0f}},
                     false});
    specs.push_back({"Skills Assessment (0-10 scale)",
                     {{"Programming", 0, 10}, {"Design", 0, 10}, {"Testing", 0, 10},
                      {"Documentation", 0, 10}, {"Communication", 0, 10}},
                     {{"Current Skills", {8.5, 6.5, 7.8, 5.5, 8.2}, Color(50, 205, 50, 120), Color(34, 139, 34, 255), 3.0f}},
                     false});
    specs.push_back({"Product Comparison (8 axes, 3 series)",
                     {{"Performance", 0, 100}, {"Features", 0, 100}, {"Usability", 0, 100},
                      {"Price", 0, 100}, {"Support", 0, 100}, {"Reliability", 0, 100},
                      {"Security", 0, 100}, {"Scalability", 0, 100}},
                     {{"Product A", {85, 90, 78, 60, 85, 92, 88, 82}, Color(220, 20, 60, 80), Color(220, 20, 60, 255), 2.5f},
                      {"Product B", {75, 85, 92, 80, 70, 88, 85, 90}, Color(30, 144, 255, 80), Color(30, 144, 255, 255), 2.5f},
                      {"Product C", {90, 75, 85, 70, 95, 80, 90, 85}, Color(50, 205, 50, 80), Color(50, 205, 50, 255), 2.5f}},
                     false});
    specs.push_back({"Team Comparison (value labels enabled)",
                     {{"Planning", 0, 100}, {"Execution", 0, 100}, {"Communication", 0, 100},
                      {"Quality", 0, 100}, {"Velocity", 0, 100}},
                     {{"Team Alpha", {78, 88, 70, 92, 65}, Color(138, 43, 226, 90), Color(138, 43, 226, 255), 2.5f},
                      {"Team Beta", {85, 72, 90, 80, 88}, Color(255, 99, 71, 80), Color(255, 99, 71, 255), 2.5f}},
                     true});
    return specs;
}

std::shared_ptr<UltraCanvasRadarChartElement> BuildLegacyRadar(const RadarSpec& spec) {
    auto chart = CreateRadarChartElement("CmpRadarLegacy", 0, 0, 400, 300);
    chart->SetChartTitle(spec.title);
    chart->SetGridDisplay(true, Color(220, 220, 220, 255), 1.0f);
    chart->SetAxisLabels(true, Color(60, 60, 60, 255), "Arial", 11.0f);
    chart->SetLegendDisplay(true);
    chart->SetValueLabels(spec.valueLabels);
    chart->SetAnimationEnabled(true, 900);
    for (const EngineRadarAxis& axis : spec.axes) chart->AddAxis(axis.name, axis.minValue, axis.maxValue);
    for (size_t i = 0; i < spec.series.size(); ++i) {
        const RadarSpec::Series& s = spec.series[i];
        chart->AddSeries(s.name, s.values, s.fill, s.stroke);
        chart->SetSeriesProperties(i, s.fill, s.stroke, s.width, s.fill.a / 255.0f);
    }
    return chart;
}

std::shared_ptr<UltraCanvasEngineRadarChart> BuildEngineRadar(const RadarSpec& spec) {
    auto chart = CreateEngineRadarChart("CmpRadarEngine");
    chart->SetChartTitle(spec.title);
    chart->SetShowValueLabels(spec.valueLabels);
    for (const EngineRadarAxis& axis : spec.axes) chart->AddAxis(axis.name, axis.minValue, axis.maxValue);
    for (size_t i = 0; i < spec.series.size(); ++i) {
        const RadarSpec::Series& s = spec.series[i];
        chart->AddSeries(s.name, s.values, s.stroke);
        chart->SetSeriesStyle(i, s.stroke, s.width, s.fill.a / 255.0f);
    }
    return chart;
}

std::shared_ptr<UltraCanvasUIElement> MakeRadarTab() {
    struct State {
        std::shared_ptr<UltraCanvasRadarChartElement> legacy;
        std::shared_ptr<UltraCanvasEngineRadarChart> engine;
        bool grid = true;
        int valueLabels = -1;                 // -1 = the example's own choice
        ChartPolarGridShape web = ChartPolarGridShape::Polygon;
        std::string theme = "Light";
        bool themePalette = false;
    };
    auto state = std::make_shared<State>();
    auto legacyHolder = MakeHolder("CmpRadarLegacyHolder");
    auto engineHolder = MakeHolder("CmpRadarEngineHolder");
    const std::vector<RadarSpec> examples = RadarExamples();

    auto show = [state, legacyHolder, engineHolder, examples](size_t index) {
        const RadarSpec& spec = examples[index];
        state->legacy = BuildLegacyRadar(spec);
        state->engine = BuildEngineRadar(spec);
        state->legacy->SetGridDisplay(state->grid, Color(220, 220, 220, 255), 1.0f);
        state->engine->SetShowGrid(state->grid);
        state->engine->SetPolarGridShape(state->web);
        state->engine->SetTheme(state->theme);
        state->engine->SetUseThemePalette(state->themePalette);
        if (state->valueLabels >= 0) {
            state->legacy->SetValueLabels(state->valueLabels == 1);
            state->engine->SetShowValueLabels(state->valueLabels == 1);
        }
        PutInHolder(*legacyHolder, state->legacy);
        PutInHolder(*engineHolder, state->engine);
    };
    show(2);

    auto strip = MakeControlStrip("CmpRadar");
    AddStripLabel(*strip, "CmpRadarExampleLabel", "Example:", 56);
    std::vector<std::string> names;
    for (const RadarSpec& spec : examples) names.push_back(spec.title);
    auto example = MakeDropdown("CmpRadarExample", 250, names, 2);
    example->onSelectionChanged = [show](int index, const DropdownItem&) {
        show(static_cast<size_t>(std::max(0, index)));
    };
    AddToStrip(*strip, example);

    auto grid = MakeCheckbox("CmpRadarGrid", "Grid", true, 64);
    grid->onStateChanged = [state](CheckedState, CheckedState now) {
        state->grid = Checked(now);
        state->legacy->SetGridDisplay(state->grid, Color(220, 220, 220, 255), 1.0f);
        state->engine->SetShowGrid(state->grid);
    };
    AddToStrip(*strip, grid);

    AddStripLabel(*strip, "CmpRadarWebLabel", "Engine web:", 76);
    auto web = MakeDropdown("CmpRadarWeb", 100, {"Polygon", "Circles"});
    web->onSelectionChanged = [state](int index, const DropdownItem&) {
        state->web = index == 1 ? ChartPolarGridShape::Circle : ChartPolarGridShape::Polygon;
        state->engine->SetPolarGridShape(state->web);
    };
    AddToStrip(*strip, web);

    auto values = MakeCheckbox("CmpRadarValues", "Value labels", false, 110);
    values->onStateChanged = [state](CheckedState, CheckedState now) {
        state->valueLabels = Checked(now) ? 1 : 0;
        state->legacy->SetValueLabels(Checked(now));
        state->engine->SetShowValueLabels(Checked(now));
    };
    AddToStrip(*strip, values);

    AddStripLabel(*strip, "CmpRadarThemeLabel", "Theme:", 48);
    AddToStrip(*strip, MakeThemeDropdown("CmpRadarTheme",
        [state](const std::string& theme, bool palette) {
            state->theme = theme;
            state->themePalette = palette;
            state->engine->SetTheme(theme);
            state->engine->SetUseThemePalette(palette);
        }));

    auto replay = MakeButton("CmpRadarReplay", "Replay animation", 130);
    replay->onClick = [state]() {
        state->legacy->StartAnimation();
        state->engine->StartEngineAnimation(0.9f);
    };
    AddToStrip(*strip, replay);

    return MakeComparisonTab(
        "CmpRadar",
        "Radar: the engine port is a cyclic category axis (the spokes) and a value axis under the Polar projection - the rim labels,\n"
        "radial rule, polygon web and spokes are engine furniture; the chart draws only the series polygons. Hover a vertex for its value.",
        "UltraCanvasRadarChartElement", legacyHolder, "UltraCanvasEngineRadarChart", engineHolder, strip);
}

// =============================================================================
// RADIAL BAR
// =============================================================================

struct RadialSeriesData {
    std::string name;
    Color color;
    std::vector<std::pair<std::string, double>> values;
};

struct RadialSpec {
    std::string title;
    std::vector<RadialSeriesData> series;
    bool lines = false;
    bool gradient = true;
    RadialBarCapStyle cap = RadialBarCapStyle::Round;
    float inner = 0.25f;
    float gap = 3.0f;
    float start = -90.0f, sweep = 360.0f;
    bool ringGuides = false;
    int ringCount = 3;
    bool peaks = false;
    bool manual = false;
    double lo = 0.0, hi = 1.0;
    float barWidth = 0.0f;
    std::string center;
    int decimals = -1;
    std::string unit;
};

std::vector<RadialSpec> RadialExamples() {
    std::vector<RadialSpec> specs;

    // Six sensors with 24 hourly readings each, from a fixed seed.
    RadialSpec sensors;
    sensors.title = "Multi-Sensor Readings (6 Sensors x 24 Hours)";
    std::mt19937 gen(42);
    struct SensorSpec { const char* name; double mean, stddev; Color color; };
    const SensorSpec sensorSpecs[] = {
        {"Sensor 01", 467.0, 100.0, Color(139, 0, 0, 255)},
        {"Sensor 02", 258.0, 80.0, Color(178, 34, 34, 255)},
        {"Sensor 03", 993.0, 200.0, Color(220, 20, 60, 255)},
        {"Sensor 04", 50.0, 20.0, Color(255, 105, 180, 255)},
        {"Sensor 05", 650.0, 150.0, Color(255, 182, 193, 255)},
        {"Sensor 06", 985.0, 100.0, Color(255, 160, 122, 255)}};
    for (const SensorSpec& s : sensorSpecs) {
        std::normal_distribution<> dist(s.mean, s.stddev);
        RadialSeriesData series{s.name, s.color, {}};
        for (int i = 0; i < 24; ++i) {
            series.values.emplace_back("T" + std::to_string(i + 1), std::max(5.0, dist(gen)));
        }
        sensors.series.push_back(series);
    }
    sensors.center = "Sensors";
    sensors.peaks = true;
    specs.push_back(sensors);

    RadialSpec ages;
    ages.title = "Median Age by Country (Radial Lines per Continent)";
    ages.series = {
        {"North America", Color(33, 150, 243, 255),
         {{"United States", 38.5}, {"Canada", 41.8}, {"Mexico", 29.2}, {"Guatemala", 22.1},
          {"Costa Rica", 32.6}, {"Cuba", 42.1}, {"Jamaica", 31.4}, {"Panama", 29.8}}},
        {"South America", Color(255, 193, 7, 255),
         {{"Brazil", 33.2}, {"Argentina", 31.9}, {"Chile", 35.4}, {"Peru", 28.0},
          {"Colombia", 31.0}, {"Ecuador", 27.2}, {"Uruguay", 35.0}, {"Bolivia", 25.4}}},
        {"Europe", Color(3, 169, 244, 255),
         {{"Germany", 47.8}, {"France", 41.7}, {"United Kingdom", 40.6}, {"Italy", 46.5},
          {"Spain", 44.9}, {"Poland", 41.9}, {"Netherlands", 42.8}, {"Greece", 45.3},
          {"Sweden", 41.1}, {"Ireland", 37.8}}},
        {"Africa", Color(76, 175, 80, 255),
         {{"Nigeria", 18.1}, {"Ethiopia", 19.5}, {"Egypt", 24.6}, {"South Africa", 27.3},
          {"Kenya", 20.1}, {"Ghana", 21.4}, {"Morocco", 29.3}, {"Tunisia", 32.7},
          {"Senegal", 19.4}, {"Uganda", 16.7}}},
        {"Asia", Color(233, 30, 99, 255),
         {{"China", 38.4}, {"India", 28.2}, {"Japan", 48.6}, {"Indonesia", 30.2},
          {"Vietnam", 32.5}, {"Thailand", 39.0}, {"South Korea", 43.7}, {"Turkey", 32.2},
          {"Israel", 30.4}, {"Singapore", 42.2}}},
        {"Oceania", Color(156, 39, 176, 255),
         {{"Australia", 38.7}, {"New Zealand", 38.4}, {"Fiji", 28.9}, {"Samoa", 21.4},
          {"Palau", 34.5}}}};
    ages.lines = true;
    ages.gradient = false;
    ages.inner = 0.18f;
    ages.gap = 6.0f;
    ages.center = "Median Age";
    ages.decimals = 1;
    ages.unit = " yrs";
    specs.push_back(ages);

    RadialSpec rain;
    rain.title = "Monthly Rainfall (270 Degree Fan, Ring Guides, Square Caps)";
    rain.series = {{"Rainfall (mm)", Color(0, 122, 204, 255),
                    {{"Jan", 78}, {"Feb", 64}, {"Mar", 82}, {"Apr", 110}, {"May", 145},
                     {"Jun", 180}, {"Jul", 210}, {"Aug", 196}, {"Sep", 152}, {"Oct", 118},
                     {"Nov", 96}, {"Dec", 84}}}};
    rain.sweep = 270.0f;
    rain.start = -225.0f;
    rain.cap = RadialBarCapStyle::Square;
    rain.gradient = false;
    rain.ringGuides = true;
    rain.ringCount = 4;
    rain.inner = 0.3f;
    rain.center = "2026";
    rain.peaks = true;
    rain.decimals = 0;
    rain.unit = " mm";
    specs.push_back(rain);

    RadialSpec kpi;
    kpi.title = "KPI Attainment (Arrow Caps, Manual 0-150% Range)";
    kpi.series = {{"Sales", Color(15, 157, 88, 255), {{"Q1", 92}, {"Q2", 108}, {"Q3", 117}, {"Q4", 131}}},
                  {"Support", Color(66, 133, 244, 255), {{"Q1", 74}, {"Q2", 88}, {"Q3", 95}, {"Q4", 103}}},
                  {"Engineering", Color(244, 180, 0, 255), {{"Q1", 99}, {"Q2", 112}, {"Q3", 104}, {"Q4", 121}}}};
    kpi.manual = true;
    kpi.lo = 0.0;
    kpi.hi = 150.0;
    kpi.cap = RadialBarCapStyle::Arrow;
    kpi.gradient = false;
    kpi.barWidth = 5.0f;
    kpi.gap = 10.0f;
    kpi.inner = 0.28f;
    kpi.ringGuides = true;
    kpi.ringCount = 3;
    kpi.center = "KPI %";
    kpi.decimals = 0;
    kpi.unit = "%";
    specs.push_back(kpi);
    return specs;
}

EngineRadialBarCap EngineCapOf(RadialBarCapStyle cap) {
    switch (cap) {
        case RadialBarCapStyle::Butt:   return EngineRadialBarCap::Butt;
        case RadialBarCapStyle::Square: return EngineRadialBarCap::Square;
        case RadialBarCapStyle::Arrow:  return EngineRadialBarCap::Arrow;
        case RadialBarCapStyle::Round:
        default:                        return EngineRadialBarCap::Round;
    }
}

std::shared_ptr<UltraCanvasRadialBarChart> BuildLegacyRadial(const RadialSpec& spec) {
    auto chart = CreateRadialBarChart("CmpRadialLegacy", 0, 0, 400, 300);
    chart->SetChartTitle(spec.title);
    for (const RadialSeriesData& s : spec.series) chart->AddSeries(s.name, s.values, s.color);
    chart->SetBarStyle(spec.lines ? RadialBarStyle::Lines : RadialBarStyle::Bars);
    chart->SetGradientFade(spec.gradient);
    chart->SetCapStyle(spec.cap);
    chart->SetInnerRadiusFraction(spec.inner);
    chart->SetSeriesGapAngle(spec.gap);
    chart->SetSweepAngle(spec.sweep);
    chart->SetStartAngle(spec.start);
    chart->SetShowRingGuides(spec.ringGuides);
    chart->SetRingGuideCount(spec.ringCount);
    chart->SetShowPeakLabels(spec.peaks);
    if (spec.manual) chart->SetValueRange(spec.lo, spec.hi);
    if (spec.barWidth > 0.0f) chart->SetBarWidth(spec.barWidth);
    chart->SetCenterText(spec.center);
    if (spec.decimals >= 0) chart->SetValueFormatter(UnitFormatter(spec.decimals, spec.unit));
    chart->SetTooltipsEnabled(true);
    return chart;
}

std::shared_ptr<UltraCanvasEngineRadialBarChart> BuildEngineRadial(const RadialSpec& spec) {
    auto chart = CreateEngineRadialBarChart("CmpRadialEngine");
    chart->SetChartTitle(spec.title);
    for (const RadialSeriesData& s : spec.series) chart->AddSeries(s.name, s.values, s.color);
    chart->SetBarStyle(spec.lines ? EngineRadialBarStyle::Lines : EngineRadialBarStyle::Bars);
    chart->SetGradientFade(spec.gradient);
    chart->SetCapStyle(EngineCapOf(spec.cap));
    chart->SetInnerRadiusFraction(spec.inner);
    chart->SetSeriesGapAngle(spec.gap);
    chart->SetSweepAngle(spec.sweep);
    chart->SetStartAngle(spec.start);
    chart->SetShowRingGuides(spec.ringGuides);
    chart->SetRingGuideCount(spec.ringCount);
    chart->SetShowPeakLabels(spec.peaks);
    if (spec.manual) chart->SetValueRange(spec.lo, spec.hi);
    if (spec.barWidth > 0.0f) chart->SetBarWidth(spec.barWidth);
    chart->SetCenterText(spec.center);
    if (spec.decimals >= 0) chart->SetValueFormatter(UnitFormatter(spec.decimals, spec.unit));
    return chart;
}

std::shared_ptr<UltraCanvasUIElement> MakeRadialBarTab() {
    struct State {
        std::shared_ptr<UltraCanvasRadialBarChart> legacy;
        std::shared_ptr<UltraCanvasEngineRadialBarChart> engine;
        int cap = -1, gradient = -1, normalization = -1, guides = -1;   // -1 = the example's
        std::string theme = "Light";
        bool themePalette = false;
    };
    auto state = std::make_shared<State>();
    auto legacyHolder = MakeHolder("CmpRadialLegacyHolder");
    auto engineHolder = MakeHolder("CmpRadialEngineHolder");
    const std::vector<RadialSpec> examples = RadialExamples();

    auto applyCap = [state]() {
        if (state->cap < 0) return;
        const RadialBarCapStyle caps[] = {RadialBarCapStyle::Round, RadialBarCapStyle::Butt,
                                          RadialBarCapStyle::Square, RadialBarCapStyle::Arrow};
        const RadialBarCapStyle cap = caps[state->cap];
        state->legacy->SetCapStyle(cap);
        state->engine->SetCapStyle(EngineCapOf(cap));
    };
    auto applyRest = [state]() {
        if (state->gradient >= 0) {
            state->legacy->SetGradientFade(state->gradient == 1);
            state->engine->SetGradientFade(state->gradient == 1);
        }
        if (state->normalization >= 0) {
            const bool per = state->normalization == 1;
            state->legacy->SetNormalization(per ? RadialBarNormalization::PerSeries
                                                : RadialBarNormalization::Global);
            state->engine->SetNormalization(per ? EngineRadialBarNormalization::PerSeries
                                                : EngineRadialBarNormalization::Global);
        }
        if (state->guides >= 0) {
            state->legacy->SetShowRingGuides(state->guides == 1);
            state->engine->SetShowRingGuides(state->guides == 1);
        }
    };
    auto show = [state, legacyHolder, engineHolder, examples, applyCap, applyRest](size_t index) {
        state->legacy = BuildLegacyRadial(examples[index]);
        state->engine = BuildEngineRadial(examples[index]);
        state->engine->SetTheme(state->theme);
        state->engine->SetUseThemePalette(state->themePalette);
        applyCap();
        applyRest();
        PutInHolder(*legacyHolder, state->legacy);
        PutInHolder(*engineHolder, state->engine);
    };
    show(0);

    auto strip = MakeControlStrip("CmpRadial");
    AddStripLabel(*strip, "CmpRadialExampleLabel", "Example:", 56);
    std::vector<std::string> names;
    for (const RadialSpec& spec : examples) names.push_back(spec.title);
    auto example = MakeDropdown("CmpRadialExample", 280, names);
    example->onSelectionChanged = [show](int index, const DropdownItem&) {
        show(static_cast<size_t>(std::max(0, index)));
    };
    AddToStrip(*strip, example);

    auto cap = MakeDropdown("CmpRadialCap", 110, {"Round cap", "Butt cap", "Square cap", "Arrow cap"});
    cap->onSelectionChanged = [state, applyCap](int index, const DropdownItem&) {
        state->cap = std::clamp(index, 0, 3);
        applyCap();
    };
    AddToStrip(*strip, cap);

    auto gradient = MakeCheckbox("CmpRadialGradient", "Gradient fade", true, 120);
    gradient->onStateChanged = [state, applyRest](CheckedState, CheckedState now) {
        state->gradient = Checked(now) ? 1 : 0;
        applyRest();
    };
    AddToStrip(*strip, gradient);

    auto scaling = MakeDropdown("CmpRadialScale", 140, {"Global scale", "Per-series scale"});
    scaling->onSelectionChanged = [state, applyRest](int index, const DropdownItem&) {
        state->normalization = index == 1 ? 1 : 0;
        applyRest();
    };
    AddToStrip(*strip, scaling);

    auto guides = MakeCheckbox("CmpRadialGuides", "Ring guides", false, 110);
    guides->onStateChanged = [state, applyRest](CheckedState, CheckedState now) {
        state->guides = Checked(now) ? 1 : 0;
        applyRest();
    };
    AddToStrip(*strip, guides);

    AddStripLabel(*strip, "CmpRadialThemeLabel", "Theme:", 48);
    AddToStrip(*strip, MakeThemeDropdown("CmpRadialTheme",
        [state](const std::string& theme, bool palette) {
            state->theme = theme;
            state->themePalette = palette;
            state->engine->SetTheme(theme);
            state->engine->SetUseThemePalette(palette);
        }));

    auto replay = MakeButton("CmpRadialReplay", "Replay grow-out", 120);
    replay->onClick = [state]() { state->engine->StartEngineAnimation(0.8f); };
    AddToStrip(*strip, replay);

    return MakeComparisonTab(
        "CmpRadial",
        "Radial bars (rays): under the engine's Polar projection the fan's angle is the domain and the ray length the value - SetPolarAngles\n"
        "gives the sweep, SetPolarInnerRadius the base ring, the series names are the rim labels and the ring guides the value grid.",
        "UltraCanvasRadialBarChart", legacyHolder, "UltraCanvasEngineRadialBarChart", engineHolder, strip);
}

// =============================================================================
// PIE AND DONUT
// =============================================================================

struct PieSpec {
    std::string title;
    std::vector<std::pair<std::string, double>> slices;
    std::vector<Color> colors;
    LabelContent content = LabelContent::NamePercentage;
    LabelPosition position = LabelPosition::Auto;
    bool gradients = false;
    float border = 2.0f;
};

std::vector<PieSpec> PieExamples() {
    const std::vector<std::pair<std::string, double>> market = {
        {"Product A", 350}, {"Product B", 280}, {"Product C", 210}, {"Product D", 160}, {"Others", 120}};
    const std::vector<std::pair<std::string, double>> budget = {
        {"Salaries", 4500}, {"Marketing", 2300}, {"R&D", 1800}, {"Operations", 1200},
        {"Infrastructure", 800}, {"Other", 400}};
    const std::vector<Color> budgetColors = {Color(255, 159, 64, 255), Color(54, 162, 235, 255),
                                             Color(255, 99, 132, 255), Color(75, 192, 192, 255),
                                             Color(153, 102, 255, 255), Color(199, 199, 199, 255)};
    return {
        {"Market Share Distribution", market,
         {Color(54, 162, 235, 255), Color(255, 99, 132, 255), Color(255, 205, 86, 255),
          Color(75, 192, 192, 255), Color(153, 102, 255, 255)},
         LabelContent::NamePercentage, LabelPosition::Auto, false, 2.0f},
        {"Annual Budget Allocation", budget, budgetColors,
         LabelContent::NameValue, LabelPosition::Outside, false, 2.0f},
        {"Stylized Pie Chart (radial gradients)", budget, budgetColors,
         LabelContent::NameValue, LabelPosition::Auto, true, 3.0f}};
}

EnginePieLabelContent EngineContentOf(LabelContent content) {
    switch (content) {
        case LabelContent::Name:            return EnginePieLabelContent::Name;
        case LabelContent::Value:           return EnginePieLabelContent::Value;
        case LabelContent::Percentage:      return EnginePieLabelContent::Percentage;
        case LabelContent::NameValue:       return EnginePieLabelContent::NameValue;
        case LabelContent::ValuePercentage: return EnginePieLabelContent::ValuePercentage;
        case LabelContent::All:             return EnginePieLabelContent::All;
        case LabelContent::NamePercentage:
        default:                            return EnginePieLabelContent::NamePercentage;
    }
}

// Dropdown order: Auto, Inside, Outside, Hidden.
const LabelPosition kLegacyPositions[] = {LabelPosition::Auto, LabelPosition::Inside,
                                          LabelPosition::Outside, LabelPosition::NoLabels};
const EnginePieLabelPosition kEnginePositions[] = {
    EnginePieLabelPosition::Auto, EnginePieLabelPosition::Inside,
    EnginePieLabelPosition::Outside, EnginePieLabelPosition::Hidden};

int PositionIndexOf(LabelPosition position) {
    for (int i = 0; i < 4; ++i) if (kLegacyPositions[i] == position) return i;
    return 0;
}

std::shared_ptr<UltraCanvasPieChartElement> BuildLegacyPie(const PieSpec& spec) {
    auto data = std::make_shared<ChartDataVector>();
    std::vector<ChartDataPoint> points;
    for (size_t i = 0; i < spec.slices.size(); ++i) {
        points.emplace_back(static_cast<double>(i + 1), spec.slices[i].second, 0.0,
                            spec.slices[i].first, spec.slices[i].second);
    }
    data->LoadFromArray(points);
    auto chart = CreatePieChartElement("CmpPieLegacy", 0, 0, 400, 300);
    chart->SetChartTitle(spec.title);
    chart->SetDataSource(data);
    chart->SetColorPalette(spec.colors);
    chart->SetBorderColor(Colors::White);
    chart->SetBorderWidth(spec.border);
    chart->SetLabelContent(spec.content);
    chart->SetLabelPosition(spec.position);
    chart->SetLeaderLinesEnabled(true);
    if (spec.gradients) chart->SetAutoRadialGradients();
    chart->SetTooltipsEnabled(true);
    return chart;
}

std::shared_ptr<UltraCanvasEnginePieChart> BuildEnginePie(const PieSpec& spec) {
    auto chart = CreateEnginePieChart("CmpPieEngine");
    chart->SetChartTitle(spec.title);
    std::vector<EnginePieSlice> slices;
    for (size_t i = 0; i < spec.slices.size(); ++i) {
        slices.emplace_back(spec.slices[i].first, spec.slices[i].second,
                            i < spec.colors.size() ? spec.colors[i] : Color(0, 0, 0, 0));
    }
    chart->SetSlices(slices);
    chart->SetBorder(Colors::White, spec.border);
    chart->SetLabelContent(EngineContentOf(spec.content));
    chart->SetLabelPosition(kEnginePositions[PositionIndexOf(spec.position)]);
    chart->SetRadialGradients(spec.gradients);
    chart->SetShowLegend(true);
    return chart;
}

std::shared_ptr<UltraCanvasUIElement> MakePieTab() {
    struct State {
        std::shared_ptr<UltraCanvasPieChartElement> legacy;
        std::shared_ptr<UltraCanvasEnginePieChart> engine;
        bool donut = false;
        bool explode = false;
        int position = -1;
        std::string theme = "Light";
        bool themePalette = false;
    };
    auto state = std::make_shared<State>();
    auto legacyHolder = MakeHolder("CmpPieLegacyHolder");
    auto engineHolder = MakeHolder("CmpPieEngineHolder");
    const std::vector<PieSpec> examples = PieExamples();

    auto apply = [state]() {
        state->legacy->SetDonutMode(state->donut);
        state->engine->SetDonutMode(state->donut);
        state->engine->SetCenterText(state->donut ? "Total" : "");
        if (state->explode) {
            state->legacy->SetSliceExplosion(0, 0.15f);
            state->engine->SetSliceExplosion(0, 0.15f);
        } else {
            state->legacy->ClearSliceExplosion(0);
            state->engine->ClearSliceExplosions();
        }
        if (state->position >= 0) {
            state->legacy->SetLabelPosition(kLegacyPositions[state->position]);
            state->engine->SetLabelPosition(kEnginePositions[state->position]);
        }
    };
    auto show = [state, legacyHolder, engineHolder, examples, apply](size_t index) {
        state->legacy = BuildLegacyPie(examples[index]);
        state->engine = BuildEnginePie(examples[index]);
        state->engine->SetTheme(state->theme);
        state->engine->SetUseThemePalette(state->themePalette);
        apply();
        PutInHolder(*legacyHolder, state->legacy);
        PutInHolder(*engineHolder, state->engine);
    };
    show(0);

    auto strip = MakeControlStrip("CmpPie");
    AddStripLabel(*strip, "CmpPieExampleLabel", "Example:", 56);
    std::vector<std::string> names;
    for (const PieSpec& spec : examples) names.push_back(spec.title);
    auto example = MakeDropdown("CmpPieExample", 240, names);
    example->onSelectionChanged = [show](int index, const DropdownItem&) {
        show(static_cast<size_t>(std::max(0, index)));
    };
    AddToStrip(*strip, example);

    auto donut = MakeCheckbox("CmpPieDonut", "Donut", false, 70);
    donut->onStateChanged = [state, apply](CheckedState, CheckedState now) {
        state->donut = Checked(now);
        apply();
    };
    AddToStrip(*strip, donut);

    auto explode = MakeCheckbox("CmpPieExplode", "Explode first slice", false, 150);
    explode->onStateChanged = [state, apply](CheckedState, CheckedState now) {
        state->explode = Checked(now);
        apply();
    };
    AddToStrip(*strip, explode);

    AddStripLabel(*strip, "CmpPieLabelsLabel", "Labels:", 50);
    auto labels = MakeDropdown("CmpPieLabels", 100, {"Auto", "Inside", "Outside", "Hidden"});
    labels->onSelectionChanged = [state, apply](int index, const DropdownItem&) {
        state->position = std::clamp(index, 0, 3);
        apply();
    };
    AddToStrip(*strip, labels);

    AddStripLabel(*strip, "CmpPieThemeLabel", "Theme:", 48);
    AddToStrip(*strip, MakeThemeDropdown("CmpPieTheme",
        [state](const std::string& theme, bool palette) {
            state->theme = theme;
            state->themePalette = palette;
            state->engine->SetTheme(theme);
            state->engine->SetUseThemePalette(palette);
        }));

    auto replay = MakeButton("CmpPieReplay", "Replay sweep", 110);
    replay->onClick = [state]() { state->engine->StartEngineAnimation(0.8f); };
    AddToStrip(*strip, replay);

    return MakeComparisonTab(
        "CmpPie",
        "Pie and donut: on the engine a pie is the Polar projection of one 100% stacked bar - slices are ring sectors from BuildBarOutline,\n"
        "outside labels ride the solved label plan (the centre text is an obstacle), and clicking a legend entry drops the slice from the shares.",
        "UltraCanvasPieChartElement", legacyHolder, "UltraCanvasEnginePieChart", engineHolder, strip);
}

// =============================================================================
// POLAR
// =============================================================================

// One example, described once and built into both charts.
struct PolarSpec {
    std::string title;
    bool categorical = false;
    std::vector<std::string> categories;
    bool betweenSpokes = false;
    std::vector<PolarSeries> series;          // the legacy model; the engine is mapped from it
    bool manualRange = false;
    double lo = 0.0, hi = 1.0;
    double tickInterval = 0.0;
    int tickCount = 0;
    float angleInterval = 0.0f;
    float zeroAngle = -90.0f;
    bool counterClockwise = false;
    std::string unit;
    std::vector<PolarRadialBand> bands;
    PolarStackMode stack = PolarStackMode::NoStacking;
    float columnWidth = 0.85f;
    std::vector<Color> palette;
};

EnginePolarSeriesType EngineTypeOf(PolarSeriesType type) {
    switch (type) {
        case PolarSeriesType::Spline:     return EnginePolarSeriesType::Spline;
        case PolarSeriesType::Area:       return EnginePolarSeriesType::Area;
        case PolarSeriesType::SplineArea: return EnginePolarSeriesType::SplineArea;
        case PolarSeriesType::Scatter:    return EnginePolarSeriesType::Scatter;
        case PolarSeriesType::Column:     return EnginePolarSeriesType::Column;
        case PolarSeriesType::Line:
        default:                          return EnginePolarSeriesType::Line;
    }
}

ChartMarkerShape EngineMarkerOf(PolarMarkerShape marker) {
    switch (marker) {
        case PolarMarkerShape::NoMarker: return ChartMarkerShape::NoMarker;
        case PolarMarkerShape::Square:   return ChartMarkerShape::Square;
        case PolarMarkerShape::Diamond:  return ChartMarkerShape::Diamond;
        case PolarMarkerShape::Triangle: return ChartMarkerShape::TriangleUp;
        case PolarMarkerShape::Cross:    return ChartMarkerShape::Cross;
        case PolarMarkerShape::Star:     return ChartMarkerShape::Plus;
        case PolarMarkerShape::Circle:
        default:                         return ChartMarkerShape::Circle;
    }
}

std::vector<PolarSpec> PolarExamples() {
    std::vector<PolarSpec> specs;

    // Three signal clusters scattered over the full circle, fixed seed.
    PolarSpec signals;
    signals.title = "Signal Scatter (numeric angles)";
    {
        std::mt19937 gen(1234);
        std::uniform_real_distribution<double> angleDist(0.0, 360.0);
        struct SignalSpec { const char* name; double mean, spread; Color color; PolarMarkerShape marker; };
        const SignalSpec signalSpecs[] = {
            {"Signal A", 4.5, 2.0, Color(102, 178, 235, 255), PolarMarkerShape::Circle},
            {"Signal B", 6.5, 3.0, Color(25, 118, 210, 255), PolarMarkerShape::Diamond},
            {"Signal C", 7.5, 2.5, Color(245, 130, 32, 255), PolarMarkerShape::Square}};
        for (const SignalSpec& s : signalSpecs) {
            std::normal_distribution<double> radiusDist(s.mean, s.spread);
            PolarSeries series(s.name, PolarSeriesType::Scatter, s.color);
            series.marker = s.marker;
            series.markerSize = 7.0f;
            series.lineWidth = 0.0f;
            series.closed = false;
            for (int i = 0; i < 26; ++i) {
                series.points.emplace_back(angleDist(gen), std::max(0.5, radiusDist(gen)));
            }
            signals.series.push_back(series);
        }
    }
    signals.manualRange = true;
    signals.lo = 0.0;
    signals.hi = 16.0;
    signals.tickInterval = 2.0;
    signals.angleInterval = 30.0f;
    specs.push_back(signals);

    // Two rose curves r = a cos(k theta), with a negative radial minimum.
    PolarSpec rose;
    rose.title = "Rose Curves (counter-clockwise, 0 degrees east)";
    {
        PolarSeries first("Series 1", PolarSeriesType::Line, Color(240, 120, 100, 255));
        first.marker = PolarMarkerShape::NoMarker;
        first.lineWidth = 1.6f;
        PolarSeries second("Series 2", PolarSeriesType::Line, Color(150, 150, 150, 255));
        second.marker = PolarMarkerShape::NoMarker;
        second.lineWidth = 1.6f;
        for (int deg = 0; deg <= 360; ++deg) {
            const double theta = deg * M_PI / 180.0;
            first.points.emplace_back(deg, 120.0 * std::cos(2.0 * theta));
            second.points.emplace_back(deg, 90.0 * std::cos(3.0 * theta) + 20.0);
        }
        first.closed = second.closed = false;
        rose.series = {first, second};
    }
    rose.manualRange = true;
    rose.lo = -100.0;
    rose.hi = 200.0;
    rose.tickInterval = 100.0;
    rose.angleInterval = 20.0f;
    rose.zeroAngle = 0.0f;
    rose.counterClockwise = true;
    specs.push_back(rose);

    // Four translucent petals - the polar area look.
    PolarSpec petals;
    petals.title = "Petal Areas (spline areas)";
    {
        struct PetalSpec { const char* name; double centerDeg; Color color; };
        const PetalSpec petalSpecs[] = {{"North", 0.0, Color(66, 165, 245, 190)},
                                        {"East", 90.0, Color(236, 64, 122, 170)},
                                        {"South", 180.0, Color(251, 192, 45, 190)},
                                        {"West", 270.0, Color(77, 208, 174, 190)}};
        for (const PetalSpec& p : petalSpecs) {
            PolarSeries series(p.name, PolarSeriesType::SplineArea, p.color);
            series.fillOpacity = 0.55f;
            series.lineWidth = 2.0f;
            series.marker = PolarMarkerShape::NoMarker;
            series.closed = true;
            for (int deg = 0; deg < 360; deg += 10) {
                const double delta = (deg - p.centerDeg) * M_PI / 180.0;
                const double value = 10.0 * std::max(0.0, std::cos(delta * 0.5));
                series.points.emplace_back(deg, value * value / 10.0);
            }
            petals.series.push_back(series);
        }
    }
    petals.manualRange = true;
    petals.lo = 0.0;
    petals.hi = 10.0;
    petals.tickInterval = 5.0;
    petals.angleInterval = 45.0f;
    specs.push_back(petals);

    // Sales by category - categorical spokes with a filled outline.
    PolarSpec sales;
    sales.title = "Category Sales (categorical spokes)";
    sales.categorical = true;
    sales.categories = {"Furniture", "Binders", "Phones", "Storage", "Chairs",
                        "Tables", "Machines", "Copiers", "Paper", "Art"};
    {
        PolarSeries series("2018 Sales", PolarSeriesType::SplineArea, Color(102, 187, 176, 255));
        series.fillOpacity = 0.45f;
        series.lineWidth = 2.0f;
        series.marker = PolarMarkerShape::NoMarker;
        series.closed = true;
        for (double v : {184.0, 142.0, 210.0, 96.0, 168.0, 74.0, 58.0, 132.0, 88.0, 46.0}) {
            series.points.emplace_back(0.0, v);
        }
        sales.series.push_back(series);
    }
    sales.tickCount = 6;
    specs.push_back(sales);

    // Monthly concentration against tolerance bands.
    PolarSpec concentration;
    concentration.title = "Concentration with Radial Bands (ppm)";
    concentration.categorical = true;
    concentration.categories = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    {
        PolarSeries series("Concentration", PolarSeriesType::Line, Color(20, 40, 160, 255));
        series.lineWidth = 3.0f;
        series.marker = PolarMarkerShape::Circle;
        series.markerSize = 8.0f;
        series.closed = true;
        for (double v : {58.0, 21.0, 30.0, 44.0, 28.0, 64.0, 72.0, 34.0, 26.0, 88.0, 40.0, 33.0}) {
            series.points.emplace_back(0.0, v, "", Color(255, 214, 0, 255));
        }
        concentration.series.push_back(series);
    }
    concentration.manualRange = true;
    concentration.lo = 0.0;
    concentration.hi = 100.0;
    concentration.tickInterval = 20.0;
    concentration.unit = " ppm";
    concentration.bands = {PolarRadialBand(0.0, 20.0, Color(150, 160, 235, 150), "Under-Absorp"),
                           PolarRadialBand(20.0, 60.0, Color(150, 230, 160, 150), "Normal"),
                           PolarRadialBand(60.0, 100.0, Color(240, 150, 150, 150), "Over-Absorp")};
    specs.push_back(concentration);

    // Regional profit by product and year - stacked polar columns.
    PolarSpec profit;
    profit.title = "Profit Rose (stacked polar columns)";
    profit.categorical = true;
    profit.betweenSpokes = true;
    profit.categories = {"Powder", "Nail polish", "Eyebrow pencil", "Rouge", "Lipstick",
                         "Eyeshadows", "Eyeliner", "Foundation", "Lip gloss", "Mascara"};
    const std::vector<std::pair<std::string, std::vector<double>>> years = {
        {"2023", {21, 11, 9, 6, 13, 18, 24, 26, 29, 20}},
        {"2024", {26, 14, 11, 8, 16, 22, 29, 33, 36, 25}},
        {"2025", {33, 18, 14, 10, 21, 28, 37, 41, 45, 32}},
        {"2026", {41, 22, 17, 12, 26, 35, 46, 52, 57, 40}}};
    profit.palette = {Color(255, 213, 79, 255), Color(245, 130, 32, 255),
                      Color(25, 118, 210, 255), Color(129, 190, 240, 255)};
    for (size_t i = 0; i < years.size(); ++i) {
        PolarSeries series(years[i].first, PolarSeriesType::Column, profit.palette[i]);
        for (double v : years[i].second) series.points.emplace_back(0.0, v);
        profit.series.push_back(series);
    }
    profit.stack = PolarStackMode::Stacked;
    profit.columnWidth = 1.0f;
    specs.push_back(profit);
    return specs;
}

std::shared_ptr<UltraCanvasPolarChart> BuildLegacyPolar(const PolarSpec& spec) {
    auto chart = CreatePolarChart("CmpPolarLegacy", 0, 0, 400, 300);
    chart->SetChartTitle(spec.title);
    if (spec.categorical) {
        chart->SetCategories(spec.categories);
        chart->SetCategoryPlacement(spec.betweenSpokes ? PolarCategoryPlacement::BetweenSpokes
                                                       : PolarCategoryPlacement::OnSpokes);
    } else {
        chart->SetAngleMode(PolarAngleMode::Numeric);
    }
    if (spec.manualRange) chart->SetRadialRange(spec.lo, spec.hi);
    if (spec.tickInterval > 0.0) chart->SetRadialTickInterval(spec.tickInterval);
    if (spec.tickCount > 0) chart->SetRadialTickCount(spec.tickCount);
    if (spec.angleInterval > 0.0f) chart->SetAngleTickInterval(spec.angleInterval);
    chart->SetZeroAngle(spec.zeroAngle);
    chart->SetDirection(spec.counterClockwise ? PolarDirection::CounterClockwise
                                              : PolarDirection::Clockwise);
    if (!spec.unit.empty()) chart->SetRadialLabelUnit(spec.unit);
    for (const PolarRadialBand& band : spec.bands) chart->AddRadialBand(band);
    chart->SetStackMode(spec.stack);
    chart->SetColumnWidthFraction(spec.columnWidth);
    if (!spec.palette.empty()) chart->SetColorPalette(spec.palette);
    for (const PolarSeries& series : spec.series) chart->AddSeries(series);
    return chart;
}

std::shared_ptr<UltraCanvasEnginePolarChart> BuildEnginePolar(const PolarSpec& spec) {
    auto chart = CreateEnginePolarChart("CmpPolarEngine");
    chart->SetChartTitle(spec.title);
    if (spec.categorical) {
        chart->SetCategories(spec.categories);
        chart->SetCategoryPlacement(spec.betweenSpokes ? EnginePolarCategoryPlacement::BetweenSpokes
                                                       : EnginePolarCategoryPlacement::OnSpokes);
    } else {
        chart->SetAngleMode(EnginePolarAngleMode::Numeric);
    }
    if (spec.manualRange) chart->SetRadialRange(spec.lo, spec.hi);
    if (spec.tickInterval > 0.0) chart->SetRadialTickInterval(spec.tickInterval);
    if (spec.tickCount > 0) chart->SetRadialTickCount(spec.tickCount);
    if (spec.angleInterval > 0.0f) chart->SetAngleTickInterval(spec.angleInterval);
    chart->SetZeroAngle(spec.zeroAngle);
    chart->SetClockwise(!spec.counterClockwise);
    if (!spec.unit.empty()) chart->SetRadialLabelUnit(spec.unit);
    for (const PolarRadialBand& band : spec.bands) {
        chart->AddRadialBand(band.from, band.to, band.color, band.label);
    }
    chart->SetStackMode(spec.stack == PolarStackMode::Stacked ? EnginePolarStackMode::Stacked
                        : spec.stack == PolarStackMode::PercentStacked
                            ? EnginePolarStackMode::PercentStacked
                            : EnginePolarStackMode::NoStacking);
    chart->SetColumnWidthFraction(spec.columnWidth);
    for (const PolarSeries& legacy : spec.series) {
        EnginePolarSeries series;
        series.name = legacy.name;
        series.type = EngineTypeOf(legacy.type);
        series.color = legacy.color;
        series.fillOpacity = legacy.fillOpacity;
        series.lineWidth = legacy.lineWidth;
        series.dashed = legacy.dashed;
        series.marker = EngineMarkerOf(legacy.marker);
        series.markerSize = legacy.markerSize;
        series.closed = legacy.closed;
        series.showValueLabels = legacy.showValueLabels;
        for (const PolarDataPoint& p : legacy.points) {
            series.points.emplace_back(p.angle, p.radius, p.label, p.color);
        }
        chart->AddSeries(series);
    }
    return chart;
}

std::shared_ptr<UltraCanvasUIElement> MakePolarTab() {
    struct State {
        std::shared_ptr<UltraCanvasPolarChart> legacy;
        std::shared_ptr<UltraCanvasEnginePolarChart> engine;
        int grid = -1, scale = -1, sweep = -1, bands = -1;
        std::string theme = "Light";
        bool themePalette = false;
    };
    auto state = std::make_shared<State>();
    auto legacyHolder = MakeHolder("CmpPolarLegacyHolder");
    auto engineHolder = MakeHolder("CmpPolarEngineHolder");
    const std::vector<PolarSpec> examples = PolarExamples();

    auto apply = [state]() {
        if (state->grid >= 0) {
            const bool polygon = state->grid == 1;
            state->legacy->SetGridShape(polygon ? PolarGridShape::Polygonal : PolarGridShape::Circular);
            state->engine->SetPolarGridShape(polygon ? ChartPolarGridShape::Polygon
                                                     : ChartPolarGridShape::Circle);
        }
        if (state->scale >= 0) {
            const PolarRadialScale legacy[] = {PolarRadialScale::Linear, PolarRadialScale::SquareRoot,
                                               PolarRadialScale::Logarithmic};
            const EnginePolarRadialScale engine[] = {EnginePolarRadialScale::Linear,
                                                     EnginePolarRadialScale::SquareRoot,
                                                     EnginePolarRadialScale::Logarithmic};
            state->legacy->SetRadialScale(legacy[state->scale]);
            state->engine->SetRadialScale(engine[state->scale]);
        }
        if (state->sweep >= 0) {
            const float sweeps[] = {360.0f, 270.0f, 180.0f};
            state->legacy->SetSweepAngle(sweeps[state->sweep]);
            state->engine->SetSweepAngle(sweeps[state->sweep]);
        }
        if (state->bands >= 0) {
            state->legacy->SetAlternatingBands(state->bands == 1);
            state->engine->SetAlternatingBands(state->bands == 1);
        }
    };
    auto show = [state, legacyHolder, engineHolder, examples, apply](size_t index) {
        state->legacy = BuildLegacyPolar(examples[index]);
        state->engine = BuildEnginePolar(examples[index]);
        state->engine->SetTheme(state->theme);
        state->engine->SetUseThemePalette(state->themePalette);
        apply();
        PutInHolder(*legacyHolder, state->legacy);
        PutInHolder(*engineHolder, state->engine);
    };
    show(4);

    auto strip = MakeControlStrip("CmpPolar");
    AddStripLabel(*strip, "CmpPolarExampleLabel", "Example:", 56);
    std::vector<std::string> names;
    for (const PolarSpec& spec : examples) names.push_back(spec.title);
    auto example = MakeDropdown("CmpPolarExample", 260, names, 4);
    example->onSelectionChanged = [show](int index, const DropdownItem&) {
        show(static_cast<size_t>(std::max(0, index)));
    };
    AddToStrip(*strip, example);

    auto grid = MakeDropdown("CmpPolarGrid", 100, {"Circles", "Polygons"});
    grid->onSelectionChanged = [state, apply](int index, const DropdownItem&) {
        state->grid = index == 1 ? 1 : 0;
        apply();
    };
    AddToStrip(*strip, grid);

    auto scale = MakeDropdown("CmpPolarScale", 110, {"Linear", "Square root", "Logarithmic"});
    scale->onSelectionChanged = [state, apply](int index, const DropdownItem&) {
        state->scale = std::clamp(index, 0, 2);
        apply();
    };
    AddToStrip(*strip, scale);

    auto sweep = MakeDropdown("CmpPolarSweep", 90, {"360 deg", "270 deg", "180 deg"});
    sweep->onSelectionChanged = [state, apply](int index, const DropdownItem&) {
        state->sweep = std::clamp(index, 0, 2);
        apply();
    };
    AddToStrip(*strip, sweep);

    auto bands = MakeCheckbox("CmpPolarBands", "Alternating bands", false, 140);
    bands->onStateChanged = [state, apply](CheckedState, CheckedState now) {
        state->bands = Checked(now) ? 1 : 0;
        apply();
    };
    AddToStrip(*strip, bands);

    AddStripLabel(*strip, "CmpPolarThemeLabel", "Theme:", 48);
    AddToStrip(*strip, MakeThemeDropdown("CmpPolarTheme",
        [state](const std::string& theme, bool palette) {
            state->theme = theme;
            state->themePalette = palette;
            state->engine->SetTheme(theme);
            state->engine->SetUseThemePalette(palette);
        }));

    auto replay = MakeButton("CmpPolarReplay", "Replay", 70);
    replay->onClick = [state]() { state->engine->StartEngineAnimation(0.8f); };
    AddToStrip(*strip, replay);

    return MakeComparisonTab(
        "CmpPolar",
        "Polar: scatter, line, spline, area and column series on numeric or categorical angles. The engine port draws lines and areas with\n"
        "BuildLineRuns / BuildAreaOutlines and columns with BuildBarOutline; radial bands are ValueBand highlights, which are rings under Polar.",
        "UltraCanvasPolarChart", legacyHolder, "UltraCanvasEnginePolarChart", engineHolder, strip);
}

} // namespace

std::vector<ComparisonTab> PolarFamilyTabs() {
    return {
        {"Radar", MakeRadarTab},
        {"Radial Bar", MakeRadialBarTab},
        {"Pie & Donut", MakePieTab},
        {"Polar", MakePolarTab},
    };
}

} // namespace EngineComparison
} // namespace UltraCanvas
