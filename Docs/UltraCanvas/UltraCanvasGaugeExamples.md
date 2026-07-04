# UltraCanvasGauge Documentation

## Overview

**UltraCanvasGaugeDiagramElement** is a single, mode-driven element that renders a wide family of gauges, meters and dials from one API. A gauge is created once with a factory function and then switched between modes — analog speedometers and compasses, progress and LED bars, circular activity rings, batteries, thermometers, cylinders, analog/digital clocks and stopwatches — by calling `SetMode()` and a handful of configuration setters.

All modes share the same value model (`min`/`max`/`current`), the same colour and text styling, and the same optional decorations (coloured ranges, threshold markers, external pointers and a sub-dial), so the same data can be visualised in radically different ways without changing the surrounding code.

**Version:** 1.0.0
**Header:** `include/Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **One element, many modes**: 17 gauge modes selectable at runtime via `SetMode()`
- **Analog dials**: Speedometer (270°), Semicircular (180°), Quadrant (90°), Compass (360°), Analog Clock, Stopwatch
- **Progress & linear meters**: Linear Bar, LED bar, Segmented (brick) bar, Multi-Pointer bar, Bar with arrow, Linear Scale
- **Round (CircularRing) gauges**: solid-arc, segmented, dashed and chunky "segmented-ring" styles, with optional liquid surface fill (straight or waved), faded colours, and a centre value/label/icon
- **Specialized shapes**: Battery (bar or LED pointer), Thermometer, Cylinder, Digital LED/LCD panel
- **Live displays**: Analog Clock and Digital clock mode tick once a second; Stopwatch has Start/Stop/Reset controls
- **Decorations**: coloured `GaugeRangeSegment` zones, `GaugeThreshold` markers, `GaugeExternalPointer` arrows and a secondary `GaugeSubDial`
- **Built-in centre icons**: battery and bolt glyphs for round gauges
- **Value callback**: `onGaugeValueChange` fires whenever the value changes (e.g. driven by a slider or the stopwatch)

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
```

## Class Reference

### Factory Function

Always create gauges through the factory function; it returns a
`std::shared_ptr` ready to add to a container or layout.

```cpp
std::shared_ptr<UltraCanvasGaugeDiagramElement> CreateGaugeDiagramElement(
    const std::string& id, long x, long y, long width, long height);
```

```cpp
auto gauge = CreateGaugeDiagramElement("speed", 0, 0, 272, 374);
```

### Gauge Modes

```cpp
enum class GaugeMode {
    // Analog
    Speedometer,        // 270° circular gauge with needle (car speedometer)
    Semicircular,       // 180° arc gauge with needle (temperature, sales)
    Quadrant,           // 90° arc gauge
    Compass,            // 360° compass with cardinal directions
    AnalogClock,        // analog clock with hour/minute/second hands
    Stopwatch,          // stopwatch with optional sub-dial

    // Progress / bar
    LinearBar,          // horizontal or vertical bar (download progress)
    LinearLED,          // segmented LED-style bar (VU meter, signal strength)
    LinearSegmented,    // brick-style segmented bar (business metrics)
    LinearMultiPointer, // bar with multiple external pointers (recipe layers)
    LinearWithArrow,    // bar with external arrow pointer and value label
    LinearScale,        // linear scale with multiple markers (radio tuner)
    CircularRing,       // 360° progress ring (round gauge)
    Battery,            // battery shape with terminal
    Thermometer,        // vertical thermometer with bulb
    Cylinder,           // 3D-style cylindrical bar

    // Digital
    Digital             // LED/LCD numeric display
};
```

### Supporting Enumerations

```cpp
enum class GaugeNeedleStyle { Classic, Thin, Arrow, Triangle, NoNeedle };
enum class GaugeTickPosition { NoTicks, Inside, Outside, Both };
enum class GaugeOrientation { Horizontal, Vertical };
enum class GaugeBatteryStyle { BarPointer, LedPointer };

// Round-gauge (CircularRing) styling
enum class GaugeRingStyle { SolidArc, Segmented, Dashed, SegmentedRing };
enum class GaugeRingSegmentStyle { Bars, Dots, Blocks };
enum class GaugeFillStyle { NoFill, StraightLevel, WavedLevel };
enum class GaugeRingCenterContent { NoContent, TextLabel, Icon };
enum class GaugeRingIcon { Battery, Bolt };
```

