# UltraCanvas Slider Control Documentation

## Overview

The **UltraCanvasSlider** is a versatile and feature-rich interactive slider control component for the UltraCanvas Framework. It provides multiple styles, value display options, and comprehensive customization capabilities for creating intuitive range selection interfaces.

**File Location**: `include/UltraCanvasSlider.h`  
**Version**: 2.0.0  
**Last Modified**: 2025-08-17  
**Author**: UltraCanvas Framework

## Features

### Core Functionality
- ✅ **Multiple Slider Styles**: Horizontal, Vertical, Circular, Progress, Range, Rounded
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
    Rounded         // Rounded corners
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
    Color disabledTrackColor = Color(180, 180, 180);

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

    // Font
    FontStyle fontStyle;
};
```

## Constructor

```cpp
UltraCanvasSlider(const std::string& identifier = "Slider",
                  long x = 0, long y = 0, 
                  long w = 200, long h = 30)
```

**Parameters:**
- `identifier`: Unique string identifier for the slider
- `id`: Numeric ID for the control
- `x`, `y`: Position coordinates
- `w`, `h`: Width and height dimensions

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

// Set dimensions
void SetTrackHeight(float height);
void SetHandleSize(float size);

// Set text formatting
void SetValueFormat(const std::string& format);
void SetCustomText(const std::string& text);

// Access style directly
SliderVisualStyle& GetStyle();
const SliderVisualStyle& GetStyle() const;
```

## Event Callbacks

```cpp
// Value changed (after release)
std::function<void(float)> onValueChanged;

// Value changing (during drag)
std::function<void(float)> onValueChanging;

// Mouse events
std::function<void(const UCEvent&)> onPress;
std::function<void(const UCEvent&)> onRelease;
std::function<void(const UCEvent&)> onClick;
```

## Factory Functions

```cpp
// Create basic slider
std::shared_ptr<UltraCanvasSlider> CreateSlider(
    const std::string& identifier, long id, 
    long x, long y, long width, long height);

// Create horizontal slider
std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(
    const std::string& identifier, long id, 
    long x, long y, long width, long height,
    float min = 0.0f, float max = 100.0f);

// Create vertical slider
std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(
    const std::string& identifier, long id, 
    long x, long y, long width, long height,
    float min = 0.0f, float max = 100.0f);

// Create circular slider
std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(
    const std::string& identifier, long id, 
    long x, long y, long size,
    float min = 0.0f, float max = 100.0f);

// Create rounded slider
std::shared_ptr<UltraCanvasSlider> CreateRoundedSlider(
    const std::string& identifier, long id, 
    long x, long y, long width, long height,
    float min = 0.0f, float max = 100.0f);
```

## Convenience Functions

```cpp
// Set slider value
void SetSliderValue(UltraCanvasSlider* slider, float value);

// Get slider value
float GetSliderValue(const UltraCanvasSlider* slider);

// Set slider range
void SetSliderRange(UltraCanvasSlider* slider, float min, float max);
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

## Usage Examples

### Basic Horizontal Slider

```cpp
// Create a horizontal slider
auto hSlider = CreateHorizontalSlider("volume", 100, 50, 100, 200, 30, 0.0f, 100.0f);
hSlider->SetValue(50.0f);
hSlider->SetStep(5.0f);
hSlider->SetValueDisplay(SliderValueDisplay::Number);

// Add callback
hSlider->onValueChanged = [](float value) {
    std::cout << "Volume: " << value << std::endl;
};

// Add to container
container->AddChild(hSlider);
```

### Vertical Slider with Percentage

```cpp
// Create a vertical slider
auto vSlider = CreateVerticalSlider("progress", 101, 300, 50, 30, 200, 0.0f, 100.0f);
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
auto knob = CreateCircularSlider("knob", 102, 400, 150, 100, 0.0f, 360.0f);
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
- **Rounded**: Bar with rounded corners

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
