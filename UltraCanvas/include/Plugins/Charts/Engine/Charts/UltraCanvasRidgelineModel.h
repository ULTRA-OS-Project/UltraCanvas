// include/Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h
// The UI-free model of a ridgeline chart (a joyplot): one row per category,
// each with a curve per series that shows how its values are spread - a
// kernel density of raw values, a histogram of them, or a curve the program
// already has - and the statistics a row reads out. UltraCanvasRidgelineChart
// draws it.
//
// The density is Gaussian. Its bandwidth comes from Scott's or Silverman's
// rule or is set, and a bounded scale (a 0-10 rating) reflects the kernels
// at its ends, so no mass leaks past them. A wrapping scale (the hours of a
// day) wraps them instead. Large rows are binned first (linear binning),
// which keeps the cost independent of the row's size.
//
// Design: Docs/UltraCanvas/UltraCanvasRidgelineChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasRidgelineChart.md
//
// Version: 1.1.0 - Phase 2: several series per row, a wrapping x axis
//                  (period), tail cut, binned curves, the JSON and CSV
//                  interchange, the Sleep sample
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

class JSONValue;   // DataFormats/UltraCanvasJSON.h - only the JSON functions need it

enum class RidgelineBandwidth { Scott, Silverman, Fixed };

// How the rows' curves are scaled against each other.
enum class RidgelineScale {
    RowPeak,   // every row to its own peak (the tallest of its series): only shapes are compared
    Shared,    // one scale for all rows: heights are compared too
    RowArea    // every curve to the same area, then one scale
};

enum class RidgelineSort { AsGiven, ByMean, ByMedian, ByPeak, ByName };

struct RidgelineDensityOptions {
    RidgelineBandwidth rule = RidgelineBandwidth::Scott;
    double bandwidth = 0.0;              // Fixed: in x units
    double adjust = 1.0;                 // multiplies the bandwidth the rule gives
    // One bandwidth for every row (the mean of the curves' own), so the rows
    // are equally smooth; false = each its own.
    bool sharedBandwidth = false;
    // A bounded scale: the density is reflected at the bounds and drawn only
    // between them. Ignored when the model has a period.
    std::optional<double> lowerBound;
    std::optional<double> upperBound;
    int resolution = 256;                // points each density is evaluated at
    // End each curve where it falls below this share of its peak (0..0.5):
    // no long flat tails along the baselines. 0 = draw them.
    double tailCut = 0.0;
    // Count the values in `bins` equal bins over the x range instead of
    // smoothing them: a stepped outline, the histogram look.
    bool binned = false;
    int bins = 20;
};

struct RidgelineCurvePoint {
    double x = 0.0;
    double y = 0.0;
};

// One series' data in one row.
struct RidgelineEntry {
    std::vector<double> values;               // raw values: the curve is their density
    std::vector<RidgelineCurvePoint> curve;   // or the curve itself, x increasing

    bool HasValues() const { return !values.empty(); }
    bool HasData() const { return !values.empty() || curve.size() >= 2; }
};

struct RidgelineRow {
    std::string name;
    // The first series' data.
    std::vector<double> values;
    std::vector<RidgelineCurvePoint> curve;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the chart's colouring
    std::string note;                    // an extra tooltip line
    // The second series' data and on (model.series names them).
    std::vector<RidgelineEntry> more;

    bool HasValues() const { return !values.empty(); }
    bool HasData() const { return !values.empty() || curve.size() >= 2; }
    // Series `s`'s data (0 = values / curve above); empty past the last.
    const std::vector<double>& ValuesOf(size_t s) const;
    const std::vector<RidgelineCurvePoint>& CurveOf(size_t s) const;
    bool HasValuesOf(size_t s) const { return !ValuesOf(s).empty(); }
    bool HasDataOf(size_t s) const { return HasValuesOf(s) || CurveOf(s).size() >= 2; }
    // Replaces series `s`'s data, adding entries up to it.
    void SetValues(size_t s, std::vector<double> data);
    void SetCurve(size_t s, std::vector<RidgelineCurvePoint> data);
};

