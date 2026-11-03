// Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.cpp
// The UI-free model of the ternary chart and its sample models.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// =============================================================================
// MODEL
// =============================================================================

    bool TernaryPoint::Shares(double& sa, double& sb, double& sc) const {
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return false;
        if (a < 0.0 || b < 0.0 || c < 0.0) return false;
        const double sum = a + b + c;
        if (!(sum > 0.0) || !std::isfinite(sum)) return false;
        sa = a / sum;
        sb = b / sum;
        sc = c / sum;
        return true;
    }

    int TernaryModel::LabelEdgeStart(int corner) const {
        // Clockwise on screen is B -> A -> C -> B, so each axis starts at the
        // corner before its own: A from B, C from A, B from C.
        return reading == TernaryReading::Clockwise ? (corner + 1) % 3 : (corner + 2) % 3;
    }

    std::vector<double> TernaryModel::Ticks(int corner) const {
        std::vector<double> ticks;
        if (corner < 0 || corner > 2) return ticks;
        const double step = axes[corner].tickStep;
        if (!(step > 0.0) || step > 0.5) return {0.0, 1.0};
        // Counted, not accumulated, so 0.1 * 10 lands on 1 exactly.
        const int count = static_cast<int>(std::floor(1.0 / step + 1e-9));
        for (int k = 0; k <= count; ++k) ticks.push_back(std::min(1.0, k * step));
        if (ticks.back() < 1.0 - 1e-9) ticks.push_back(1.0);
        return ticks;
    }

    size_t TernaryModel::PointCount() const {
        size_t n = 0;
        for (const TernarySeries& s : series) n += s.points.size();
        return n;
    }

    std::vector<std::string> TernaryModel::Validate() const {
        std::vector<std::string> problems;
        const char* corners[3] = {"A", "B", "C"};
        for (int i = 0; i < 3; ++i) {
            if (axes[i].title.empty()) problems.push_back(std::string("Axis ") + corners[i] + " has no title");
            if (!(axes[i].tickStep > 0.0) || axes[i].tickStep > 0.5) {
                problems.push_back(std::string("Axis ") + corners[i] + " has a tick step outside (0, 0.5]");
            }
        }
        for (const TernarySeries& s : series) {
            for (size_t p = 0; p < s.points.size(); ++p) {
                double sa = 0.0, sb = 0.0, sc = 0.0;
                if (s.points[p].Shares(sa, sb, sc)) continue;
                const std::string name = s.points[p].label.empty() ? "point " + std::to_string(p)
                                                                   : "\"" + s.points[p].label + "\"";
                problems.push_back("In \"" + s.name + "\", " + name +
                                   " has a negative or missing part, or parts summing to 0");
            }
        }
        return problems;
    }

    std::string FormatTernaryShare(double share) {
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f%%", share * 100.0);   // locale-ok: a tooltip a person reads
        return text;
    }

