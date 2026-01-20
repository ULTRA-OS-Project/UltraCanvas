# UltraCanvasTextInput Control Documentation

## Overview

**UltraCanvasTextInput** is an advanced text input component within the UltraCanvas Framework that provides comprehensive text editing capabilities with validation, formatting, and feedback systems. It supports multiple input types, real-time validation, custom formatting, undo/redo functionality, and extensive customization options.

**Version:** 1.1.0  
**Last Modified:** 2025-01-06  
**Author:** UltraCanvas Framework

## Key Features

### Core Capabilities
- **Multiple Input Types**: Plain text, password, email, phone, number, currency, date/time, URL, multiline, and custom types
- **Validation System**: Built-in and custom validation rules with visual feedback
- **Text Formatting**: Automatic formatting for phone numbers, dates, currency, and custom patterns
- **Undo/Redo**: Full undo/redo stack with configurable history depth
- **Selection Management**: Text selection with keyboard and mouse support
- **Auto-completion**: Configurable auto-complete modes for different input types
- **Placeholder Text**: Contextual hints when field is empty
- **Read-only Mode**: Disable editing while maintaining visual presentation

### Visual Features
- **Caret Animation**: Blinking cursor with customizable rate
- **Selection Highlighting**: Visual feedback for selected text
- **Validation States**: Color-coded borders and icons for validation feedback
- **Focus Animations**: Optional animated transitions on focus changes
- **Multiple Styles**: Predefined styles (Default, Material, Flat, Outlined, Underlined)
- **Password Masking**: Secure text display for password fields

## Class Definition

### Header File Location
```cpp
#include "UltraCanvasTextInput.h"
```

### Namespace
```cpp
namespace UltraCanvas
```

## Constructor

```cpp
UltraCanvasTextInput(const std::string& id, long uid, long x, long y, long w, long h)
```

**Parameters:**
- `id`: Unique string identifier for the control
- `uid`: Numeric unique identifier
- `x, y`: Position coordinates (in pixels)
- `w, h`: Width and height dimensions (in pixels)

## Input Types

### TextInputType Enum

```cpp
enum class TextInputType {
    Text,          // Plain text input
    Password,      // Password field with masking
    Email,         // Email with validation
    Phone,         // Phone number with formatting
    Number,        // Numeric input only
    Integer,       // Integer numbers only
    Decimal,       // Decimal numbers
    Currency,      // Currency with formatting
    Date,          // Date input (MM/DD/YYYY)
    Time,          // Time input
    DateTime,      // Combined date and time
    URL,           // URL with validation
    Search,        // Search field with clear button
    Multiline,     // Multi-line text area
    Custom         // Custom validation rules
};
```

### Setting Input Type

```cpp
void SetInputType(TextInputType type)
```

Automatically configures validation and formatting based on the selected type:
- **Password**: Enables password masking
- **Email**: Adds email validation and email auto-complete
- **Phone**: Applies phone number formatting
- **Number/Integer/Decimal**: Restricts to numeric input
- **Currency**: Applies currency formatting
- **Date**: Validates and formats date input

## Text Management

### Basic Text Operations

```cpp
// Set the text content
void SetText(const std::string& newText)

// Get the current text
const std::string& GetText() const

// Get formatted display text
const std::string& GetDisplayText() const

// Set placeholder text
void SetPlaceholder(const std::string& placeholder)

// Get placeholder text
const std::string& GetPlaceholder() const
```

### Text Properties

```cpp
// Set read-only mode
void SetReadOnly(bool readonly)

// Check if read-only
bool IsReadOnly() const

// Set maximum text length
void SetMaxLength(int length)

// Get maximum length
int GetMaxLength() const
```

## Validation System

### ValidationRule Structure

```cpp
struct ValidationRule {
    std::string name;
    std::string errorMessage;
    std::function<bool(const std::string&)> validator;
    bool isRequired;
    int priority;
};
```

### Predefined Validation Rules

```cpp
// Required field
ValidationRule::Required("This field is required")

// Minimum length
ValidationRule::MinLength(8, "Must be at least 8 characters")

// Maximum length
ValidationRule::MaxLength(100, "Must be less than 100 characters")

// Email validation
ValidationRule::Email("Invalid email address")

// Phone validation
ValidationRule::Phone("Invalid phone number")

// URL validation
ValidationRule::URL("Invalid URL")

// Custom regex pattern
ValidationRule::Pattern(std::regex("^[A-Za-z0-9]+$"), "Only alphanumeric allowed")

// Custom validation function
ValidationRule::Custom("custom", "Custom error",
    [](const std::string& value) { return value.length() > 0; })
```