> **Note on `None`:** X11 `#define`s `None`, so the enums use `NoNeedle`,
> `NoTicks`, `NoFill` and `NoContent` instead of a bare `None`.

### Data Structures

```cpp
// A coloured zone along the gauge scale (e.g. "safe / warning / danger").
struct GaugeRangeSegment {
    double startValue;
    double endValue;
    Color  color;
    std::string label;
    GaugeRangeSegment(double s, double e, const Color& c, const std::string& l = "");
};

// A single marker drawn at one value (e.g. a tuner station).
struct GaugeThreshold {
    double value;
    Color  color;
    std::string label;
    GaugeThreshold(double v, const Color& c, const std::string& l = "");
};

// An arrow/marker drawn outside a linear bar.
struct GaugeExternalPointer {
    double value;
    Color  color;
    std::string label;
    bool   showOnLeft;
    GaugeExternalPointer(double v, const Color& c, const std::string& l, bool left = false);
};

// A small secondary dial drawn inside the main gauge.
struct GaugeSubDial {
    bool        enabled       = false;
    GaugeMode   mode          = GaugeMode::Semicircular;
    double      currentValue  = 0.0;
    double      minValue      = 0.0;
    double      maxValue      = 100.0;
    std::string title;
    std::string unit;
    double      relativeSize  = 0.3;   // size relative to the main dial
    double      offsetXRatio  = 0.0;   // centre offset (fraction of radius)
    double      offsetYRatio  = 0.2;
};
```

### Mode & Value

```cpp
void   SetMode(GaugeMode m);
GaugeMode GetMode() const;

void   SetValue(double val);           // fires onGaugeValueChange
double GetValue() const;
void   SetMinValue(double v);
double GetMinValue() const;
void   SetMaxValue(double v);
double GetMaxValue() const;
```

### Title & Unit

```cpp
void SetTitle(const std::string& t);   // caption above/below the gauge
void SetUnit(const std::string& u);    // e.g. "km/h", "%", "C"
```

### Visual Style

```cpp
void SetNeedleStyle(GaugeNeedleStyle s);     // Classic, Thin, Arrow, Triangle, NoNeedle
void SetTickPosition(GaugeTickPosition p);   // NoTicks, Inside, Outside, Both
void SetOrientation(GaugeOrientation o);     // Horizontal / Vertical (linear modes)
void SetBatteryStyle(GaugeBatteryStyle s);   // BarPointer / LedPointer (Battery mode)
void SetArcAngles(double startDeg, double endDeg);  // custom sweep for analog dials
```

### Colours

```cpp
void SetGaugeColor(const Color& c);       // fill / needle / indicator colour
void SetBackgroundColor(const Color& c);  // panel background
void SetTextColor(const Color& c);        // value / title text colour
```

### Round-Gauge (CircularRing) Styling

```cpp
void SetRingThickness(float t);                    // indicator width in px
void SetRingStyle(GaugeRingStyle s);               // SolidArc / Segmented / Dashed / SegmentedRing
void SetRingSegmentStyle(GaugeRingSegmentStyle s); // Bars / Dots / Blocks
void SetRingSegmentCount(int count);               // number of chunks/ticks/dots
void SetRingSegmentRounded(bool rounded);          // rounded vs sharp segment ends
void SetRingBorder(bool enabled);                  // outline around segments
void SetRingBorderColor(const Color& c);
void SetFillStyle(GaugeFillStyle s);               // NoFill / StraightLevel / WavedLevel
void SetTrackColor(const Color& c);                // colour of the un-filled track
void SetRingFaded(bool faded);                     // soft lightened indicator gradient
void SetFillFaded(bool faded);                     // pale lightened centre fill

// Centre content
void SetRingCenterContent(GaugeRingCenterContent c);  // NoContent / TextLabel / Icon
void SetRingCenterLabel(const std::string& label);    // text under the value
void SetRingCenterIcon(GaugeRingIcon icon);           // Battery / Bolt glyph
```

