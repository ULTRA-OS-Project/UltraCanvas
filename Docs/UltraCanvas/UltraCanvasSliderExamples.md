# UltraCanvas Slider Control Documentation

<!-- doc-check: void updateProgressBar(float value); void rotateObject(float angle); -->

## Overview

The **UltraCanvasSlider** is a versatile and feature-rich interactive slider control component for the UltraCanvas Framework. It provides multiple styles, value display options, and comprehensive customization capabilities for creating intuitive range selection interfaces.

**File Location**: `include/UltraCanvasSlider.h`  
**Version**: 2.0.1  
**Last Modified**: 2026-10-07  
**Author**: UltraCanvas Framework

## Features

### Core Functionality
- ✅ **Multiple Slider Styles**: Horizontal, Vertical, Circular, Progress, Range (two handles)
- ✅ **Value Management**: Min/max range, current value, percentage calculations
- ✅ **Step Control**: Configurable increment/decrement step values
- ✅ **Value Display Options**: Number, Percentage, Tooltip, Always Visible
- ✅ **Mouse Interaction**: Click-to-set, drag-to-adjust
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
};
```

### SliderValueDisplay
Controls how the value is displayed:
```cpp
enum class SliderValueDisplay {
    NoDisplay,        // No value display
    Number,          // Numeric value
    Percentage,      // Percentage display
    Tooltip,         // Show on hover
    AlwaysVisible    // Always visible
};
```

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

    // Dimensions
    float trackHeight = 6.0f;
    float handleSize = 16.0f;
    float borderWidth = 1.0f;
    float cornerRadius = 3.0f;
    SliderHandleShape handleShape = SliderHandleShape::Circle;  // Circle, Square, Triangle, Diamond

    // Font
    FontStyle fontStyle;
};
```

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

1. **Track Rectangle**: Background track for the slider
2. **Active Track**: Filled portion showing current value
3. **Handle**: Draggable control element
4. **Value Display**: Optional text showing current value

Different styles render with variations:
- **Linear**: Traditional bar with handle
- **Circular**: Arc track with rotating handle
- **Progress**: Bar without handle
- **Range**: Bar with two handles and the range between them highlighted

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

- [UltraCanvasAdvancedSlider](UltraCanvasAdvancedSlider.md) - Extended slider with advanced features
- [UltraCanvasSpinBox](UltraCanvasSpinBox.md) - Numeric input with increment/decrement
- [UltraCanvasProgressBar](UltraCanvasProgressBar.md) - Progress indication control
