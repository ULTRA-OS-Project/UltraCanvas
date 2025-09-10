// UltraCanvasChartSpecialized.h
// Advanced chart types with performance optimization for large datasets
// Version: 1.0.1
// Last Modified: 2025-09-06
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartRenderer.h"
#include <thread>
#include <future>
#include <mutex>

namespace UltraCanvas {

// =============================================================================
// PERFORMANCE-OPTIMIZED SCATTER PLOT FOR MILLIONS OF POINTS
// =============================================================================

    class HighPerformanceScatterRenderer {
    private:
        struct QuadTreeNode {
            double minX, maxX, minY, maxY;
            std::vector<size_t> pointIndices;
            std::unique_ptr<QuadTreeNode> children[4];
            bool isLeaf = true;

            QuadTreeNode(double mx, double Mx, double my, double My)
                    : minX(mx), maxX(Mx), minY(my), maxY(My) {}
        };

        std::unique_ptr<QuadTreeNode> spatialIndex;
        static const size_t MAX_POINTS_PER_NODE = 100;

    public:
        // Build spatial index for fast culling and LOD
        void BuildSpatialIndex(IChartDataSource* dataSource, const DataBounds& bounds) {
            spatialIndex = std::make_unique<QuadTreeNode>(
                    bounds.minX, bounds.maxX, bounds.minY, bounds.maxY);

            // Multi-threaded index building for large datasets
            if (dataSource->GetPointCount() > 100000) {
                BuildIndexMultiThreaded(dataSource);
            } else {
                BuildIndexSingleThreaded(dataSource);
            }
        }

        // Render with Level of Detail based on zoom level
        void RenderWithLOD(const ChartConfiguration& config,
                           const PlotArea& plotArea,
                           const DataBounds& viewBounds,
                           double zoomLevel,
                           void* renderTarget) {

            // Calculate appropriate point size based on zoom
            float pointSize = CalculatePointSize(zoomLevel, config.dataSource->GetPointCount());

            // Use different rendering strategies based on point count and zoom
            if (zoomLevel < 0.1) {
                // Very zoomed out - use density visualization
                RenderDensityVisualization(config, plotArea, viewBounds, renderTarget);
            } else if (config.dataSource->GetPointCount() > 1000000) {
                // Large dataset - use GPU-accelerated rendering
                RenderGPUAccelerated(config, plotArea, viewBounds, pointSize, renderTarget);
            } else {
                // Standard rendering with culling
                RenderWithCulling(config, plotArea, viewBounds, pointSize, renderTarget);
            }
        }

    private:
        void BuildIndexMultiThreaded(IChartDataSource* dataSource) {
            const size_t numThreads = std::thread::hardware_concurrency();
            const size_t pointsPerThread = dataSource->GetPointCount() / numThreads;

            std::vector<std::future<void>> futures;
            std::mutex indexMutex;

            for (size_t i = 0; i < numThreads; ++i) {
                size_t start = i * pointsPerThread;
                size_t end = (i == numThreads - 1) ? dataSource->GetPointCount() : start + pointsPerThread;

                futures.push_back(std::async(std::launch::async, [this, dataSource, start, end, &indexMutex]() {
                    for (size_t idx = start; idx < end; ++idx) {
                        auto point = dataSource->GetPoint(idx);

                        std::lock_guard<std::mutex> lock(indexMutex);
                        InsertPointIntoQuadTree(spatialIndex.get(), idx, point.x, point.y);
                    }
                }));
            }

            // Wait for all threads to complete
            for (auto& future : futures) {
                future.wait();
            }
        }

        void RenderDensityVisualization(const ChartConfiguration& config,
                                        const PlotArea& plotArea,
                                        const DataBounds& viewBounds,
                                        void* renderTarget) {
            // Create density heatmap for extremely zoomed out views
            const int gridSize = 64;
            std::vector<std::vector<int>> densityGrid(gridSize, std::vector<int>(gridSize, 0));

            // Count points in each grid cell
            for (size_t i = 0; i < config.dataSource->GetPointCount(); ++i) {
                auto point = config.dataSource->GetPoint(i);

                if (point.x >= viewBounds.minX && point.x <= viewBounds.maxX &&
                    point.y >= viewBounds.minY && point.y <= viewBounds.maxY) {

                    int gridX = (int)((point.x - viewBounds.minX) / (viewBounds.maxX - viewBounds.minX) * gridSize);
                    int gridY = (int)((point.y - viewBounds.minY) / (viewBounds.maxY - viewBounds.minY) * gridSize);

                    gridX = std::clamp(gridX, 0, gridSize - 1);
                    gridY = std::clamp(gridY, 0, gridSize - 1);

                    densityGrid[gridY][gridX]++;
                }
            }

            // Render density grid as colored rectangles
            DrawDensityGrid(densityGrid, plotArea, renderTarget);
        }

