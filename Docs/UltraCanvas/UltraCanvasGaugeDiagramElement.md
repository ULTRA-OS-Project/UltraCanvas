# UltraCanvasGaugeDiagramElement Documentation

## Overview

**UltraCanvasGaugeDiagramElement** is a single, multi-mode gauge widget that renders 17 different gauge styles from one class. It covers analog dials (speedometer, semicircular, quadrant, compass, analog clock, stopwatch), progress / linear bars (rounded bar, LED meter, segmented brick, multi-pointer, arrow pointer, linear scale, circular ring), and specialized indicators (battery, thermometer, cylinder, digital LED display). Every mode shares the same value model (`min` / `max` / `current`), the same color-range and threshold system, and the same title / unit / value formatting, so switching mode is a single `SetMode()` call.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 2.0.0

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasGaugeDiagramElement
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
```

## Features

- **17 gauge modes** from one class, selected with `SetMode()`
- **Unified value model**: `SetMinValue` / `SetMaxValue` / `SetValue`, with an `onGaugeValueChange` callback
- **Color ranges** (`AddRange`) paint bands of the scale; the value color is picked from the band it falls in
- **Thresholds** (`AddThreshold`) place labeled markers along the scale (used by `LinearScale`)
- **External pointers** (`AddExternalPointer`) draw extra arrows beside linear bars (multi-layer indicators)
- **Sub-dials** (`SetSubDial`) embed a small secondary gauge inside an analog face (e.g. RPM in a speedometer, minutes in a stopwatch)
- **Horizontal or vertical** orientation for every linear mode
- **Configurable arc sweep** for analog dials via `SetArcAngles`
- **Tick control**: major / minor counts, inside / outside / both placement, needle styles
- **Live time modes**: `AnalogClock` reads system time; `Stopwatch` has Start / Stop / Reset controls
- **Value formatting**: decimal places, unit suffix, optional glow / lightning-bolt / labels toggles

## Gauge Modes

| `GaugeMode` value     | Family       | Typical use                                  |
|-----------------------|--------------|----------------------------------------------|
| `Speedometer`         | Analog       | 270° dial with needle (car speed)            |
| `Semicircular`        | Analog       | 180° arc with needle (sales, temperature)    |
| `Quadrant`            | Analog       | 90° arc (power output)                        |
| `Compass`             | Analog       | 360° compass with cardinal directions        |
| `AnalogClock`         | Analog       | Live clock with hour/minute/second hands     |
| `Stopwatch`           | Analog       | Stopwatch with optional minutes sub-dial     |
| `LinearBar`           | Progress     | Rounded horizontal/vertical bar (download %) |
| `LinearLED`           | Progress     | Segmented LED bar (VU meter, signal)         |
| `LinearSegmented`     | Progress     | Brick-style segmented bar (business KPI)     |
| `LinearMultiPointer`  | Progress     | Bar with multiple external pointers          |
| `LinearWithArrow`     | Progress     | Bar with an external arrow + value label     |
| `LinearScale`         | Progress     | Linear scale with named markers (tuner)      |
| `CircularRing`        | Progress     | 360° progress ring                           |
| `Battery`             | Specialized  | Battery shape (bar or LED fill)              |
| `Thermometer`         | Specialized  | Vertical thermometer with bulb               |
| `Cylinder`            | Specialized  | 3D-style cylindrical tank                    |
| `Digital`             | Specialized  | LED / LCD numeric display                    |

## Data Structures

### Enumerations

```cpp
enum class GaugeNeedleStyle {
    Classic, Thin, Arrow, Triangle,
    NoNeedle        // 'None' avoided — X11 #defines None as a macro
};

enum class GaugeTickPosition {
    NoTicks, Inside, Outside, Both
};

enum class GaugeOrientation {
    Horizontal, Vertical
};

