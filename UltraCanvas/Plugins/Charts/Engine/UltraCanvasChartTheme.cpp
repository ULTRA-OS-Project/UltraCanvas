// Plugins/Charts/Engine/UltraCanvasChartTheme.cpp
// The chart engine's theme and categorical palette. The built-in palettes are
// the ones the existing charts each defined privately, lifted here verbatim:
// Bright is the parallel coordinate chart's Paul Tol list, Dark pairs the
// cumulative flow chart's dark furniture with the git graph's dark lane
// colours, Corporate/Vibrant/Pastel/Ocean/Sunset/Forest/Slate/Monochrome are
// the timeline family's, Colorblind is the nested area chart's Okabe-Ito
// list, and Material/Classic/Tableau are the sunburst, pie and bubble charts'
// ten-colour lists.
//
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartTheme.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// PALETTE
// =============================================================================

Color ChartPalette::ColorAt(size_t index) const {
    if (colors.empty()) return Color(128, 128, 128, 255);
    const Color base = colors[index % colors.size()];
    const size_t cycle = index / colors.size();
    if (cycle == 0) return base;
    // Wrapped cycles re-tint progressively - lighter, darker, lighter still -
    // so element 9 of an 8-colour palette is not a repeat of element 1.
    const float factor = std::min(0.6f, 0.25f + 0.1f * ((cycle - 1) / 2));
    return (cycle % 2 == 1) ? base.Lighten(factor) : base.Darken(factor);
}

Color ChartPalette::ColorAt(size_t index, size_t count) const {
    if (colors.empty()) return Color(128, 128, 128, 255);
    if (count == 0 || count > colors.size() || index >= count) {
        return ColorAt(index);
    }
    if (!isRamp || count == colors.size()) return colors[index];
    // A ramp sampled for fewer elements than it holds: spread across the run,
    // ends included, so three elements get dark / mid / light instead of the
    // three nearly-equal darkest entries.
    if (count == 1) return colors[colors.size() / 2];
    const double t = static_cast<double>(index) / static_cast<double>(count - 1);
    const size_t sampled = static_cast<size_t>(
        std::lround(t * static_cast<double>(colors.size() - 1)));
    return colors[sampled];
}

std::vector<Color> ChartPalette::ColorsFor(size_t count) const {
    std::vector<Color> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) result.push_back(ColorAt(i, count));
    return result;
}

ChartPalette ChartPalette::FromColormap(HeatmapColormap map, size_t count,
                                        bool reverse) {
    ChartPalette palette;
    palette.name = "Colormap";
    palette.isRamp = !IsDivergingColormap(map);
    if (count == 0) return palette;
    palette.colors.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const double t = (count == 1)
                             ? 0.5
                             : static_cast<double>(i) /
                                   static_cast<double>(count - 1);
        palette.colors.push_back(SampleColormap(map, {}, t, reverse));
    }
    return palette;
}

// =============================================================================
// BUILT-IN THEMES
// =============================================================================