        void RenderGPUAccelerated(const ChartConfiguration& config,
                                  const PlotArea& plotArea,
                                  const DataBounds& viewBounds,
                                  float pointSize,
                                  void* renderTarget) {
            // Prepare vertex buffer for GPU rendering
            std::vector<float> vertices;
            std::vector<uint32_t> colors;

            vertices.reserve(config.dataSource->GetPointCount() * 2);
            colors.reserve(config.dataSource->GetPointCount());

            // Batch process points for GPU upload
            const size_t batchSize = 10000;
            for (size_t start = 0; start < config.dataSource->GetPointCount(); start += batchSize) {
                size_t end = std::min(start + batchSize, config.dataSource->GetPointCount());

                for (size_t i = start; i < end; ++i) {
                    auto point = config.dataSource->GetPoint(i);

                    // Frustum culling
                    if (point.x >= viewBounds.minX && point.x <= viewBounds.maxX &&
                        point.y >= viewBounds.minY && point.y <= viewBounds.maxY) {

                        // Transform to screen coordinates
                        float screenX = TransformToScreenX(point.x, viewBounds, plotArea);
                        float screenY = TransformToScreenY(point.y, viewBounds, plotArea);

                        vertices.push_back(screenX);
                        vertices.push_back(screenY);
                        colors.push_back(point.color != 0 ? point.color : 0xFF0080FF);
                    }
                }
            }

            // Upload to GPU and render as point sprites
            RenderPointSprites(vertices, colors, pointSize, renderTarget);
        }
    };

// =============================================================================
// REAL-TIME FINANCIAL CHART RENDERER
// =============================================================================

    class FinancialChartRenderer {
    public:
        struct CandlestickData {
            double timestamp;
            double open, high, low, close;
            double volume;
            std::string symbol;

            CandlestickData(double time, double o, double h, double l, double c, double vol = 0.0)
                    : timestamp(time), open(o), high(h), low(l), close(c), volume(vol) {}
        };

        struct TechnicalIndicator {
            enum class Type { SMA, EMA, Bollinger, RSI, MACD, Stochastic };

            Type type;
            std::vector<double> values;
            std::vector<double> upperBand;  // For Bollinger bands
            std::vector<double> lowerBand;
            uint32_t color;
            std::string name;

            TechnicalIndicator(Type t, uint32_t col, const std::string& n)
                    : type(t), color(col), name(n) {}
        };

    private:
        std::vector<CandlestickData> candleData;
        std::vector<TechnicalIndicator> indicators;

    public:
        void LoadCandlestickData(const std::vector<CandlestickData>& data) {
            candleData = data;
            // Sort by timestamp
            std::sort(candleData.begin(), candleData.end(),
                      [](const CandlestickData& a, const CandlestickData& b) {
                          return a.timestamp < b.timestamp;
                      });
        }

        void AddTechnicalIndicator(TechnicalIndicator::Type type,
                                   const std::vector<double>& params,
                                   uint32_t color,
                                   const std::string& name) {
            TechnicalIndicator indicator(type, color, name);

            switch (type) {
                case TechnicalIndicator::Type::SMA:
                    CalculateSMA(indicator, (int)params[0]);
                    break;
                case TechnicalIndicator::Type::EMA:
                    CalculateEMA(indicator, (int)params[0]);
                    break;
                case TechnicalIndicator::Type::Bollinger:
                    CalculateBollingerBands(indicator, (int)params[0], params[1]);
                    break;
                case TechnicalIndicator::Type::RSI:
                    CalculateRSI(indicator, (int)params[0]);
                    break;
                case TechnicalIndicator::Type::MACD:
                    CalculateMACD(indicator, (int)params[0], (int)params[1], (int)params[2]);
                    break;
            }

            indicators.push_back(indicator);
        }

        void RenderFinancialChart(const ChartConfiguration& config,
                                  const PlotArea& plotArea,
                                  void* renderTarget) {
            if (candleData.empty()) return;

            // Calculate time and price bounds
            DataBounds timeBounds = CalculateTimeBounds();
            DataBounds priceBounds = CalculatePriceBounds();

            // Draw candlesticks
            DrawCandlesticks(plotArea, timeBounds, priceBounds, renderTarget);

            // Draw technical indicators
            for (const auto& indicator : indicators) {
                DrawTechnicalIndicator(indicator, plotArea, timeBounds, priceBounds, renderTarget);
            }

            // Draw volume bars (if space available)
            if (plotArea.height > 400) {
                PlotArea volumeArea = {
                        plotArea.x,
                        plotArea.y + plotArea.height - 100,
                        plotArea.width,
                        80
                };
                DrawVolumeIndicator(volumeArea, timeBounds, renderTarget);
            }
        }

