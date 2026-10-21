// Apps/DemoApp/UltraCanvasEngineComparisonFinance.cpp
// The "Finance & Mosaics" family of the Chart Engine Comparison page: the
// legacy financial, heatmap and Mekko charts next to their engine ports, fed
// the same data, with the options that matter for the comparison applied to
// both sides at once.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasEngineComparison.h"
#include "Plugins/Charts/UltraCanvasFinancialChart.h"
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasMekkoChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineMekkoChart.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace UltraCanvas {
namespace EngineComparison {

namespace {

// =============================================================================
// SHARED CONTROLS
// =============================================================================

std::shared_ptr<UltraCanvasDropdown> MakeDropdown(const std::string& id,
                                                  const std::vector<std::string>& items,
                                                  int selected, int width = 130) {
    auto dropdown = std::make_shared<UltraCanvasDropdown>(id, width, 24);
    for (const std::string& item : items) dropdown->AddItem(item);
    dropdown->SetSelectedIndex(selected, false);
    return dropdown;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheck(const std::string& id, const std::string& text,
                                               bool checked, int width = 120) {
    auto box = std::make_shared<UltraCanvasCheckbox>(id, width, 24);
    box->SetText(text);
    box->SetChecked(checked);
    return box;
}

// Theme dropdown and replay button for the engine side - the legacy charts
// have neither, which is part of what the comparison shows.
void AddEngineExtras(UltraCanvasContainer& strip, const std::string& idPrefix,
                     std::shared_ptr<UltraCanvasChartEngineElement> engine) {
    AddStripLabel(strip, idPrefix + "ThemeLabel", "Engine theme:", 90);
    auto themes = MakeDropdown(idPrefix + "Theme", ChartThemes::Names(), 0, 120);
    themes->onSelectionChanged = [engine](int, const DropdownItem& item) {
        engine->SetTheme(item.text);
    };
    AddToStrip(strip, themes);

    auto replay = std::make_shared<UltraCanvasButton>(idPrefix + "Replay", 0, 0, 120, 24);
    replay->SetText("Replay animation");
    replay->onClick = [engine]() { engine->StartEngineAnimation(0.7f); };
    AddToStrip(strip, replay);
}

// =============================================================================
// FINANCIAL
// =============================================================================

// 90 trading days of a seeded random walk with a slight upward drift - the
// same bars for both charts.
std::vector<ChartOHLCBar> MakeStockBars() {
    std::mt19937 gen(20261020);
    std::uniform_real_distribution<> volatility(-2.0, 2.0);
    std::uniform_real_distribution<> volumeDist(1000000, 5000000);
    std::vector<ChartOHLCBar> bars;
    double basePrice = 100.0;
    for (int day = 0; day < 90; ++day) {
        basePrice += 0.15 + volatility(gen);
        const double open = basePrice + volatility(gen);
        const double close = open + volatility(gen) * 1.5;
        const double high = std::max(open, close) + std::abs(volatility(gen)) * 0.5;
        const double low = std::min(open, close) - std::abs(volatility(gen)) * 0.5;
        std::ostringstream date;
        date << "2024-" << std::setfill('0') << std::setw(2) << (day / 30) + 1 << "-"
             << std::setw(2) << (day % 30) + 1;
        bars.emplace_back(date.str(), open, high, low, close, volumeDist(gen));
    }
    return bars;
}

std::shared_ptr<UltraCanvasUIElement> BuildFinancialTab() {
    const std::vector<ChartOHLCBar> bars = MakeStockBars();

    auto legacyData = std::make_shared<FinancialChartDataVector>();
    for (size_t i = 0; i < bars.size(); ++i) {
        const ChartOHLCBar& b = bars[i];
        legacyData->AddFinancialPoint(FinancialChartDataPoint(
            static_cast<double>(i), b.open, b.high, b.low, b.close, b.volume, b.date));
    }
    auto legacy = CreateFinancialChartElement("CmpFinLegacy", 0, 0, 0, 0);
    legacy->SetFinancialDataSource(legacyData);
    legacy->SetChartTitle("FCHI - Fantasy Chart Inc. (90 days)");
    legacy->SetShowVolumePanel(true);
    legacy->SetVolumePanelHeightRatio(0.25f);
    legacy->SetShowMovingAverage(true, 20);
    legacy->SetMovingAverageColor(Color(0, 100, 200, 255));
    legacy->SetBullishCandleColor(Color(0, 180, 0, 255));
    legacy->SetBearishCandleColor(Color(220, 0, 0, 255));
    legacy->SetEnableTooltips(true);

    auto engine = CreateEngineFinancialChart("CmpFinEngine");
    engine->SetData(bars);
    engine->SetChartTitle("FCHI - Fantasy Chart Inc. (90 days)");
    engine->SetShowVolumePane(true);
    engine->SetVolumePaneRatio(0.25);
    engine->SetShowMovingAverage(true, 20);
    engine->SetMovingAverageColor(Color(0, 100, 200, 255));
    engine->SetBullishColor(Color(0, 180, 0, 255));
    engine->SetBearishColor(Color(220, 0, 0, 255));

    auto strip = MakeControlStrip("CmpFin");
    AddStripLabel(*strip, "CmpFinStyleLabel", "Style:", 40);
    auto style = MakeDropdown("CmpFinStyle", {"Candlestick", "OHLC Bars", "Heikin-Ashi"}, 0);
    style->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        using LegacyStyle = UltraCanvasFinancialChartElement::CandleDisplayStyle;
        legacy->SetCandleDisplayStyle(index == 1 ? LegacyStyle::OHLCBars
                                      : index == 2 ? LegacyStyle::HeikinAshi
                                                   : LegacyStyle::Candlestick);
        engine->SetCandleStyle(index == 1 ? ChartCandleStyle::OHLCBars
                               : index == 2 ? ChartCandleStyle::HeikinAshi
                                            : ChartCandleStyle::Candlestick);
    };
    AddToStrip(*strip, style);

    auto volume = MakeCheck("CmpFinVolume", "Volume", true, 90);
    volume->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetShowVolumePanel(on);
        engine->SetShowVolumePane(on);
    };
    AddToStrip(*strip, volume);

    AddStripLabel(*strip, "CmpFinMALabel", "Moving average:", 100);
    auto average = MakeDropdown("CmpFinMA", {"Off", "SMA 20", "SMA 50", "EMA 20 (engine)"}, 1);
    average->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const int period = index == 2 ? 50 : 20;
        // The legacy chart has no exponential average; it keeps the SMA.
        legacy->SetShowMovingAverage(index != 0, period);
        engine->SetShowMovingAverage(index != 0, period,
                                     index == 3 ? ChartMovingAverageKind::Exponential
                                                : ChartMovingAverageKind::Simple);
    };
    AddToStrip(*strip, average);

    auto volumeAverage = MakeCheck("CmpFinVolMA", "Volume SMA", false, 110);
    volumeAverage->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetShowVolumeMovingAverage(on, 20);
        engine->SetShowVolumeMovingAverage(on, 20);
    };
    AddToStrip(*strip, volumeAverage);
    AddEngineExtras(*strip, "CmpFin", engine);

    return MakeComparisonTab(
        "CmpFin",
        "Candlestick / OHLC / Heikin-Ashi over 90 days with a volume pane and moving averages. The port puts price and volume in two\n"
        "sub-bands of one plot (shared date axis, engine grid), computes real Heikin-Ashi candles, adds an EMA, a crosshair and a legend.",
        "UltraCanvasFinancialChartElement", legacy,
        "UltraCanvasEngineFinancialChart", engine, strip);
}

