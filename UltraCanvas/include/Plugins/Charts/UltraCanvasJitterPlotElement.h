// include/Plugins/Charts/UltraCanvasJitterPlotElement.h
// Jitter plot (strip plot) element with statistical overlays and hybrid modes
// Version: 1.1.3
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <random>
#include <algorithm>

#undef None
namespace UltraCanvas {

// =============================================================================
// JITTER PLOT ENUMERATIONS
// =============================================================================

enum class JitterDistribution {
    Uniform,        // Rectangular spread (default) - uses uniform random
    Gaussian,       // Circular/normal spread - uses Box-Muller transform
    Beeswarm        // Beeswarm packing (swarm, center, hex, square)
};

enum class JitterRepresentationMode {
    IndividualPoints,   // Each dot = one data point (default)
    CountMode          // Dot size/count represents aggregation
};

enum class JitterHybridMode {
    JitterOnly,         // Pure jitter plot (default)
    JitterBoxPlot,      // Jitter + box plot overlay
    JitterBarChart,     // Jitter + bar chart base
    JitterViolinPlot,   // Jitter + violin plot
    RaincloudPlot      // Half-violin + jitter horizontal
};

enum class StatisticalMarkerStyle {
    MedianLine,         // Horizontal line at median (most common)
    MeanPoint,         // Point/diamond at mean
    MeanLine,          // Horizontal line at mean
    QuartileLines,     // Lines at Q1, Q2 (median), Q3
    BoxPlotOverlay     // Full box plot with whiskers
};

enum class AxisScale {
    Linear,            // Standard linear scale (default)
    Logarithmic,       // Log10 scale for exponential data
    SymmetricLog       // Symmetric log (handles negative values)
};

enum class MeanMarkerShape {
    Diamond,           // Diamond symbol (default)
    Cross,             // Plus/cross symbol (✚)
    Circle,            // Filled circle
    Square,            // Filled square
    Star               // Star symbol
};

enum class CategoryLabelPosition {
    Bottom,            // Below chart (default)
    Top,               // Above chart
    Both               // Mirror labels top and bottom
};

// =============================================================================
// BEESWARM ENUMERATIONS
// =============================================================================

enum class BeeswarmMethod {
    Swarm,             // Greedy swarm placement (default)
    Center,            // Symmetric center layout
    Hex,               // Hexagonal grid
    Square             // Square grid
};

enum class BeeswarmPriority {
    Ascending,         // Smallest values first (default)
    Descending,        // Largest values first
    Random,            // Random order
    Compact,           // Tightest fit first
    Dense              // Highest local density first
};

enum class BeeswarmCorral {
    NoneCorral,        // No constraint (default; renamed from None to avoid X11 macro collision)
    Open,              // Allow escape with warning
    Wrap,              // Wrap points to opposite side
    Clamp              // Force within bounds
};

enum class BeeswarmSide {
    Both,              // Spread both directions (default)
    Left,              // Force left side only
    Right              // Force right side only
};

// =============================================================================
// JITTER PLOT DATA STRUCTURES
// =============================================================================

// Category data container for jitter plots
struct JitterCategoryData {
    std::string categoryName;
    std::vector<double> values;
    std::string hueGroup;  // Secondary grouping variable
    Color customColor;     // Optional custom color override
    
    // Statistical caching
    double cachedMedian = 0.0;
    double cachedMean = 0.0;
    double cachedQ1 = 0.0;
    double cachedQ3 = 0.0;
    double cachedMin = 0.0;
    double cachedMax = 0.0;
    bool statisticsCached = false;
    
    JitterCategoryData(const std::string& name, const std::string& hue = "")
        : categoryName(name), hueGroup(hue), customColor(0) {}
};

// =============================================================================
// JITTER PLOT ELEMENT CLASS
// =============================================================================

class UltraCanvasJitterPlotElement : public UltraCanvasChartElementBase {
public:
    // ===== BEESWARM HELPER STRUCTURE (MUST BE FIRST - USED BY PRIVATE METHODS) =====
    struct BeeswarmPoint {
        Point2Dd position;      // Final rendered position
        float radius;           // Circle radius
        int originalIndex;      // Index in original data array
        double yValue;          // Original Y data value
    };
    
