# UltraCanvasFinancialChartElement Documentation

## Overview

The `UltraCanvasFinancialChartElement` is a sophisticated financial charting component within the UltraCanvas framework designed for displaying stock market, cryptocurrency, and forex data. It provides professional-grade OHLC (Open, High, Low, Close) candlestick charts with integrated volume visualization, moving averages, and interactive features.

**Version:** 1.3.0  
**Last Modified:** 2025-09-15  
**Author:** UltraCanvas Framework  
**Namespace:** `UltraCanvas`  
**Header:** `include/Plugins/Charts/UltraCanvasFinancialChart.h`  
**Implementation:** `Plugins/Charts/UltraCanvasFinancialChart.cpp`

## Features

### Core Capabilities
- **Multiple Display Styles**: Candlestick, OHLC Bars, and Heikin-Ashi charts
- **Volume Visualization**: Integrated volume panel with customizable height ratio
- **Moving Averages**: Price and volume moving averages with configurable periods
- **Interactive Features**: Tooltips, zooming, and panning capabilities
- **Customizable Styling**: Full control over colors, sizes, and visual appearance
- **Grid System**: Optional grid lines for better data reading
- **Axis Labels**: Automatic scaling and labeling for price and volume axes

### Display Modes
1. **Candlestick Chart**: Traditional Japanese candlesticks showing open, high, low, close
2. **OHLC Bars**: Open-High-Low-Close bars for cleaner visualization
3. **Heikin-Ashi**: Smoothed candlesticks for trend identification

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasFinancialChartElement
```

## Data Structures

### FinancialChartDataPoint

Represents a single financial data point with OHLC values and volume.

```cpp
struct FinancialChartDataPoint : public ChartDataPoint {
    double open;     // Opening price
    double high;     // Highest price in period
    double low;      // Lowest price in period
    double close;    // Closing price
    double volume;   // Trading volume
    std::string date; // Date/timestamp string
};
```

### FinancialChartDataVector

Container class for managing collections of financial data points.

```cpp
class FinancialChartDataVector : public IChartDataSource {
    // Methods for data management
    void LoadFromCSV(const std::string& filePath);
    void LoadFinancialData(const std::vector<FinancialChartDataPoint>& data);
    void AddFinancialPoint(const FinancialChartDataPoint& point);
    void Clear();
};
```

## Constructor

```cpp
UltraCanvasFinancialChartElement(
    const std::string& id,  // Unique identifier
    long uid,               // Unique numeric ID
    int x,                  // X position
    int y,                  // Y position
    int width,              // Chart width
    int height              // Chart height
);
```

## Configuration Methods

### Chart Style Configuration

| Method | Description | Parameters |
|--------|-------------|------------|
| `SetCandleDisplayStyle()` | Sets the chart display style | `CandleDisplayStyle` enum value |
| `SetBullishCandleColor()` | Sets color for rising prices | `Color` object |
| `SetBearishCandleColor()` | Sets color for falling prices | `Color` object |
| `SetWickLineColor()` | Sets color for candle wicks | `Color` object |
| `SetCandleWidthRatio()` | Sets candle width relative to spacing | `float` (0.1 - 1.0) |

### Volume Panel Configuration

| Method | Description | Parameters |
|--------|-------------|------------|
| `SetShowVolumePanel()` | Toggles volume panel visibility | `bool` |
| `SetVolumePanelHeightRatio()` | Sets volume panel height ratio | `float` (0.1 - 0.5) |
| `SetVolumeBarColor()` | Sets volume bar color | `Color` object |
| `SetVolumeChartBackgroundColor()` | Sets volume panel background | `Color` object |
| `SetVolumeChartBorderColor()` | Sets volume panel border color | `Color` object |
| `SetShowVolumeAxisLabels()` | Toggles volume axis labels | `bool` |
| `SetShowVolumeBorder()` | Toggles volume panel border | `bool` |
| `SetVolumeSeparationHeight()` | Sets gap between price and volume panels | `int` (5-30 pixels) |

### Moving Average Configuration

| Method | Description | Parameters |
|--------|-------------|------------|
| `SetShowMovingAverage()` | Enables/disables price moving average | `bool show`, `int period` |
| `SetMovingAverageColor()` | Sets moving average line color | `Color` object |
| `SetShowVolumeMovingAverage()` | Enables/disables volume moving average | `bool show`, `int period` |
| `SetVolumeMovingAverageColor()` | Sets volume moving average color | `Color` object |

### Data Management

| Method | Description | Parameters |
|--------|-------------|------------|
| `SetFinancialDataSource()` | Sets the data source | `shared_ptr<FinancialChartDataVector>` |
| `GetFinancialDataSource()` | Retrieves current data source | None |
| `SetChartTitle()` | Sets the chart title | `string` |

## Enumerations

### CandleDisplayStyle

```cpp
enum class CandleDisplayStyle {
    Candlestick,  // Traditional candlestick chart
    OHLCBars,     // Open-High-Low-Close bars
    HeikinAshi    // Smoothed Heikin-Ashi candles
};
```

## Factory Functions

### CreateFinancialChartElement

Creates a basic financial chart element.

```cpp
std::shared_ptr<UltraCanvasFinancialChartElement> CreateFinancialChartElement(
    const std::string& id,
    long uid,
    int x, int y,
    int width, int height
);
```

### CreateFinancialChartWithData

Creates a financial chart with pre-loaded data.

```cpp
std::shared_ptr<UltraCanvasFinancialChartElement> CreateFinancialChartWithData(
    const std::string& id,
    long uid,
    int x, int y,
    int width, int height,
    std::shared_ptr<FinancialChartDataVector> data,
    const std::string& title = ""
);
```

## Usage Examples

### Basic Chart Creation

```cpp
// Create a financial chart element
auto stockChart = CreateFinancialChartElement(
    "StockChart",  // ID
    1001,          // UID
    20, 50,        // Position
    980, 400       // Size
);

