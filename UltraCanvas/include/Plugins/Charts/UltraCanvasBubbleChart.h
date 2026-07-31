// include/Plugins/Charts/UltraCanvasBubbleChart.h
// Comprehensive bubble chart element: XY scatter bubbles, packed bubbles and
// categorical bubble matrix, with area/diameter size encoding, palette /
// category / colormap colour encoding, nested size legend, in-bubble and
// outside labels, tooltips, hover highlight and click callbacks.
// Version: 1.1.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <functional>

namespace UltraCanvas {

// =============================================================================
// BUBBLE CHART ENUMERATIONS
// =============================================================================

// The classic bubble chart families:
//   ScatterBubbles     - scatter plot whose marks carry a third (size) and
//                        optional fourth (colour) dimension ("Concerns when
//                        traveling").
//   PackedBubbles      - axis-free Tableau-style packed bubble cloud / circle
//                        packing; position is meaningless, size and colour
//                        carry the values. Optionally drawn inside an
//                        enclosure circle (SetPackedEnclosure).
//   BubbleMatrix       - categorical rows x columns grid of proportionally
//                        sized bubbles ("Mrs. President" style survey charts).
//   HierarchicalPacked - two-level circle packing: bubbles sharing a category
//                        are packed inside a tinted parent circle labelled
//                        with the group name, and the parent circles are
//                        packed against each other (d3-style hierarchy).
//   TimelineBubbles    - named categorical rows on the Y axis, a continuous
//                        (usually time) value on X, bubble area = value
//                        (Tableau "orders per month" style).
enum class BubbleChartMode {
    ScatterBubbles,
    PackedBubbles,
    BubbleMatrix,
    HierarchicalPacked,
    TimelineBubbles
};

// How the size value maps to the drawn circle.
//   Area     - value proportional to circle AREA (perceptually correct,
//              recommended best practice; default).
//   Diameter - value proportional to the diameter (exaggerates differences).
enum class BubbleSizeScale {
    Area,
    Diameter
};

// How each bubble's fill colour is chosen (a per-point override colour always
// wins over the mode).
enum class BubbleColorMode {
    Uniform,          // every bubble uses the uniform colour
    Palette,          // cycle through the palette by bubble index
    CategoryMap,      // look up the bubble's category in the category map
    ColormapByValue,  // continuous colormap over BubbleDataPoint::colorValue
    ColormapBySize    // continuous colormap over BubbleDataPoint::size
};

// Visual finish of the circles.
enum class BubbleRenderStyle {
    Flat,     // solid fill
    Shaded,   // radial gradient - light top-left, darker rim (soft 3D)
    Glossy    // Shaded plus a white specular highlight
};

// Placement of the bubble's name label.
enum class BubbleNameLabelMode {
    Hidden,
    Inside,       // centred in the bubble (contrast colour picked automatically)
    BelowBubble,  // under the bubble (renamed from Below - X11 macro collision)
    Auto          // Inside when it fits, otherwise BelowBubble
};

// Placement of the bubble's numeric value label.
enum class BubbleValueLabelMode {
    Hidden,
    Inside,       // centred (or under the name when the name is also inside)
    Auto          // Inside when the bubble is large enough, otherwise hidden
};

// Placement order for the packed layout.
enum class PackedSortOrder {
    SizeDescending,   // biggest first, ends up centred (default, Tableau-like)
    SizeAscending,
    DataOrder         // keep insertion order
};

// =============================================================================
// BUBBLE DATA POINT
// =============================================================================

struct BubbleDataPoint {
    double x = 0.0;              // X position (ScatterBubbles)
    double y = 0.0;              // Y position (ScatterBubbles)
    double size = 0.0;           // third dimension -> circle area/diameter
    double colorValue = 0.0;     // optional fourth dimension -> colormap
    std::string name;            // display name ("Egypt", "Under 45", ...)
    std::string category;        // grouping key for CategoryMap colouring
    Color overrideColor = Colors::Transparent;  // per-point colour override
    int row = -1;                // BubbleMatrix row index (set internally)
    int col = -1;                // BubbleMatrix column index (set internally)

    BubbleDataPoint() = default;
    BubbleDataPoint(double x_, double y_, double size_, double colorValue_ = 0.0,
                    const std::string& name_ = "", const std::string& category_ = "",
                    const Color& override_ = Colors::Transparent)
        : x(x_), y(y_), size(size_), colorValue(colorValue_),
          name(name_), category(category_), overrideColor(override_) {}
};

// =============================================================================
// BUBBLE CHART ELEMENT
// =============================================================================

class UltraCanvasBubbleChartElement : public UltraCanvasChartElementBase {
public:
    UltraCanvasBubbleChartElement(const std::string& id, int x, int y, int width, int height);