    // ===== POINT SHAPE ENUM (MUST BE PUBLIC FOR EXAMPLES) =====
    enum class PointShape {
        Circle, Square, Triangle, Diamond
    };
    
private:
    // ===== CORE JITTER SETTINGS =====
    JitterDistribution jitterDistribution = JitterDistribution::Gaussian;
    float jitterAmount = 0.2f;           // Width of jitter (0.0-1.0)
    int jitterSeed = 0;                  // For reproducibility (0 = random)
    
    // ===== REPRESENTATION MODE =====
    JitterRepresentationMode representationMode = JitterRepresentationMode::IndividualPoints;
    
    // ===== HYBRID MODE =====
    JitterHybridMode hybridMode = JitterHybridMode::JitterOnly;
    
    // ===== POINT APPEARANCE =====
    Color pointColor = Color(0, 102, 204, 200);  // Default with alpha
    float pointSize = 6.0f;
    float pointAlpha = 0.7f;            // Transparency (0.0-1.0)
    Color pointEdgeColor = Color(255, 255, 255, 100);
    float pointEdgeWidth = 0.5f;
    PointShape pointShape = PointShape::Circle;
    
    // ===== STATISTICAL OVERLAYS =====
    bool showMedianMarker = false;
    bool showMeanMarker = false;
    bool showQuartiles = false;
    StatisticalMarkerStyle markerStyle = StatisticalMarkerStyle::MedianLine;
    Color medianLineColor = Color(255, 0, 0, 255);
    float medianLineWidth = 2.5f;
    Color meanMarkerColor = Color(0, 128, 0, 255);
    float meanMarkerSize = 8.0f;
    MeanMarkerShape meanMarkerShape = MeanMarkerShape::Diamond;
    Color quartileLineColor = Color(100, 100, 100, 200);
    float quartileLineWidth = 1.5f;
    
    // ===== AXIS SCALING =====
    AxisScale yAxisScale = AxisScale::Linear;
    double logScaleBase = 10.0;  // Base for logarithmic scale
    
    // ===== CATEGORY LABEL POSITIONING =====
    CategoryLabelPosition categoryLabelPosition = CategoryLabelPosition::Bottom;
    
    // ===== GROUPING (SECONDARY CATEGORICAL VARIABLE) =====
    std::string hueVariable;
    bool enableDodge = false;
    float dodgeWidth = 0.8f;
    std::map<std::string, Color> hueColorMap;
    std::vector<std::string> hueCategories;  // Ordered hue categories
    
    // ===== CATEGORY SETTINGS =====
    std::vector<std::string> categories;
    std::map<std::string, std::vector<JitterCategoryData>> categoryData;  // category -> list of hue groups
    float categoryWidth = 1.0f;
    float categorySpacing = 0.3f;
    
    // ===== ORIENTATION =====
    bool horizontalOrientation = false;  // false = vertical (default)
    
    // ===== BOX PLOT SETTINGS (FOR HYBRID MODE) =====
    bool showBoxPlotWhiskers = true;
    bool showBoxPlotOutliers = true;
    float boxPlotWidth = 0.6f;
    Color boxPlotFillColor = Color(100, 100, 100, 100);
    Color boxPlotBorderColor = Color(50, 50, 50, 255);
    float boxPlotBorderWidth = 1.5f;
    
    // ===== BAR CHART SETTINGS (FOR HYBRID MODE) =====
    Color barChartColor = Color(150, 150, 150, 150);
    float barChartWidth = 0.8f;
    bool showBarValues = true;
    
    // ===== VIOLIN PLOT SETTINGS (FOR HYBRID MODE) =====
    Color violinFillColor = Color(100, 100, 150, 100);
    Color violinBorderColor = Color(50, 50, 100, 200);
    float violinBorderWidth = 1.0f;
    int violinResolution = 50;  // Number of density calculation points
    
    // ===== FILTERING =====
    std::string selectedRegion;
    double minScoreFilter = 0.0;
    
    // ===== CACHE FOR POINT POSITIONS =====
    struct PointPosition {
        float x, y;
        double value;
        std::string category;
        std::string region;
    };
    mutable std::vector<PointPosition> pointPositionsCache;
    mutable bool pointCacheValid = false;
    
    // ===== MOUSE MOVE THROTTLE =====
    Point2Di lastMousePos;
    size_t lastMouseMoveTime = 0;
    static constexpr size_t MOUSE_MOVE_INTERVAL_MS = 50;
    
    // ===== RANDOM NUMBER GENERATOR =====
    mutable std::mt19937 randomGenerator;
    mutable std::uniform_real_distribution<float> uniformDist;
    mutable std::normal_distribution<float> gaussianDist;
    