// Configure the chart
stockChart->SetChartTitle("AAPL - Apple Inc. (90 Day Chart)");
stockChart->SetCandleDisplayStyle(
    UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick
);
```

### Loading and Displaying Data

```cpp
// Create data container
auto stockData = std::make_shared<FinancialChartDataVector>();

// Add data points
stockData->AddFinancialPoint(
    FinancialChartDataPoint(
        1.0,      // Time index
        100.50,   // Open
        105.20,   // High
        99.80,    // Low
        104.30,   // Close
        1500000,  // Volume
        "2025-01-01"
    )
);

// Set data source
stockChart->SetFinancialDataSource(stockData);
```

### Customizing Appearance

```cpp
// Color configuration
stockChart->SetBullishCandleColor(Color(0, 180, 0, 255));    // Green
stockChart->SetBearishCandleColor(Color(220, 0, 0, 255));    // Red
stockChart->SetWickLineColor(Color(100, 100, 100, 255));     // Gray
stockChart->SetVolumeBarColor(Color(150, 150, 150, 100));    // Semi-transparent

// Volume panel configuration
stockChart->SetShowVolumePanel(true);
stockChart->SetVolumePanelHeightRatio(0.25f);  // 25% of chart height
stockChart->SetShowVolumeBorder(true);
stockChart->SetVolumeSeparationHeight(15);      // 15px gap

// Moving averages
stockChart->SetShowMovingAverage(true, 20);     // 20-day MA
stockChart->SetMovingAverageColor(Color(0, 100, 200, 255));
```

### Interactive Features

```cpp
// Enable interactive features
stockChart->SetEnableTooltips(true);
stockChart->SetEnableZoom(true);
stockChart->SetEnablePan(true);

