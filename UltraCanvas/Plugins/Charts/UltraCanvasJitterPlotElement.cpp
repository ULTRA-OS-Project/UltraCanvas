// Plugins/Charts/UltraCanvasJitterPlotElement.cpp
// Jitter plot (strip plot) element implementation
// Version: 1.2.4
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
//
// Changelog:
//   1.2.3 - Fix: Center and Square produced visually identical layouts
//           because both centered each row symmetrically around
//           categoryCenter. Per the R `beeswarm` package source
//           (aroneklund/beeswarm/R/beeswarm.R), the actual difference is:
//             * Center subtracts `mean(a)` from the column index sequence
//               1..n, giving symmetric positions even for even counts
//               (e.g. n=4 → -1.5, -0.5, 0.5, 1.5).
//             * Square subtracts `floor(mean(a))`, giving an asymmetric
//               offset for even counts (e.g. n=4 → -1, 0, 1, 2). This is
//               what makes Square's columns align across rows into a
//               regular rectangular lattice.
//           Center already used the symmetric offset; Square now uses
//           floor-mean offset to match R behavior.
//   1.2.2 - Fix: beeswarm layout produced collapsed/invisible swarms because
//           the four `CalculateBeeswarm*` methods mixed two coordinate
//           systems. `pointRadius` is in PIXELS (e.g. 8) but `(maxVal - minVal)`
//           is in DATA UNITS (e.g. score 1.2 to 7.0 = 5.8). Computing
//           `numBins = (maxVal - minVal) / (pointRadius * 2)` therefore
//           yielded `5.8 / 16 ≈ 0` → `max(1, 0+1) = 1` bin, so all points
//           collapsed to a single horizontal row in Center/Hex/Square.
//           Additionally, `RenderJitterPoints` passed `categoryWidth * 0.8f`
//           where the member `categoryWidth` is 1.0 in data units, not
//           pixels — so beeswarm received maxWidth ≈ 0.8 pixels and
//           Swarm method packed everything into a near-vertical line.
//
//           Fix strategy: every beeswarm calculation now works entirely in
//           PIXEL space.
//             * RenderJitterPoints derives the per-category pixel width
//               from `cachedPlotArea.width / categories.size()` and passes
//               that (× 0.8 for breathing room) to CalculateBeeswarmLayout.
//             * The 4 layout methods now bin Y by PIXEL HEIGHT
//               (GetValuePosition delta) instead of by raw data delta,
//               so binHeight and numBins are dimensionally consistent.
//             * Swarm preserves Y-pixel exactly per point (true beeswarm
//               behavior — see r-charts.com/distribution/beeswarm).
//             * Center / Hex / Square snap Y to grid rows correctly.
//
//   1.2.1 - Fix: setters that mutate point positions now invalidate the
//           position cache (`pointCacheValid = false`). Previously these
//           setters only called RequestRedraw(), which redrew the chart
//           using the STALE cached positions — so changing beeswarm
//           method/priority/corral/side/spacing/maxWidth/compact, or
//           switching jitter distribution/amount/seed, or representation
//           mode, had no visible effect on already-rendered charts.
//           Affected setters: SetJitterDistribution, SetJitterAmount,
//           SetJitterSeed, SetRepresentationMode, SetBeeswarmMethod,
//           SetBeeswarmPriority, SetBeeswarmCorral, SetBeeswarmSide,
//           SetBeeswarmSpacing, SetBeeswarmMaxWidth, SetBeeswarmCompact.
//   1.2.0 - Beeswarm packing implementation.

#include "Plugins/Charts/UltraCanvasJitterPlotElement.h"
#include "UltraCanvasTooltipManager.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasJitterPlotElement::UltraCanvasJitterPlotElement(
    const std::string& id, int x, int y, int width, int height)
    : UltraCanvasChartElementBase(id, x, y, width, height),
      uniformDist(-1.0f, 1.0f),
      gaussianDist(0.0f, 1.0f) {
    
    if (jitterSeed == 0) {
        std::random_device rd;
        randomGenerator.seed(rd());
    } else {
        randomGenerator.seed(jitterSeed);
    }
    
    enableTooltips = true;
    enableZoom = true;
    enablePan = true;
    
    useIndexBasedPositioning = true;
    xAxisLabelMode = XAxisLabelMode::DataLabel;
}

// =============================================================================
// CONFIGURATION METHODS
// =============================================================================

