// include/Plugins/Charts/Engine/UltraCanvasChartTheme.h
// The chart engine's theme and categorical palette - the one home the engine
// proposal reserves for them (§5.2). A ChartTheme bundles the furniture
// colours every engine layer paints with (background, plot area, grid, axes,
// title, legend) with a ChartPalette, the ordered series colours a chart
// cycles through. Built-in themes are harvested from the palettes the
// existing charts and diagrams already carried; continuous value-to-colour
// ramps stay in UltraCanvasColormap, which ChartPalette::FromColormap bridges
// into a categorical palette of any requested size.
//
// UI-free on purpose, like the rest of the engine's model layer, so it can be
// unit tested without a window (Tests/ChartEngineTest.cpp).
//
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// CATEGORICAL PALETTE
// =============================================================================

// An ordered list of series/category colours plus the cycling rule the charts
// all hand-rolled as `palette[index % size]`. Two kinds of list exist among
// the harvested palettes and they want different treatment when a chart knows
// how many elements it will draw:
//   - categorical: hand-designed for contrast in list order (first k are the
//     right k) - Bright, Vibrant, Colorblind, Material, ...
//   - ramp: a light-to-dark run (Ocean, Sunset, Forest, Slate, Monochrome) -
//     for few elements the ends beat the first few entries, so the palette is
//     sampled spread across the run instead.
struct ChartPalette {
    std::string name;
    std::vector<Color> colors;
    // True for light-to-dark runs: ColorAt(index, count) with count < size
    // samples the run evenly instead of taking the first `count` entries.
    bool isRamp = false;

    ChartPalette() = default;
    ChartPalette(std::string paletteName, std::vector<Color> paletteColors,
                 bool ramp = false)
        : name(std::move(paletteName)), colors(std::move(paletteColors)),
          isRamp(ramp) {}

    bool Empty() const { return colors.empty(); }
    size_t Size() const { return colors.size(); }

    // The colour for element `index` with no element count known: the classic
    // cycle, except that wrapped cycles are re-tinted (lightened, then
    // darkened, progressively) so runs longer than the base list stay
    // tellable apart instead of silently repeating.
    Color ColorAt(size_t index) const;

    // The colour for element `index` of `count` total. count <= size picks
    // from the base colours (spread across the run for a ramp, first-k
    // otherwise); count > size falls back to the tinted cycling of
    // ColorAt(index).
    Color ColorAt(size_t index, size_t count) const;

    // The full colour list for a chart with `count` elements - ColorAt(i,
    // count) for i in [0, count).
    std::vector<Color> ColorsFor(size_t count) const;

    // A palette of `count` colours sampled evenly from a continuous colormap
    // (Viridis, Spectral, ...). The right tool once an element count exceeds
    // what any hand-designed categorical list can keep distinguishable.
    static ChartPalette FromColormap(HeatmapColormap map, size_t count,
                                     bool reverse = false);
};

// =============================================================================
// THEME
// =============================================================================

// Everything the engine's own layers paint with, plus the palette the chart
// content draws its series from. Defaults are the Light theme's values.
struct ChartTheme {
    std::string name = "Light";

    // Furniture (engine phases 1 and 3)
    Color backgroundColor = Color(255, 255, 255, 255);
    Color plotAreaColor = Color(250, 250, 250, 255);
    Color gridColor = Color(220, 220, 220, 255);
    Color axisLineColor = Color(60, 60, 60, 255);
    Color axisTickColor = Color(60, 60, 60, 255);
    Color axisLabelColor = Color(70, 70, 70, 255);
    Color titleTextColor = Color(20, 20, 20, 255);
    Color legendTextColor = Color(60, 60, 60, 255);
    Color legendBackground = Color(255, 255, 255, 220);

    // Series colours (chart content, legend swatches)
    ChartPalette palette;
};

// =============================================================================
// BUILT-IN THEMES
// =============================================================================
// Fourteen named themes, their palettes lifted from the definitions the
// existing charts each carried privately (pie, sunburst/chord, bubble,
// timeline, git graph, nested area, cumulative flow, parallel coordinates).
// Get() resolves a name case-insensitively and falls back to Light, so a
// theme name can travel through the named-property surface as a plain string.

namespace ChartThemes {

    const ChartTheme& Light();        // default furniture, Paul Tol bright 8
    const ChartTheme& Dark();         // dark furniture, GitHub-dark lane 8
    const ChartTheme& Corporate();    // corporate blue 8
    const ChartTheme& Vibrant();      // flat-UI 8
    const ChartTheme& Pastel();       // soft 8
    const ChartTheme& Colorblind();   // Okabe-Ito 8
    const ChartTheme& Material();     // Google Material 10
    const ChartTheme& Classic();      // Chart.js 10
    const ChartTheme& Tableau();      // Tableau 10
    const ChartTheme& Ocean();        // blue-green ramp 8
    const ChartTheme& Sunset();       // plum-to-sand ramp 8
    const ChartTheme& Forest();       // green ramp 8
    const ChartTheme& Slate();        // blue-grey ramp 8
    const ChartTheme& Monochrome();   // grey ramp 6, print-safe furniture

    // nullptr when the name matches no built-in (case-insensitive).
    const ChartTheme* Find(const std::string& name);
    // Find() with a Light fallback - never fails.
    const ChartTheme& Get(const std::string& name);
    // Registry order: the order the accessors above are listed in.
    std::vector<std::string> Names();

} // namespace ChartThemes

} // namespace UltraCanvas