### Using Validation

```cpp
// Add validation rule
textInput->AddValidationRule(ValidationRule::Email());

// Add multiple rules
textInput->AddValidationRule(ValidationRule::Required());
textInput->AddValidationRule(ValidationRule::MinLength(8));

// Clear all rules
textInput->ClearValidationRules();

// Manually validate
ValidationResult result = textInput->Validate();

// Check if valid
if (textInput->IsValid()) {
    // Process valid input
}

// Get last validation result
const ValidationResult& result = textInput->GetLastValidationResult();
```

### ValidationResult Structure

```cpp
struct ValidationResult {
    bool isValid;
    ValidationState state;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};
```

### Validation States

```cpp
enum class ValidationState {
    NoValidation,   // No validation performed
    Valid,          // Input is valid
    Invalid,        // Input is invalid
    Warning,        // Input has warnings
    Processing,     // Validation in progress
    Required        // Required field indicator
};
```

## Text Formatting

### TextFormatter Structure

```cpp
struct TextFormatter {
    std::string name;
    std::function<std::string(const std::string&)> formatFunction;
    std::function<std::string(const std::string&)> unformatFunction;
    std::string mask;
    std::string placeholder;
};
```

### Predefined Formatters

```cpp
// Phone number formatter (US format)
TextFormatter::Phone()  // Formats to (XXX) XXX-XXXX

// Currency formatter (US dollars)
TextFormatter::Currency()  // Formats to $X,XXX.XX

// Credit card formatter
TextFormatter::CreditCard()  // Formats to XXXX XXXX XXXX XXXX

// Date formatter
TextFormatter::Date()  // Formats to MM/DD/YYYY

// Custom formatter
TextFormatter::Custom("custom",
    [](const std::string& value) { return formatted; },
    [](const std::string& value) { return unformatted; })
```

### Using Formatters

```cpp
// Set formatter
textInput->SetFormatter(TextFormatter::Phone());

// Get current formatter
const TextFormatter& formatter = textInput->GetFormatter();
```

## Selection and Cursor Management

### Selection Operations

```cpp
// Set selection range
void SetSelection(size_t start, size_t end)

// Select all text
void SelectAll()

// Clear selection
void ClearSelection()

// Check if has selection
bool HasSelection() const

// Get selected text
std::string GetSelectedText() const

// Delete selected text
void DeleteSelection()
```

### Cursor Operations

```cpp
// Set cursor position
void SetCaretPosition(size_t position)

// Get cursor position
size_t GetCaretPosition() const

// Move cursor to beginning
void MoveCaretToBeginning()

// Move cursor to end
void MoveCaretToEnd()
```

## Event Handling

### Keyboard Events

The control handles various keyboard inputs:

- **Character Input**: Regular text entry
- **Backspace/Delete**: Text deletion
- **Arrow Keys**: Cursor movement
- **Home/End**: Jump to beginning/end
- **Enter**: Submit (single-line) or new line (multi-line)
- **Escape**: Cancel editing
- **Tab**: Focus navigation or tab character (multi-line)
- **Ctrl+A**: Select all
- **Ctrl+C**: Copy
- **Ctrl+V**: Paste
- **Ctrl+X**: Cut
- **Ctrl+Z**: Undo
- **Ctrl+Y**: Redo

### Mouse Events

- **Click**: Position cursor
- **Double-click**: Select word
- **Triple-click**: Select line/all
- **Drag**: Select text range
- **Right-click**: Context menu (if enabled)

### Event Callbacks

```cpp
// Text change notification
std::function<void(const std::string&)> onTextChanged;

// Validation state change
std::function<void(ValidationResult)> onValidationStateChanged;

// Enter key pressed (single-line mode)
std::function<void()> onEnterPressed;

// Escape key pressed
std::function<void()> onEscapePressed;

// Focus events
std::function<void()> onFocusGained;
std::function<void()> onFocusLost;

// Selection change
std::function<void(size_t, size_t)> onSelectionChanged;
```

## Styling System

### TextInputStyle Structure