    private:
        void CalculateSMA(TechnicalIndicator& indicator, int period) {
            indicator.values.clear();
            indicator.values.reserve(candleData.size());

            for (size_t i = 0; i < candleData.size(); ++i) {
                if (i < period - 1) {
                    indicator.values.push_back(0.0);  // Not enough data
                } else {
                    double sum = 0.0;
                    for (int j = 0; j < period; ++j) {
                        sum += candleData[i - j].close;
                    }
                    indicator.values.push_back(sum / period);
                }
            }
        }

        void CalculateBollingerBands(TechnicalIndicator& indicator, int period, double stdDevMultiplier) {
            CalculateSMA(indicator, period);  // Calculate middle band (SMA)

            indicator.upperBand.clear();
            indicator.lowerBand.clear();
            indicator.upperBand.reserve(candleData.size());
            indicator.lowerBand.reserve(candleData.size());

            for (size_t i = 0; i < candleData.size(); ++i) {
                if (i < period - 1) {
                    indicator.upperBand.push_back(0.0);
                    indicator.lowerBand.push_back(0.0);
                } else {
                    // Calculate standard deviation
                    double mean = indicator.values[i];
                    double variance = 0.0;

                    for (int j = 0; j < period; ++j) {
                        double diff = candleData[i - j].close - mean;
                        variance += diff * diff;
                    }

                    double stdDev = std::sqrt(variance / period);
                    indicator.upperBand.push_back(mean + stdDevMultiplier * stdDev);
                    indicator.lowerBand.push_back(mean - stdDevMultiplier * stdDev);
                }
            }
        }

        void DrawCandlesticks(const PlotArea& plotArea,
                              const DataBounds& timeBounds,
                              const DataBounds& priceBounds,
                              void* renderTarget) {

            double timeRange = timeBounds.maxX - timeBounds.minX;
            double priceRange = priceBounds.maxY - priceBounds.minY;

            for (const auto& candle : candleData) {
                // Calculate screen coordinates
                double timePercent = (candle.timestamp - timeBounds.minX) / timeRange;
                double x = plotArea.x + timePercent * plotArea.width;

                double highPercent = (candle.high - priceBounds.minY) / priceRange;
                double lowPercent = (candle.low - priceBounds.minY) / priceRange;
                double openPercent = (candle.open - priceBounds.minY) / priceRange;
                double closePercent = (candle.close - priceBounds.minY) / priceRange;

                double highY = plotArea.y + plotArea.height - highPercent * plotArea.height;
                double lowY = plotArea.y + plotArea.height - lowPercent * plotArea.height;
                double openY = plotArea.y + plotArea.height - openPercent * plotArea.height;
                double closeY = plotArea.y + plotArea.height - closePercent * plotArea.height;

                // Determine candle color (green for up, red for down)
                uint32_t candleColor = (candle.close >= candle.open) ? 0xFF00AA00 : 0xFFAA0000;

                // Draw high-low line (wick)
                DrawVerticalLine(x, highY, lowY, 1.0f, 0xFF666666, renderTarget);

                // Draw open-close rectangle (body)
                double bodyTop = std::min(openY, closeY);
                double bodyBottom = std::max(openY, closeY);
                double bodyHeight = bodyBottom - bodyTop;

                if (bodyHeight < 1.0) bodyHeight = 1.0;  // Minimum visible height

                DrawFilledRectangle(x - 2, bodyTop, 4, bodyHeight, candleColor, renderTarget);
            }
        }
    };

// =============================================================================
// 3D SURFACE CHART WITH MESH OPTIMIZATION
// =============================================================================

    class Surface3DRenderer {
    private:
        struct Vertex3D {
            float x, y, z;
            float nx, ny, nz;  // Normal vector
            uint32_t color;

            Vertex3D(float px, float py, float pz, uint32_t col = 0xFFFFFFFF)
                    : x(px), y(py), z(pz), nx(0), ny(1), nz(0), color(col) {}
        };

        struct Triangle {
            size_t v1, v2, v3;
            Triangle(size_t a, size_t b, size_t c) : v1(a), v2(b), v3(c) {}
        };