// =============================================================================
// HEATMAP
// =============================================================================

const std::vector<std::pair<std::string, HeatmapColormap>>& ColormapChoices() {
    static const std::vector<std::pair<std::string, HeatmapColormap>> choices = {
        {"Inferno", HeatmapColormap::Inferno}, {"Viridis", HeatmapColormap::Viridis},
        {"Magma", HeatmapColormap::Magma},     {"Turbo", HeatmapColormap::Turbo},
        {"Blues", HeatmapColormap::Blues},     {"RdBu", HeatmapColormap::RdBu},
        {"Coolwarm", HeatmapColormap::Coolwarm}};
    return choices;
}

// The legacy demo's "Peaks" preset: three 2-D gaussians on a 48 x 32 grid.
std::vector<double> MakePeaks(int cols, int rows) {
    struct Peak { double cx, cy, amp, sigma; };
    const Peak peaks[] = {{0.30, 0.35, 1.0, 0.12}, {0.70, 0.55, 0.8, 0.16}, {0.50, 0.80, 0.6, 0.10}};
    std::vector<double> v(static_cast<size_t>(cols * rows), 0.0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const double nx = (c + 0.5) / cols, ny = (r + 0.5) / rows;
            double s = 0.0;
            for (const Peak& p : peaks) {
                const double dx = nx - p.cx, dy = ny - p.cy;
                s += p.amp * std::exp(-(dx * dx + dy * dy) / (2 * p.sigma * p.sigma));
            }
            v[static_cast<size_t>(r * cols + c)] = s;
        }
    }
    return v;
}

