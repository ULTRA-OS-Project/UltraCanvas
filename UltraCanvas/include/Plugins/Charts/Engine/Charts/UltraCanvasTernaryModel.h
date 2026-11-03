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
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"   // ChartMarkerShape
#include <string>
#include <vector>

namespace UltraCanvas {

    // One composition, as given: any non-negative amounts.
    struct TernaryPoint {
        double a = 0.0, b = 0.0, c = 0.0;
        std::string label;
        std::string note;                 // tooltip only

        TernaryPoint() = default;
        TernaryPoint(double pa, double pb, double pc, std::string pointLabel = std::string())
            : a(pa), b(pb), c(pc), label(std::move(pointLabel)) {}

        // a / (a + b + c) etc. False - and the shares left alone - for a
        // negative or non-finite part, or a sum of 0.
        bool Shares(double& sa, double& sb, double& sc) const;
        double Sum() const { return a + b + c; }
    };

    // A group of points with one colour, marker and legend entry.
    struct TernarySeries {
        std::string name;
        std::vector<TernaryPoint> points;
        Color color = Color(0, 0, 0, 0);          // transparent = from the palette
        ChartMarkerShape marker = ChartMarkerShape::Circle;
        float markerRadius = 4.5f;

        TernarySeries() = default;
        explicit TernarySeries(std::string seriesName) : name(std::move(seriesName)) {}
    };

    // One part: the corner that is 100 % of it.
    struct TernaryAxis {
        std::string title;                // "Clay"; drawn at its corner and along its edge
        std::string unit = "%";           // after the tick numbers in the edge title
        double tickStep = 0.1;            // as a share: 0.1 = every 10 %

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

        // The corner whose labels run up to the given corner's, i.e. where
        // the axis of `corner` reads 0 % on its labelled edge. Clockwise:
        // A from B, B from C, C from A.
        int LabelEdgeStart(int corner) const;

        // The shares at which the axis has a tick: 0, step, 2 step ... 1.
        std::vector<double> Ticks(int corner) const;

        size_t PointCount() const;

        // Problems that make the model unusable or misleading: an axis
        // without a title, a tick step outside (0, 0.5], a point with a
        // negative or non-finite part or a sum of 0. Empty = valid.
        std::vector<std::string> Validate() const;
    };

    // A share as a person reads it: "38.1%". Display only, in the locale's
    // digits; files use FormatFloatClassic.
    std::string FormatTernaryShare(double share);

    namespace TernarySamples {
        // Three parties' shares of the vote in invented regions, at two
        // elections: a scatter in two series, anticlockwise reading.
        TernaryModel ThreePartyVotes();
        // Invented soil samples (sand, silt, clay) on the USDA layout: clay at
        // the apex, sand bottom left, silt bottom right, read clockwise. Phase 1
        // draws the points only; the texture classes are Phase 2.
        TernaryModel SoilSamples();
    }

} // namespace UltraCanvas
