// Plugins/Charts/UltraCanvasColormap.cpp
// Colour-map utilities for heatmap-style charts (UI-free).
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    std::vector<Color> ColormapAnchors(HeatmapColormap c) {
        switch (c) {
            case HeatmapColormap::Grayscale:
                return { Color(0, 0, 0), Color(255, 255, 255) };
            case HeatmapColormap::Viridis:
                return {
                    Color(68, 1, 84),   Color(72, 40, 120),  Color(62, 73, 137),
                    Color(49, 104, 142),Color(38, 130, 142), Color(31, 158, 137),
                    Color(53, 183, 121),Color(110, 206, 88), Color(181, 222, 43),
                    Color(253, 231, 37)
                };
            case HeatmapColormap::Inferno:
                return {
                    Color(0, 0, 4),     Color(31, 12, 72),   Color(85, 15, 109),
                    Color(136, 34, 106),Color(186, 54, 85),  Color(227, 89, 51),
                    Color(249, 140, 10),Color(249, 201, 50), Color(252, 255, 164)
                };
            case HeatmapColormap::Magma:
                return {
                    Color(0, 0, 4),     Color(28, 16, 68),   Color(79, 18, 123),
                    Color(129, 37, 129),Color(181, 54, 122), Color(229, 80, 100),
                    Color(251, 135, 97),Color(254, 194, 135),Color(252, 253, 191)
                };
            case HeatmapColormap::Plasma:
                return {
                    Color(13, 8, 135),  Color(75, 3, 161),   Color(125, 3, 168),
                    Color(168, 34, 150),Color(203, 70, 121), Color(229, 107, 93),
                    Color(248, 148, 65),Color(253, 195, 40), Color(240, 249, 33)
                };
            case HeatmapColormap::Cividis:
                return {
                    Color(0, 34, 78),   Color(38, 62, 110),  Color(73, 82, 112),
                    Color(108, 103, 109),Color(146, 123, 103),Color(187, 145, 87),
                    Color(225, 170, 61),Color(255, 221, 57)
                };
            case HeatmapColormap::Turbo:
                return {
                    Color(48, 18, 59),  Color(64, 90, 210),  Color(28, 168, 232),
                    Color(36, 231, 138),Color(166, 249, 40), Color(250, 186, 30),
                    Color(232, 79, 12), Color(122, 4, 3)
                };
            case HeatmapColormap::Jet:
                return {
                    Color(0, 0, 128),   Color(0, 0, 255),    Color(0, 255, 255),
                    Color(255, 255, 0), Color(255, 0, 0),    Color(128, 0, 0)
                };
            case HeatmapColormap::Hot:
                return {
                    Color(0, 0, 0), Color(255, 0, 0), Color(255, 255, 0), Color(255, 255, 255)
                };
            case HeatmapColormap::Cool:
                return { Color(0, 255, 255), Color(255, 0, 255) };
            case HeatmapColormap::Blues:
                return {
                    Color(247, 251, 255),Color(198, 219, 239),Color(158, 202, 225),
                    Color(107, 174, 214),Color(66, 146, 198), Color(33, 113, 181),
                    Color(8, 81, 156),   Color(8, 48, 107)
                };
            case HeatmapColormap::Greens:
                return {
                    Color(247, 252, 245),Color(199, 233, 192),Color(161, 217, 155),
                    Color(116, 196, 118),Color(65, 171, 93),  Color(35, 139, 69),
                    Color(0, 109, 44),   Color(0, 68, 27)
                };
            case HeatmapColormap::Reds:
                return {
                    Color(255, 245, 240),Color(252, 187, 161),Color(252, 146, 114),
                    Color(251, 106, 74), Color(239, 59, 44),  Color(203, 24, 29),
                    Color(165, 15, 21),  Color(103, 0, 13)
                };
            case HeatmapColormap::Oranges:
                return {
                    Color(255, 245, 235),Color(253, 208, 162),Color(253, 174, 107),
                    Color(253, 141, 60), Color(241, 105, 19), Color(217, 72, 1),
                    Color(166, 54, 3),   Color(127, 39, 4)
                };
            case HeatmapColormap::Spectral:
                return {
                    Color(158, 1, 66),  Color(213, 62, 79),  Color(244, 109, 67),
                    Color(253, 174, 97),Color(254, 224, 139),Color(255, 255, 191),
                    Color(230, 245, 152),Color(171, 221, 164),Color(102, 194, 165),
                    Color(50, 136, 189),Color(94, 79, 162)
                };
            case HeatmapColormap::RdBu:
                return {
                    Color(103, 0, 31),  Color(178, 24, 43),  Color(214, 96, 77),
                    Color(244, 165, 130),Color(253, 219, 199),Color(247, 247, 247),
                    Color(209, 229, 240),Color(146, 197, 222),Color(67, 147, 195),
                    Color(33, 102, 172),Color(5, 48, 97)
                };
            case HeatmapColormap::RdYlBu:
                return {
                    Color(165, 0, 38),  Color(215, 48, 39),  Color(244, 109, 67),
                    Color(253, 174, 97),Color(254, 224, 144),Color(255, 255, 191),
                    Color(224, 243, 248),Color(171, 217, 233),Color(116, 173, 209),
                    Color(69, 117, 180),Color(49, 54, 149)
                };
            case HeatmapColormap::RdYlGn:
                return {
                    Color(165, 0, 38),  Color(215, 48, 39),  Color(244, 109, 67),
                    Color(253, 174, 97),Color(254, 224, 139),Color(255, 255, 191),
                    Color(217, 239, 139),Color(166, 217, 106),Color(102, 189, 99),
                    Color(26, 152, 80), Color(0, 104, 55)
                };
            case HeatmapColormap::Coolwarm:
                return {
                    Color(59, 76, 192), Color(124, 159, 249),Color(192, 212, 245),
                    Color(242, 242, 242),Color(246, 194, 165),Color(222, 96, 77),
                    Color(180, 4, 38)
                };
            case HeatmapColormap::Custom:
            default:
                return { Color(0, 0, 0), Color(255, 255, 255) };
        }
    }

    Color InterpolateColormap(const std::vector<Color>& anchors, double t) {
        if (anchors.empty()) return Color(0, 0, 0);
        if (anchors.size() == 1) return anchors[0];
        t = std::clamp(t, 0.0, 1.0);
        double scaled = t * (anchors.size() - 1);
        int i = static_cast<int>(std::floor(scaled));
        if (i >= static_cast<int>(anchors.size()) - 1) return anchors.back();
        double f = scaled - i;
        const Color& a = anchors[i];
        const Color& b = anchors[i + 1];
        auto lerp = [](uint8_t x, uint8_t y, double f) -> uint8_t {
            return static_cast<uint8_t>(std::lround(x + (y - x) * f));
        };
        return Color(lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f), 255);
    }

    Color SampleColormap(HeatmapColormap c, const std::vector<Color>& custom,
                         double t, bool reverse) {
        t = std::clamp(t, 0.0, 1.0);
        if (reverse) t = 1.0 - t;
        if (c == HeatmapColormap::Custom && custom.size() >= 2) {
            return InterpolateColormap(custom, t);
        }
        return InterpolateColormap(ColormapAnchors(c), t);
    }

    bool IsDivergingColormap(HeatmapColormap c) {
        switch (c) {
            case HeatmapColormap::Spectral:
            case HeatmapColormap::RdBu:
            case HeatmapColormap::RdYlBu:
            case HeatmapColormap::RdYlGn:
            case HeatmapColormap::Coolwarm:
                return true;
            default:
                return false;
        }
    }

    double QuantizeNorm(double t, int levels) {
        if (levels < 2) return t;
        t = std::clamp(t, 0.0, 1.0);
        int idx = static_cast<int>(t * levels);
        if (idx >= levels) idx = levels - 1;
        return (idx + 0.5) / levels;
    }

    double DivergingNorm(double v, double lo, double mid, double hi) {
        double t;
        if (v <= mid) {
            double d = mid - lo;
            t = (d > 0.0) ? 0.5 * (v - lo) / d : 0.0;
        } else {
            double d = hi - mid;
            t = (d > 0.0) ? 0.5 + 0.5 * (v - mid) / d : 1.0;
        }
        return std::clamp(t, 0.0, 1.0);
    }

} // namespace UltraCanvas
