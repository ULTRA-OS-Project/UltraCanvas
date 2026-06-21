// include/Plugins/Charts/UltraCanvasRadarChartElement.h
// Comprehensive radar chart element with multi-axis, multi-series visualization
// Version: 2.1.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTimer.h"
#include <vector>
#include <string>
#include <utility>
#include <chrono>

namespace UltraCanvas {

// =============================================================================
// RADAR CHART SPECIFIC DATA STRUCTURES
// =============================================================================

    struct RadarChartAxis {
        std::string name;
        double minValue;
        double maxValue;
        Color axisColor;
        bool showGrid;
        int gridLevels;

        RadarChartAxis(const std::string& axisName, double min = 0.0, double max = 100.0)
                : name(axisName), minValue(min), maxValue(max),
                  axisColor(Color(128, 128, 128, 255)), showGrid(true), gridLevels(5) {}
    };

    struct RadarChartSeries {
        std::string name;
        std::vector<double> values;
        Color fillColor;
        Color strokeColor;
        float strokeWidth;
        float fillOpacity;
        bool showPoints;
        float pointRadius;

        RadarChartSeries(const std::string& seriesName, const std::vector<double>& seriesValues)
                : name(seriesName), values(seriesValues),
                  fillColor(Color(0, 102, 204, 80)), strokeColor(Color(0, 102, 204, 255)),
                  strokeWidth(2.0f), fillOpacity(0.3f), showPoints(true), pointRadius(4.0f) {}
    };

// =============================================================================
// RADAR CHART ELEMENT CLASS
// =============================================================================

    class UltraCanvasRadarChartElement : public UltraCanvasChartElementBase {
    private:
        // Radar chart data
        std::vector<RadarChartAxis> axes;
        std::vector<RadarChartSeries> series;

        // Geometry (in element-local coordinates)
        Point2Df centerPoint;
        float maxRadius = 0.0f;
        float startAngle;
        bool clockwiseRotation;

        // Grid line width (showGrid / gridColor are inherited from the base class)
        float gridLineWidth;

        // Axis labels
        bool showAxisLabels;
        Color axisLabelColor;
        std::string axisLabelFont;
        float axisLabelFontSize;

        // Legend settings
        bool showLegend;
        Point2Df legendPosition;
        Color legendBackgroundColor;
        Color legendTextColor;

        // Animation (radar uses its own smoothstep grow-out animation,
        // independent of the base-class animation fields)
        bool enableAnimation;
        float animationProgress;
        std::chrono::steady_clock::time_point radarAnimationStart;
        int animationDurationMs;
        TimerId animationTimerId = 0;   // periodic frame driver while the grow-out animation runs

    public:
        UltraCanvasRadarChartElement(const std::string& id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height),
                  startAngle(-90.0f), clockwiseRotation(true),
                  gridLineWidth(1.0f),
                  showAxisLabels(true), axisLabelColor(Color(64, 64, 64, 255)),
                  axisLabelFont("Arial"), axisLabelFontSize(12.0f),
                  showLegend(true), legendPosition(Point2Df(0, 0)),
                  legendBackgroundColor(Color(255, 255, 255, 200)), legendTextColor(Color(64, 64, 64, 255)),
                  enableAnimation(false), animationProgress(1.0f), animationDurationMs(1000) {

            // Sensible base-class defaults for a radar chart
            showGrid = true;
            gridColor = Color(200, 200, 200, 255);
            showValueLabels = false;
            valueLabelColor = Color(64, 64, 64, 255);
            valueLabelFontSize = 10.0f;
            showAxes = false;          // base Cartesian axes are not used
            enableZoom = false;        // radar charts typically don't zoom
            enablePan = false;         // radar charts typically don't pan
            enableSelection = true;
        }

        ~UltraCanvasRadarChartElement() override;

        // =============================================================================
        // AXIS MANAGEMENT
        // =============================================================================

        void AddAxis(const std::string& name, double minValue = 0.0, double maxValue = 100.0) {
            axes.emplace_back(name, minValue, maxValue);
            RequestRedraw();
        }

        void SetAxisProperties(size_t axisIndex, const Color& color, bool showGridLines = true, int gridLevels = 5) {
            if (axisIndex >= axes.size()) return;
            axes[axisIndex].axisColor = color;
            axes[axisIndex].showGrid = showGridLines;
            axes[axisIndex].gridLevels = gridLevels;
            RequestRedraw();
        }

        void SetAxisRange(size_t axisIndex, double minValue, double maxValue) {
            if (axisIndex >= axes.size()) return;
            axes[axisIndex].minValue = minValue;
            axes[axisIndex].maxValue = maxValue;
            RequestRedraw();
        }

        size_t GetAxisCount() const { return axes.size(); }

        // =============================================================================
        // SERIES MANAGEMENT
        // =============================================================================

        void AddSeries(const std::string& name, const std::vector<double>& values,
                       const Color& fillColor = Color(0, 102, 204, 80),
                       const Color& strokeColor = Color(0, 102, 204, 255)) {
            // Pad / truncate to match the current axis count
            std::vector<double> adjustedValues = values;
            if (!axes.empty()) {
                adjustedValues.resize(axes.size(), 0.0);
            }

            series.emplace_back(name, adjustedValues);
            series.back().fillColor = fillColor;
            series.back().strokeColor = strokeColor;

            StartAnimation();
            RequestRedraw();
        }