    // ===== MODE =====
    void SetMode(BubbleChartMode mode);
    BubbleChartMode GetMode() const { return mode; }

    // ===== DATA (SCATTER / PACKED) =====
    void AddBubble(double x, double y, double size, double colorValue = 0.0,
                   const std::string& name = "", const std::string& category = "");
    void AddBubble(const BubbleDataPoint& bubble);

    // ===== DATA (MATRIX) =====
    // Define the grid first, then fill the cells. Values are the bubble sizes.
    void SetMatrixLayout(const std::vector<std::string>& rowNames,
                         const std::vector<std::string>& columnNames);
    void AddMatrixBubble(const std::string& rowName, const std::string& columnName,
                         double value, const Color& overrideColor = Colors::Transparent);

    void ClearBubbles();
    size_t GetBubbleCount() const { return bubbles.size(); }
    const BubbleDataPoint& GetBubble(size_t index) const { return bubbles[index]; }

    // ===== SIZE ENCODING =====
    // Pixel radius range used by Scatter mode (Packed and Matrix derive the
    // maximum radius from the available space).
    void SetRadiusRange(float minRadius, float maxRadius);
    void SetSizeScale(BubbleSizeScale scale);
    // Explicit size domain; when unset the domain is [min size, max size] of
    // the data (Matrix mode: [0, max value]).
    void SetSizeDomain(double minValue, double maxValue);
    void SetAutoSizeDomain();

    // ===== COLOR ENCODING =====
    void SetColorMode(BubbleColorMode mode);
    void SetUniformColor(const Color& color);
    void SetPalette(const std::vector<Color>& colors);
    void SetCategoryColorMap(const std::map<std::string, Color>& colorMap);
    void SetColormap(HeatmapColormap map, bool reverse = false);
    void SetCustomColormap(const std::vector<Color>& anchors);
    // Explicit colormap domain; when unset it spans the data's colour values.
    void SetColorDomain(double minValue, double maxValue);
    void SetAutoColorDomain();
    void SetBubbleAlpha(float alpha);   // 0..1 multiplier on the fill alpha

    // ===== STYLE =====
    void SetBubbleStyle(BubbleRenderStyle style);
    void SetBubbleBorder(const Color& color, float width);
    void SetHighlightOnHover(bool enable);

    // ===== LABELS =====
    void SetNameLabelMode(BubbleNameLabelMode mode);
    void SetValueLabelMode(BubbleValueLabelMode mode);
    void SetLabelFontSize(float size);
    void SetNameLabelColor(const Color& color);    // used for Below labels
    void SetValueDecimals(int decimals);
    void SetValueSuffix(const std::string& suffix);   // e.g. "%"
    // Bubbles smaller than this radius never get inside labels (Auto modes).
    void SetMinRadiusForInsideLabels(float radius);
    // Bubbles smaller than this radius get no name label at all (0 = off) -
    // avoids label collisions between neighbouring tiny bubbles.
    void SetHideNamesBelowRadius(float radius);

    // ===== MATRIX SPECIFICS =====
    // Colour per row name (e.g. "Men" -> purple). CategoryMap colours by
    // column name instead; a row colour wins over the column category colour.
    void SetRowColorMap(const std::map<std::string, Color>& colorMap);
    void SetMatrixHeaderStyle(float fontSize, const Color& color, bool bold);
    void SetMatrixRowLabelStyle(float fontSize, const Color& color);

    // ===== PACKED SPECIFICS =====
    void SetPackedSortOrder(PackedSortOrder order);
    void SetPackedPadding(float pixels);   // gap kept between packed circles
    // Draw an outlined enclosure circle around the packed cluster and scale
    // the packing to fill it (circle-packing-in-a-circle look).
    void SetPackedEnclosure(bool show,
                            const Color& strokeColor = Color(55, 65, 90, 255),
                            float strokeWidth = 1.5f,
                            const Color& fillColor = Colors::Transparent);