    // ===== BEESWARM CONFIGURATION =====
    BeeswarmMethod beeswarmMethod = BeeswarmMethod::Swarm;
    BeeswarmPriority beeswarmPriority = BeeswarmPriority::Ascending;
    BeeswarmCorral beeswarmCorral = BeeswarmCorral::NoneCorral;
    BeeswarmSide beeswarmSide = BeeswarmSide::Both;
    float beeswarmSpacing = 0.5f;        // Minimum gap between circles (pixels)
    float beeswarmMaxWidth = 0.8f;       // Maximum swarm width (0.0-1.0 of categoryWidth)
    bool beeswarmCompact = false;         // Greedy vs predetermined order
    
    // ===== BEESWARM HELPER METHODS =====
    // Main beeswarm layout calculator
    std::vector<BeeswarmPoint> CalculateBeeswarmLayout(
        const std::vector<double>& values,
        float categoryCenter,
        float categoryWidth,
        float pointRadius);
    
    // Swarm method: greedy placement preserving Y positions
    std::vector<BeeswarmPoint> CalculateBeeswarmSwarm(
        const std::vector<double>& values,
        const std::vector<int>& sortedIndices,
        float categoryCenter,
        float categoryWidth,
        float pointRadius);
    
    // Center method: symmetric grid layout
    std::vector<BeeswarmPoint> CalculateBeeswarmCenter(
        const std::vector<double>& values,
        float categoryCenter,
        float categoryWidth,
        float pointRadius);
    
    // Hex method: hexagonal grid layout
    std::vector<BeeswarmPoint> CalculateBeeswarmHex(
        const std::vector<double>& values,
        float categoryCenter,
        float categoryWidth,
        float pointRadius);
    
    // Square method: square grid layout
    std::vector<BeeswarmPoint> CalculateBeeswarmSquare(
        const std::vector<double>& values,
        float categoryCenter,
        float categoryWidth,
        float pointRadius);
    
    // Check if two circles collide
    bool CheckBeeswarmCollision(
        const Point2Dd& testPos,
        float testRadius,
        const std::vector<BeeswarmPoint>& placedPoints,
        float spacing);
    
    // Sort point indices by priority mode
    std::vector<int> SortIndicesByPriority(
        const std::vector<double>& values,
        BeeswarmPriority priority);
    
    // Calculate local density for density-based priority
    double CalculateLocalDensity(
        const std::vector<double>& values,
        int index,
        double bandwidth);
    
    // Apply corral strategy to runaway points
    void ApplyCorralStrategy(
        std::vector<BeeswarmPoint>& points,
        float categoryCenter,
        float maxWidth,
        BeeswarmCorral corral);
    
    // Apply side constraint (both/left/right)
    void ApplySideConstraint(
        std::vector<BeeswarmPoint>& points,
        float categoryCenter,
        BeeswarmSide side);

public:
    // ===== CONSTRUCTOR =====
    UltraCanvasJitterPlotElement(const std::string& id, 
                                  int x, int y, int width, int height);
    
    // ===== CONFIGURATION METHODS =====
    
    // Jitter settings
    void SetJitterDistribution(JitterDistribution dist);
    void SetJitterAmount(float amount);  // 0.0-1.0, default 0.2
    void SetJitterSeed(int seed);  // 0 = random, >0 = reproducible
    
    // Representation mode
    void SetRepresentationMode(JitterRepresentationMode mode);
    
    // Hybrid mode
    void SetHybridMode(JitterHybridMode mode);
    
    // Point appearance
    void SetPointColor(const Color& color);
    void SetPointSize(float size);
    void SetPointAlpha(float alpha);  // 0.0-1.0
    void SetPointEdgeColor(const Color& color);
    void SetPointEdgeWidth(float width);
    void SetPointShape(PointShape shape);
    
    // Statistical overlays
    void SetShowMedianMarker(bool show);
    void SetMedianMarkerStyle(const Color& color, float width);
    void SetShowMeanMarker(bool show);
    void SetMeanMarkerStyle(const Color& color, float size);
    void SetMeanMarkerShape(MeanMarkerShape shape);
    void SetShowQuartiles(bool show);
    void SetQuartileMarkerStyle(const Color& color, float width);
    void SetStatisticalMarkerStyle(StatisticalMarkerStyle style);
    
    // Axis scaling
    void SetYAxisScale(AxisScale scale);
    void SetLogScaleBase(double base);  // For logarithmic scale (default: 10.0)
    