struct RidgelineSeries {
    std::string name;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the theme's palette
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
    // The series, when there are two or more (one legend entry each); empty
    // = one unnamed series.
    std::vector<RidgelineSeries> series;
    std::vector<RidgelineRow> rows;      // from the top down
    RidgelineDensityOptions density;
    RidgelineScale scale = RidgelineScale::Shared;
    std::optional<double> xMin, xMax;    // unset = from the data
    // A wrapping x axis, such as the 24 hours of a day: the axis runs from
    // xMin (0 when unset) for one period, values before or after it are
    // moved by whole periods into it, and a density wraps across its ends.
    std::optional<double> period;

    // Series in use: model.series, or the most any row has.
    size_t SeriesCount() const;
    // `x` moved by whole periods into the axis; `x` itself without a period.
    double Wrap(double x) const;
    // The x range the rows are drawn over: one period from xMin when the axis
    // wraps; xMin / xMax when set; otherwise the data's range, a density's
    // extended by three bandwidths, clamped to the bounds.
    std::pair<double, double> XRange() const;
    // The bandwidth a values curve is smoothed with (0 for a supplied curve).
    double Bandwidth(size_t row, size_t series = 0) const;
    // The curve in data units: the density (or histogram) of the values over
    // XRange, or the supplied curve, wrapped and tail-cut. Empty without data.
    std::vector<RidgelineCurvePoint> RawCurve(size_t row, size_t series = 0) const;
    // The same curve scaled by `scale`: the tallest drawn height is 1.
    std::vector<RidgelineCurvePoint> Curve(size_t row, size_t series = 0) const;
    // One series' scaled curve for every row (cheaper than Curve per row).
    std::vector<std::vector<RidgelineCurvePoint>> Curves(size_t series = 0) const;
    // Every row's scaled curves at once: [row][series].
    std::vector<std::vector<std::vector<RidgelineCurvePoint>>> AllCurves() const;
    RidgelineRowStatistics Statistics(size_t row, size_t series = 0) const;
    // Reorders the rows by their first series with data (stable; rows
    // without data go last).
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
    // When people of seven age groups might go to bed and wake up: two series
    // per row, 250 answers each, on a 24-hour axis from 18:00 to 18:00 that
    // wraps (bedtimes after midnight stay next to those before it).
    static UltraCanvasRidgelineModel Sleep();
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

// ---- interchange ---------------------------------------------------------------
// The model functions take and return text; reading and writing the files is
// UltraCanvasFileLoader's job (CLAUDE.md).
//
// JSON - the whole model as { "format": "ultracanvas-ridgeline", "version": 1,
// ... }: titles, series, rows with their values or curves, the density
// options, scale, range and period. Defaults are left out. The reader refuses
// - leaving `out` untouched and saying why - another format, a newer version,
// an unknown rule or scale, an unreadable colour or number, or a model that
// does not Validate().
JSONValue RidgelineModelToJson(const UltraCanvasRidgelineModel& model);
std::string ExportRidgelineJson(const UltraCanvasRidgelineModel& model, bool pretty = true);
bool RidgelineModelFromJson(const JSONValue& json, UltraCanvasRidgelineModel& out, std::string* error = nullptr);
bool ImportRidgelineJson(const std::string& text, UltraCanvasRidgelineModel& out, std::string* error = nullptr);

// CSV - the data only, one value or one curve point per line under a header:
//   row,series,value        (values; series only when there are two or more)
//   row,series,x,y          (curves)
// The row column may also be called name, group or category; columns come in
// any order, and a file may mix value lines and curve-point lines (a line
// fills one or the other). Numbers are dot-decimal whatever the locale.
// Importing replaces the rows and series, keeps every other setting, and
// keeps a series' colour when the model already has it. Fails - leaving the
// model untouched and naming the line - on a missing row column, neither a
// value column nor x and y, a line with more or fewer fields than the
// header, or a value that is not a number.
std::string ExportRidgelineCsv(const UltraCanvasRidgelineModel& model);
bool ImportRidgelineCsv(const std::string& csv, UltraCanvasRidgelineModel& model, std::string* error = nullptr);

} // namespace UltraCanvas
