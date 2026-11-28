// include/Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h
// The UI-free model of a spiral plot: a time series wound along a spiral,
// one turn per period (a day, a week, a year), so times one period apart
// land on the same ray one ring further out and a rhythm lines up. The model
// holds the samples and the period and maps a time to its place on the
// spiral; UltraCanvasSpiralChart draws it.
//
// Geometry, in a unit circle: with n rings and an inner radius r0, each ring
// is w = (1 - r0) / (n + 1) wide. A time u = (t - start) / period turns from
// the start has its centre line at radius r0 + w (u + 0.5) and its angle
// startAngle -/+ 2 pi u - an Archimedean spiral, evenly spaced rings.
//
// Design: Docs/UltraCanvas/UltraCanvasSpiralChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasSpiralChart.md
//
// Version: 1.0.0 - Phase 1: samples, period, geometry, value range, the
//                  DailyTemperature and HourlyVisits samples
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

struct SpiralSample {
    double t = 0.0;                  // time, in any unit; increasing
    double value = 0.0;              // NaN = missing (a gap)
    std::string note;                // an extra tooltip line
};

// A place on the spiral in a unit circle.
struct SpiralPosition {
    double angle = 0.0;              // radians, 0 = 3 o'clock, counter-clockwise (y up)
    double radius = 0.0;             // 0..1 of the spiral's outer radius
};

struct UltraCanvasSpiralModel {
    std::string title;
    std::string subtitle;
    std::string valueName = "Value";            // for the tooltip and the colour bar
    std::vector<SpiralSample> samples;
    double period = 1.0;                        // time units per turn
    std::optional<double> start;                // time at the first turn's start (unset = first sample)
    std::vector<std::string> sectorLabels;      // evenly around a turn: Jan..Dec, Mon..Sun
    std::vector<std::string> turnLabels;        // at each turn's start: 2021, 2022, ...
    std::optional<double> valueMin, valueMax;   // unset = the data's range
    double innerRadius = 0.18;                  // the empty middle, 0..0.6 of the radius
    double startAngle = 90.0;                   // degrees where a turn starts (90 = 12 o'clock)
    bool clockwise = true;

    // The time the first turn starts at.
    double Start() const;
    // How many turns from the start a time is: (t - start) / period.
    double TurnsOf(double t) const;
    // The rings the samples need (at least 1).
    int TurnCount() const;
    // A ring's width, in units of the outer radius.
    double RingWidth() const;
    // Where a time is on the spiral: its centre line, moved outward by
    // `offset` ring widths (-0.5 = the ring's inner edge, 0.5 its outer).
    SpiralPosition Position(double t, double offset = 0.0) const;
    // The value range the colours and widths are scaled to.
    std::pair<double, double> ValueRange() const;
    // A value as 0..1 of the range (clamped); NaN stays NaN.
    double Normalize(double value) const;
    // The time sample i covers: to the next sample, or one typical step for
    // the last and where the next is more than three typical steps away.
    double SampleSpan(size_t i) const;
    // The typical step between samples (their median).
    double TypicalStep() const;
    // The sample nearest a time (SIZE_MAX without samples).
    size_t NearestSample(double t) const;
    // Problems that keep the model from drawing as meant; empty = fine.
    std::vector<std::string> Validate() const;

    // ---- samples (invented data) -------------------------------------------
    // Daily temperatures over three years of 365 days, one turn per year,
    // month names around and the years at each turn's start.
    static UltraCanvasSpiralModel DailyTemperature();
    // Website visits per hour over four weeks, one turn per week starting on
    // Monday 00:00, day names around: busy weekday working hours, quiet
    // nights and weekends, and a sale on the third Thursday.
    static UltraCanvasSpiralModel HourlyVisits();
};

} // namespace UltraCanvas