    // ===== HIERARCHICAL PACKING SPECIFICS =====
    // Bubbles are grouped by their `category`; each group becomes a parent
    // circle. Parent fill = group colour lightened by the tint factor.
    void SetGroupPadding(float pixels);        // gap between children and rim
    void SetGroupTint(float lightenFactor);    // 0..1, parent fill tint
    // Transparent colour = automatic contrast against the parent fill.
    void SetGroupLabelStyle(float fontSize, const Color& color, bool bold);

    // ===== TIMELINE SPECIFICS =====
    // Optional explicit row order; otherwise rows appear in insertion order.
    void SetTimelineRows(const std::vector<std::string>& rows);
    void AddTimelineBubble(const std::string& rowName, double xValue,
                           double size, double colorValue = 0.0);
    // Formats the X tick labels (e.g. month index -> "June 2014").
    void SetXAxisLabelFormatter(std::function<std::string(double)> formatter);

    // ===== SIZE LEGEND / ANNOTATION =====
    // Nested-circles size legend (largest at the back), drawn bottom-right.
    void SetShowSizeLegend(bool show);
    void SetSizeLegendTitle(const std::string& title);
    // Explicit legend values; empty = automatic (max / mid / min).
    void SetSizeLegendValues(const std::vector<double>& values);
    void SetFootnote(const std::string& text);   // small caption under the plot

    // ===== INTERACTION =====
    std::function<void(size_t index, const BubbleDataPoint& bubble)> onBubbleClick;

    // ===== OVERRIDES =====
    void RenderChart(IRenderContext* ctx) override;
    bool HandleChartMouseMove(const Point2Di& mousePos) override;
    bool OnEvent(const UCEvent& event) override;
    std::string GenerateTooltipContent(const ChartDataPoint& point, size_t index) override;

protected:
    ChartPlotArea CalculatePlotArea() override;
    ChartDataBounds CalculateDataBounds() override;
    void RenderCommonBackground(IRenderContext* ctx) override;

private:
    // One laid-out circle in element-local pixels.
    struct LayoutCircle {
        double cx = 0.0, cy = 0.0, r = 0.0;
        size_t index = 0;       // index into bubbles
    };

    // ===== CONFIGURATION =====
    BubbleChartMode mode = BubbleChartMode::ScatterBubbles;

    BubbleSizeScale sizeScale = BubbleSizeScale::Area;
    float minBubbleRadius = 6.0f;
    float maxBubbleRadius = 42.0f;
    bool autoSizeDomain = true;
    double sizeDomainMin = 0.0, sizeDomainMax = 1.0;

    BubbleColorMode colorMode = BubbleColorMode::Palette;
    Color uniformColor = Color(70, 130, 180, 255);
    std::vector<Color> palette;
    std::map<std::string, Color> categoryColors;
    HeatmapColormap colormap = HeatmapColormap::Blues;
    bool colormapReversed = false;
    std::vector<Color> customColormapAnchors;
    bool autoColorDomain = true;
    double colorDomainMin = 0.0, colorDomainMax = 1.0;
    float bubbleAlpha = 1.0f;

    BubbleRenderStyle bubbleStyle = BubbleRenderStyle::Flat;
    Color bubbleBorderColor = Colors::Transparent;
    float bubbleBorderWidth = 0.0f;
    bool highlightOnHover = true;

    BubbleNameLabelMode nameLabelMode = BubbleNameLabelMode::Auto;
    BubbleValueLabelMode bubbleValueLabelMode = BubbleValueLabelMode::Auto;
    float labelFontSize = 11.0f;
    Color nameLabelColor = Color(70, 70, 70, 255);
    int valueDecimals = 0;
    std::string valueSuffix;
    float minInsideLabelRadius = 12.0f;
    float hideNamesBelowRadius = 0.0f;

    std::vector<std::string> matrixRows;
    std::vector<std::string> matrixColumns;
    std::map<std::string, Color> rowColors;
    float matrixHeaderFontSize = 12.0f;
    Color matrixHeaderColor = Color(60, 60, 60, 255);
    bool matrixHeaderBold = true;
    float matrixRowLabelFontSize = 12.0f;
    Color matrixRowLabelColor = Color(60, 60, 60, 255);

    PackedSortOrder packedSortOrder = PackedSortOrder::SizeDescending;
    float packedPadding = 2.0f;
    bool packedEnclosureShow = false;
    Color packedEnclosureStroke = Color(55, 65, 90, 255);
    float packedEnclosureWidth = 1.5f;
    Color packedEnclosureFill = Colors::Transparent;

    float groupPadding = 8.0f;
    float groupTint = 0.60f;
    float groupLabelFontSize = 11.0f;
    Color groupLabelColor = Colors::Transparent;   // transparent = auto contrast
    bool groupLabelBold = true;