### Ranges, Thresholds & Pointers

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
void SetDecimalPlaces(int places);   // value formatting precision
void SetShowGlow(bool glow);         // LED glow (Digital mode)
void SetShowBolt(bool show);         // charging bolt (Battery mode)
void SetShowLabels(bool show);       // tick/scale labels
void SetMajorTickCount(int count);   // major scale divisions
void SetMinorTickCount(int count);   // minor ticks per major division
void SetSegmentCount(int count);     // LED / segmented bar segments
```

### Digital Display & Clock

```cpp
// In Digital mode, show the live wall-clock time (HH:MM:SS) instead of the value.
void SetDigitalClock(bool en);
// Comma-separated font fallback list, e.g. "DSEG7 Classic,Monospace".
void SetDigitalFontFamily(const std::string& family);
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
std::function<void(double)> onGaugeValueChange;  // value changed (slider, stopwatch, …)
```

---

## Examples

The runnable source for every example below lives in
`Apps/DemoApp/UltraCanvasGaugeExamples.cpp` (open it from the **C++ source**
icon in the demo header).

### 1. Analog Speedometer

A 270° automotive-style dial with a coloured scale and a needle.

```cpp
auto speedo = CreateGaugeDiagramElement("speedo", 0, 0, 272, 374);
speedo->SetMode(GaugeMode::Speedometer);
speedo->SetTitle("Speed");
speedo->SetUnit("km/h");
speedo->SetMaxValue(240.0);
speedo->SetMajorTickCount(6);
speedo->SetValue(109.0);
```

### 2. Semicircular Gauge with Coloured Ranges

A 180° arc split into coloured zones — ideal for KPIs and assessments.

```cpp
auto semi = CreateGaugeDiagramElement("semi", 0, 0, 272, 374);
semi->SetMode(GaugeMode::Semicircular);
semi->SetTitle("Sales Assessment");
semi->SetUnit("k$");
semi->SetMinValue(0.0);
semi->SetMaxValue(100.0);
semi->SetMajorTickCount(5);
semi->AddRange(GaugeRangeSegment(0.0,  50.0,  Color(100, 180, 255, 255)));
semi->AddRange(GaugeRangeSegment(50.0, 100.0, Color(80,  200, 120, 255)));
semi->SetValue(73.0);
```

### 3. Compass with a Sub-Dial

A 360° compass showing wind direction, with a small semicircular wind-speed
sub-dial nested inside.

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

compass->SetValue(120.0);   // degrees
```

### 4. Analog Clock (live)

`AnalogClock` mode draws hour/minute/second hands and ticks once a second on
its own internal timer — no value updates required.

```cpp
auto clock = CreateGaugeDiagramElement("clock", 0, 0, 272, 374);
clock->SetMode(GaugeMode::AnalogClock);
clock->SetTitle("Clock");
```

### 5. Stopwatch with Start/Stop/Reset

```cpp
auto sw = CreateGaugeDiagramElement("sw", 0, 0, 272, 374);
sw->SetMode(GaugeMode::Stopwatch);
sw->SetTitle("Stopwatch");
sw->SetMaxValue(60.0);
sw->SetMajorTickCount(12);

// Wire the controls to buttons:
startBtn->SetOnClick([sw] { sw->StopwatchStart(); });
stopBtn ->SetOnClick([sw] { sw->StopwatchStop();  });
resetBtn->SetOnClick([sw] { sw->StopwatchReset(); });

// React to elapsed time:
sw->onGaugeValueChange = [](double seconds) {
    // update a label, etc.
};
```

### 6. Linear Progress Bar

```cpp
auto bar = CreateGaugeDiagramElement("bar", 0, 0, 272, 374);
bar->SetMode(GaugeMode::LinearBar);
bar->SetTitle("Download Progress");
bar->SetUnit("%");
bar->SetGaugeColor(Color(0, 140, 255, 255));
bar->SetValue(65.0);
```