void UltraCanvasJitterPlotElement::SetJitterDistribution(JitterDistribution dist) {
    jitterDistribution = dist;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetJitterAmount(float amount) {
    jitterAmount = std::clamp(amount, 0.0f, 1.0f);
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetJitterSeed(int seed) {
    jitterSeed = seed;
    if (seed == 0) {
        std::random_device rd;
        randomGenerator.seed(rd());
    } else {
        randomGenerator.seed(seed);
    }
    pointCacheValid = false;
    RequestRedraw();
}

// =============================================================================
// BEESWARM CONFIGURATION METHODS
// =============================================================================

void UltraCanvasJitterPlotElement::SetBeeswarmMethod(BeeswarmMethod method) {
    beeswarmMethod = method;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmPriority(BeeswarmPriority priority) {
    beeswarmPriority = priority;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmCorral(BeeswarmCorral corral) {
    beeswarmCorral = corral;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmSide(BeeswarmSide side) {
    beeswarmSide = side;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmSpacing(float spacing) {
    beeswarmSpacing = std::max(0.0f, spacing);
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmMaxWidth(float maxWidth) {
    beeswarmMaxWidth = std::clamp(maxWidth, 0.1f, 1.0f);
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBeeswarmCompact(bool compact) {
    beeswarmCompact = compact;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetRepresentationMode(JitterRepresentationMode mode) {
    representationMode = mode;
    pointCacheValid = false;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetHybridMode(JitterHybridMode mode) {
    hybridMode = mode;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointColor(const Color& color) {
    pointColor = color;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointSize(float size) {
    pointSize = std::max(1.0f, size);
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointAlpha(float alpha) {
    pointAlpha = std::clamp(alpha, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointEdgeColor(const Color& color) {
    pointEdgeColor = color;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointEdgeWidth(float width) {
    pointEdgeWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetPointShape(PointShape shape) {
    pointShape = shape;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetShowMedianMarker(bool show) {
    showMedianMarker = show;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetMedianMarkerStyle(const Color& color, float width) {
    medianLineColor = color;
    medianLineWidth = width;
    if (showMedianMarker) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetShowMeanMarker(bool show) {
    showMeanMarker = show;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetMeanMarkerStyle(const Color& color, float size) {
    meanMarkerColor = color;
    meanMarkerSize = size;
    if (showMeanMarker) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetShowQuartiles(bool show) {
    showQuartiles = show;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetQuartileMarkerStyle(const Color& color, float width) {
    quartileLineColor = color;
    quartileLineWidth = width;
    if (showQuartiles) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetStatisticalMarkerStyle(StatisticalMarkerStyle style) {
    markerStyle = style;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetMeanMarkerShape(MeanMarkerShape shape) {
    meanMarkerShape = shape;
    if (showMeanMarker) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetYAxisScale(AxisScale scale) {
    yAxisScale = scale;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetLogScaleBase(double base) {
    logScaleBase = std::max(2.0, base);
    if (yAxisScale != AxisScale::Linear) {
    InvalidateCache();
    pointCacheValid = false;
    RequestRedraw();
}
}

void UltraCanvasJitterPlotElement::SetCategoryLabelPosition(CategoryLabelPosition position) {
    categoryLabelPosition = position;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetHueVariable(const std::string& varName) {
    hueVariable = varName;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetHueColorMap(const std::map<std::string, Color>& colorMap) {
    hueColorMap = colorMap;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetDodgeEnabled(bool enabled, float width) {
    enableDodge = enabled;
    dodgeWidth = std::clamp(width, 0.1f, 1.0f);
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetCategories(const std::vector<std::string>& cats) {
    categories = cats;
    
    for (const auto& cat : categories) {
        if (categoryData.find(cat) == categoryData.end()) {
            categoryData[cat] = std::vector<JitterCategoryData>();
        }
    }
    
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetCategorySpacing(float spacing) {
    categorySpacing = std::clamp(spacing, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetOrientation(bool horizontal) {
    horizontalOrientation = horizontal;
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBoxPlotSettings(bool showWhiskers, bool showOutliers, float width) {
    showBoxPlotWhiskers = showWhiskers;
    showBoxPlotOutliers = showOutliers;
    boxPlotWidth = std::clamp(width, 0.1f, 1.0f);
    if (hybridMode == JitterHybridMode::JitterBoxPlot) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBoxPlotColors(const Color& fill, const Color& border, float borderWidth) {
    boxPlotFillColor = fill;
    boxPlotBorderColor = border;
    boxPlotBorderWidth = borderWidth;
    if (hybridMode == JitterHybridMode::JitterBoxPlot) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetBarChartSettings(const Color& color, float width, bool showValues) {
    barChartColor = color;
    barChartWidth = std::clamp(width, 0.1f, 1.0f);
    showBarValues = showValues;
    if (hybridMode == JitterHybridMode::JitterBarChart) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetViolinSettings(const Color& fill, const Color& border, 
                                                     float borderWidth, int resolution) {
    violinFillColor = fill;
    violinBorderColor = border;
    violinBorderWidth = borderWidth;
    violinResolution = std::max(10, resolution);
    if (hybridMode == JitterHybridMode::JitterViolinPlot || 
        hybridMode == JitterHybridMode::RaincloudPlot) {
        RequestRedraw();
    }
}

void UltraCanvasJitterPlotElement::SetBackgroundColor(const Color& color) {
    backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetGridVisible(bool visible) {
    showGrid = visible;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetGridColor(const Color& color) {
    gridColor = color;
    if (showGrid) RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetAxesVisible(bool visible) {
    showAxes = visible;
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::SetRegionFilter(const std::string& region) {
    if (selectedRegion != region) {
        selectedRegion = region;
        // No rebuild cache, just change alpha in render
        RequestRedraw();
    }
}

void UltraCanvasJitterPlotElement::SetMinScoreFilter(double minScore) {
    if (minScoreFilter != minScore) {
        minScoreFilter = minScore;
        // No rebuild cache, just skip points in render
        RequestRedraw();
    }
}

// =============================================================================
// DATA LOADING
// =============================================================================

void UltraCanvasJitterPlotElement::AddCategoryData(const std::string& category, 
                                                   const std::vector<double>& values,
                                                   const std::string& hueValue) {
    if (std::find(categories.begin(), categories.end(), category) == categories.end()) {
        categories.push_back(category);
    }
    
    auto& catDataList = categoryData[category];
    
    auto it = std::find_if(catDataList.begin(), catDataList.end(),
        [&hueValue](const JitterCategoryData& data) {
            return data.hueGroup == hueValue;
        });
    
    if (it != catDataList.end()) {
        it->values.insert(it->values.end(), values.begin(), values.end());
        it->statisticsCached = false;
    } else {
        JitterCategoryData newData(category, hueValue);
        newData.values = values;
        catDataList.push_back(newData);
        
        if (!hueValue.empty() && 
            std::find(hueCategories.begin(), hueCategories.end(), hueValue) == hueCategories.end()) {
            hueCategories.push_back(hueValue);
        }
    }
    
    InvalidateCache();
    RequestRedraw();
}

void UltraCanvasJitterPlotElement::ClearData() {
    categoryData.clear();
    hueCategories.clear();
    pointPositionsCache.clear();
    pointCacheValid = false;
    InvalidateCache();
    RequestRedraw();
}

// =============================================================================
// JITTER CALCULATION
// =============================================================================

float UltraCanvasJitterPlotElement::CalculateJitter(size_t dataIndex, size_t groupIndex) const {
    // Simple deterministic jitter calculation
    float jitter = 0.0f;
    if (jitterDistribution == JitterDistribution::Gaussian) {
        float u1 = std::abs(std::sin(static_cast<float>(dataIndex * 12345 + groupIndex * 67890)));
        float u2 = std::cos(static_cast<float>(dataIndex * 12345 + groupIndex * 67890));
        jitter = std::sqrt(-2.0f * std::log(u1 + 0.001f)) * u2;
        jitter = std::clamp(jitter, -3.0f, 3.0f) / 3.0f;
    } else {
        jitter = std::sin(static_cast<float>(dataIndex * 12345 + groupIndex * 67890));
    }
    
    return jitter * jitterAmount;
}

float UltraCanvasJitterPlotElement::CalculateGaussianJitter() const {
    float u1 = uniformDist(randomGenerator) * 0.5f + 0.5f;
    float u2 = uniformDist(randomGenerator) * 0.5f + 0.5f;
    
    float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265f * u2);
    
    return std::clamp(z, -3.0f, 3.0f) / 3.0f;
}

float UltraCanvasJitterPlotElement::CalculateUniformJitter() const {
    return uniformDist(randomGenerator);
}

// =============================================================================
// DATA BOUNDS CALCULATION
// =============================================================================

ChartDataBounds UltraCanvasJitterPlotElement::CalculateDataBounds() {
    ChartDataBounds bounds;
    
    if (categoryData.empty()) {
        bounds.minX = 0.0;
        bounds.maxX = 1.0;
        bounds.minY = 0.0;
        bounds.maxY = 1.0;
        return bounds;
    }
    
    bool firstValue = true;
    
    for (const auto& [category, catDataList] : categoryData) {
        for (const auto& catData : catDataList) {
            for (double value : catData.values) {
                if (firstValue) {
                    bounds.minY = value;
                    bounds.maxY = value;
                    firstValue = false;
                } else {
                    bounds.minY = std::min(bounds.minY, value);
                    bounds.maxY = std::max(bounds.maxY, value);
                }
            }
        }
    }
    
    bounds.minX = 0.0;
    bounds.maxX = static_cast<double>(categories.size());
    
    double yRange = bounds.maxY - bounds.minY;
    if (yRange > 0) {
        bounds.minY -= yRange * 0.05;
        bounds.maxY += yRange * 0.05;
    } else {
        bounds.minY -= 1.0;
        bounds.maxY += 1.0;
    }
    
    return bounds;
}

// =============================================================================
// STATISTICAL CALCULATIONS
// =============================================================================

void UltraCanvasJitterPlotElement::CalculateStatistics(JitterCategoryData& data) {
    if (data.statisticsCached || data.values.empty()) return;
    
    std::vector<double> sortedValues = data.values;
    std::sort(sortedValues.begin(), sortedValues.end());
    
    data.cachedMedian = CalculateMedian(sortedValues);
    data.cachedMean = CalculateMean(sortedValues);
    
    auto quartiles = CalculateQuartiles(sortedValues);
    data.cachedQ1 = quartiles.first;
    data.cachedQ3 = quartiles.second;
    
    auto minMax = CalculateMinMax(sortedValues);
    data.cachedMin = minMax.first;
    data.cachedMax = minMax.second;
    
    data.statisticsCached = true;
}

double UltraCanvasJitterPlotElement::CalculateMedian(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    
    size_t n = values.size();
    if (n % 2 == 0) {
        return (values[n/2 - 1] + values[n/2]) / 2.0;
    } else {
        return values[n/2];
    }
}

double UltraCanvasJitterPlotElement::CalculateMean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

std::pair<double, double> UltraCanvasJitterPlotElement::CalculateQuartiles(
    const std::vector<double>& values) {
    
    if (values.size() < 4) {
        return {values.front(), values.back()};
    }
    
    size_t n = values.size();
    size_t q1Pos = n / 4;
    size_t q3Pos = (3 * n) / 4;
    
    return {values[q1Pos], values[q3Pos]};
}

std::pair<double, double> UltraCanvasJitterPlotElement::CalculateMinMax(
    const std::vector<double>& values) {
    
    if (values.empty()) return {0.0, 0.0};
    
    return {values.front(), values.back()};
}

// =============================================================================
// DENSITY ESTIMATION (FOR VIOLIN PLOTS)
// =============================================================================

std::vector<float> UltraCanvasJitterPlotElement::CalculateKernelDensity(
    const std::vector<double>& values, int resolution) {
    
    std::vector<float> density(resolution, 0.0f);
    
    if (values.empty()) return density;
    
    double minVal = *std::min_element(values.begin(), values.end());
    double maxVal = *std::max_element(values.begin(), values.end());
    double range = maxVal - minVal;
    
    if (range < 1e-6) {
        density[resolution / 2] = 1.0f;
        return density;
    }
    
    double n = values.size();
    double sigma = 0.0;
    double mean = CalculateMean(values);
    for (double v : values) {
        sigma += (v - mean) * (v - mean);
    }
    sigma = std::sqrt(sigma / n);
    double bandwidth = 0.9 * sigma * std::pow(n, -0.2);
    
    for (int i = 0; i < resolution; ++i) {
        double x = minVal + (i * range / (resolution - 1));
        double sum = 0.0;
        
        for (double value : values) {
            double diff = (x - value) / bandwidth;
            sum += std::exp(-0.5 * diff * diff);
        }
        
        density[i] = static_cast<float>(sum / (n * bandwidth * std::sqrt(2.0 * 3.14159265)));
    }
    
    float maxDensity = *std::max_element(density.begin(), density.end());
    if (maxDensity > 0.0f) {
        for (float& d : density) {
            d /= maxDensity;
        }
    }
    
    return density;
}

// =============================================================================
// RENDERIZAR EJES PARA JITTER PLOT
// =============================================================================

void UltraCanvasJitterPlotElement::RenderJitterAxes(IRenderContext* ctx) {
    if (!ctx || categories.empty()) return;
    
    UpdateRenderingCache();
    
    // Dibujar líneas de ejes principales
    ctx->SetStrokePaint(Color(0, 0, 0, 255));
    ctx->SetStrokeWidth(1.5f);
    
    // Eje X (horizontal en la parte inferior)
    float axisY = cachedPlotArea.y + cachedPlotArea.height;
    ctx->DrawLine({cachedPlotArea.x, axisY}, {cachedPlotArea.x + cachedPlotArea.width, axisY});

    // Eje Y (vertical en la parte izquierda)
    ctx->DrawLine({cachedPlotArea.x, cachedPlotArea.y}, {cachedPlotArea.x, axisY});
    
    // Configurar texto
    ctx->SetTextPaint(Color(0, 0, 0, 255));
    ctx->SetFontSize(10.0f);
    
    // ===== EJE Y: Valores numéricos =====
    int numYTicks = 8;
    double yMin = cachedDataBounds.minY;
    double yMax = cachedDataBounds.maxY;
    double yRange = yMax - yMin;
    
    for (int i = 0; i <= numYTicks; ++i) {
        double value = yMin + (i * yRange / numYTicks);
        float y = cachedPlotArea.y + cachedPlotArea.height - 
                  (static_cast<float>(i) / numYTicks) * cachedPlotArea.height;
        
        // Línea de tick pequeña
        ctx->DrawLine({cachedPlotArea.x - 5, y}, {cachedPlotArea.x, y});

        // Label del valor
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << value;
        std::string label = oss.str();

        Size2Di yDim = ctx->GetTextLineDimensions(label);
        int txtW = yDim.width;
        int txtH = yDim.height;
        ctx->DrawText(label, {static_cast<double>(cachedPlotArea.x - txtW - 8), static_cast<double>(y - txtH / 2)});
    }
    
    // ===== EJE X: Nombres de estados =====
    size_t numCategories = categories.size();
    
    for (size_t i = 0; i < numCategories; ++i) {
        float catPos = GetCategoryPosition(i, 0);
        
        // Tick pequeño hacia arriba
        ctx->DrawLine({catPos, axisY}, {catPos, axisY + 5});

        // Label del nombre del estado - más separado del eje
        std::string label = categories[i];

        Size2Di catDim = ctx->GetTextLineDimensions(label);
        int txtW = catDim.width;

        // Labels sin rotar, más abajo
        ctx->DrawText(label, {static_cast<double>(catPos - txtW / 2), static_cast<double>(axisY + 12)});
    }
    
    // Título del eje Y - más separado del eje
    if (!chartTitle.empty()) {
        ctx->SetFontSize(12.0f);
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        
        // Título rotado verticalmente a la izquierda del eje Y
        ctx->PushState();
        ctx->Translate(cachedPlotArea.x - 55, cachedPlotArea.y + cachedPlotArea.height / 2);
        ctx->Rotate(-90.0f * 3.14159f / 180.0f);
        ctx->DrawText(chartTitle, {0.0, 0.0});
        ctx->PopState();
    }
}

// =============================================================================
// MAIN RENDERING
// =============================================================================

void UltraCanvasJitterPlotElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;

    ctx->PushState();

    Rect2Di local = GetLocalBounds();

    if (categoryData.empty()) {
        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(Rect2Dd(local.x, local.y, local.width, local.height));

        ctx->SetTextPaint(Color(128, 128, 128, 255));
        ctx->SetFontSize(14.0f);

        std::string emptyText = "No data to display";
        int textX = local.x + local.width / 2 - 60;
        int textY = local.y + local.height / 2;

        ctx->DrawText(emptyText, {static_cast<double>(textX), static_cast<double>(textY)});
        ctx->PopState();
        return;
    }

    UpdateRenderingCache();

    if (animationEnabled && !animationComplete) {
        UpdateAnimation();
    }

    ctx->ClipRect(Rect2Dd(local.x, local.y, local.width, local.height));
    
    RenderCommonBackground(ctx);
    
    // Renderizar bandas de fondo por región
    RenderRegionBands(ctx);
    
    // Renderizar ejes específicos para Jitter Plot (since we don't use dataSource)
    RenderJitterAxes(ctx);
    
    RenderChart(ctx);
    
    if (enableSelection) {
        DrawSelectionIndicators(ctx);
    }
    
    ctx->PopState();
}

// =============================================================================
// BANDAS DE FONDO POR REGIÓN
// =============================================================================

void UltraCanvasJitterPlotElement::RenderRegionBands(IRenderContext* ctx) {
    if (!ctx || categories.empty()) return;
    
    // Definir los rangos de estados por región (índices)
    std::vector<std::pair<size_t, size_t>> regionRanges = {
        {0, 9},    // NorthEast: AL, BA, CE, MA, PB, PE, PI, RN, SE (9 estados)
        {9, 16},   // North: AC, AM, AP, PA, RO, RR, TO (7 estados)
        {16, 20},  // CentralWest: DF, GO, MS, MT (4 estados)
        {20, 24},  // SouthEast: ES, MG, RJ, SP (4 estados)
        {24, 27}   // South: PR, RS, SC (3 estados)
    };
    
    // Colores muy claros para las bandas de fondo
    std::vector<Color> bandColors = {
        Color(200, 230, 215, 40),   // NorthEast: verde muy claro
        Color(200, 220, 220, 40),    // North: cyan muy claro
        Color(230, 220, 190, 40),    // CentralWest: naranja muy claro
        Color(230, 200, 200, 40),    // SouthEast: rosa muy claro
        Color(200, 210, 230, 40)     // South: azul muy claro
    };
    
    size_t numCategories = categories.size();
    float categorySpan = cachedPlotArea.width / numCategories;
    
    for (size_t r = 0; r < regionRanges.size(); ++r) {
        size_t startIdx = regionRanges[r].first;
        size_t endIdx = regionRanges[r].second;
        
        if (startIdx >= numCategories) continue;
        
        float x1 = cachedPlotArea.x + startIdx * categorySpan;
        float x2 = cachedPlotArea.x + std::min(endIdx, numCategories) * categorySpan;
        
        // Dibujar banda de color de fondo
        ctx->SetFillPaint(bandColors[r]);
        ctx->FillRectangle(Rect2Dd(x1, cachedPlotArea.y, x2 - x1, cachedPlotArea.height));
    }
}

// =============================================================================
// LÍNEA DE PROMEDIO NACIONAL
// =============================================================================

void UltraCanvasJitterPlotElement::RenderNationalAverageLine(IRenderContext* ctx) {
    if (!ctx || categoryData.empty()) return;
    
    // Calcular promedio nacional
    double sum = 0.0;
    size_t count = 0;
    
    for (const auto& [category, catDataList] : categoryData) {
        for (const auto& catData : catDataList) {
            for (double value : catData.values) {
                sum += value;
                count++;
            }
        }
    }
    
    if (count == 0) return;
    
    double nationalAverage = sum / count;
    float avgY = GetValuePosition(nationalAverage);
    
    // Línea punteada horizontal para el promedio
    ctx->SetStrokePaint(Color(80, 80, 80, 200));
    ctx->SetStrokeWidth(1.5f);
    
    // Dibujar línea punteada
    float dashLength = 8.0f;
    float gapLength = 4.0f;
    float x = cachedPlotArea.x;
    
    while (x < cachedPlotArea.x + cachedPlotArea.width) {
        float nextX = std::min(x + dashLength, static_cast<float>(cachedPlotArea.x + cachedPlotArea.width));
        ctx->DrawLine({x, avgY}, {nextX, avgY});
        x = nextX + gapLength;
    }

    // Label del promedio
    ctx->SetTextPaint(Color(80, 80, 80, 255));
    ctx->SetFontSize(9.0f);
    std::ostringstream oss;
    oss << "Avg: " << std::fixed << std::setprecision(2) << nationalAverage;
    ctx->DrawText(oss.str(), {static_cast<double>(cachedPlotArea.x + cachedPlotArea.width - 55), static_cast<double>(avgY - 15)});
}

void UltraCanvasJitterPlotElement::RenderChart(IRenderContext* ctx) {
    if (!ctx || categories.empty()) return;
    
    UpdateRenderingCache();
    
    switch (hybridMode) {
        case JitterHybridMode::JitterBarChart:
            RenderBarChartBase(ctx);
            break;
        case JitterHybridMode::JitterBoxPlot:
            RenderBoxPlotOverlay(ctx);
            break;
        case JitterHybridMode::JitterViolinPlot:
            RenderViolinOverlay(ctx);
            break;
        case JitterHybridMode::RaincloudPlot:
            RenderRaincloudPlot(ctx);
            break;
        default:
            break;
    }
    
    if (hybridMode != JitterHybridMode::JitterBoxPlot) {
        if (showQuartiles) {
            RenderQuartileMarkers(ctx);
        }
        if (showMedianMarker) {
            RenderMedianMarkers(ctx);
        }
        if (showMeanMarker) {
            RenderMeanMarkers(ctx);
        }
    }
    
    // Renderizar línea de promedio nacional
    RenderNationalAverageLine(ctx);
    
    RenderJitterPoints(ctx);
}

// =============================================================================
// JITTER POINTS RENDERING
// =============================================================================

void UltraCanvasJitterPlotElement::RenderJitterPoints(IRenderContext* ctx) {
    if (!ctx) return;
    
    // Build point positions cache if needed
    if (!pointCacheValid) {
        pointPositionsCache.clear();
        
        size_t catIdx = 0;
        for (const auto& cat : categories) {
            const auto& catDataList = GetCategoryDataList(cat);
            size_t hueIdx = 0;
            for (const auto& catData : catDataList) {
                // Check if we're using beeswarm packing
                if (jitterDistribution == JitterDistribution::Beeswarm) {
                    // Use beeswarm layout
                    float categoryCenter = GetCategoryPosition(catIdx, hueIdx);
                    
                    // FIX (v1.2.2): compute category width in PIXELS, not
                    // data units. The member `categoryWidth` (= 1.0) is a
                    // logical span in data space; the real horizontal slot
                    // each category occupies is the chart width divided by
                    // category count.
                    float categoryPixelWidth = categories.empty() ? 1.0f :
                        cachedPlotArea.width / static_cast<float>(categories.size());
                    float categoryWidthForBeeswarm = categoryPixelWidth * 0.8f;
                    
                    auto beeswarmPoints = CalculateBeeswarmLayout(
                        catData.values,
                        categoryCenter,
                        categoryWidthForBeeswarm,
                        pointSize
                    );
                    
                    for (const auto& bp : beeswarmPoints) {
                        PointPosition pp;
                        pp.x = bp.position.x;
                        pp.y = bp.position.y;
                        pp.value = bp.yValue;
                        pp.category = cat;
                        pp.region = catData.hueGroup;
                        pointPositionsCache.push_back(pp);
                    }
                } else {
                    // Use traditional random jitter (Uniform or Gaussian)
                    size_t ptIdx = 0;
                    for (double val : catData.values) {
                        float jitter = CalculateJitter(ptIdx, hueIdx);
                        Point2Dd pos = CalculatePointPosition(catIdx, val, jitter, hueIdx);
                        
                        PointPosition pp;
                        pp.x = pos.x;
                        pp.y = pos.y;
                        pp.value = val;
                        pp.category = cat;
                        pp.region = catData.hueGroup;
                        pointPositionsCache.push_back(pp);
                        
                        ptIdx++;
                    }
                }
                hueIdx++;
            }
            catIdx++;
        }
        pointCacheValid = true;
    }
    
    // Render from cache - much faster!
    for (const auto& pp : pointPositionsCache) {
        // Apply region filter
        if (!selectedRegion.empty() && pp.region != selectedRegion) {
            continue;
        }
        
        // Apply score filter
        if (pp.value < minScoreFilter) {
            continue;
        }
        
        // Get color for region
        Color color = Color(0, 102, 204, 180);
        for (size_t c = 0; c < categories.size(); c++) {
            if (categories[c] == pp.category) {
                const auto& catDataList = GetCategoryDataList(categories[c]);
                for (const auto& cd : catDataList) {
                    if (cd.hueGroup == pp.region) {
                        color = GetPointColor(c, pp.region);
                        break;
                    }
                }
                break;
            }
        }
        
        color.a = static_cast<uint8_t>(pointAlpha * 255);
        ctx->SetFillPaint(color);
        ctx->FillCircle({pp.x, pp.y}, pointSize);
    }
}

void UltraCanvasJitterPlotElement::DrawJitterPoint(IRenderContext* ctx, const Point2Dd& pos,
                                                   const Color& color, float size) {
    if (pointEdgeWidth > 0.0f) {
        ctx->SetStrokePaint(pointEdgeColor);
        ctx->SetStrokeWidth(pointEdgeWidth);
    }
    
    ctx->SetFillPaint(color);
    
    switch (pointShape) {
        case PointShape::Circle:
            ctx->FillCircle({pos.x, pos.y}, size);
            if (pointEdgeWidth > 0.0f) {
                ctx->DrawCircle({pos.x, pos.y}, size);
            }
            break;

        case PointShape::Square: {
            float halfSize = size;
            ctx->FillRectangle(Rect2Dd(pos.x - halfSize, pos.y - halfSize,
                                       halfSize * 2, halfSize * 2));
            if (pointEdgeWidth > 0.0f) {
                ctx->DrawRectangle(Rect2Dd(pos.x - halfSize, pos.y - halfSize,
                                           halfSize * 2, halfSize * 2));
            }
            break;
        }
            
        case PointShape::Triangle: {
            std::vector<Point2Dd> triangle = {
                Point2Dd(pos.x, pos.y - size),
                Point2Dd(pos.x - size, pos.y + size),
                Point2Dd(pos.x + size, pos.y + size)
            };
            ctx->FillLinePath(triangle);
            if (pointEdgeWidth > 0.0f) {
                ctx->DrawLinePath(triangle, true);
            }
            break;
        }
            
        case PointShape::Diamond: {
            std::vector<Point2Dd> diamond = {
                Point2Dd(pos.x, pos.y - size),
                Point2Dd(pos.x + size, pos.y),
                Point2Dd(pos.x, pos.y + size),
                Point2Dd(pos.x - size, pos.y)
            };
            ctx->FillLinePath(diamond);
            if (pointEdgeWidth > 0.0f) {
                ctx->DrawLinePath(diamond, true);
            }
            break;
        }
    }
}

void UltraCanvasJitterPlotElement::DrawMeanMarker(IRenderContext* ctx, const Point2Dd& pos,
                                                  MeanMarkerShape shape, float size) {
    if (!ctx) return;
    
    switch (shape) {
        case MeanMarkerShape::Diamond: {
            std::vector<Point2Dd> diamond = {
                Point2Dd(pos.x, pos.y - size),
                Point2Dd(pos.x + size, pos.y),
                Point2Dd(pos.x, pos.y + size),
                Point2Dd(pos.x - size, pos.y)
            };
            ctx->FillLinePath(diamond);
            ctx->DrawLinePath(diamond, true);
            break;
        }
        
        case MeanMarkerShape::Cross: {
            float armLength = size;
            ctx->SetStrokeWidth(2.5f);
            ctx->DrawLine({pos.x - armLength, pos.y}, {pos.x + armLength, pos.y});
            ctx->DrawLine({pos.x, pos.y - armLength}, {pos.x, pos.y + armLength});
            break;
        }

        case MeanMarkerShape::Circle: {
            ctx->FillCircle({pos.x, pos.y}, size);
            ctx->DrawCircle({pos.x, pos.y}, size);
            break;
        }

        case MeanMarkerShape::Square: {
            float halfSize = size;
            ctx->FillRectangle(Rect2Dd(pos.x - halfSize, pos.y - halfSize,
                                       halfSize * 2, halfSize * 2));
            ctx->DrawRectangle(Rect2Dd(pos.x - halfSize, pos.y - halfSize,
                                       halfSize * 2, halfSize * 2));
            break;
        }
        
        case MeanMarkerShape::Star: {
            std::vector<Point2Dd> star;
            float outerRadius = size;
            float innerRadius = size * 0.4f;
            
            for (int i = 0; i < 10; ++i) {
                float angle = (i * 36.0f - 90.0f) * (3.14159265f / 180.0f);
                float radius = (i % 2 == 0) ? outerRadius : innerRadius;
                star.push_back(Point2Dd(
                    pos.x + radius * std::cos(angle),
                    pos.y + radius * std::sin(angle)
                ));
            }
            ctx->FillLinePath(star);
            ctx->DrawLinePath(star, true);
            break;
        }
    }
}

// =============================================================================
// STATISTICAL MARKERS RENDERING
// =============================================================================

void UltraCanvasJitterPlotElement::RenderMedianMarkers(IRenderContext* ctx) {
    if (!ctx) return;
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (auto& catData : catDataList) {
            CalculateStatistics(const_cast<JitterCategoryData&>(catData));
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float valuePos = GetValuePosition(catData.cachedMedian);
            
            float lineWidth = categoryWidth * 0.35f;
            
            // Dibujar línea blanca de fondo (borde) para mayor contraste
            ctx->SetStrokePaint(Color(255, 255, 255, 255));
            ctx->SetStrokeWidth(medianLineWidth + 2.0f);
            
            if (horizontalOrientation) {
                ctx->DrawLine({valuePos, catPos - lineWidth},
                              {valuePos, catPos + lineWidth});
            } else {
                ctx->DrawLine({catPos - lineWidth, valuePos},
                              {catPos + lineWidth, valuePos});
            }

            // Dibujar línea negra principal
            ctx->SetStrokePaint(Color(0, 0, 0, 255));
            ctx->SetStrokeWidth(medianLineWidth);

            if (horizontalOrientation) {
                ctx->DrawLine({valuePos, catPos - lineWidth},
                              {valuePos, catPos + lineWidth});
            } else {
                ctx->DrawLine({catPos - lineWidth, valuePos},
                              {catPos + lineWidth, valuePos});
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

void UltraCanvasJitterPlotElement::RenderMeanMarkers(IRenderContext* ctx) {
    if (!ctx) return;
    
    ctx->SetFillPaint(meanMarkerColor);
    ctx->SetStrokePaint(meanMarkerColor);
    ctx->SetStrokeWidth(2.0f);
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (auto& catData : catDataList) {
            CalculateStatistics(const_cast<JitterCategoryData&>(catData));
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float valuePos = GetValuePosition(catData.cachedMean);
            
            Point2Dd pos = horizontalOrientation ? 
                Point2Dd(valuePos, catPos) : Point2Dd(catPos, valuePos);
            
            DrawMeanMarker(ctx, pos, meanMarkerShape, meanMarkerSize);
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

void UltraCanvasJitterPlotElement::RenderQuartileMarkers(IRenderContext* ctx) {
    if (!ctx) return;
    
    ctx->SetStrokePaint(quartileLineColor);
    ctx->SetStrokeWidth(quartileLineWidth);
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (auto& catData : catDataList) {
            CalculateStatistics(const_cast<JitterCategoryData&>(catData));
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float q1Pos = GetValuePosition(catData.cachedQ1);
            float q3Pos = GetValuePosition(catData.cachedQ3);
            
            float lineWidth = categoryWidth * 0.2f;
            
            if (horizontalOrientation) {
                ctx->DrawLine({q1Pos, catPos - lineWidth}, {q1Pos, catPos + lineWidth});
                ctx->DrawLine({q3Pos, catPos - lineWidth}, {q3Pos, catPos + lineWidth});
            } else {
                ctx->DrawLine({catPos - lineWidth, q1Pos}, {catPos + lineWidth, q1Pos});
                ctx->DrawLine({catPos - lineWidth, q3Pos}, {catPos + lineWidth, q3Pos});
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

// =============================================================================
// HYBRID MODE RENDERERS
// =============================================================================

void UltraCanvasJitterPlotElement::RenderBoxPlotOverlay(IRenderContext* ctx) {
    if (!ctx) return;
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (auto& catData : catDataList) {
            CalculateStatistics(const_cast<JitterCategoryData&>(catData));
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float q1Pos = GetValuePosition(catData.cachedQ1);
            float q3Pos = GetValuePosition(catData.cachedQ3);
            float medianPos = GetValuePosition(catData.cachedMedian);
            
            double iqr = catData.cachedQ3 - catData.cachedQ1;
            double whiskerLow = catData.cachedQ1 - 1.5 * iqr;
            double whiskerHigh = catData.cachedQ3 + 1.5 * iqr;
            
            whiskerLow = std::max(whiskerLow, catData.cachedMin);
            whiskerHigh = std::min(whiskerHigh, catData.cachedMax);
            
            float whiskerLowPos = GetValuePosition(whiskerLow);
            float whiskerHighPos = GetValuePosition(whiskerHigh);
            
            float boxWidth = categoryWidth * boxPlotWidth * 0.4f;
            
            if (horizontalOrientation) {
                float boxTop = catPos - boxWidth;
                float boxBottom = catPos + boxWidth;
                
                ctx->SetFillPaint(boxPlotFillColor);
                ctx->FillRectangle(Rect2Dd(q1Pos, boxTop, q3Pos - q1Pos, boxBottom - boxTop));

                ctx->SetStrokePaint(boxPlotBorderColor);
                ctx->SetStrokeWidth(boxPlotBorderWidth);
                ctx->DrawRectangle(Rect2Dd(q1Pos, boxTop, q3Pos - q1Pos, boxBottom - boxTop));

                ctx->SetStrokeWidth(3.0f);
                ctx->SetStrokePaint(Color(0, 0, 0, 255));
                ctx->DrawLine({medianPos, boxTop}, {medianPos, boxBottom});

                if (showBoxPlotWhiskers) {
                    ctx->SetStrokeWidth(boxPlotBorderWidth);
                    ctx->SetStrokePaint(boxPlotBorderColor);

                    ctx->DrawLine({q1Pos, catPos}, {whiskerLowPos, catPos});
                    ctx->DrawLine({q3Pos, catPos}, {whiskerHighPos, catPos});

                    float capHeight = boxWidth * 0.5f;
                    ctx->DrawLine({whiskerLowPos, catPos - capHeight},
                                  {whiskerLowPos, catPos + capHeight});
                    ctx->DrawLine({whiskerHighPos, catPos - capHeight},
                                  {whiskerHighPos, catPos + capHeight});
                }

                if (showBoxPlotOutliers) {
                    ctx->SetFillPaint(boxPlotBorderColor);
                    for (double value : catData.values) {
                        if (value < whiskerLow || value > whiskerHigh) {
                            float outlierPos = GetValuePosition(value);
                            ctx->FillCircle({outlierPos, catPos}, 3.0f);
                        }
                    }
                }

            } else {
                float boxLeft = catPos - boxWidth;
                float boxRight = catPos + boxWidth;
                float boxHeight = q1Pos - q3Pos;

                ctx->SetFillPaint(boxPlotFillColor);
                ctx->FillRectangle(Rect2Dd(boxLeft, q3Pos, boxRight - boxLeft, boxHeight));

                ctx->SetStrokePaint(boxPlotBorderColor);
                ctx->SetStrokeWidth(boxPlotBorderWidth);
                ctx->DrawRectangle(Rect2Dd(boxLeft, q3Pos, boxRight - boxLeft, boxHeight));

                ctx->SetStrokeWidth(3.0f);
                ctx->SetStrokePaint(Color(0, 0, 0, 255));
                ctx->DrawLine({boxLeft, medianPos}, {boxRight, medianPos});

                if (showBoxPlotWhiskers) {
                    ctx->SetStrokeWidth(boxPlotBorderWidth);
                    ctx->SetStrokePaint(boxPlotBorderColor);

                    ctx->DrawLine({catPos, q1Pos}, {catPos, whiskerLowPos});
                    ctx->DrawLine({catPos, q3Pos}, {catPos, whiskerHighPos});

                    float capWidth = boxWidth * 0.5f;
                    ctx->DrawLine({catPos - capWidth, whiskerLowPos},
                                  {catPos + capWidth, whiskerLowPos});
                    ctx->DrawLine({catPos - capWidth, whiskerHighPos},
                                  {catPos + capWidth, whiskerHighPos});
                }

                if (showBoxPlotOutliers) {
                    ctx->SetFillPaint(boxPlotBorderColor);
                    for (double value : catData.values) {
                        if (value < whiskerLow || value > whiskerHigh) {
                            float outlierPos = GetValuePosition(value);
                            ctx->FillCircle({catPos, outlierPos}, 3.0f);
                        }
                    }
                }
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

void UltraCanvasJitterPlotElement::RenderBarChartBase(IRenderContext* ctx) {
    if (!ctx) return;
    
    size_t maxCount = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        for (const auto& catData : catDataList) {
            maxCount = std::max(maxCount, catData.values.size());
        }
    }
    
    if (maxCount == 0) return;
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (const auto& catData : catDataList) {
            size_t count = catData.values.size();
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            
            float barWidth = categoryWidth * barChartWidth * 0.4f;
            float normalizedCount = static_cast<float>(count) / static_cast<float>(maxCount);
            
            if (horizontalOrientation) {
                float barHeight = cachedPlotArea.width * normalizedCount;
                float barTop = catPos - barWidth;
                float barBottom = catPos + barWidth;

                ctx->SetFillPaint(barChartColor);
                ctx->FillRectangle(Rect2Dd(cachedPlotArea.x, barTop, barHeight, barBottom - barTop));

                ctx->SetStrokePaint(Color(barChartColor.r, barChartColor.g, barChartColor.b, 255));
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(Rect2Dd(cachedPlotArea.x, barTop, barHeight, barBottom - barTop));

                if (showBarValues) {
                    ctx->SetFillPaint(Color(60, 60, 60, 255));
                    std::string countStr = "n=" + std::to_string(count);
                    ctx->DrawText(countStr, {static_cast<double>(cachedPlotArea.x + barHeight + 5), static_cast<double>(catPos)});
                }
            } else {
                float barHeight = cachedPlotArea.height * normalizedCount;
                float barLeft = catPos - barWidth;
                float barRight = catPos + barWidth;
                float barTop = cachedPlotArea.y + cachedPlotArea.height - barHeight;

                ctx->SetFillPaint(barChartColor);
                ctx->FillRectangle(Rect2Dd(barLeft, barTop, barRight - barLeft, barHeight));

                ctx->SetStrokePaint(Color(barChartColor.r, barChartColor.g, barChartColor.b, 255));
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(Rect2Dd(barLeft, barTop, barRight - barLeft, barHeight));

                if (showBarValues) {
                    ctx->SetFillPaint(Color(60, 60, 60, 255));
                    std::string countStr = "n=" + std::to_string(count);
                    Size2Di barDim = ctx->GetTextLineDimensions(countStr);
                    int txtW = barDim.width;
                    ctx->DrawText(countStr, {static_cast<double>(catPos - txtW / 2), static_cast<double>(barTop - 5)});
                }
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

void UltraCanvasJitterPlotElement::RenderViolinOverlay(IRenderContext* ctx) {
    if (!ctx) return;
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (const auto& catData : catDataList) {
            if (catData.values.empty()) {
                hueIndex++;
                continue;
            }
            
            std::vector<float> density = CalculateKernelDensity(catData.values, violinResolution);
            
            double minVal = *std::min_element(catData.values.begin(), catData.values.end());
            double maxVal = *std::max_element(catData.values.begin(), catData.values.end());
            double range = maxVal - minVal;
            
            if (range < 1e-6) {
                hueIndex++;
                continue;
            }
            
            float maxDensity = *std::max_element(density.begin(), density.end());
            if (maxDensity < 1e-6) {
                hueIndex++;
                continue;
            }
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float violinWidth = categoryWidth * 0.4f;
            
            std::vector<Point2Dd> violinShape;
            
            if (horizontalOrientation) {
                for (int i = 0; i < violinResolution; ++i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    violinShape.push_back(Point2Dd(valuePos, catPos + width));
                }
                for (int i = violinResolution - 1; i >= 0; --i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    violinShape.push_back(Point2Dd(valuePos, catPos - width));
                }
            } else {
                for (int i = 0; i < violinResolution; ++i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    violinShape.push_back(Point2Dd(catPos + width, valuePos));
                }
                for (int i = violinResolution - 1; i >= 0; --i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    violinShape.push_back(Point2Dd(catPos - width, valuePos));
                }
            }
            
            if (violinShape.size() >= 3) {
                ctx->SetFillPaint(violinFillColor);
                ctx->FillLinePath(violinShape);
                
                ctx->SetStrokePaint(violinBorderColor);
                ctx->SetStrokeWidth(violinBorderWidth);
                ctx->DrawLinePath(violinShape, true);
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

void UltraCanvasJitterPlotElement::RenderRaincloudPlot(IRenderContext* ctx) {
    if (!ctx) return;
    
    size_t categoryIndex = 0;
    for (const auto& category : categories) {
        const auto& catDataList = GetCategoryDataList(category);
        
        size_t hueIndex = 0;
        for (const auto& catData : catDataList) {
            if (catData.values.empty()) {
                hueIndex++;
                continue;
            }
            
            std::vector<float> density = CalculateKernelDensity(catData.values, violinResolution);
            
            double minVal = *std::min_element(catData.values.begin(), catData.values.end());
            double maxVal = *std::max_element(catData.values.begin(), catData.values.end());
            double range = maxVal - minVal;
            
            if (range < 1e-6) {
                hueIndex++;
                continue;
            }
            
            float maxDensity = *std::max_element(density.begin(), density.end());
            if (maxDensity < 1e-6) {
                hueIndex++;
                continue;
            }
            
            float catPos = GetCategoryPosition(categoryIndex, hueIndex);
            float violinWidth = categoryWidth * 0.35f;
            
            std::vector<Point2Dd> halfViolinShape;
            
            if (horizontalOrientation) {
                for (int i = 0; i < violinResolution; ++i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    halfViolinShape.push_back(Point2Dd(valuePos, catPos + width));
                }
                for (int i = violinResolution - 1; i >= 0; --i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    halfViolinShape.push_back(Point2Dd(valuePos, catPos));
                }
            } else {
                for (int i = 0; i < violinResolution; ++i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    float width = (density[i] / maxDensity) * violinWidth;
                    
                    halfViolinShape.push_back(Point2Dd(catPos - width, valuePos));
                }
                for (int i = violinResolution - 1; i >= 0; --i) {
                    double value = minVal + (i * range / (violinResolution - 1));
                    float valuePos = GetValuePosition(value);
                    halfViolinShape.push_back(Point2Dd(catPos, valuePos));
                }
            }
            
            if (halfViolinShape.size() >= 3) {
                ctx->SetFillPaint(violinFillColor);
                ctx->FillLinePath(halfViolinShape);
                
                ctx->SetStrokePaint(violinBorderColor);
                ctx->SetStrokeWidth(violinBorderWidth);
                ctx->DrawLinePath(halfViolinShape, true);
            }
            
            hueIndex++;
        }
        
        categoryIndex++;
    }
}

// =============================================================================
// POSITION CALCULATION HELPERS
// =============================================================================

Point2Dd UltraCanvasJitterPlotElement::CalculatePointPosition(
    size_t categoryIndex, double value, float jitter, size_t hueIndex) {
    
    float catPos = GetCategoryPosition(categoryIndex, hueIndex);
    float valuePos = GetValuePosition(value);
    
    if (horizontalOrientation) {
        return Point2Dd(valuePos, catPos + jitter * categoryWidth * 0.4f);
    } else {
        return Point2Dd(catPos + jitter * categoryWidth * 0.4f, valuePos);
    }
}

float UltraCanvasJitterPlotElement::GetCategoryPosition(size_t categoryIndex, size_t hueIndex) {
    if (categories.empty()) return 0.0f;
    
    float totalCategories = static_cast<float>(categories.size());
    float categorySpan = horizontalOrientation ? 
        cachedPlotArea.height / totalCategories : 
        cachedPlotArea.width / totalCategories;
    
    float basePos = horizontalOrientation ?
        cachedPlotArea.y + (categoryIndex + 0.5f) * categorySpan :
        cachedPlotArea.x + (categoryIndex + 0.5f) * categorySpan;
    
    if (enableDodge && !hueCategories.empty() && hueIndex < hueCategories.size()) {
        float hueCount = static_cast<float>(hueCategories.size());
        float dodgeOffset = ((hueIndex / hueCount) - 0.5f) * categorySpan * dodgeWidth;
        basePos += dodgeOffset;
    }
    
    return basePos;
}

float UltraCanvasJitterPlotElement::GetValuePosition(double value) {
    if (cachedDataBounds.GetYRange() == 0) {
        return horizontalOrientation ?
            cachedPlotArea.x + cachedPlotArea.width * 0.5f :
            cachedPlotArea.y + cachedPlotArea.height * 0.5f;
    }
    
    float normalizedValue = 0.0f;
    
    if (yAxisScale == AxisScale::Logarithmic) {
        if (value <= 0) {
            normalizedValue = 0.0f;
        } else {
            double minVal = std::max(1.0, cachedDataBounds.minY);
            double maxVal = cachedDataBounds.maxY;
            
            double logMin = std::log(minVal) / std::log(logScaleBase);
            double logMax = std::log(maxVal) / std::log(logScaleBase);
            double logValue = std::log(value) / std::log(logScaleBase);
            
            normalizedValue = static_cast<float>((logValue - logMin) / (logMax - logMin));
        }
    } else if (yAxisScale == AxisScale::SymmetricLog) {
        auto symLog = [this](double v) -> double {
            return v >= 0 ? 
                std::log(1.0 + v) / std::log(logScaleBase) : 
                -std::log(1.0 - v) / std::log(logScaleBase);
        };
        
        double logMin = symLog(cachedDataBounds.minY);
        double logMax = symLog(cachedDataBounds.maxY);
        double logValue = symLog(value);
        
        normalizedValue = static_cast<float>((logValue - logMin) / (logMax - logMin));
    } else {
        normalizedValue = static_cast<float>(
            (value - cachedDataBounds.minY) / cachedDataBounds.GetYRange()
        );
    }
    
    normalizedValue = std::clamp(normalizedValue, 0.0f, 1.0f);
    
    if (horizontalOrientation) {
        return cachedPlotArea.x + normalizedValue * cachedPlotArea.width;
    } else {
        return cachedPlotArea.y + cachedPlotArea.height - 
               (normalizedValue * cachedPlotArea.height);
    }
}

// =============================================================================
// COLOR MANAGEMENT
// =============================================================================

Color UltraCanvasJitterPlotElement::GetPointColor(size_t categoryIndex, const std::string& hueValue) {
    if (!hueValue.empty() && hueColorMap.find(hueValue) != hueColorMap.end()) {
        return hueColorMap[hueValue];
    }
    
    if (!categoryData.empty()) {
        const auto& catDataList = GetCategoryDataList(categories[categoryIndex]);
        if (!catDataList.empty() && catDataList[0].customColor.a > 0) {
            return catDataList[0].customColor;
        }
    }
    
    return pointColor;
}

// =============================================================================
// DATA ACCESS HELPERS
// =============================================================================

std::vector<JitterCategoryData>& UltraCanvasJitterPlotElement::GetCategoryDataList(
    const std::string& category) {
    return categoryData[category];
}

const std::vector<JitterCategoryData>& UltraCanvasJitterPlotElement::GetCategoryDataList(
    const std::string& category) const {
    static std::vector<JitterCategoryData> emptyList;
    auto it = categoryData.find(category);
    return (it != categoryData.end()) ? it->second : emptyList;
}

// =============================================================================
// MOUSE HANDLING
// =============================================================================

bool UltraCanvasJitterPlotElement::HandleChartMouseMove(const Point2Di& mousePos) {
    if (!enableTooltips) return false;
    
    if (mousePos.x < cachedPlotArea.x || mousePos.x > cachedPlotArea.GetRight() ||
        mousePos.y < cachedPlotArea.y || mousePos.y > cachedPlotArea.GetBottom()) {
        UltraCanvasTooltipManager::HideTooltip();
        return false;
    }
    
    float closestDist = pointSize * 3.0f;
    std::string closestTooltip;
    
    for (const auto& pp : pointPositionsCache) {
        if (!selectedRegion.empty() && pp.region != selectedRegion) continue;
        if (pp.value < minScoreFilter) continue;
        
        float dx = mousePos.x - pp.x;
        float dy = mousePos.y - pp.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < closestDist) {
            closestDist = dist;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "Estado: " << pp.category << "\n";
            oss << "Region: " << pp.region << "\n";
            oss << "Score: " << pp.value;
            closestTooltip = oss.str();
        }
    }
    
    if (!closestTooltip.empty()) {
        Point2Di mpos(mousePos.x + 15, mousePos.y - 20);
        auto windowPos = MapFromLocal(mpos, nullptr);
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), closestTooltip, windowPos);
        return true;
    } else {
        UltraCanvasTooltipManager::HideTooltip();
    }
    
    return false;
}

// =============================================================================
// TOOLTIP GENERATION
// =============================================================================

std::string UltraCanvasJitterPlotElement::GenerateTooltipContent(
    const ChartDataPoint& point, size_t index) {
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    if (!point.category.empty()) {
        oss << "Category: " << point.category << "\n";
    }
    
    oss << "Value: " << point.y;
    
    if (!point.label.empty()) {
        oss << "\nLabel: " << point.label;
    }
    
    return oss.str();
}

// =============================================================================
// BEESWARM LAYOUT IMPLEMENTATION
// =============================================================================

std::vector<UltraCanvasJitterPlotElement::BeeswarmPoint> 
UltraCanvasJitterPlotElement::CalculateBeeswarmLayout(
    const std::vector<double>& values,
    float categoryCenter,
    float categoryWidth,
    float pointRadius)
{
    if (values.empty()) {
        return {};
    }
    
    // Sort indices by priority (if using Swarm method)
    std::vector<int> sortedIndices;
    if (beeswarmMethod == BeeswarmMethod::Swarm) {
        sortedIndices = SortIndicesByPriority(values, beeswarmPriority);
    }
    
    // Calculate layout based on method
    std::vector<BeeswarmPoint> points;
    
    switch (beeswarmMethod) {
        case BeeswarmMethod::Swarm:
            points = CalculateBeeswarmSwarm(values, sortedIndices, 
                                           categoryCenter, categoryWidth, pointRadius);
            break;
            
        case BeeswarmMethod::Center:
            points = CalculateBeeswarmCenter(values, categoryCenter, 
                                            categoryWidth, pointRadius);
            break;
            
        case BeeswarmMethod::Hex:
            points = CalculateBeeswarmHex(values, categoryCenter, 
                                         categoryWidth, pointRadius);
            break;
            
        case BeeswarmMethod::Square:
            points = CalculateBeeswarmSquare(values, categoryCenter, 
                                            categoryWidth, pointRadius);
            break;
    }
    
    // Apply corral strategy for runaway points
    float maxWidth = categoryWidth * beeswarmMaxWidth;
    ApplyCorralStrategy(points, categoryCenter, maxWidth, beeswarmCorral);
    
    // Apply side constraint (for raincloud plots)
    ApplySideConstraint(points, categoryCenter, beeswarmSide);
    
    return points;
}

// =============================================================================
// SWARM METHOD: GREEDY PLACEMENT (PRESERVES Y POSITIONS)
// =============================================================================

std::vector<UltraCanvasJitterPlotElement::BeeswarmPoint>
UltraCanvasJitterPlotElement::CalculateBeeswarmSwarm(
    const std::vector<double>& values,
    const std::vector<int>& sortedIndices,
    float categoryCenter,
    float categoryWidth,
    float pointRadius)
{
    std::vector<BeeswarmPoint> placed;
    placed.reserve(values.size());
    
    // FIX (v1.2.2): `categoryWidth` is the TOTAL pixel width of the category
    // slot. Points spiral OUT from `categoryCenter` toward both sides, so
    // each side only has half the width available. Previously this was
    // treating the full width as one-sided budget — combined with the
    // bug in RenderJitterPoints passing `1.0 * 0.8 = 0.8` pixels, the
    // search loop exited immediately.
    float maxHalfWidth = categoryWidth * 0.5f * beeswarmMaxWidth;
    float searchIncrement = pointRadius * 0.5f;
    
    // Place points in sorted order
    for (int idx : sortedIndices) {
        double yValue = values[idx];
        float yPos = GetValuePosition(yValue);
        
        // Try placing at center first
        Point2Dd testPos(categoryCenter, yPos);
        
        // Check if center position is free
        if (!CheckBeeswarmCollision(testPos, pointRadius, placed, beeswarmSpacing)) {
            placed.push_back({testPos, pointRadius, idx, yValue});
            continue;
        }
        
        // Spiral outward alternating left/right
        float offset = searchIncrement;
        int side = 1; // Start right
        bool foundPosition = false;
        
        while (offset < maxHalfWidth && !foundPosition) {
            float xPos = categoryCenter + (offset * side);
            testPos = Point2Dd(xPos, yPos);
            
            if (!CheckBeeswarmCollision(testPos, pointRadius, placed, beeswarmSpacing)) {
                placed.push_back({testPos, pointRadius, idx, yValue});
                foundPosition = true;
            } else {
                // Alternate sides
                if (side == 1) {
                    side = -1;
                } else {
                    side = 1;
                    offset += searchIncrement;
                }
            }
        }
        
        // If no position found (runaway point), place at max offset
        if (!foundPosition) {
            testPos = Point2Dd(categoryCenter + (maxHalfWidth * side), yPos);
            placed.push_back({testPos, pointRadius, idx, yValue});
        }
    }
    
    return placed;
}

// =============================================================================
// CENTER METHOD: SYMMETRIC GRID LAYOUT
// =============================================================================

std::vector<UltraCanvasJitterPlotElement::BeeswarmPoint>
UltraCanvasJitterPlotElement::CalculateBeeswarmCenter(
    const std::vector<double>& values,
    float categoryCenter,
    float categoryWidth,
    float pointRadius)
{
    std::vector<BeeswarmPoint> points;
    points.reserve(values.size());
    
    if (values.empty()) return points;
    
    // FIX (v1.2.2): bin in PIXEL space, not data-value space.
    // Convert every data value to its Y pixel first, then bin those pixels
    // into rows whose height matches one circle diameter (+ spacing).
    // Previously this method computed `numBins = (maxVal - minVal) / (pointRadius * 2)`
    // mixing data units (5.8 score points) with pixels (8 px), which
    // collapsed the entire distribution into 1 bin.
    
    struct YPixelEntry { int origIdx; float yPx; double yVal; };
    std::vector<YPixelEntry> entries;
    entries.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        entries.push_back({static_cast<int>(i), GetValuePosition(values[i]), values[i]});
    }
    
    float minYPx = entries[0].yPx;
    float maxYPx = entries[0].yPx;
    for (const auto& e : entries) {
        minYPx = std::min(minYPx, e.yPx);
        maxYPx = std::max(maxYPx, e.yPx);
    }
    
    float binHeight = pointRadius * 2.0f + beeswarmSpacing;
    int numBins = std::max(1, static_cast<int>((maxYPx - minYPx) / binHeight) + 1);
    
    std::vector<std::vector<int>> bins(numBins);
    for (const auto& e : entries) {
        int binIdx = static_cast<int>((e.yPx - minYPx) / binHeight);
        binIdx = std::clamp(binIdx, 0, numBins - 1);
        bins[binIdx].push_back(e.origIdx);
    }
    
    // Place points in each bin symmetrically around categoryCenter
    float spacing = pointRadius * 2.0f + beeswarmSpacing;
    for (int binIdx = 0; binIdx < numBins; ++binIdx) {
        if (bins[binIdx].empty()) continue;
        
        // Bin center in pixel Y
        float binY = minYPx + binIdx * binHeight + binHeight * 0.5f;
        int count = static_cast<int>(bins[binIdx].size());
        
        float totalWidth = (count - 1) * spacing;
        float startX = categoryCenter - totalWidth * 0.5f;
        
        for (int col = 0; col < count; ++col) {
            int idx = bins[binIdx][col];
            float xPos = startX + col * spacing;
            
            points.push_back({
                Point2Dd(xPos, binY),
                pointRadius,
                idx,
                values[idx]
            });
        }
    }
    
    return points;
}

// =============================================================================
// HEX METHOD: HEXAGONAL GRID LAYOUT
// =============================================================================

std::vector<UltraCanvasJitterPlotElement::BeeswarmPoint>
UltraCanvasJitterPlotElement::CalculateBeeswarmHex(
    const std::vector<double>& values,
    float categoryCenter,
    float categoryWidth,
    float pointRadius)
{
    std::vector<BeeswarmPoint> points;
    points.reserve(values.size());
    
    if (values.empty()) return points;
    
    // FIX (v1.2.2): bin in PIXEL space, not data-value space.
    // Hex packing uses bin height = pointRadius * sqrt(3) which gives a
    // ~13.4% tighter vertical packing than Center/Square (cos(30°) factor),
    // and alternates rows by spacing/2 horizontally so circles nest into
    // the gaps of the row above.
    
    struct YPixelEntry { int origIdx; float yPx; double yVal; };
    std::vector<YPixelEntry> entries;
    entries.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        entries.push_back({static_cast<int>(i), GetValuePosition(values[i]), values[i]});
    }
    
    float minYPx = entries[0].yPx;
    float maxYPx = entries[0].yPx;
    for (const auto& e : entries) {
        minYPx = std::min(minYPx, e.yPx);
        maxYPx = std::max(maxYPx, e.yPx);
    }
    
    float binHeight = pointRadius * std::sqrt(3.0f) + beeswarmSpacing * 0.5f;
    int numBins = std::max(1, static_cast<int>((maxYPx - minYPx) / binHeight) + 1);
    
    std::vector<std::vector<int>> bins(numBins);
    for (const auto& e : entries) {
        int binIdx = static_cast<int>((e.yPx - minYPx) / binHeight);
        binIdx = std::clamp(binIdx, 0, numBins - 1);
        bins[binIdx].push_back(e.origIdx);
    }
    
    float spacing = pointRadius * 2.0f + beeswarmSpacing;
    for (int binIdx = 0; binIdx < numBins; ++binIdx) {
        if (bins[binIdx].empty()) continue;
        
        float binY = minYPx + binIdx * binHeight + binHeight * 0.5f;
        int count = static_cast<int>(bins[binIdx].size());
        
        // Hex offset: odd rows shift right by half spacing → nested packing
        float rowOffset = (binIdx % 2 == 0) ? 0.0f : spacing * 0.5f;
        float totalWidth = (count - 1) * spacing;
        float startX = categoryCenter - totalWidth * 0.5f + rowOffset;
        
        for (int col = 0; col < count; ++col) {
            int idx = bins[binIdx][col];
            float xPos = startX + col * spacing;
            
            points.push_back({
                Point2Dd(xPos, binY),
                pointRadius,
                idx,
                values[idx]
            });
        }
    }
    
    return points;
}

// =============================================================================
// SQUARE METHOD: REGULAR SQUARE GRID LAYOUT
// =============================================================================

std::vector<UltraCanvasJitterPlotElement::BeeswarmPoint>
UltraCanvasJitterPlotElement::CalculateBeeswarmSquare(
    const std::vector<double>& values,
    float categoryCenter,
    float categoryWidth,
    float pointRadius)
{
    std::vector<BeeswarmPoint> points;
    points.reserve(values.size());
    
    if (values.empty()) return points;
    
    // FIX (v1.2.2): bin in PIXEL space, not data-value space.
    // Square = Center but every row uses the same horizontal offset (no
    // alternating shift) → regular rectangular lattice.
    
    struct YPixelEntry { int origIdx; float yPx; double yVal; };
    std::vector<YPixelEntry> entries;
    entries.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        entries.push_back({static_cast<int>(i), GetValuePosition(values[i]), values[i]});
    }
    
    float minYPx = entries[0].yPx;
    float maxYPx = entries[0].yPx;
    for (const auto& e : entries) {
        minYPx = std::min(minYPx, e.yPx);
        maxYPx = std::max(maxYPx, e.yPx);
    }
    
    float binHeight = pointRadius * 2.0f + beeswarmSpacing;
    int numBins = std::max(1, static_cast<int>((maxYPx - minYPx) / binHeight) + 1);
    
    std::vector<std::vector<int>> bins(numBins);
    for (const auto& e : entries) {
        int binIdx = static_cast<int>((e.yPx - minYPx) / binHeight);
        binIdx = std::clamp(binIdx, 0, numBins - 1);
        bins[binIdx].push_back(e.origIdx);
    }
    
    float spacing = pointRadius * 2.0f + beeswarmSpacing;
    for (int binIdx = 0; binIdx < numBins; ++binIdx) {
        if (bins[binIdx].empty()) continue;
        
        float binY = minYPx + binIdx * binHeight + binHeight * 0.5f;
        int count = static_cast<int>(bins[binIdx].size());
        
        // FIX (v1.2.3): R's `square` method subtracts `floor(mean(1..n))`
        // from the column sequence, NOT `mean(1..n)` (which is what `center`
        // does). For odd counts the two are equal; for even counts square
        // gives an asymmetric offset that aligns column positions across
        // rows, producing the regular rectangular lattice that visually
        // distinguishes Square from Center.
        //   Sequence 1..count → (1..count) - floor((count+1)/2)
        //   count=3:  1,2,3 - floor(2) = -1, 0, 1   (symmetric, same as center)
        //   count=4:  1,2,3,4 - floor(2.5)=2 → -1, 0, 1, 2   (asymmetric)
        //   count=5:  1,2,3,4,5 - 3 = -2,-1,0,1,2   (symmetric)
        //   count=6:  1..6 - floor(3.5)=3 → -2,-1,0,1,2,3   (asymmetric)
        // We compute the leftmost offset directly: (1 - floor((count+1)/2)).
        int floorMean = (count + 1) / 2;          // = floor((count+1)/2.0)
        float colOffset0 = static_cast<float>(1 - floorMean);  // offset of first column
        float startX = categoryCenter + colOffset0 * spacing;
        
        for (int col = 0; col < count; ++col) {
            int idx = bins[binIdx][col];
            float xPos = startX + col * spacing;
            
            points.push_back({
                Point2Dd(xPos, binY),
                pointRadius,
                idx,
                values[idx]
            });
        }
    }
    
    return points;
}

// =============================================================================
// COLLISION DETECTION
// =============================================================================

bool UltraCanvasJitterPlotElement::CheckBeeswarmCollision(
    const Point2Dd& testPos,
    float testRadius,
    const std::vector<BeeswarmPoint>& placedPoints,
    float spacing)
{
    for (const auto& p : placedPoints) {
        float dx = testPos.x - p.position.x;
        float dy = testPos.y - p.position.y;
        float distSq = dx * dx + dy * dy;
        
        float minDist = testRadius + p.radius + spacing;
        float minDistSq = minDist * minDist;
        
        if (distSq < minDistSq) {
            return true;
        }
    }
    
    return false;
}

// =============================================================================
// PRIORITY SORTING
// =============================================================================

std::vector<int> UltraCanvasJitterPlotElement::SortIndicesByPriority(
    const std::vector<double>& values,
    BeeswarmPriority priority)
{
    std::vector<int> indices(values.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    switch (priority) {
        case BeeswarmPriority::Ascending:
            std::sort(indices.begin(), indices.end(),
                     [&values](int a, int b) { return values[a] < values[b]; });
            break;
            
        case BeeswarmPriority::Descending:
            std::sort(indices.begin(), indices.end(),
                     [&values](int a, int b) { return values[a] > values[b]; });
            break;
            
        case BeeswarmPriority::Dense:
            {
                auto bounds = CalculateDataBounds();
                double range = bounds.maxY - bounds.minY;
                double bandwidth = range * 0.1;
                std::vector<double> densities(values.size());
                
                for (int i = 0; i < values.size(); ++i) {
                    densities[i] = CalculateLocalDensity(values, i, bandwidth);
                }
                
                std::sort(indices.begin(), indices.end(),
                         [&densities](int a, int b) { return densities[a] > densities[b]; });
            }
            break;
            
        case BeeswarmPriority::Compact:
            // Keep original order (compact mode uses original order)
            break;
            
        case BeeswarmPriority::Random:
            std::shuffle(indices.begin(), indices.end(), randomGenerator);
            break;
    }
    
    return indices;
}

// =============================================================================
// LOCAL DENSITY CALCULATION
// =============================================================================

double UltraCanvasJitterPlotElement::CalculateLocalDensity(
    const std::vector<double>& values,
    int index,
    double bandwidth)
{
    if (bandwidth <= 0.0) return 0.0;
    
    double density = 0.0;
    double target = values[index];
    
    for (double v : values) {
        double diff = (v - target) / bandwidth;
        density += std::exp(-0.5 * diff * diff);
    }
    
    return density / (values.size() * bandwidth * std::sqrt(2.0 * M_PI));
}

// =============================================================================
// CORRAL STRATEGIES
// =============================================================================

void UltraCanvasJitterPlotElement::ApplyCorralStrategy(
    std::vector<BeeswarmPoint>& points,
    float categoryCenter,
    float maxWidth,
    BeeswarmCorral corral)
{
    if (corral == BeeswarmCorral::NoneCorral) {
        return;
    }
    
    float leftBound = categoryCenter - maxWidth * 0.5f;
    float rightBound = categoryCenter + maxWidth * 0.5f;
    
    for (auto& pt : points) {
        float offset = pt.position.x - categoryCenter;
        
        if (std::abs(offset) > maxWidth * 0.5f) {
            switch (corral) {
                case BeeswarmCorral::Open:
                    // Points can escape - no action needed
                    break;
                    
                case BeeswarmCorral::Wrap:
                    while (pt.position.x > rightBound) {
                        pt.position.x -= maxWidth;
                    }
                    while (pt.position.x < leftBound) {
                        pt.position.x += maxWidth;
                    }
                    break;
                    
                case BeeswarmCorral::Clamp:
                    pt.position.x = std::clamp(pt.position.x, static_cast<double>(leftBound), static_cast<double>(rightBound));
                    break;

                default:
                    // BeeswarmCorral::NoneCorral - no constraint
                    break;
            }
        }
    }
    
    if (corral == BeeswarmCorral::Clamp) {
        points.erase(
            std::remove_if(points.begin(), points.end(),
                          [](const BeeswarmPoint& p) { return p.radius <= 0.0f; }),
            points.end()
        );
    }
}

// =============================================================================
// SIDE CONSTRAINT
// =============================================================================

void UltraCanvasJitterPlotElement::ApplySideConstraint(
    std::vector<BeeswarmPoint>& points,
    float categoryCenter,
    BeeswarmSide side)
{
    if (side == BeeswarmSide::Both) {
        return;
    }
    
    for (auto& pt : points) {
        float offset = pt.position.x - categoryCenter;
        
        if (side == BeeswarmSide::Left && offset > 0) {
            pt.position.x = categoryCenter - std::abs(offset);
        } else if (side == BeeswarmSide::Right && offset < 0) {
            pt.position.x = categoryCenter + std::abs(offset);
        }
    }
}

} // namespace UltraCanvas