    std::vector<std::string> timelineRows;
    std::function<std::string(double)> xLabelFormatter;

    bool showSizeLegend = false;
    std::string sizeLegendTitle = "Size";
    std::vector<double> sizeLegendValues;
    std::string footnote;

    // ===== DATA =====
    std::vector<BubbleDataPoint> bubbles;

    // ===== LAYOUT CACHE (rebuilt each render, reused for hit tests) =====
    std::vector<LayoutCircle> layoutCircles;
    size_t hoveredBubble = SIZE_MAX;    // index into bubbles, SIZE_MAX = none

    // Matrix grid geometry computed by BuildMatrixLayout, used by the chrome.
    struct MatrixMetrics {
        double x0 = 0, y0 = 0, cellW = 0, cellH = 0, headerH = 0, rowLabelW = 0;
    };
    MatrixMetrics matrixMetrics;

    // Parent circles computed by BuildHierarchicalLayout.
    struct ParentCircle {
        double cx = 0, cy = 0, r = 0;
        double innerR = 0;       // radius of the children cluster (label band
                                 // is the ring between innerR and r)
        std::string group;
        Color base;              // saturated group colour (children)
        Color fill;              // tinted parent fill
    };
    std::vector<ParentCircle> parentCircles;

    // Group label rects drawn by RenderParentCircles this frame; passed to the
    // label placement solver as keep-out obstacles for the bubble name labels.
    std::vector<Rect2Dd> parentLabelRects;

    // Timeline geometry computed by BuildTimelineLayout, used by the chrome.
    struct TimelineMetrics {
        double x0 = 0, y0 = 0, rowH = 0, width = 0, bottomY = 0;
        double sidePad = 0, usable = 0;
        double xMin = 0, xMax = 1;
    };
    TimelineMetrics timelineMetrics;

    // Enclosure circle in screen coordinates (packed mode, when enabled).
    double enclosureCx = 0, enclosureCy = 0, enclosureR = 0;

    // Radius mapping actually used by the last layout pass, so the size legend
    // stays consistent with the drawn bubbles (packed layouts rescale radii).
    double effectiveMinRadius = 0.0, effectiveMaxRadius = 0.0;
    double effectiveDomainMin = 0.0, effectiveDomainMax = 1.0;

    // ===== HELPERS =====
    void SyncDataSource();              // mirror bubbles into the base dataSource
    void GetSizeDomain(double& outMin, double& outMax) const;
    void GetColorDomain(double& outMin, double& outMax) const;
    double SizeToRadius(double size, double domainMin, double domainMax,
                        double minR, double maxR) const;
    Color ResolveBubbleColor(const BubbleDataPoint& b, size_t index) const;
    Color PickContrastTextColor(const Color& background) const;
    std::string FormatValue(double value) const;

    // Shared greedy tangent circle packing: places items (radii pre-filled,
    // in the given order) around the origin, writing x/y.
    struct PackItem { double x = 0, y = 0, r = 0; size_t index = 0; };
    static void GreedyPack(std::vector<PackItem>& items, double pad);
    // Approximate minimal enclosing circle of a set of packed circles.
    static void ComputeEnclosingCircle(const std::vector<PackItem>& items,
                                       double& outCx, double& outCy, double& outR);
    Color ResolveGroupBaseColor(const std::string& group) const;

    void BuildScatterLayout();
    void BuildPackedLayout();
    void BuildMatrixLayout(IRenderContext* ctx);
    void BuildHierarchicalLayout();
    void BuildTimelineLayout(IRenderContext* ctx);

    void RenderBubbles(IRenderContext* ctx);
    void RenderParentCircles(IRenderContext* ctx);
    void RenderTimelineChrome(IRenderContext* ctx);
    void RenderEnclosureCircle(IRenderContext* ctx);
    void DrawBubble(IRenderContext* ctx, const LayoutCircle& c, const Color& fill,
                    bool hovered);
    void RenderBubbleLabels(IRenderContext* ctx);
    void RenderMatrixChrome(IRenderContext* ctx);   // row labels + column headers
    void RenderSizeLegend(IRenderContext* ctx);
    void RenderFootnote(IRenderContext* ctx);

    // Returns index into layoutCircles hit by the point, or SIZE_MAX.
    size_t HitTest(const Point2Di& pos) const;