```cpp
struct TextInputStyle {
    // Colors
    Color backgroundColor;
    Color borderColor;
    Color focusBorderColor;
    Color textColor;
    Color placeholderColor;
    Color selectionColor;
    Color caretColor;
    
    // Validation colors
    Color validBorderColor;
    Color invalidBorderColor;
    Color warningBorderColor;
    
    // Dimensions
    int borderWidth;
    int borderRadius;
    int paddingLeft;
    int paddingRight;
    int paddingTop;
    int paddingBottom;
    
    // Typography
    FontStyle fontStyle;
    TextAlignment textAlignment;
    
    // Caret
    int caretWidth;
    int caretBlinkRate;
    
    // Effects
    bool showShadow;
    Color shadowColor;
    Point2Di shadowOffset;
    int shadowBlur;
    
    // Animations
    bool enableFocusAnimation;
    float animationDuration;
};
```

### Predefined Styles

```cpp
// Default style
TextInputStyle::Default()

// Material Design style
TextInputStyle::Material()

// Flat style (no borders)
TextInputStyle::Flat()

// Outlined style
TextInputStyle::Outlined()

// Underlined style
TextInputStyle::Underlined()
```

### Applying Styles

```cpp
// Set style
textInput->SetStyle(TextInputStyle::Material());

// Get current style
const TextInputStyle& style = textInput->GetStyle();

// Modify specific style properties
TextInputStyle customStyle = TextInputStyle::Default();
customStyle.borderColor = Color(100, 100, 255);
customStyle.borderRadius = 8;
textInput->SetStyle(customStyle);
```

## Undo/Redo System

### Operations

```cpp
// Undo last operation
void Undo()

// Redo last undone operation
void Redo()

// Check if can undo
bool CanUndo() const

// Check if can redo
bool CanRedo() const

// Clear undo/redo history
void ClearHistory()

// Set maximum undo states (default: 50)
void SetMaxUndoStates(int maxStates)
```

The undo system automatically saves state before:
- Text insertion
- Text deletion
- Paste operations
- Format changes

## Auto-completion

### AutoComplete Modes

```cpp
enum class AutoComplete {
    Off,
    On,
    Name,
    Email,
    Username,
    CurrentPassword,
    NewPassword,
    OneTimeCode,
    Organization,
    StreetAddress,
    Country,
    PostalCode
};
```

### Using Auto-completion

```cpp
// Set auto-complete mode
textInput->SetAutoCompleteMode(AutoComplete::Email);

// Set custom suggestions
std::vector<std::string> suggestions = {"option1", "option2", "option3"};
textInput->SetAutoCompleteSuggestions(suggestions);

// Enable/disable auto-complete
textInput->SetShowAutoComplete(true);
```

## Factory Functions

### Convenience Creation Functions

```cpp
// Create basic text input
auto textInput = CreateTextInput("myInput", 10, 10, 200, 30);

// Create email input
auto emailInput = CreateEmailInput("email", 10, 50, 200, 30);

// Create password input
auto passwordInput = CreatePasswordInput("password", 10, 90, 200, 30);

// Create phone input
auto phoneInput = CreatePhoneInput("phone", 10, 130, 200, 30);

// Create number input
auto numberInput = CreateNumberInput("number", 10, 170, 200, 30);
```

## Builder Pattern

### TextInputBuilder

```cpp
auto textInput = TextInputBuilder()
    .SetIdentifier("userInput")
    .SetID(1001)
    .SetPosition(100, 100)
    .SetCurrentSize(250, 35)
    .SetType(TextInputType::Email)
    .SetPlaceholder("Enter email address")
    .SetStyle(TextInputStyle::Material())
    .AddValidationRule(ValidationRule::Email())
    .AddValidationRule(ValidationRule::Required())
    .SetMaxLength(100)
    .Build();
```

## Usage Examples

### Basic Text Input

```cpp
// Create text input
auto nameInput = std::make_shared<UltraCanvasTextInput>(
    "nameInput", 50, 50, 200, 30);

// Configure
nameInput->SetPlaceholder("Enter your name");
nameInput->SetMaxLength(50);

// Add validation
nameInput->AddValidationRule(ValidationRule::Required());
nameInput->AddValidationRule(ValidationRule::MinLength(2));

// Set callback
nameInput->onTextChanged = [](const std::string& text) {
    std::cout << "Name changed: " << text << std::endl;
};

// Add to window
window->AddElement(nameInput);
```

### Email Input with Validation

