# UltraCanvas Checkbox Component Documentation

## Overview

The **UltraCanvasCheckbox** is an interactive checkbox component that provides multiple states and customizable appearance options. It supports standard checkboxes, tri-state checkboxes, toggle switches, and radio button styles within the UltraCanvas framework.

**Version:** 1.1.0  
**Last Modified:** 2024-12-19  
**Author:** UltraCanvas Framework  
**Header:** `include/UltraCanvasCheckbox.h`  
**Implementation:** `core/UltraCanvasCheckbox.cpp`  

## Features

### Core Capabilities
- **Three-state support**: Unchecked, Checked, and Indeterminate states
- **Multiple styles**: Standard, Rounded, Switch, Radio, and Material Design
- **Customizable appearance**: Colors, sizes, fonts, and visual effects
- **Auto-sizing**: Automatic size calculation based on content
- **Keyboard navigation**: Full keyboard support with Space and Enter keys
- **Focus indication**: Visual focus ring for accessibility
- **Event callbacks**: Comprehensive event handling system
- **Hover effects**: Interactive hover states for better user feedback

## Class Definition

### Main Class: `UltraCanvasCheckbox`

```cpp
namespace UltraCanvas {
    class UltraCanvasCheckbox : public UltraCanvasUIElement {
        // Implementation details
    };
}
```

### Inheritance
- **Base Class:** `UltraCanvasUIElement`
- **Namespace:** `UltraCanvas`

## Enumerations

### CheckboxState
Defines the three possible states of a checkbox:

```cpp
enum class CheckboxState {
    Unchecked,      // Default unchecked state
    Checked,        // Checked state
    Indeterminate   // Partially checked state (for tree views)
};
```

### CheckboxStyle
Defines the visual style variants:

```cpp
enum class CheckboxStyle {
    Standard,       // Default square checkbox
    Rounded,        // Rounded corners
    Switch,         // Toggle switch style
    Radio,          // Radio button style (circular)
    Material        // Material Design style
};
```

## Visual Style Configuration

### CheckboxVisualStyle Structure

The `CheckboxVisualStyle` structure provides comprehensive customization options:

```cpp
struct CheckboxVisualStyle {
    // Box appearance
    Color boxColor = Colors::ButtonFace;
    Color boxBorderColor = Colors::ButtonShadow;
    Color boxHoverColor = Colors::SelectionHover;
    Color boxPressedColor = Color(204, 228, 247, 255);
    Color boxDisabledColor = Colors::LightGray;
    
    // Checkmark appearance
    Color checkmarkColor = Colors::TextDefault;
    Color checkmarkHoverColor = Colors::TextDefault;
    Color checkmarkDisabledColor = Colors::TextDisabled;
    
    // Text appearance
    Color textColor = Colors::TextDefault;
    Color textHoverColor = Colors::TextDefault;
    Color textDisabledColor = Colors::TextDisabled;
    
    // Layout
    float boxSize = 16.0f;
    float borderWidth = 1.0f;
    float cornerRadius = 2.0f;
    float checkmarkThickness = 2.0f;
    int textSpacing = 6;
    
    // Text styling
    std::string fontFamily = "Arial";
    float fontSize = 12.0f;
    FontWeight fontWeight = FontWeight::Normal;
    
    // Effects
    bool hasFocusRing = true;
    Color focusRingColor = Color(0, 120, 215, 128);
    float focusRingWidth = 2.0f;
};
```

## Constructor

```cpp
UltraCanvasCheckbox(
    const std::string& identifier = "",
    long id = 0,
    long x = 0,
    long y = 0,
    long w = 150,
    long h = 24,
    const std::string& labelText = ""
);
```

### Parameters
- `identifier`: Unique string identifier for the checkbox
- `id`: Numeric ID for the checkbox
- `x`, `y`: Position coordinates
- `w`, `h`: Width and height (can be 0 for auto-sizing)
- `labelText`: Optional label text to display next to the checkbox

## Public Methods

### State Management

#### SetChecked
```cpp
void SetChecked(bool checked);
```
Sets the checkbox to checked or unchecked state.

#### IsChecked
```cpp
bool IsChecked() const;
```
Returns true if the checkbox is in the checked state.

#### SetCheckState
```cpp
void SetCheckState(CheckboxState state);
```
Sets the checkbox to a specific state (Unchecked, Checked, or Indeterminate).

#### GetCheckState
```cpp
CheckboxState GetCheckState() const;
```
Returns the current state of the checkbox.

#### SetIndeterminate
```cpp
void SetIndeterminate(bool indeterminate);
```
Sets the checkbox to the indeterminate state (if allowed).

