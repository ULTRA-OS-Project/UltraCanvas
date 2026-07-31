// include/Plugins/Charts/UltraCanvasNestedAreaChart.h
// Nested Proportional Area Chart - Layered shapes with area proportional to values
// Version: 1.2.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <algorithm>

// X11 defines `None` as a macro that clobbers our enumerators.
#ifdef None
#undef None
#endif

namespace UltraCanvas {

// =============================================================================
// NESTED AREA CHART ENUMS
// =============================================================================

/// Shape mode for nested area visualization
    enum class NestedAreaShapeMode {
        Rectangle,      // Nested squares
        Circle,         // Nested circles (bubble style)
        RoundedRect     // Rounded corner squares
    };

/// Alignment mode for shape positioning
    enum class NestedAreaAlignmentMode {
        BottomLeft,     // Shapes align to bottom-left corner (default)
        BottomRight,    // Shapes align to bottom-right corner
        TopLeft,        // Shapes align to top-left corner
        TopRight,       // Shapes align to top-right corner
        Center,         // Shapes align to center (concentric)
        BottomCenter,   // Shapes align to bottom-center
        TopCenter       // Shapes align to top-center
    };

/// Label position for value display
    enum class NestedAreaLabelPosition {
        Inside,         // Label inside the shape
        Outside,        // Label outside the shape
        Tooltip,        // Label shown on hover only
        None            // No labels
    };

/// Predefined color themes for the chart
    enum class NestedAreaColorTheme {
        Default,        // Blue sequential gradient
        Categorical,    // Distinct colors for comparison
        Sequential,     // Light-to-dark single hue
        Warm,           // Orange/red gradient
        Cool,           // Blue/teal gradient
        Earth,          // Brown/green natural tones
        Pastel,         // Soft muted colors
        Vibrant,        // High saturation colors
        Monochrome,     // Grayscale
        Ocean,          // Blue-cyan theme
        Sunset,         // Orange-pink-purple theme
        Forest,         // Green theme
        Corporate,      // Professional blue-gray
        Colorblind,     // Accessible color palette
        Custom          // User-defined colors
    };

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/// Single data point for nested area chart
    struct NestedAreaDataPoint {
        std::string label;          // Display label
        double value = 0.0;         // Value (area will be proportional to this)
        Color color = Colors::Transparent;  // Explicit color override (Transparent = use theme)
        std::string tooltip;        // Custom tooltip text (optional)

        // Metadata for additional info display
        std::string category;       // Category grouping
        std::string unit;           // Unit of measurement (e.g., "km²", "$", "people")

        NestedAreaDataPoint() = default;

        NestedAreaDataPoint(const std::string& lbl, double val)
                : label(lbl), value(val) {}

        NestedAreaDataPoint(const std::string& lbl, double val, const Color& col)
                : label(lbl), value(val), color(col) {}

        NestedAreaDataPoint(const std::string& lbl, double val, const Color& col,
                            const std::string& tip)
                : label(lbl), value(val), color(col), tooltip(tip) {}
    };

// =============================================================================
// DATA SOURCE
// =============================================================================

/// Data source holding the nested area points; plugs into the common
/// IChartDataSource pipeline of UltraCanvasChartElementBase.
    class NestedAreaDataSource : public IChartDataSource {
    private:
        std::vector<NestedAreaDataPoint> points;

    public:
        // ===== IChartDataSource interface =====
        size_t GetPointCount() const override { return points.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            if (index < points.size()) {
                const auto& p = points[index];
                return ChartDataPoint(static_cast<double>(index), p.value, 0.0,
                                      p.label, p.value, p.color);
            }
            return ChartDataPoint(0, 0);
        }

        bool SupportsStreaming() const override { return false; }

        void LoadFromCSV(const std::string& filePath) override {
            // CSV format: label,value[,unit[,category]] - not implemented yet
        }

        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            points.clear();
            for (const auto& p : data) {
                NestedAreaDataPoint np(p.label, p.y, p.color);
                np.category = p.category;
                points.push_back(np);
            }
        }

        // ===== Nested-area specific access =====
        void AddPoint(const NestedAreaDataPoint& point) { points.push_back(point); }
        void SetPoints(const std::vector<NestedAreaDataPoint>& pts) { points = pts; }
        void Clear() { points.clear(); }