// Grid display
stockChart->SetShowGrid(true);
stockChart->SetGridColor(Color(210, 210, 210, 255));
```

### Complete Example Application

```cpp
class FinancialChartApp : public UltraCanvasApplication {
private:
    std::shared_ptr<UltraCanvasFinancialChartElement> chart;
    std::shared_ptr<FinancialChartDataVector> data;

public:
    bool Initialize() override {
        // Generate sample data
        data = GenerateSampleStockData("ULTR", 90);
        
        // Create chart
        chart = CreateFinancialChartElement(
            "MainChart", 1000, 20, 50, 980, 400
        );
        
        // Configure chart
        chart->SetFinancialDataSource(data);
        chart->SetChartTitle("ULTR - UltraCanvas Inc.");
        chart->SetCandleDisplayStyle(
            UltraCanvasFinancialChartElement::CandleDisplayStyle::Candlestick
        );
        
        // Styling
        chart->SetBullishCandleColor(Color(0, 150, 0, 255));
        chart->SetBearishCandleColor(Color(200, 0, 0, 255));
        
        // Features
        chart->SetShowVolumePanel(true);
        chart->SetShowMovingAverage(true, 20);
        chart->SetEnableTooltips(true);
        chart->SetEnableZoom(true);
        
        // Add to container
        mainContainer->AddChild(chart);
        
        return true;
    }
};
```

## Rendering Pipeline

The financial chart uses a sophisticated multi-layer rendering approach:

1. **Background Rendering**: Chart background and title
2. **Price Chart Rendering**:
   - Background and border
   - Grid lines (optional)
   - Axis labels
   - Financial data (candles/bars)
   - Moving average overlay
3. **Volume Chart Rendering**:
   - Volume panel background
   - Volume grid and axes
   - Volume bars
   - Volume moving average

### Key Rendering Methods

| Method | Description | Access |
|--------|-------------|--------|
| `RenderChart()` | Main rendering entry point | Public (override) |
| `RenderPriceChart()` | Renders price chart area | Private |
| `RenderVolumeChart()` | Renders volume panel | Private |
| `RenderFinancialData()` | Renders candles/bars | Private |
| `DrawCandlestickCandle()` | Draws individual candlestick | Private |
| `DrawOHLCBar()` | Draws OHLC bar | Private |
| `RenderMovingAverageData()` | Renders moving average line | Private |

## Event Handling

The chart supports various interactive events inherited from `UltraCanvasChartElementBase`:

- **Mouse Events**: Hover for tooltips, drag for panning
- **Wheel Events**: Zooming in/out
- **Keyboard Events**: Can be extended for custom shortcuts

## Performance Considerations

- **Caching**: Rendering areas are cached and only recalculated when necessary
- **Efficient Data Structures**: Optimized vector-based storage for financial data
- **Selective Rendering**: Only visible data points are rendered
- **Double Buffering**: Inherited from base framework for smooth updates

## Dependencies

- `UltraCanvasCommonTypes.h`: Basic type definitions
- `UltraCanvasUIElement.h`: Base UI element functionality
- `UltraCanvasRenderContext.h`: Rendering interface
- `UltraCanvasChartElementBase.h`: Base chart functionality
- `UltraCanvasChartDataStructures.h`: Chart data interfaces

## Platform Support

The financial chart component is fully cross-platform and works on:
- Windows (Win32)
- Linux (X11/Wayland)
- macOS (Cocoa)
- Future: Android support planned

## Best Practices

1. **Data Loading**: Load all data before rendering for best performance
2. **Color Selection**: Use contrasting colors for bullish/bearish candles
3. **Volume Panel**: Keep height ratio between 0.2-0.3 for optimal visibility
4. **Moving Averages**: Common periods are 20, 50, and 200 days
5. **Grid Lines**: Enable for easier value reading, disable for cleaner look
6. **Tooltips**: Always enable for interactive data exploration

## Limitations

- Heikin-Ashi mode currently renders as simplified candlesticks
- Maximum recommended data points: ~10,000 for smooth performance
- Volume data is optional but recommended for complete visualization
- Moving average requires minimum data points equal to the period

## Future Enhancements

- Additional technical indicators (RSI, MACD, Bollinger Bands)
- Multiple moving average overlays
- Customizable time frame aggregation
- Real-time data streaming support
- Export functionality (PNG, SVG, PDF)
- Comparison mode for multiple securities

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.3.0 | 2025-09-15 | Enhanced volume panel with separate rendering |
| 1.2.0 | 2025-08-01 | Added moving averages and Heikin-Ashi support |
| 1.1.0 | 2025-06-15 | Implemented interactive features (zoom, pan, tooltips) |
| 1.0.0 | 2025-05-01 | Initial release with basic candlestick and OHLC charts |
