// include/Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h
// The UI-free model of the ternary chart: compositions of three parts, each
// drawn as a point in a triangle whose corners are 100 % of one part.
//
// A point keeps the amounts it was given (counts, grams, votes); its shares
// are computed by dividing by their sum, never stored, so the tooltip can
// still show the raw values and the shares always add up to 1.
//
// The corners are fixed: A is the apex, B the bottom left, C the bottom
// right. Which part goes to which corner is the model's choice, and which
// way the axes read round the triangle is one flag (TernaryReading).
//
// Design: Docs/UltraCanvas/UltraCanvasTernaryChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasTernaryChart.md
//
// Version: 1.3.0 - edgeLean: which side of a shared edge a point on it belongs to;
//   the USDA preset follows the Soil Survey Manual's class boundaries
// Version: 1.2.0 - Phase 3: the sub-triangle zoom - a minimum share per axis,
//   ZoomSpan, ToZoomed / FromZoomed, ticks over the visible range
// Version: 1.1.0 - Phase 2: regions with classification (RegionOf), paths,
//   bubble sizes, reference curves, the JSON round trip of the whole model and
//   the CSV of the points; the SoilTexture (USDA), DeFinetti and
//   VoteTrajectories samples
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"   // ChartMarkerShape
#include <array>
#include <string>
#include <vector>

namespace UltraCanvas {

    class JSONValue;   // DataFormats/UltraCanvasJSON.h - only the JSON functions need it

    // Three amounts, as given; a region corner or a curve vertex.
    struct TernaryComposition {
        double a = 0.0, b = 0.0, c = 0.0;

        TernaryComposition() = default;
        TernaryComposition(double pa, double pb, double pc) : a(pa), b(pb), c(pc) {}
        // As TernaryPoint::Shares.
        bool Shares(double& sa, double& sb, double& sc) const;
    };

    // One composition, as given: any non-negative amounts.
    struct TernaryPoint {
        double a = 0.0, b = 0.0, c = 0.0;
        std::string label;
        std::string note;                 // tooltip only
        // A fourth value drawn as the marker's area (a bubble); 0 = the
        // series' marker size. Bubbles are scaled against the largest size
        // in the model.
        double size = 0.0;

        TernaryPoint() = default;
        TernaryPoint(double pa, double pb, double pc, std::string pointLabel = std::string())
            : a(pa), b(pb), c(pc), label(std::move(pointLabel)) {}

        // a / (a + b + c) etc. False - and the shares left alone - for a
        // negative or non-finite part, or a sum of 0.
        bool Shares(double& sa, double& sb, double& sc) const;
        double Sum() const { return a + b + c; }
    };

    // Scatter: the points alone. Path: the points joined in order - a
    // composition changing over time - with an arrowhead on each step when
    // `arrows` is set.
    enum class TernarySeriesKind { Scatter, Path };

    // A group of points with one colour, marker and legend entry.
    struct TernarySeries {
        std::string name;
        std::vector<TernaryPoint> points;
        TernarySeriesKind kind = TernarySeriesKind::Scatter;
        bool arrows = false;                      // Path only
        Color color = Color(0, 0, 0, 0);          // transparent = from the palette
        ChartMarkerShape marker = ChartMarkerShape::Circle;
        float markerRadius = 4.5f;

        TernarySeries() = default;
        explicit TernarySeries(std::string seriesName) : name(std::move(seriesName)) {}
    };

    // An authored area of the triangle: a soil texture class, a rock field.
    // The outline is closed and given in order; its corners may be in any
    // unit, as points are.
    struct TernaryRegion {
        std::string id;
        std::string name;                         // "Silty clay loam"; drawn in the region
        std::vector<TernaryComposition> outline;
        Color fill = Color(0, 0, 0, 0);           // transparent = from the palette, light
        std::string note;                         // tooltip only
    };