        const std::vector<NestedAreaDataPoint>& GetPoints() const { return points; }
        const NestedAreaDataPoint& GetNestedPoint(size_t index) const { return points.at(index); }
        NestedAreaDataPoint& GetNestedPointMutable(size_t index) { return points.at(index); }
    };

    inline std::shared_ptr<NestedAreaDataSource> CreateNestedAreaDataSource() {
        return std::make_shared<NestedAreaDataSource>();
    }

// =============================================================================
// COLOR THEME PALETTES
// =============================================================================

/// Static color palette definitions
    class NestedAreaColorPalettes {
    public:
        /// Get color palette for a specific theme
        static std::vector<Color> GetPalette(NestedAreaColorTheme theme) {
            switch (theme) {
                case NestedAreaColorTheme::Categorical:  return GetCategoricalPalette();
                case NestedAreaColorTheme::Sequential:   return GetSequentialPalette();
                case NestedAreaColorTheme::Warm:         return GetWarmPalette();
                case NestedAreaColorTheme::Cool:         return GetCoolPalette();
                case NestedAreaColorTheme::Earth:        return GetEarthPalette();
                case NestedAreaColorTheme::Pastel:       return GetPastelPalette();
                case NestedAreaColorTheme::Vibrant:      return GetVibrantPalette();
                case NestedAreaColorTheme::Monochrome:   return GetMonochromePalette();
                case NestedAreaColorTheme::Ocean:        return GetOceanPalette();
                case NestedAreaColorTheme::Sunset:       return GetSunsetPalette();
                case NestedAreaColorTheme::Forest:       return GetForestPalette();
                case NestedAreaColorTheme::Corporate:    return GetCorporatePalette();
                case NestedAreaColorTheme::Colorblind:   return GetColorblindPalette();
                case NestedAreaColorTheme::Default:
                default:                                 return GetDefaultPalette();
            }
        }

        /// Generate sequential colors from a base color
        static std::vector<Color> GenerateSequential(const Color& baseColor, int count) {
            std::vector<Color> colors;
            colors.reserve(count);
            for (int i = 0; i < count; ++i) {
                float factor = 0.3f + (0.7f * i / std::max(1, count - 1));
                colors.push_back(Color(
                        static_cast<uint8_t>(baseColor.r * factor),
                        static_cast<uint8_t>(baseColor.g * factor),
                        static_cast<uint8_t>(baseColor.b * factor),
                        baseColor.a
                ));
            }
            return colors;
        }

    private:
        // Default: Blue sequential gradient
        static std::vector<Color> GetDefaultPalette() {
            return {
                    Color(0, 255, 255, 255),    // Cyan (largest)
                    Color(100, 180, 220, 255),  // Light blue
                    Color(65, 105, 225, 255),   // Royal blue
                    Color(30, 60, 180, 255),    // Medium blue
                    Color(0, 32, 128, 255),     // Dark blue
                    Color(0, 20, 80, 255),      // Navy
                    Color(0, 100, 0, 255),      // Green (smallest)
                    Color(34, 139, 34, 255)     // Forest green
            };
        }

        // Categorical: Distinct colors for comparison
        static std::vector<Color> GetCategoricalPalette() {
            return {
                    Color(54, 162, 235, 255),   // Blue
                    Color(255, 99, 132, 255),   // Red
                    Color(255, 205, 86, 255),   // Yellow
                    Color(75, 192, 192, 255),   // Teal
                    Color(153, 102, 255, 255),  // Purple
                    Color(255, 159, 64, 255),   // Orange
                    Color(76, 175, 80, 255),    // Green
                    Color(233, 30, 99, 255)     // Pink
            };
        }

        // Sequential: Light-to-dark blue
        static std::vector<Color> GetSequentialPalette() {
            return {
                    Color(239, 243, 255, 255),  // Lightest
                    Color(198, 219, 239, 255),
                    Color(158, 202, 225, 255),
                    Color(107, 174, 214, 255),
                    Color(66, 146, 198, 255),
                    Color(33, 113, 181, 255),
                    Color(8, 81, 156, 255),
                    Color(8, 48, 107, 255)      // Darkest
            };
        }

        // Warm: Orange/red gradient
        static std::vector<Color> GetWarmPalette() {
            return {
                    Color(255, 245, 200, 255),  // Light yellow
                    Color(255, 220, 150, 255),  // Light orange
                    Color(255, 180, 100, 255),  // Orange
                    Color(255, 140, 60, 255),   // Dark orange
                    Color(255, 100, 50, 255),   // Orange-red
                    Color(220, 60, 40, 255),    // Red
                    Color(180, 30, 30, 255),    // Dark red
                    Color(128, 0, 0, 255)       // Maroon
            };
        }

        // Cool: Blue/teal gradient
        static std::vector<Color> GetCoolPalette() {
            return {
                    Color(224, 255, 255, 255),  // Light cyan
                    Color(175, 238, 238, 255),  // Pale turquoise
                    Color(127, 255, 212, 255),  // Aquamarine
                    Color(64, 224, 208, 255),   // Turquoise
                    Color(0, 206, 209, 255),    // Dark turquoise
                    Color(32, 178, 170, 255),   // Light sea green
                    Color(0, 139, 139, 255),    // Dark cyan
                    Color(0, 100, 100, 255)     // Teal
            };
        }

        // Earth: Brown/green natural tones
        static std::vector<Color> GetEarthPalette() {
            return {
                    Color(245, 235, 220, 255),  // Cream
                    Color(210, 180, 140, 255),  // Tan
                    Color(160, 140, 100, 255),  // Khaki
                    Color(139, 119, 101, 255),  // Brown
                    Color(107, 142, 35, 255),   // Olive drab
                    Color(85, 107, 47, 255),    // Dark olive
                    Color(60, 80, 40, 255),     // Forest
                    Color(34, 51, 34, 255)      // Dark green
            };
        }

        // Pastel: Soft muted colors
        static std::vector<Color> GetPastelPalette() {
            return {
                    Color(255, 179, 186, 255),  // Pink
                    Color(255, 223, 186, 255),  // Peach
                    Color(255, 255, 186, 255),  // Light yellow
                    Color(186, 255, 201, 255),  // Mint
                    Color(186, 225, 255, 255),  // Light blue
                    Color(186, 186, 255, 255),  // Lavender
                    Color(225, 186, 255, 255),  // Light purple
                    Color(255, 186, 255, 255)   // Light magenta
            };
        }

        // Vibrant: High saturation colors
        static std::vector<Color> GetVibrantPalette() {
            return {
                    Color(255, 0, 0, 255),      // Red
                    Color(255, 127, 0, 255),    // Orange
                    Color(255, 255, 0, 255),    // Yellow
                    Color(0, 255, 0, 255),      // Green
                    Color(0, 255, 255, 255),    // Cyan
                    Color(0, 0, 255, 255),      // Blue
                    Color(127, 0, 255, 255),    // Purple
                    Color(255, 0, 255, 255)     // Magenta
            };
        }

        // Monochrome: Grayscale
        static std::vector<Color> GetMonochromePalette() {
            return {
                    Color(240, 240, 240, 255),  // Almost white
                    Color(210, 210, 210, 255),  // Light gray
                    Color(180, 180, 180, 255),
                    Color(150, 150, 150, 255),  // Medium gray
                    Color(120, 120, 120, 255),
                    Color(90, 90, 90, 255),     // Dark gray
                    Color(60, 60, 60, 255),
                    Color(30, 30, 30, 255)      // Almost black
            };
        }

        // Ocean: Blue-cyan theme
        static std::vector<Color> GetOceanPalette() {
            return {
                    Color(173, 216, 230, 255),  // Light blue
                    Color(135, 206, 250, 255),  // Light sky blue
                    Color(100, 149, 237, 255),  // Cornflower blue
                    Color(70, 130, 180, 255),   // Steel blue
                    Color(30, 144, 255, 255),   // Dodger blue
                    Color(0, 105, 148, 255),    // Sea blue
                    Color(0, 80, 120, 255),     // Deep sea
                    Color(0, 50, 80, 255)       // Ocean depths
            };
        }

        // Sunset: Orange-pink-purple theme
        static std::vector<Color> GetSunsetPalette() {
            return {
                    Color(255, 200, 100, 255),  // Golden yellow
                    Color(255, 160, 80, 255),   // Orange
                    Color(255, 120, 100, 255),  // Coral
                    Color(255, 100, 130, 255),  // Salmon pink
                    Color(220, 80, 150, 255),   // Pink
                    Color(180, 60, 160, 255),   // Magenta
                    Color(140, 50, 150, 255),   // Purple
                    Color(80, 40, 120, 255)     // Deep purple
            };
        }

        // Forest: Green theme
        static std::vector<Color> GetForestPalette() {
            return {
                    Color(200, 230, 200, 255),  // Pale green
                    Color(144, 238, 144, 255),  // Light green
                    Color(124, 205, 124, 255),
                    Color(60, 179, 113, 255),   // Medium sea green
                    Color(46, 139, 87, 255),    // Sea green
                    Color(34, 139, 34, 255),    // Forest green
                    Color(0, 100, 0, 255),      // Dark green
                    Color(0, 64, 0, 255)        // Very dark green
            };
        }

        // Corporate: Professional blue-gray
        static std::vector<Color> GetCorporatePalette() {
            return {
                    Color(200, 210, 220, 255),  // Light blue-gray
                    Color(160, 180, 200, 255),
                    Color(100, 140, 180, 255),  // Steel blue
                    Color(70, 110, 160, 255),
                    Color(50, 90, 140, 255),    // Corporate blue
                    Color(40, 70, 110, 255),
                    Color(30, 50, 80, 255),     // Dark corporate
                    Color(20, 35, 55, 255)      // Near black
            };
        }

        // Colorblind: Accessible palette (optimized for deuteranopia/protanopia)
        static std::vector<Color> GetColorblindPalette() {
            return {
                    Color(230, 159, 0, 255),    // Orange
                    Color(86, 180, 233, 255),   // Sky blue
                    Color(0, 158, 115, 255),    // Bluish green
                    Color(240, 228, 66, 255),   // Yellow
                    Color(0, 114, 178, 255),    // Blue
                    Color(213, 94, 0, 255),     // Vermillion
                    Color(204, 121, 167, 255),  // Reddish purple
                    Color(100, 100, 100, 255)   // Gray
            };
        }
    };