enum class GaugeBatteryStyle {
    BarPointer,     // Continuous fill
    LedPointer      // Discrete LED segments
};
```

### GaugeRangeSegment

A colored band of the scale. The value's color is taken from the band that contains it.

```cpp
struct GaugeRangeSegment {
    double      startValue;
    double      endValue;
    Color       color;
    std::string label;

    GaugeRangeSegment(double s, double e, const Color& c, const std::string& l = "");
};
```

### GaugeThreshold

A labeled marker at a single value (used by `LinearScale` and the analog threshold markers).

```cpp
struct GaugeThreshold {
    double      value;
    Color       color;
    std::string label;

    GaugeThreshold(double v, const Color& c, const std::string& l = "");
};
```

### GaugeExternalPointer

An extra pointer drawn beside a linear bar (multi-layer indicators).

```cpp
struct GaugeExternalPointer {
    double      value;
    Color       color;
    std::string label;
    bool        showOnLeft;     // Draw on the left side of the bar

    GaugeExternalPointer(double v, const Color& c, const std::string& l, bool left = false);
};
```

### GaugeSubDial

A secondary gauge embedded inside an analog face.

```cpp
struct GaugeSubDial {
    bool        enabled      = false;
    GaugeMode   mode         = GaugeMode::Semicircular;
    double      currentValue = 0.0;
    double      minValue     = 0.0;
    double      maxValue     = 100.0;
    std::string title;
    std::string unit;
    double      relativeSize  = 0.3;    // Fraction of the main radius
    double      offsetXRatio  = 0.0;    // Position relative to main center
    double      offsetYRatio  = 0.2;
};
```

## Class Reference

### Constructor

```cpp
UltraCanvasGaugeDiagramElement(const std::string& id,
                               float x, float y, float width, float height);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasGaugeDiagramElement> CreateGaugeDiagramElement(
        const std::string& id, long x, long y, long width, long height);
```

### Mode & Value

```cpp
void   SetMode(GaugeMode m);
GaugeMode GetMode() const;

void   SetValue(double val);
double GetValue() const;
void   SetMinValue(double v);
void   SetMaxValue(double v);
```

### Title & Unit

```cpp
void SetTitle(const std::string& t);
void SetUnit(const std::string& u);
```

### Visual Style

```cpp
void SetNeedleStyle(GaugeNeedleStyle s);
void SetTickPosition(GaugeTickPosition p);
void SetOrientation(GaugeOrientation o);
void SetBatteryStyle(GaugeBatteryStyle s);
void SetArcAngles(double startDeg, double endDeg);   // Analog sweep
```

### Colors

```cpp
void SetGaugeColor(const Color& c);        // Fill / needle accent
void SetBackgroundColor(const Color& c);
void SetTextColor(const Color& c);
```

### Ranges, Thresholds & External Pointers

```cpp
void AddRange(const GaugeRangeSegment& range);
void ClearRanges();

void AddThreshold(const GaugeThreshold& t);
void ClearThresholds();

void AddExternalPointer(const GaugeExternalPointer& p);
void ClearExternalPointers();
```

### Sub-Dial

```cpp
void SetSubDial(const GaugeSubDial& sub);
void SetSubDialValue(double val);
void SetSubDialEnabled(bool en);
```

### Display Options

```cpp
void SetDecimalPlaces(int places);
void SetShowGlow(bool glow);
void SetShowBolt(bool show);        // Lightning bolt (battery charging)
void SetShowLabels(bool show);
void SetMajorTickCount(int count);
void SetMinorTickCount(int count);
void SetSegmentCount(int count);    // LED / segmented / brick modes
```

### Stopwatch Controls

```cpp
void StopwatchStart();
void StopwatchStop();
void StopwatchReset();
bool IsStopwatchRunning() const;
```

### Callbacks

```cpp
std::function<void(double)> onGaugeValueChange;
```

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasGaugeExamples.cpp`.

### Speedometer — analog dial with needle

```cpp
auto speedo = CreateGaugeDiagramElement("speedo", 0, 0, 272, 374);
speedo->SetMode(GaugeMode::Speedometer);
speedo->SetTitle("Speed");
speedo->SetUnit("km/h");
speedo->SetMaxValue(240.0);
speedo->SetMajorTickCount(6);
speedo->SetValue(109.0);
```

