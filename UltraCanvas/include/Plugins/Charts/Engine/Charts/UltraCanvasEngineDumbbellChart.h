// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineDumbbellChart.h
// Dumbbell (connected dot) chart on the chart engine: per row, one dot per
// group joined by a connector from the lowest to the highest value - the
// gap between men and women, before and after, a minimum and a maximum.
//
// The engine port of UltraCanvasDumbbellChart, generalised from two dots to
// any number of groups. The engine supplies the axes, grid, layout, legend,
// solved value / difference labels, hover tooltips, themes and the
// animation (dots slide out from the row's middle). Horizontal projection by
// default (rows down the left edge); Vertical turns the rows into columns.
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

struct DumbbellGroup {
    std::string name;                    // "Men", "Before", "Minimum"
    Color color = Color(0, 0, 0, 0);     // alpha 0 = from the palette
    ChartMarkerShape marker = ChartMarkerShape::Circle;

    DumbbellGroup() = default;
    DumbbellGroup(std::string groupName, const Color& c,
                  ChartMarkerShape shape = ChartMarkerShape::Circle)
        : name(std::move(groupName)), color(c), marker(shape) {}
};

// One row: a value per group (NaN = no dot for that group).
struct DumbbellRow {
    std::string label;
    std::vector<double> values;
    Color connectorColor = Color(0, 0, 0, 0);   // alpha 0 = the chart's connector colour

    DumbbellRow() = default;
    DumbbellRow(std::string rowLabel, std::vector<double> rowValues)
        : label(std::move(rowLabel)), values(std::move(rowValues)) {}
};

// InsertionOrder, not None/Unsorted: X11's Xlib.h #defines both.
enum class DumbbellRowSort {
    InsertionOrder,
    LabelAscending, LabelDescending,
    FirstAscending, FirstDescending,     // by the first group's value
    LastAscending, LastDescending,       // by the last group's value
    RangeAscending, RangeDescending      // by the connector's length
};

class UltraCanvasEngineDumbbellChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineDumbbellChart(const std::string& id, int x = 0, int y = 0,
                                   int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetGroups(const std::vector<DumbbellGroup>& groups);
    void SetRows(const std::vector<DumbbellRow>& rows);
    void AddRow(const std::string& label, const std::vector<double>& values);
    void AddRow(const std::string& label, double first, double second);
    void ClearRows();
    const std::vector<DumbbellGroup>& GetGroups() const { return groups; }
    const std::vector<DumbbellRow>& GetRows() const { return rows; }

    // ---- presentation ---------------------------------------------------
    void SetSortOrder(DumbbellRowSort order);
    DumbbellRowSort GetSortOrder() const { return sortOrder; }
    // The rows in display order (indices into GetRows()).
    std::vector<size_t> DisplayOrder() const;
    void SetDotRadius(float radius);
    void SetConnectorWidth(float width);
    void SetConnectorColor(const Color& color);
    // Pin the value axis; ClearValueRange returns to automatic.
    void SetValueRange(double minValue, double maxValue);
    void ClearValueRange();
    void SetIncludeZero(bool include);
    void SetValueFormat(const std::string& prefix, const std::string& suffix,
                        bool compact = false, int decimals = -1);
    void SetValueAxisTitle(const std::string& title);
    void SetShowValueLabels(bool show);
    // The connector's length ("+15") beside its middle.
    void SetShowDifferenceLabels(bool show);
    // A band behind the hovered row.
    void SetShowRowHighlight(bool show);

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    std::vector<DumbbellGroup> groups;
    std::vector<DumbbellRow> rows;
    std::vector<bool> groupEnabled;
    DumbbellRowSort sortOrder = DumbbellRowSort::InsertionOrder;
    float dotRadius = 7.0f;
    float connectorWidth = 3.0f;
    Color connectorColor = Color(200, 200, 200, 255);
    bool manualRange = false;
    double rangeMin = 0.0, rangeMax = 100.0;
    bool includeZero = false;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;
    std::string valueAxisTitle;
    bool showValues = true;
    bool showDifferences = false;
    bool showRowHighlight = true;

    Color GroupColor(size_t group) const;
    bool GroupActive(size_t group) const;
    // The visible values of a row and its extent; false when it has none.
    bool RowExtent(const DumbbellRow& row, double& lo, double& hi) const;
    void RefreshLegend();
    std::string FormatValue(double value) const;
    std::string TooltipFor(size_t row, size_t group) const;
};

inline std::shared_ptr<UltraCanvasEngineDumbbellChart> CreateEngineDumbbellChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineDumbbellChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