    // A reference line through the triangle: the Hardy-Weinberg parabola of a
    // De Finetti diagram, a mixing line, a phase boundary.
    struct TernaryCurve {
        std::string name;                         // the legend entry
        std::vector<TernaryComposition> points;   // in order
        Color color = Color(90, 90, 90, 255);
        float width = 1.6f;
        bool dashed = true;
    };

    // One part: the corner that is 100 % of it.
    struct TernaryAxis {
        std::string title;                // "Clay"; drawn at its corner and along its edge
        std::string unit = "%";           // after the tick numbers in the edge title
        double tickStep = 0.1;            // as a share: 0.1 = every 10 %; finer when zoomed in
        // The smallest share shown: above 0, the chart shows only the
        // sub-triangle where every part is at least its minimum (a zoom).
        // The three minimums must leave some of the triangle: their sum
        // stays under 1.
        double minimum = 0.0;

        TernaryAxis() = default;
        explicit TernaryAxis(std::string axisTitle) : title(std::move(axisTitle)) {}
    };

    // Which way the tick labels run round the triangle.
    //   Clockwise:     A up the left edge, C down the right edge, B right to left
    //                  along the bottom - the USDA soil texture triangle.
    //   Anticlockwise: A up the right edge, B down the left edge, C left to
    //                  right along the bottom.
    // Either way a share is read by following its grid line - parallel to the
    // edge opposite its corner - to the edge that carries its labels.
    enum class TernaryReading { Clockwise, Anticlockwise };

    struct TernaryModel {
        std::string title;
        std::string subtitle;
        TernaryAxis axes[3];              // A (apex), B (bottom left), C (bottom right)
        TernaryReading reading = TernaryReading::Clockwise;
        std::vector<TernarySeries> series;
        std::vector<TernaryRegion> regions;       // drawn under the grid, in order
        // Which region a point lying on an edge two regions share belongs
        // to: the one on the side this direction leans into - a change in the
        // A, B and C shares, summing to 0. All zero (the default) = the
        // region listed first. Where the lean would leave the triangle (a
        // point on its rim), the listing order decides. The USDA preset leans
        // to more clay and silt and less sand: the Soil Survey Manual gives
        // every class boundary to that side ("7 to less than 27 percent
        // clay", "52 percent or less sand").
        std::array<double, 3> edgeLean = {0.0, 0.0, 0.0};
        std::vector<TernaryCurve> curves;         // drawn over the regions, under the points

        // The corner whose labels run up to the given corner's, i.e. where
        // the axis of `corner` reads 0 % on its labelled edge. Clockwise:
        // A from B, B from C, C from A.
        int LabelEdgeStart(int corner) const;

        // The shares at which the axis has a tick, over the range it shows:
        // its minimum, the multiples of the step inside the range, and the
        // range's top (1 when not zoomed). Zoomed in far enough that the
        // step would leave fewer than three ticks, it uses a finer one.
        std::vector<double> Ticks(int corner) const;

        // ---- the zoom ------------------------------------------------------
        // The share of the whole triangle the zoomed one spans: 1 minus the
        // minimums. 1 = not zoomed.
        double ZoomSpan() const;
        bool IsZoomed() const { return ZoomSpan() < 1.0 - 1e-12; }
        // Shares of the whole triangle to shares of the zoomed one, and
        // back. A composition outside the zoomed triangle gets a negative
        // zoomed share.
        void ToZoomed(double sa, double sb, double sc, double& za, double& zb, double& zc) const;
        void FromZoomed(double za, double zb, double zc, double& sa, double& sb, double& sc) const;

        size_t PointCount() const;
        // The largest point size in the model; 0 when no point has one.
        double MaxPointSize() const;

        // The region a composition falls in, by the same geometry the chart
        // draws; -1 for none or for a composition with no shares. A point on
        // an edge two regions share belongs to the one edgeLean leans into,
        // else to the one listed first.
        int RegionOf(double a, double b, double c) const;
        int RegionOf(const TernaryPoint& point) const { return RegionOf(point.a, point.b, point.c); }

