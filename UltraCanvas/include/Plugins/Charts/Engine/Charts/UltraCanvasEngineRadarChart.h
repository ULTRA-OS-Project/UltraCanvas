// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.h
// Radar (spider) chart on the chart engine - the port of
// UltraCanvasRadarChartElement.
//
// The chart describes one cyclic category axis (the spokes) and one value
// axis; the engine's Polar projection turns them into the rim labels, the
// radial value rule, the polygon web (SetPolarGridShape) and the spokes
// (SetDomainGridAxis). The content only draws the series polygons and their
// markers, mapped through the projection. Legend, toggling, tooltips, value
// labels (the solved label plan) and the grow-out animation are engine
// services.
//
// Each spoke keeps its own value range, as in the legacy chart: when every
// spoke shares one range the radial rule is labelled in it, otherwise the rule
// reads 0-100% of each spoke's range and the tooltips give the actual values.
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

// One spoke: its name and the value range it spans from centre to rim.
struct EngineRadarAxis {
    std::string name;
    double minValue = 0.0;
    double maxValue = 100.0;

    EngineRadarAxis() = default;
    EngineRadarAxis(const std::string& axisName, double lo, double hi)
        : name(axisName), minValue(lo), maxValue(hi) {}
};

// One series: a value per spoke, drawn as a closed polygon.
struct EngineRadarSeries {
    std::string name;
    std::vector<double> values;          // one per axis; missing = 0
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the theme palette's colour
    float fillOpacity = 0.3f;            // 0 = outline only
    float strokeWidth = 2.0f;
    bool showPoints = true;
    float pointRadius = 4.0f;
    ChartMarkerShape marker = ChartMarkerShape::Circle;
};

class UltraCanvasEngineRadarChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineRadarChart(const std::string& id, int x = 0, int y = 0,
                                int w = 0, int h = 0);

    // ---- axes (spokes) -----------------------------------------------------
    void AddAxis(const std::string& name, double minValue = 0.0, double maxValue = 100.0);
    void SetAxisRange(size_t axisIndex, double minValue, double maxValue);
    void ClearAxes();
    size_t GetAxisCount() const { return radarAxes.size(); }

    // ---- series ------------------------------------------------------------
    void AddSeries(const EngineRadarSeries& series);
    void AddSeries(const std::string& name, const std::vector<double>& values,
                   const Color& color = Color(0, 0, 0, 0));
    void SetSeriesStyle(size_t seriesIndex, const Color& color, float strokeWidth = 2.0f,
                        float fillOpacity = 0.3f);
    void SetSeriesPointDisplay(size_t seriesIndex, bool showPoints, float pointRadius = 4.0f,
                               ChartMarkerShape marker = ChartMarkerShape::Circle);
    void ClearSeries();
    size_t GetSeriesCount() const { return radarSeries.size(); }

    // ---- presentation ------------------------------------------------------
    // Rings and spokes on or off (the engine's grids follow the axes' ticks).
    void SetShowGrid(bool show);
    // Number of rings between centre and rim (the legacy gridLevels).
    void SetGridLevels(int levels);
    void SetShowAxisLabels(bool show);
    void SetShowValueLabels(bool show);
    // Where the first spoke points (degrees, -90 = 12 o'clock) and which way
    // the spokes run.
    void SetRadarOrientation(double startAngleDegrees, bool clockwise = true);
    // true: every series takes the theme palette, ignoring its own colour -
    // so a theme change recolours the series as well as the furniture.
    void SetUseThemePalette(bool use);

    // ---- engine contract ---------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    std::vector<EngineRadarAxis> radarAxes;
    std::vector<EngineRadarSeries> radarSeries;
    std::vector<bool> seriesHidden;
    bool showGridLines = true;
    int gridLevels = 5;
    bool showAxisLabels = true;
    bool showValues = false;
    bool useThemePalette = false;

    void RefreshLegend();
    Color SeriesColor(size_t seriesIndex) const;
    bool SharedRange(double& lo, double& hi) const;
    // A spoke's value as a fraction of its range (unclamped).
    double NormalizeOnAxis(size_t axisIndex, double value) const;
    double ValueAt(const EngineRadarSeries& series, size_t axisIndex) const;
    std::string FormatAxisValue(size_t axisIndex, double value) const;
    static int64_t RegionId(size_t seriesIndex, size_t axisIndex) {
        return static_cast<int64_t>(seriesIndex) * 4096 + static_cast<int64_t>(axisIndex);
    }
};

inline std::shared_ptr<UltraCanvasEngineRadarChart> CreateEngineRadarChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineRadarChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
