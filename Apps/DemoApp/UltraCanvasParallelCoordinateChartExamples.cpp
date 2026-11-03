// Apps/DemoApp/UltraCanvasParallelCoordinateChartExamples.cpp
// Parallel coordinate chart demonstration - the chart engine's first native
// client. Three examples mirror the reference images from the proposal: the
// Iris common-scale z-score look, a dense hyper-parameter sweep with brushes
// and value colouring, and curved group bundles.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, the two small charts side by side while they fit,
//                  the curved chart as wide as the page
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasParallelCoordinateChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <cmath>
#include <vector>

namespace UltraCanvas {

namespace {

// Deterministic pseudo-random stream so the page looks the same on every run.
struct DemoRandom {
    uint32_t state = 20260801u;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;
    }
    double Range(double lo, double hi) { return lo + (hi - lo) * Next(); }
};

// Compact Iris-like clusters: three species with the classic separations.
void FillIrisLike(std::shared_ptr<UltraCanvasParallelCoordinateChartElement>& chart) {
    DemoRandom rng;
    std::vector<double> petalLength, petalWidth, sepalLength, sepalWidth;
    std::vector<std::string> species;

    struct Cluster {
        const char* name;
        double pl, pw, sl, sw;      // cluster centres
        double spread;
    };
    const Cluster clusters[] = {
        {"setosa",     1.45, 0.25, 5.0, 3.4, 0.18},
        {"versicolor", 4.25, 1.32, 5.9, 2.8, 0.30},
        {"virginica",  5.55, 2.02, 6.6, 3.0, 0.33},
    };
    for (const Cluster& c : clusters) {
        for (int i = 0; i < 25; ++i) {
            petalLength.push_back(c.pl + rng.Range(-c.spread, c.spread) * 2.0);
            petalWidth.push_back(c.pw + rng.Range(-c.spread, c.spread));
            sepalLength.push_back(c.sl + rng.Range(-c.spread, c.spread) * 2.0);
            sepalWidth.push_back(c.sw + rng.Range(-c.spread, c.spread) * 1.5);
            species.push_back(c.name);
        }
    }
    chart->AddDimension("Petal Length", petalLength);
    chart->AddDimension("Petal Width", petalWidth);
    chart->AddDimension("Sepal Length", sepalLength);
    chart->AddDimension("Sepal Width", sepalWidth);
    chart->SetRecordGroups(species);
}

} // namespace

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateParallelCoordinateChartExamples() {
    auto page = DemoKit::CreatePage("PCPContainer",
        "Parallel Coordinate Chart - Multi-Dimensional Records as Polylines",
        "Each dimension gets its own vertical axis; one record becomes one line. Only neighbouring axes "
        "compare directly, so the axis order is interactive: drag an axis header (just above its top) to "
        "reorder, double-click an axis to invert it, drag along an axis to brush a range (click the axis "
        "to clear), hover a line for its values, click a line to pin it.");

    // The two small charts share a row; a card is never narrower than its
    // chart, so on a narrow page the second one moves under the first.
    auto topRow = DemoKit::AddRow(page, "PCPTopRow");

    // ===== EXAMPLE 1: Iris - common scale, z-scored, grouped (top left) =====
    auto irisCard = DemoKit::AddCard(topRow, "PCPIrisCard", "", "",
                                     560 + 2 * DemoKit::kCardPadding + 2);
    DemoKit::AddHeading(irisCard, "PCPIrisLabel",
        "Common scale + z-score per column, coloured by species (Iris look)");

    auto iris = CreateParallelCoordinateChartElement("pcpIris", 0, 0, 560, 320);
    FillIrisLike(iris);
    iris->SetNormalizationMode(PCPNormalization::CommonScale);
    iris->SetStandardize(PCPStandardize::ZScore);
    iris->SetShowVertexMarkers(true, 2.5f);
    iris->SetLineAlpha(0.75f);
    iris->SetChartTitle("Iris (standardised)");
    iris->SetGroupColors({{"setosa", Color(68, 1, 84, 255)},
                          {"versicolor", Color(33, 145, 140, 255)},
                          {"virginica", Color(253, 231, 37, 255)}});
    DemoKit::AddFixed(irisCard, iris);

    // ===== EXAMPLE 2: Hyper-parameter sweep - dense, value-coloured, brushed =====
    auto sweepCard = DemoKit::AddCard(topRow, "PCPSweepCard", "", "",
                                      580 + 2 * DemoKit::kCardPadding + 2);
    DemoKit::AddHeading(sweepCard, "PCPSweepLabel",
        "Dense sweep: per-axis ranges, coloured by accuracy, brushed (dimmed context)");

    auto sweep = CreateParallelCoordinateChartElement("pcpSweep", 0, 0, 580, 320);
    {
        DemoRandom rng;
        std::vector<double> batch, channels, learningRate, dropout, accuracy;
        for (int i = 0; i < 260; ++i) {
            const double lr = std::pow(10.0, rng.Range(-4.0, -1.0));
            const double b = std::floor(rng.Range(3.0, 9.0)) * 16.0;
            const double ch = std::floor(rng.Range(2.0, 8.0)) * 32.0;
            const double dr = rng.Range(0.0, 0.6);
            // Accuracy peaks at moderate lr and low dropout, plus noise.
            const double sweet = std::exp(-std::pow(std::log10(lr) + 2.6, 2.0)) *
                                 (1.0 - dr * 0.55);
            accuracy.push_back(0.55 + 0.4 * sweet + rng.Range(-0.04, 0.04));
            batch.push_back(b);
            channels.push_back(ch);
            learningRate.push_back(lr);
            dropout.push_back(dr);
        }
        sweep->AddDimension("batch_size", batch);
        sweep->AddDimension("channels", channels);
        sweep->AddDimension("learning_rate", learningRate);
        sweep->AddDimension("dropout", dropout);
        sweep->AddDimension("accuracy", accuracy);
    }
    sweep->SetColorMode(PCPColorMode::ByValue);
    sweep->SetColorDimension("accuracy");
    sweep->SetColormap(HeatmapColormap::Turbo);
    sweep->SetContextStyle(Color(185, 185, 192, 45));
    sweep->AddBrush("accuracy", 0.80, 1.00);        // the interesting runs
    sweep->SetChartTitle("260 training runs");
    DemoKit::AddFixed(sweepCard, sweep);

    // ===== EXAMPLE 3: Curved group bundles (bottom, wide) =====
    auto curveCard = DemoKit::AddCard(page, "PCPCurveCard");
    DemoKit::AddHeading(curveCard, "PCPCurveLabel",
        "Curved lines (Catmull-Rom), two process regimes, per-axis normalisation");

    auto curved = CreateParallelCoordinateChartElement("pcpCurved", 0, 0, 1160, 300);
    {
        DemoRandom rng;
        std::vector<PCPRecord> records;
        for (int i = 0; i < 46; ++i) {
            const bool stable = (i % 2) == 0;
            PCPRecord r;
            const double temperature = stable ? rng.Range(180.0, 210.0) : rng.Range(230.0, 275.0);
            const double pressure = stable ? rng.Range(2.1, 2.9) : rng.Range(1.2, 2.0);
            const double flow = stable ? rng.Range(40.0, 55.0) : rng.Range(52.0, 78.0);
            const double vibration = stable ? rng.Range(0.2, 0.9) : rng.Range(0.8, 2.4);
            const double yield = stable ? rng.Range(93.0, 98.5) : rng.Range(82.0, 92.0);
            r.values = {temperature, pressure, flow, vibration, yield};
            r.group = stable ? "stable" : "aggressive";
            r.label = (stable ? "Batch S" : "Batch A") + std::to_string(i / 2 + 1);
            records.push_back(r);
        }
        curved->AddDimension("temperature", {});
        curved->AddDimension("pressure", {});
        curved->AddDimension("flow", {});
        curved->AddDimension("vibration", {});
        curved->AddDimension("yield", {});
        curved->SetRecords(records);
    }
    curved->SetLineMode(PCPLineMode::Curved);
    curved->SetCurveTension(0.6f);
    curved->SetLineWidth(1.8f);
    curved->SetLineAlpha(0.55f);
    curved->SetGroupColors({{"stable", Color(34, 136, 51, 255)},
                            {"aggressive", Color(238, 119, 51, 255)}});
    curved->SetChartTitle("Process batches");
    // Built 1160 wide only to fill the old page: it takes the card's width.
    DemoKit::AddStretched(curveCard, curved);

    return page;
}

} // namespace UltraCanvas
