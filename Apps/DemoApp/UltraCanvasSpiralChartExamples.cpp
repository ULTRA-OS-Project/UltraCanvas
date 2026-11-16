// Apps/DemoApp/UltraCanvasSpiralChartExamples.cpp
// Charts > Spiral Chart: a time series wound along a spiral, one turn per
// period (UltraCanvasSpiralChart). Two invented samples - three years of
// daily temperatures, a year per turn, and four weeks of hourly website
// visits, a week per turn - with the display (ribbon, band, line), the colour
// map and the direction to change, and a period slider: wind the spiral to
// another period and watch the rhythm line up or drift.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

struct SpiralDemoState {
    std::shared_ptr<UltraCanvasSpiralChart> chart;
    std::shared_ptr<UltraCanvasLabel> readout, periodCaption;
    std::shared_ptr<UltraCanvasSlider> period;
    std::shared_ptr<UltraCanvasDropdown> colours;
    bool temperature = true;
};

// Day 0 is 1 January 2021; the sample's years have 365 days.
std::string DayText(double t) {
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int day = static_cast<int>(std::floor(t));
    const int year = 2021 + day / 365;
    day %= 365;
    int month = 0;
    while (month < 11 && day >= lengths[month]) day -= lengths[month++];
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d %s %d", day + 1, months[month], year);   // locale-ok: display text
    return buffer;
}

// Hour 0 is Monday 00:00 of week 1.
std::string HourText(double t) {
    static const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    const int hour = static_cast<int>(std::floor(t));
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%s, week %d, %02d:00", days[(hour / 24) % 7], hour / 168 + 1,   // locale-ok: display text
                  hour % 24);
    return buffer;
}

void ShowPeriodCaption(SpiralDemoState& state, double period) {
    char text[64];
    if (state.temperature) {
        std::snprintf(text, sizeof(text), "Period: %.0f days a turn", period);   // locale-ok: display text
    } else {
        std::snprintf(text, sizeof(text), "Period: %.0f hours a turn", period);   // locale-ok: display text
    }
    state.periodCaption->SetText(text);
}

void ApplyColours(SpiralDemoState& state) {
    const DropdownItem* item = state.colours->GetSelectedItem();
    const std::string c = item ? item->value : "rdbu";
    if (c == "viridis") state.chart->SetColormap(HeatmapColormap::Viridis);
    else if (c == "inferno") state.chart->SetColormap(HeatmapColormap::Inferno);
    else if (c == "turbo") state.chart->SetColormap(HeatmapColormap::Turbo);
    else state.chart->SetColormap(HeatmapColormap::RdBu, true);     // cold blue, warm red
}

void ShowSample(SpiralDemoState& state) {
    auto& chart = *state.chart;
    if (state.temperature) {
        chart.SetModel(UltraCanvasSpiralModel::DailyTemperature());
        chart.SetTimeFormatter(DayText);
        chart.SetValueDecimals(1);
        state.period->SetRange(300.0f, 430.0f);
        state.period->SetStep(1.0f);
        state.period->SetValue(365.0f);
    } else {
        chart.SetModel(UltraCanvasSpiralModel::HourlyVisits());
        chart.SetTimeFormatter(HourText);
        chart.SetValueDecimals(0);
        state.period->SetRange(12.0f, 200.0f);
        state.period->SetStep(1.0f);
        state.period->SetValue(168.0f);
    }
    ShowPeriodCaption(state, chart.GetModel().period);
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSpiralChartExamples() {
    auto page = DemoKit::CreatePage("spiralMainContainer", "Spiral Chart",
                                    "A time series wound along a spiral, one turn per period: times one period "
                                    "apart land on the same ray, one ring further out, so a rhythm lines up. "
                                    "Change the period to find it. The data is invented.");
    auto row = DemoKit::AddRow(page, "spiralContent");

    auto card = DemoKit::AddCard(row, "spiralChartCard");
    auto state = std::make_shared<SpiralDemoState>();
    state->chart = CreateSpiralChart("spiralChart", 0, 0, 680, 700);
    DemoKit::AddFixed(card, state->chart);

    auto sidebar = DemoKit::AddCard(row, "spiralSidebar", "", "", 260);
    auto addCaption = [&](const std::string& id, const std::string& text) {
        DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
    };
    auto addDropdown = [&](const std::string& id, const std::vector<std::pair<std::string, std::string>>& items) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 220, 28);
        for (const auto& [label, value] : items) dropdown->AddItem(label, value);
        dropdown->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, dropdown);
        return dropdown;
    };

    addCaption("spiralSampleCaption", "Sample");
    auto sample = addDropdown("spiralSample", {{"Temperatures (a year a turn)", "temperature"},
                                               {"Website visits (a week a turn)", "visits"}});

    state->periodCaption = DemoKit::AddCaption(sidebar, "spiralPeriodCaption", "Period: 365 days a turn");
    state->periodCaption->SetFontWeight(FontWeight::Bold);
    state->period = std::make_shared<UltraCanvasSlider>("spiralPeriod", 0, 0, 220, 28);
    state->period->SetValueDisplay(SliderValueDisplay::NoDisplay);
    DemoKit::AddStretched(sidebar, state->period);

    addCaption("spiralDisplayCaption", "Display");
    auto display = addDropdown("spiralDisplay", {{"Ribbon (colour)", "ribbon"},
                                                 {"Band (width)", "band"},
                                                 {"Line", "line"}});
    addCaption("spiralColourCaption", "Colours");
    state->colours = addDropdown("spiralColours", {{"Blue to red", "rdbu"}, {"Viridis", "viridis"},
                                                   {"Inferno", "inferno"}, {"Turbo", "turbo"}});
    auto anticlockwise = UltraCanvasSwitch::Create("spiralDirection", 0, 0, "Anticlockwise", false);
    DemoKit::Add(sidebar, anticlockwise);
    auto guide = UltraCanvasSwitch::Create("spiralGuide", 0, 0, "Guide and spokes", true);
    DemoKit::Add(sidebar, guide);

    addCaption("spiralReadoutCaption", "Under the pointer");
    state->readout = DemoKit::AddCaption(sidebar, "spiralReadout", "Hover the spiral.");
    state->readout->SetTextColor(DemoKit::BodyText());

    sample->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->temperature = item.value == "temperature";
        ShowSample(*state);
    };
    state->period->onValueChanged = [state](float value) {
        state->chart->SetPeriod(value);
        ShowPeriodCaption(*state, value);
    };
    display->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->chart->SetDisplay(item.value == "band" ? SpiralDisplay::Band
                                 : item.value == "line" ? SpiralDisplay::Line
                                                        : SpiralDisplay::Ribbon);
    };
    state->colours->onSelectionChanged = [state](int, const DropdownItem&) { ApplyColours(*state); };
    anticlockwise->onStateChanged = [state](CheckedState, CheckedState now) {
        UltraCanvasSpiralModel model = state->chart->GetModel();
        model.clockwise = now != CheckedState::Checked;
        state->chart->SetModel(model);
    };
    guide->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetShowGuide(now == CheckedState::Checked);
    };
    std::weak_ptr<SpiralDemoState> weak = state;
    state->chart->onSampleHover = [weak](int index) {
        auto s = weak.lock();
        if (!s) return;
        s->readout->SetText(index < 0 ? std::string("Hover the spiral.")
                                      : s->chart->DescribeSample(static_cast<size_t>(index)));
    };

    ShowSample(*state);
    ApplyColours(*state);
    return page;
}

} // namespace UltraCanvas
