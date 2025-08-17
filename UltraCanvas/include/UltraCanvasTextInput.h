// UltraCanvasTextInput.h
// Advanced text input component with validation, formatting, and feedback systems
// Version: 1.1.0
// Last Modified: 2025-01-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <functional>
#include <regex>
#include <memory>
#include <chrono>
#include <unordered_map>

namespace UltraCanvas {

// ===== TEXT INPUT TYPE DEFINITIONS =====
enum class TextInputType {
    Text,           // Plain text input
    Password,       // Password field with masking
    Email,          // Email validation
    Phone,          // Phone number formatting
    Number,         // Numeric input only
    Integer,        // Integer numbers only
    Decimal,        // Decimal numbers
    Currency,       // Currency formatting
    Date,           // Date input
    Time,           // Time input
    DateTime,       // Date and time
    URL,            // URL validation
    Search,         // Search field with clear button
    Multiline,      // Multi-line text area
    Custom          // Custom validation rules
};

enum class ValidationState {
    NoValidation,           // No validation performed
    Valid,          // Input is valid
    Invalid,        // Input is invalid
    Warning,        // Input has warnings
    Processing,     // Validation in progress
    Required        // Required field indicator
};

// *** REMOVED DUPLICATE ENUM - Using TextAlign from UltraCanvasRenderInterface.h ***

enum class AutoComplete {
    Off, On, Name, Email, Username, CurrentPassword, NewPassword, 
    OneTimeCode, Organization, StreetAddress, Country, PostalCode
};

// ===== VALIDATION SYSTEM =====
struct ValidationRule {
    std::string name;
    std::string errorMessage;
    std::function<bool(const std::string&)> validator;
    bool isRequired = false;
    int priority = 0;  // Higher priority rules checked first
    
    ValidationRule() = default;
    ValidationRule(const std::string& ruleName, const std::string& message, 
                  std::function<bool(const std::string&)> validatorFunc, bool required = false)
        : name(ruleName), errorMessage(message), validator(validatorFunc), isRequired(required) {}
    
    // Predefined validation rules
    static ValidationRule Required(const std::string& message = "This field is required") {
        return ValidationRule("Required", message, [](const std::string& value) {
            return !value.empty();
        }, true);
    }
    
    static ValidationRule MinLength(int minLen, const std::string& message = "") {
        std::string msg = message.empty() ? 
            "Must be at least " + std::to_string(minLen) + " characters" : message;
        return ValidationRule("MinLength", msg, [minLen](const std::string& value) {
            return value.length() >= static_cast<size_t>(minLen);
        });
    }
    
    static ValidationRule MaxLength(int maxLen, const std::string& message = "") {
        std::string msg = message.empty() ? 
            "Must be no more than " + std::to_string(maxLen) + " characters" : message;
        return ValidationRule("MaxLength", msg, [maxLen](const std::string& value) {
            return value.length() <= static_cast<size_t>(maxLen);
        });
    }
    
    static ValidationRule Email(const std::string& message = "Invalid email format") {
        return ValidationRule("Email", message, [](const std::string& value) {
            std::regex emailRegex(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
            return std::regex_match(value, emailRegex);
        });
    }
    
    static ValidationRule Phone(const std::string& message = "Invalid phone format") {
        return ValidationRule("Phone", message, [](const std::string& value) {
            std::regex phoneRegex(R"(\+?[\d\s\-\(\)\.]{10,})");
            return std::regex_match(value, phoneRegex);
        });
    }
    
    static ValidationRule Numeric(const std::string& message = "Must be a number") {
        return ValidationRule("Numeric", message, [](const std::string& value) {
            try {
                std::stod(value);
                return true;
            } catch (...) {
                return false;
            }
        });
    }
    
    static ValidationRule Range(double min, double max, const std::string& message = "") {
        std::string msg = message.empty() ? 
            "Must be between " + std::to_string(min) + " and " + std::to_string(max) : message;
        return ValidationRule("Range", msg, [min, max](const std::string& value) {
            try {
                double val = std::stod(value);
                return val >= min && val <= max;
            } catch (...) {
                return false;
            }
        });
    }
    
    static ValidationRule Pattern(const std::string& pattern, const std::string& message = "Invalid format") {
        return ValidationRule("Pattern", message, [pattern](const std::string& value) {
            try {
                std::regex regex(pattern);
                return std::regex_match(value, regex);
            } catch (...) {
                return false;
            }
        });
    }
};

struct ValidationResult {
    ValidationState state = ValidationState::NoValidation;
    std::string message;
    std::string ruleName;
    bool isValid = true;
    
