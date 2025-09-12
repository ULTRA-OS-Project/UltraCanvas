// UltraCanvasChartDataStructures.h
// Essential data structures for chart rendering
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <fstream>

namespace UltraCanvas {

//    enum class ChartType {
//        Line, Bar, Area, Scatter, Pie, Histogram, BoxPlot, Surface3D,
//        Heatmap, Radar, Bubble, CandleStick, Gantt
//    };

// =============================================================================
// DATA STRUCTURES
// =============================================================================

    struct ChartDataPoint {
        double x, y, z;
        std::string label;
        std::string category;
        double value;
        Color color;  // Override color for this point

        ChartDataPoint(double x_val, double y_val, double z_val = 0.0,
                       const std::string& lbl = "", double val = 0.0)
                : x(x_val), y(y_val), z(z_val), label(lbl), value(val), color(0) {}
    };

// Base interface for all data sources
    class IChartDataSource {
    public:
        virtual ~IChartDataSource() = default;
        virtual size_t GetPointCount() const = 0;
        virtual ChartDataPoint GetPoint(size_t index) const = 0;
        virtual bool SupportsStreaming() const = 0;
        virtual void LoadFromCSV(const std::string& filePath) = 0;
        virtual void LoadFromArray(const std::vector<ChartDataPoint>& data) = 0;
    };

// Standard vector-based data container
    class ChartDataVector : public IChartDataSource {
    private:
        std::vector<ChartDataPoint> data;

    public:
        size_t GetPointCount() const override { return data.size(); }
        ChartDataPoint GetPoint(size_t index) const override { return data[index]; }
        bool SupportsStreaming() const override { return false; }
        void LoadFromCSV(const std::string& filePath) override;
        void LoadFromArray(const std::vector<ChartDataPoint>& newData) override { data = newData; }
        void AddPoint(const ChartDataPoint& point) { data.push_back(point); }
        void Clear() { data.clear(); }
    protected:
        ChartDataPoint ParseCSVLine(const std::string& line);
    };


    class ChartDataStream : public IChartDataSource {
    private:
        std::string filePath;
        mutable std::ifstream fileStream;
        mutable std::vector<ChartDataPoint> cache;
        mutable size_t cacheStartIndex = 0;
        static const size_t CHUNK_SIZE = 10000;  // Load 10K points per chunk
        mutable size_t totalPoints = 0;
        mutable bool pointCountCalculated = false;

    public:
        ChartDataStream(const std::string& path) : filePath(path) {}

        size_t GetPointCount() const override;
        ChartDataPoint GetPoint(size_t index) const override;
        bool SupportsStreaming() const override { return true; }
        void LoadFromCSV(const std::string& path) override;
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override;

    private:
        void CalculatePointCount() const;
        void LoadChunk(size_t targetIndex) const;
        ChartDataPoint ParseCSVLine(const std::string& line) const;
    };

// =============================================================================
// CHART RENDERING STRUCTURES
// =============================================================================

    struct ChartPlotArea {
        float x, y, width, height;

        ChartPlotArea() : x(0), y(0), width(0), height(0) {}
        ChartPlotArea(float x_pos, float y_pos, float w, float h) : x(x_pos), y(y_pos), width(w), height(h) {}

        // Helper methods
        float GetRight() const { return x + width; }
        float GetBottom() const { return y + height; }
        Point2Df GetCenter() const { return Point2Df(x + (width / 2.0f), y + (height / 2.0f)); }
        bool Contains(float px, float py) const {
            return px >= x && px < GetRight() && py >= y && py < GetBottom();
        }

        // Convert to UltraCanvas Rect2D
        Rect2Df ToRect2D() const {
            return Rect2Df(x, y, width, height);
        }
    };

    struct ChartDataBounds {
        double minX, maxX, minY, maxY, minZ, maxZ;
        bool hasData;

        ChartDataBounds() : minX(0), maxX(0), minY(0), maxY(0), minZ(0), maxZ(0), hasData(false) {}

