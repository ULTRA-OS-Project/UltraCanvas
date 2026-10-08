# UltraCanvasTextInput Control Documentation

## Overview

**UltraCanvasTextInput** is an advanced text input component within the UltraCanvas Framework that provides comprehensive text editing capabilities with validation, formatting, and feedback systems. It supports multiple input types, real-time validation, custom formatting, undo/redo functionality, and extensive customization options. It is a single-line field; for multi-line text use `UltraCanvasTextArea`.

**Version:** 1.4.0  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework

## Key Features

### Core Capabilities
- **Multiple Input Types**: Plain text, password, email, phone, number, currency, date/time, URL, search, and custom types
- **Validation System**: Built-in and custom validation rules with visual feedback
- **Text Formatting**: Automatic formatting for phone numbers, dates, currency, and custom patterns
- **Undo/Redo**: Undo/redo stack holding the last 50 states
- **Selection Management**: Text selection with keyboard and mouse support
- **Clipboard**: Copy, cut and paste from the keyboard or from code
- **Clear Button**: Optional in-field button that empties the field
- **Placeholder Text**: Contextual hints when field is empty
- **Read-only Mode**: Disable editing while maintaining visual presentation
- **UTF-8 Text**: Every position the field keeps is a character boundary, so accented, umlauted and non-Latin text edits like any other (see [Text and UTF-8](#text-and-utf-8))

### Visual Features
- **Caret Animation**: Blinking cursor with customizable rate
- **Selection Highlighting**: Visual feedback for selected text
- **Validation States**: Color-coded borders and icons for validation feedback
- **Focus Animations**: Optional animated transitions on focus changes
- **Multiple Styles**: Predefined styles (Default, Material, Flat, Outlined, Underlined)
- **Password Masking**: Secure text display for password fields
- **Password Reveal**: Optional in-field eye button (and a settable reveal state) to show the typed password

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
UltraCanvasTextInput(const std::string& id, float x, float y, float w, float h);

// Position left to the layout (x and y are -1)
UltraCanvasTextInput(const std::string& id, float w, float h);

// Position and size left to the layout
explicit UltraCanvasTextInput(const std::string& id);
```

**Parameters:**
- `id`: Unique string identifier for the control
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
    Custom         // Custom validation rules
};
```

### Setting Input Type

```cpp
void SetInputType(TextInputType type);
TextInputType GetInputType() const;
```

Automatically configures validation and formatting based on the selected type:
- **Password**: Enables password masking (switching to any other type drops it)
- **Email**: Adds `ValidationRule::Email()`
- **Phone**: Applies `TextFormatter::Phone()` and adds `ValidationRule::Phone()`
- **Number/Integer/Decimal**: Adds `ValidationRule::Numeric()`
- **Currency**: Applies `TextFormatter::Currency()` and adds `ValidationRule::Numeric()`
- **Date**: Applies `TextFormatter::Date()`

The rules a type brings are replaced when the type changes: switching Email
to Text drops the email check, and setting a type twice does not check twice.
Rules added with `AddValidationRule()` stay through any type change, whether
they were added before or after it. `ClearValidationRules()` removes both.

Phone, Currency and Date set their formatter through `SetFormatter()`, so a
field without a placeholder gets the formatter's (see
[Using Formatters](#using-formatters)).

## Text Management

### Basic Text Operations

```cpp
// Set the text content
void SetText(const std::string& newText);

// Get the current text
const std::string& GetText() const;

// Get formatted display text
const std::string& GetDisplayText() const;

// The text as painted: one '*' per character in password mode,
// otherwise the formatted display text
std::string GetRenderText() const;

// Set placeholder text
void SetPlaceholder(const std::string& placeholder);

// Get placeholder text
const std::string& GetPlaceholder() const;

// Keep showing the placeholder while the empty field has focus
void SetShowPlaceholderAlways(bool show);
bool IsShowPlaceholderAlways() const;
```

### Text Properties

```cpp
// Set read-only mode
void SetReadOnly(bool readonly);

// Check if read-only
bool IsReadOnly() const;

// Set maximum text length, counted in characters
void SetMaxLength(int length);

// Get maximum length
int GetMaxLength() const;
```

### Text and UTF-8

Text is UTF-8 throughout, and a character is not a byte: `ö` is two bytes, `€`
three, an emoji four. The field addresses its buffer by byte offset — that is
what `GetCaretPosition()`, `SetCaretPosition()` and `SetSelection()` take and
return — but it only ever *keeps* an offset that starts a character:

- Backspace and Delete erase a whole character; the arrow keys step over one.
- `SetCaretPosition()` and `SetSelection()` snap an offset that lands inside a
  character back to that character's start, so a selection is always a whole
  number of characters.
- Clicking snaps to the nearer boundary of the character under the pointer.
- `SetMaxLength()` and the `MinLength` / `MaxLength` validation rules count
  characters, the unit their message speaks in — `"Fröhling"` is eight
  characters in nine bytes.
- Text handed in from outside (`SetText()`, a paste, a keyboard backend with no
  input method) is repaired with U+FFFD if it is not valid UTF-8, because a
  single stray byte would otherwise make the whole string unrenderable.
- A password field masks one `*` per character, so the row of stars matches what
  was typed rather than how many bytes it took.

Walking a byte offset by hand is the one way back into trouble: use
`utf8_prev_boundary()` / `utf8_next_boundary()` from `UltraCanvasUtilsUtf8.h`
rather than `pos - 1` / `pos + 1`.

## Validation System

### ValidationRule Structure

```cpp
struct ValidationRule {
    std::string name;
    std::string errorMessage;
    std::function<bool(const std::string&)> validator;
    bool isRequired = false;
    int priority = 0;  // Higher priority rules checked first

    ValidationRule() = default;
    ValidationRule(const std::string& ruleName, const std::string& message,
                  std::function<bool(const std::string&)> validatorFunc, bool required = false);
};
```

A custom rule is built with the constructor: a name, the error message, and a
function that returns `true` when the value is acceptable.

### Predefined Validation Rules

All are static members of `ValidationRule`:

```cpp
struct ValidationRule {
    static ValidationRule Required(const std::string& message = "This field is required");
    static ValidationRule MinLength(int minLen, const std::string& message = "");
    static ValidationRule MaxLength(int maxLen, const std::string& message = "");
    static ValidationRule Email(const std::string& message = "Invalid email format");
    static ValidationRule Phone(const std::string& message = "Invalid phone format");
    static ValidationRule Numeric(const std::string& message = "Must be a number");
    static ValidationRule Range(double min, double max, const std::string& message = "");

    // Regular expression, passed as a string
    static ValidationRule Pattern(const std::string& pattern, const std::string& message = "Invalid format");

    // Password rules
    static ValidationRule RequireUppercase(int minCount = 1, const std::string& message = "");
    static ValidationRule RequireLowercase(int minCount = 1, const std::string& message = "");
    static ValidationRule RequireDigit(int minCount = 1, const std::string& message = "");
    static ValidationRule RequireSpecialChar(int minCount = 1, const std::string& message = "");
    static ValidationRule NoRepeatingChars(int maxRepeat = 3, const std::string& message = "");
    static ValidationRule NoSequentialChars(int maxSequence = 3, const std::string& message = "");
    static ValidationRule NoCommonPasswords(const std::string& message = "This password is too common");
    static ValidationRule NoUserInfo(const std::string& username, const std::string& email = "", const std::string& message = "");

    // Password strength helpers (not rules)
    static float CalculatePasswordStrength(const std::string& password);
    static std::string GetPasswordStrengthLevel(float strength);
    static Color GetPasswordStrengthColor(float strength);
};
```

```cpp
// Custom regex pattern
textInput->AddValidationRule(
    ValidationRule::Pattern("^[A-Za-z0-9]+$", "Only alphanumeric allowed"));

// Custom validation function
textInput->AddValidationRule(ValidationRule("custom", "Custom error",
    [](const std::string& value) { return value.length() > 0; }));
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
const ValidationResult& last = textInput->GetLastValidationResult();

// Show or hide the validation border colour and icon (shown by default)
textInput->SetShowValidationState(false);
```

`Validate()` runs the rules in priority order (rules of equal priority in the
order they were added) and stops at the first one that fails, so a result
carries at most one message.

The rules run on every edit and again when the field loses the focus, before
`onFocusLost` is called: a required field the user tabs through without typing
shows its error. A field that loses the focus because it is being hidden or
disabled is not validated.

A format rule - `Email()`, `Phone()`, `Numeric()`, `Range()`, `Pattern()` -
accepts an empty value: it checks what was typed, not whether something was.
An optional Email field left empty is fine; add `ValidationRule::Required()`
to make it mandatory, as with `required` on an HTML form field.
(`MinLength()` does count an empty value as too short, which is what a
password checklist shows.)

### ValidationResult Structure

```cpp
struct ValidationResult {
    ValidationState state = ValidationState::NoValidation;
    std::string message;    // error message of the failing rule
    std::string ruleName;   // name of the failing rule
    bool isValid = true;

    static ValidationResult Valid();
    static ValidationResult Invalid(const std::string& message, const std::string& rule = "");
    static ValidationResult Warning(const std::string& message, const std::string& rule = "");
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
    std::string inputMask;
    std::string placeholder;
};
```

### Predefined Formatters

All are static members of `TextFormatter`:

```cpp
struct TextFormatter {
    // No formatting: the text is shown as typed
    static TextFormatter NoFormat();

    // Phone number formatter (US format): (XXX) XXX-XXXX once 10 digits are typed
    static TextFormatter Phone();

    // Currency formatter: "$" followed by the number
    static TextFormatter Currency();

    // Date formatter: MM/DD/YYYY once 8 digits are typed
    static TextFormatter Date();

    // Custom formatter
    static TextFormatter Custom(const std::string& name,
                               std::function<std::string(const std::string&)> formatFunc,
                               std::function<std::string(const std::string&)> unformatFunc);
};
```

```cpp
// Custom formatter: show the text in upper case
textInput->SetFormatter(TextFormatter::Custom("upper",
    [](const std::string& value) {
        std::string formatted = value;
        for (char& c : formatted) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return formatted;
    },
    [](const std::string& value) { return value; }));
```

The formatter only changes what is shown (`GetDisplayText()`); `GetText()`
returns the text as typed.

The field never turns formatted text back into raw text - it keeps the typed
text and formats a copy for display - so it does not call `unformatFunction`.
That function is for the caller holding formatted text, such as the display
text, who wants the raw value:

```cpp
// "(555) 123-4567" on screen -> "5551234567"
std::string digits = textInput->GetFormatter().unformatFunction(textInput->GetDisplayText());
```

### Using Formatters

```cpp
// Set formatter
textInput->SetFormatter(TextFormatter::Phone());

// Get current formatter
const TextFormatter& formatter = textInput->GetFormatter();
```

A formatter's `placeholder` ("(555) 123-4567", "$0.00", "MM/DD/YYYY") is a
default: `SetFormatter()` uses it only when the field has no placeholder, and
keeps one you set.

## Selection and Cursor Management

### Selection Operations

```cpp
// Set selection range
void SetSelection(size_t start, size_t end);

// Select all text
void SelectAll();

// Clear selection
void ClearSelection();

// Check if has selection
bool HasSelection() const;

// Get selected text
std::string GetSelectedText() const;
```

### Clipboard

```cpp
// Copy the selection (works on a read-only field too)
void Copy();

// Copy, then delete the selection (no-op when read-only or nothing is selected)
void Cut();

// Insert the clipboard at the caret (no-op when read-only); line breaks become spaces
void Paste();
```

`TextInputStyle::selectionColor` is painted as a band **behind** the selected
characters, so a translucent selection colour tints the background without
washing the text out.

### Cursor Operations

```cpp
// Set cursor position
void SetCaretPosition(size_t position);

// Get cursor position
size_t GetCaretPosition() const;
```

To move the caret to the start or end from code:

```cpp
textInput->SetCaretPosition(0);
textInput->SetCaretPosition(textInput->GetText().size());
```

## Event Handling

### Keyboard Events

The control handles various keyboard inputs:

- **Character Input**: Regular text entry
- **Backspace/Delete**: Text deletion
- **Arrow Keys**: Cursor movement (Ctrl+Left/Right jump a word)
- **Home/End**: Jump to beginning/end
- **Enter**: Calls `onEnterPressed` with the text. The numeric
  keypad's Enter does the same — it arrives as its own key code
  (`UCKeys::NumPadEnter`) and is treated exactly like `UCKeys::Return`.
- **Escape**: Calls `onEscapePressed`
- **Tab**: Focus navigation
- **Ctrl+A**: Select all
- **Ctrl+C / Ctrl+Insert**: Copy
- **Ctrl+V / Shift+Insert**: Paste
- **Ctrl+X / Ctrl+Delete**: Cut
- **Ctrl+Z**: Undo
- **Ctrl+Y / Ctrl+Shift+Z**: Redo

### Mouse Events

- **Click**: Position cursor
- **Shift+Click**: Extend the selection to the click
- **Drag**: Select text range (keeps selecting while the pointer is outside the field)
- **Click on the clear button / eye button**: Empty the field / toggle the password mask

### Event Callbacks

```cpp
// Text change notification
std::function<void(const std::string&)> onTextChanged;

// Validation result after every Validate()
std::function<void(const ValidationResult&)> onValidationChanged;

// Enter key pressed; receives the text, return true if handled
std::function<bool(const std::string&)> onEnterPressed;

// Escape key pressed; return true if handled
std::function<bool()> onEscapePressed;

// Focus events
std::function<void()> onFocusGained;
std::function<void()> onFocusLost;

// Selection change
std::function<void(size_t, size_t)> onSelectionChanged;

// The clear button emptied the field
std::function<void()> onCleared;

// The password mask was toggled (see Password Reveal)
std::function<void(bool)> onPasswordVisibilityChanged;
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

All are static members of `TextInputStyle`:

```cpp
struct TextInputStyle {
    // Default style
    static TextInputStyle Default();

    // Material Design style
    static TextInputStyle Material();

    // Flat style (no borders)
    static TextInputStyle Flat();

    // Outlined style
    static TextInputStyle Outlined();

    // Underlined style
    static TextInputStyle Underlined();
};
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

// Change only the font size
textInput->SetFontSize(14.0f);
```

```cpp
void SetStyle(const TextInputStyle& inputStyle);
const TextInputStyle& GetStyle() const;
void SetFontSize(float size);
```

## Undo/Redo System

### Operations

```cpp
// Undo last operation
void Undo();

// Redo last undone operation
void Redo();

// Check if can undo
bool CanUndo() const;

// Check if can redo
bool CanRedo() const;
```

The undo system keeps the last 50 states and automatically saves state before:
- Text insertion
- Text deletion
- Paste operations
- `SetText()` and the clear button

One key press is one undo step, including a character, Space or paste that
replaces a selection, and Backspace or Delete on a selection. A key press the
length limit refuses changes nothing and leaves no step.

## Clear Button

```cpp
void SetShowClearButton(bool show);
bool IsShowClearButton() const;
```

When on, a small button at the right of the field empties it. It is shown only
while the field has text and is not read-only. Clicking it saves an undo state,
clears the text and calls `onTextChanged` and then `onCleared`.

## Auto-completion

`UltraCanvasTextInput` has no suggestion list of its own. The header declares an
`AutoComplete` enum (`Off`, `On`, `Name`, `Email`, ...), but no member of the
text input takes it. For a field with a drop-down of suggestions use
`UltraCanvasAutoComplete` — see
[UltraCanvasAutoComplete](UltraCanvasAutoCompleteExamples.md).

## Password Reveal ("Show Password")

Password fields mask what the user types, so they need a way to read it back.
`UltraCanvasTextInput` offers both of the usual controls, and they share one state.

### In-field eye button

```cpp
// Every password field has the eye icon at its right by default.
auto passwordInput = CreatePasswordInput("password", 10, 10, 300, 30);

// A field that must never show its text can switch it off:
passwordInput->SetShowPasswordToggle(false);
```

`showPasswordToggle` defaults to `true` (since TextInput 1.6.0), for the
builder as well. `CreateRevealablePasswordInput()` is kept and is now the same
as `CreatePasswordInput()`.

The button is only painted for fields in password mode (`TextInputType::Password`),
so it is safe to switch on before the input type is set. It sits to the left of the
validation icon, and the text area shrinks so typed text never runs underneath it.
Clicking it flips the mask; the icon shows a plain eye while the password is hidden
and a struck-through eye while it is visible.

### Explicit "Show password" flag

Any external control can drive the same state - useful when the form wants a visible
label rather than an icon:

```cpp
auto showPassword = UltraCanvasCheckbox::CreateCheckbox(
        "ShowPassword", 10, 50, 160, 20, "Show password", false);

auto* input = passwordInput.get();
showPassword->onStateChanged = [input](CheckedState, CheckedState newState) {
    input->SetPasswordRevealed(newState == CheckedState::Checked);
};
```

### API

```cpp
void SetShowPasswordToggle(bool show);   // show/hide the in-field eye button
bool IsShowPasswordToggle() const;

void SetPasswordRevealed(bool revealed); // unmask/mask from code
bool IsPasswordRevealed() const;
void TogglePasswordVisibility();

void SetPasswordToggleColors(const Color& normal, const Color& hovered);

// Fired by the eye button and by SetPasswordRevealed() alike; a no-op change
// does not fire, so an eye button and a checkbox can mirror each other safely.
std::function<void(bool)> onPasswordVisibilityChanged;
```

`GetText()` always returns the real text; only the painted glyphs change. Reveal
state is reset automatically when the field is switched away from password mode.

To assistive technology a text input is a text field, and in password mode a
password field (`IsAccessiblePassword()`, revealed or not): screen readers on
Windows (UI Automation `IsPassword`) and Linux (AT-SPI password text) then say
"password" instead of speaking what is typed. See
[UltraCanvasAccessibility](UltraCanvasAccessibility.md).

## Factory Functions

### Convenience Creation Functions

Every factory takes the identifier first and float geometry, like the
constructor: `(const std::string& identifier, float x, float y, float w, float h)`.

```cpp
// Create basic text input
auto textInput = CreateTextInput("myInput", 10, 10, 200, 30);

// Create email input
auto emailInput = CreateEmailInput("email", 10, 50, 200, 30);

// Create password input
auto passwordInput = CreatePasswordInput("password", 10, 90, 200, 30);

// Create password input carrying the in-field eye ("show password") button
auto revealablePassword = CreateRevealablePasswordInput("password2", 10, 90, 200, 30);

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
    .SetPosition(100, 100)
    .SetSize(250, 35)
    .SetType(TextInputType::Email)
    .SetPlaceholder("Enter email address")
    .SetStyle(TextInputStyle::Material())
    .AddValidationRule(ValidationRule::Email())
    .AddValidationRule(ValidationRule::Required())
    .SetMaxLength(100)
    .ShowPasswordToggle()   // eye button (the default); only painted for password-type fields
    .Build();
```

`SetPosition()` and `SetSize()` take floats and keep fractions.
The builder also has `SetText()`, `SetFormatter()`, `SetReadOnly()` and
shortcuts that add a rule: `Required()`, `MinLength()`, `MaxLength()`,
`Email()`, `Phone()` and `Numeric()`.

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
    std::cerr << "Name changed: " << text << std::endl;
};

// Add to window
window->AddChild(nameInput);
```

### Email Input with Validation

```cpp
auto emailInput = CreateEmailInput("email", 50, 100, 250, 35);
emailInput->SetPlaceholder("user@example.com");
emailInput->SetStyle(TextInputStyle::Outlined());

emailInput->onValidationChanged = [](const ValidationResult& result) {
    if (!result.isValid) {
        std::cerr << "Error: " << result.message << std::endl;
    }
};
```

### Password Input

```cpp
auto passwordInput = CreatePasswordInput("password", 50, 150, 250, 35);
passwordInput->SetPlaceholder("Enter password");
passwordInput->AddValidationRule(ValidationRule::MinLength(8, "Password must be at least 8 characters"));
passwordInput->AddValidationRule(ValidationRule::RequireUppercase(1, "Password must contain uppercase letter"));
passwordInput->AddValidationRule(ValidationRule::Pattern(
    ".*[0-9].*", "Password must contain a number"));

// Let the user check what was typed (see "Password Reveal" above)
passwordInput->SetShowPasswordToggle(true);
```

### Phone Number Input

```cpp
auto phoneInput = CreatePhoneInput("phone", 50, 200, 250, 35);
phoneInput->SetPlaceholder("(555) 123-4567");
phoneInput->SetFormatter(TextFormatter::Phone());
```

### Search Field

```cpp
auto searchInput = std::make_shared<UltraCanvasTextInput>(
    "search", 50, 250, 250, 35);
searchInput->SetInputType(TextInputType::Search);
searchInput->SetPlaceholder("Search...");
searchInput->SetShowClearButton(true);
searchInput->onEnterPressed = [](const std::string& query) {
    std::cerr << "Search for: " << query << std::endl;
    return true;
};
```

For multi-line text use `UltraCanvasTextArea` — see
[UltraCanvasTextArea](UltraCanvasTextAreaExamples.md).

### Custom Validation

```cpp
auto customInput = CreateTextInput("custom", 50, 300, 250, 35);

// Add custom validator
customInput->AddValidationRule(ValidationRule(
    "username",
    "Username must be alphanumeric and 3-20 characters",
    [](const std::string& value) {
        if (value.length() < 3 || value.length() > 20) return false;
        return std::all_of(value.begin(), value.end(),
            [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
    }
));
```

## Performance Considerations

### Text Width Caching
The control caches text width measurements internally to optimize rendering performance.

### Scrolling for Long Text
Automatic horizontal scrolling is implemented for text that exceeds the visible area:
- Scroll offset is automatically adjusted when cursor moves
- Maximum scroll offset is calculated based on text width

### Event Optimization
- Every text change re-runs the validation rules; a field with no rules skips the work
- Caret blinking uses timer-based updates to minimize redraws
- Selection rendering is optimized to only redraw affected regions

## Thread Safety

The UltraCanvasTextInput control is **not thread-safe**. All operations should be performed on the main UI thread. If you need to update text from another thread, use appropriate synchronization or message passing to the UI thread.

## Integration with UltraCanvas Framework

### Rendering System
The control uses the UltraCanvas rendering system:
```cpp
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
```

### Event System
Fully integrated with UCEvent system:
```cpp
bool OnEvent(const UCEvent& event) override;
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

- **1.4.0** (2026-10-07): `SetFormatter` keeps a placeholder you set and fills an empty one; the rules a type adds are replaced when the type changes; losing the focus validates; one key press is one undo step; float geometry for every factory and the builder; when to use `unformatFunction`
- **1.3.1** (2026-10-07): Matched the API to the header: constructors, validation and formatter members, callbacks, builder; removed members that do not exist (Multiline type, auto-complete setters, history settings)
- **1.1.0** (2025-01-06): Enhanced validation, formatting, and multiline support
- **1.0.0** (2024-12-15): Initial release with basic text input functionality

## See Also

- [UltraCanvasUIElements](UltraCanvasUIElements.md) - UI element overview
- [UltraCanvasTextArea](UltraCanvasTextAreaExamples.md) - Multi-line text editing
- [UltraCanvasAutoComplete](UltraCanvasAutoCompleteExamples.md) - Text field with suggestions
- [UltraCanvasCheckbox](UltraCanvasCheckbox.md) - Checkbox used for "Show password"