    ValidationResult() = default;
    ValidationResult(ValidationState validationState, const std::string& msg = "", const std::string& rule = "")
        : state(validationState), message(msg), ruleName(rule), isValid(validationState == ValidationState::Valid || validationState == ValidationState::NoValidation) {}
    
    static ValidationResult Valid() {
        return ValidationResult(ValidationState::Valid);
    }
    
    static ValidationResult Invalid(const std::string& message, const std::string& rule = "") {
        return ValidationResult(ValidationState::Invalid, message, rule);
    }
    
    static ValidationResult Warning(const std::string& message, const std::string& rule = "") {
        return ValidationResult(ValidationState::Warning, message, rule);
    }
};

// ===== TEXT FORMATTING SYSTEM =====
struct TextFormatter {
    std::string name;
    std::function<std::string(const std::string&)> formatFunction;
    std::function<std::string(const std::string&)> unformatFunction;
    std::string inputMask;
    std::string placeholder;
    
    TextFormatter() = default;
    TextFormatter(const std::string& formatterName, 
                 std::function<std::string(const std::string&)> formatFunc,
                 std::function<std::string(const std::string&)> unformatFunc,
                 const std::string& mask = "",
                 const std::string& placeholderText = "")
        : name(formatterName), formatFunction(formatFunc), unformatFunction(unformatFunc), inputMask(mask), placeholder(placeholderText) {}
    
    static TextFormatter NoFormat() {
        return TextFormatter("NoneFill",
            [](const std::string& value) { return value; },
            [](const std::string& value) { return value; });
    }
    
    static TextFormatter Phone() {
        return TextFormatter("Phone",
            [](const std::string& value) {
                std::string digits;
                for (char c : value) {
                    if (std::isdigit(c)) digits += c;
                }
                if (digits.length() >= 10) {
                    return "(" + digits.substr(0, 3) + ") " + digits.substr(3, 3) + "-" + digits.substr(6);
                }
                return value;
            },
            [](const std::string& value) {
                std::string digits;
                for (char c : value) {
                    if (std::isdigit(c)) digits += c;
                }
                return digits;
            },
            "(000) 000-0000",
            "(555) 123-4567");
    }
    
    static TextFormatter Currency() {
        return TextFormatter("Currency",
            [](const std::string& value) {
                try {
                    double val = std::stod(value);
                    return "$" + std::to_string(val);
                } catch (...) {
                    return value;
                }
            },
            [](const std::string& value) {
                std::string result = value;
                if (result.front() == '$') result = result.substr(1);
                return result;
            },
            "$0.00",
            "$0.00");
    }
    
    static TextFormatter Date() {
        return TextFormatter("Date",
            [](const std::string& value) {
                // Simple date formatting MM/DD/YYYY
                std::string digits;
                for (char c : value) {
                    if (std::isdigit(c)) digits += c;
                }
                if (digits.length() >= 8) {
                    return digits.substr(0, 2) + "/" + digits.substr(2, 2) + "/" + digits.substr(4, 4);
                }
                return value;
            },
            [](const std::string& value) {
                std::string digits;
                for (char c : value) {
                    if (std::isdigit(c)) digits += c;
                }
                return digits;
            },
            "00/00/0000",
            "MM/DD/YYYY");
    }
    
