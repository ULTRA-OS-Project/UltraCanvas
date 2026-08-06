// include/Plugins/Charts/Engine/UltraCanvasChartEngineElement.h
// The chart engine's three-phase render driver.
//
// A chart type derives from this element and implements only phase 2 - its own
// drawing - plus small descriptor methods. Phase 1 (background, grid derived
// from the axis ticks, limiters, edge axes with decluttered tick labels) and
// phase 3 (in-plot axes, the solved label plan, the legend, the interaction
// overlay) are engine-supplied. Phase 2 is clipped to the plot area, so a
// chart cannot paint over the edge axes or the legend by accident; in-plot
// axes render above the content so the chart's marks cannot bury them.
//
// Layout is a measure/solve negotiation instead of hardcoded margins, and the
// label plan is solved when the chart is created or invalidated - never on an
// ordinary redraw (chart engine proposal §5.4-5.9).
//
// This driver is a new subclass rather than a change to
// UltraCanvasChartElementBase, so the existing charts stay untouched (Tier 0
// of the migration plan); native (Tier 2) charts derive from here.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include "Plugins/Charts/Engine/UltraCanvasChartLabels.h"
#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
#include "UltraCanvasElementProperties.h"
#include <memory>

namespace UltraCanvas {

// =============================================================================
// SUPPORT TYPES
// =============================================================================

// The frozen frame every phase renders against. Read-only while drawing.
struct ChartEngineFrame {
    Rect2Dd plotArea;
    const ChartAxisSet* axes = nullptr;
    const IChartProjection* projection = nullptr;
    const ChartLabelPlan* labelPlan = nullptr;
    double animationProgress = 1.0;
    uint64_t generation = 0;
};

enum class ChartLimiterKind { Minimum, Maximum, Average, Median, Target, Threshold, Custom };

// A reference line across the plot at a value on one axis (phase 1, slot 400).
struct ChartLimiter {
    ChartLimiterKind kind = ChartLimiterKind::Custom;
    size_t axisIndex = 0;
    double value = 0.0;
    Color color = Color(200, 60, 60, 255);
    float width = 1.5f;
    bool dashed = true;
    std::string caption;             // solved as a LimiterCaption label
    Color captionColor = Color(120, 40, 40, 255);
};

struct ChartLegendEntry {
    std::string label;
    Color color;
};

// =============================================================================
// ENGINE ELEMENT
// =============================================================================

class UltraCanvasChartEngineElement : public UltraCanvasChartElementBase,
                                      public IConfigurableElement {
public:
    UltraCanvasChartEngineElement(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasChartEngineElement() override;

    // =========================================================================
    // THE CONTENT CONTRACT - what a chart type implements
    // =========================================================================

    // Populate the axis set from the chart's data. Called only when Data or
    // Style is dirty, never per frame.
    virtual void DescribeAxes(ChartAxisSet& axes) = 0;

    // Phase 2: the chart's own drawing, already clipped to frame.plotArea.
    virtual void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) = 0;

    // Optional extra margin (header chips, spill-out markers). Measure pass only.
    virtual void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {}

    // Labels this chart wants placed. Called only when the label plan is
    // rebuilt; text must arrive measured (use ctx->GetTextLineDimensions).
    virtual void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) {}

    // Phase 3 top: hover emphasis, crosshairs, brush bands, drag ghosts.
    virtual void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) {}

    // React to a configured property (key already validated against the bag).
    virtual void OnEnginePropertyChanged(const std::string& key, const UCPropertyValue& value) {}

    // =========================================================================
    // ENGINE CONFIGURATION
    // =========================================================================

    void SetProjectionKind(ChartProjectionKind kind);
    ChartProjectionKind GetProjectionKind() const;

    void SetLabelPolicy(const ChartLabelPolicy& policy);
    void SetLabelOptions(const LabelPlacementOptions& options);
    const ChartLabelPlan& GetLabelPlan() const { return labelPlan; }

    // Grid lines are derived from this axis's ticks (fixes the base class's
    // fixed 10x8 grid that never matched the ticks). SIZE_MAX = no grid axis.
    void SetGridAxis(size_t axisIndex);

    size_t AddLimiter(const ChartLimiter& limiter);
    void ClearLimiters();

    void SetShowLegend(bool show);
    void SetLegendEntries(const std::vector<ChartLegendEntry>& entries);

    // Invalidation. Ordinary redraws reuse the frozen frame and the solved
    // plan; only these rebuild them.
    void MarkEngineDirty(ChartDirty flags);
    void InvalidateLabels() { MarkEngineDirty(ChartDirty::Style); }

    const ChartAxisSet& Axes() const { return engineAxes; }
    ChartAxisSet& Axes() { return engineAxes; }
    const ChartEngineFrame& Frame() const { return frame; }

    // =========================================================================
    // ICONFIGURABLE ELEMENT (named properties across module boundaries)
    // =========================================================================

    bool SetProperty(const std::string& key, const UCPropertyValue& value) override;
    UCPropertyValue GetProperty(const std::string& key) const override;
    std::vector<std::string> ListProperties() const override;

    // =========================================================================
    // RENDER DRIVER (final shape of the three phases)
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    // Legacy base-class pure virtuals, adapted onto the engine path.
    void RenderChart(IRenderContext* ctx) override;
    bool HandleChartMouseMove(const Point2Di& mousePos) override { return false; }

