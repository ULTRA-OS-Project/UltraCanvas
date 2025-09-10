// UltraCanvasChartRenderer.cpp
// Implementation of unified charting system with streaming data support
// Version: 1.0.1
// Last Modified: 2025-09-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartRenderer.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace UltraCanvas {

// =============================================================================
// DATA STREAMING SYSTEM FOR HIGH-VOLUME CHARTS
// =============================================================================

// Streaming data container for large datasets (1M+ points)
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

        size_t GetPointCount() const override {
            if (!pointCountCalculated) {
                CalculatePointCount();
                pointCountCalculated = true;
            }
            return totalPoints;
        }

        ChartDataPoint GetPoint(size_t index) const override {
            // Check if point is in current cache
            if (index >= cacheStartIndex && index < cacheStartIndex + cache.size()) {
                return cache[index - cacheStartIndex];
            }

            // Load appropriate chunk
            LoadChunk(index);

            if (index >= cacheStartIndex && index < cacheStartIndex + cache.size()) {
                return cache[index - cacheStartIndex];
            }

            // Fallback - return empty point
            return ChartDataPoint(0, 0, 0);
        }

        bool SupportsStreaming() const override { return true; }

        void LoadFromCSV(const std::string& path) override {
            filePath = path;
            pointCountCalculated = false;
            cache.clear();
            cacheStartIndex = 0;
        }

        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            // Not supported for streaming - use ChartDataVector instead
            throw std::runtime_error("ChartDataStream doesn't support LoadFromArray - use ChartDataVector");
        }

    private:
        void CalculatePointCount() const {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                totalPoints = 0;
                return;
            }

            std::string line;
            totalPoints = 0;

            // Skip header if present
            if (std::getline(file, line)) {
                // Check if first line looks like a header
                if (line.find("x") != std::string::npos || line.find("y") != std::string::npos) {
                    // Skip header
                } else {
                    totalPoints = 1;  // First line is data
                }
            }

            while (std::getline(file, line)) {
                if (!line.empty()) totalPoints++;
            }
        }

        void LoadChunk(size_t targetIndex) const {
            std::ifstream file(filePath);
            if (!file.is_open()) return;

            // Calculate chunk start
            cacheStartIndex = (targetIndex / CHUNK_SIZE) * CHUNK_SIZE;
            cache.clear();
            cache.reserve(CHUNK_SIZE);

            std::string line;
            size_t currentIndex = 0;

            // Skip header if present
            if (std::getline(file, line)) {
                if (line.find("x") == std::string::npos && line.find("y") == std::string::npos) {
                    // First line is data, process it
                    if (currentIndex >= cacheStartIndex && cache.size() < CHUNK_SIZE) {
                        cache.push_back(ParseCSVLine(line));
                    }
                    currentIndex++;
                }
            }

            // Skip to target chunk
            while (currentIndex < cacheStartIndex && std::getline(file, line)) {
                currentIndex++;
            }

            // Load chunk data
            while (cache.size() < CHUNK_SIZE && std::getline(file, line)) {
                if (!line.empty()) {
                    cache.push_back(ParseCSVLine(line));
                }
            }
        }

        ChartDataPoint ParseCSVLine(const std::string& line) const {
            std::stringstream ss(line);
            std::string cell;
            std::vector<std::string> values;

            while (std::getline(ss, cell, ',')) {
                // Trim whitespace
                cell.erase(0, cell.find_first_not_of(" \t"));
                cell.erase(cell.find_last_not_of(" \t") + 1);
                values.push_back(cell);
            }

            if (values.size() >= 2) {
                double x = std::stod(values[0]);
                double y = std::stod(values[1]);
                double z = values.size() > 2 ? std::stod(values[2]) : 0.0;
                std::string label = values.size() > 3 ? values[3] : "";

                return ChartDataPoint(x, y, z, label);
            }

            return ChartDataPoint(0, 0, 0);
        }
    };

// =============================================================================
// CHART DATA VECTOR IMPLEMENTATION
// =============================================================================

    void ChartDataVector::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open CSV file: " + filePath);
        }

        data.clear();
        std::string line;
        bool skipHeader = false;

        // Check for header
        if (std::getline(file, line)) {
            if (line.find("x") != std::string::npos || line.find("y") != std::string::npos) {
                skipHeader = true;
            } else {
                // Process first line as data
                data.push_back(ParseCSVLine(line));
            }
        }

        while (std::getline(file, line)) {
            if (!line.empty()) {
                data.push_back(ParseCSVLine(line));
            }
        }
    }

    ChartDataPoint ChartDataVector::ParseCSVLine(const std::string& line) {
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> values;

        while (std::getline(ss, cell, ',')) {
            // Trim whitespace
            cell.erase(0, cell.find_first_not_of(" \t"));
            cell.erase(cell.find_last_not_of(" \t") + 1);
            values.push_back(cell);
        }

        if (values.size() >= 2) {
            double x = std::stod(values[0]);
            double y = std::stod(values[1]);
            double z = values.size() > 2 ? std::stod(values[2]) : 0.0;
            std::string label = values.size() > 3 ? values[3] : "";

            return ChartDataPoint(x, y, z, label);
        }

        return ChartDataPoint(0, 0, 0);
    }

