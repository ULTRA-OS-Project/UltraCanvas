// include/Plugins/Charts/UltraCanvasColormap.h
// Colour-map utilities for heatmap-style charts. UI-free and reusable: maps a
// normalized position in [0, 1] to a Color via a set of built-in palettes
// (sequential, diverging, single-hue) or a user-supplied gradient, plus helpers
// for discrete (quantized) levels and diverging normalization.
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <vector>

namespace UltraCanvas {

// Built-in colour maps. Custom uses a caller-supplied anchor list.
    enum class HeatmapColormap {
        // Perceptually-uniform sequential
        Grayscale,
        Viridis,
        Inferno,    // good default for audio spectrograms
        Magma,
        Plasma,
        Cividis,
        Turbo,
        // Classic / misc sequential
        Jet,
        Hot,
        Cool,
        // Single-hue sequential
        Blues,
        Greens,
        Reds,
        Oranges,
        // Diverging (pair with SetDiverging + a midpoint)
        Spectral,
        RdBu,
        RdYlBu,
        RdYlGn,
        Coolwarm,
        // User-supplied
        Custom
    };

// Evenly-spaced colour anchors for a built-in map (>= 2 entries).
    std::vector<Color> ColormapAnchors(HeatmapColormap c);

// Linear interpolation across an evenly-spaced anchor list. t in [0, 1].
    Color InterpolateColormap(const std::vector<Color>& anchors, double t);

// Sample a colour map at t in [0, 1]. Uses `custom` when c == Custom and the
// list has >= 2 entries. `reverse` flips the map direction.
    Color SampleColormap(HeatmapColormap c, const std::vector<Color>& custom,
                         double t, bool reverse);

// True for the diverging palettes (centre is the neutral colour).
    bool IsDivergingColormap(HeatmapColormap c);

// Snap a normalized value to the centre of one of `levels` equal bands.
// levels < 2 returns t unchanged (continuous).
    double QuantizeNorm(double t, int levels);

// Diverging normalization: maps [lo, mid] -> [0, 0.5] and [mid, hi] -> [0.5, 1],
// so `mid` lands on the colour map's centre. Result clamped to [0, 1].
    double DivergingNorm(double v, double lo, double mid, double hi);

} // namespace UltraCanvas