std::shared_ptr<UltraCanvasUIElement> BuildHeatmapTab() {
    const int cols = 48, rows = 32;
    const std::vector<double> peaks = MakePeaks(cols, rows);

    auto legacy = CreateHeatmapChartElement("CmpHeatLegacy", 0, 0, 0, 0);
    legacy->SetData(peaks, cols, rows);
    legacy->SetColormap(HeatmapColormap::Inferno);
    legacy->SetScale(HeatmapScale::Logarithmic);
    legacy->SetShowColorBar(true);
    legacy->SetChartTitle("Peaks - three gaussians, log scale");

    auto engine = CreateEngineHeatmapChart("CmpHeatEngine");
    engine->SetData(peaks, cols, rows);
    engine->SetColormap(HeatmapColormap::Inferno);
    engine->SetScale(ChartHeatmapScale::Logarithmic);
    engine->SetShowColorBar(true);
    engine->SetChartTitle("Peaks - three gaussians, log scale");

    auto strip = MakeControlStrip("CmpHeat");
    AddStripLabel(*strip, "CmpHeatMapLabel", "Colour map:", 75);
    std::vector<std::string> names;
    for (const auto& choice : ColormapChoices()) names.push_back(choice.first);
    auto colormap = MakeDropdown("CmpHeatMap", names, 0, 110);
    colormap->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const HeatmapColormap map = ColormapChoices()[static_cast<size_t>(index)].second;
        legacy->SetColormap(map);
        engine->SetColormap(map);
    };
    AddToStrip(*strip, colormap);

    auto scale = MakeDropdown("CmpHeatScale", {"Linear", "Logarithmic"}, 1, 110);
    scale->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        legacy->SetScale(index == 1 ? HeatmapScale::Logarithmic : HeatmapScale::Linear);
        engine->SetScale(index == 1 ? ChartHeatmapScale::Logarithmic : ChartHeatmapScale::Linear);
    };
    AddToStrip(*strip, scale);

    auto shape = MakeDropdown("CmpHeatShape", {"Rectangles", "Rounded", "Circles"}, 0, 110);
    shape->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        legacy->SetCellShape(index == 1 ? HeatmapCellShape::RoundedRectangle
                             : index == 2 ? HeatmapCellShape::Circle : HeatmapCellShape::Rectangle);
        engine->SetCellShape(index == 1 ? ChartHeatmapCellShape::RoundedRectangle
                             : index == 2 ? ChartHeatmapCellShape::Circle
                                          : ChartHeatmapCellShape::Rectangle);
        const double gap = index == 0 ? 0.0 : 0.12;
        legacy->SetCellGap(gap);
        engine->SetCellGap(gap);
    };
    AddToStrip(*strip, shape);

    auto levels = MakeCheck("CmpHeatLevels", "6 bands", false, 85);
    levels->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const int count = state == CheckedState::Checked ? 6 : 0;
        legacy->SetColorLevels(count);
        engine->SetColorLevels(count);
    };
    AddToStrip(*strip, levels);

    auto reverse = MakeCheck("CmpHeatReverse", "Reverse", false, 80);
    reverse->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetReverseColormap(on);
        engine->SetReverseColormap(on);
    };
    AddToStrip(*strip, reverse);
    AddEngineExtras(*strip, "CmpHeat", engine);

    return MakeComparisonTab(
        "CmpHeat",
        "A 48 x 32 density grid through a colour map, keyed by a colour bar. The port's rows and columns are engine Category axes (dense labels\n"
        "declutter instead of overprinting), the key is the shared legend in ColorBar mode with log ticks read back through the cells' own mapping.",
        "UltraCanvasHeatmapChartElement", legacy,
        "UltraCanvasEngineHeatmapChart", engine, strip);
}