// =============================================================================
// NESTED AREA CHART ELEMENT
// =============================================================================

    class UltraCanvasNestedAreaChart : public UltraCanvasChartElementBase {
    private:
        // Data (also registered as base-class dataSource)
        std::shared_ptr<NestedAreaDataSource> nestedDataSource;

        // Configuration
        NestedAreaShapeMode shapeMode = NestedAreaShapeMode::Rectangle;
        NestedAreaAlignmentMode alignmentMode = NestedAreaAlignmentMode::BottomLeft;
        NestedAreaLabelPosition labelPosition = NestedAreaLabelPosition::Inside;
        NestedAreaColorTheme colorTheme = NestedAreaColorTheme::Default;

        // Custom colors (when theme is Custom)
        std::vector<Color> customColors;

        // Visual settings
        float borderWidth = 1.0f;
        Color borderColor = Color(80, 80, 80, 255);
        bool showBorders = true;
        bool showLabels = true;
        bool showValues = true;
        bool showLegend = true;
        float cornerRadius = 8.0f;   // For RoundedRect mode
        float chartPadding = 10.0f;  // Padding inside chart area
        float legendHeight = 55.0f;  // Space reserved for legend below the shapes

        // Grid reference lines (optional)
        bool showGridLines = false;
        int gridLineCount = 5;
        Color gridLineColor = Color(200, 200, 200, 128);

        // Interaction state
        int hoveredIndex = -1;
        int selectedIndex = -1;

        // Cached per-shape layout (local coordinates). Label positions are not
        // cached: they are solved at render time by PlaceShapeLabels(), which
        // needs the render context for text measurement.
        struct ShapeCache {
            std::vector<Rect2Df> shapeBounds;      // Calculated bounds for each shape
            std::vector<float> shapeRadii;         // Radius for circles / half side for rects
            bool isValid = false;
            void Invalidate() { isValid = false; }
        };
        ShapeCache shapeCache;

        // Cached value range
        double maxDataValue = 0.0;
        double minDataValue = 0.0;

    public:
        // Callbacks
        std::function<void(size_t index)> onShapeHover;
        std::function<void(size_t index)> onShapeClick;
        std::function<void(size_t index)> onShapeSelect;
        std::function<void()> onSelectionClear;

        // ==========================================================================
        // CONSTRUCTOR
        // ==========================================================================

        UltraCanvasNestedAreaChart(const std::string& id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableZoom = false;   // Not needed for this chart type
            enablePan = false;
            enableTooltips = true;
            showGrid = false;     // No cartesian grid for nested areas
            showAxes = false;     // No axes either

            nestedDataSource = CreateNestedAreaDataSource();
            dataSource = nestedDataSource;  // Register with base class pipeline
        }

        ~UltraCanvasNestedAreaChart() override = default;

        // ==========================================================================
        // DATA MANAGEMENT
        // ==========================================================================

        /// Add a single data point
        void AddDataPoint(const NestedAreaDataPoint& point) {
            nestedDataSource->AddPoint(point);
            OnDataChanged();
        }

        /// Add data point with basic parameters
        void AddDataPoint(const std::string& label, double value) {
            nestedDataSource->AddPoint(NestedAreaDataPoint(label, value));
            OnDataChanged();
        }

        /// Add data point with explicit color
        void AddDataPoint(const std::string& label, double value, const Color& color) {
            nestedDataSource->AddPoint(NestedAreaDataPoint(label, value, color));
            OnDataChanged();
        }

        /// Set all data points at once
        void SetDataPoints(const std::vector<NestedAreaDataPoint>& points) {
            nestedDataSource->SetPoints(points);
            OnDataChanged();
        }

        /// Clear all data points
        void ClearDataPoints() {
            nestedDataSource->Clear();
            hoveredIndex = -1;
            selectedIndex = -1;
            OnDataChanged();
        }

        /// Get data point count
        size_t GetDataPointCount() const { return nestedDataSource->GetPointCount(); }

        /// Get data point by index
        const NestedAreaDataPoint& GetDataPoint(size_t index) const {
            return nestedDataSource->GetNestedPoint(index);
        }

        /// Get mutable data point by index (invalidates layout)
        NestedAreaDataPoint& GetDataPointMutable(size_t index) {
            shapeCache.Invalidate();
            return nestedDataSource->GetNestedPointMutable(index);
        }

        // ==========================================================================
        // CONFIGURATION
        // ==========================================================================

        /// Set shape mode (Rectangle, Circle, RoundedRect)
        void SetShapeMode(NestedAreaShapeMode mode) {
            shapeMode = mode;
            shapeCache.Invalidate();
            RequestRedraw();
        }
        NestedAreaShapeMode GetShapeMode() const { return shapeMode; }

        /// Set alignment mode
        void SetAlignmentMode(NestedAreaAlignmentMode mode) {
            alignmentMode = mode;
            shapeCache.Invalidate();
            RequestRedraw();
        }
        NestedAreaAlignmentMode GetAlignmentMode() const { return alignmentMode; }

        /// Set color theme
        void SetColorTheme(NestedAreaColorTheme theme) {
            colorTheme = theme;
            RequestRedraw();
        }
        NestedAreaColorTheme GetColorTheme() const { return colorTheme; }

        /// Set custom colors (automatically sets theme to Custom)
        void SetCustomColors(const std::vector<Color>& colors) {
            customColors = colors;
            colorTheme = NestedAreaColorTheme::Custom;
            RequestRedraw();
        }

        /// Set label position
        void SetLabelPosition(NestedAreaLabelPosition position) {
            labelPosition = position;
            RequestRedraw();
        }
        NestedAreaLabelPosition GetLabelPosition() const { return labelPosition; }

        /// Set border width
        void SetBorderWidth(float width) {
            borderWidth = width;
            RequestRedraw();
        }

        /// Set border color
        void SetBorderColor(const Color& color) {
            borderColor = color;
            RequestRedraw();
        }

        /// Enable/disable borders
        void SetShowBorders(bool show) {
            showBorders = show;
            RequestRedraw();
        }

        /// Enable/disable labels
        void SetShowLabels(bool show) {
            showLabels = show;
            RequestRedraw();
        }

        /// Enable/disable value display
        void SetShowValues(bool show) {
            showValues = show;
            RequestRedraw();
        }

        /// Enable/disable legend
        void SetShowLegend(bool show) {
            showLegend = show;
            InvalidateCache();       // Plot area changes
            shapeCache.Invalidate();
            RequestRedraw();
        }

        /// Set corner radius (for RoundedRect mode)
        void SetCornerRadius(float radius) {
            cornerRadius = radius;
            RequestRedraw();
        }

        /// Set padding between the element border and the drawn shapes
        /// (distinct from UltraCanvasUIElement::SetPadding, which sets CSS box padding)
        void SetChartPadding(float pad) {
            chartPadding = pad;
            InvalidateCache();
            shapeCache.Invalidate();
            RequestRedraw();
        }
        float GetChartPadding() const { return chartPadding; }

        /// Enable/disable grid reference lines
        void SetShowGridLines(bool show) {
            showGridLines = show;
            RequestRedraw();
        }

        /// Set grid line count
        void SetGridLineCount(int count) {
            gridLineCount = std::max(1, count);
            RequestRedraw();
        }

        // ==========================================================================
        // SELECTION API
        // ==========================================================================

        /// Get currently hovered index (-1 if none)
        int GetHoveredIndex() const { return hoveredIndex; }

        /// Get currently selected index (-1 if none)
        int GetSelectedIndex() const { return selectedIndex; }

        /// Set selected index programmatically
        void SetSelectedIndex(int index) {
            selectedIndex = index;
            if (index >= 0 && onShapeSelect) {
                onShapeSelect(static_cast<size_t>(index));
            }
            RequestRedraw();
        }

        /// Clear selection
        void ClearSelection() {
            selectedIndex = -1;
            if (onSelectionClear) {
                onSelectionClear();
            }
            RequestRedraw();
        }

        // ==========================================================================
        // BASE CLASS OVERRIDES
        // ==========================================================================

        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;

        // ==========================================================================
        // INTERNAL METHODS
        // ==========================================================================

        void OnDataChanged() {
            shapeCache.Invalidate();
            InvalidateCache();
            UpdateValueRange();
            RequestRedraw();
        }

        /// Update min/max value range
        void UpdateValueRange() {
            const auto& pts = nestedDataSource->GetPoints();
            if (pts.empty()) {
                maxDataValue = 0.0;
                minDataValue = 0.0;
                return;
            }
            maxDataValue = pts[0].value;
            minDataValue = pts[0].value;
            for (const auto& point : pts) {
                maxDataValue = std::max(maxDataValue, point.value);
                minDataValue = std::min(minDataValue, point.value);
            }
        }

        /// Indices sorted by value, descending (largest shape first)
        std::vector<size_t> GetSortedIndices(bool descending) const;

        /// Calculate per-shape layout in local coordinates
        void CalculateShapeCache();

        /// Get color for data point at index
        Color GetColorForIndex(size_t index) const;

        /// Shape rendering
        void RenderShapes(IRenderContext* ctx);
        void RenderShapeLabels(IRenderContext* ctx);
        void RenderLegend(IRenderContext* ctx);
        void RenderGridLines(IRenderContext* ctx);

        /// Hit test for local mouse position; returns data index or -1
        int HitTest(const Point2Di& pos) const;

        /// Calculate side length from value (area-true scaling)
        float CalculateSideFromValue(double value, double maxVal, float maxSide) const {
            if (maxVal <= 0 || value <= 0) return 0.0f;
            // Area is proportional to value, so side = sqrt(value/maxValue) * maxSide
            return static_cast<float>(std::sqrt(value / maxVal)) * maxSide;
        }

        /// Calculate radius from value (area-true scaling for circles)
        float CalculateRadiusFromValue(double value, double maxVal, float maxRadius) const {
            if (maxVal <= 0 || value <= 0) return 0.0f;
            // Area is proportional to value: A = π*r², so r = sqrt(value/maxValue) * maxRadius
            return static_cast<float>(std::sqrt(value / maxVal)) * maxRadius;
        }

        /// Format value for display
        std::string FormatNestedValue(double value, const std::string& unit = "") const;

        /// Generate tooltip text for a data point
        std::string GenerateNestedTooltip(size_t index) const;

        /// Show/update the framework tooltip for the hovered shape
        void UpdateNestedTooltip(const Point2Di& mousePos);
    };

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

    inline std::shared_ptr<UltraCanvasNestedAreaChart> CreateNestedAreaChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasNestedAreaChart>(id, x, y, width, height);
    }

