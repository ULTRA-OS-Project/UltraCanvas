// include/Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.h
// Pie and donut chart on the chart engine - the port of
// UltraCanvasPieChartElement.
//
// A pie is the Polar projection of one 100%-stacked bar: each slice is a span
// of the domain (u = its share of the total) across the whole value range, so
// a slice is BuildBarOutline's ring sector and a donut is
// SetPolarInnerRadius. The axes are hidden; the outside labels ride the
// engine's solved label plan (the centre text is an obstacle they steer
// around) and the chart strokes their leader lines from the rim. Legend
// toggling (a hidden slice drops out of the shares), hover explosion,
// tooltips and the sweep-in animation are engine services.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// Where slice labels go. Hidden, not None: X11's Xlib.h #defines None.
enum class EnginePieLabelPosition { Hidden, Inside, Outside, Auto };

enum class EnginePieLabelContent {
    Name, Value, Percentage, NameValue, NamePercentage, ValuePercentage, All
};

struct EnginePieSlice {
    std::string name;
    double value = 0.0;
    Color color = Color(0, 0, 0, 0);    // alpha 0 = the theme palette's colour
    float explosion = -1.0f;            // fraction of the radius; < 0 = the global value

    EnginePieSlice() = default;
    EnginePieSlice(const std::string& sliceName, double sliceValue,
                   const Color& sliceColor = Color(0, 0, 0, 0))
        : name(sliceName), value(sliceValue), color(sliceColor) {}
};

class UltraCanvasEnginePieChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEnginePieChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data --------------------------------------------------------------
    void SetSlices(const std::vector<EnginePieSlice>& slices);
    void AddSlice(const std::string& name, double value, const Color& color = Color(0, 0, 0, 0));
    void ClearSlices();
    size_t GetSliceCount() const { return slices.size(); }

    // ---- geometry ----------------------------------------------------------
    void SetDonutMode(bool on);
    bool GetDonutMode() const { return donutMode; }
    void SetInnerRadius(float fraction);           // donut hole, fraction of the radius
    void SetStartAngle(double degrees);            // -90 = 12 o'clock (default)
    void SetClockwise(bool clockwise);
    void SetGlobalExplosion(float fraction);       // every slice pulled out by this
    void SetSliceExplosion(size_t index, float fraction);
    void ClearSliceExplosions();
    void SetHoverExplosion(float fraction);        // the hovered slice; 0 = off

    // ---- appearance --------------------------------------------------------
    // Slice separators; alpha 0 = the theme's plot-area colour.
    void SetBorder(const Color& color, float width);
    void SetRadialGradients(bool on);
    void SetCenterText(const std::string& text, const std::string& caption = std::string());
    // true: slices take the theme palette, ignoring their own colours.
    void SetUseThemePalette(bool use);

    // ---- labels ------------------------------------------------------------
    void SetLabelPosition(EnginePieLabelPosition position);
    void SetLabelContent(EnginePieLabelContent content);
    void SetLeaderLinesEnabled(bool on);
    void SetLeaderLineStyle(const Color& color, float width);
    void SetValueFormatter(std::function<std::string(double)> formatter);
    void SetPercentFormatter(std::function<std::string(double)> formatter);

    // ---- engine contract ---------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    // One visible slice resolved into normalised space.
    struct SliceSpan {
        size_t index = 0;
        double u0 = 0.0, u1 = 0.0;
        double share = 0.0;          // 0..1 of the visible total
    };

    std::vector<EnginePieSlice> slices;
    std::vector<bool> sliceHidden;
    bool donutMode = false;
    float innerFraction = 0.45f;
    double startAngle = -90.0;
    bool clockwiseSweep = true;
    float globalExplosion = 0.0f;
    float hoverExplosion = 0.06f;
    Color borderColor = Color(0, 0, 0, 0);
    float borderWidth = 1.5f;
    bool radialGradients = false;
    std::string centerText;
    std::string centerCaption;
    bool useThemePalette = false;
    EnginePieLabelPosition labelPosition = EnginePieLabelPosition::Auto;
    EnginePieLabelContent labelContent = EnginePieLabelContent::NamePercentage;
    bool leaderLines = true;
    Color leaderColor = Color(150, 150, 150, 255);
    float leaderWidth = 1.0f;
    std::function<std::string(double)> valueFormatter;
    std::function<std::string(double)> percentFormatter;
    // Outside labels decided by the last label collection, per slice.
    std::vector<bool> labelOutside;

    void RefreshLegend();
    void ApplyRadii();
    std::vector<SliceSpan> Spans() const;
    Color SliceColor(size_t index) const;
    float ExplosionOf(size_t index) const;
    float MaxExplosion() const;
    // Normalised radius of the slices' outer edge: below 1 when slices may be
    // pulled out, so an exploded slice still fits the plot.
    double SliceOuterV() const;
    // Screen offset of an exploded slice along its middle angle.
    Point2Dd ExplosionOffset(const ChartEngineFrame& frame, double midU, float explosion) const;
    std::string FormatValue(double value) const;
    std::string FormatPercent(double percent) const;
    std::string LabelText(const SliceSpan& span) const;
    std::string InsideLabelText(const SliceSpan& span) const;
    bool WantsOutside(IRenderContext* ctx, const SliceSpan& span) const;
    Rect2Dd CenterTextBox(IRenderContext* ctx) const;
};

inline std::shared_ptr<UltraCanvasEnginePieChart> CreateEnginePieChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEnginePieChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
