// Apps/DemoApp/UltraCanvasRidgelineChartExamples.cpp
// Charts > Ridgeline Chart: one curve per row on its own baseline, rising over
// the rows above (UltraCanvasRidgelineChart). Three invented samples - how
// people might rate words from 0 to 10 (densities of the answers, coloured
// red to green by the average score), when in the day people might do 16
// activities (curves, each to its own peak) and when people of seven age
// groups go to bed and wake up (two series per row on a 24-hour axis that
// wraps) - with the overlap, scale, order, colouring, marks, bands, tail cut
// and histogram look to change, the row under the pointer read out, a click
// pinning a row, and the model opened, saved, imported and exported through
// UltraCanvasFileLoader.
// Version: 1.1.0 - Phase 2: the Sleep sample, marks, quantile bands, highlight,
//                  trimmed tails, histogram, row click, Open / Save JSON,
//                  Import / Export CSV
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
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
    std::shared_ptr<UltraCanvasLabel> readout, pinned;
    std::shared_ptr<UltraCanvasDropdown> sample, scale, sort, colouring, marks;
    bool tails = false, histogram = false;
    std::string sampleId = "words";               // words, day, sleep or file
    UltraCanvasRidgelineModel opened;             // the model a file gave
    std::string openedName;
};

// Hours as a clock time: 24.67 reads 00:40 (the axis runs past midnight).
std::string HourText(double hours) {
    const int minutes = static_cast<int>(std::lround(hours * 60.0)) % (24 * 60);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);   // locale-ok: display text
    return buffer;
}

std::string Selected(const std::shared_ptr<UltraCanvasDropdown>& dropdown, const std::string& fallback) {
    const DropdownItem* item = dropdown->GetSelectedItem();
    return item ? item->value : fallback;
}