### Semicircular — 180° arc with color ranges

```cpp
auto semi = CreateGaugeDiagramElement("semi", 0, 0, 272, 374);
semi->SetMode(GaugeMode::Semicircular);
semi->SetTitle("Sales Assessment");
semi->SetUnit("k$");
semi->SetMinValue(0.0);
semi->SetMaxValue(100.0);
semi->SetMajorTickCount(5);
semi->AddRange(GaugeRangeSegment(0.0,  50.0,  Color(100, 180, 255, 255)));
semi->AddRange(GaugeRangeSegment(50.0, 100.0, Color( 80, 200, 120, 255)));
semi->SetValue(73.0);
```

### Compass — with an embedded wind-speed sub-dial

```cpp
auto compass = CreateGaugeDiagramElement("compass", 0, 0, 272, 374);
compass->SetMode(GaugeMode::Compass);
compass->SetTitle("Wind Direction");
compass->SetMaxValue(360.0);
compass->SetNeedleStyle(GaugeNeedleStyle::Thin);

GaugeSubDial wind;
wind.enabled      = true;
wind.mode         = GaugeMode::Semicircular;
wind.title        = "Wind";
wind.minValue     = 0.0;
wind.maxValue     = 25.0;
wind.currentValue = 12.0;
wind.unit         = "m/s";
compass->SetSubDial(wind);

compass->SetValue(120.0);   // bearing in degrees
```

### Stopwatch — Start / Stop / Reset with a live callback

```cpp
auto stopwatch = CreateGaugeDiagramElement("sw", 0, 0, 272, 374);
stopwatch->SetMode(GaugeMode::Stopwatch);
stopwatch->SetTitle("Stopwatch");
stopwatch->SetMaxValue(60.0);
stopwatch->SetMajorTickCount(12);

GaugeSubDial mins;
mins.enabled  = true;
mins.mode     = GaugeMode::Semicircular;
mins.title    = "min";
mins.maxValue = 30.0;
mins.unit     = "m";
stopwatch->SetSubDial(mins);

startButton->SetOnClick([gauge = stopwatch.get()] { gauge->StopwatchStart(); });
stopButton ->SetOnClick([gauge = stopwatch.get()] { gauge->StopwatchStop();  });
resetButton->SetOnClick([gauge = stopwatch.get()] { gauge->StopwatchReset(); });

stopwatch->onGaugeValueChange = [label](double seconds) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << seconds << " s";
    label->SetText(oss.str());
};
```

### Linear Bar — rounded horizontal progress bar

```cpp
auto bar = CreateGaugeDiagramElement("bar", 0, 0, 272, 374);
bar->SetMode(GaugeMode::LinearBar);
bar->SetTitle("Download Progress");
bar->SetUnit("%");
bar->SetGaugeColor(Color(0, 140, 255, 255));
bar->SetValue(65.0);
```

### LED VU Meter — vertical segmented bar

```cpp
auto led = CreateGaugeDiagramElement("led", 0, 0, 272, 374);
led->SetMode(GaugeMode::LinearLED);
led->SetOrientation(GaugeOrientation::Vertical);
led->SetTitle("VU Meter");
led->SetUnit("%");
led->SetSegmentCount(16);
led->SetGaugeColor(Color(0, 200, 255, 255));
led->AddRange(GaugeRangeSegment(0.0,  80.0,  Color(0, 200, 255, 255)));   // normal
led->AddRange(GaugeRangeSegment(80.0, 100.0, Color(255, 100, 80, 255)));  // hot
led->SetValue(65.0);
```

### Segmented Brick — business metric