        ChartDataBounds(double min_x, double max_x, double min_y, double max_y, double min_z = 0, double max_z = 0)
                : minX(min_x), maxX(max_x), minY(min_y), maxY(max_y), minZ(min_z), maxZ(max_z), hasData(true) {}

        // Helper methods
        double GetXRange() const { return maxX - minX; }
        double GetYRange() const { return maxY - minY; }
        double GetZRange() const { return maxZ - minZ; }

        void Expand(double x, double y, double z = 0);
        void AddMargin(double marginPercent = 0.05);
    };

// =============================================================================
// COORDINATE TRANSFORMATION
// =============================================================================

    class ChartCoordinateTransform {
    private:
        ChartPlotArea plotArea;
        ChartDataBounds dataBounds;

    public:
        ChartCoordinateTransform(const ChartPlotArea& plot, const ChartDataBounds& bounds)
                : plotArea(plot), dataBounds(bounds) {}

        // Transform data coordinates to screen coordinates
        float DataToScreenX(double dataX) const {
            if (dataBounds.GetXRange() == 0) return plotArea.x;
            return plotArea.x + (dataX - dataBounds.minX) / dataBounds.GetXRange() * plotArea.width;
        }

        float DataToScreenY(double dataY) const {
            if (dataBounds.GetYRange() == 0) return plotArea.y + (plotArea.height / 2.0);
            // Note: Y is flipped because screen coordinates have origin at top-left
            return plotArea.y + plotArea.height - (dataY - dataBounds.minY) / dataBounds.GetYRange() * plotArea.height;
        }

        Point2Df DataToScreen(double dataX, double dataY) const {
            return Point2Df(DataToScreenX(dataX), DataToScreenY(dataY));
        }

        // Transform screen coordinates back to data coordinates
        double ScreenToDataX(float screenX) const {
            if (plotArea.width == 0) return dataBounds.minX;
            return dataBounds.minX + (screenX - plotArea.x) / plotArea.width * dataBounds.GetXRange();
        }

        double ScreenToDataY(float screenY) const {
            if (plotArea.height == 0) return dataBounds.minY;
            // Note: Y is flipped
            return dataBounds.minY + (plotArea.y + plotArea.height - screenY) / plotArea.height * dataBounds.GetYRange();
        }

