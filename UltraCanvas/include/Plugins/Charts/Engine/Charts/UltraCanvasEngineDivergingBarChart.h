// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineDivergingBarChart.h
// Diverging bar chart on the chart engine: rows of bars that grow left and
// right of a shared zero line. One content class covers four styles:
//   - Likert          stacked answer shares per row, disagreement left and
//                     agreement right, the neutral share split across zero
//   - PopulationPyramid  stacked amounts per side on a common, symmetric axis;
//                     with one category per side it is the classic age/sex
//                     pyramid, with surplus highlighting and average lines
//   - Tornado         one net bar per row (right total minus left total)
//   - OpposingBars    the two side totals as half-height bars per row
//
// The engine port of UltraCanvasDivergingBarChart and UltraCanvasPopulationChart.
// Both sides share one honest value scale (ChartAxis::symmetric) and read as
// magnitudes (ChartAxis::absoluteLabels); the legacy charts scaled each side
// to its own maximum. The engine supplies the axes, grid, layout, legend,
// label plan, tooltips, themes and grow-in animation. Horizontal projection
// by default; Vertical turns the rows into columns.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class DivergingBarStyle { Likert, PopulationPyramid, Tornado, OpposingBars };

// Which side of zero a category's segments grow on. Neutral is split across
// zero under Likert (SetSplitNeutral) and sits on the negative side otherwise.
enum class DivergingSide { Negative, Neutral, Positive };

struct DivergingBarCategory {
    std::string name;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = from the palette
    DivergingSide side = DivergingSide::Positive;

    DivergingBarCategory() = default;
    DivergingBarCategory(std::string categoryName, const Color& c, DivergingSide s)
        : name(std::move(categoryName)), color(c), side(s) {}
};

// One row: a value per category, in category order (missing = 0). Values
// are amounts - the side decides the direction, the sign is ignored.
struct DivergingBarRow {
    std::string label;
    std::vector<double> values;

    DivergingBarRow() = default;
    DivergingBarRow(std::string rowLabel, std::vector<double> rowValues)
        : label(std::move(rowLabel)), values(std::move(rowValues)) {}
};

// A population pyramid's age group.
struct DivergingPopulationGroup {
    std::string label;      // "0-4", "85-89", "100+" - parsed for average ages
    double left = 0.0;      // e.g. males
    double right = 0.0;     // e.g. females
};

enum class DivergingRowOrder {
    TopDown,    // first row at the top (survey questions)
    BottomUp    // first row at the bottom (age groups, youngest first)
};

class UltraCanvasEngineDivergingBarChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineDivergingBarChart(const std::string& id, int x = 0, int y = 0,
                                       int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetCategories(const std::vector<DivergingBarCategory>& categories);
    void SetRows(const std::vector<DivergingBarRow>& rows);
    // Adds a row; a label that is already a row replaces its values.
    void AddRow(const std::string& label, const std::vector<double>& values);
    void ClearRows();
    const std::vector<DivergingBarCategory>& GetCategories() const { return categories; }
    const std::vector<DivergingBarRow>& GetRows() const { return rows; }

    // Population pyramid in one call: two categories (left / right side), one
    // row per group, rows bottom-up, the PopulationPyramid style.
    void SetPopulationData(const std::vector<DivergingPopulationGroup>& groups,
                           const std::string& leftName, const Color& leftColor,
                           const std::string& rightName, const Color& rightColor);

    // ---- style ----------------------------------------------------------
    void SetStyle(DivergingBarStyle style);
    DivergingBarStyle GetStyle() const { return style; }
    void SetRowOrder(DivergingRowOrder order);
    // Fraction of a row the bar fills (0.2 .. 1).
    void SetBarFill(double fill);
    // Likert: the neutral share straddles zero, half on each side.
    void SetSplitNeutral(bool split);
    // Likert: shares of the row total (default) or the raw amounts.
    void SetPercentOfRow(bool percent);
    // Tornado: rows sorted by the size of their net bar, largest first.
    void SetSortByMagnitude(bool sort);
    // Tornado / OpposingBars: the colours of the two sides.
    void SetSideColors(const Color& negative, const Color& positive);
    // Population pyramid with one category per side: the part of the longer
    // bar beyond the shorter one in a darker shade of its colour.
    void SetHighlightSurplus(bool highlight);
    // Population pyramid: a dashed line per side at the population-weighted
    // average row - an average age when the labels parse as age ranges.
    void SetShowAverageLines(bool show);
    void SetShowCenterLine(bool show);
    // Amounts inside the segments (Likert, pyramid) or past the bar ends.
    void SetShowValueLabels(bool show);
    void SetValueFormat(const std::string& prefix, const std::string& suffix,
                        bool compact = false, int decimals = -1);
    // Pin the value axis to [-limit, limit]; 0 = automatic.
    void SetValueLimit(double limit);
    void SetValueAxisTitle(const std::string& title);

    // The population-weighted average of one side (true = negative side) in
    // row units: an average age when the row labels parse as age ranges
    // ("20-24", "100+"), else the average row index. NaN without data.
    double AverageOfSide(bool negativeSide, bool* inAgeUnits = nullptr) const;

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    // One drawn segment in value space.
    struct Segment {
        size_t row = 0;
        size_t category = SIZE_MAX;    // SIZE_MAX = a side total (tornado / opposing)
        int side = 1;                  // -1 left of zero, +1 right
        double from = 0.0, to = 0.0;   // signed value extent
        double amount = 0.0;           // what the segment represents
        double offset = 0.0;           // slot offset (-0.5..0.5) for half-height bars
        double fill = 1.0;             // fraction of the bar thickness
        bool surplus = false;
        Color color;
    };

    std::vector<DivergingBarCategory> categories;
    std::vector<DivergingBarRow> rows;
    std::vector<bool> categoryEnabled;
    DivergingBarStyle style = DivergingBarStyle::Likert;
    DivergingRowOrder rowOrder = DivergingRowOrder::TopDown;
    double barFill = 0.8;
    bool splitNeutral = true;
    bool percentOfRow = true;
    bool sortByMagnitude = false;
    Color negativeColor = Color(200, 100, 100, 255);
    Color positiveColor = Color(100, 150, 200, 255);
    bool highlightSurplus = true;
    bool showAverageLines = true;
    bool showCenterLine = true;
    bool showValues = false;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;
    double valueLimit = 0.0;
    std::string valueAxisTitle;

    std::vector<Segment> BuildSegments() const;
    // Display order of the rows (sorting); index into rows.
    std::vector<size_t> RowOrder() const;
    Color CategoryColor(size_t category) const;
    bool CategoryActive(size_t category) const;
    bool IsPopulationPair() const;
    void RefreshLegend();
    void RefreshLimiters();
    std::string FormatAmount(double value) const;
    std::string TooltipFor(const Segment& segment) const;
    // Row slot of display position `slot` -> domain extent of a segment.
    void SegmentSpan(const ChartAxis& rowAxis, size_t slot, const Segment& segment,
                     double& u0, double& u1) const;
};

inline std::shared_ptr<UltraCanvasEngineDivergingBarChart> CreateEngineDivergingBarChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineDivergingBarChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