#### IsIndeterminate
```cpp
bool IsIndeterminate() const;
```
Returns true if the checkbox is in the indeterminate state.

#### SetAllowIndeterminate
```cpp
void SetAllowIndeterminate(bool allow);
```
Enables or disables tri-state functionality.

#### Toggle
```cpp
void Toggle();
```
Toggles the checkbox between states based on current state and tri-state setting.

### Appearance Configuration

#### SetText
```cpp
void SetText(const std::string& labelText);
```
Sets the label text displayed next to the checkbox.

#### GetText
```cpp
std::string GetText() const;
```
Returns the current label text.

#### SetStyle
```cpp
void SetStyle(CheckboxStyle newStyle);
```
Sets the visual style of the checkbox.

#### SetVisualStyle
```cpp
void SetVisualStyle(const CheckboxVisualStyle& newStyle);
```
Sets the complete visual style configuration.

#### SetBoxSize
```cpp
void SetBoxSize(float size);
```
Sets the size of the checkbox box in pixels.

#### SetColors
```cpp
void SetColors(const Color& box, const Color& checkmark, const Color& text);
```
Sets the primary colors for the checkbox components.

#### SetFont
```cpp
void SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal);
```
Configures the font settings for the label text.

#### SetAutoSize
```cpp
void SetAutoSize(bool val);
```
Enables or disables automatic sizing based on content.

## Event Callbacks

The checkbox provides several callback functions for state changes:

```cpp
// Called when state changes with old and new states
std::function<void(CheckboxState oldState, CheckboxState newState)> onStateChanged;

// Called when checkbox becomes checked
std::function<void()> onChecked;

// Called when checkbox becomes unchecked
std::function<void()> onUnchecked;

// Called when checkbox enters indeterminate state
std::function<void()> onIndeterminate;
```

## Factory Methods

### CreateCheckbox
```cpp
static std::shared_ptr<UltraCanvasCheckbox> CreateCheckbox(
    const std::string& identifier, long id,
    long x, long y, long w, long h,
    const std::string& text = "",
    bool checked = false
);
```
Creates a standard checkbox with optional initial state.

### CreateSwitch
```cpp
static std::shared_ptr<UltraCanvasCheckbox> CreateSwitch(
    const std::string& identifier, long id,
    long x, long y,
    const std::string& text = "",
    bool checked = false
);
```
Creates a toggle switch style checkbox.

### CreateRadioButton
```cpp
static std::shared_ptr<UltraCanvasCheckbox> CreateRadioButton(
    const std::string& identifier, long id,
    long x, long y,
    const std::string& text = "",
    bool checked = false
);
```
Creates a radio button style checkbox.

## Radio Button Group Management

### UltraCanvasRadioGroup Class

A helper class for managing groups of radio buttons:

```cpp
class UltraCanvasRadioGroup {
public:
    void AddRadioButton(std::shared_ptr<UltraCanvasCheckbox> button);
    void RemoveRadioButton(std::shared_ptr<UltraCanvasCheckbox> button);
    void SelectButton(std::shared_ptr<UltraCanvasCheckbox> button);
    std::shared_ptr<UltraCanvasCheckbox> GetSelectedButton() const;
    void ClearSelection();
    
    // Callback for selection changes
    std::function<void(std::shared_ptr<UltraCanvasCheckbox>)> onSelectionChanged;
};
```

## Event Handling

The checkbox responds to the following events:

### Mouse Events
- **MouseDown**: Initiates checkbox press state
- **MouseUp**: Toggles checkbox if released over the control
- **MouseMove**: Updates hover state
- **MouseEnter/Leave**: Manages hover highlighting

### Keyboard Events
- **Space/Enter**: Toggles the checkbox state
- **Tab**: Navigation between controls

### Focus Events
- **FocusGained/Lost**: Shows/hides focus ring

## Rendering Details

### Drawing Components

The checkbox rendering consists of several components:

1. **Checkbox Box**: The main square/rounded container
2. **Checkmark**: Drawn as two connected lines forming a check
3. **Indeterminate Mark**: Horizontal line in the middle
4. **Label Text**: Optional text displayed beside the checkbox
5. **Focus Ring**: Visual indicator when keyboard focused

### Render Order
1. Background box with border
2. Checkmark or indeterminate mark (if applicable)
3. Label text
4. Focus ring (if focused)

## Usage Examples

### Basic Checkbox
```cpp
auto checkbox = std::make_shared<UltraCanvasCheckbox>(
    "AgreeCheckbox", 1001, 10, 10, 200, 24, "I agree to terms"
);
checkbox->SetChecked(false);
checkbox->onChecked = []() {
    std::cout << "User agreed to terms" << std::endl;
};
```

