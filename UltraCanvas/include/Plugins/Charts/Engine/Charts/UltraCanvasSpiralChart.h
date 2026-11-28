// include/Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.h
// A spiral plot on the chart engine: a time series wound along a spiral, one
// turn per period, so a rhythm lines up from the centre outward. Three
// displays - a ribbon coloured by value, a band whose width is the value, a
// line that moves outward with the value - with spokes and labels for the
// parts of a period, a label at each turn's start, a colour bar for the
// ribbon and a tooltip per sample.
//
// The engine supplies the frame, title, theme, tooltips and hit regions; its
// two axes are hidden. The spiral's geometry is UltraCanvasSpiralModel's,
// and the chart draws everything on the spiral itself (as the ternary chart
// draws its own edge axes).
//
// Design: Docs/UltraCanvas/UltraCanvasSpiralChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasSpiralChart.md
//
// Version: 1.0.0 - Phase 1
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralModel.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class SpiralDisplay {
    Ribbon,   // a piece of ribbon per sample, coloured by its value
    Band,     // a filled band from the ring's inner edge, its width the value
    Line      // a line that moves outward from the ring's inner edge with the value
};

class UltraCanvasSpiralChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasSpiralChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    // Takes the model's title as the chart title and draws its subtitle.
    void SetModel(const UltraCanvasSpiralModel& model);
    const UltraCanvasSpiralModel& GetModel() const { return model; }
    // Rewinds the spiral to a new period (the rings and angles change).
    void SetPeriod(double period);

    // ---- look -----------------------------------------------------------
    void SetDisplay(SpiralDisplay display);
    SpiralDisplay GetDisplay() const { return display; }
    // The ribbon's colours, low to high (reversed: high to low).
    void SetColormap(HeatmapColormap colormap, bool reversed = false);
    // The band's and the line's colour; alpha 0 = the palette's first.
    void SetColor(const Color& color);
    // How much of its ring the ribbon fills, 0.2..1.
    void SetRibbonWidth(float share);
    // The spiral's centre line as a faint guide, and the spokes.
    void SetShowGuide(bool show);
    // The colour bar under the spiral (ribbon only).
    void SetShowColorBar(bool show);
    // How a time and a value read in the tooltip and the colour bar.
    void SetTimeFormatter(ValueFormatter formatter);
    void SetValueDecimals(int decimals);

    // ---- reading ----------------------------------------------------------
    // The sample under the pointer, -1 when none.
    int GetHoveredSample() const { return hoveredSample; }
    // The tooltip text of a sample.
    std::string DescribeSample(size_t index) const;
    // The colour the ribbon gives a value.
    Color ValueColor(double value) const;
    // A place on the spiral (time, offset in ring widths) on screen, as of
    // the last render.
    Point2Dd ScreenPoint(double t, double offset = 0.0) const;
    // The spiral's centre and outer radius on screen, as of the last render.
    Point2Dd Centre() const { return centre; }
    double OuterRadius() const { return outerRadius; }
    // The pointer entered a sample or left them all (-1).
    std::function<void(int index)> onSampleHover;

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnHitRegionHoverChanged(int64_t regionId) override;

protected:
    void RenderEngineTitle(IRenderContext* ctx) override;

private:
    UltraCanvasSpiralModel model;
    SpiralDisplay display = SpiralDisplay::Ribbon;
    HeatmapColormap colormap = HeatmapColormap::Viridis;
    bool colormapReversed = false;
    Color color = Color(0, 0, 0, 0);
    float ribbonWidth = 0.9f;
    bool showGuide = true;
    bool showColorBar = true;
    ValueFormatter timeFormatter;
    int valueDecimals = 1;
    int hoveredSample = -1;
    Point2Dd centre;
    double outerRadius = 0.0;

    Color MainColor() const;
    std::string FormatTime(double t) const;
    std::string FormatValue(double v) const;
    // The outline of the piece of ring from t0 to t1 between two offsets.
    std::vector<Point2Dd> Piece(double t0, double t1, double inner, double outer) const;
    double SubtitleHeight(IRenderContext* ctx) const;
    bool ColorBarShown() const { return showColorBar && display == SpiralDisplay::Ribbon; }
};

inline std::shared_ptr<UltraCanvasSpiralChart> CreateSpiralChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasSpiralChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