    float MatrixRowLabelWidth(IRenderContext* ctx) const;
    float SizeLegendReservedWidth() const;
};

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

inline std::shared_ptr<UltraCanvasBubbleChartElement> CreateBubbleChartElement(
        const std::string& id, int x, int y, int width, int height,
        BubbleChartMode mode = BubbleChartMode::ScatterBubbles) {
    auto chart = std::make_shared<UltraCanvasBubbleChartElement>(id, x, y, width, height);
    chart->SetMode(mode);
    return chart;
}

inline std::shared_ptr<UltraCanvasBubbleChartElement> CreatePackedBubbleChart(
        const std::string& id, int x, int y, int width, int height) {
    return CreateBubbleChartElement(id, x, y, width, height, BubbleChartMode::PackedBubbles);
}

inline std::shared_ptr<UltraCanvasBubbleChartElement> CreateBubbleMatrixChart(
        const std::string& id, int x, int y, int width, int height,
        const std::vector<std::string>& rows, const std::vector<std::string>& columns) {
    auto chart = CreateBubbleChartElement(id, x, y, width, height, BubbleChartMode::BubbleMatrix);
    chart->SetMatrixLayout(rows, columns);
    return chart;
}

inline std::shared_ptr<UltraCanvasBubbleChartElement> CreateHierarchicalBubbleChart(
        const std::string& id, int x, int y, int width, int height) {
    return CreateBubbleChartElement(id, x, y, width, height,
                                    BubbleChartMode::HierarchicalPacked);
}

inline std::shared_ptr<UltraCanvasBubbleChartElement> CreateTimelineBubbleChart(
        const std::string& id, int x, int y, int width, int height,
        const std::vector<std::string>& rows = {}) {
    auto chart = CreateBubbleChartElement(id, x, y, width, height,
                                          BubbleChartMode::TimelineBubbles);
    if (!rows.empty()) chart->SetTimelineRows(rows);
    return chart;
}

} // namespace UltraCanvas

/*
=== BUBBLE CHART FEATURES SUMMARY ===

**CHART TYPES:**
- Scatter bubble chart - XY position + size (3rd dimension) + colour (4th)
- Packed bubble chart / bubble cloud - axis-free circle packing, optionally
  inside an outlined enclosure circle
- Bubble matrix - categorical rows x columns grid of sized bubbles
- Hierarchical packed bubbles - children packed inside tinted, labelled
  parent circles; parents packed against each other
- Timeline bubbles - named rows x continuous (time) X axis, size = value

**SIZE ENCODING:**
- Area-proportional scaling (perceptual best practice) or diameter scaling
- Configurable pixel radius range and explicit or automatic value domain

**COLOR ENCODING:**
- Uniform, palette cycling, per-category map
- Continuous colormaps (all HeatmapColormap palettes + custom anchors)
  over a fourth data dimension or over the size value

**LABELS & LEGEND:**
- Name labels inside/below bubbles with automatic contrast + fit detection
- Value labels with decimals/suffix formatting
- Nested-circles size legend, footnote caption

**INTERACTIVITY:**
- Tooltips (framework tooltip system), hover highlight, click callback

=== USAGE EXAMPLES ===

**Scatter bubbles with colormap ("darkness = 4th dimension"):**
```cpp
auto chart = CreateBubbleChartElement("travel", 20, 20, 640, 460);
chart->AddBubble(9, 79, 46.1, 74, "Mexico");
chart->AddBubble(44, 78, 38.0, 71, "Egypt");
chart->SetColorMode(BubbleColorMode::ColormapByValue);
chart->SetColormap(HeatmapColormap::Blues);
chart->SetShowSizeLegend(true);
chart->SetSizeLegendTitle("Health");
```

**Packed bubbles:**
```cpp
auto packed = CreatePackedBubbleChart("share", 20, 20, 600, 500);
packed->AddBubble(0, 0, 31.2, 0, "Chrome");
packed->AddBubble(0, 0, 17.4, 0, "Safari");
packed->SetNameLabelMode(BubbleNameLabelMode::Auto);
```

**Bubble matrix (survey style):**
```cpp
auto matrix = CreateBubbleMatrixChart("poll", 20, 20, 400, 600,
        {"Under 45", "45-64", "65+"}, {"Yes", "No"});
matrix->AddMatrixBubble("Under 45", "Yes", 79);
matrix->AddMatrixBubble("Under 45", "No", 16);
matrix->SetValueLabelMode(BubbleValueLabelMode::Inside);
```
*/