    static TextFormatter Custom(const std::string& name,
                               std::function<std::string(const std::string&)> formatFunc,
                               std::function<std::string(const std::string&)> unformatFunc) {
        return TextFormatter(name, formatFunc, unformatFunc, "", "");
    }
};

// ===== TEXT INPUT STYLING =====
struct TextInputStyle {
    // Colors
    Color backgroundColor = Colors::White;
    Color borderColor = Color(200, 200, 200, 255);
    Color focusBorderColor = Color(0, 120, 215, 255);
    Color textColor = Colors::Black;
    Color placeholderColor = Color(150, 150, 150, 255);
    Color selectionColor = Color(0, 120, 215, 100);
    Color caretColor = Colors::Black;
    
    // Validation colors
    Color validBorderColor = Color(76, 175, 80, 255);
    Color invalidBorderColor = Color(244, 67, 54, 255);
    Color warningBorderColor = Color(255, 152, 0, 255);
    
    // Dimensions
    float borderWidth = 1.0f;
    float borderRadius = 4.0f;
    float paddingLeft = 8.0f;
    float paddingRight = 8.0f;
    float paddingTop = 6.0f;
    float paddingBottom = 6.0f;
    
    // Typography (inherits from TextStyle in RenderInterface)
    std::string fontFamily = "Arial";
    float fontSize = 12.0f;
    FontWeight fontWeight = FontWeight::Normal;
    TextAlign textAlignment = TextAlign::Left;
    
    // Caret
    float caretWidth = 1.0f;
    float caretBlinkRate = 1.0f;  // Blinks per second
    
    // Effects
    bool showShadow = false;
    Color shadowColor = Color(0, 0, 0, 50);
    Point2D shadowOffset = Point2D(1, 1);
    float shadowBlur = 2.0f;
    
    // Animations
    bool enableFocusAnimation = false;
    float animationDuration = 0.2f;
    
    // Predefined styles
    static TextInputStyle Default();
    static TextInputStyle Material();
    static TextInputStyle Flat();
    static TextInputStyle Outlined();
    static TextInputStyle Underlined();
};

// ===== UNDO/REDO SYSTEM =====
struct TextInputState {
    std::string text;
    size_t caretPosition;
    size_t selectionStart;
    size_t selectionEnd;
    std::chrono::steady_clock::time_point timestamp;
    
    TextInputState(const std::string& inputText, size_t caret, size_t selStart, size_t selEnd)
        : text(inputText), caretPosition(caret), selectionStart(selStart), selectionEnd(selEnd), timestamp(std::chrono::steady_clock::now()) {}
};

// ===== MAIN TEXT INPUT COMPONENT =====
class UltraCanvasTextInput : public UltraCanvasElement {
private:
    // ===== CORE PROPERTIES =====
    std::string text;
    std::string placeholderText;
    TextInputType inputType;
    bool readOnly;
    bool passwordMode;
    int maxLength;
    
    // ===== VALIDATION =====
    std::vector<ValidationRule> validationRules;
    ValidationResult lastValidationResult;
    bool showValidationState;
    bool validateOnChange;
    bool validateOnBlur;

    mutable std::unordered_map<std::string, float> textWidthCache;
    mutable std::string lastMeasuredFont;
    mutable float lastMeasuredSize;

    // ===== FORMATTING =====
    TextFormatter formatter;
    std::string displayText;  // Formatted version of text
    
    // ===== STYLING =====
    TextInputStyle style;
    
    // ===== CURSOR AND SELECTION =====
    size_t caretPosition;
    size_t selectionStart;
    size_t selectionEnd;
    bool hasSelection;
    bool isCaretVisible;
    float caretBlinkTimer;
    
    // ===== SCROLLING (for long text) =====
    float scrollOffset;
    float maxScrollOffset;
    