```cpp
auto emailInput = CreateEmailInput("email", 50, 100, 250, 35);
emailInput->SetPlaceholder("user@example.com");
emailInput->SetStyle(TextInputStyle::Outlined());

emailInput->onValidationStateChanged = [](const ValidationResult& result) {
    if (!result.isValid) {
        for (const auto& error : result.errors) {
            std::cout << "Error: " << error << std::endl;
        }
    }
};
```

### Password Input

```cpp
auto passwordInput = CreatePasswordInput("password", 50, 150, 250, 35);
passwordInput->SetPlaceholder("Enter password");
passwordInput->AddValidationRule(ValidationRule::MinLength(8, "Password must be at least 8 characters"));
passwordInput->AddValidationRule(ValidationRule::Pattern(
    std::regex(".*[A-Z].*"), "Password must contain uppercase letter"));
passwordInput->AddValidationRule(ValidationRule::Pattern(
    std::regex(".*[0-9].*"), "Password must contain a number"));
```

### Phone Number Input

```cpp
auto phoneInput = CreatePhoneInput("phone", 50, 200, 250, 35);
phoneInput->SetPlaceholder("(555) 123-4567");
phoneInput->SetFormatter(TextFormatter::Phone());
```

### Multiline Text Area

```cpp
auto textArea = std::make_shared<UltraCanvasTextInput>(
    "comments", 50, 250, 400, 150);
textArea->SetInputType(TextInputType::Multiline);
textArea->SetPlaceholder("Enter comments here...");
textArea->SetMaxLength(1000);
```

### Custom Validation

```cpp
auto customInput = CreateTextInput("custom", 50, 300, 250, 35);

// Add custom validator
customInput->AddValidationRule(ValidationRule::Custom(
    "username",
    "Username must be alphanumeric and 3-20 characters",
    [](const std::string& value) {
        if (value.length() < 3 || value.length() > 20) return false;
        return std::all_of(value.begin(), value.end(), 
            [](char c) { return std::isalnum(c); });
    }
));
```

## Performance Considerations

### Text Width Caching
The control caches text width measurements to optimize rendering performance:
```cpp
mutable std::unordered_map<std::string, float> textWidthCache;
```

### Scrolling for Long Text
Automatic horizontal scrolling is implemented for text that exceeds the visible area:
- Scroll offset is automatically adjusted when cursor moves
- Maximum scroll offset is calculated based on text width

### Event Optimization
- Text changes trigger validation only when `validateOnChange` is enabled
- Caret blinking uses timer-based updates to minimize redraws
- Selection rendering is optimized to only redraw affected regions

## Thread Safety

The UltraCanvasTextInput control is **not thread-safe**. All operations should be performed on the main UI thread. If you need to update text from another thread, use appropriate synchronization or message passing to the UI thread.

## Integration with UltraCanvas Framework

### Rendering System
The control uses the UltraCanvas rendering system:
```cpp
void Render(IRenderContext* ctx) override
```

### Event System
Fully integrated with UCEvent system:
```cpp
bool OnEvent(const UCEvent& event) override
```

### Focus Management
Supports framework focus system:
```cpp
bool AcceptsFocus() const override { return true; }
```

## Best Practices

1. **Validation**: Always validate user input before processing
2. **Placeholders**: Use descriptive placeholder text to guide users
3. **Length Limits**: Set appropriate maximum lengths for fields
4. **Feedback**: Provide immediate visual feedback for validation states
5. **Accessibility**: Ensure proper tab order and keyboard navigation
6. **Error Messages**: Provide clear, actionable error messages
7. **Formatting**: Use formatters to improve data entry consistency
8. **Performance**: For large amounts of text, consider using pagination or virtualization

## Troubleshooting

### Common Issues

1. **Text not appearing**: Check text color vs background color
2. **Validation not working**: Ensure validation rules are added before input
3. **Formatting issues**: Verify formatter is compatible with input type
4. **Focus problems**: Check if control is added to window and visible
5. **Selection not visible**: Verify selection color has sufficient contrast

## Version History

- **1.1.0** (2025-01-06): Enhanced validation, formatting, and multiline support
- **1.0.0** (2024-12-15): Initial release with basic text input functionality

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class documentation
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event system documentation
- [UltraCanvasValidation](UltraCanvasValidation.md) - Advanced validation guide
- [UltraCanvasFormatting](UltraCanvasFormatting.md) - Text formatting guide