### Tri-State Checkbox
```cpp
auto triStateBox = std::make_shared<UltraCanvasCheckbox>(
    "SelectAll", 2001, 10, 50, 200, 24, "Select All Items"
);
triStateBox->SetAllowIndeterminate(true);
triStateBox->SetCheckState(CheckboxState::Indeterminate);
triStateBox->onStateChanged = [](CheckboxState oldState, CheckboxState newState) {
    std::cout << "State changed from " << (int)oldState 
              << " to " << (int)newState << std::endl;
};
```

### Toggle Switch
```cpp
auto toggleSwitch = UltraCanvasCheckbox::CreateSwitch(
    "NotificationSwitch", 3001, 10, 100, "Enable Notifications", true
);
toggleSwitch->onChecked = []() {
    std::cout << "Notifications enabled" << std::endl;
};
toggleSwitch->onUnchecked = []() {
    std::cout << "Notifications disabled" << std::endl;
};
```

### Radio Button Group
```cpp
auto radioGroup = std::make_shared<UltraCanvasRadioGroup>();

auto radio1 = UltraCanvasCheckbox::CreateRadioButton(
    "Option1", 4001, 10, 150, "Option 1", true
);
auto radio2 = UltraCanvasCheckbox::CreateRadioButton(
    "Option2", 4002, 10, 180, "Option 2", false
);
auto radio3 = UltraCanvasCheckbox::CreateRadioButton(
    "Option3", 4003, 10, 210, "Option 3", false
);

radioGroup->AddRadioButton(radio1);
radioGroup->AddRadioButton(radio2);
radioGroup->AddRadioButton(radio3);

radioGroup->onSelectionChanged = [](auto selected) {
    std::cout << "Selected: " << selected->GetText() << std::endl;
};
```

### Custom Styling
```cpp
auto customCheckbox = std::make_shared<UltraCanvasCheckbox>(
    "CustomBox", 5001, 10, 250, 200, 30, "Custom Styled"
);

CheckboxVisualStyle customStyle;
customStyle.boxSize = 20.0f;
customStyle.boxColor = Color(240, 240, 255, 255);
customStyle.boxBorderColor = Color(100, 100, 200, 255);
customStyle.checkmarkColor = Color(0, 150, 0, 255);
customStyle.fontSize = 14.0f;
customStyle.fontWeight = FontWeight::Bold;
customStyle.cornerRadius = 4.0f;

customCheckbox->SetVisualStyle(customStyle);
customCheckbox->SetStyle(CheckboxStyle::Rounded);
```

## Best Practices

### Accessibility
1. Always provide descriptive label text
2. Ensure adequate contrast ratios for colors
3. Maintain minimum touch target sizes (44x44 pixels for mobile)
4. Support keyboard navigation

### Performance
1. Use auto-sizing sparingly for large numbers of checkboxes
2. Cache visual styles when creating multiple similar checkboxes
3. Batch state changes when updating multiple checkboxes

### User Experience
1. Provide immediate visual feedback on interaction
2. Use indeterminate state for parent checkboxes in hierarchical selections
3. Group related checkboxes visually
4. Consider using switches for on/off settings instead of checkboxes

## Implementation Status

**Current Version:** 1.1.0  
**Status:** Fully Implemented  
**Platform Support:** Windows, Linux, macOS  

### Implemented Features
- ✅ Basic checkbox functionality
- ✅ Tri-state support
- ✅ Multiple visual styles
- ✅ Keyboard navigation
- ✅ Mouse interaction
- ✅ Focus management
- ✅ Auto-sizing
- ✅ Custom styling
- ✅ Event callbacks
- ✅ Radio button groups

### Future Enhancements
- Animation support for state transitions
- Additional Material Design animations
- Custom checkmark shapes
- Theme preset support
- Right-to-left (RTL) layout support

## Related Components

- **UltraCanvasRadioGroup**: Manages exclusive selection among radio buttons
- **UltraCanvasButton**: Alternative interaction component
- **UltraCanvasSwitch**: Dedicated toggle switch component (planned)
- **UltraCanvasTreeView**: Uses checkboxes for multi-selection

## File Locations

- **Header:** `UltraCanvas/include/UltraCanvasCheckbox.h`
- **Implementation:** `UltraCanvas/core/UltraCanvasCheckbox.cpp`
- **Examples:** `Apps/DemoApp/UltraCanvasCheckboxExamples.cpp`
- **Documentation:** `Docs/UltraCanvasCheckbox.md`