The fill is a pill that always keeps the bar's full corner radius: just above
the minimum it renders as a full-radius circle and grows lengthwise from
there. At the exact minimum nothing is drawn. Two optional low-value warnings
are available:

```cpp
// Show a circle in the warning colour at the zero/empty position instead of
// drawing nothing.
bar->SetShowZeroValueWarning(true);

// Blink the fill in the warning colour while the value is at or below the
// low-level limit (in gauge units; default 10).
bar->SetLowLevelWarning(true);
bar->SetLowLevelLimit(10.0);

// Both warnings share the warning colour (default red).
bar->SetWarningColor(Color(230, 55, 45, 255));
```

### 7. Vertical LED VU Meter

```cpp
auto led = CreateGaugeDiagramElement("led", 0, 0, 272, 374);
led->SetMode(GaugeMode::LinearLED);
led->SetOrientation(GaugeOrientation::Vertical);
led->SetTitle("VU Meter");
led->SetUnit("%");
led->SetSegmentCount(16);
led->AddRange(GaugeRangeSegment(0.0,  80.0,  Color(0,   200, 255, 255)));
led->AddRange(GaugeRangeSegment(80.0, 100.0, Color(255, 100, 80,  255)));
led->SetValue(65.0);
```

### 8. Multi-Pointer Bar

A vertical bar carrying several external pointers (e.g. layered quantities).

```cpp
auto multi = CreateGaugeDiagramElement("multi", 0, 0, 272, 374);
multi->SetMode(GaugeMode::LinearMultiPointer);
multi->SetOrientation(GaugeOrientation::Vertical);
multi->SetTitle("Recipe Layers");
multi->SetUnit("ml");
multi->SetMaxValue(2000.0);
multi->AddExternalPointer(GaugeExternalPointer(1500.0, Color(0,   160, 255, 255), "Base"));
multi->AddExternalPointer(GaugeExternalPointer(1660.0, Color(255, 180, 60,  255), "Fill"));
multi->AddExternalPointer(GaugeExternalPointer(2000.0, Color(180, 180, 190, 255), "Max"));
```

