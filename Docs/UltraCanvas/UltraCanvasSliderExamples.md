# UltraCanvas Slider Control Documentation

<!-- doc-check: void updateProgressBar(float value); void rotateObject(float angle); std::string FormatTime(float seconds); float bufferedSeconds; std::vector<float> bucketCounts; Color amber; Color red; -->

## Contents

- [Overview](#overview)
- [Features](#features)
  - [Core Functionality](#core-functionality)
- [Class Definition](#class-definition)
- [Enumerations](#enumerations)
  - [SliderStyle](#sliderstyle)
  - [SliderValueDisplay](#slidervaluedisplay)
  - [SliderOrientation](#sliderorientation)
  - [SliderState](#sliderstate)
  - [SliderHandleShape](#sliderhandleshape)
  - [SliderTickPosition and SliderTickStyle](#slidertickposition-and-slidertickstyle)
  - [SliderColorZone](#slidercolorzone)
- [Visual Style Structure](#visual-style-structure)
- [Constructor](#constructor)
- [Key Methods](#key-methods)
  - [Value Management](#value-management)
  - [Style Configuration](#style-configuration)
  - [Appearance Customization](#appearance-customization)
  - [Tick Marks and Labels](#tick-marks-and-labels)
  - [Fill Origin, Reset and Colour Zones](#fill-origin-reset-and-colour-zones)
  - [Rotary, Seek, Histogram, Segments, Ruler](#rotary-seek-histogram-segments-ruler)
  - [Range Mode (two handles)](#range-mode-two-handles)
- [Event Callbacks](#event-callbacks)
- [Factory Functions](#factory-functions)
- [Keyboard Controls](#keyboard-controls)
- [Usage Examples](#usage-examples)
  - [Basic Horizontal Slider](#basic-horizontal-slider)
  - [Vertical Slider with Percentage](#vertical-slider-with-percentage)
  - [Circular Knob Control](#circular-knob-control)
  - [Custom Styled Slider](#custom-styled-slider)
  - [Slider Designs](#slider-designs)
  - [Progress Bar Style](#progress-bar-style)
- [Rendering Details](#rendering-details)
- [Advanced Features](#advanced-features)
  - [Step Snapping](#step-snapping)
  - [Custom Value Formatting](#custom-value-formatting)
  - [Range Validation](#range-validation)
- [Integration Notes](#integration-notes)
- [Dependencies](#dependencies)
- [Performance Considerations](#performance-considerations)
- [See Also](#see-also)

## Overview

The **UltraCanvasSlider** is a versatile and feature-rich interactive slider control component for the UltraCanvas Framework. It provides multiple styles, value display options, and comprehensive customization capabilities for creating intuitive range selection interfaces.

**File Location**: `include/UltraCanvasSlider.h`  
**Version**: 3.9.0  
**Last Modified**: 2026-11-24  
**Author**: UltraCanvas Framework

## Features

### Core Functionality
- ✅ **Multiple Slider Styles**: Horizontal, Vertical, Circular, Progress, Range (two handles),
  Knob, Field, Seek, Ruler, Segmented
- ✅ **Tick Marks and Scales**: before, after or on the track; lines, a ramp or dots; major and
  minor intervals or chosen values; labels with a formatter; named stops
- ✅ **Handle Shapes**: Circle, Square, Triangle, Diamond, Pointer, Bar, Block, FaderCap, or none,
  with an optional shadow
- ✅ **Value Bubble**: the value in a balloon over the handle, always or while dragging
- ✅ **Fill Origin and Reset**: fill outward from a neutral value; double-click returns to it
- ✅ **Value Management**: Min/max range, current value, percentage calculations
- ✅ **Step Control**: Configurable increment/decrement step values
- ✅ **Value Display Options**: Number, Percentage, Tooltip, Always Visible, Bubble
- ✅ **Mouse Interaction**: Click-to-set, drag-to-adjust; knobs and the ruler turn by dragging
  and take the wheel
- ✅ **Keyboard Navigation**: Arrow keys, Home, End, Page Up/Down
- ✅ **Visual Feedback**: Hover, pressed, focused, disabled states
- ✅ **Custom Styling**: Configurable colors, sizes, fonts
- ✅ **Event Callbacks**: Value changed, changing, press, release

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasSlider : public UltraCanvasUIElement {
        // Main slider component implementation
    };
}
```

## Enumerations

### SliderStyle
Defines the visual style of the slider:
```cpp
enum class SliderStyle {
    Horizontal,     // Classic horizontal bar
    Vertical,       // Classic vertical bar
    Circular,       // Circular/knob style
    Progress,       // Progress bar style
    Range,          // Range slider with two handles
    Knob,           // Rotary cap with a pointer line inside a ring of dots
    Field,          // A field that fills with the value: a name on the left, the number on the right
    Seek,           // Media timeline: thin line, buffered part, thumb shown on hover
    Ruler,          // Fixed centre marker; the scale slides under it
    Segmented       // A row (or column) of blocks lit up to the value, like a level meter
};
```

`Circular` and `Knob` are turned, not slid: dragging up raises the value
(the whole range over 200 pixels), and the mouse wheel steps it. `Ruler` is
dragged sideways - the scale follows the pointer, so the value moves the
other way - and takes the wheel too. A linear slider ignores the wheel, so it
does not catch a scrolling page.

### SliderValueDisplay
Controls how the value is displayed:
```cpp
enum class SliderValueDisplay {
    NoDisplay,        // No value display
    Number,          // Numeric value
    Percentage,      // Percentage display
    Tooltip,         // Show on hover
    AlwaysVisible,   // Always visible
    Bubble,          // In a balloon over the handle, always
    BubbleWhileDragging // In a balloon over the handle while it is dragged
};
```

A slider that shows a value keeps its bar at the far edge of its box - the
foot of a horizontal one, the left of a vertical one - so the value has room
above or beside it. With `NoDisplay` (the default) the bar is centred across
the box instead, so a slider in a row of labels sits on their line whatever
height it is given.

### SliderOrientation
Defines the slider orientation:
```cpp
enum class SliderOrientation {
    Horizontal,
    Vertical
};
```

### SliderState
Internal state management:
```cpp
enum class SliderState {
    Normal,
    Hovered,
    Pressed,
    Focused,
    Disabled
};
```

### SliderHandleShape
The shape of the handle (`SliderVisualStyle::handleShape`, `SetHandleShape`).
A scoped enum: always write `SliderHandleShape::Square`, never a bare `Square`.
```cpp
enum class SliderHandleShape {
    Circle,     // the default
    Square,
    Triangle,
    Diamond,
    Pointer,    // Five-sided thumb that points at the ticks
    Bar,        // Slim bar across the track, in the active colour (see handleGap)
    Block,      // Small upright block, as on a compact desktop trackbar
    FaderCap,   // Ribbed rectangular cap of a mixing-desk fader
    NoHandle    // No handle at all (a meter, or a Seek bar at rest)
};
```

A `Pointer` points at the ticks: up (or left) with `SliderTickPosition::Before`,
down (or right) otherwise.

### SliderTickPosition and SliderTickStyle
Where tick marks go and how they look (`SetTickPosition`, `SetTickStyle`):
```cpp
enum class SliderTickPosition {
    NoTicks,    // the default
    Before,     // above a horizontal track, left of a vertical one
    After,      // below / right
    BothSides,
    OnTrack     // inside the track itself
};

enum class SliderTickStyle {
    Line,       // straight marks; major ones longer than minor ones
    Ramp,       // marks that grow from the minimum to the maximum
    Dot         // round dots
};
```

### SliderColorZone
A colour from a fraction of the range upward, for `SetColorZones`:
```cpp
struct SliderColorZone {
    float fromFraction = 0.0f;
    Color color;
};
```

## Visual Style Structure

```cpp
struct SliderVisualStyle {
    // Track colors
    Color trackColor = Color(200, 200, 200);
    Color activeTrackColor = Color(0, 120, 215);
    Color disabledTrackColor = Color(210, 210, 214);
    Color disabledActiveTrackColor = Color(175, 175, 180);  // replaces activeTrackColor when disabled
    Color rangeTrackColor = Color(0, 120, 215, 180);        // range between the two handles

    // Handle colors
    Color handleColor = Colors::White;
    Color handleBorderColor = Color(100, 100, 100);
    Color handleHoverColor = Color(240, 240, 240);
    Color handlePressedColor = Color(200, 200, 200);
    Color handleDisabledColor = Color(220, 220, 220);

    // Text colors
    Color textColor = Colors::Black;
    Color disabledTextColor = Color(150, 150, 150);

    // Ticks, labels, the value bubble, the tooltip, a Seek bar's buffered
    // part and the histogram
    Color tickColor = Color(110, 110, 110);
    Color tickLabelColor = Color(90, 90, 90);
    Color bubbleColor = Color(40, 40, 40);
    Color bubbleTextColor = Colors::White;
    Color tooltipColor = Color(255, 255, 220, 230);
    Color tooltipBorderColor = Color(128, 128, 128);
    Color secondaryTrackColor = Color(160, 160, 160);
    Color histogramColor = Color(205, 205, 210);
    Color histogramActiveColor = Color(0, 120, 215, 200);

    // Dimensions
    float trackHeight = 6.0f;
    float handleSize = 16.0f;
    float borderWidth = 1.0f;           // the track's outline and the handle's border; 0 for none
    float cornerRadius = 3.0f;          // rounds the track, its fill and the bar (at most half its thickness)
    SliderHandleShape handleShape = SliderHandleShape::Circle;
    bool handleShadow = false;          // a soft shadow under the handle
    float handleGap = 0.0f;             // space clear of the track either side of a Bar handle
    bool trackInset = false;            // a sunken, bevelled track
    float tickLength = 6.0f;            // major ticks; a Ramp grows up to this
    float minorTickLength = 3.0f;
    float tickGap = 3.0f;               // between the handle's band and the ticks

    // Font
    FontStyle fontStyle;
};
```

`SliderVisualStyle::FromTheme` fills the colours from the theme's roles (ticks
and their labels in muted ink, the bubble in ink with the ground's colour for
its text, the histogram in the line colour) and keeps every size and flag of
the style it is given, so a slider shaped by hand keeps its shape when the
theme changes.

## Constructor

```cpp
UltraCanvasSlider(const std::string& identifier, float x, float y, float w, float h);
UltraCanvasSlider(const std::string& identifier, float w, float h);
explicit UltraCanvasSlider(const std::string& identifier);
```

**Parameters:**
- `identifier`: Unique string identifier for the slider
- `x`, `y`: Position coordinates (omitted: the layout places the slider)
- `w`, `h`: Width and height dimensions (omitted: sized by the layout)

## Key Methods

### Value Management

```cpp
// Set value range
void SetRange(float min, float max);

// Set/get current value
void SetValue(float value);
float GetValue() const;

// Get min/max values
float GetMinValue() const;
float GetMaxValue() const;

// Percentage operations
float GetPercentage() const;
void SetPercentage(float percentage);

// Step control
void SetStep(float stepValue);
float GetStep() const;
```

#### Step: what a slider snaps to

`step` is the snap increment; **0 makes the slider continuous** (every position
the track offers). A slider that is never given one follows its range:

- range of **20 units or more** (0..100, 5..40) → whole-unit steps, as before;
- **narrower** (0.2..3.0 gamma, a 0..1 ratio, 0..2 gain) → **continuous**,
  because whole units would leave such a range with a handful of reachable
  positions — a 0..2 slider would offer exactly three.

`SetStep()` overrides that for good: the value stated by the caller survives any
later `SetRange()`, so an integer control (quality 0..100 that narrows to 0..9,
an effort level 0..6) keeps whole steps whatever range it is given.

```cpp
auto gammaSlider = CreateSlider("gamma", 10, 10, 200, 30);
auto quality = CreateSlider("quality", 10, 50, 200, 30);
auto effort = CreateSlider("effort", 10, 90, 200, 30);
auto seek = CreateSlider("seek", 10, 130, 200, 30);

gammaSlider->SetRange(0.2f, 3.0f);  // continuous by default
quality->SetRange(0, 100);          // whole units by default
effort->SetStep(1.0f);              // whole units, stated
effort->SetRange(0, 6);             // ...and kept here
seek->SetStep(0.0f);                // continuous, stated
```

`UltraCanvasSlider::MinAutoStepSpan` (20) is the threshold, and
`DefaultStepForRange(min, max)` is the rule itself.

### Style Configuration

```cpp
// Set slider style
void SetSliderStyle(SliderStyle newStyle);
SliderStyle GetSliderStyle() const;

// Set value display mode
void SetValueDisplay(SliderValueDisplay mode);
SliderValueDisplay GetValueDisplay() const;

// Set orientation
void SetOrientation(SliderOrientation orient);
SliderOrientation GetOrientation() const;
```

### Appearance Customization

```cpp
// Set colors
void SetColors(const Color& track, const Color& activeTrack, const Color& handle);

// Set dimensions and handle shape
void SetTrackHeight(float height);
void SetHandleSize(float size);
void SetHandleShape(SliderHandleShape shape);

// Set text formatting
void SetValueFormat(const std::string& format);
void SetCustomText(const std::string& text);

// Optional gradient painted inside the track (e.g. a hue palette)
void SetTrackGradient(const std::vector<GradientStop>& stops);
void ClearTrackGradient();
bool HasTrackGradient() const;

// Access style directly
SliderVisualStyle& GetStyle();
const SliderVisualStyle& GetStyle() const;
void SetStyle(const SliderVisualStyle& st);
```

### Tick Marks and Labels

```cpp
void SetTickPosition(SliderTickPosition position);   // NoTicks by default
void SetTickStyle(SliderTickStyle tickStyle);        // Line, Ramp, Dot
void SetTickInterval(float major, float minor = 0.0f);
void SetTickMarks(const std::vector<float>& values); // ticks at these values only
void SetTickLabels(bool show);                       // a label at each major tick
void SetStopLabels(const std::vector<std::string>& labels);
std::string GetTickLabel(float value) const;
std::vector<std::pair<float, bool>> GetTickValues() const;  // value, is major

std::function<std::string(float)> tickLabelFormatter;       // the label of a tick
```

With no interval set, the major ticks fall on every step when that gives at
most 20 of them, and on every tenth of the range otherwise. Minor ticks are
drawn only with a minor interval, and the major interval should be a multiple
of it. `SetTickMarks` replaces the intervals with chosen values (all major):
the 0 dB mark of a fader, or -100 / 0 / +100 of a balance control.

`SetStopLabels` names each step from the minimum (`SetRange(0, 6)`,
`SetStep(1)` and seven names): every stop gets a tick and its name, the name
of the current stop is drawn in the text colour, and the value display shows
the name instead of the number.

Ticks and labels take room across the bar, and a label wider than the handle
pulls both ends of the travel in so the end labels are not cut off: give the
slider the height (or width) they need.

### Fill Origin, Reset and Colour Zones

```cpp
void SetFillOrigin(float value);   // fill from this value to the handle
void ClearFillOrigin();
void SetResetValue(float value);   // a double-click returns here
void ClearResetValue();
void SetColorZones(const std::vector<SliderColorZone>& zones);
```

A fill origin makes a bipolar control: on a -100..100 balance slider,
`SetFillOrigin(0)` fills outward from the middle and marks it. Colour zones
colour the fill (and a `Segmented` slider's blocks) by how far up the range
it reaches.

### Rotary, Seek, Histogram, Segments, Ruler

```cpp
void SetArcAngles(float startDegrees, float sweepDegrees);  // Circular, Knob: 135, 270 by default

void SetSecondaryValue(float value);   // Seek: what has loaded
void ClearSecondaryValue();
void SetHoverPreview(bool enabled);    // the value under the pointer, in a bubble
std::function<std::string(float)> hoverText;

void SetHistogram(const std::vector<float>& bucketCounts);  // bars behind a horizontal track
void ClearHistogram();

void SetSegmentCount(int count);       // Segmented
void SetPixelsPerUnit(float pixels);   // Ruler
```

Angles are in degrees clockwise from 3 o'clock; the default arc starts at 135
and sweeps 270, leaving a gap at the bottom. The histogram's buckets spread
evenly across the range, in the free space above the track; the bars under
the selection (or the fill) take `histogramActiveColor`.

### Range Mode (two handles)

```cpp
void SetRangeMode(bool enabled);
bool IsRangeMode() const;
void SetLowerValue(float value);
void SetUpperValue(float value);
void SetRangeValues(float lower, float upper);
float GetLowerValue() const;
float GetUpperValue() const;
void SetHandleCollisionMargin(float margin);   // minimum distance between the handles
float GetHandleCollisionMargin() const;
```

In range mode the arrow keys move the active handle (both when none is
active), and Tab switches between the handles. Range changes are reported
through `onLowerValueChanged`, `onUpperValueChanged` and `onRangeChanged`
(see below).

## Event Callbacks

```cpp
// Value changed: a committed value - set programmatically, stepped by the
// keyboard or the wheel, or settled on at the end of a drag (fired once on
// release, and only when the drag actually moved the value)
std::function<void(float)> onValueChanged;

// Value changing (continuously, during a drag)
std::function<void(float)> onValueChanging;

// Mouse events
std::function<void(const UCEvent&)> onPress;
std::function<void(const UCEvent&)> onRelease;
std::function<void(const UCEvent&)> onClick;

// Range mode
std::function<void(float)> onLowerValueChanged;
std::function<void(float)> onUpperValueChanged;
std::function<void(float, float)> onRangeChanged;
```

## Factory Functions

```cpp
// Create basic slider
std::shared_ptr<UltraCanvasSlider> CreateSlider(
    const std::string& identifier, float x, float y, float width, float height);

// Create horizontal slider
std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(
    const std::string& identifier, float x, float y, float width, float height,
    float min = 0.0f, float max = 100.0f);

// Create vertical slider
std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(
    const std::string& identifier, float x, float y, float width, float height,
    float min = 0.0f, float max = 100.0f);

// Create circular slider (size x size)
std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(
    const std::string& identifier, float x, float y, float size,
    float min = 0.0f, float max = 100.0f);

// Create range slider (two handles, range mode on)
std::shared_ptr<UltraCanvasSlider> CreateRangeSlider(
    const std::string& identifier, float x, float y, float width, float height,
    float min = 0.0f, float max = 100.0f, float lower = 25.0f, float upper = 75.0f);
```

## Keyboard Controls

The slider supports comprehensive keyboard navigation:

| Key | Action |
|-----|--------|
| **Left/Down** | Decrease value by step |
| **Right/Up** | Increase value by step |
| **Home** | Set to minimum value |
| **End** | Set to maximum value |
| **Page Up** | Increase by 10 × step |
| **Page Down** | Decrease by 10 × step |

A double-click puts a slider with a reset value (`SetResetValue`) back to it.

With a continuous slider (step 0) one key step is 1% of the range.

## Usage Examples

### Basic Horizontal Slider

```cpp
// Create a horizontal slider
auto hSlider = CreateHorizontalSlider("volume", 50, 100, 200, 30, 0.0f, 100.0f);
hSlider->SetValue(50.0f);
hSlider->SetStep(5.0f);
hSlider->SetValueDisplay(SliderValueDisplay::Number);

// Add callback
hSlider->onValueChanged = [](float value) {
    std::cerr << "Volume: " << value << std::endl;
};

// Add to container
container->AddChild(hSlider);
```

### Vertical Slider with Percentage

```cpp
// Create a vertical slider
auto vSlider = CreateVerticalSlider("progress", 300, 50, 30, 200, 0.0f, 100.0f);
vSlider->SetValueDisplay(SliderValueDisplay::Percentage);
vSlider->SetValue(75.0f);

// Continuous feedback during drag
vSlider->onValueChanging = [](float value) {
    updateProgressBar(value);
};

container->AddChild(vSlider);
```

### Circular Knob Control

```cpp
// Create a circular knob
auto knob = CreateCircularSlider("knob", 400, 150, 100, 0.0f, 360.0f);
knob->SetValueDisplay(SliderValueDisplay::Tooltip);
knob->SetValueFormat("%.0f°");

knob->onValueChanged = [](float angle) {
    rotateObject(angle);
};

container->AddChild(knob);
```

### Custom Styled Slider

```cpp
// Create slider with custom appearance
auto customSlider = CreateSlider("custom", 50, 250, 300, 40);
customSlider->SetRange(-50.0f, 50.0f);
customSlider->SetValue(0.0f);

// Customize colors
customSlider->SetColors(
    Color(100, 100, 100),    // Track
    Color(255, 100, 0),      // Active track
    Color(255, 255, 255)     // Handle
);

// Customize dimensions
customSlider->SetTrackHeight(8.0f);
customSlider->SetHandleSize(20.0f);

// Custom text display
customSlider->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
customSlider->SetValueFormat("%.1f dB");

container->AddChild(customSlider);
```

### Slider Designs

Each of these is on the demo app's slider page. The first is a compact
trackbar with ticks that grow towards the maximum:

```cpp
auto size = CreateSlider("BrushSize", 0, 0, 300, 40);
size->SetStep(25.0f);
size->SetTickPosition(SliderTickPosition::Before);
size->SetTickStyle(SliderTickStyle::Ramp);
size->SetTickInterval(25.0f);
size->SetHandleShape(SliderHandleShape::Block);
size->SetHandleSize(12.0f);
size->GetStyle().trackInset = true;
size->GetStyle().tickLength = 12.0f;
```

A pointer thumb over a row of ticks, and a labelled scale with a unit:

```cpp
auto pointer = CreateSlider("Pointer", 0, 0, 300, 40);
pointer->SetStep(10.0f);
pointer->SetTickPosition(SliderTickPosition::After);
pointer->SetTickInterval(10.0f);
pointer->SetHandleShape(SliderHandleShape::Pointer);

auto delay = CreateSlider("Delay", 0, 0, 300, 56);
delay->SetTickPosition(SliderTickPosition::After);
delay->SetTickInterval(25.0f, 5.0f);
delay->SetTickLabels(true);
delay->tickLabelFormatter = [](float v) { return std::to_string(int(v)) + " ms"; };
```

Named stops, the value in a bubble, and a centre-origin balance control:

```cpp
auto sizes = CreateSlider("Size", 0, 0, 300, 56);
sizes->SetRange(0, 6);
sizes->SetStep(1);
sizes->SetTickPosition(SliderTickPosition::OnTrack);
sizes->SetTickStyle(SliderTickStyle::Dot);
sizes->SetStopLabels({"Off", "XS", "S", "M", "L", "XL", "XXL"});
sizes->SetTrackHeight(10.0f);

auto opacity = CreateSlider("Opacity", 0, 0, 300, 56);
opacity->SetValueDisplay(SliderValueDisplay::Bubble);
opacity->SetValueFormat("%.0f%%");

auto balance = CreateSlider("Balance", 0, 0, 300, 50);
balance->SetRange(-100, 100);
balance->SetFillOrigin(0);
balance->SetResetValue(0);
balance->SetTickPosition(SliderTickPosition::After);
balance->SetTickMarks({-100, 0, 100});
```

A pill track split by a bar, and a soft thick track for touch:

```cpp
auto pill = CreateSlider("Pill", 0, 0, 300, 40);
pill->SetTrackHeight(16.0f);
pill->SetHandleShape(SliderHandleShape::Bar);
pill->GetStyle().cornerRadius = 8.0f;
pill->GetStyle().handleGap = 4.0f;
pill->GetStyle().borderWidth = 0.0f;

auto touch = CreateSlider("Touch", 0, 0, 300, 40);
touch->SetTrackHeight(8.0f);
touch->SetHandleSize(28.0f);
touch->GetStyle().handleShadow = true;
touch->GetStyle().borderWidth = 0.0f;
```

An inline field for a property panel, a media seek bar and a range with the
data's spread behind it:

```cpp
auto roughness = CreateSlider("Roughness", 0, 0, 300, 28);
roughness->SetSliderStyle(SliderStyle::Field);
roughness->SetRange(0.0f, 1.0f);
roughness->SetValueFormat("%.2f");
roughness->SetCustomText("Roughness");   // the name; the number is drawn beside it

auto timeline = CreateSlider("Timeline", 0, 0, 300, 50);
timeline->SetSliderStyle(SliderStyle::Seek);
timeline->SetSecondaryValue(bufferedSeconds);
timeline->SetHoverPreview(true);
timeline->hoverText = [](float v) { return FormatTime(v); };

auto price = CreateRangeSlider("Price", 0, 0, 300, 110, 0.0f, 500.0f, 140.0f, 350.0f);
price->SetHistogram(bucketCounts);
price->SetValueDisplay(SliderValueDisplay::BubbleWhileDragging);
```

A ruler dial, the two knobs, a console fader and a level meter:

```cpp
auto straighten = CreateSlider("Straighten", 0, 0, 300, 64);
straighten->SetSliderStyle(SliderStyle::Ruler);
straighten->SetRange(-45.0f, 45.0f);
straighten->SetStep(0.5f);
straighten->SetTickInterval(10.0f, 2.0f);
straighten->SetTickLabels(true);
straighten->SetValueDisplay(SliderValueDisplay::Number);
straighten->SetPixelsPerUnit(6.0f);

auto gain = CreateCircularSlider("Gain", 0, 0, 120);
gain->SetValueDisplay(SliderValueDisplay::Number);
gain->SetArcAngles(135.0f, 270.0f);

auto drive = CreateSlider("Drive", 0, 0, 120, 120);
drive->SetSliderStyle(SliderStyle::Knob);
drive->SetRange(0.0f, 10.0f);

auto channel = CreateVerticalSlider("Channel", 0, 0, 100, 220);
channel->SetHandleShape(SliderHandleShape::FaderCap);
channel->SetTickPosition(SliderTickPosition::Before);
channel->SetTickMarks({0.0f, 25.0f, 50.0f, 75.0f, 100.0f});
channel->SetTickLabels(true);

auto level = CreateVerticalSlider("Level", 0, 0, 60, 220);
level->SetSliderStyle(SliderStyle::Segmented);
level->SetSegmentCount(16);
level->SetColorZones({{0.7f, amber}, {0.875f, red}});
level->SetTrackHeight(26.0f);
level->SetHandleShape(SliderHandleShape::NoHandle);
```

### Progress Bar Style

```cpp
// Create a progress bar style slider
auto progressBar = CreateSlider("loading", 50, 350, 400, 20);
progressBar->SetSliderStyle(SliderStyle::Progress);
progressBar->SetRange(0.0f, 100.0f);
progressBar->SetValueDisplay(SliderValueDisplay::Percentage);

// Animate progress
auto animateProgress = [progressBar]() {
    float current = progressBar->GetValue();
    if (current < 100.0f) {
        progressBar->SetValue(current + 1.0f);
    }
};

container->AddChild(progressBar);
```

## Rendering Details

The slider uses the UltraCanvas rendering system to draw:

1. **Histogram**: Optional bars behind the track
2. **Track Rectangle**: Background track, rounded by `cornerRadius`
3. **Active Track**: Filled portion from the minimum (or the fill origin) to the value
4. **Ticks and Labels**: Optional, either side of the track or on it
5. **Handle**: Draggable control element, in one of the handle shapes
6. **Value Display**: Optional text, tooltip or bubble showing the value

Different styles render with variations:
- **Linear**: Traditional bar with handle
- **Circular**: An arc (135° to 405° by default) filled up to the value, the handle at its end
- **Knob**: A raised cap with a pointer line inside a ring of dots
- **Progress**: Bar without handle
- **Range**: Bar with two handles and the range between them highlighted
- **Field**: The whole element as a field that fills with the value
- **Seek**: A thin line with the buffered part; the thumb shows on hover
- **Ruler**: A moving scale under a fixed marker, fading at both ends
- **Segmented**: Blocks lit up to the value, in the colour zones

## Advanced Features

### Step Snapping

```cpp
// Enable step snapping
slider->SetStep(10.0f);  // Snap to multiples of 10
```

### Custom Value Formatting

```cpp
// Custom format string (printf-style)
slider->SetValueFormat("%.2f%%");  // Two decimal places with percent

// Or use custom text
slider->SetCustomText("Custom Label");
```

### Range Validation

The slider automatically validates and clamps values:
- Values are constrained to [minValue, maxValue]
- Step snapping is applied if step > 0
- Percentage is calculated as (value - min) / (max - min)

## Integration Notes

The UltraCanvasSlider component:
- ✅ Extends UltraCanvasUIElement properly
- ✅ Uses unified rendering system
- ✅ Handles UCEvent comprehensively
- ✅ Follows naming conventions (PascalCase)
- ✅ Includes proper version header
- ✅ Provides factory functions
- ✅ Uses UltraCanvas namespace
- ✅ Memory safe with smart pointers
- ✅ Cross-platform compatible

## Dependencies

Required headers:
- `UltraCanvasUIElement.h`
- `UltraCanvasRenderContext.h`
- `UltraCanvasEvent.h`
- `UltraCanvasCommonTypes.h`

## Performance Considerations

- Rendering is optimized to only redraw when values change
- Event handling uses efficient state management
- Mouse dragging uses differential updates
- Tooltip rendering is conditional on hover state

## See Also

- [UltraCanvasSpinner](../../UltraCanvas/include/UltraCanvasSpinner.h) - Numeric input with increment/decrement
- [UltraCanvasGaugeDiagramElement](../../UltraCanvas/include/Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h) - Progress indication control