```cpp
auto brick = CreateGaugeDiagramElement("brick", 0, 0, 272, 374);
brick->SetMode(GaugeMode::LinearSegmented);
brick->SetTitle("Revenue");
brick->SetUnit("M$");
brick->SetMaxValue(20.0);
brick->SetSegmentCount(20);
brick->SetDecimalPlaces(2);
brick->AddRange(GaugeRangeSegment(0.0,  10.0, Color(255, 140, 80, 255)));
brick->AddRange(GaugeRangeSegment(10.0, 20.0, Color(0, 180, 220, 255)));
brick->SetValue(12.45);
```

### Multi-Pointer — stacked external pointers

```cpp
auto multi = CreateGaugeDiagramElement("multi", 0, 0, 272, 374);
multi->SetMode(GaugeMode::LinearMultiPointer);
multi->SetOrientation(GaugeOrientation::Vertical);
multi->SetTitle("Recipe Layers");
multi->SetUnit("ml");
multi->SetMaxValue(2000.0);
multi->SetGaugeColor(Color(0, 160, 255, 255));
multi->AddExternalPointer(GaugeExternalPointer(1500.0, Color(0, 160, 255, 255), "Base"));
multi->AddExternalPointer(GaugeExternalPointer(1660.0, Color(255, 180, 60, 255), "Fill"));
multi->AddExternalPointer(GaugeExternalPointer(2000.0, Color(180, 180, 190, 255), "Max"));
multi->SetValue(1500.0);
```

### Linear With Arrow — clinical range indicator

```cpp
auto arrow = CreateGaugeDiagramElement("arrow", 0, 0, 272, 374);
arrow->SetMode(GaugeMode::LinearWithArrow);
arrow->SetOrientation(GaugeOrientation::Vertical);
arrow->SetTitle("Glucose Level");
arrow->SetUnit("mmol/l");
arrow->SetMaxValue(15.0);
arrow->SetDecimalPlaces(1);
arrow->AddRange(GaugeRangeSegment(0.0, 5.6,  Color( 80, 200, 120, 255), "Normal"));
arrow->AddRange(GaugeRangeSegment(5.6, 7.0,  Color(255, 190,  60, 255), "Borderline"));
arrow->AddRange(GaugeRangeSegment(7.0, 15.0, Color(255, 100,  80, 255), "Elevated"));
arrow->SetValue(5.7);
```

### Linear Scale — named markers (radio tuner)

```cpp
auto scale = CreateGaugeDiagramElement("scale", 0, 0, 272, 374);
scale->SetMode(GaugeMode::LinearScale);
scale->SetTitle("Radio Tuner");
scale->SetUnit("MHz");
scale->SetMinValue(180.0);
scale->SetMaxValue(970.0);
scale->AddThreshold(GaugeThreshold(180.0, Color(180, 180, 190, 255), "Galaxy"));
scale->AddThreshold(GaugeThreshold(571.0, Color(180, 180, 190, 255), "Dukes"));
scale->AddThreshold(GaugeThreshold(780.0, Color(180, 180, 190, 255), "Frasier"));
scale->AddThreshold(GaugeThreshold(970.0, Color(180, 180, 190, 255), "Simpsons"));
scale->SetValue(571.0);
```

### Battery — bar fill vs. LED fill

```cpp
// Continuous bar fill with a charging bolt
auto bat1 = CreateGaugeDiagramElement("bat1", 0, 0, 272, 374);
bat1->SetMode(GaugeMode::Battery);
bat1->SetTitle("Battery (Bar)");
bat1->SetBatteryStyle(GaugeBatteryStyle::BarPointer);
bat1->SetShowBolt(true);
bat1->SetValue(75.0);

// Discrete LED segments
auto bat2 = CreateGaugeDiagramElement("bat2", 0, 0, 272, 374);
bat2->SetMode(GaugeMode::Battery);
bat2->SetTitle("Battery (LED)");
bat2->SetBatteryStyle(GaugeBatteryStyle::LedPointer);
bat2->SetSegmentCount(10);
bat2->SetValue(100.0);
```

### Thermometer — bipolar scale