    // Category label positioning
    void SetCategoryLabelPosition(CategoryLabelPosition position);
    
    // Grouping (secondary categorical variable)
    void SetHueVariable(const std::string& varName);
    void SetHueColorMap(const std::map<std::string, Color>& colorMap);
    void SetDodgeEnabled(bool enabled, float width = 0.8f);
    
    // Category settings
    void SetCategories(const std::vector<std::string>& cats);
    void SetCategorySpacing(float spacing);  // 0.0-1.0
    
    // Orientation
    void SetOrientation(bool horizontal);  // false = vertical, true = horizontal
    
    // Box plot hybrid settings
    void SetBoxPlotSettings(bool showWhiskers, bool showOutliers, float width);
    void SetBoxPlotColors(const Color& fill, const Color& border, float borderWidth);
    
    // Bar chart hybrid settings
    void SetBarChartSettings(const Color& color, float width, bool showValues);
    
    // Violin plot hybrid settings
    void SetViolinSettings(const Color& fill, const Color& border, 
                          float borderWidth, int resolution);
    
    // ===== DATA LOADING =====
    void AddCategoryData(const std::string& category, 
                        const std::vector<double>& values,
                        const std::string& hueValue = "");
    
    void ClearData();
    
    // ===== BEESWARM CONFIGURATION (NEW) =====
    void SetBeeswarmMethod(BeeswarmMethod method);
    void SetBeeswarmPriority(BeeswarmPriority priority);
    void SetBeeswarmCorral(BeeswarmCorral corral);
    void SetBeeswarmSide(BeeswarmSide side);
    void SetBeeswarmSpacing(float spacing);
    void SetBeeswarmMaxWidth(float maxWidth);
    void SetBeeswarmCompact(bool compact);
    
    // ===== APPEARANCE SETTINGS =====
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible);
    void SetGridColor(const Color& color);
    void SetAxesVisible(bool visible);
    
    // ===== FILTERING =====
    void SetRegionFilter(const std::string& region);
    void SetMinScoreFilter(double minScore);
    
    // ===== RENDERING (OVERRIDE FROM BASE) =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;  // Override base class Render
    void RenderChart(IRenderContext* ctx) override;
    bool HandleChartMouseMove(const Point2Di& mousePos) override;
    
    // ===== TOOLTIP OVERRIDE =====
    std::string GenerateTooltipContent(const ChartDataPoint& point, size_t index) override;

protected:
    // ===== OVERRIDE FROM BASE =====
    ChartDataBounds CalculateDataBounds() override;

private:
    // ===== JITTER CALCULATION =====
    float CalculateJitter(size_t dataIndex, size_t groupIndex) const;
    float CalculateGaussianJitter() const;
    float CalculateUniformJitter() const;
    
    // ===== STATISTICAL CALCULATIONS =====
    void CalculateStatistics(JitterCategoryData& data);
    double CalculateMedian(const std::vector<double>& values);
    double CalculateMean(const std::vector<double>& values);
    std::pair<double, double> CalculateQuartiles(const std::vector<double>& values);
    std::pair<double, double> CalculateMinMax(const std::vector<double>& values);
    
    // ===== DENSITY ESTIMATION (FOR VIOLIN) =====
    std::vector<float> CalculateKernelDensity(const std::vector<double>& values, int resolution);
    
    // ===== RENDERING HELPERS =====
    void RenderJitterPoints(IRenderContext* ctx);
    void RenderJitterAxes(IRenderContext* ctx);
    void RenderRegionBands(IRenderContext* ctx);
    void RenderNationalAverageLine(IRenderContext* ctx);
    void RenderMedianMarkers(IRenderContext* ctx);
    void RenderMeanMarkers(IRenderContext* ctx);
    void RenderQuartileMarkers(IRenderContext* ctx);
    void RenderBoxPlotOverlay(IRenderContext* ctx);
    void RenderBarChartBase(IRenderContext* ctx);
    void RenderViolinOverlay(IRenderContext* ctx);
    void RenderRaincloudPlot(IRenderContext* ctx);
    
    // Point rendering
    void DrawJitterPoint(IRenderContext* ctx, const Point2Dd& pos, 
                        const Color& color, float size);
    void DrawMeanMarker(IRenderContext* ctx, const Point2Dd& pos, 
                       MeanMarkerShape shape, float size);
    
