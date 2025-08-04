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
    UltraCanvasTextInput(const std::string& id, long uid, long x, long y, long w, long h)
        : UltraCanvasElement(id, uid, x, y, w, h)
        , text("")
        , placeholderText("")
        , inputType(TextInputType::Text)
        , readOnly(false)
        , passwordMode(false)
        , maxLength(-1)
        , lastValidationResult(ValidationResult::Valid())
        , showValidationState(true)
        , validateOnChange(true)
        , validateOnBlur(true)
        , formatter(TextFormatter::NoFormat())
        , displayText("")
        , style(TextInputStyle::Default())
        , caretPosition(0)
        , selectionStart(0)
        , selectionEnd(0)
        , hasSelection(false)
        , isCaretVisible(true)
        , caretBlinkTimer(0.0f)
        , scrollOffset(0.0f)
        , maxScrollOffset(0.0f)
        , maxUndoStates(50)
        , isDragging(false)
        , autoCompleteMode(AutoComplete::Off)
        , showAutoComplete(false) {
    }
    
    virtual ~UltraCanvasTextInput() = default;
    
    // ===== TEXT MANAGEMENT =====
    void SetText(const std::string& newText) {
        if (readOnly) return;
        
        SaveState();  // For undo
        
        text = newText;
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
        
        // Clamp caret position
        caretPosition = std::min(caretPosition, text.length());
        
        // Clear selection if it's now invalid
        if (selectionEnd > text.length()) {
            ClearSelection();
        }
        
        // Validate if needed
        if (validateOnChange) {
            Validate();
        }
        
        UpdateScrollOffset();
        
        if (onTextChanged) onTextChanged(text);
    }
    
    const std::string& GetText() const { return text; }
    const std::string& GetDisplayText() const { return displayText; }
    
    void SetPlaceholder(const std::string& placeholder) {
        placeholderText = placeholder;
    }
    
    const std::string& GetPlaceholder() const { return placeholderText; }
    
    // ===== INPUT TYPE AND BEHAVIOR =====
    void SetInputType(TextInputType type) {
        inputType = type;
        
        // Configure based on type
        switch (type) {
            case TextInputType::Password:
                passwordMode = true;
                break;
            case TextInputType::Email:
                AddValidationRule(ValidationRule::Email());
                autoCompleteMode = AutoComplete::Email;
                break;
            case TextInputType::Phone:
                SetFormatter(TextFormatter::Phone());
                AddValidationRule(ValidationRule::Phone());
                break;
            case TextInputType::Number:
            case TextInputType::Integer:
            case TextInputType::Decimal:
                AddValidationRule(ValidationRule::Numeric());
                break;
            case TextInputType::Currency:
                SetFormatter(TextFormatter::Currency());
                AddValidationRule(ValidationRule::Numeric());
                break;
            case TextInputType::Date:
                SetFormatter(TextFormatter::Date());
                break;
            default:
                break;
        }
    }
    
    TextInputType GetInputType() const { return inputType; }
    
    void SetReadOnly(bool readonly) {
        readOnly = readonly;
        if (readonly) {
            ClearSelection();
        }
    }
    
    bool IsReadOnly() const { return readOnly; }
    
    void SetMaxLength(int length) {
        maxLength = length;
        if (maxLength > 0 && static_cast<int>(text.length()) > maxLength) {
            SetText(text.substr(0, maxLength));
        }
    }
    
    int GetMaxLength() const { return maxLength; }
    
    // ===== VALIDATION =====
    void AddValidationRule(const ValidationRule& rule) {
        validationRules.push_back(rule);
    }
    
    void ClearValidationRules() {
        validationRules.clear();
    }
    
    ValidationResult Validate() {
        ValidationResult result = ValidationResult::Valid();
        
        // Check all rules in priority order
        std::sort(validationRules.begin(), validationRules.end(), 
                  [](const ValidationRule& a, const ValidationRule& b) {
                      return a.priority > b.priority;
                  });
        
        for (const auto& rule : validationRules) {
            if (!rule.validator(text)) {
                result = ValidationResult::Invalid(rule.errorMessage, rule.name);
                break;
            }
        }
        
        lastValidationResult = result;
        
        if (onValidationChanged) onValidationChanged(result);
        
        return result;
    }
    
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
    void SetFormatter(const TextFormatter& textFormatter) {
        formatter = textFormatter;
        if (!placeholderText.empty() && !formatter.placeholder.empty()) {
            placeholderText = formatter.placeholder;
        }
        
        // Reformat current text
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
    }
    
    const TextFormatter& GetFormatter() const { return formatter; }
    
    // ===== SELECTION AND CARET =====
    void SetSelection(size_t start, size_t end) {
        selectionStart = std::min(start, text.length());
        selectionEnd = std::min(end, text.length());
        
        if (selectionStart > selectionEnd) {
            std::swap(selectionStart, selectionEnd);
        }
        
        hasSelection = (selectionStart != selectionEnd);
        caretPosition = selectionEnd;
        
        UpdateScrollOffset();
        
        if (onSelectionChanged) onSelectionChanged(selectionStart, selectionEnd);
    }
    
    void SelectAll() {
        SetSelection(0, text.length());
    }
    
    void ClearSelection() {
        SetSelection(caretPosition, caretPosition);
    }
    
    bool HasSelection() const { return hasSelection; }
    
    std::string GetSelectedText() const {
        if (!hasSelection) return "";
        return text.substr(selectionStart, selectionEnd - selectionStart);
    }
    
    void SetCaretPosition(size_t position) {
        caretPosition = std::min(position, text.length());
        ClearSelection();
        UpdateScrollOffset();
    }
    
    size_t GetCaretPosition() const { return caretPosition; }
    
    // ===== UNDO/REDO =====
    void Undo() {
        if (undoStack.empty()) return;
        
        // Save current state to redo stack
        redoStack.emplace_back(text, caretPosition, selectionStart, selectionEnd);
        
        // Restore previous state
        const auto& state = undoStack.back();
        text = state.text;
        caretPosition = state.caretPosition;
        selectionStart = state.selectionStart;
        selectionEnd = state.selectionEnd;
        hasSelection = (selectionStart != selectionEnd);
        
        undoStack.pop_back();
        
        // Reformat text
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
        
        UpdateScrollOffset();
        
        if (onTextChanged) onTextChanged(text);
    }
    
    void Redo() {
        if (redoStack.empty()) return;
        
        // Save current state to undo stack
        undoStack.emplace_back(text, caretPosition, selectionStart, selectionEnd);
        
        // Restore next state
        const auto& state = redoStack.back();
        text = state.text;
        caretPosition = state.caretPosition;
        selectionStart = state.selectionStart;
        selectionEnd = state.selectionEnd;
        hasSelection = (selectionStart != selectionEnd);
        
        redoStack.pop_back();
        
        // Reformat text
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
        
        UpdateScrollOffset();
        
        if (onTextChanged) onTextChanged(text);
    }
    
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    
    // ===== STYLING =====
    void SetStyle(const TextInputStyle& inputStyle) {
        style = inputStyle;
    }
    
    const TextInputStyle& GetStyle() const { return style; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Update caret blinking
        UpdateCaretBlink();
        
        // Get colors based on state
        Color backgroundColor = GetBackgroundColor();
        Color borderColor = GetBorderColor();
        Color textColor = GetTextColor();
        
        Rect2D bounds = GetBounds();
        
        // Draw background
        SetFillColor(backgroundColor);
        if (style.borderRadius > 0) {
            // Would need rounded rectangle implementation
            DrawRect(bounds);
        } else {
            DrawRect(bounds);
        }
        
        // Draw border
        SetStrokeColor(borderColor);
        SetStrokeWidth(style.borderWidth);
        DrawRect(bounds);
        
        // Draw shadow if enabled
        if (style.showShadow) {
            DrawShadow(bounds);
        }
        
        // Get text area (excluding padding)
        Rect2D textArea = GetTextArea();
        
        // Set clipping for text area
        SetClipRect(textArea);
        
        // Draw text content
        if (!text.empty()) {
            RenderText(textArea, textColor);
        } else if (!placeholderText.empty() && !IsFocused()) {
            RenderPlaceholder(textArea);
        }
        
        // Draw selection
        if (HasSelection() && IsFocused()) {
            RenderSelection(textArea);
        }
        
        // Draw caret
        if (IsFocused() && isCaretVisible && !HasSelection()) {
            RenderCaret(textArea);
        }
        
        // Clear clipping
        ClearClipRect();
        
        // Draw validation feedback
        if (showValidationState && lastValidationResult.state != ValidationState::NoValidation) {
            RenderValidationFeedback(bounds);
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
                
            case UCEventType::TextInput:
                HandleTextInput(event);
                break;
                
            case UCEventType::FocusGained:
                HandleFocusGained(event);
                break;
                
            case UCEventType::FocusLost:
                HandleFocusLost(event);
                break;
        }
    }
    
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
    
    void SaveState() {
        undoStack.emplace_back(text, caretPosition, selectionStart, selectionEnd);
        
        // Limit undo stack size
        if (static_cast<int>(undoStack.size()) > maxUndoStates) {
            undoStack.erase(undoStack.begin());
        }
        
        // Clear redo stack when new state is saved
        redoStack.clear();
    }
    
    void UpdateScrollOffset() {
        // Simple horizontal scrolling for single-line inputs
        if (inputType == TextInputType::Multiline) return;
        
        Rect2D textArea = GetTextArea();
        float caretX = GetCaretXPosition();
        
        // Scroll if caret is outside visible area
        if (caretX < textArea.x + scrollOffset) {
            scrollOffset = caretX - textArea.x;
        } else if (caretX > textArea.x + textArea.width + scrollOffset) {
            scrollOffset = caretX - textArea.x - textArea.width;
        }
        
        scrollOffset = std::max(0.0f, scrollOffset);
    }
    
    float GetCaretXPosition() {
        // Simplified - would need proper text measurement
        return style.paddingLeft + caretPosition * 8.0f; // Assume 8px per character
    }
    
    Rect2D GetTextArea() const {
        Rect2D bounds = GetBounds();
        return Rect2D(
            bounds.x + style.paddingLeft,
            bounds.y + style.paddingTop,
            bounds.width - style.paddingLeft - style.paddingRight,
            bounds.height - style.paddingTop - style.paddingBottom
        );
    }
    
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
    
    void RenderText(const Rect2D& area, const Color& color) {
        std::string renderText = passwordMode ? std::string(text.length(), '*') : displayText;
        
        // Set text style
        TextStyle textStyle;
        textStyle.fontFamily = style.fontFamily;
        textStyle.fontSize = style.fontSize;
        textStyle.fontWeight = style.fontWeight;
        textStyle.textColor = color;
        textStyle.alignment = style.textAlignment;
        SetTextStyle(textStyle);
        
        Point2D textPos(area.x - scrollOffset, area.y);
        
        if (inputType == TextInputType::Multiline) {
            RenderMultilineText(area, renderText, textPos);
        } else {
            DrawText(renderText, textPos);
        }
    }
    
    void RenderPlaceholder(const Rect2D& area) {
        TextStyle placeholderStyle;
        placeholderStyle.fontFamily = style.fontFamily;
        placeholderStyle.fontSize = style.fontSize;
        placeholderStyle.fontWeight = style.fontWeight;
        placeholderStyle.textColor = style.placeholderColor;
        placeholderStyle.alignment = style.textAlignment;
        SetTextStyle(placeholderStyle);
        
        DrawText(placeholderText, Point2D(area.x, area.y));
    }
    
    void RenderSelection(const Rect2D& area) {
        std::string displayText = GetDisplayText();
        float charWidth = GetAverageCharacterWidth();
        
        float selStartX = area.x + selectionStart * charWidth;
        float selEndX = area.x + selectionEnd * charWidth;
        
        Rect2D selectionRect(selStartX, area.y, selEndX - selStartX, area.height);
        SetFillColor(style.selectionColor);
        DrawRect(selectionRect);
    }
    
    void RenderCaret(const Rect2D& area) {
        std::string displayText = GetDisplayText();
        float charWidth = GetAverageCharacterWidth();
        
        float caretX = area.x + caretPosition * charWidth;
        
        SetStrokeColor(style.caretColor);
        SetStrokeWidth(style.caretWidth);
        DrawLine(Point2D(caretX, area.y + 2), Point2D(caretX, area.y + area.height - 2));
    }
    
    void RenderMultilineText(const Rect2D& area, const std::string& displayText, const Point2D& startPos) {
        // Split text into lines and render each line
        std::vector<std::string> lines = SplitTextIntoLines(displayText, area.width);
        
        float lineHeight = style.fontSize * 1.2f;
        float currentY = startPos.y;
        
        for (const auto& line : lines) {
            if (currentY > area.y + area.height) break;  // Stop if we're outside the area
            
            DrawText(line, Point2D(startPos.x, currentY));
            currentY += lineHeight;
        }
    }
    
    void RenderValidationFeedback(const Rect2D& bounds) {
        Color feedbackColor;
        
        switch (lastValidationResult.state) {
            case ValidationState::Valid:
                feedbackColor = style.validBorderColor;
                break;
            case ValidationState::Invalid:
                feedbackColor = style.invalidBorderColor;
                break;
            case ValidationState::Warning:
                feedbackColor = style.warningBorderColor;
                break;
            default:
                return;
        }
        
        // Draw validation border
        SetStrokeColor(feedbackColor);
        SetStrokeWidth(2.0f);
        DrawRect(bounds);
        
        // Draw validation icon (simplified)
        if (lastValidationResult.state == ValidationState::Valid) {
            // Draw checkmark
            Point2D iconPos(bounds.x + bounds.width - 20, bounds.y + bounds.height / 2 - 6);
            SetStrokeColor(style.validBorderColor);
            SetStrokeWidth(2.0f);
            DrawLine(iconPos, Point2D(iconPos.x + 4, iconPos.y + 4));
            DrawLine(Point2D(iconPos.x + 4, iconPos.y + 4), Point2D(iconPos.x + 12, iconPos.y - 4));
        } else if (lastValidationResult.state == ValidationState::Invalid) {
            // Draw X
            Point2D iconPos(bounds.x + bounds.width - 20, bounds.y + bounds.height / 2 - 6);
            SetStrokeColor(style.invalidBorderColor);
            SetStrokeWidth(2.0f);
            DrawLine(iconPos, Point2D(iconPos.x + 12, iconPos.y + 12));
            DrawLine(Point2D(iconPos.x, iconPos.y + 12), Point2D(iconPos.x + 12, iconPos.y));
        }
    }
    
    void DrawShadow(const Rect2D& bounds) {
        if (!style.showShadow) return;
        
        Rect2D shadowRect(
            bounds.x + style.shadowOffset.x,
            bounds.y + style.shadowOffset.y,
            bounds.width,
            bounds.height
        );
        
        SetFillColor(style.shadowColor);
        DrawRect(shadowRect);
    }
    
    float GetAverageCharacterWidth() {
        // Simplified character width calculation
        return style.fontSize * 0.6f; // Rough approximation
    }
    
    std::vector<std::string> SplitTextIntoLines(const std::string& text, float maxWidth) {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (GetTextWidth(line) <= maxWidth) {
                lines.push_back(line);
            } else {
                // Word wrap logic
                std::vector<std::string> wrappedLines = WrapLine(line, maxWidth);
                lines.insert(lines.end(), wrappedLines.begin(), wrappedLines.end());
            }
        }
        
        return lines;
    }
    
    std::vector<std::string> WrapLine(const std::string& line, float maxWidth) {
        std::vector<std::string> wrappedLines;
        std::istringstream words(line);
        std::string word;
        std::string currentLine;
        
        while (words >> word) {
            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
            
            if (GetTextWidth(testLine) <= maxWidth) {
                currentLine = testLine;
            } else {
                if (!currentLine.empty()) {
                    wrappedLines.push_back(currentLine);
                    currentLine = word;
                } else {
                    // Single word is too long, break it
                    wrappedLines.push_back(word);
                }
            }
        }
        
        if (!currentLine.empty()) {
            wrappedLines.push_back(currentLine);
        }
        
        return wrappedLines;
    }
    
    size_t GetTextPositionFromPoint(const Point2D& point) {
        Rect2D textArea = GetTextArea();
        
        if (!textArea.Contains(point)) {
            return point.x < textArea.x ? 0 : text.length();
        }
        
        float relativeX = point.x - textArea.x + scrollOffset;
        float charWidth = GetAverageCharacterWidth();
        
        size_t position = static_cast<size_t>(relativeX / charWidth);
        return std::min(position, text.length());
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        SetFocus(true);
        
        Point2D clickPoint(event.x, event.y);
        size_t clickPosition = GetTextPositionFromPoint(clickPoint);
        
        if (event.shift && hasSelection) {
            // Extend selection
            SetSelection(selectionStart, clickPosition);
        } else {
            // Start new selection
            SetCaretPosition(clickPosition);
            isDragging = true;
            dragStartPosition = clickPoint;
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (!isDragging) return;
        
        Point2D currentPoint(event.x, event.y);
        size_t currentPosition = GetTextPositionFromPoint(currentPoint);
        size_t startPosition = GetTextPositionFromPoint(dragStartPosition);
        
        SetSelection(startPosition, currentPosition);
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDragging = false;
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (readOnly) return;
        
        switch (event.virtualKey) {
            case UCKeys::Return:
                if (inputType == TextInputType::Multiline) {
                    InsertText("\n");
                } else {
                    if (onEnterPressed) onEnterPressed();
                }
                break;
                
            case UCKeys::Escape:
                if (onEscapePressed) onEscapePressed();
                break;
                
            case UCKeys::Backspace:
                if (hasSelection) {
                    DeleteSelection();
                } else if (caretPosition > 0) {
                    SaveState();
                    text.erase(caretPosition - 1, 1);
                    caretPosition--;
                    UpdateDisplayText();
                    if (onTextChanged) onTextChanged(text);
                }
                break;
                
            case UCKeys::Delete:
                if (hasSelection) {
                    DeleteSelection();
                } else if (caretPosition < text.length()) {
                    SaveState();
                    text.erase(caretPosition, 1);
                    UpdateDisplayText();
                    if (onTextChanged) onTextChanged(text);
                }
                break;
                
            case UCKeys::Left:
                if (event.shift) {
                    // Extend selection
                    if (!hasSelection) {
                        selectionStart = caretPosition;
                    }
                    if (caretPosition > 0) caretPosition--;
                    selectionEnd = caretPosition;
                    hasSelection = (selectionStart != selectionEnd);
                } else {
                    if (hasSelection) {
                        caretPosition = selectionStart;
                        ClearSelection();
                    } else if (caretPosition > 0) {
                        caretPosition--;
                    }
                }
                UpdateScrollOffset();
                break;
                
            case UCKeys::Right:
                if (event.shift) {
                    // Extend selection
                    if (!hasSelection) {
                        selectionStart = caretPosition;
                    }
                    if (caretPosition < text.length()) caretPosition++;
                    selectionEnd = caretPosition;
                    hasSelection = (selectionStart != selectionEnd);
                } else {
                    if (hasSelection) {
                        caretPosition = selectionEnd;
                        ClearSelection();
                    } else if (caretPosition < text.length()) {
                        caretPosition++;
                    }
                }
                UpdateScrollOffset();
                break;
                
            case UCKeys::Home:
                if (event.shift) {
                    if (!hasSelection) selectionStart = caretPosition;
                    caretPosition = 0;
                    selectionEnd = caretPosition;
                    hasSelection = true;
                } else {
                    caretPosition = 0;
                    ClearSelection();
                }
                UpdateScrollOffset();
                break;
                
            case UCKeys::End:
                if (event.shift) {
                    if (!hasSelection) selectionStart = caretPosition;
                    caretPosition = text.length();
                    selectionEnd = caretPosition;
                    hasSelection = true;
                } else {
                    caretPosition = text.length();
                    ClearSelection();
                }
                UpdateScrollOffset();
                break;
                
            case UCKeys::A:
                if (event.ctrl) {
                    SelectAll();
                }
                break;
                
            case UCKeys::X:
                if (event.ctrl && hasSelection) {
                    CopyToClipboard(GetSelectedText());
                    DeleteSelection();
                }
                break;
                
            case UCKeys::C:
                if (event.ctrl && hasSelection) {
                    CopyToClipboard(GetSelectedText());
                }
                break;
                
            case UCKeys::V:
                if (event.ctrl) {
                    std::string clipboardText = GetFromClipboard();
                    if (!clipboardText.empty()) {
                        InsertText(clipboardText);
                    }
                }
                break;
                
            case UCKeys::Z:
                if (event.ctrl) {
                    if (event.shift) {
                        Redo();
                    } else {
                        Undo();
                    }
                }
                break;
        }
    }
    
    void HandleTextInput(const UCEvent& event) {
        if (readOnly || event.text.empty()) return;
        
        InsertText(event.text);
    }
    
    void HandleFocusGained(const UCEvent& event) {
        isCaretVisible = true;
        caretBlinkTimer = 0.0f;
        
        if (onFocusGained) onFocusGained();
    }
    
    void HandleFocusLost(const UCEvent& event) {
        if (validateOnBlur) {
            Validate();
        }
        
        ClearSelection();
        
        if (onFocusLost) onFocusLost();
    }
    
    void InsertText(const std::string& insertText) {
        if (readOnly) return;
        
        SaveState();
        
        // Check max length
        if (maxLength > 0 && static_cast<int>(text.length() + insertText.length()) > maxLength) {
            return;
        }
        
        // Delete selection if any
        if (hasSelection) {
            text.erase(selectionStart, selectionEnd - selectionStart);
            caretPosition = selectionStart;
            ClearSelection();
        }
        
        // Insert new text
        text.insert(caretPosition, insertText);
        caretPosition += insertText.length();
        
        UpdateDisplayText();
        UpdateScrollOffset();
        
        if (validateOnChange) {
            Validate();
        }
        
        if (onTextChanged) onTextChanged(text);
    }
    
    void DeleteSelection() {
        if (!hasSelection) return;
        
        SaveState();
        
        text.erase(selectionStart, selectionEnd - selectionStart);
        caretPosition = selectionStart;
        ClearSelection();
        
        UpdateDisplayText();
        UpdateScrollOffset();
        
        if (validateOnChange) {
            Validate();
        }
        
        if (onTextChanged) onTextChanged(text);
    }
    
    void UpdateDisplayText() {
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
    }
    
    // Placeholder clipboard functions - would need platform-specific implementation
    void CopyToClipboard(const std::string& text) {
        // Platform-specific clipboard implementation needed
    }
    
    std::string GetFromClipboard() {
        // Platform-specific clipboard implementation needed
        return "";
    }
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

inline std::shared_ptr<UltraCanvasTextInput> CreateTextArea(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    auto input = CreateTextInput(identifier, id, x, y, w, h);
    input->SetInputType(TextInputType::Multiline);
    return input;
}

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