```cpp
auto thermo = CreateGaugeDiagramElement("thermo", 0, 0, 272, 374);
thermo->SetMode(GaugeMode::Thermometer);
thermo->SetTitle("Temperature");
thermo->SetUnit("C");
thermo->SetMinValue(-25.0);
thermo->SetMaxValue(25.0);
thermo->SetGaugeColor(Color(0, 180, 255, 255));
thermo->SetValue(12.0);
```

### Cylinder — 3D-style tank

```cpp
auto cyl = CreateGaugeDiagramElement("cyl", 0, 0, 272, 374);
cyl->SetMode(GaugeMode::Cylinder);
cyl->SetTitle("Water");
cyl->SetUnit("ml");
cyl->SetMaxValue(1000.0);
cyl->SetGaugeColor(Color(0, 200, 200, 255));
cyl->SetValue(1000.0);
```

### Circular Ring — 360° progress ring

```cpp
auto ring = CreateGaugeDiagramElement("ring", 0, 0, 272, 374);
ring->SetMode(GaugeMode::CircularRing);
ring->SetTitle("Completion");
ring->SetUnit("%");
ring->SetGaugeColor(Color(0, 200, 140, 255));
ring->SetValue(75.0);
```

### Digital — LED numeric display with color thresholds

```cpp
auto digital = CreateGaugeDiagramElement("digital", 0, 0, 272, 374);
digital->SetMode(GaugeMode::Digital);
digital->SetTitle("LED Display");
digital->SetUnit("Hz");
digital->SetMaxValue(9999.0);
digital->SetDecimalPlaces(1);
digital->SetShowGlow(true);
digital->AddRange(GaugeRangeSegment(0.0,    5000.0, Color(0, 180, 255, 255)));
digital->AddRange(GaugeRangeSegment(5000.0, 8000.0, Color(255, 200, 60, 255)));
digital->AddRange(GaugeRangeSegment(8000.0, 9999.0, Color(255,  80, 80, 255)));
digital->SetValue(1234.5);
```

### Driving a gauge from a slider

```cpp
slider->onValueChanged = [gauge = gaugePtr, label = labelPtr](float v) {
    gauge->SetValue(static_cast<double>(v));
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << v << " %";
    label->SetText(oss.str());
};
```

## Encoding Cheat Sheet

| Visual                | Controlled by                                                |
|-----------------------|--------------------------------------------------------------|
| Gauge style           | `SetMode(GaugeMode)`                                          |
| Value position        | `SetValue` within `SetMinValue` … `SetMaxValue`              |
| Color of the value    | `AddRange` band that contains the current value             |
| Scale markers         | `AddThreshold` (value + label)                              |
| Extra pointers        | `AddExternalPointer` (linear modes)                        |
| Embedded sub-gauge    | `SetSubDial` (analog modes)                                |
| Bar/dial direction    | `SetOrientation` (linear) / `SetArcAngles` (analog)        |
| Segment resolution    | `SetSegmentCount` (LED / segmented / brick / battery LED)  |
| Ticks                 | `SetMajorTickCount` / `SetMinorTickCount` / `SetTickPosition` |
| Number format         | `SetDecimalPlaces` + `SetUnit`                             |

## Notes

- **`NoTicks` / `NoNeedle`, not `None`** — X11 `#define`s `None`, so the enum values are spelled out to avoid the macro collision.
- For very small linear-bar values the fill stays a proper rounded capsule; the corner radius is clamped to the fill's smaller dimension so it never collapses into a circle.
- `AnalogClock` and `Stopwatch` drive themselves with internal timers; the rest are updated by calling `SetValue`.

## See Also

- [UltraCanvasSlider](UltraCanvasSliderExamples.md) — input control commonly paired with a gauge
- [UltraCanvasBarChartElement](UltraCanvasBarChartElement.md) — categorical value visualization
- [UltraCanvasFinancialChart](UltraCanvasFinancialChart.md) — time-series value visualization
- [UltraCanvasArcDiagramExamples](UltraCanvasArcDiagramExamples.md) — another diagrams-plugin element
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
