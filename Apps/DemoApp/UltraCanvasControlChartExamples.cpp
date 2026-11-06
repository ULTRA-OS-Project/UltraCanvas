// Apps/DemoApp/UltraCanvasControlChartExamples.cpp
// The control chart card on the Line Chart page: a Shewhart control chart
// built from the engine line chart's general features (stacked panes,
// per-point styles and notes, bands, limiters, point clicks) by
// ShowControlChart, with the limits and run-rule signals computed by
// UltraCanvasControlLimits.
// Version: 1.1.0 - p and c charts, the Nelson rules, the change as a stage,
//   specification limits and capability, a click excludes a point
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
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// A small fixed generator, so every run draws the same invented process.
class Random {
public:
    explicit Random(uint32_t seed) : state(seed) {}
    double Uniform() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;
    }
    // Box-Muller from two uniforms.
    double Normal() {
        const double u1 = std::max(1e-12, Uniform());
        const double u2 = Uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
    // Defectives among n items, each defective with probability p.
    double Binomial(int n, double p) {
        int k = 0;
        for (int i = 0; i < n; ++i) k += Uniform() < p ? 1 : 0;
        return k;
    }
    // Defects with mean `mean` (Knuth's method; fine for small means).
    double Poisson(double mean) {
        const double limit = std::exp(-mean);
        double product = Uniform();
        int k = 0;
        while (product > limit) {
            product *= Uniform();
            ++k;
        }
        return k;
    }

private:
    uint32_t state;
};

enum class Process { Shaft, Calls, Inspection, Paint };

struct ProcessInfo {
    const char* title;
    const char* domain;
    ControlChartKind kind;
    size_t changeAt;           // where the process changes, when it does
    const char* changeName;    // the stage that change starts
};

ProcessInfo Info(Process process) {
    switch (process) {
        case Process::Shaft:
            return {"Shaft diameter (mm), X-bar and R, n = 5", "Hour", ControlChartKind::XbarR, 17, "Worn tool"};
        case Process::Calls:
            return {"Call waiting time (minutes), individuals and moving range", "Day",
                    ControlChartKind::IndividualsMR, 20, "New rota"};
        case Process::Inspection:
            return {"Failed inspections per day, p chart (lot sizes vary)", "Day", ControlChartKind::P, 18,
                    "New supplier"};
        default:
            return {"Paint defects per car body, c chart", "Body", ControlChartKind::C, 18, "New paint line"};
    }
}

// The invented data; with `change`, the process moves at Info().changeAt.
std::vector<ControlSubgroup> Data(Process process, bool change) {
    std::vector<ControlSubgroup> data;
    switch (process) {
        case Process::Shaft: {
            // 25 hours, five parts each; a worn tool moves the mean up by a
            // little over one sigma of the mean.
            Random random(20261106u);
            for (int hour = 0; hour < 25; ++hour) {
                ControlSubgroup g;
                char label[16];
                std::snprintf(label, sizeof(label), "%02d:00", (6 + hour) % 24);   // locale-ok: a clock label
                g.label = label;
                const double mean = 25.000 + ((change && hour >= 17) ? 0.012 : 0.0);
                for (int k = 0; k < 5; ++k) g.values.push_back(mean + 0.010 * random.Normal());
                data.push_back(g);
            }
            break;
        }
        case Process::Calls: {
            // 30 days, one average each; a new rota lets the wait drift up.
            Random random(4021u);
            for (int day = 0; day < 30; ++day) {
                ControlSubgroup g;
                g.label = "Day " + std::to_string(day + 1);
                const double drift = (change && day >= 20) ? 0.18 * (day - 19) : 0.0;
                g.values.push_back(4.0 + drift + 0.35 * random.Normal());
                data.push_back(g);
            }
            break;
        }
        case Process::Inspection: {
            // 25 days of lots from 80 to 260 items, 4 % failing; a new
            // supplier raises that to 7 %.
            Random random(777u);
            for (int day = 0; day < 25; ++day) {
                ControlSubgroup g;
                g.label = "Day " + std::to_string(day + 1);
                const int lot = 80 + static_cast<int>(random.Uniform() * 180.0);
                const double rate = (change && day >= 18) ? 0.07 : 0.04;
                g.size = lot;
                g.count = random.Binomial(lot, rate);
                data.push_back(g);
            }
            break;
        }
        case Process::Paint: {
            // 25 bodies, about 4 blemishes each; a new paint line makes 7.
            Random random(31337u);
            for (int body = 0; body < 25; ++body) {
                ControlSubgroup g;
                g.label = "#" + std::to_string(4101 + body);
                g.count = random.Poisson((change && body >= 18) ? 7.0 : 4.0);
                data.push_back(g);
            }
            break;
        }
    }
    return data;
}

struct ControlDemoState {
    std::shared_ptr<UltraCanvasEngineLineChart> chart;
    std::shared_ptr<UltraCanvasLabel> summary;
    Process process = Process::Shaft;
    bool change = true;
    bool asStage = false;
    bool specs = false;
    ControlRuleSet ruleSet = ControlRuleSet::WesternElectric;
    std::set<size_t> excluded;
    ControlChartStyle style;
};

std::string Number(double v, int decimals) {
    if (!std::isfinite(v)) return "-";           // Cp with one specification limit
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);   // locale-ok: display text
    return buffer;
}