// =============================================================================
// CHART RENDERER IMPLEMENTATION
// =============================================================================

    bool UltraCanvasChartRenderer::RenderChart(const ChartConfiguration& config,
                                               int width, int height,
                                               void* renderTarget) {
        if (!config.dataSource || config.dataSource->GetPointCount() == 0) {
            return false;
        }

        // Switch based on chart type
        switch (config.type) {
            case ChartType::Line:
                return RenderLineChart(config, width, height, renderTarget);
            case ChartType::Bar:
                return RenderBarChart(config, width, height, renderTarget);
            case ChartType::Scatter:
                return RenderScatterPlot(config, width, height, renderTarget);
            case ChartType::Area:
                return RenderAreaChart(config, width, height, renderTarget);
            case ChartType::Surface3D:
                return Render3DSurface(config, width, height, renderTarget);
            case ChartType::Heatmap:
                return RenderHeatmap(config, width, height, renderTarget);
            default:
                return false;
        }
    }

// =============================================================================
// SPECIFIC CHART TYPE IMPLEMENTATIONS
// =============================================================================

    bool UltraCanvasChartRenderer::RenderLineChart(const ChartConfiguration& config,
                                                   int width, int height,
                                                   void* renderTarget) {
        // Calculate plot area (leave space for axes, titles, legend)
        PlotArea plotArea = CalculatePlotArea(config, width, height);

        // Draw background and grid
        DrawChartBackground(config, plotArea, renderTarget);
        DrawGrid(config, plotArea, renderTarget);

        // Calculate data scaling
        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);

        // Draw axes
        DrawAxes(config, plotArea, bounds, renderTarget);

        // Draw line series
        DrawLineSeries(config, plotArea, bounds, renderTarget);

        // Draw trend lines if any
        for (const auto& trendLine : config.trendLines) {
            DrawTrendLine(config, trendLine, plotArea, bounds, renderTarget);
        }

        // Draw axis highlights
        DrawAxisHighlights(config, plotArea, bounds, renderTarget);

        // Draw titles and legend
        DrawTitles(config, width, height, renderTarget);
        DrawLegend(config, plotArea, renderTarget);

        return true;
    }

    bool UltraCanvasChartRenderer::RenderScatterPlot(const ChartConfiguration& config,
                                                     int width, int height,
                                                     void* renderTarget) {
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DrawChartBackground(config, plotArea, renderTarget);
        DrawGrid(config, plotArea, renderTarget);

        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);
        DrawAxes(config, plotArea, bounds, renderTarget);

        // Optimized scatter point rendering for large datasets
        if (config.dataSource->SupportsStreaming() &&
            config.dataSource->GetPointCount() > 100000) {
            DrawScatterPointsOptimized(config, plotArea, bounds, renderTarget);
        } else {
            DrawScatterPointsStandard(config, plotArea, bounds, renderTarget);
        }

        // Draw trend lines and highlights
        for (const auto& trendLine : config.trendLines) {
            DrawTrendLine(config, trendLine, plotArea, bounds, renderTarget);
        }
        DrawAxisHighlights(config, plotArea, bounds, renderTarget);

        DrawTitles(config, width, height, renderTarget);
        DrawLegend(config, plotArea, renderTarget);

        return true;
    }

    bool UltraCanvasChartRenderer::RenderBarChart(const ChartConfiguration& config,
                                                  int width, int height,
                                                  void* renderTarget) {
        PlotArea plotArea = CalculatePlotArea(config, width, height);
        DrawChartBackground(config, plotArea, renderTarget);
        DrawGrid(config, plotArea, renderTarget);

        DataBounds bounds = CalculateDataBounds(config.dataSource.get(), config);
        DrawAxes(config, plotArea, bounds, renderTarget);

        // Draw bars with texture/styling
        DrawBarsWithStyling(config, plotArea, bounds, renderTarget);

        DrawAxisHighlights(config, plotArea, bounds, renderTarget);
        DrawTitles(config, width, height, renderTarget);
        DrawLegend(config, plotArea, renderTarget);

        return true;
    }

// =============================================================================
// HELPER FUNCTIONS FOR CHART CREATION
// =============================================================================

    ChartConfiguration UltraCanvasChartRenderer::CreateLineChart(
            std::shared_ptr<IChartDataSource> data, const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Line;
        config.dataSource = data;
        config.title = title;
        config.enableAnimations = true;
        config.enableTooltips = true;
        return config;
    }

    ChartConfiguration UltraCanvasChartRenderer::CreateScatterPlot(
            std::shared_ptr<IChartDataSource> data, const std::string& title) {
        ChartConfiguration config;
        config.type = ChartType::Scatter;
        config.dataSource = data;
        config.title = title;
        config.enableZoom = true;
        config.enablePan = true;

        // Optimize for large datasets
        if (data && data->GetPointCount() > 50000) {
            config.enableAnimations = false;  // Disable for performance
        }

        return config;
    }