        Point2Df ScreenToData(float screenX, float screenY) const {
            return Point2Df(static_cast<float>(ScreenToDataX(screenX)), static_cast<float>(ScreenToDataY(screenY)));
        }
    };
/*
// =============================================================================
// CHART DRAWING STATE
// =============================================================================

    struct ChartDrawingState {
        // Current drawing context
        IRenderContext* renderContext;
        ChartCoordinateTransform* transform;

        // Current styles
        Color fillColor;
        Color strokeColor;
        float strokeWidth;
        std::string fontFamily;
        float fontSize;
        Color textColor;

        ChartDrawingState()
                : renderContext(nullptr), transform(nullptr),
                  fillColor(Colors::Blue), strokeColor(Colors::Black), strokeWidth(1.0f),
                  fontFamily("Arial"), fontSize(12.0f), textColor(Colors::Black) {}

        ChartDrawingState(IRenderContext* ctx, ChartCoordinateTransform* trans)
                : renderContext(ctx), transform(trans),
                  fillColor(Colors::Blue), strokeColor(Colors::Black), strokeWidth(1.0f),
                  fontFamily("Arial"), fontSize(12.0f), textColor(Colors::Black) {}
    };


// =============================================================================
// CHART RENDERING HELPERS
// =============================================================================

    class ChartRenderingHelpers {
    public:
        // Calculate nice axis tick values
        static std::vector<double> CalculateAxisTicks(double minVal, double maxVal, int targetTickCount = 5) {
            std::vector<double> ticks;

            if (maxVal <= minVal) {
                ticks.push_back(minVal);
                return ticks;
            }

            double range = maxVal - minVal;
            double roughStep = range / (targetTickCount - 1);

            // Find nice step size
            double magnitude = std::pow(10, std::floor(std::log10(roughStep)));
            double normalizedStep = roughStep / magnitude;

            double niceStep;
            if (normalizedStep <= 1) niceStep = 1;
            else if (normalizedStep <= 2) niceStep = 2;
            else if (normalizedStep <= 5) niceStep = 5;
            else niceStep = 10;

            niceStep *= magnitude;

            // Generate ticks
            double start = std::ceil(minVal / niceStep) * niceStep;
            for (double tick = start; tick <= maxVal + niceStep * 0.01; tick += niceStep) {
                ticks.push_back(tick);
            }

            return ticks;
        }

        // Format number for axis labels
        static std::string FormatAxisLabel(double value, int decimalPlaces = -1) {
            if (decimalPlaces < 0) {
                // Auto-determine decimal places
                if (std::abs(value) >= 1000000) {
                    return std::to_string(static_cast<int>(value / 1000000)) + "M";
                } else if (std::abs(value) >= 1000) {
                    return std::to_string(static_cast<int>(value / 1000)) + "K";
                } else if (std::abs(value - std::round(value)) < 0.001) {
                    return std::to_string(static_cast<int>(value));
                } else {
                    return std::to_string(value).substr(0, 6); // Limit to 6 characters
                }
            } else {
                // Fixed decimal places
                std::string result = std::to_string(value);
                size_t decimal = result.find('.');
                if (decimal != std::string::npos) {
                    result = result.substr(0, decimal + decimalPlaces + 1);
                }
                return result;
            }
        }

        // Color interpolation for gradients
        static Color InterpolateColor(const Color& color1, const Color& color2, float t) {
            t = std::clamp(t, 0.0f, 1.0f);

            return Color(
                    static_cast<uint8_t>(color1.r + (color2.r - color1.r) * t),
                    static_cast<uint8_t>(color1.g + (color2.g - color1.g) * t),
                    static_cast<uint8_t>(color1.b + (color2.b - color1.b) * t),
                    static_cast<uint8_t>(color1.a + (color2.a - color1.a) * t)
            );
        }

        // Generate color palette
        static std::vector<Color> GenerateColorPalette(int count) {
            std::vector<Color> palette;

            // Standard chart colors
            std::vector<Color> baseColors = {
                    Color(0, 102, 204),   // Blue
                    Color(204, 102, 0),   // Orange
                    Color(0, 204, 102),   // Green
                    Color(204, 0, 102),   // Pink
                    Color(102, 0, 204),   // Purple
                    Color(204, 204, 0),   // Yellow
                    Color(102, 204, 0),   // Lime
                    Color(0, 204, 204)    // Cyan
            };

            for (int i = 0; i < count; ++i) {
                if (i < baseColors.size()) {
                    palette.push_back(baseColors[i]);
                } else {
                    // Generate additional colors by varying hue
                    float hue = (i * 360.0f / count);
                    palette.push_back(HSVToRGB(hue, 0.8f, 0.8f));
                }
            }

            return palette;
        }

    private:
        // Convert HSV to RGB
        static Color HSVToRGB(float hue, float saturation, float value) {
            float c = value * saturation;
            float x = c * (1 - std::abs(std::fmod(hue / 60.0f, 2) - 1));
            float m = value - c;

            float r, g, b;

            if (hue >= 0 && hue < 60) {
                r = c; g = x; b = 0;
            } else if (hue >= 60 && hue < 120) {
                r = x; g = c; b = 0;
            } else if (hue >= 120 && hue < 180) {
                r = 0; g = c; b = x;
            } else if (hue >= 180 && hue < 240) {
                r = 0; g = x; b = c;
            } else if (hue >= 240 && hue < 300) {
                r = x; g = 0; b = c;
            } else {
                r = c; g = 0; b = x;
            }

            return Color(
                    static_cast<uint8_t>((r + m) * 255),
                    static_cast<uint8_t>((g + m) * 255),
                    static_cast<uint8_t>((b + m) * 255)
            );
        }
    };
*/
} // namespace UltraCanvas