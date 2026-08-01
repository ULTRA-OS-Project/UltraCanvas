// include/Plugins/Charts/Engine/UltraCanvasChartTypes.h
// Shared vocabulary for the chart engine: render phases and their ordering
// slots, the invalidation model, and the value formatter every chart used to
// declare for itself.
//
// Deliberately free of UI dependencies so the engine's model layer can be unit
// tested without a window, following the UltraCanvasContourGrid precedent.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

namespace UltraCanvas {

// =============================================================================
// VALUE FORMATTING
// =============================================================================

// One home for the formatter signature that ScatterPlot3D, ContourSurface3D and
// ContourSurfaceGL each used to declare privately.
using ValueFormatter = std::function<std::string(double)>;

// =============================================================================
// RENDER PHASES AND SLOTS
// =============================================================================

// Charts render in three phases. A chart type implements the middle one and
// inserts itself there; everything above and below is engine-supplied.
enum class ChartRenderPhase {
    Under,      // background, highlights, grid, limiters, axes, distributions
    Content,    // the specific chart - the one method a chart type must provide
    Over        // analysis, labels, legend, annotations, interaction
};

// Ordering inside a phase. Values are spaced so custom layers can be slotted
// between the standard ones without renumbering.
enum class ChartLayerSlot : int {
    Background   = 100,   // element / chart-area / plot-area fills, gradients, images
    Highlight    = 200,   // group rectangles, ellipses, hull blobs, value bands
    Grid         = 300,   // gridlines derived from the axis ticks, alternating bands
    Limiter      = 400,   // min / max / average / target / threshold lines
    Axes         = 500,   // axis lines, ticks, tick labels, axis titles
    Distribution = 550,   // marginal histograms / density / box summaries
    Content      = 600,   // the chart itself
    Analysis     = 700,   // trend, moving average, correlation, statistic annotation
    Annotation   = 800,   // solved labels, legend, colour bar, callouts, leaders
    Interaction  = 900    // hover, crosshair, brush bands, rubber band
};

inline ChartRenderPhase PhaseOfSlot(ChartLayerSlot slot) {
    const int s = static_cast<int>(slot);
    if (s < static_cast<int>(ChartLayerSlot::Content)) return ChartRenderPhase::Under;
    if (s == static_cast<int>(ChartLayerSlot::Content)) return ChartRenderPhase::Content;
    return ChartRenderPhase::Over;
}

// =============================================================================
// INVALIDATION
// =============================================================================

// What changed since the last frame. The engine maps these onto the work that
// actually has to be redone: most notably, only Data / Geometry / Style rebuild
// the label plan, so hovering, selecting and animating never run the solver.
enum class ChartDirty : uint32_t {
    None      = 0,
    Data      = 1u << 0,   // values added, replaced or cleared
    Geometry  = 1u << 1,   // element resized, plot area re-solved, axis range changed
    Style     = 1u << 2,   // fonts, label text, formatters, legend contents
    Selection = 1u << 3,   // selection / pinning / brushing
    Hover     = 1u << 4,   // pointer moved
    Animation = 1u << 5,   // animation frame advanced
    All       = 0x3Fu
};

inline ChartDirty operator|(ChartDirty a, ChartDirty b) {
    return static_cast<ChartDirty>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ChartDirty& operator|=(ChartDirty& a, ChartDirty b) { a = a | b; return a; }
inline bool HasDirty(ChartDirty set, ChartDirty flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0u;
}

// The label plan is rebuilt only for changes that can move a label. Hover,
// selection and animation deliberately do not qualify.
inline bool DirtyRebuildsLabels(ChartDirty set) {
    return HasDirty(set, ChartDirty::Data) ||
           HasDirty(set, ChartDirty::Geometry) ||
           HasDirty(set, ChartDirty::Style);
}

inline bool DirtyRebuildsLayout(ChartDirty set) {
    return HasDirty(set, ChartDirty::Data) ||
           HasDirty(set, ChartDirty::Geometry) ||
           HasDirty(set, ChartDirty::Style);
}

// =============================================================================
// LAYOUT NEGOTIATION
// =============================================================================

enum class ChartAxisEdge { Left, Right, Top, Bottom };

struct ChartMargins {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;

    void Merge(const ChartMargins& other) {
        left   = std::max(left,   other.left);
        right  = std::max(right,  other.right);
        top    = std::max(top,    other.top);
        bottom = std::max(bottom, other.bottom);
    }
};

// Filled during the measure pass: axes, legend, colour bar, marginal
// histograms and the chart itself each declare the space they need, and the
// engine subtracts the total from the chart area to get the plot area. This is
// what replaces every chart's hardcoded margin guesses.
struct ChartLayoutRequest {
    ChartMargins margins;

    void Reserve(ChartAxisEdge edge, double amount) {
        if (amount <= 0.0) return;
        switch (edge) {
            case ChartAxisEdge::Left:   margins.left   = std::max(margins.left,   amount); break;
            case ChartAxisEdge::Right:  margins.right  = std::max(margins.right,  amount); break;
            case ChartAxisEdge::Top:    margins.top    = std::max(margins.top,    amount); break;
            case ChartAxisEdge::Bottom: margins.bottom = std::max(margins.bottom, amount); break;
        }
    }
};

// Applies the reserved margins to a chart area, never producing a negative or
// zero-sized plot area however greedy the reservations were.
Rect2Dd SolvePlotArea(const Rect2Dd& chartArea, const ChartMargins& margins,
                      double minWidth = 8.0, double minHeight = 8.0);

} // namespace UltraCanvas