    // Position calculation
    Point2Dd CalculatePointPosition(size_t categoryIndex, double value, 
                                    float jitter, size_t hueIndex = 0);
    
    float GetCategoryPosition(size_t categoryIndex, size_t hueIndex = 0);
    float GetValuePosition(double value);
    
    // Color management
    Color GetPointColor(size_t categoryIndex, const std::string& hueValue);
    
    // Data access
    std::vector<JitterCategoryData>& GetCategoryDataList(const std::string& category);
    const std::vector<JitterCategoryData>& GetCategoryDataList(const std::string& category) const;
};

// =============================================================================
// FACTORY FUNCTION - FOLLOWS EXISTING ULTRACANVAS PATTERNS
// =============================================================================

inline std::shared_ptr<UltraCanvasJitterPlotElement> CreateJitterPlotElement(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasJitterPlotElement>(id, x, y, width, height);
}

// Helper factory for jitter plot with basic configuration
inline std::shared_ptr<UltraCanvasJitterPlotElement> CreateJitterPlotWithCategories(
        const std::string& id, int x, int y, int width, int height,
        const std::vector<std::string>& categories, const std::string& title = "") {
    
    auto chart = CreateJitterPlotElement(id, x, y, width, height);
    chart->SetCategories(categories);
    if (!title.empty()) {
        chart->SetChartTitle(title);
    }
    return chart;
}

} // namespace UltraCanvas

/*
=== JITTER PLOT FEATURES SUMMARY ===

✅ **CORE FEATURES:**
- Dual jitter algorithms (Uniform & Gaussian distribution)
- Individual points and count aggregation modes
- Vertical and horizontal orientations
- Configurable jitter amount and reproducible seeds

✅ **STATISTICAL OVERLAYS:**
- Median line markers (horizontal lines)
- Mean markers (points or lines)
- Quartile indicators (Q1, Q2, Q3)
- Full box plot integration

✅ **HYBRID VISUALIZATION MODES:**
- Jitter-only (pure strip plot)
- Jittered Box Plot (dual axis)
- Jittered Bar Chart (magnitude + distribution)
- Jittered Violin Plot (density + points)
- Raincloud Plot (half-violin + jitter)

✅ **GROUPING & ORGANIZATION:**
- Multiple categories on axis
- Secondary grouping with hue variable
- Dodge mode for separating hue groups
- Custom color mapping per category/hue

✅ **VISUAL CUSTOMIZATION:**
- Point shapes (Circle, Square, Triangle, Diamond)
- Alpha/opacity control for overlaps
- Point size and edge styling
- Color coding by category or hue

✅ **INTERACTIVE FEATURES:**
- Tooltips with exact values
- Mouse hover highlighting
- Zoom/pan support (inherited from base)
- Selection capability (inherited from base)

✅ **ARCHITECTURE COMPLIANCE:**
- Inherits from UltraCanvasChartElementBase
- Uses IRenderContext for unified rendering
- Follows UltraCanvas naming conventions
- Platform-independent implementation
- Proper version header with YYYY-MM-DD date

=== USAGE EXAMPLES ===

**Basic Jitter Plot:**
```cpp
auto jitter = CreateJitterPlotElement("jitter1", 1001, 50, 50, 600, 400);
jitter->SetCategories({"Group A", "Group B", "Group C"});
jitter->AddCategoryData("Group A", {1.2, 1.5, 1.8, 2.1, 1.9});
jitter->AddCategoryData("Group B", {2.5, 2.8, 3.1, 2.9, 3.2});
jitter->AddCategoryData("Group C", {1.8, 2.1, 2.4, 2.2, 2.6});
jitter->SetShowMedianMarker(true);
```

**Jittered Box Plot with Grouping:**
```cpp
auto jitter = CreateJitterPlotElement("jitter2", 1002, 50, 50, 600, 400);
jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);
jitter->SetHueVariable("Treatment");
jitter->SetDodgeEnabled(true, 0.8f);
jitter->AddCategoryData("Pre", values1, "Control");
jitter->AddCategoryData("Pre", values2, "Treated");
```

**Gaussian Jitter with Statistics:**
```cpp
auto jitter = CreateJitterPlotElement("jitter3", 1003, 50, 50, 600, 400);
jitter->SetJitterDistribution(JitterDistribution::Gaussian);
jitter->SetJitterAmount(0.15f);
jitter->SetPointAlpha(0.6f);
jitter->SetShowMedianMarker(true);
jitter->SetShowQuartiles(true);
```

**/