protected:
    // Subclasses register their configurable keys here (with defaults).
    UCPropertyBag& Properties() { return engineProperties; }

    // Phase implementations - overridable for charts that need to extend a
    // phase, but the defaults cover the standard layer stack.
    virtual void RenderPhaseUnder(IRenderContext* ctx);
    virtual void RenderPhaseOver(IRenderContext* ctx);

    virtual void RenderEngineBackground(IRenderContext* ctx);
    virtual void RenderEngineGrid(IRenderContext* ctx);
    virtual void RenderEngineLimiters(IRenderContext* ctx);
    // Edge axes render under the content (slot 500); in-plot axes render in
    // the Over phase so the chart's own marks cannot paint over them.
    virtual void RenderEngineAxes(IRenderContext* ctx);
    virtual void RenderEngineInPlotAxes(IRenderContext* ctx);
    virtual void RenderEngineTitle(IRenderContext* ctx);
    virtual void RenderPlannedLabels(IRenderContext* ctx);
    virtual void RenderEngineLegend(IRenderContext* ctx);

    // Layout/plan lifecycle. Both are cheap no-ops when nothing is dirty.
    void EnsureEngineLayout(IRenderContext* ctx);
    void EnsureLabelPlan(IRenderContext* ctx);

    Rect2Dd LegendRect() const { return legendRect; }

    // Styling shared by the engine layers (theme work will lift these later).
    float axisFontSize = 11.0f;
    float titleFontSize = 16.0f;
    Color axisLineColor = Color(60, 60, 60, 255);
    Color axisTickColor = Color(60, 60, 60, 255);
    Color axisLabelColor = Color(70, 70, 70, 255);
    Color titleTextColor = Color(20, 20, 20, 255);
    Color legendTextColor = Color(60, 60, 60, 255);
    Color legendBackground = Color(255, 255, 255, 220);
    double tickLength = 5.0;
    double tickLabelGap = 4.0;
    int targetTickCount = 6;

private:
    // Engine state
    ChartAxisSet engineAxes;
    std::unique_ptr<IChartProjection> engineProjection;
    ChartProjectionKind projectionKind = ChartProjectionKind::Vertical;
    ChartLabelPolicy labelPolicy = ChartLabelPolicy::Default();
    LabelPlacementOptions labelOptions;
    ChartLabelPlan labelPlan;
    ChartEngineFrame frame;
    ChartDirty pendingDirty = ChartDirty::All;
    uint64_t generation = 0;
    int lastLayoutWidth = -1, lastLayoutHeight = -1;

    size_t gridAxisIndex = static_cast<size_t>(-1);
    std::vector<ChartLimiter> limiters;

    bool showLegend = false;
    std::vector<ChartLegendEntry> legendEntries;
    Rect2Dd legendRect;

    UCPropertyBag engineProperties;

    void RunLayout(IRenderContext* ctx);
    void BuildLabelPlan(IRenderContext* ctx);
    void DeclutterAxisTickLabels(IRenderContext* ctx);
    // Screen segment of an axis rule (in-plot or edge) under the projection.
    void AxisScreenLine(const ChartAxis& axis, Point2Dd& lowEnd, Point2Dd& highEnd) const;
    // One axis's rule, ticks, labels and title - shared by both axis passes.
    void RenderAxisFurniture(IRenderContext* ctx, size_t axisIndex);
};

} // namespace UltraCanvas