namespace ChartThemes {

namespace {

ChartTheme MakeLightTheme(const std::string& name, ChartPalette palette) {
    ChartTheme theme;                     // defaults are the Light furniture
    theme.name = name;
    theme.palette = std::move(palette);
    return theme;
}

} // namespace

const ChartTheme& Light() {
    static const ChartTheme theme = MakeLightTheme(
        "Light",
        ChartPalette("Bright",
                     {Color(68, 119, 170, 255), Color(238, 102, 119, 255),
                      Color(34, 136, 51, 255), Color(204, 187, 68, 255),
                      Color(102, 204, 238, 255), Color(170, 51, 119, 255),
                      Color(187, 187, 187, 255), Color(153, 68, 85, 255)}));
    return theme;
}

const ChartTheme& Dark() {
    static const ChartTheme theme = [] {
        ChartTheme t;
        t.name = "Dark";
        t.backgroundColor = Color(30, 32, 38, 255);
        t.plotAreaColor = Color(36, 39, 46, 255);
        t.gridColor = Color(52, 56, 64, 255);
        t.axisLineColor = Color(95, 100, 110, 255);
        t.axisTickColor = Color(95, 100, 110, 255);
        t.axisLabelColor = Color(165, 170, 180, 255);
        t.titleTextColor = Color(245, 245, 245, 255);
        t.legendTextColor = Color(225, 225, 225, 255);
        t.legendBackground = Color(42, 45, 53, 230);
        t.palette = ChartPalette(
            "DarkLanes",
            {Color(88, 166, 255, 255), Color(63, 185, 80, 255),
             Color(219, 97, 162, 255), Color(219, 149, 62, 255),
             Color(163, 113, 247, 255), Color(57, 197, 207, 255),
             Color(248, 81, 73, 255), Color(191, 143, 108, 255)});
        return t;
    }();
    return theme;
}

const ChartTheme& Corporate() {
    static const ChartTheme theme = MakeLightTheme(
        "Corporate",
        ChartPalette("CorporateBlue",
                     {Color(31, 97, 176, 255), Color(41, 128, 185, 255),
                      Color(52, 152, 219, 255), Color(93, 173, 226, 255),
                      Color(26, 188, 156, 255), Color(22, 160, 133, 255),
                      Color(72, 100, 132, 255), Color(120, 144, 168, 255)}));
    return theme;
}

const ChartTheme& Vibrant() {
    static const ChartTheme theme = MakeLightTheme(
        "Vibrant",
        ChartPalette("Vibrant",
                     {Color(52, 152, 219, 255), Color(46, 204, 113, 255),
                      Color(241, 196, 15, 255), Color(230, 126, 34, 255),
                      Color(231, 76, 60, 255), Color(155, 89, 182, 255),
                      Color(26, 188, 156, 255), Color(52, 73, 94, 255)}));
    return theme;
}

const ChartTheme& Pastel() {
    // Soft furniture to match the soft palette: warm off-white ground, gentle
    // grid, muted warm-grey text - none of the Light theme's hard contrast.
    static const ChartTheme theme = [] {
        ChartTheme t;
        t.name = "Pastel";
        t.backgroundColor = Color(251, 249, 246, 255);
        t.plotAreaColor = Color(255, 255, 255, 255);
        t.gridColor = Color(234, 230, 224, 255);
        t.axisLineColor = Color(150, 145, 138, 255);
        t.axisTickColor = Color(150, 145, 138, 255);
        t.axisLabelColor = Color(120, 115, 108, 255);
        t.titleTextColor = Color(90, 85, 80, 255);
        t.legendTextColor = Color(110, 105, 98, 255);
        t.legendBackground = Color(255, 255, 255, 230);
        t.palette = ChartPalette(
            "Pastel",
            {Color(129, 199, 212, 255), Color(160, 214, 180, 255),
             Color(247, 214, 141, 255), Color(244, 177, 131, 255),
             Color(240, 152, 152, 255), Color(190, 168, 220, 255),
             Color(150, 206, 199, 255), Color(176, 186, 200, 255)});
        return t;
    }();
    return theme;
}

const ChartTheme& Colorblind() {
    static const ChartTheme theme = MakeLightTheme(
        "Colorblind",
        ChartPalette("OkabeIto",
                     {Color(230, 159, 0, 255), Color(86, 180, 233, 255),
                      Color(0, 158, 115, 255), Color(240, 228, 66, 255),
                      Color(0, 114, 178, 255), Color(213, 94, 0, 255),
                      Color(204, 121, 167, 255), Color(100, 100, 100, 255)}));
    return theme;
}

const ChartTheme& Material() {
    static const ChartTheme theme = MakeLightTheme(
        "Material",
        ChartPalette("Material",
                     {Color(66, 133, 244, 255), Color(219, 68, 55, 255),
                      Color(244, 180, 0, 255), Color(15, 157, 88, 255),
                      Color(171, 71, 188, 255), Color(0, 172, 193, 255),
                      Color(255, 112, 67, 255), Color(158, 157, 36, 255),
                      Color(92, 107, 192, 255), Color(240, 98, 146, 255)}));
    return theme;
}

const ChartTheme& Classic() {
    static const ChartTheme theme = MakeLightTheme(
        "Classic",
        ChartPalette("Classic",
                     {Color(54, 162, 235, 255), Color(255, 99, 132, 255),
                      Color(255, 205, 86, 255), Color(75, 192, 192, 255),
                      Color(153, 102, 255, 255), Color(255, 159, 64, 255),
                      Color(199, 199, 199, 255), Color(83, 102, 255, 255),
                      Color(255, 99, 255, 255), Color(99, 255, 132, 255)}));
    return theme;
}

const ChartTheme& Tableau() {
    static const ChartTheme theme = MakeLightTheme(
        "Tableau",
        ChartPalette("Tableau",
                     {Color(78, 121, 167, 255), Color(242, 142, 43, 255),
                      Color(225, 87, 89, 255), Color(118, 183, 178, 255),
                      Color(89, 161, 79, 255), Color(237, 201, 72, 255),
                      Color(176, 122, 161, 255), Color(255, 157, 167, 255),
                      Color(156, 117, 95, 255), Color(186, 176, 172, 255)}));
    return theme;
}

const ChartTheme& Ocean() {
    static const ChartTheme theme = MakeLightTheme(
        "Ocean",
        ChartPalette("Ocean",
                     {Color(13, 71, 111, 255), Color(21, 101, 152, 255),
                      Color(31, 133, 176, 255), Color(46, 162, 186, 255),
                      Color(78, 190, 189, 255), Color(122, 209, 190, 255),
                      Color(163, 223, 197, 255), Color(200, 235, 213, 255)},
                     true));
    return theme;
}

const ChartTheme& Sunset() {
    static const ChartTheme theme = MakeLightTheme(
        "Sunset",
        ChartPalette("Sunset",
                     {Color(122, 40, 96, 255), Color(176, 52, 92, 255),
                      Color(219, 80, 74, 255), Color(238, 124, 62, 255),
                      Color(247, 165, 60, 255), Color(250, 200, 88, 255),
                      Color(252, 222, 133, 255), Color(253, 237, 183, 255)},
                     true));
    return theme;
}

const ChartTheme& Forest() {
    static const ChartTheme theme = MakeLightTheme(
        "Forest",
        ChartPalette("Forest",
                     {Color(27, 67, 50, 255), Color(45, 106, 79, 255),
                      Color(64, 145, 108, 255), Color(82, 183, 136, 255),
                      Color(116, 198, 157, 255), Color(149, 213, 178, 255),
                      Color(183, 228, 199, 255), Color(216, 243, 220, 255)},
                     true));
    return theme;
}

const ChartTheme& Slate() {
    static const ChartTheme theme = MakeLightTheme(
        "Slate",
        ChartPalette("Slate",
                     {Color(46, 52, 64, 255), Color(59, 66, 82, 255),
                      Color(76, 86, 106, 255), Color(94, 107, 130, 255),
                      Color(115, 129, 152, 255), Color(136, 150, 171, 255),
                      Color(160, 172, 190, 255), Color(184, 194, 208, 255)},
                     true));
    return theme;
}

const ChartTheme& Monochrome() {
    static const ChartTheme theme = MakeLightTheme(
        "Monochrome",
        ChartPalette("Monochrome",
                     {Color(33, 37, 43, 255), Color(60, 66, 76, 255),
                      Color(90, 97, 109, 255), Color(120, 128, 141, 255),
                      Color(150, 158, 171, 255), Color(180, 187, 198, 255)},
                     true));
    return theme;
}

namespace {

const std::vector<const ChartTheme*>& Registry() {
    static const std::vector<const ChartTheme*> registry = {
        &Light(),  &Dark(),       &Corporate(), &Vibrant(), &Pastel(),
        &Colorblind(), &Material(), &Classic(), &Tableau(), &Ocean(),
        &Sunset(), &Forest(),     &Slate(),     &Monochrome()};
    return registry;
}

bool NameEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

} // namespace

const ChartTheme* Find(const std::string& name) {
    for (const ChartTheme* theme : Registry()) {
        if (NameEquals(theme->name, name)) return theme;
    }
    return nullptr;
}

const ChartTheme& Get(const std::string& name) {
    const ChartTheme* theme = Find(name);
    return theme ? *theme : Light();
}

std::vector<std::string> Names() {
    std::vector<std::string> names;
    names.reserve(Registry().size());
    for (const ChartTheme* theme : Registry()) names.push_back(theme->name);
    return names;
}

} // namespace ChartThemes

} // namespace UltraCanvas
