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
// Version: 1.4.1 - same-side edge axes with overlapping bands stack outward
// Version: 1.4.0 - polar furniture (rim labels, radial value rule, ring and
//   polygon grids), polar angles / inner radius, domain gridlines, axis
//   sub-bands on the edge rule, DrawChartMarker, domain-axis limiters with
//   a partial extent, span annotations
// Version: 1.3.1 - default bodies mark unused parameters (void): no -Wunused-parameter
// Version: 1.3.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include "Plugins/Charts/Engine/UltraCanvasChartHighlights.h"
#include "Plugins/Charts/Engine/UltraCanvasChartLabels.h"
#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
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
    // How far the line runs across the plot, as a fraction of the direction
    // perpendicular to its axis: a value-axis limiter spans the domain, a
    // limiter on a domain axis (an average age on a population pyramid's age
    // rows) spans the value range. 0..1 = the whole plot; 0..0.5 stops at the
    // middle - one side of a diverging chart. The caption sits at the end.
    double extentStart = 0.0;
    double extentEnd = 1.0;
};

// A dimension line between two value-space points, with an arrowhead at
// each end and a boxed caption at its middle (phase 3, slot 700): the lead
// time / cycle time / WIP spans of a cumulative flow diagram, a gap between
// two series, a measured interval. x maps through the axis at xAxisIndex (the
// projection's domain / u), y through yAxisIndex. The caption box is an
// obstacle in the label plan, so solved value labels steer around it.
struct ChartSpanAnnotation {
    size_t xAxisIndex = 0;
    size_t yAxisIndex = 1;
    Point2Dd from;                   // value space
    Point2Dd to;
    std::string label;
    Color color = Color(120, 122, 128, 255);
    float width = 2.4f;
    double arrowSize = 7.0;
    Color boxColor = Color(235, 235, 238, 235);
    Color textColor = Color(70, 72, 78, 255);
};

// How RenderBarConnections strokes the lines BuildBarConnections produced
// (Engine/UltraCanvasChartSeries.h). Each line takes its series' colour.
struct ChartBarConnectionStyle {
    float width = 2.0f;
    // Widened stroke for the series whose bar is hovered; 0 = no emphasis.
    float emphasisWidth = 3.5f;
    bool dashed = false;
    // Knot dots at every joined bar; 0 = none.
    float markerRadius = 0.0f;
    // A contrasting underlay so a line stays readable where it crosses a bar
    // of its own colour; 0 = none. Its colour defaults to the theme's plot
    // area when left fully transparent.
    float haloWidth = 1.5f;
    Color haloColor = Color(0, 0, 0, 0);
};

// The rings a polar value grid draws: true circles, or polygons through the
// domain ticks - the radar "web".
enum class ChartPolarGridShape { Circle, Polygon };

