// include/Plugins/Charts/UltraCanvasChartLegend.h
// Shared legend component for chart and diagram elements.
//
// This is a *renderable helper*, not a UIElement: a host element owns one by
// value and draws it inside its own paint pass, so no child-widget
// relationship or layout participation is involved.
//
// Replaces the per-chart private RenderLegend()/DrawLegend() implementations
// (10 of them at the time of writing) and their five mutually incompatible
// position enums (CircularLegendPosition, DumbbellLegendPosition,
// FunnelLegendPosition, LegendPosition, PolarLegendPosition) plus the
// stringly-typed sixth in UltraCanvasPopulationChart.
//
// Usage from a host element's Render():
//
//     Rect2Dd consumed = legend.Measure(ctx, contentArea);
//     Rect2Dd plotArea  = legend.RemainingArea(contentArea, consumed);
//     ... draw the chart into plotArea ...
//     legend.Render(ctx, consumed);
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

    class IRenderContext;

// =============================================================================
// ENUMERATIONS
// =============================================================================

// Placement of the legend relative to the host's content area.
// Outside placements consume space; Inset placements float over the plot.
    enum class ChartLegendPosition {
        TopStart,      TopCenter,      TopEnd,
        BottomStart,   BottomCenter,   BottomEnd,
        LeftStart,     LeftCenter,     LeftEnd,
        RightStart,    RightCenter,    RightEnd,
        InsetTopLeft,  InsetTopRight,
        InsetBottomLeft, InsetBottomRight
    };

    enum class LegendSwatch {
        Square,        // filled rectangle (categorical series, packet layers)
        Circle,        // filled disc (scatter, node roles - "* Routers")
        Line,          // horizontal stroke (line series)
        DashedLine,    // dashed stroke (thresholds, optional fields)
        Marker,        // small diamond
        Gradient       // the entry's own mini colour ramp (band legends)
    };

    enum class LegendOrientation {
        Auto,          // Horizontal for top/bottom, Vertical for left/right
        Horizontal,
        Vertical
    };

// =============================================================================
// ENTRY
// =============================================================================

    struct ChartLegendEntry {
        std::string label;
        Color       color = Color(128, 128, 128, 255);
        LegendSwatch swatch = LegendSwatch::Square;

        // Optional right-aligned secondary text (value, count, percentage).
        std::string valueText;

        // Discrete band entries: when set, the label is generated from the
        // interval rather than supplied verbatim. See SetIntervalFormat().
        bool   isInterval = false;
        double intervalLow = 0.0;
        double intervalHigh = 0.0;
        bool   openHigh = false;        // renders as "> low"

        // Interactive state (G8). Disabled entries render dimmed.
        bool enabled = true;
        bool highlighted = false;

        // Host-supplied identity, echoed back by hit testing.
        std::string id;

        ChartLegendEntry() = default;
        ChartLegendEntry(const std::string& lbl, const Color& c,
                         LegendSwatch sw = LegendSwatch::Square)
                : label(lbl), color(c), swatch(sw) {}
    };

// Interval text rendering for band legends: "[0.00, 0.02]", "0.00 - 0.02",
// "0.00 to 0.02". The open top band always renders as "> low".
    enum class LegendIntervalFormat {
        Brackets,      // [a, b]
        Dash,          // a - b
        ToWord         // a to b
    };

// =============================================================================
// STYLE
// =============================================================================

    struct ChartLegendStyle {
        // Text
        std::string fontFamily = "Arial";
        float       fontSize = 11.0f;
        Color       textColor = Color(40, 40, 40, 255);
        Color       disabledTextColor = Color(160, 160, 160, 255);

        // Title
        float titleFontSize = 12.0f;
        Color titleColor = Color(20, 20, 20, 255);

        // Swatch
        float swatchWidth = 12.0f;
        float swatchHeight = 12.0f;
        float swatchTextGap = 6.0f;
        float swatchBorderWidth = 1.0f;
        Color swatchBorderColor = Color(90, 90, 90, 255);
        bool  drawSwatchBorder = true;

        // Spacing
        float entrySpacingX = 16.0f;   // between entries on one row
        float entrySpacingY = 4.0f;    // between rows
        float paddingX = 8.0f;
        float paddingY = 6.0f;
        float titleGap = 4.0f;
        float hostGap = 8.0f;          // gap between legend box and plot area

        // Box
        bool  drawBackground = false;
        Color backgroundColor = Color(255, 255, 255, 230);
        bool  drawBorder = false;
        Color borderColor = Color(200, 200, 200, 255);
        float borderWidth = 1.0f;
        float cornerRadius = 3.0f;

        static ChartLegendStyle Light();
        static ChartLegendStyle Dark();
        static ChartLegendStyle Monochrome();
    };

// =============================================================================
// LAYOUT RESULT
// =============================================================================