    // ===== UNDO/REDO =====
    std::vector<TextInputState> undoStack;
    std::vector<TextInputState> redoStack;
    int maxUndoStates;
    
    // ===== INTERACTION STATE =====
    bool isDragging;
    Point2D dragStartPosition;
    
    // ===== AUTO-COMPLETE =====
    AutoComplete autoCompleteMode;
    std::vector<std::string> autoCompleteSuggestions;
    bool showAutoComplete;
    
public:
    // ===== CONSTRUCTOR =====
    UltraCanvasTextInput(const std::string& id, long uid, long x, long y, long w, long h);
    
    virtual ~UltraCanvasTextInput() = default;
    
    // ===== TEXT MANAGEMENT =====
    void SetText(const std::string& newText);
    
    const std::string& GetText() const { return text; }
    const std::string& GetDisplayText() const { return displayText; }
    
    void SetPlaceholder(const std::string& placeholder) {
        placeholderText = placeholder;
    }
    
    const std::string& GetPlaceholder() const { return placeholderText; }
    
    // ===== INPUT TYPE AND BEHAVIOR =====
    void SetInputType(TextInputType type);
    
    TextInputType GetInputType() const { return inputType; }
    
    void SetReadOnly(bool readonly);
    
    bool IsReadOnly() const { return readOnly; }
    
    void SetMaxLength(int length);
    
    int GetMaxLength() const { return maxLength; }
    
    // ===== VALIDATION =====
    void AddValidationRule(const ValidationRule& rule) {
        validationRules.push_back(rule);
    }
    
    void ClearValidationRules() {
        validationRules.clear();
    }
    
    ValidationResult Validate();
    
    bool IsValid() const {
        return lastValidationResult.isValid;
    }
    
    const ValidationResult& GetLastValidationResult() const {
        return lastValidationResult;
    }
    
    void SetShowValidationState(bool show) {
        showValidationState = show;
    }
    
    // ===== FORMATTING =====
    void SetFormatter(const TextFormatter& textFormatter);
    
    const TextFormatter& GetFormatter() const { return formatter; }
    
    // ===== SELECTION AND CARET =====
    void SetSelection(size_t start, size_t end);
    
    void SelectAll() {
        SetSelection(0, text.length());
    }
    
    void ClearSelection() {
        SetSelection(caretPosition, caretPosition);
    }
    
    bool HasSelection() const { return hasSelection; }
    
    std::string GetSelectedText() const;
    
    void SetCaretPosition(size_t position);
    
    size_t GetCaretPosition() const { return caretPosition; }
    
    // ===== UNDO/REDO =====
    void Undo();
    
    void Redo();
    
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    
    // ===== STYLING =====
    void SetStyle(const TextInputStyle& inputStyle) {
        style = inputStyle;
    }
    