        // Problems that make the model unusable or misleading: an axis
        // without a title, a tick step outside (0, 0.5], a point with a
        // negative or non-finite part or a sum of 0, a negative point size,
        // a region with fewer than three corners or a corner with no shares,
        // a curve with fewer than two points, a minimum outside [0, 1) or
        // minimums that leave less than 1 % of the triangle. Empty = valid.
        std::vector<std::string> Validate() const;
    };

    // ===== INTERCHANGE =====
    // The model functions take and return text; reading and writing the files
    // is UltraCanvasFileLoader's job (CLAUDE.md).
    //
    // JSON - the whole model as { "format": "ultracanvas-ternary", "version": 1,
    // ... }: title, axes, reading, series with their points, regions and
    // curves. Empty strings, default numbers and transparent colours are left
    // out. The reader refuses - leaving `out` untouched and saying why - another
    // format, a newer version, an unknown series kind or reading, an
    // unreadable colour, or a model that does not Validate().
    JSONValue TernaryModelToJson(const TernaryModel& model);
    std::string ExportTernaryJson(const TernaryModel& model, bool pretty = true);
    bool TernaryModelFromJson(const JSONValue& json, TernaryModel& out, std::string* error = nullptr);
    bool ImportTernaryJson(const std::string& text, TernaryModel& out, std::string* error = nullptr);

    // CSV - the points only, one per line under a header row:
    //   label,series,<title A>,<title B>,<title C>,size,note
    // The three part columns are found by the axes' titles (any letter case)
    // or as a, b, c; the other columns may be left out and come in any order.
    // Numbers are dot-decimal whatever the locale. Importing replaces the
    // model's points: a series named in the file keeps its look when the
    // model already has it, a new one gets the default look, and lines
    // without a series go to "Points". Fails - leaving the model untouched and
    // naming the line - on a missing part column, a line with more or fewer
    // fields than the header, or a part or size that is not a number.
    std::string ExportTernaryCsv(const TernaryModel& model);
    bool ImportTernaryCsv(const std::string& csv, TernaryModel& model, std::string* error = nullptr);

    // A share as a person reads it: "38.1%". Display only, in the locale's
    // digits; files use FormatFloatClassic.
    std::string FormatTernaryShare(double share);

    namespace TernarySamples {
        // Three parties' shares of the vote in invented regions, at two
        // elections: a scatter in two series, anticlockwise reading.
        TernaryModel ThreePartyVotes();
        // Invented soil samples (sand, silt, clay) on the USDA layout: clay at
        // the apex, sand bottom left, silt bottom right, read clockwise.
        TernaryModel SoilSamples();
        // The USDA soil texture triangle: the twelve texture classes as
        // regions, on the same layout as SoilSamples(), no points. The class
        // outlines are derived from the class rules of the USDA Soil Survey
        // Manual (sand, loamy sand, sandy loam, loam, silt loam, silt, sandy
        // clay loam, clay loam, silty clay loam, sandy clay, silty clay, clay),
        // and listed in that order.
        TernaryModel SoilTexture();
        // A De Finetti diagram: genotype shares AA (bottom left), Aa (apex)
        // and aa (bottom right) of invented populations, sized by how many
        // were sampled, with the Hardy-Weinberg curve Aa^2 = 4 AA aa.
        TernaryModel DeFinetti();
        // Invented regions' vote shares across five elections, as paths with
        // arrowheads, on the ThreePartyVotes() axes.
        TernaryModel VoteTrajectories();
        // 2,400 invented feldspar analyses (orthoclase, albite, anorthite)
        // round three compositions, from a fixed generator: a cloud for the
        // density view, where markers would hide one another.
        TernaryModel FeldsparCloud();
    }

} // namespace UltraCanvas