// Paints one point marker (Engine/UltraCanvasChartSeries.h ChartMarkerShape)
// centred on `center`. Filled shapes take `fill` and an optional outline
// (`stroke` with alpha 0 = none); Cross and Plus are strokes in the fill
// colour. Shared by the scatter, bubble and line charts so a marker looks the
// same everywhere it appears.
void DrawChartMarker(IRenderContext* ctx, ChartMarkerShape shape, const Point2Dd& center,
                     double radius, const Color& fill,
                     const Color& stroke = Color(0, 0, 0, 0), float strokeWidth = 1.0f);

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
    virtual void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) { (void)ctx; (void)request; }

    // Labels this chart wants placed. Called only when the label plan is
    // rebuilt; text must arrive measured (use ctx->GetTextLineDimensions).
    virtual void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) { (void)ctx; (void)broker; }

    // Phase 3 top: hover emphasis, crosshairs, brush bands, drag ghosts.
    virtual void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) { (void)ctx; (void)frame; }

    // React to a configured property (key already validated against the bag).
    virtual void OnEnginePropertyChanged(const std::string& key, const UCPropertyValue& value) { (void)key; (void)value; }

    // =========================================================================
    // ENGINE CONFIGURATION
    // =========================================================================

    void SetProjectionKind(ChartProjectionKind kind);
    ChartProjectionKind GetProjectionKind() const;

    // Polar projection geometry, kept across projection switches: where the
    // domain starts (degrees, 0 = 3 o'clock, -90 = 12 o'clock), how far it
    // sweeps (360 = full circle; 270 for a radial bar chart) and which way,
    // and the fraction of the radius left empty in the middle (donuts, radial
    // bars). Geometry-only - no re-describe of the axes.
    void SetPolarAngles(double startAngleDegrees, double sweepDegrees, bool clockwise = true);
    void SetPolarInnerRadius(double fraction);
    // Value-grid rings under Polar: circles (default) or polygons through
    // the domain ticks (the radar web).
    void SetPolarGridShape(ChartPolarGridShape shape);
    ChartPolarGridShape GetPolarGridShape() const { return polarGridShape; }

    // Whether an axis maps the domain (u) or the value (v) under the current
    // projection - ChartAxis::role, or inferred from its side when Auto.
    bool IsDomainAxis(const ChartAxis& axis) const;

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
    // A value axis draws lines across the domain (rings under Polar); a
    // domain axis draws lines across the value range (spokes under Polar).
    void SetGridAxis(size_t axisIndex);
    // A second grid source, so a chart can rule both directions - a scatter
    // plot's x and y, a radar's rings and spokes. SIZE_MAX = none (default).
    void SetDomainGridAxis(size_t axisIndex);

    size_t AddLimiter(const ChartLimiter& limiter);
    void ClearLimiters();

    // Highlights (Engine/UltraCanvasChartHighlights.h): group washes drawn
    // under the grid (zSlot 200) or overlaid above the content (zSlot 700),
    // clipped to the plot. Computed shapes (ConfidenceEllipse, Hull, Blob,
    // PointHalo) take value-space member points mapped through the axes at
    // xAxisIndex/yAxisIndex; a highlight's label rides the label plan as
    // ChartLabelClass::HighlightLabel.
    size_t AddHighlight(const ChartHighlight& highlight);
    void ClearHighlights();

    // Span annotations (ChartSpanAnnotation above) - drawn over the content.
    size_t AddSpanAnnotation(const ChartSpanAnnotation& span);
    void ClearSpanAnnotations();

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
    // Discrete entries (default), a continuous ColorBar, or a bubble
    // SizeLegend - configure the latter two through
    // Legend().SetColorBar(...) / Legend().SetSizeScale(...).
    void SetLegendMode(ChartLegendMode mode);
    // The component itself, for the long tail (style, value text, interval
    // entries, max-entries overflow, label formatter). Mutations that change
    // the legend's size need MarkEngineDirty(ChartDirty::Geometry) after.
    ChartLegend& Legend() { return engineLegend; }
    const ChartLegend& GetLegend() const { return engineLegend; }
    // The engine owns legend interaction: hovering an entry highlights it
    // (repaint-only), clicking one toggles its enabled state (the entry
    // renders dimmed) and notifies the chart, which decides what "disabled"
    // means - typically hide the series and MarkEngineDirty(ChartDirty::Data).
    virtual void OnLegendEntryToggled(size_t entryIndex, bool enabled) { (void)entryIndex; (void)enabled; }

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
    virtual void OnHitRegionHoverChanged(int64_t regionId) { (void)regionId; }

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
    bool HandleChartMouseMove(const Point2Di& mousePos) override { (void)mousePos; return false; }