// Where each entry ended up. Produced by Measure(), consumed by Render() and
// HitTest(). Exposed so hosts can do their own hit testing or decoration.
    struct ChartLegendItemRect {
        size_t   entryIndex = 0;
        Rect2Dd  bounds;        // full clickable row (swatch + label)
        Rect2Dd  swatchRect;
    };

    struct ChartLegendLayout {
        Rect2Dd box;                             // total area the legend occupies
        Rect2Dd titleRect;
        std::vector<ChartLegendItemRect> items;
        size_t  visibleCount = 0;                // entries actually laid out
        size_t  overflowCount = 0;               // entries dropped (see maxEntries)
        bool    valid = false;
    };

// =============================================================================
// THE LEGEND
// =============================================================================

    class ChartLegend {
    public:
        ChartLegend() = default;

        // ----- content -----
        void AddEntry(const ChartLegendEntry& entry);
        void SetEntries(const std::vector<ChartLegendEntry>& list);
        void ClearEntries();
        size_t GetEntryCount() const { return entries.size(); }
        const ChartLegendEntry& GetEntry(size_t i) const { return entries[i]; }
        ChartLegendEntry& GetEntry(size_t i) { return entries[i]; }
        const std::vector<ChartLegendEntry>& GetEntries() const { return entries; }

        // Convenience for band legends (contour/hexbin/packet byte bands).
        void AddIntervalEntry(double low, double high, const Color& color);
        void AddOpenIntervalEntry(double low, const Color& color);

        // ----- configuration -----
        void SetTitle(const std::string& t) { title = t; Invalidate(); }
        const std::string& GetTitle() const { return title; }

        void SetPosition(ChartLegendPosition p) { position = p; Invalidate(); }
        ChartLegendPosition GetPosition() const { return position; }

        void SetOrientation(LegendOrientation o) { orientation = o; Invalidate(); }
        LegendOrientation GetOrientation() const { return orientation; }

        void SetVisible(bool v) { visible = v; Invalidate(); }
        bool IsVisible() const { return visible; }

        void SetStyle(const ChartLegendStyle& s) { style = s; Invalidate(); }
        const ChartLegendStyle& GetStyle() const { return style; }
        ChartLegendStyle& GetStyle() { Invalidate(); return style; }

        // Cap the number of rendered entries; the remainder collapse into a
        // final "...and N more" row. 0 = unlimited.
        void SetMaxEntries(size_t n) { maxEntries = n; Invalidate(); }
        size_t GetMaxEntries() const { return maxEntries; }

        void SetIntervalFormat(LegendIntervalFormat f) { intervalFormat = f; Invalidate(); }
        void SetIntervalDecimals(int d) { intervalDecimals = d < 0 ? 0 : d; Invalidate(); }

        // Custom label formatter, applied after interval expansion.
        void SetLabelFormatter(std::function<std::string(const ChartLegendEntry&)> fn) {
            labelFormatter = std::move(fn);
            Invalidate();
        }

        // ----- layout & painting -----

        // Lay the legend out inside availableArea and return the rectangle it
        // occupies. Cheap to call every frame: the result is cached and only
        // recomputed when the content, style or area changes.
        const ChartLegendLayout& Measure(IRenderContext* ctx, const Rect2Dd& availableArea);

        // The area left over for the host's own content, given a measured
        // legend. Inset positions consume nothing and return availableArea.
        Rect2Dd RemainingArea(const Rect2Dd& availableArea) const;

        // Draw. Measure() must have been called for the same area first;
        // Render() calls it itself if the cache is stale.
        void Render(IRenderContext* ctx, const Rect2Dd& availableArea);

        // ----- interaction (G8) -----

        // Index of the entry under the point, or SIZE_MAX. Uses the cached
        // layout, so Measure() must have run.
        size_t HitTest(const Point2Dd& point) const;

        void SetHighlightedEntry(size_t index);   // SIZE_MAX clears
        size_t GetHighlightedEntry() const { return highlightedIndex; }

        void ToggleEntryEnabled(size_t index);

        void Invalidate() { layout.valid = false; }

    private:
        std::vector<ChartLegendEntry> entries;
        std::string title;
        ChartLegendPosition position = ChartLegendPosition::RightCenter;
        LegendOrientation orientation = LegendOrientation::Auto;
        ChartLegendStyle style;
        bool   visible = true;
        size_t maxEntries = 0;
        size_t highlightedIndex = SIZE_MAX;
        LegendIntervalFormat intervalFormat = LegendIntervalFormat::Brackets;
        int    intervalDecimals = 2;
        std::function<std::string(const ChartLegendEntry&)> labelFormatter;

        ChartLegendLayout layout;
        Rect2Dd lastArea;

        std::string EntryText(const ChartLegendEntry& entry) const;
        bool IsVerticalFlow() const;
        bool IsInset() const;
        void DrawSwatch(IRenderContext* ctx, const ChartLegendEntry& entry,
                        const Rect2Dd& rect) const;
    };

// =============================================================================
// FREE HELPERS
// =============================================================================

// Format one interval the way LegendIntervalFormat prescribes. Exposed for
// hosts that build their own label strings.
    std::string FormatLegendInterval(double low, double high, bool openHigh,
                                     LegendIntervalFormat format, int decimals);

} // namespace UltraCanvas