// A symmetric 12 x 12 correlation matrix in [-1, 1] (the legacy demo's
// "Correlation" preset), labelled, with cell values.
std::shared_ptr<UltraCanvasUIElement> BuildCorrelationTab() {
    const int n = 12;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<double> base(n);
    for (double& b : base) b = uni(rng) * 2.0 - 1.0;
    std::vector<double> v(static_cast<size_t>(n * n));
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const double corr = 0.4 * base[r] * base[c] + 0.6 * (uni(rng) * 2 - 1);
            v[static_cast<size_t>(r * n + c)] = (r == c) ? 1.0 : std::clamp(corr, -1.0, 1.0);
        }
    }
    for (int r = 0; r < n; ++r) {
        for (int c = r + 1; c < n; ++c) v[static_cast<size_t>(c * n + r)] = v[static_cast<size_t>(r * n + c)];
    }
    const std::vector<std::string> labels = {"Revenue", "Margin", "Churn", "NPS", "Visits", "Signups",
                                             "Tickets", "Uptime", "Latency", "Spend", "Leads", "Deals"};
    auto rounded = [](double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << value;
        return out.str();
    };

    auto legacy = CreateHeatmapChartElement("CmpCorrLegacy", 0, 0, 0, 0);
    legacy->SetData(v, n, n);
    legacy->SetColormap(HeatmapColormap::RdBu);
    legacy->SetValueRange(-1.0, 1.0);
    legacy->SetDiverging(true, 0.0);
    legacy->SetShowCellValues(true);
    legacy->SetShowCellBorders(true, Color(255, 255, 255, 255), 1.0f);
    legacy->SetColumnLabels(labels);
    legacy->SetRowLabels(labels);
    legacy->SetChartTitle("Metric correlation (diverging around 0)");

    auto engine = CreateEngineHeatmapChart("CmpCorrEngine");
    engine->SetData(v, n, n);
    engine->SetColormap(HeatmapColormap::RdBu);
    engine->SetValueRange(-1.0, 1.0);
    engine->SetDiverging(true, 0.0);
    engine->SetShowCellValues(true);
    engine->SetShowCellBorders(true, Color(255, 255, 255, 255), 1.0f);
    engine->SetValueFormatter(rounded);
    engine->SetColumnLabels(labels);
    engine->SetRowLabels(labels);
    engine->SetChartTitle("Metric correlation (diverging around 0)");

    auto strip = MakeControlStrip("CmpCorr");
    AddStripLabel(*strip, "CmpCorrMaskLabel", "Mask:", 40);
    auto mask = MakeDropdown("CmpCorrMask", {"Full matrix", "Lower", "Lower, no diagonal",
                                             "Upper", "Upper, no diagonal"}, 0, 150);
    mask->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const HeatmapTriangularMask legacyMasks[] = {
            HeatmapTriangularMask::NoMask, HeatmapTriangularMask::Lower,
            HeatmapTriangularMask::LowerNoDiagonal, HeatmapTriangularMask::Upper,
            HeatmapTriangularMask::UpperNoDiagonal};
        const ChartHeatmapMask engineMasks[] = {
            ChartHeatmapMask::NoMask, ChartHeatmapMask::Lower, ChartHeatmapMask::LowerNoDiagonal,
            ChartHeatmapMask::Upper, ChartHeatmapMask::UpperNoDiagonal};
        const size_t i = static_cast<size_t>(std::clamp(index, 0, 4));
        legacy->SetTriangularMask(legacyMasks[i]);
        engine->SetTriangularMask(engineMasks[i]);
    };
    AddToStrip(*strip, mask);

    auto values = MakeCheck("CmpCorrValues", "Cell values", true, 100);
    values->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetShowCellValues(on);
        engine->SetShowCellValues(on);
    };
    AddToStrip(*strip, values);

    auto bottomUp = MakeCheck("CmpCorrBottomUp", "Row 0 at bottom", false, 130);
    bottomUp->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetRowOrder(on ? HeatmapRowOrder::BottomUp : HeatmapRowOrder::TopDown);
        engine->SetRowOrder(on ? ChartHeatmapRowOrder::BottomUp : ChartHeatmapRowOrder::TopDown);
    };
    AddToStrip(*strip, bottomUp);

    auto transpose = MakeCheck("CmpCorrTranspose", "Transpose (engine)", false, 140);
    transpose->onStateChanged = [engine](CheckedState, CheckedState state) {
        engine->SetProjectionKind(state == CheckedState::Checked ? ChartProjectionKind::Horizontal
                                                                 : ChartProjectionKind::Vertical);
    };
    AddToStrip(*strip, transpose);
    AddEngineExtras(*strip, "CmpCorr", engine);

    return MakeComparisonTab(
        "CmpCorr",
        "A labelled 12 x 12 correlation matrix on a diverging map centred on 0, with cell values and triangular masks. In the port the\n"
        "row and column names are axis furniture measured into the layout, and the same cells render transposed under the Horizontal projection.",
        "UltraCanvasHeatmapChartElement", legacy,
        "UltraCanvasEngineHeatmapChart", engine, strip);
}