### 9. Linear Scale with Thresholds (Radio Tuner)

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
scale->SetValue(571.0);
```

### 10. Round Gauge (CircularRing)

The most configurable mode. The defaults give a classic solid-arc progress
ring; the style setters reshape it into segmented blocks, dashed tachymeters,
dot rings or chunky battery-style "segmented rings".

```cpp
auto ring = CreateGaugeDiagramElement("ring", 0, 0, 272, 374);
ring->SetMode(GaugeMode::CircularRing);
ring->SetTitle("Completion");
ring->SetUnit("%");
ring->SetGaugeColor(Color(0, 200, 140, 255));
ring->SetRingThickness(10.0f);
ring->SetRingStyle(GaugeRingStyle::SolidArc);
ring->SetValue(74.0);
```

**Segmented blocks:**

```cpp
ring->SetRingStyle(GaugeRingStyle::Segmented);
ring->SetRingSegmentStyle(GaugeRingSegmentStyle::Blocks);
ring->SetRingSegmentCount(12);
```

**Dashed tachymeter bars:**

```cpp
ring->SetRingStyle(GaugeRingStyle::Dashed);
ring->SetRingSegmentStyle(GaugeRingSegmentStyle::Bars);
ring->SetRingSegmentCount(60);
```

**Liquid surface fill with a faded tint:**

```cpp
ring->SetFillStyle(GaugeFillStyle::WavedLevel);  // or StraightLevel
ring->SetFillFaded(true);
```

**Chunky segmented ring with a centre battery icon:**

```cpp
ring->SetRingStyle(GaugeRingStyle::SegmentedRing);
ring->SetRingSegmentCount(8);
ring->SetRingSegmentRounded(true);
ring->SetRingCenterIcon(GaugeRingIcon::Battery);
ring->SetRingCenterContent(GaugeRingCenterContent::Icon);
```

### 11. Battery

```cpp
auto bat = CreateGaugeDiagramElement("bat", 0, 0, 272, 374);
bat->SetMode(GaugeMode::Battery);
bat->SetTitle("Battery");
bat->SetBatteryStyle(GaugeBatteryStyle::LedPointer);  // or BarPointer
bat->SetSegmentCount(10);
bat->SetShowBolt(true);   // charging bolt overlay
bat->SetValue(75.0);
```

### 12. Thermometer

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

### 13. Cylinder

```cpp
auto cyl = CreateGaugeDiagramElement("cyl", 0, 0, 272, 374);
cyl->SetMode(GaugeMode::Cylinder);
cyl->SetTitle("Water");
cyl->SetUnit("ml");
cyl->SetMaxValue(1000.0);
cyl->SetGaugeColor(Color(0, 200, 200, 255));
cyl->SetValue(1000.0);
```

### 14. Digital LED Panel

```cpp
auto digital = CreateGaugeDiagramElement("digital", 0, 0, 272, 374);
digital->SetMode(GaugeMode::Digital);
digital->SetTitle("LED Display");
digital->SetUnit("Hz");
digital->SetMaxValue(9999.0);
digital->SetDecimalPlaces(1);
digital->SetShowGlow(true);
digital->AddRange(GaugeRangeSegment(0.0,    5000.0, Color(0,   180, 255, 255)));
digital->AddRange(GaugeRangeSegment(5000.0, 8000.0, Color(255, 200, 60,  255)));
digital->AddRange(GaugeRangeSegment(8000.0, 9999.0, Color(255, 80,  80,  255)));
digital->SetValue(1234.5);
```

### 15. Digital Clock

```cpp
auto dclock = CreateGaugeDiagramElement("dclock", 0, 0, 272, 374);
dclock->SetMode(GaugeMode::Digital);
dclock->SetTitle("Clock");
dclock->SetDigitalClock(true);
// Prefer an LED-style font if installed; fall back to Monospace otherwise.
dclock->SetDigitalFontFamily("DSEG7 Classic,DSEG7 Modern,Digital-7,Monospace");
dclock->SetGaugeColor(Color(0, 230, 80, 255));   // classic LED green
```

### 16. Driving a Gauge from a Slider

Most demo cards pair a gauge with a slider so the value can be scrubbed live.

```cpp
auto gauge  = CreateGaugeDiagramElement("g", 0, 0, 272, 300);
gauge->SetMode(GaugeMode::CircularRing);
gauge->SetUnit("%");

auto slider = std::make_shared<UltraCanvasSlider>("g_sl", 0, 0, 200, 22);
slider->SetOrientation(SliderOrientation::Horizontal);
slider->SetRange(0.0f, 100.0f);
slider->SetValue(74.0f);

auto gaugePtr = gauge.get();
slider->onValueChanged = [gaugePtr](float v) {
    gaugePtr->SetValue(static_cast<double>(v));
};
```

---

## Best Practices

- **Create with the factory.** Use `CreateGaugeDiagramElement()` so the element
  is returned as a ready-to-share `std::shared_ptr`.
- **Set the mode first.** Call `SetMode()` before mode-specific setters
  (`SetRingStyle`, `SetBatteryStyle`, `SetDigitalClock`, …) so the configuration
  applies to the active renderer.
- **Match decorations to the mode.** Coloured `AddRange` zones suit analog,
  LED and digital modes; `AddThreshold` markers suit `LinearScale`;
  `AddExternalPointer` is for `LinearMultiPointer` / `LinearWithArrow`; and the
  ring-centre icon/label options apply only to `CircularRing`.
- **Let live modes own their timers.** `AnalogClock`, the `Digital` clock and a
  running `Stopwatch` redraw themselves once a second; you don't need to push
  values to keep them ticking.
- **Use `onGaugeValueChange` for read-back.** It fires for every value change,
  whether driven by a slider, by `SetValue()` or by the stopwatch — ideal for
  updating an external label.
- **Keep capture pointers raw.** Capture `gauge.get()` in lambdas (as the demo
  does) to avoid extending the shared_ptr's lifetime inside UI callbacks.

## See Also

- `Apps/DemoApp/UltraCanvasGaugeExamples.cpp` — full, runnable demo source for
  every mode shown above (Round Gauges, Progress & Linear, Specialized, Analog).
- `include/Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h` — complete header.
