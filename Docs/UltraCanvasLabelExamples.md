# UltraCanvasLabel Documentation

## Overview

**UltraCanvasLabel** is a modern text display control in the UltraCanvas framework that provides rich text rendering capabilities with comprehensive styling and crossAlignment options. It serves as the foundation for displaying static or dynamic text content in UltraCanvas applications.

**Version:** 1.0.0  
**Header:** `include/UltraCanvasLabel.h`  
**Namespace:** `UltraCanvas`  
**Base Class:** `UltraCanvasUIElement`

## Features

- **Text Rendering**: High-quality text rendering with FreeType backend
- **Styling Options**: Comprehensive text and background styling
- **LayoutAlignment Control**: Horizontal and vertical text crossAlignment
- **Auto-Sizing**: Automatic resizing based on text content
- **Word Wrapping**: Multi-line text support with word wrapping
- **Text Effects**: Shadow effects for enhanced visibility
- **Interactive Support**: Click and hover event handling
- **Unicode Support**: Full international text support
- **Performance**: Optimized rendering with layout caching

## Class Declaration

```cpp
class UltraCanvasLabel : public UltraCanvasUIElement
```

## Constructor

```cpp
UltraCanvasLabel(const std::string& identifier = "Label",
                 long id = 0, 
                 long x = 0, 
                 long y = 0, 
                 long w = 100, 
                 long h = 25,
                 const std::string& labelText = "")
```

### Parameters
- `identifier`: Unique string identifier for the label
- `id`: Numeric ID for the label
- `x`, `y`: Position coordinates
- `w`, `h`: Width and height dimensions
- `labelText`: Initial text content

## LabelStyle Structure

The `LabelStyle` structure encapsulates all visual properties of a label:

```cpp
struct LabelStyle {
    // Text appearance
    FontStyle fontStyle;
    Color textColor = Colors::Black;
    
    // Background and border
    Color backgroundColor = Colors::Transparent;
    Color borderColor = Colors::Transparent;
    float borderWidth = 0.0f;
    float borderRadius = 0.0f;
    
    // Text crossAlignment
    TextAlignment horizontalAlign = TextAlignment::Left;
    TextVerticalAlignment verticalAlign = TextVerticalAlignment::Middle;
    
    // Padding
    float paddingLeft = 4.0f;
    float paddingRight = 4.0f;
    float paddingTop = 2.0f;
    float paddingBottom = 2.0f;
    
    // Text effects
    bool hasShadow = false;
    Color shadowColor = Color(0, 0, 0, 128);
    Point2Di shadowOffset = Point2Di(1, 1);
    
    // Layout options
    bool wordWrap = false;
    bool autoResize = false;
}
```

### Predefined Styles

- `LabelStyle::DefaultStyle()` - Standard label appearance
- `LabelStyle::HeaderStyle()` - Large, bold text for headers
- `LabelStyle::SubHeaderStyle()` - Medium-sized bold text
- `LabelStyle::CaptionStyle()` - Small, muted text
- `LabelStyle::StatusStyle()` - Status bar text styling

## Core Methods

### Text Management

```cpp
void SetText(const std::string& newText)
```
Sets the label's text content. Triggers layout recalculation and optional auto-resize.

```cpp
const std::string& GetText() const
```
Returns the current text content.

```cpp
void AppendText(const std::string& additionalText)
```
Appends text to existing content.

```cpp
void ClearText()
```
Clears all text content.

```cpp
bool IsEmpty() const
```
Returns true if the label contains no text.

### Style Management

```cpp
void SetStyle(const LabelStyle& newStyle)
```
Applies a complete style configuration.

```cpp
const LabelStyle& GetStyle() const
```
Returns the current style configuration.

### Font Configuration

```cpp
void SetFont(const std::string& fontFamily, 
             float fontSize = 12.0f, 
             FontWeight weight = FontWeight::Normal)
```
Sets the font family, size, and weight.

```cpp
void SetFontSize(float fontSize)
```
Sets only the font size.

```cpp
void SetFontWeight(FontWeight weight)
```
Sets the font weight (Normal, Bold, etc.).

### Color Configuration

```cpp
void SetTextColor(const Color& color)
```
Sets the text color.

```cpp
void SetBackgroundColor(const Color& color)
```
Sets the background fill color.

```cpp
void SetBordersColor(const Color& color)
```
Sets the border color.

