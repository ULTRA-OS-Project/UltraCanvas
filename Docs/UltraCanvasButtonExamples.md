# UltraCanvasButton Documentation

**Version:** 1.0.0  
**Last Modified:** 2025-01-08  
**Author:** UltraCanvas Framework

## Overview

UltraCanvasButton is a customizable button control in the UltraCanvas framework that provides cross-platform button functionality with consistent behavior across Windows, Linux, macOS, and UltraOS.

## Basic Usage

### Including the Header

```cpp
#include "UltraCanvasButton.h"
```

### Creating a Button

```cpp
// Using constructor
auto button = std::make_shared<UltraCanvasButton>(
    "MyButton",    // identifier
    101,           // id
    100, 50,       // x, y position
    120, 40,       // width, height
    "Click Me"     // button text
);

// Using factory function
auto button = CreateButton("MyButton", 101, 100, 50, 120, 40, "Click Me");

// Using auto-sized button
auto button = CreateAutoButton("MyButton", 100, 50, "Click Me");
```

## Class Reference

### Constructor

```cpp
UltraCanvasButton(
    const std::string& identifier = "Button",
    long id = 0,
    long x = 0, long y = 0,
    long w = 100, long h = 30,
    const std::string& buttonText = "Button"
);
```

### Core Methods

#### Text Management
```cpp
const std::string& GetText() const;
void SetText(const std::string& newText);
```

#### State Methods
```cpp
ButtonState GetButtonState() const;
bool IsPressed() const;
bool AcceptsFocus() const;  // Returns true for buttons
```

#### Auto-sizing
```cpp
void SetAutoresize(bool value);
bool GetAutoresize() const;
void AutoResize();  // Manually trigger auto-resize
```

#### Style Configuration
```cpp
const ButtonStyle& GetStyle() const;
void SetStyle(const ButtonStyle& newStyle);

// Color configuration
void SetColors(
    const Color& normal,
    const Color& hover,
    const Color& pressed,
    const Color& disabled
);

void SetTextColors(
    const Color& normal,
    const Color& hover,
    const Color& pressed,
    const Color& disabled
);

// Font configuration
void SetFont(
    const std::string& fontFamily,
    float fontSize,
    FontWeight weight = FontWeight::Normal
);

// Layout configuration
void SetPadding(int left, int right, int top, int bottom);
void SetCornerRadius(float radius);

// Shadow configuration
void SetShadow(
    bool enabled,
    const Color& color = Color(0, 0, 0, 64),
    const Point2Di& offset = Point2Di(1, 1)
);
```

#### Interaction
```cpp
void Click(const UCEvent& ev);  // Programmatically click the button
void SetOnClick(std::function<void()> _onClick);
```

### Events

The button supports the following callback events:

```cpp
std::function<void()> onClick;       // Fired when button is clicked
std::function<void()> onPress;       // Fired when button is pressed down
std::function<void()> onRelease;     // Fired when button is released
std::function<void()> onHoverEnter;  // Fired when mouse enters button
std::function<void()> onHoverLeave;  // Fired when mouse leaves button
```

### Event Usage Example

```cpp
auto button = CreateButton("MyButton", 1, 10, 10, 100, 30, "Click Me");

// Set click handler
button->onClick = []() {
    std::cout << "Button clicked!" << std::endl;
};

// Set hover handlers
button->onHoverEnter = []() {
    std::cout << "Mouse entered button" << std::endl;
};

button->onHoverLeave = []() {
    std::cout << "Mouse left button" << std::endl;
};
```

## Button States

The button can be in one of the following states:

```cpp
enum class ButtonState {
    Normal,    // Default state
    Hovered,   // Mouse is over the button
    Pressed,   // Button is being pressed
    Disabled   // Button is disabled
};
```

## ButtonStyle Structure

The ButtonStyle structure contains appearance settings:

```cpp
struct ButtonStyle {
    // Colors for different states
    Color normalColor;
    Color hoverColor;
    Color pressedColor;
    Color disabledColor;
    
    // Text colors for different states
    Color normalTextColor;
    Color hoverTextColor;
    Color pressedTextColor;
    Color disabledTextColor;
    
    // Font settings
    std::string fontFamily;
    float fontSize;
    FontWeight fontWeight;
    
    // Layout
    Padding padding;
    float cornerRadius;
    
    // Shadow
    bool hasShadow;
    Color shadowColor;
    Point2Di shadowOffset;
};
```

## ButtonBuilder Pattern

For fluent interface construction:

```cpp
class ButtonBuilder {
    ButtonBuilder& SetColors(const Color& normal, const Color& hover,
                            const Color& pressed, const Color& disabled);
    ButtonBuilder& SetTextColors(const Color& normal, const Color& hover,
                                const Color& pressed, const Color& disabled);
    ButtonBuilder& SetFont(const std::string& fontFamily, float fontSize);
    ButtonBuilder& SetPadding(int horizontal, int vertical);
    ButtonBuilder& SetCornerRadius(float radius);
    ButtonBuilder& SetShadow(bool enabled);
    ButtonBuilder& SetAutoresize(bool enabled);
    ButtonBuilder& OnClick(std::function<void()> handler);
    
    std::shared_ptr<UltraCanvasButton> Build();
};
```

### Builder Usage Example

```cpp
auto button = ButtonBuilder("MyButton", 10, 10, "Click Me")
    .SetColors(Colors::Blue, Colors::LightBlue, Colors::DarkBlue, Colors::Gray)
    .SetTextColors(Colors::White, Colors::White, Colors::White, Colors::DarkGray)
    .SetCornerRadius(5.0f)
    .OnClick([]() { std::cout << "Clicked!" << std::endl; })
    .Build();
```

## Practical Examples

### Standard Button

```cpp
auto standardBtn = CreateButton("StandardButton", 1, 20, 20, 100, 30, "Standard");
standardBtn->onClick = []() {
    std::cout << "Standard button clicked" << std::endl;
};
```

### Colored Button

```cpp
auto primaryBtn = CreateButton("PrimaryButton", 2, 20, 60, 100, 30, "Primary");
primaryBtn->SetColors(
    Color(0, 123, 255, 255),  // Normal
    Color(0, 100, 225, 255),  // Hover
    Color(0, 90, 215, 255),   // Pressed
    Color(100, 133, 255, 255) // Disabled
);
primaryBtn->SetTextColors(
    Colors::White, Colors::White, Colors::White, Colors::LightGray
);
```

### Auto-sized Button

```cpp
auto autoBtn = CreateAutoButton("AutoButton", 20, 100, "Auto Size");
autoBtn->SetAutoresize(true);
// Button will automatically resize when text changes
autoBtn->SetText("This is a longer text that will auto-resize");
```

### Disabled Button

```cpp
auto disabledBtn = CreateButton("DisabledButton", 3, 20, 140, 100, 30, "Disabled");
disabledBtn->SetEnabled(false);
```

## Keyboard Support

Buttons support keyboard interaction:
- **Space/Enter**: Activates the button when focused
- **Tab**: Navigate to button (if focus enabled)

## Rendering

The button automatically handles:
- Background color based on state
- Text rendering with proper crossAlignment
- Focus indicator when focused
- Border and corner radius
- Optional shadow effect

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-01-08 | Initial documentation based on actual implementation |