std::string Summary(const ControlResult& result, const ControlDemoState& state) {
    std::string text;
    size_t onValues = 0, onRanges = 0;
    for (const ControlViolation& v : result.violations) (v.pane == 0 ? onValues : onRanges)++;
    if (result.violations.empty()) {
        text = "No signal: the variation is the usual kind. Leave the process alone.";
    } else {
        text = std::to_string(onValues) + " signal(s) on the values" +
               (result.dispersion ? ", " + std::to_string(onRanges) + " on the ranges" : std::string()) +
               ". Hover a red diamond for the rule it breaks.";
    }
    if (result.capability.available) {
        const ControlCapability& c = result.capability;
        text += "\nCapability: Cp " + Number(c.cp, 2) + ", Cpk " + Number(c.cpk, 2) + "  (Pp " +
                Number(c.pp, 2) + ", Ppk " + Number(c.ppk, 2) + ")";
    }
    if (!state.excluded.empty()) {
        text += "\n" + std::to_string(state.excluded.size()) + " point(s) excluded from the limits.";
    }
    for (const std::string& warning : result.warnings) text += "\n" + warning;
    return text;
}

void Refresh(ControlDemoState& state) {
    const ProcessInfo info = Info(state.process);
    std::vector<ControlSubgroup> data = Data(state.process, state.change);
    for (size_t i : state.excluded) {
        if (i < data.size()) data[i].excluded = true;
    }
    ControlOptions options;
    options.kind = info.kind;
    options.ruleSet = state.ruleSet;
    if (state.change && state.asStage) options.stages = {{info.changeAt, info.changeName}};
    if (state.specs && state.process == Process::Shaft) {
        options.lowerSpec = 24.97;
        options.upperSpec = 25.03;
        options.target = 25.0;
    } else if (state.specs && state.process == Process::Calls) {
        options.upperSpec = 6.0;
    }
    const ControlResult result = ComputeControlChart(data, options);
    state.chart->SetChartTitle(info.title);
    state.chart->SetAxisTitles(info.domain, "");
    ShowControlChart(*state.chart, result, state.style);
    if (state.summary) state.summary->SetText(Summary(result, state));
}

} // namespace

void AddControlChartCard(const std::shared_ptr<UltraCanvasContainer>& page) {
    auto card = DemoKit::AddCard(page, "controlChartCard", "Control chart (statistical process control)",
                                 "A line chart with computed limits: the centre line, and control limits "
                                 "three sigma either side, estimated within subgroups. A point beyond the "
                                 "limits, or a run of points inside them, is a signal worth investigating; "
                                 "everything else is noise to leave alone. Click a point to exclude it "
                                 "from the limits (a cause found and fixed), and again to bring it back.");
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
    data->AddItem("Shaft diameters (X-bar and R)", "shaft");
    data->AddItem("Call waiting time (I-MR)", "calls");
    data->AddItem("Failed inspections (p chart)", "inspection");
    data->AddItem("Paint defects (c chart)", "paint");
    data->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, data);

    addCaption("controlChartRulesCaption", "Rules");
    auto rules = std::make_shared<UltraCanvasDropdown>("controlChartRuleSet", 0, 0, 200, 28);
    rules->AddItem("Western Electric (1-4)", "we");
    rules->AddItem("Nelson (1-8)", "nelson");
    rules->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, rules);

    auto change = UltraCanvasSwitch::Create("controlChartShift", 0, 0, "Process changes late on", true);
    auto stage = UltraCanvasSwitch::Create("controlChartStage", 0, 0, "The change is a new stage", false);
    auto specs = UltraCanvasSwitch::Create("controlChartSpecs", 0, 0, "Specification limits", false);
    auto zones = UltraCanvasSwitch::Create("controlChartZones", 0, 0, "Zones A, B, C", false);
    auto sigma = UltraCanvasSwitch::Create("controlChartSigma", 0, 0, "1 and 2 sigma lines", false);
    auto ranges = UltraCanvasSwitch::Create("controlChartRanges", 0, 0, "Range pane", true);
    addCaption("controlChartShowCaption", "Show");
    for (const auto& s : {change, stage, specs, zones, sigma, ranges}) DemoKit::Add(sidebar, s);

    addCaption("controlChartSignalsCaption", "Signals");
    state->summary = DemoKit::AddCaption(sidebar, "controlChartSummary", "");
    state->summary->SetTextColor(DemoKit::BodyText());

    auto onSwitch = [state](bool ControlDemoState::*field) {
        return [state, field](CheckedState, CheckedState now) {
            (*state).*field = (now == CheckedState::Checked);
            Refresh(*state);
        };
    };
    data->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->process = item.value == "calls"        ? Process::Calls
                         : item.value == "inspection" ? Process::Inspection
                         : item.value == "paint"      ? Process::Paint
                                                      : Process::Shaft;
        state->excluded.clear();            // the points are different ones
        Refresh(*state);
    };
    rules->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->ruleSet = item.value == "nelson" ? ControlRuleSet::Nelson : ControlRuleSet::WesternElectric;
        Refresh(*state);
    };
    change->onStateChanged = onSwitch(&ControlDemoState::change);
    stage->onStateChanged = onSwitch(&ControlDemoState::asStage);
    specs->onStateChanged = onSwitch(&ControlDemoState::specs);
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
    // A click on a point (either pane) excludes it from the limits, or
    // brings it back.
    state->chart->onPointClick = [state](size_t, size_t index) {
        if (!state->excluded.erase(index)) state->excluded.insert(index);
        Refresh(*state);
    };

    Refresh(*state);
}

} // namespace UltraCanvas
