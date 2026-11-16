// include/Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h
// The UI-free model of a ridgeline chart (a joyplot): one row per category,
// each with a curve that shows how its values are spread - a kernel density
// of raw values, or a curve the program already has - and the statistics a
// row reads out. UltraCanvasRidgelineChart draws it.
//
// The density is Gaussian. Its bandwidth comes from Scott's or Silverman's
// rule or is set, and a bounded scale (a 0-10 rating) reflects the kernels
// at its ends, so no mass leaks past them. Large rows are binned first
// (linear binning), which keeps the cost independent of the row's size.
//
// Design: Docs/UltraCanvas/UltraCanvasRidgelineChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasRidgelineChart.md
//
// Version: 1.0.0 - Phase 1: rows from values or curves, density, scaling,
//                  sorting, statistics, the WordRatings and DailyActivities
//                  samples
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

enum class RidgelineBandwidth { Scott, Silverman, Fixed };

// How the rows' curves are scaled against each other.
enum class RidgelineScale {
    RowPeak,   // every row to its own peak: only the shapes are compared
    Shared,    // one scale for all rows: heights are compared too
    RowArea    // every row to the same area, then one scale
};

enum class RidgelineSort { AsGiven, ByMean, ByMedian, ByPeak, ByName };

struct RidgelineDensityOptions {
    RidgelineBandwidth rule = RidgelineBandwidth::Scott;
    double bandwidth = 0.0;              // Fixed: in x units
    double adjust = 1.0;                 // multiplies the bandwidth the rule gives
    // One bandwidth for every row (the mean of the rows' own), so the rows
    // are equally smooth; false = each row its own.
    bool sharedBandwidth = false;
    // A bounded scale: the density is reflected at the bounds and drawn only
    // between them.
    std::optional<double> lowerBound;
    std::optional<double> upperBound;
    int resolution = 256;                // points each density is evaluated at
};

struct RidgelineCurvePoint {
    double x = 0.0;
    double y = 0.0;
};

struct RidgelineRow {
    std::string name;
    // Raw values: the row's curve is their density. Used when not empty.
    std::vector<double> values;
    // Or the curve itself, (x, y) points in increasing x; y >= 0.
    std::vector<RidgelineCurvePoint> curve;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the chart's colouring
    std::string note;                    // an extra tooltip line

    bool HasValues() const { return !values.empty(); }
    bool HasData() const { return !values.empty() || curve.size() >= 2; }
};

struct RidgelineRowStatistics {
    size_t count = 0;                    // values; 0 for a supplied curve
    double mean = 0.0;
    double median = 0.0;
    double standardDeviation = 0.0;      // sample (n - 1) for values
    double quartile1 = 0.0;
    double quartile3 = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double peakAt = 0.0;                 // x of the curve's highest point
    bool valid = false;                  // false: the row has no data
};

struct UltraCanvasRidgelineModel {
    std::string title;
    std::string subtitle;
    std::string xTitle;
    std::string curveName = "Density";   // what a curve's height is, for the tooltip
    std::vector<RidgelineRow> rows;      // from the top down
    RidgelineDensityOptions density;
    RidgelineScale scale = RidgelineScale::Shared;
    std::optional<double> xMin, xMax;    // unset = from the data

    // The x range the rows are drawn over: xMin / xMax when set; otherwise
    // the data's range, a density's extended by three bandwidths, clamped to
    // the bounds.
    std::pair<double, double> XRange() const;
    // The bandwidth a values row is smoothed with (0 for a curve row).
    double Bandwidth(size_t row) const;
    // The row's curve in data units: the density of its values over XRange,
    // or its own curve. Empty for a row without data.
    std::vector<RidgelineCurvePoint> RawCurve(size_t row) const;
    // The same curve scaled by `scale`: the tallest drawn height is 1.
    std::vector<RidgelineCurvePoint> Curve(size_t row) const;
    // Every row's scaled curve at once (the same as Curve per row, cheaper).
    std::vector<std::vector<RidgelineCurvePoint>> Curves() const;
    RidgelineRowStatistics Statistics(size_t row) const;
    // Reorders the rows (stable; rows without data go last).
    void Sort(RidgelineSort by, bool descending = false);
    // Problems that keep the model from drawing as meant; empty = fine.
    std::vector<std::string> Validate() const;

    // ---- samples (invented data) -------------------------------------------
    // How people might rate 33 words on a scale of 0 to 10, 300 answers each,
    // sorted by the mean.
    static UltraCanvasRidgelineModel WordRatings();
    // When in the day people might do 16 activities, as participants per
    // quarter hour from 03:00 to 03:00 (x in hours, 3..27), each to its own
    // peak, sorted by when it peaks.
    static UltraCanvasRidgelineModel DailyActivities();
};

// ---- density ------------------------------------------------------------------

// The Gaussian kernel density of `values` at each of `xs`, integrating to 1.
// With a bound, each kernel is reflected at it and the density outside is 0.
std::vector<double> RidgelineKernelDensity(const std::vector<double>& values,
                                           const std::vector<double>& xs,
                                           double bandwidth,
                                           std::optional<double> lowerBound = std::nullopt,
                                           std::optional<double> upperBound = std::nullopt);
// Scott's rule: 1.06 sigma n^(-1/5). 0 for fewer than two distinct values.
double RidgelineScottBandwidth(const std::vector<double>& values);
// Silverman's rule: 0.9 min(sigma, IQR / 1.34) n^(-1/5). 0 as above.
double RidgelineSilvermanBandwidth(const std::vector<double>& values);
// The p-quantile (0..1) of sorted values, linearly interpolated (R's type 7).
double RidgelineQuantile(const std::vector<double>& sorted, double p);

} // namespace UltraCanvas