    public:
        void RenderSurface3D(const ChartConfiguration& config,
                             const PlotArea& plotArea,
                             const DataBounds& bounds,
                             void* renderTarget) {

            // Generate 3D mesh from data
            auto mesh = GenerateSurfaceMesh(config.dataSource.get(), bounds);

            // Calculate lighting and camera
            Camera3D camera = SetupDefaultCamera(bounds);
            LightSetup lighting = SetupDefaultLighting();

            // Perform depth sorting for proper rendering
            auto sortedTriangles = DepthSortTriangles(mesh.first, mesh.second, camera);

            // Render triangles with shading
            for (const auto& triangle : sortedTriangles) {
                DrawShadedTriangle(triangle, mesh.first, lighting, camera, plotArea, renderTarget);
            }

            // Draw wireframe overlay if enabled
            if (config.wireframeOverlay) {
                DrawWireframe(mesh.first, mesh.second, camera, plotArea, renderTarget);
            }
        }

    private:
        std::pair<std::vector<Vertex3D>, std::vector<Triangle>>
        GenerateSurfaceMesh(IChartDataSource* dataSource, const DataBounds& bounds) {

            // For now, assume regular grid data
            // In real implementation, would handle irregular data with triangulation

            std::vector<Vertex3D> vertices;
            std::vector<Triangle> triangles;

            // Determine grid resolution based on data size
            int gridWidth = (int)std::sqrt(dataSource->GetPointCount());
            int gridHeight = gridWidth;

            if (gridWidth * gridHeight != dataSource->GetPointCount()) {
                // Handle irregular data by creating regular grid and interpolating
                gridWidth = 50;  // Default resolution
                gridHeight = 50;
            }

            // Generate vertices
            for (int j = 0; j < gridHeight; ++j) {
                for (int i = 0; i < gridWidth; ++i) {
                    double x = bounds.minX + (double)i / (gridWidth - 1) * (bounds.maxX - bounds.minX);
                    double y = bounds.minY + (double)j / (gridHeight - 1) * (bounds.maxY - bounds.minY);

                    // Get Z value from data (interpolate if necessary)
                    double z = InterpolateZValue(dataSource, x, y, bounds);

                    // Map Z to color
                    uint32_t color = MapZToColor(z, bounds.minZ, bounds.maxZ);

                    vertices.emplace_back((float)x, (float)y, (float)z, color);
                }
            }

            // Generate triangles
            for (int j = 0; j < gridHeight - 1; ++j) {
                for (int i = 0; i < gridWidth - 1; ++i) {
                    size_t topLeft = j * gridWidth + i;
                    size_t topRight = j * gridWidth + (i + 1);
                    size_t bottomLeft = (j + 1) * gridWidth + i;
                    size_t bottomRight = (j + 1) * gridWidth + (i + 1);

                    // Create two triangles per quad
                    triangles.emplace_back(topLeft, bottomLeft, topRight);
                    triangles.emplace_back(topRight, bottomLeft, bottomRight);
                }
            }

            // Calculate vertex normals for smooth shading
            CalculateVertexNormals(vertices, triangles);

            return {vertices, triangles};
        }

        uint32_t MapZToColor(double z, double minZ, double maxZ) {
            if (maxZ == minZ) return 0xFFFFFFFF;

            double normalized = (z - minZ) / (maxZ - minZ);
            normalized = std::clamp(normalized, 0.0, 1.0);

            // Create heat map colors: blue -> green -> yellow -> red
            uint8_t r, g, b;

            if (normalized < 0.25) {
                // Blue to cyan
                double t = normalized / 0.25;
                r = 0;
                g = (uint8_t)(t * 255);
                b = 255;
            } else if (normalized < 0.5) {
                // Cyan to green
                double t = (normalized - 0.25) / 0.25;
                r = 0;
                g = 255;
                b = (uint8_t)((1.0 - t) * 255);
            } else if (normalized < 0.75) {
                // Green to yellow
                double t = (normalized - 0.5) / 0.25;
                r = (uint8_t)(t * 255);
                g = 255;
                b = 0;
            } else {
                // Yellow to red
                double t = (normalized - 0.75) / 0.25;
                r = 255;
                g = (uint8_t)((1.0 - t) * 255);
                b = 0;
            }

            return 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    };

// =============================================================================
// PLUGIN REGISTRATION FOR SPECIALIZED CHARTS
// =============================================================================

    bool RegisterSpecializedChartPlugins() {
        // Register high-performance scatter plot plugin
        auto scatterPlugin = std::make_shared<HighPerformanceScatterPlugin>();
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(scatterPlugin);

        // Register financial chart plugin
        auto financialPlugin = std::make_shared<FinancialChartPlugin>();
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(financialPlugin);

        // Register 3D surface plugin
        auto surface3DPlugin = std::make_shared<Surface3DChartPlugin>();
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(surface3DPlugin);

        return true;
    }

} // namespace UltraCanvas