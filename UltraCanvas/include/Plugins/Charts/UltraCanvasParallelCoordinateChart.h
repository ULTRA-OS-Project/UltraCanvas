// include/Plugins/Charts/UltraCanvasParallelCoordinateChart.h
// Parallel coordinate chart element - the chart engine's first native client.
//
// Implements only phase 2 of the engine's three-phase render (the polylines)
// plus the content descriptors; axes, endpoint labels, grid, legend, label
// plan and the clipping discipline come from UltraCanvasChartEngineElement.
// The data lives in the unit-tested ParallelAxisModel.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include "Plugins/Charts/UltraCanvasParallelAxisModel.h"
#include <functional>
#include <map>
#include <set>

namespace UltraCanvas {

enum class PCPColorMode {
    ByGroup,       // categorical palette / user map keyed on PCPRecord::group
    ByValue,       // continuous colormap over one chosen dimension
    PerRecord,     // PCPRecord::colorOverride
    Single         // one colour for every line
};

enum class PCPLineMode { Straight, Curved };

class UltraCanvasParallelCoordinateChartElement : public UltraCanvasChartEngineElement {
public:
    UltraCanvasParallelCoordinateChartElement(const std::string& id,
                                              int x, int y, int w, int h);

    // ---- data (thin wrappers over the model) ------------------------------
    void AddDimension(const std::string& name, const std::vector<double>& column);
    void SetRecords(std::vector<PCPRecord> records);
    void SetRecordGroups(const std::vector<std::string>& groups);
    void ClearData();
    ParallelAxisModel& Model() { return model; }
    const ParallelAxisModel& Model() const { return model; }

    // ---- normalisation ----------------------------------------------------
    void SetNormalizationMode(PCPNormalization mode);
    void SetStandardize(PCPStandardize mode);
    void SetAxisInverted(const std::string& dimension, bool inverted);

    // ---- lines ------------------------------------------------------------
    void SetLineMode(PCPLineMode mode);
    void SetCurveTension(float tension);          // 0..1, Catmull-Rom alpha
    void SetLineWidth(float width);
    void SetLineAlpha(float alpha);               // 0 = automatic from record count
    void SetShowVertexMarkers(bool show, float radius = 3.0f);
    void SetContextStyle(const Color& color);     // filtered-out records
    void SetShowContext(bool show);               // false = hide instead of dim

    // ---- colour -----------------------------------------------------------
    void SetColorMode(PCPColorMode mode);
    void SetSingleColor(const Color& color);
    void SetGroupColors(const std::map<std::string, Color>& colors);
    void SetColorDimension(const std::string& dimension);   // ByValue source
    void SetColormap(HeatmapColormap map, bool reverse = false);

    // ---- axes decorations -------------------------------------------------
    void SetShowAxisEndpointLabels(bool show);

    // ---- brushing / selection --------------------------------------------
    void SetEnableBrushing(bool enable) { brushingEnabled = enable; }
    void AddBrush(const std::string& dimension, double low, double high);
    void ClearBrushes();
    void PinRecord(size_t recordIndex, bool pinned);
    void ClearPins() { pinnedRecords.clear(); MarkEngineDirty(ChartDirty::Selection); }

    // ---- callbacks --------------------------------------------------------
    std::function<void(size_t passingCount)> onBrushChanged;
    std::function<void(int recordIndex)> onRecordClick;
    std::function<void(const std::vector<std::string>& order)> onAxisOrderChanged;

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnEnginePropertyChanged(const std::string& key, const UCPropertyValue& value) override;
    bool OnEvent(const UCEvent& event) override;

private:
    ParallelAxisModel model;

    PCPLineMode lineMode = PCPLineMode::Straight;
    float curveTension = 0.5f;
    float lineWidth = 1.5f;
    float lineAlpha = 0.0f;                 // 0 = auto
    bool showVertexMarkers = false;
    float vertexRadius = 3.0f;
    Color contextColor = Color(190, 190, 195, 60);
    bool showContext = true;

    PCPColorMode colorMode = PCPColorMode::ByGroup;
    Color singleColor = Color(70, 110, 190, 255);
    std::map<std::string, Color> groupColors;
    std::string colorDimensionName;
    HeatmapColormap colormap = HeatmapColormap::Viridis;
    bool colormapReversed = false;

    bool endpointLabels = true;
    bool brushingEnabled = true;

    std::set<size_t> pinnedRecords;
    int hoveredRecord = -1;

    // Active brush drag: display slot being brushed, in normalised v.
    int brushDragSlot = -1;
    double brushDragStartV = 0.0, brushDragCurrentV = 0.0;
    // Active axis header drag (reorder).
    int headerDragSlot = -1;
    double headerDragU = 0.0;

    // Display-order snapshot used while rendering / hit testing.
    std::vector<size_t> displayOrder;

    uint8_t EffectiveAlpha() const;
    Color RecordColor(size_t record) const;
    Color FallbackGroupColor(size_t groupIndex) const;
    void RebuildLegend();
    void EmitAxisOrderChanged();
    // Screen -> normalised chart space through the frame's projection.
    bool ToNormalized(const Point2Di& mousePos, double& u, double& v) const;
    int SlotNearU(double u, double tolerance) const;
    void BuildRecordPath(size_t record, const ChartEngineFrame& frame,
                         std::vector<Point2Dd>& path) const;
    void StrokeRecord(IRenderContext* ctx, const ChartEngineFrame& frame,
                      size_t record, const Color& color, float width) const;
};

// Factory helper, matching the framework convention.
std::shared_ptr<UltraCanvasParallelCoordinateChartElement>
CreateParallelCoordinateChartElement(const std::string& id, int x, int y, int w, int h);

// The element descriptor, shared by the bundled registration below and the
// runtime module's plugin init (which routes it through the host vtable).
struct UCElementDescriptor;
UCElementDescriptor MakeParallelCoordinateChartDescriptor();

// Registers "parallel-coordinate-chart" with UltraCanvasElementRegistry.
// Explicit call, never a static initialiser (see the element plugin plan).
void RegisterParallelCoordinateChartElement();

} // namespace UltraCanvas
