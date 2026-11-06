// Apps/DemoApp/UltraCanvasControlChartExamples.cpp
// The control chart card on the Line Chart page: a Shewhart control chart
// built from the engine line chart's general features (stacked panes,
// per-point styles and notes, limiters) by ShowControlChart, with the limits
// and run-rule signals computed by UltraCanvasControlLimits.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// A small fixed generator, so every run draws the same invented process.
class Normal {
public:
    explicit Normal(uint32_t seed) : state(seed) {}
    double Next() {
        // Box-Muller from two uniforms.
        const double u1 = std::max(1e-12, Uniform());
        const double u2 = Uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

private:
    uint32_t state;
    double Uniform() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;
    }
};

// Shaft diameters, mm: 25 subgroups of five parts measured every hour. With
// `shift`, the process moves up by a little over one sigma of the mean after
// the 17th hour - a worn tool.
std::vector<ControlSubgroup> ShaftDiameters(bool shift) {
    Normal noise(20261106u);
    std::vector<ControlSubgroup> data;
    for (int hour = 0; hour < 25; ++hour) {
        ControlSubgroup g;
        char label[16];
        std::snprintf(label, sizeof(label), "%02d:00", (6 + hour) % 24);   // locale-ok: a clock label
        g.label = label;
        const double mean = 25.000 + ((shift && hour >= 17) ? 0.012 : 0.0);
        for (int k = 0; k < 5; ++k) g.values.push_back(mean + 0.010 * noise.Next());
        data.push_back(g);
    }
    return data;
}

// Call waiting time, minutes: one average per day for 30 days. With `shift`,
// the last ten days drift up - a slow trend the run rules catch before any
// day crosses the limit.
std::vector<ControlSubgroup> CallWaiting(bool shift) {
    Normal noise(4021u);
    std::vector<ControlSubgroup> data;
    for (int day = 0; day < 30; ++day) {
        ControlSubgroup g;
        g.label = "Day " + std::to_string(day + 1);
        const double drift = (shift && day >= 20) ? 0.18 * (day - 19) : 0.0;
        g.values.push_back(4.0 + drift + 0.35 * noise.Next());
        data.push_back(g);
    }
    return data;
}

struct ControlDemoState {
    std::shared_ptr<UltraCanvasEngineLineChart> chart;
    std::shared_ptr<UltraCanvasLabel> summary;
    bool individuals = false;
    bool shift = true;
    ControlChartStyle style;
};

std::string Summary(const ControlResult& result) {
    std::string text;
    size_t onValues = 0, onRanges = 0;
    for (const ControlViolation& v : result.violations) (v.pane == 0 ? onValues : onRanges)++;
    if (result.violations.empty()) {
        text = "No signal: the variation is the usual kind. Leave the process alone.";
    } else {
        text = std::to_string(onValues) + " signal(s) on the values, " + std::to_string(onRanges) +
               " on the ranges. Hover a red diamond for the rule it breaks.";
    }
    for (const std::string& warning : result.warnings) text += "\n" + warning;
    return text;
}

void Refresh(ControlDemoState& state) {
    ControlOptions options;
    options.kind = state.individuals ? ControlChartKind::IndividualsMR : ControlChartKind::XbarR;
    const ControlResult result = ComputeControlChart(
        state.individuals ? CallWaiting(state.shift) : ShaftDiameters(state.shift), options);
    state.chart->SetChartTitle(state.individuals ? "Call waiting time (minutes), individuals and moving range"
                                                 : "Shaft diameter (mm), X-bar and R, n = 5");
    state.chart->SetAxisTitles(state.individuals ? "Day" : "Hour", "");
    ShowControlChart(*state.chart, result, state.style);
    if (state.summary) state.summary->SetText(Summary(result));
}

} // namespace

void AddControlChartCard(const std::shared_ptr<UltraCanvasContainer>& page) {
    auto card = DemoKit::AddCard(page, "controlChartCard", "Control chart (statistical process control)",
                                 "A line chart with computed limits: the centre line, and control limits "
                                 "three sigma either side, estimated within subgroups. A point beyond the "
                                 "limits, or a run of points inside them, is a signal worth investigating; "
                                 "everything else is noise to leave alone.");
    auto row = DemoKit::AddRow(card, "controlChartRow");

    auto state = std::make_shared<ControlDemoState>();
    state->chart = CreateEngineLineChart("controlChart", 0, 0, 700, 460);
    state->chart->SetShowMarkers(true);
    state->chart->SetMarkerRadius(3.5f);
    state->chart->SetLineWidth(1.8f);
    DemoKit::AddFixed(row, state->chart);

    auto sidebar = DemoKit::AddCard(row, "controlChartSidebar", "", "", 240);
    auto addCaption = [&](const std::string& id, const std::string& text) {
        DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
    };

    addCaption("controlChartDataCaption", "Process");
    auto data = std::make_shared<UltraCanvasDropdown>("controlChartData", 0, 0, 200, 28);
    data->AddItem("Shaft diameters (X-bar and R)", "xbar");
    data->AddItem("Call waiting time (I-MR)", "imr");
    data->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, data);

    auto shift = UltraCanvasSwitch::Create("controlChartShift", 0, 0, "Process changes late on", true);
    auto zones = UltraCanvasSwitch::Create("controlChartZones", 0, 0, "Zones A, B, C", false);
    auto sigma = UltraCanvasSwitch::Create("controlChartSigma", 0, 0, "1 and 2 sigma lines", false);
    auto ranges = UltraCanvasSwitch::Create("controlChartRanges", 0, 0, "Range pane", true);
    addCaption("controlChartShowCaption", "Show");
    DemoKit::Add(sidebar, shift);
    DemoKit::Add(sidebar, zones);
    DemoKit::Add(sidebar, sigma);
    DemoKit::Add(sidebar, ranges);

    addCaption("controlChartSignalsCaption", "Signals");
    state->summary = DemoKit::AddCaption(sidebar, "controlChartSummary", "");
    state->summary->SetTextColor(DemoKit::BodyText());

    DemoKit::AddCaption(sidebar, "controlChartRules",
                        "Western Electric rules: 1 a point beyond the limits; 2 two of three beyond "
                        "2 sigma on one side; 3 four of five beyond 1 sigma on one side; 4 eight in a "
                        "row on one side of the centre line.");

    data->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->individuals = (item.value == "imr");
        Refresh(*state);
    };
    shift->onStateChanged = [state](CheckedState, CheckedState now) {
        state->shift = (now == CheckedState::Checked);
        Refresh(*state);
    };
    zones->onStateChanged = [state](CheckedState, CheckedState now) {
        state->style.showZones = (now == CheckedState::Checked);
        Refresh(*state);
    };
    sigma->onStateChanged = [state](CheckedState, CheckedState now) {
        state->style.showSigmaLines = (now == CheckedState::Checked);
        Refresh(*state);
    };
    ranges->onStateChanged = [state](CheckedState, CheckedState now) {
        state->style.showDispersion = (now == CheckedState::Checked);
        Refresh(*state);
    };

    Refresh(*state);
}

} // namespace UltraCanvas
