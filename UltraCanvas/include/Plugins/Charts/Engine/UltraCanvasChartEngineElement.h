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
// Version: 1.2.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include "Plugins/Charts/Engine/UltraCanvasChartLabels.h"
#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
#include "Plugins/Charts/Engine/UltraCanvasChartTheme.h"
#include "UltraCanvasElementProperties.h"
#include "UltraCanvasTimer.h"
#include <cstdint>
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
    const ChartTheme* theme = nullptr;
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

// The legend is the shared ChartLegend component
// (Plugins/Charts/UltraCanvasChartLegend.h): ChartLegendEntry, LegendSwatch
// (Square, Circle, Ring, Line, DashedLine, Marker, Glyph, Gradient, Outline,
// Hatched, Image), ChartLegendPosition (12 outside placements + 4 insets) and
// LegendOrientation all come from there.

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

    // Theme and palette (Engine/UltraCanvasChartTheme.h). A theme change is
    // repaint-only - it never re-runs layout or the label solver. Content
    // charts draw their series from Palette() (typically
    // Palette().ColorAt(i, count)) and refresh anything cached from the
    // theme - legend entry colours above all - in OnThemeChanged().
    void SetTheme(const ChartTheme& theme);
    // Resolves a built-in theme by name ("Dark", "Ocean", ...); returns false
    // and changes nothing when the name is unknown. Also reachable through
    // the named-property surface as SetProperty("theme", name).
    bool SetTheme(const std::string& themeName);
    const ChartTheme& GetTheme() const { return engineTheme; }
    // Replace only the series palette, keeping the theme's furniture.
    void SetPalette(const ChartPalette& palette);
    const ChartPalette& Palette() const { return engineTheme.palette; }
    virtual void OnThemeChanged() {}

    void SetLabelPolicy(const ChartLabelPolicy& policy);
    void SetLabelOptions(const LabelPlacementOptions& options);
    const ChartLabelPlan& GetLabelPlan() const { return labelPlan; }

    // Grid lines are derived from this axis's ticks (fixes the base class's
    // fixed 10x8 grid that never matched the ticks). SIZE_MAX = no grid axis.
    void SetGridAxis(size_t axisIndex);

    size_t AddLimiter(const ChartLimiter& limiter);
    void ClearLimiters();

    // Legend - the shared ChartLegend component drawn and laid out by the
    // engine: an outside placement reserves its edge in the layout
    // negotiation, an inset placement floats over the plot and reserves
    // nothing, and either way the legend box is an obstacle the solved
    // labels steer around.
    void SetShowLegend(bool show);
    void SetLegendEntries(const std::vector<ChartLegendEntry>& entries);
    void SetLegendPosition(ChartLegendPosition position);
    void SetLegendOrientation(LegendOrientation orientation);
    void SetLegendTitle(const std::string& title);
    // The component itself, for the long tail (style, value text, interval
    // entries, max-entries overflow, label formatter). Mutations that change
    // the legend's size need MarkEngineDirty(ChartDirty::Geometry) after.
    ChartLegend& Legend() { return engineLegend; }
    const ChartLegend& GetLegend() const { return engineLegend; }

    // =========================================================================
    // ANIMATION DRIVER
    // =========================================================================
    // Drives frame.animationProgress from 0 to 1 (ease-out cubic) and keeps
    // repainting until it arrives - no chart-side timers. Content that wants
    // an entrance animation scales its geometry by frame.animationProgress.
    // The frames come from a ~60fps periodic application timer: RequestRedraw()
    // alone cannot advance an animation, because the event loop blocks until a
    // native event or a timer wakes it, so a chart left to redraw itself would
    // freeze at whatever progress its last paint happened to see.

    void StartEngineAnimation(float durationSeconds = 0.8f);
    // Restart the animation automatically whenever ChartDirty::Data is marked.
    void SetAnimateOnDataChange(bool enable, float durationSeconds = 0.8f);
    bool IsEngineAnimating() const { return engineAnimating; }

    // =========================================================================
    // HIT REGIONS AND TOOLTIPS
    // =========================================================================
    // Content registers its interactive geometry while rendering phase 2 (the
    // engine clears the list first); the engine then owns hover tracking:
    // MouseMove hit-tests the regions (last added wins - draw order), hover
    // changes repaint via ChartDirty::Hover without re-running layout or the
    // label solver, and a region's tooltip is shown through the standard
    // tooltip manager when tooltips are enabled.

    void AddHitRegion(const Rect2Dd& bounds, int64_t regionId,
                      const std::string& tooltip = std::string());
    // Non-rectangular content (polar sectors, wedges): point-in-polygon test.
    void AddHitRegion(const std::vector<Point2Dd>& polygon, int64_t regionId,
                      const std::string& tooltip = std::string());
    void ClearHitRegions();
    // The hovered region's id, -1 when none. Ids are chart-defined (encode a
    // series/category pair, a data index, ...).
    int64_t HoveredRegionId() const { return hoveredRegionId; }
    virtual void OnHitRegionHoverChanged(int64_t regionId) {}

    bool OnEvent(const UCEvent& event) override;

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

    // The measured legend box from the last layout (zero-sized when hidden).
    Rect2Dd LegendRect() const { return legendBox; }

    // Styling shared by the engine layers. The colours are working copies
    // filled from the active theme by SetTheme(); a chart may still override
    // one after setting a theme.
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
    ChartTheme engineTheme;
    ChartAxisSet engineAxes;
    std::unique_ptr<IChartProjection> engineProjection;
    ChartProjectionKind projectionKind = ChartProjectionKind::Vertical;
    ChartLabelPolicy labelPolicy = ChartLabelPolicy::Default();
    LabelPlacementOptions labelOptions;
    ChartLabelPlan labelPlan;
    ChartEngineFrame frame;
    // What the content itself reserved in MeasureContent - the bands its
    // solved labels may spill into (see BuildLabelPlan).
    ChartMargins contentMargins;
    ChartDirty pendingDirty = ChartDirty::All;
    uint64_t generation = 0;
    int lastLayoutWidth = -1, lastLayoutHeight = -1;

    size_t gridAxisIndex = static_cast<size_t>(-1);
    std::vector<ChartLimiter> limiters;

    // The shared legend component; the engine owns its layout: measured
    // against legendArea (the element minus the title band) during the
    // layout negotiation, its box cached for the label plan's obstacle.
    ChartLegend engineLegend;
    Rect2Dd legendArea;
    Rect2Dd legendBox;

    // Animation driver state
    bool engineAnimating = false;
    bool animateOnDataChange = false;
    float animateOnDataDuration = 0.8f;
    TimerId engineAnimationTimer = InvalidTimerId;   // ~60fps frame driver
    void StopEngineAnimationTimer();

    // Hit regions (element-local space, rebuilt by content every render)
    struct ChartHitRegion {
        Rect2Dd bounds;
        std::vector<Point2Dd> polygon;   // empty = rectangular region
        int64_t id = -1;
        std::string tooltip;
    };
    std::vector<ChartHitRegion> hitRegions;
    int64_t hoveredRegionId = -1;
    const ChartHitRegion* HitTestRegions(const Point2Dd& point) const;

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