### Layout Configuration

```cpp
void SetCrossAlignment(TextAlignment horizontal, 
                  TextVerticalAlignment vertical = TextVerticalAlignment::Middle)
```
Sets horizontal and vertical text crossAlignment.

```cpp
void SetPadding(float padding)
```
Sets uniform padding on all sides.

```cpp
void SetPadding(float left, float right, float top, float bottom)
```
Sets individual padding values for each side.

```cpp
void SetWordWrap(bool wrap)
```
Enables or disables word wrapping for multi-line text.

```cpp
void SetAutoResize(bool autoResize)
```
Enables automatic resizing based on text content.

### Border Configuration

```cpp
void SetBorderWidth(float width)
```
Sets the border thickness.

```cpp
void SetBorderRadius(float radius)
```
Sets the corner radius for rounded borders.

### Shadow Effects

```cpp
void SetShadow(bool enabled, 
                const Color& color = Color(0, 0, 0, 128),
                const Point2Di& offset = Point2Di(1, 1))
```
Configures text shadow effects.

## Event Callbacks

UltraCanvasLabel supports several event callbacks:

```cpp
std::function<void()> onClick;
std::function<void()> onHoverEnter;
std::function<void()> onHoverLeave;
std::function<void(const std::string&)> onTextChanged;
std::function<void(long, long)> onSizeChanged;
```

### Example Event Handling

```cpp
label->onClick = []() {
    std::cout << "Label clicked!" << std::endl;
};

label->onTextChanged = [](const std::string& newText) {
    std::cout << "Text changed to: " << newText << std::endl;
};
```

## Factory Functions

### CreateLabel
```cpp
std::shared_ptr<UltraCanvasLabel> CreateLabel(
    const std::string& identifier, 
    long id, 
    long x, long y, 
    long w, long h,
    const std::string& text = "")
```
Creates a standard label with specified dimensions.

### CreateAutoLabel
```cpp
std::shared_ptr<UltraCanvasLabel> CreateAutoLabel(
    const std::string& identifier,
    long id,
    long x, long y,
    const std::string& text)
```
Creates a label that automatically sizes to fit its text content.

### CreateHeaderLabel
```cpp
std::shared_ptr<UltraCanvasLabel> CreateHeaderLabel(
    const std::string& identifier,
    long id,
    long x, long y,
    long w, long h,
    const std::string& text)
```
Creates a label with header styling.

### CreateStatusLabel
```cpp
std::shared_ptr<UltraCanvasLabel> CreateStatusLabel(
    const std::string& identifier,
    long id,
    long x, long y,
    long w, long h,
    const std::string& text = "Ready")
```
Creates a label with status bar styling.

## Builder Pattern

UltraCanvasLabel supports a fluent builder pattern for convenient configuration:

```cpp
auto label = CreateLabelBuilder("myLabel", 10, 10, 200, 30)
    .SetText("Hello World")
    .SetFont("Arial", 14.0f)
    .SetTextColor(Colors::Blue)
    .SetBackgroundColor(Color(240, 240, 240))
    .SetCrossAlignment(TextAlignment::Center)
    .SetPadding(10.0f)
    .SetAutoResize(true)
    .OnClick([]() { std::cout << "Clicked!" << std::endl; })
    .Build();
```

## Usage Examples

### Basic Label
```cpp
auto basicLabel = std::make_shared<UltraCanvasLabel>(
    "basic", 1, 10, 10, 200, 25, "Simple Text");
container->AddChild(basicLabel);
```

### Styled Header
```cpp
auto header = std::make_shared<UltraCanvasLabel>(
    "header", 10, 50, 400, 35);
header->SetText("Application Title");
header->SetFontSize(24);
header->SetFontWeight(FontWeight::Bold);
header->SetTextColor(Color(0, 100, 200));
container->AddChild(header);
```