// =============================================================================
// MEKKO
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildMekkoTab() {
    struct Seed { const char* name; Color color; };
    const Seed seeds[] = {{"USA", Color(25, 60, 130, 255)}, {"China", Color(235, 75, 105, 255)},
                          {"Europe", Color(65, 145, 225, 255)}, {"Japan", Color(250, 190, 40, 255)}};
    const std::vector<std::pair<std::string, std::vector<double>>> columnData = {
        {"Technology", {88, 44, 30, 21}}, {"Transportation", {52, 38, 34, 15}},
        {"Energy", {35, 42, 18, 10}},     {"Education", {40, 30, 26, 16}},
        {"Finance", {95, 55, 48, 22}},    {"Tourism", {41, 25, 51, 18}}};

    auto legacyData = std::make_shared<MekkoChartDataVector>();
    std::vector<ChartMekkoSeries> series;
    for (const Seed& seed : seeds) {
        legacyData->AddSeries(seed.name, seed.color);
        series.emplace_back(seed.name, seed.color);
    }
    std::vector<ChartMekkoColumn> columns;
    for (const auto& [label, values] : columnData) {
        legacyData->AddColumn(label, values);
        columns.emplace_back(label, values);
    }

    using Legacy = UltraCanvasMekkoChartElement;
    auto legacy = CreateMekkoChartWithData("CmpMekkoLegacy", 0, 0, 0, 0, legacyData,
                                           "Market Share by Sector ($B)");
    legacy->SetMekkoMode(Legacy::MekkoMode::Marimekko);
    legacy->SetColumnHeaderMode(Legacy::ColumnHeaderMode::WidthPercent);
    legacy->SetSegmentLabelMode(Legacy::SegmentLabelMode::Value);
    legacy->SetLegendPosition(Legacy::LegendPosition::Right);
    legacy->SetValueUnits("$", "B");

    auto engine = CreateEngineMekkoChart("CmpMekkoEngine");
    engine->SetData(series, columns);
    engine->SetChartTitle("Market Share by Sector ($B)");
    engine->SetMekkoMode(ChartMekkoMode::Marimekko);
    engine->SetColumnHeaderMode(ChartMekkoHeader::WidthPercent);
    engine->SetSegmentLabelMode(ChartMekkoSegmentLabel::Value);
    engine->SetValueUnits("$", "B");

    auto strip = MakeControlStrip("CmpMekko");
    auto mode = MakeDropdown("CmpMekkoMode", {"Marimekko", "Bar-Mekko"}, 0, 110);
    mode->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        legacy->SetMekkoMode(index == 1 ? Legacy::MekkoMode::BarMekko : Legacy::MekkoMode::Marimekko);
        engine->SetMekkoMode(index == 1 ? ChartMekkoMode::BarMekko : ChartMekkoMode::Marimekko);
    };
    AddToStrip(*strip, mode);

    AddStripLabel(*strip, "CmpMekkoLabelsLabel", "Segments:", 65);
    auto labels = MakeDropdown("CmpMekkoLabels", {"Value", "Percent", "Value (percent)", "Series",
                                                  "None"}, 0, 130);
    labels->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const Legacy::SegmentLabelMode legacyModes[] = {
            Legacy::SegmentLabelMode::Value, Legacy::SegmentLabelMode::Percent,
            Legacy::SegmentLabelMode::ValueAndPercent, Legacy::SegmentLabelMode::SeriesName,
            Legacy::SegmentLabelMode::NoLabels};
        const ChartMekkoSegmentLabel engineModes[] = {
            ChartMekkoSegmentLabel::Value, ChartMekkoSegmentLabel::Percent,
            ChartMekkoSegmentLabel::ValueAndPercent, ChartMekkoSegmentLabel::SeriesName,
            ChartMekkoSegmentLabel::NoLabels};
        const size_t i = static_cast<size_t>(std::clamp(index, 0, 4));
        legacy->SetSegmentLabelMode(legacyModes[i]);
        engine->SetSegmentLabelMode(engineModes[i]);
    };
    AddToStrip(*strip, labels);

    AddStripLabel(*strip, "CmpMekkoSortLabel", "Sort:", 35);
    auto sort = MakeDropdown("CmpMekkoSort", {"Data order", "Largest first", "Smallest first",
                                              "A-Z"}, 0, 120);
    sort->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const Legacy::ColumnSortMode legacyModes[] = {
            Legacy::ColumnSortMode::DataOrder, Legacy::ColumnSortMode::TotalDescending,
            Legacy::ColumnSortMode::TotalAscending, Legacy::ColumnSortMode::Alphabetical};
        const ChartMekkoColumnSort engineModes[] = {
            ChartMekkoColumnSort::DataOrder, ChartMekkoColumnSort::TotalDescending,
            ChartMekkoColumnSort::TotalAscending, ChartMekkoColumnSort::Alphabetical};
        const size_t i = static_cast<size_t>(std::clamp(index, 0, 3));
        legacy->SetColumnSortMode(legacyModes[i]);
        engine->SetColumnSort(engineModes[i]);
    };
    AddToStrip(*strip, sort);

    AddStripLabel(*strip, "CmpMekkoHeaderLabel", "Headers:", 55);
    auto headers = MakeDropdown("CmpMekkoHeader", {"Width %", "Total", "Total (%)", "None"}, 0, 100);
    headers->onSelectionChanged = [legacy, engine](int index, const DropdownItem&) {
        const Legacy::ColumnHeaderMode legacyModes[] = {
            Legacy::ColumnHeaderMode::WidthPercent, Legacy::ColumnHeaderMode::Total,
            Legacy::ColumnHeaderMode::TotalAndPercent, Legacy::ColumnHeaderMode::NoHeader};
        const ChartMekkoHeader engineModes[] = {
            ChartMekkoHeader::WidthPercent, ChartMekkoHeader::Total,
            ChartMekkoHeader::TotalAndPercent, ChartMekkoHeader::NoHeader};
        const size_t i = static_cast<size_t>(std::clamp(index, 0, 3));
        legacy->SetColumnHeaderMode(legacyModes[i]);
        engine->SetColumnHeaderMode(engineModes[i]);
    };
    AddToStrip(*strip, headers);

    auto cumulative = MakeCheck("CmpMekkoCumulative", "Cumulative %", false, 115);
    cumulative->onStateChanged = [legacy, engine](CheckedState, CheckedState state) {
        const bool on = state == CheckedState::Checked;
        legacy->SetShowCumulativeXAxis(on);
        engine->SetShowCumulativeAxis(on);
    };
    AddToStrip(*strip, cumulative);
    AddEngineExtras(*strip, "CmpMekko", engine);

    return MakeComparisonTab(
        "CmpMekko",
        "Variable-width 100% stacked columns: width = the sector's share of the total. The port's domain is a 0-100% axis, the column names and\n"
        "cumulative ticks are stacked edge axes, headers ride the label plan; click a legend entry to drop a region and watch the widths re-share.",
        "UltraCanvasMekkoChartElement", legacy,
        "UltraCanvasEngineMekkoChart", engine, strip);
}

} // namespace

std::vector<ComparisonTab> FinanceAndGridTabs() {
    return {
        {"Candlestick / OHLC", BuildFinancialTab},
        {"Heatmap", BuildHeatmapTab},
        {"Correlation Matrix", BuildCorrelationTab},
        {"Mekko", BuildMekkoTab},
    };
}

} // namespace EngineComparison
} // namespace UltraCanvas
