// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineWaterfallChart.h
// Waterfall chart on the chart engine: floating bars that walk a running
// total from step to step, subtotal and total bars that stand on zero, and
// connector lines carrying each bar's end level over to the next bar.
//
// The engine port of UltraCanvasWaterfallChartElement. The engine supplies
// the axes, the grid, the measured layout, the legend (increase / decrease /
// subtotal / total), the solved value-label plan, hover tooltips, themes and
// the grow-in animation; this class adds only the waterfall content. The
// same content renders under the Vertical and Horizontal projections.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// What a step contributes to the running total.
enum class WaterfallStepKind {
    Change,      // floats from the running total by `value`
    Subtotal,    // stands on zero at the running total so far; value ignored
    Total        // stands on zero at the running total plus `value`
};

struct WaterfallStep {
    std::string label;
    double value = 0.0;
    WaterfallStepKind kind = WaterfallStepKind::Change;
    Color color = Color(0, 0, 0, 0);    // alpha 0 = the chart's colour for the kind

    WaterfallStep() = default;
    WaterfallStep(std::string stepLabel, double stepValue,
                  WaterfallStepKind stepKind = WaterfallStepKind::Change)
        : label(std::move(stepLabel)), value(stepValue), kind(stepKind) {}
};

// One step resolved into value space: the bar runs from `start` to `end`.
struct WaterfallResolvedStep {
    double start = 0.0;
    double end = 0.0;
    double runningTotal = 0.0;   // the level after this step
};

// Connector lines between neighbouring bars. Hidden, not None: X11's Xlib.h
// #defines None.
enum class WaterfallConnectorStyle { Hidden, Solid, Dashed, Dotted };

enum class WaterfallBarStyle { Standard, Rounded, Gradient };

class UltraCanvasEngineWaterfallChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineWaterfallChart(const std::string& id, int x = 0, int y = 0,
                                    int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetSteps(const std::vector<WaterfallStep>& steps);
    void AddStep(const std::string& label, double value,
                 WaterfallStepKind kind = WaterfallStepKind::Change);
    void ClearSteps();
    const std::vector<WaterfallStep>& GetSteps() const { return steps; }
    // The steps walked into bar extents (same order as GetSteps).
    std::vector<WaterfallResolvedStep> ResolveSteps() const;
    double GetFinalTotal() const;

    // ---- appearance -----------------------------------------------------
    void SetIncreaseColor(const Color& color);
    void SetDecreaseColor(const Color& color);
    void SetSubtotalColor(const Color& color);
    void SetTotalColor(const Color& color);
    void SetBarStyle(WaterfallBarStyle style);
    WaterfallBarStyle GetBarStyle() const { return barStyle; }
    // Fraction of a category slot a bar fills (0.2 .. 1).
    void SetSlotFill(double fill);
    void SetConnectorStyle(WaterfallConnectorStyle style,
                           const Color& color = Color(117, 117, 117, 255),
                           float width = 1.5f);
    WaterfallConnectorStyle GetConnectorStyle() const { return connectorStyle; }

    // Value labels show each step's change (totals show their level);
    // cumulative labels append the running total ("+150 = 650").
    void SetShowValueLabels(bool show);
    bool GetShowValueLabels() const { return showValues; }
    void SetShowCumulativeLabels(bool show);
    // Number formatting shared by the value axis, the labels and tooltips.
    void SetValueFormat(const std::string& prefix, const std::string& suffix,
                        bool compact = false, int decimals = -1);
    // Draws the zero line as a limiter (useful when the walk goes negative).
    void SetShowZeroLine(bool show);

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;

private:
    std::vector<WaterfallStep> steps;

    Color increaseColor = Color(76, 175, 80, 255);
    Color decreaseColor = Color(244, 67, 54, 255);
    Color subtotalColor = Color(255, 193, 7, 255);
    Color totalColor = Color(33, 150, 243, 255);
    WaterfallBarStyle barStyle = WaterfallBarStyle::Standard;
    double slotFill = 0.7;
    WaterfallConnectorStyle connectorStyle = WaterfallConnectorStyle::Solid;
    Color connectorColor = Color(117, 117, 117, 255);
    float connectorWidth = 1.5f;

    bool showValues = true;
    bool showCumulative = false;
    bool showZeroLine = false;
    std::string valuePrefix;
    std::string valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;

    Color ColorFor(const WaterfallStep& step) const;
    void RefreshLegend();
    void RefreshLimiters();
    // Domain extent of step i's bar.
    void StepSpan(const ChartAxis& category, size_t index, double& u0, double& u1) const;
    std::string FormatValue(double value) const;
    std::string TooltipFor(size_t index, const WaterfallResolvedStep& resolved) const;
    // The value label of step i: its change (or level), plus "= total".
    std::string LabelText(size_t index, const WaterfallResolvedStep& resolved) const;
};

inline std::shared_ptr<UltraCanvasEngineWaterfallChart> CreateEngineWaterfallChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineWaterfallChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