### Status Indicators
```cpp
// Success status
auto successLabel = std::make_shared<UltraCanvasLabel>(
    "success", 10, 100, 150, 25);
successLabel->SetText("✓ Success");
successLabel->SetBackgroundColor(Color(200, 255, 200));
successLabel->SetTextColor(Color(0, 150, 0));
successLabel->SetCrossAlignment(TextAlignment::Center);

// Warning status
auto warningLabel = std::make_shared<UltraCanvasLabel>(
    "warning", 170, 100, 150, 25);
warningLabel->SetText("⚠ Warning");
warningLabel->SetBackgroundColor(Color(255, 255, 200));
warningLabel->SetTextColor(Color(200, 150, 0));
warningLabel->SetCrossAlignment(TextAlignment::Center);

// Error status
auto errorLabel = std::make_shared<UltraCanvasLabel>(
    "error", 330, 100, 150, 25);
errorLabel->SetText("✗ Error");
errorLabel->SetBackgroundColor(Color(255, 200, 200));
errorLabel->SetTextColor(Color(200, 0, 0));
errorLabel->SetCrossAlignment(TextAlignment::Center);
```

### Multi-line Label with Word Wrap
```cpp
auto multiLabel = std::make_shared<UltraCanvasLabel>(
    "multi", 10, 150, 450, 80);
multiLabel->SetText("This is a multi-line label that demonstrates\n"
                    "how text wrapping works with longer content.\n"
                    "It supports multiple lines and proper crossAlignment.");
multiLabel->SetWordWrap(true);
multiLabel->SetCrossAlignment(TextAlignment::Left);
multiLabel->SetBackgroundColor(Color(245, 245, 245));
multiLabel->SetBorderWidth(1.0f);
multiLabel->SetPadding(10.0f);
```

### Dynamic Label with Auto-Resize
```cpp
auto dynamicLabel = CreateAutoLabel("dynamic", 7, 10, 250, "");
dynamicLabel->SetText("Content will determine size");
dynamicLabel->onClick = [dynamicLabel]() {
    static int clickCount = 0;
    dynamicLabel->SetText("Clicked " + std::to_string(++clickCount) + " times");
};
```

### Label with Shadow Effect
```cpp
auto shadowLabel = std::make_shared<UltraCanvasLabel>(
    "shadow", 10, 300, 200, 30);
shadowLabel->SetText("Text with Shadow");
shadowLabel->SetShadow(true, Color(0, 0, 0, 128), Point2Di(2, 2));
shadowLabel->SetFontSize(18);
shadowLabel->SetTextColor(Colors::White);
shadowLabel->SetBackgroundColor(Color(100, 100, 100));
```

## Rendering Process

The label's rendering process follows these steps:

1. **Layout Calculation**: Computes text area based on padding and borders
2. **Text Measurement**: Determines actual text dimensions
3. **LayoutAlignment Processing**: Calculates text position based on crossAlignment settings
4. **Background Rendering**: Draws background color and border if configured
5. **Shadow Rendering**: Renders text shadow if enabled
6. **Text Rendering**: Draws the main text content
7. **Focus Indicator**: Draws focus rectangle if label has focus

## Performance Considerations

- **Layout Caching**: Layout is only recalculated when text or style changes
- **Conditional Rendering**: Only visible labels are rendered
- **Text Metrics Caching**: Font metrics are cached to avoid repeated calculations
- **State Management**: Render context state is properly managed with push/pop

## Integration with Other Components

UltraCanvasLabel integrates seamlessly with other UltraCanvas components:

- Can be added as child to any container
- Works with layout managers for automatic positioning
- Supports theming through style system
- Compatible with event propagation system

## Best Practices

1. **Use Appropriate Styles**: Leverage predefined styles for consistency
2. **Enable Auto-Resize**: For dynamic content, use auto-resize to prevent clipping
3. **Set Meaningful Identifiers**: Use descriptive identifiers for easier debugging
4. **Optimize Updates**: Batch style changes when possible to minimize redraws
5. **Consider Accessibility**: Use sufficient contrast and readable font sizes
6. **Handle Long Text**: Enable word wrap for potentially long text content

## Thread Safety

UltraCanvasLabel is not thread-safe. All operations should be performed on the UI thread.

## Platform Compatibility

UltraCanvasLabel works consistently across all supported platforms:
- Linux (X11/Wayland)
- Windows (Win32)
- macOS (Cocoa)
- Future: Android support planned

## Dependencies

- UltraCanvasUIElement (base class)
- UltraCanvasRenderContext (rendering)
- UltraCanvasEvent (event handling)
- FreeType library (font rendering)

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class documentation
- [UltraCanvasTextInput](UltraCanvasTextInput.md) - Editable text control
- [UltraCanvasButton](UltraCanvasButton.md) - Interactive button with label
- [UltraCanvasContainer](UltraCanvasContainer.md) - Container for labels