// =============================================================================
// SAMPLES
// =============================================================================

    namespace TernarySamples {

        TernaryModel ThreePartyVotes() {
            TernaryModel model;
            model.title = "Three-party vote by region (invented)";
            model.subtitle = "Each point is one region; votes cast, shown as shares";
            model.axes[0] = TernaryAxis("Blue Party");
            model.axes[1] = TernaryAxis("Red Party");
            model.axes[2] = TernaryAxis("Gold Party");
            model.reading = TernaryReading::Anticlockwise;

            // Votes per region: Blue, Red, Gold. Every number is made up.
            struct Row { const char* region; double blue, red, gold; };
            const Row earlier[] = {
                {"Northfield", 41200, 35100, 18900}, {"Harbourside", 22800, 51300, 20400},
                {"Lakeview", 30500, 29800, 33700},   {"Old Town", 18700, 44100, 31900},
                {"Westmoor", 52400, 21900, 14300},   {"Riverbend", 35600, 38800, 22100},
                {"Hillcrest", 47100, 26400, 25200},  {"Eastgate", 26300, 47700, 17800},
                {"Southbay", 28900, 31200, 41500},   {"Milltown", 33800, 42600, 12900},
                {"Greenway", 24100, 27300, 46800},   {"Kingsport", 44900, 33500, 20700},
            };
            const Row later[] = {
                {"Northfield", 38900, 31700, 26300}, {"Harbourside", 20100, 49200, 27700},
                {"Lakeview", 27400, 26100, 41900},   {"Old Town", 16900, 40800, 38600},
                {"Westmoor", 50800, 19600, 19900},   {"Riverbend", 33100, 35300, 29400},
                {"Hillcrest", 45600, 23900, 30100},  {"Eastgate", 24800, 46100, 23500},
                {"Southbay", 25200, 27900, 48600},   {"Milltown", 32900, 41700, 17600},
                {"Greenway", 21800, 24600, 53400},   {"Kingsport", 42700, 30800, 26900},
            };
            TernarySeries first("2022 election");
            for (const Row& r : earlier) first.points.emplace_back(r.blue, r.red, r.gold, r.region);
            TernarySeries second("2026 election");
            second.marker = ChartMarkerShape::Diamond;
            for (const Row& r : later) second.points.emplace_back(r.blue, r.red, r.gold, r.region);
            model.series = {first, second};
            return model;
        }

        TernaryModel SoilSamples() {
            TernaryModel model;
            model.title = "Soil samples (invented)";
            model.subtitle = "Particle-size shares of each sample";
            model.axes[0] = TernaryAxis("Clay");
            model.axes[1] = TernaryAxis("Sand");
            model.axes[2] = TernaryAxis("Silt");
            model.reading = TernaryReading::Clockwise;

            // Clay, sand, silt in percent, as a lab reports them - they do not
            // always add up to exactly 100. Every sample is made up.
            struct Row { const char* id; double clay, sand, silt; };
            const Row river[] = {
                {"R-01", 8.2, 71.5, 20.1}, {"R-02", 11.4, 62.0, 26.9}, {"R-03", 6.1, 82.3, 11.4},
                {"R-04", 14.8, 55.2, 30.3}, {"R-05", 9.7, 68.8, 21.6}, {"R-06", 4.3, 88.1, 7.5},
                {"R-07", 18.2, 47.9, 33.6}, {"R-08", 12.6, 58.4, 29.1},
            };
            const Row upland[] = {
                {"U-01", 27.3, 22.4, 50.1}, {"U-02", 33.8, 18.1, 48.0}, {"U-03", 22.1, 30.5, 47.6},
                {"U-04", 41.5, 15.2, 43.4}, {"U-05", 18.9, 25.7, 55.2}, {"U-06", 36.2, 28.0, 35.9},
                {"U-07", 12.4, 14.3, 73.1}, {"U-08", 29.9, 33.6, 36.4},
            };
            const Row basin[] = {
                {"B-01", 52.3, 12.8, 34.7}, {"B-02", 46.1, 21.5, 32.5}, {"B-03", 61.8, 9.4, 28.9},
                {"B-04", 43.7, 38.2, 18.0}, {"B-05", 38.5, 44.9, 16.8}, {"B-06", 57.0, 18.6, 24.3},
            };
            auto add = [&model](const char* name, const Row* rows, size_t count, ChartMarkerShape marker) {
                TernarySeries s(name);
                s.marker = marker;
                for (size_t i = 0; i < count; ++i) s.points.emplace_back(rows[i].clay, rows[i].sand, rows[i].silt, rows[i].id);
                model.series.push_back(s);
            };
            add("River terrace", river, sizeof(river) / sizeof(river[0]), ChartMarkerShape::Circle);
            add("Upland", upland, sizeof(upland) / sizeof(upland[0]), ChartMarkerShape::Square);
            add("Basin", basin, sizeof(basin) / sizeof(basin[0]), ChartMarkerShape::TriangleUp);
            return model;
        }

    } // namespace TernarySamples

} // namespace UltraCanvas