// =============================================================================
// BUILDER CLASS FOR FLUENT CONFIGURATION
// =============================================================================

    class NestedAreaChartBuilder {
    private:
        std::string identifier = "nestedAreaChart";
        int x = 0, y = 0, w = 400, h = 400;
        std::vector<NestedAreaDataPoint> dataPoints;
        NestedAreaShapeMode shapeMode = NestedAreaShapeMode::Rectangle;
        NestedAreaAlignmentMode alignmentMode = NestedAreaAlignmentMode::BottomLeft;
        NestedAreaColorTheme colorTheme = NestedAreaColorTheme::Default;
        std::vector<Color> customColors;
        std::string title;
        bool showLabels = true;
        bool showValues = true;
        bool showLegend = true;
        bool showBorders = true;
        float borderWidth = 1.0f;

    public:
        NestedAreaChartBuilder& SetIdentifier(const std::string& ident) {
            identifier = ident;
            return *this;
        }

        NestedAreaChartBuilder& SetPosition(int px, int py) {
            x = px; y = py;
            return *this;
        }

        NestedAreaChartBuilder& SetSize(int width, int height) {
            w = width; h = height;
            return *this;
        }

        NestedAreaChartBuilder& SetTitle(const std::string& t) {
            title = t;
            return *this;
        }

        NestedAreaChartBuilder& AddData(const std::string& label, double value) {
            dataPoints.emplace_back(label, value);
            return *this;
        }

        NestedAreaChartBuilder& AddData(const NestedAreaDataPoint& point) {
            dataPoints.push_back(point);
            return *this;
        }

        NestedAreaChartBuilder& SetShapeMode(NestedAreaShapeMode mode) {
            shapeMode = mode;
            return *this;
        }

        NestedAreaChartBuilder& SetAlignmentMode(NestedAreaAlignmentMode mode) {
            alignmentMode = mode;
            return *this;
        }

        NestedAreaChartBuilder& SetColorTheme(NestedAreaColorTheme theme) {
            colorTheme = theme;
            return *this;
        }

        NestedAreaChartBuilder& SetCustomColors(const std::vector<Color>& colors) {
            customColors = colors;
            colorTheme = NestedAreaColorTheme::Custom;
            return *this;
        }

        NestedAreaChartBuilder& ShowLabels(bool show) {
            showLabels = show;
            return *this;
        }

        NestedAreaChartBuilder& ShowValues(bool show) {
            showValues = show;
            return *this;
        }

        NestedAreaChartBuilder& ShowLegend(bool show) {
            showLegend = show;
            return *this;
        }

        NestedAreaChartBuilder& ShowBorders(bool show) {
            showBorders = show;
            return *this;
        }

        NestedAreaChartBuilder& SetBorderWidth(float width) {
            borderWidth = width;
            return *this;
        }

        std::shared_ptr<UltraCanvasNestedAreaChart> Build() {
            auto chart = CreateNestedAreaChartElement(identifier, x, y, w, h);

            chart->SetShapeMode(shapeMode);
            chart->SetAlignmentMode(alignmentMode);
            chart->SetColorTheme(colorTheme);

            if (!customColors.empty()) {
                chart->SetCustomColors(customColors);
            }
            if (!title.empty()) {
                chart->SetChartTitle(title);
            }

            chart->SetShowLabels(showLabels);
            chart->SetShowValues(showValues);
            chart->SetShowLegend(showLegend);
            chart->SetShowBorders(showBorders);
            chart->SetBorderWidth(borderWidth);

            for (const auto& point : dataPoints) {
                chart->AddDataPoint(point);
            }

            return chart;
        }
    };

} // namespace UltraCanvas
