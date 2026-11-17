// Apps/DemoApp/UltraCanvasRidgelineChartExamples.cpp
// Charts > Ridgeline Chart: one curve per row on its own baseline, rising over
// the rows above (UltraCanvasRidgelineChart). Two invented samples - how
// people might rate words from 0 to 10 (densities of the answers, coloured
// red to green by the average score) and when in the day people might do 16
// activities (curves, each to its own peak) - with the overlap, scale, sort
// and colouring to change and the row under the pointer read out.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
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

struct RidgelineDemoState {
    std::shared_ptr<UltraCanvasRidgelineChart> chart;
    std::shared_ptr<UltraCanvasLabel> readout;
    std::shared_ptr<UltraCanvasDropdown> scale, sort, colouring;
    bool words = true;
};

std::string HourText(double hours) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:00", static_cast<int>(std::lround(hours)) % 24);   // locale-ok: display text
    return buffer;
}

// The chosen sample, scaled and sorted as the controls say.
void ShowSample(RidgelineDemoState& state) {
    UltraCanvasRidgelineModel model =
        state.words ? UltraCanvasRidgelineModel::WordRatings() : UltraCanvasRidgelineModel::DailyActivities();
    const std::string scale = state.scale->GetSelectedItem() ? state.scale->GetSelectedItem()->value : "shared";
    model.scale = scale == "peak" ? RidgelineScale::RowPeak
                : scale == "area" ? RidgelineScale::RowArea
                                  : RidgelineScale::Shared;
    const std::string sort = state.sort->GetSelectedItem() ? state.sort->GetSelectedItem()->value : "sample";
    if (sort == "mean") model.Sort(RidgelineSort::ByMean);
    else if (sort == "median") model.Sort(RidgelineSort::ByMedian);
    else if (sort == "peak") model.Sort(RidgelineSort::ByPeak, !state.words);
    else if (sort == "name") model.Sort(RidgelineSort::ByName);

    auto& chart = *state.chart;
    if (state.words) {
        chart.SetXTicks({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        chart.SetXFormatter(nullptr);
        chart.SetValueColumn("Average score", RidgelineValue::Mean, 2);
        chart.SetOutline(Color(40, 40, 40, 255), 1.0f);
    } else {
        chart.SetXTicks({3, 6, 9, 12, 15, 18, 21, 24, 27});
        chart.SetXFormatter(HourText);
        chart.SetValueColumn("Peak", RidgelineValue::Peak);
        chart.SetOutline(Color(255, 255, 255, 255), 1.5f);
    }
    chart.SetModel(model);
}

void ApplyColouring(RidgelineDemoState& state) {
    const std::string c = state.colouring->GetSelectedItem() ? state.colouring->GetSelectedItem()->value : "value";
    auto& chart = *state.chart;
    if (c == "rows") {
        chart.SetColoring(RidgelineColoring::ByRow);
        chart.SetColormap(HeatmapColormap::Viridis);
    } else if (c == "alternating") {
        chart.SetColoring(RidgelineColoring::Alternating);
    } else if (c == "single") {
        chart.SetColoring(RidgelineColoring::Single);
    } else {
        chart.SetColoring(RidgelineColoring::ByRowValue);
        chart.SetColormap(HeatmapColormap::RdYlGn, !state.words);   // late peaks red, early green
    }
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRidgelineChartExamples() {
    auto page = DemoKit::CreatePage("ridgelineMainContainer", "Ridgeline Chart",
                                    "One curve per row, each on its own baseline and allowed to rise over the "
                                    "rows above: read down the rows to compare where and how each is spread. "
                                    "The data is invented.");
    auto row = DemoKit::AddRow(page, "ridgelineContent");

    auto card = DemoKit::AddCard(row, "ridgelineChartCard");
    auto state = std::make_shared<RidgelineDemoState>();
    state->chart = CreateRidgelineChart("ridgelineChart", 0, 0, 680, 820);
    state->chart->SetOverlap(3.5);
    DemoKit::AddFixed(card, state->chart);

    auto sidebar = DemoKit::AddCard(row, "ridgelineSidebar", "", "", 260);
    auto addCaption = [&](const std::string& id, const std::string& text) {
        DemoKit::AddCaption(sidebar, id, text)->SetFontWeight(FontWeight::Bold);
    };

    addCaption("ridgelineSampleCaption", "Sample");
    auto sample = std::make_shared<UltraCanvasDropdown>("ridgelineSample", 0, 0, 220, 28);
    sample->AddItem("Word ratings (answers 0-10)", "words");
    sample->AddItem("Times of day (curves)", "day");
    sample->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, sample);

    auto overlapCaption = DemoKit::AddCaption(sidebar, "ridgelineOverlapCaption", "Overlap: 3.5 row heights");
    overlapCaption->SetFontWeight(FontWeight::Bold);
    auto overlap = std::make_shared<UltraCanvasSlider>("ridgelineOverlap", 0, 0, 220, 28);
    overlap->SetRange(0.5, 5.0);
    overlap->SetStep(0.1);
    overlap->SetValue(3.5);
    overlap->SetValueDisplay(SliderValueDisplay::NoDisplay);
    DemoKit::AddStretched(sidebar, overlap);

    addCaption("ridgelineScaleCaption", "Scale");
    state->scale = std::make_shared<UltraCanvasDropdown>("ridgelineScale", 0, 0, 220, 28);
    state->scale->AddItem("One scale for all rows", "shared");
    state->scale->AddItem("Each row to its own peak", "peak");
    state->scale->AddItem("Each row the same area", "area");
    state->scale->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, state->scale);

    addCaption("ridgelineSortCaption", "Order");
    state->sort = std::make_shared<UltraCanvasDropdown>("ridgelineSort", 0, 0, 220, 28);
    state->sort->AddItem("As the sample sorts it", "sample");
    state->sort->AddItem("By mean", "mean");
    state->sort->AddItem("By median", "median");
    state->sort->AddItem("By peak", "peak");
    state->sort->AddItem("By name", "name");
    state->sort->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, state->sort);

    addCaption("ridgelineColourCaption", "Colour");
    state->colouring = std::make_shared<UltraCanvasDropdown>("ridgelineColour", 0, 0, 220, 28);
    state->colouring->AddItem("By the value column", "value");
    state->colouring->AddItem("Down the rows", "rows");
    state->colouring->AddItem("Alternating", "alternating");
    state->colouring->AddItem("One colour", "single");
    state->colouring->SetSelectedIndex(0);
    DemoKit::AddStretched(sidebar, state->colouring);

    auto rowLines = UltraCanvasSwitch::Create("ridgelineRowLines", 0, 0, "Row lines", false);
    DemoKit::Add(sidebar, rowLines);
    auto gradient = UltraCanvasSwitch::Create("ridgelineGradient", 0, 0, "Gradient fill", false);
    DemoKit::Add(sidebar, gradient);

    addCaption("ridgelineReadoutCaption", "Under the pointer");
    state->readout = DemoKit::AddCaption(sidebar, "ridgelineReadout", "Hover a curve.");
    state->readout->SetTextColor(DemoKit::BodyText());

    sample->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->words = item.value == "words";
        // Each sample reads best one way: the words to one scale, coloured by
        // their average; the activities each to its own peak, alternating.
        state->scale->SetSelectedIndex(state->words ? 0 : 1);
        state->colouring->SetSelectedIndex(state->words ? 0 : 2);
        state->sort->SetSelectedIndex(0);
        ShowSample(*state);
        ApplyColouring(*state);
    };
    std::weak_ptr<UltraCanvasLabel> weakCaption = overlapCaption;
    overlap->onValueChanged = [state, weakCaption](float value) {
        state->chart->SetOverlap(value);
        char text[48];
        std::snprintf(text, sizeof(text), "Overlap: %.1f row heights", value);   // locale-ok: display text
        if (auto caption = weakCaption.lock()) caption->SetText(text);
    };
    state->scale->onSelectionChanged = [state](int, const DropdownItem&) { ShowSample(*state); };
    state->sort->onSelectionChanged = [state](int, const DropdownItem&) { ShowSample(*state); };
    state->colouring->onSelectionChanged = [state](int, const DropdownItem&) { ApplyColouring(*state); };
    rowLines->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetShowRowLines(now == CheckedState::Checked);
    };
    gradient->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetGradientFill(now == CheckedState::Checked);
    };
    std::weak_ptr<RidgelineDemoState> weak = state;
    state->chart->onRowHover = [weak](int r) {
        auto s = weak.lock();
        if (!s) return;
        s->readout->SetText(r < 0 ? std::string("Hover a curve.")
                                  : s->chart->DescribeRow(static_cast<size_t>(r), s->chart->GetHoveredX()));
    };

    ShowSample(*state);
    ApplyColouring(*state);
    return page;
}

} // namespace UltraCanvas