// The chosen sample (or the opened file), scaled, sorted and drawn as the
// controls say.
void ShowSample(RidgelineDemoState& state) {
    UltraCanvasRidgelineModel model = state.sampleId == "day"     ? UltraCanvasRidgelineModel::DailyActivities()
                                    : state.sampleId == "sleep"   ? UltraCanvasRidgelineModel::Sleep()
                                    : state.sampleId == "file"    ? state.opened
                                                                  : UltraCanvasRidgelineModel::WordRatings();
    const std::string scale = Selected(state.scale, "shared");
    model.scale = scale == "peak" ? RidgelineScale::RowPeak
                : scale == "area" ? RidgelineScale::RowArea
                                  : RidgelineScale::Shared;
    const std::string sort = Selected(state.sort, "sample");
    if (sort == "mean") model.Sort(RidgelineSort::ByMean);
    else if (sort == "median") model.Sort(RidgelineSort::ByMedian);
    else if (sort == "peak") model.Sort(RidgelineSort::ByPeak, state.sampleId == "day");
    else if (sort == "name") model.Sort(RidgelineSort::ByName);
    model.density.tailCut = state.tails ? 0.02 : 0.0;
    if (state.histogram) {
        model.density.binned = true;
        model.density.bins = 24;
        if (state.sampleId == "words") {
            // Whole-number answers: one bin per answer, centred on it.
            model.density.lowerBound.reset();
            model.density.upperBound.reset();
            model.xMin = -0.5;
            model.xMax = 10.5;
            model.density.bins = 11;
        }
    }

    auto& chart = *state.chart;
    if (state.sampleId == "words") {
        chart.SetXTicks({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        chart.SetXFormatter(nullptr);
        chart.SetValueColumn("Average score", RidgelineValue::Mean, 2);
        chart.SetOutline(Color(40, 40, 40, 255), 1.0f);
    } else if (state.sampleId == "day") {
        chart.SetXTicks({3, 6, 9, 12, 15, 18, 21, 24, 27});
        chart.SetXFormatter(HourText);
        chart.SetValueColumn("Peak", RidgelineValue::Peak);
        chart.SetOutline(Color(255, 255, 255, 255), 1.5f);
    } else if (state.sampleId == "sleep") {
        chart.SetXTicks({18, 21, 24, 27, 30, 33, 36, 39, 42});
        chart.SetXFormatter(HourText);
        chart.SetValueColumn("Bedtime", RidgelineValue::Median);
        chart.SetOutline(Color(0, 0, 0, 0), 1.2f);
    } else {
        chart.SetXTicks({});
        chart.SetXFormatter(nullptr);
        chart.SetValueColumn("Mean", RidgelineValue::Mean, 2);
        chart.SetOutline(Color(0, 0, 0, 0), 1.2f);
    }
    chart.SetFillOpacity(state.sampleId == "sleep" ? 0.7f : 0.85f);
    chart.SetModel(model);
}

void ApplyColouring(RidgelineDemoState& state) {
    const std::string c = Selected(state.colouring, "value");
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
        chart.SetColormap(HeatmapColormap::RdYlGn, state.sampleId == "day");   // late peaks red, early green
    }
}

void ApplyMarks(RidgelineDemoState& state) {
    const std::string m = Selected(state.marks, "none");
    state.chart->SetMarks(m == "mean"        ? unsigned(RidgelineMarkMean)
                          : m == "median"    ? unsigned(RidgelineMarkMedian)
                          : m == "quartiles" ? unsigned(RidgelineMarkMedian | RidgelineMarkQuartiles)
                                             : 0u);
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
    auto addDropdown = [&](const std::string& id, const std::vector<std::pair<std::string, std::string>>& items) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 220, 28);
        for (const auto& [label, value] : items) dropdown->AddItem(label, value);
        dropdown->SetSelectedIndex(0);
        DemoKit::AddStretched(sidebar, dropdown);
        return dropdown;
    };

    addCaption("ridgelineSampleCaption", "Sample");
    state->sample = addDropdown("ridgelineSample", {{"Word ratings (answers 0-10)", "words"},
                                                    {"Times of day (curves)", "day"},
                                                    {"Sleep (two series, 24 hours)", "sleep"}});

    auto overlapCaption = DemoKit::AddCaption(sidebar, "ridgelineOverlapCaption", "Overlap: 3.5 row heights");
    overlapCaption->SetFontWeight(FontWeight::Bold);
    auto overlap = std::make_shared<UltraCanvasSlider>("ridgelineOverlap", 0, 0, 220, 28);
    overlap->SetRange(0.5, 5.0);
    overlap->SetStep(0.1);
    overlap->SetValue(3.5);
    overlap->SetValueDisplay(SliderValueDisplay::NoDisplay);
    DemoKit::AddStretched(sidebar, overlap);

    addCaption("ridgelineScaleCaption", "Scale");
    state->scale = addDropdown("ridgelineScale", {{"One scale for all rows", "shared"},
                                                  {"Each row to its own peak", "peak"},
                                                  {"Each curve the same area", "area"}});
    addCaption("ridgelineSortCaption", "Order");
    state->sort = addDropdown("ridgelineSort", {{"As the sample sorts it", "sample"}, {"By mean", "mean"},
                                                {"By median", "median"}, {"By peak", "peak"}, {"By name", "name"}});
    addCaption("ridgelineColourCaption", "Colour");
    state->colouring = addDropdown("ridgelineColour", {{"By the value column", "value"}, {"Down the rows", "rows"},
                                                       {"Alternating", "alternating"}, {"One colour", "single"}});
    addCaption("ridgelineMarksCaption", "Marks");
    state->marks = addDropdown("ridgelineMarks", {{"None", "none"}, {"Mean", "mean"}, {"Median", "median"},
                                                  {"Median and quartiles", "quartiles"}});

    auto bands = UltraCanvasSwitch::Create("ridgelineBands", 0, 0, "Shade the middle half", false);
    DemoKit::Add(sidebar, bands);
    auto tails = UltraCanvasSwitch::Create("ridgelineTails", 0, 0, "Trim the tails", false);
    DemoKit::Add(sidebar, tails);
    auto histogram = UltraCanvasSwitch::Create("ridgelineHistogram", 0, 0, "Histogram (count in bins)", false);
    DemoKit::Add(sidebar, histogram);
    auto highlight = UltraCanvasSwitch::Create("ridgelineHighlight", 0, 0, "Fade the other rows", false);
    DemoKit::Add(sidebar, highlight);
    auto rowLines = UltraCanvasSwitch::Create("ridgelineRowLines", 0, 0, "Row lines", false);
    DemoKit::Add(sidebar, rowLines);
    auto gradient = UltraCanvasSwitch::Create("ridgelineGradient", 0, 0, "Gradient fill", false);
    DemoKit::Add(sidebar, gradient);

    addCaption("ridgelineFilesCaption", "Files");
    auto openJson = std::make_shared<UltraCanvasButton>("ridgelineOpenJson", 0, 0, 220, 28, "Open JSON");
    DemoKit::AddStretched(sidebar, openJson);
    auto saveJson = std::make_shared<UltraCanvasButton>("ridgelineSaveJson", 0, 0, 220, 28, "Save JSON");
    DemoKit::AddStretched(sidebar, saveJson);
    auto importCsv = std::make_shared<UltraCanvasButton>("ridgelineImportCsv", 0, 0, 220, 28, "Import CSV");
    DemoKit::AddStretched(sidebar, importCsv);
    auto exportCsv = std::make_shared<UltraCanvasButton>("ridgelineExportCsv", 0, 0, 220, 28, "Export CSV");
    DemoKit::AddStretched(sidebar, exportCsv);

    addCaption("ridgelineReadoutCaption", "Under the pointer");
    state->readout = DemoKit::AddCaption(sidebar, "ridgelineReadout", "Hover a curve.");
    state->readout->SetTextColor(DemoKit::BodyText());
    addCaption("ridgelinePinnedCaption", "Clicked");
    state->pinned = DemoKit::AddCaption(sidebar, "ridgelinePinned", "Click a curve to keep its numbers here.");
    state->pinned->SetTextColor(DemoKit::BodyText());

    state->sample->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->sampleId = item.value;
        // Each sample reads best one way: the words to one scale, coloured by
        // their average; the activities each to its own peak, alternating;
        // sleep to one scale, the two series in their own colours.
        state->scale->SetSelectedIndex(item.value == "day" ? 1 : 0);
        state->colouring->SetSelectedIndex(item.value == "day" ? 2 : 0);
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
    state->marks->onSelectionChanged = [state](int, const DropdownItem&) { ApplyMarks(*state); };
    bands->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetQuantileBands(now == CheckedState::Checked);
    };
    tails->onStateChanged = [state](CheckedState, CheckedState now) {
        state->tails = now == CheckedState::Checked;
        ShowSample(*state);
    };
    histogram->onStateChanged = [state](CheckedState, CheckedState now) {
        state->histogram = now == CheckedState::Checked;
        ShowSample(*state);
    };
    highlight->onStateChanged = [state](CheckedState, CheckedState now) {
        state->chart->SetHighlightOnHover(now == CheckedState::Checked);
    };
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
    state->chart->onRowClick = [weak](int r) {
        auto s = weak.lock();
        if (!s || r < 0) return;
        s->pinned->SetText(s->chart->DescribeRow(static_cast<size_t>(r), s->chart->GetHoveredX()));
    };

    // ===== FILES (through UltraCanvasFileLoader, as every user file) =====
    auto fileName = [](const std::string& path) { return PathToUtf8(PathFromUtf8(path).filename()); };
    auto readText = [state, fileName](const std::string& path, std::string& text) {
        const FileBytesResult loaded = UltraCanvasFileLoader::LoadFile(path);
        if (!loaded.success) {
            state->pinned->SetText("Could not open " + fileName(path) + ": " + loaded.error);
            return false;
        }
        text.assign(loaded.bytes.begin(), loaded.bytes.end());
        return true;
    };
    auto writeText = [state, fileName](const std::string& path, const std::string& text) {
        std::string error;
        if (UltraCanvasFileLoader::SaveFile(path, std::vector<uint8_t>(text.begin(), text.end()), error)) {
            state->pinned->SetText("Saved " + fileName(path) + ".");
        } else {
            state->pinned->SetText("Could not write " + fileName(path) + ": " + error);
        }
    };
    // Shows a model a file gave, under the controls' scale and order.
    auto showOpened = [state](UltraCanvasRidgelineModel model, const std::string& name) {
        state->opened = std::move(model);
        state->openedName = name;
        state->sampleId = "file";
        ShowSample(*state);
        ApplyColouring(*state);
    };
    openJson->onClick = [state, readText, fileName, showOpened]() {
        FileDialogOptions opts;
        opts.SetTitle("Open a ridgeline chart")
            .AddFilter("Ridgeline chart (*.json)", "json")
            .AddFilter("All files", "*");
        UltraCanvasFileLoader::OpenFileDialog(opts, [state, readText, fileName, showOpened](DialogResult result,
                                                                                         const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            std::string text, error;
            if (!readText(path, text)) return;
            UltraCanvasRidgelineModel model;
            if (!ImportRidgelineJson(text, model, &error)) {
                state->pinned->SetText("Could not read " + fileName(path) + ": " + error);
                return;
            }
            const size_t rows = model.rows.size();
            showOpened(std::move(model), fileName(path));
            state->pinned->SetText("Opened " + fileName(path) + ": " + std::to_string(rows) + " rows.");
        });
    };
    saveJson->onClick = [state, writeText]() {
        FileDialogOptions opts;
        opts.SetTitle("Save the ridgeline chart")
            .AddFilter("Ridgeline chart (*.json)", "json")
            .SetDefaultExtension("json")
            .SetDefaultFileName("ridgeline.json");
        const std::string text = ExportRidgelineJson(state->chart->GetModel());
        UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            writeText(path, text);
        });
    };
    // Import CSV replaces the rows of the chart shown and keeps its settings.
    importCsv->onClick = [state, readText, fileName, showOpened]() {
        FileDialogOptions opts;
        opts.SetTitle("Import rows")
            .AddFilter("Comma-separated values (*.csv)", "csv")
            .AddFilter("All files", "*");
        UltraCanvasFileLoader::OpenFileDialog(opts, [state, readText, fileName, showOpened](DialogResult result,
                                                                                         const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            std::string text, error;
            if (!readText(path, text)) return;
            UltraCanvasRidgelineModel model = state->chart->GetModel();
            if (!ImportRidgelineCsv(text, model, &error)) {
                state->pinned->SetText("Could not import " + fileName(path) + ": " + error);
                return;
            }
            model.title = fileName(path);
            model.subtitle.clear();
            const size_t rows = model.rows.size();
            showOpened(std::move(model), fileName(path));
            state->pinned->SetText("Imported " + std::to_string(rows) + " rows from " + fileName(path) + ".");
        });
    };
    exportCsv->onClick = [state, writeText]() {
        FileDialogOptions opts;
        opts.SetTitle("Export the rows")
            .AddFilter("Comma-separated values (*.csv)", "csv")
            .SetDefaultExtension("csv")
            .SetDefaultFileName("ridgeline.csv");
        const std::string text = ExportRidgelineCsv(state->chart->GetModel());
        UltraCanvasFileLoader::SaveFileDialog(opts, [text, writeText](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            writeText(path, text);
        });
    };

    ShowSample(*state);
    ApplyColouring(*state);
    return page;
}

} // namespace UltraCanvas