protected:
    // Subclasses register their configurable keys here (with defaults).
    UCPropertyBag& Properties() { return engineProperties; }

    // Phase implementations - overridable for charts that need to extend a
    // phase, but the defaults cover the standard layer stack.
    virtual void RenderPhaseUnder(IRenderContext* ctx);
    virtual void RenderPhaseOver(IRenderContext* ctx);

    virtual void RenderEngineBackground(IRenderContext* ctx);
    virtual void RenderEngineHighlights(IRenderContext* ctx, int zSlot);
    virtual void RenderEngineGrid(IRenderContext* ctx);
    virtual void RenderEngineLimiters(IRenderContext* ctx);
    // Edge axes render under the content (slot 500); in-plot axes render in
    // the Over phase so the chart's own marks cannot paint over them.
    virtual void RenderEngineAxes(IRenderContext* ctx);
    virtual void RenderEngineInPlotAxes(IRenderContext* ctx);
    virtual void RenderEngineTitle(IRenderContext* ctx);
    virtual void RenderPlannedLabels(IRenderContext* ctx);
    virtual void RenderEngineLegend(IRenderContext* ctx);

    // Bar connections: strokes lines built by BuildBarConnections from inside
    // RenderChartContent, after the bars, so they ride over the bars and under
    // the solved labels. seriesColors[i] colours series i; the series listed
    // in emphasizedSeries (SIZE_MAX = none) is drawn at emphasisWidth.
    void RenderBarConnections(IRenderContext* ctx,
                              const std::vector<ChartBarConnection>& connections,
                              const std::vector<Color>& seriesColors,
                              const ChartBarConnectionStyle& style = {},
                              size_t emphasizedSeries = static_cast<size_t>(-1));

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

    // Per axis: how far outside the plot edge its rule sits - non-zero for
    // an edge axis stacked beyond another on the same side (RunLayout).
    std::vector<double> axisEdgeOffsets;
    size_t gridAxisIndex = static_cast<size_t>(-1);
    size_t domainGridAxisIndex = static_cast<size_t>(-1);

    // Polar geometry, applied to the projection whenever it is polar.
    double polarStartAngle = -90.0;
    double polarSweep = 360.0;
    bool polarClockwise = true;
    double polarInnerFraction = 0.0;
    ChartPolarGridShape polarGridShape = ChartPolarGridShape::Circle;
    void ApplyPolarGeometry();
    bool PolarFullCircle() const;
    // A ring at normalised value v: a sampled circle, or the polygon through
    // the domain ticks when the grid shape is Polygon.
    std::vector<Point2Dd> PolarRing(double v) const;
    // The domain axis the polygon grid and the rim labels follow.
    const ChartAxis* PolarDomainAxis() const;
    void RenderGridFrom(IRenderContext* ctx, size_t axisIndex);
    // A domain edge axis under Polar: the rim, its ticks and labels.
    void RenderPolarRimAxis(IRenderContext* ctx, size_t axisIndex);
    // Where a rim label goes for normalised domain position u.
    Rect2Dd PolarRimLabelBox(double u, const Size2Di& size) const;
    // A domain axis's ticks on the rim, deduplicated (0 and 360 degrees are
    // one spoke) and decluttered: shared by the rim pass and the label plan,
    // which treats the kept labels as obstacles.
    struct PolarRimTick {
        double u = 0.0;
        std::string label;
        Rect2Dd box;
        bool labelled = false;
    };
    std::vector<PolarRimTick> PolarRimTicks(IRenderContext* ctx, size_t axisIndex) const;
    std::vector<ChartLimiter> limiters;
    std::vector<ChartHighlight> highlights;
    std::vector<ChartSpanAnnotation> spanAnnotations;
    void RenderEngineSpanAnnotations(IRenderContext* ctx);
    // A span's screen end points, false when an axis is missing.
    bool MapSpan(const ChartSpanAnnotation& span, Point2Dd& a, Point2Dd& b) const;
    Rect2Dd SpanCaptionBox(IRenderContext* ctx, const ChartSpanAnnotation& span,
                           const Point2Dd& a, const Point2Dd& b) const;
    // A highlight's value-space point on the projection's screen, or invalid
    // (NaN) when its axes are out of range.
    Point2Dd MapHighlightPoint(const ChartHighlight& highlight,
                               const Point2Dd& value) const;

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