    const TextInputStyle& GetStyle() const { return style; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override;
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    bool OnEvent(const UCEvent& event) override;
    
    // ===== EVENT CALLBACKS =====
    std::function<void(const std::string&)> onTextChanged;
    std::function<void(size_t, size_t)> onSelectionChanged;
    std::function<void(const ValidationResult&)> onValidationChanged;
    std::function<void()> onEnterPressed;
    std::function<void()> onEscapePressed;
    std::function<void()> onFocusGained;
    std::function<void()> onFocusLost;
    
private:
    // ===== PRIVATE HELPER METHODS =====
    
    void SaveState();
    
    void UpdateScrollOffset();

    int GetCaretLineNumber() const;

    /**
     * Get the Y position for a specific line number
     */
    float GetLineYPosition(int lineNumber) const;

    /**
     * Get caret X position within current line
     */
    float GetCaretXInLine() const;

    float GetCaretXPosition();
    float GetCaretYPosition();

    Rect2D GetTextArea() const;
    
    Color GetBackgroundColor() const {
        return style.backgroundColor;
    }
    
    Color GetBorderColor() const {
        if (showValidationState) {
            switch (lastValidationResult.state) {
                case ValidationState::Valid:
                    return style.validBorderColor;
                case ValidationState::Invalid:
                    return style.invalidBorderColor;
                case ValidationState::Warning:
                    return style.warningBorderColor;
                default:
                    break;
            }
        }
        
        return IsFocused() ? style.focusBorderColor : style.borderColor;
    }
    
    Color GetTextColor() const {
        return style.textColor;
    }
    
    void UpdateCaretBlink() {
        caretBlinkTimer += 1.0f / 60.0f; // Assume 60 FPS
        
        if (caretBlinkTimer >= 1.0f / style.caretBlinkRate) {
            isCaretVisible = !isCaretVisible;
            caretBlinkTimer = 0.0f;
        }
    }
    
    void RenderText(const Rect2D& area, const Color& color);
    
    void RenderPlaceholder(const Rect2D& area);
    
    void RenderSelection(const Rect2D& area);
    
    void RenderCaret(const Rect2D& area);
    
    void RenderMultilineText(const Rect2D& area, const std::string& displayText, const Point2D& startPos);
    
    void RenderValidationFeedback(const Rect2D& bounds) const;
    
    void DrawShadow(const Rect2D& bounds);
    
    float GetAverageCharacterWidth() {
        // Simplified character width calculation
        return style.fontSize * 0.6f; // Rough approximation
    }
    
    std::vector<std::string> SplitTextIntoLines(const std::string& text, float maxWidth);
    
    std::vector<std::string> WrapLine(const std::string& line, float maxWidth);
    
    size_t GetTextPositionFromPoint(const Point2D& point);
    
    void HandleMouseDown(const UCEvent& event);
    
    void HandleMouseMove(const UCEvent& event);
    
    void HandleMouseUp(const UCEvent& event);

    void HandleKeyDown(const UCEvent& event);

    void HandleKeyUp(const UCEvent& event);

    void HandleFocusGained(const UCEvent& event);

    void HandleFocusLost(const UCEvent& event);

    void InsertText(const std::string& insertText);
    
    void DeleteSelection();
    
    void UpdateDisplayText();
    
    // Placeholder clipboard functions - would need platform-specific implementation
    void CopyToClipboard(const std::string& text);
    
    std::string GetFromClipboard();
};

// ===== STYLE IMPLEMENTATIONS =====
inline TextInputStyle TextInputStyle::Default() {
    return TextInputStyle{};
}

inline TextInputStyle TextInputStyle::Material() {
    TextInputStyle style;
    style.focusBorderColor = Color(25, 118, 210);
    style.borderRadius = 4.0f;
    style.paddingLeft = 12.0f;
    style.paddingRight = 12.0f;
    style.enableFocusAnimation = true;
    return style;
}

inline TextInputStyle TextInputStyle::Flat() {
    TextInputStyle style;
    style.borderWidth = 0.0f;
    style.backgroundColor = Color(248, 248, 248);
    style.borderRadius = 8.0f;
    return style;
}

inline TextInputStyle TextInputStyle::Outlined() {
    TextInputStyle style;
    style.backgroundColor = Colors::Transparent;
    style.borderWidth = 2.0f;
    style.borderRadius = 4.0f;
    return style;
}

inline TextInputStyle TextInputStyle::Underlined() {
    TextInputStyle style;
    style.backgroundColor = Colors::Transparent;
    style.borderWidth = 0.0f;
    style.borderRadius = 0.0f;
    // Would need special underline rendering
    return style;
}

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasTextInput> CreateTextInput(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return std::make_shared<UltraCanvasTextInput>(identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasTextInput> CreatePasswordInput(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto input = CreateTextInput(identifier, id, x, y, w, h);
    input->SetInputType(TextInputType::Password);
    return input;
}

inline std::shared_ptr<UltraCanvasTextInput> CreateEmailInput(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto input = CreateTextInput(identifier, id, x, y, w, h);
    input->SetInputType(TextInputType::Email);
    return input;
}

inline std::shared_ptr<UltraCanvasTextInput> CreatePhoneInput(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto input = CreateTextInput(identifier, id, x, y, w, h);
    input->SetInputType(TextInputType::Phone);
    return input;
}

inline std::shared_ptr<UltraCanvasTextInput> CreateNumberInput(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto input = CreateTextInput(identifier, id, x, y, w, h);
    input->SetInputType(TextInputType::Number);
    return input;
}

//inline std::shared_ptr<UltraCanvasTextInput> CreateTextInput(
//    const std::string& identifier, long id, long x, long y, long w, long h) {
//    auto input = CreateTextInput(identifier, id, x, y, w, h);
//    input->SetInputType(TextInputType::Multiline);
//    return input;
//}

// ===== BUILDER PATTERN =====
class TextInputBuilder {
private:
    std::string identifier = "TextInput";
    long id = 0;
    long x = 0, y = 0, w = 200, h = 32;
    TextInputType type = TextInputType::Text;
    std::string placeholder;
    std::string initialText;
    TextInputStyle style = TextInputStyle::Default();
    std::vector<ValidationRule> rules;
    TextFormatter formatter = TextFormatter::NoFormat();
    bool readOnly = false;
    int maxLength = -1;
    
public:
    TextInputBuilder& SetIdentifier(const std::string& inputId) { identifier = inputId; return *this; }
    TextInputBuilder& SetID(long inputId) { id = inputId; return *this; }
    TextInputBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    TextInputBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    TextInputBuilder& SetType(TextInputType inputType) { type = inputType; return *this; }
    TextInputBuilder& SetPlaceholder(const std::string& text) { placeholder = text; return *this; }
    TextInputBuilder& SetText(const std::string& text) { initialText = text; return *this; }
    TextInputBuilder& SetStyle(const TextInputStyle& inputStyle) { style = inputStyle; return *this; }
    TextInputBuilder& SetFormatter(const TextFormatter& textFormatter) { formatter = textFormatter; return *this; }
    TextInputBuilder& SetReadOnly(bool readonly) { readOnly = readonly; return *this; }
    TextInputBuilder& SetMaxLength(int length) { maxLength = length; return *this; }
    TextInputBuilder& AddValidationRule(const ValidationRule& rule) { rules.push_back(rule); return *this; }
    TextInputBuilder& Required(const std::string& message = "") { 
        rules.push_back(ValidationRule::Required(message)); return *this; 
    }
    TextInputBuilder& MinLength(int length, const std::string& message = "") { 
        rules.push_back(ValidationRule::MinLength(length, message)); return *this; 
    }
    TextInputBuilder& MaxLength(int length, const std::string& message = "") { 
        rules.push_back(ValidationRule::MaxLength(length, message)); return *this; 
    }
    TextInputBuilder& Email(const std::string& message = "") { 
        rules.push_back(ValidationRule::Email(message)); return *this; 
    }
    TextInputBuilder& Phone(const std::string& message = "") { 
        rules.push_back(ValidationRule::Phone(message)); return *this; 
    }
    TextInputBuilder& Numeric(const std::string& message = "") { 
        rules.push_back(ValidationRule::Numeric(message)); return *this; 
    }
    
    std::shared_ptr<UltraCanvasTextInput> Build() {
        auto input = std::make_shared<UltraCanvasTextInput>(identifier, id, x, y, w, h);
        
        input->SetInputType(type);
        input->SetPlaceholder(placeholder);
        input->SetText(initialText);
        input->SetStyle(style);
        input->SetFormatter(formatter);
        input->SetReadOnly(readOnly);
        input->SetMaxLength(maxLength);
        
        for (const auto& rule : rules) {
            input->AddValidationRule(rule);
        }
        
        return input;
    }
};

} // namespace UltraCanvas