// =============================================================================
// DATA MANAGEMENT HELPERS
// =============================================================================

    std::shared_ptr<ChartDataVector> UltraCanvasChartRenderer::LoadCSVData(const std::string& filePath) {
        auto data = std::make_shared<ChartDataVector>();

        try {
            data->LoadFromCSV(filePath);
            return data;
        } catch (const std::exception& e) {
            std::cerr << "Error loading CSV: " << e.what() << std::endl;
            return nullptr;
        }
    }

    std::shared_ptr<ChartDataVector> UltraCanvasChartRenderer::CreateFromArray(
            const std::vector<ChartDataPoint>& dataPoints) {
        auto data = std::make_shared<ChartDataVector>();
        data->LoadFromArray(dataPoints);
        return data;
    }

// Smart data source selection based on size
    std::shared_ptr<IChartDataSource> UltraCanvasChartRenderer::CreateOptimalDataSource(
            const std::string& filePath) {

        // Quick file size check
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) return nullptr;

        auto fileSize = file.tellg();
        file.close();

        // Use streaming for large files (>10MB)
        if (fileSize > 10 * 1024 * 1024) {
            auto streamData = std::make_shared<ChartDataStream>(filePath);
            std::cout << "Large dataset detected (" << fileSize / (1024*1024)
                      << "MB) - using streaming data source" << std::endl;
            return streamData;
        } else {
            return LoadCSVData(filePath);
        }
    }

// =============================================================================
// STYLING HELPERS
// =============================================================================

    void UltraCanvasChartRenderer::AddAxisHighlight(ChartConfiguration& config,
                                                    const std::string& axis,
                                                    double position, uint32_t color,
                                                    const std::string& label) {
        AxisHighlight highlight(position, color, 2.0f, label);

        if (axis == "x" || axis == "X") {
            config.xAxis.highlights.push_back(highlight);
        } else if (axis == "y" || axis == "Y") {
            config.yAxis.highlights.push_back(highlight);
        } else if (axis == "z" || axis == "Z") {
            config.zAxis.highlights.push_back(highlight);
        }
    }

    void UltraCanvasChartRenderer::AddTrendLine(ChartConfiguration& config,
                                                TrendLine::Type type, uint32_t color) {
        TrendLine trend(type, color);
        config.trendLines.push_back(trend);
    }

    void UltraCanvasChartRenderer::SetBarTexture(ChartConfiguration& config,
                                                 BarStyle::TextureType texture,
                                                 uint32_t primaryColor,
                                                 uint32_t secondaryColor) {
        config.barStyle.texture = texture;
        config.barStyle.primaryColor = primaryColor;
        config.barStyle.secondaryColor = secondaryColor;
    }

// =============================================================================
// PLUGIN REGISTRATION
// =============================================================================

    class UltraCanvasChartPlugin : public IGraphicsPlugin {
    public:
        std::string GetPluginName() const override {
            return "UltraCanvas Chart Renderer";
        }

        std::string GetPluginVersion() const override {
            return "1.0.1";
        }

        std::vector<std::string> GetSupportedExtensions() const override {
            return {"chart", "csv", "data", "tsv"};
        }

        bool CanHandle(const std::string& filePath) const override {
            auto extensions = GetSupportedExtensions();
            std::string ext = GetFileExtension(filePath);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
        }

        std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
            // Create chart element from data file
            auto dataSource = UltraCanvasChartRenderer::CreateOptimalDataSource(filePath);
            if (!dataSource) return nullptr;

            // Auto-detect chart type based on data characteristics
            ChartType detectedType = DetectChartType(dataSource.get());

            // Create appropriate chart configuration
            ChartConfiguration config;
            config.type = detectedType;
            config.dataSource = dataSource;
            config.title = ExtractTitleFromFilename(filePath);

            // Return chart element (would need to implement UltraCanvasChartElement)
            return CreateChartElement(config);
        }

    private:
        ChartType DetectChartType(IChartDataSource* data) {
            size_t pointCount = data->GetPointCount();

            // Large datasets -> scatter plot
            if (pointCount > 10000) {
                return ChartType::Scatter;
            }

            // Check if data has discrete categories -> bar chart
            // Check if data is time series -> line chart
            // Default to line chart
            return ChartType::Line;
        }

        std::string ExtractTitleFromFilename(const std::string& filePath) {
            size_t lastSlash = filePath.find_last_of("/\\");
            size_t lastDot = filePath.find_last_of(".");

            if (lastSlash == std::string::npos) lastSlash = 0;
            else lastSlash++;

            if (lastDot == std::string::npos) lastDot = filePath.length();

            return filePath.substr(lastSlash, lastDot - lastSlash);
        }
    };

// Register the chart plugin
    bool RegisterUltraCanvasChartPlugin() {
        auto plugin = std::make_shared<UltraCanvasChartPlugin>();
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(plugin);
        return true;
    }

} // namespace UltraCanvas