        void SetSeriesProperties(size_t seriesIndex, const Color& fillColor, const Color& strokeColor,
                                 float strokeWidth = 2.0f, float fillOpacity = 0.3f) {
            if (seriesIndex >= series.size()) return;
            series[seriesIndex].fillColor = fillColor;
            series[seriesIndex].strokeColor = strokeColor;
            series[seriesIndex].strokeWidth = strokeWidth;
            series[seriesIndex].fillOpacity = fillOpacity;
            RequestRedraw();
        }

        void SetSeriesPointDisplay(size_t seriesIndex, bool showPoints, float pointRadius = 4.0f) {
            if (seriesIndex >= series.size()) return;
            series[seriesIndex].showPoints = showPoints;
            series[seriesIndex].pointRadius = pointRadius;
            RequestRedraw();
        }

        void ClearSeries() {
            series.clear();
            RequestRedraw();
        }

        size_t GetSeriesCount() const { return series.size(); }

        // =============================================================================
        // VISUAL CONFIGURATION
        // =============================================================================

        void SetGridDisplay(bool show, const Color& color = Color(200, 200, 200, 255), float lineWidth = 1.0f) {
            showGrid = show;
            gridColor = color;
            gridLineWidth = lineWidth;
            RequestRedraw();
        }

        bool GetShowGrid() const { return showGrid; }

        void SetAxisLabels(bool show, const Color& color = Color(64, 64, 64, 255),
                           const std::string& font = "Arial", float fontSize = 12.0f) {
            showAxisLabels = show;
            axisLabelColor = color;
            axisLabelFont = font;
            axisLabelFontSize = fontSize;
            RequestRedraw();
        }

        bool GetShowAxisLabels() const { return showAxisLabels; }

        void SetValueLabels(bool show, const Color& color = Color(64, 64, 64, 255), float fontSize = 10.0f) {
            showValueLabels = show;
            valueLabelColor = color;
            valueLabelFontSize = fontSize;
            RequestRedraw();
        }

        void SetRadarOrientation(float startAngleInDegrees, bool clockwise = true) {
            startAngle = startAngleInDegrees;
            clockwiseRotation = clockwise;
            RequestRedraw();
        }

        void SetLegendDisplay(bool show, const Point2Df& position = Point2Df(0, 0),
                              const Color& backgroundColor = Color(255, 255, 255, 200),
                              const Color& textColor = Color(64, 64, 64, 255)) {
            showLegend = show;
            legendPosition = position;
            legendBackgroundColor = backgroundColor;
            legendTextColor = textColor;
            RequestRedraw();
        }

        bool GetShowLegend() const { return showLegend; }

        // =============================================================================
        // ANIMATION SETTINGS
        // =============================================================================

        void SetAnimationEnabled(bool enabled, int durationMs = 1000) {
            enableAnimation = enabled;
            animationDurationMs = durationMs;
            if (enabled) {
                StartAnimation();
            } else {
                animationProgress = 1.0f;
            }
            RequestRedraw();
        }

        bool GetAnimationEnabled() const { return enableAnimation; }

        // Starts (or restarts) the grow-out animation. Defined in the .cpp because it
        // drives frames through the application timer system.
        void StartAnimation();

        // =============================================================================
        // CORE OVERRIDES
        // =============================================================================

        // Radar charts manage their own data (axes + series) rather than the base
        // IChartDataSource, so we override Render() to bypass the data-source gate.
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    private:
        // =============================================================================
        // INTERNAL CALCULATION METHODS
        // =============================================================================

        void RecalculateLayout();
        void UpdateAnimationProgress();
        void OnAnimationTick();      // periodic timer callback: advance progress and repaint
        void StopAnimationTimer();   // cancels the running frame-driver timer, if any

        // Convert axis index + value to (angleDegrees, radius)
        Point2Df ValueToPolarCoordinate(size_t axisIndex, double value) const;
        // Convert (angleDegrees, radius) to element-local screen coordinates
        Point2Df PolarToScreen(float angle, float radius) const;
        // Normalize a value to [0..1] within its axis range
        double NormalizeValue(size_t axisIndex, double value) const;

        // =============================================================================
        // RENDERING HELPERS
        // =============================================================================

        void DrawRadarGrid(IRenderContext* ctx);
        void DrawAxisLabels(IRenderContext* ctx);
        void DrawRadarSeries(IRenderContext* ctx);
        void DrawSeriesPoints(IRenderContext* ctx, size_t seriesIndex);
        void DrawValueLabels(IRenderContext* ctx);
        void DrawRadarLegend(IRenderContext* ctx);

        // =============================================================================
        // INTERACTION
        // =============================================================================

        std::pair<size_t, size_t> FindClosestSeriesPoint(const Point2Di& mousePos) const; // returns {series, axis}
        void ShowRadarTooltip(size_t seriesIndex, size_t axisIndex, const Point2Di& mousePos);
    };

// =============================================================================
// RADAR CHART FACTORY FUNCTION
// =============================================================================

    inline std::shared_ptr<UltraCanvasRadarChartElement> CreateRadarChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasRadarChartElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
