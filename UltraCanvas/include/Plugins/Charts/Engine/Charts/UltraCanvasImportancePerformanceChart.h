// include/Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h
// The importance-performance chart (Slack 1994; Martilla & James 1977): the
// factors of a product or service plotted by how much customers care about
// them and how they compare with competitors, over authored zones that name
// the action - "urgent action", "improve", "appropriate", "excess?".
//
// Built on UltraCanvasEngineScatterChart: the factors are its points, so
// groups, markers, point labels, the legend and hover come from there. This
// class adds the zones (polygon highlights under the grid), their titles and
// icons, the labelled boundary bands, the named level ranges on both axes,
// and the zone of every factor in its tooltip. The data is an
// ImportancePerformanceModel (UltraCanvasImportancePerformanceModel.h).
//
// Axis indices follow the scatter chart: 0 = performance (y), 1 = importance (x).
//
// Design: Docs/UltraCanvas/UltraCanvasImportancePerformanceChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasImportancePerformanceChart.md
//
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceModel.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasImportancePerformanceChart : public UltraCanvasEngineScatterChart {
public:
    UltraCanvasImportancePerformanceChart(const std::string& id, int x, int y, int w, int h);

    // Replaces everything: axes, zones, bands and factors. The factors become
    // one scatter group per distinct `group`, in order of first appearance.
    void SetModel(const ImportancePerformanceModel& model);
    const ImportancePerformanceModel& GetModel() const { return model; }

    // The zone index of each factor, as ImportancePerformanceModel::ClassifyFactors.
    const std::vector<int>& GetFactorZones() const { return factorZones; }

    void SetShowZoneTitles(bool show);
    void SetShowZoneIcons(bool show);
    void SetShowBandLabels(bool show);
    // Level names at the centre of their ranges, tick marks at the range
    // boundaries and the low / high captions at the axis ends. Off = plain
    // numeric ticks.
    void SetShowLevelRanges(bool show);
    void SetShowGrid(bool show);            // off by default: the zones are the grid
    void SetZoneTitleFontSize(float size);
    // The box behind each factor name; alpha 0 = none.
    void SetFactorLabelBackground(const Color& color);

    // Marker colours and shapes per group, cycled. The defaults are dark
    // marks with a white outline, which read on every zone colour, and a
    // different shape per group so the groups do not rely on colour alone.
    void SetGroupStyles(const std::vector<Color>& colors, const std::vector<ChartMarkerShape>& shapes);

    // ---- engine contract (each calls the scatter chart's first) --------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

protected:
    void AddChartHighlights() override;
    // Factor names sit on saturated zone colours: each gets a light box
    // behind it before the engine draws the text.
    void RenderPlannedLabels(IRenderContext* ctx) override;

private:
    struct ZoneDecor {
        bool showTitle = false;
        bool showIcon = false;
        Point2Dd centre;
        Rect2Dd titleBox;
        Rect2Dd iconBox;
    };
    struct BandDecor {
        bool show = false;
        bool inside = true;          // text in the band, or beside a thin one
        Point2Dd centre;
        double angle = 0.0;          // radians, never upside down
        Size2Dd textSize;
        float fontSize = 11.0f;
        Rect2Dd bounds;              // axis-aligned box of the rotated text
    };
    struct Decor {
        Rect2Dd plot;
        uint64_t modelGeneration = 0;
        std::vector<ZoneDecor> zones;
        std::vector<BandDecor> bands;
    };

    ImportancePerformanceModel model;
    uint64_t modelGeneration = 1;
    std::vector<int> factorZones;
    // Where each factor went: (group, index within the group).
    std::vector<std::pair<size_t, size_t>> factorSlots;
    std::vector<std::string> groupNames;
    std::vector<bool> groupHidden;

    bool showZoneTitles = true;
    bool showZoneIcons = true;
    bool showBandLabels = true;
    bool showLevelRanges = true;
    bool showGrid = false;
    float zoneTitleFontSize = 14.0f;
    Color factorLabelBackground = Color(255, 255, 255, 215);
    std::vector<Color> groupColors;
    std::vector<ChartMarkerShape> groupShapes;

    Decor decor;

    void RebuildGroups();
    void ConfigureAxis(ChartAxis& axis, const ImportancePerformanceAxis& spec) const;
    Point2Dd ToScreen(const ChartEngineFrame& frame, const Point2Dd& value) const;
    std::vector<Point2Dd> ScreenShape(const ChartEngineFrame& frame, const std::vector<Point2Dd>& value) const;
    double BandThickness(const ChartEngineFrame& frame, const ImportancePerformanceBand& band) const;
    const Decor& Layout(IRenderContext* ctx, const ChartEngineFrame& frame);
    Color ZoneFill(size_t zoneIndex) const;
    std::string FactorTooltip(size_t factorIndex) const;
    std::string ZoneTooltip(size_t zoneIndex) const;
    std::string LevelText(const ImportancePerformanceAxis& axis, double value) const;
};

inline std::shared_ptr<UltraCanvasImportancePerformanceChart> CreateImportancePerformanceChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasImportancePerformanceChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
