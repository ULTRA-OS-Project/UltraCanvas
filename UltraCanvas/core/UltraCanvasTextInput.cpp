// UltraCanvasTextInput.h
// Advanced text input component with validation, formatting, and feedback systems
// Version: 1.1.0
// Last Modified: 2025-01-06
// Author: UltraCanvas Framework

#include "UltraCanvasTextInput.h"
#include <string>
#include <vector>
#include <functional>
#include <regex>
#include <memory>
#include <chrono>

namespace UltraCanvas {

    UltraCanvasTextInput::UltraCanvasTextInput(const std::string &id, long uid, long x, long y, long w, long h)
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

    void UltraCanvasTextInput::SetText(const std::string &newText) {
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

    void UltraCanvasTextInput::SetInputType(TextInputType type) {
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

    void UltraCanvasTextInput::SetReadOnly(bool readonly) {
        readOnly = readonly;
        if (readonly) {
            ClearSelection();
        }
    }

    void UltraCanvasTextInput::SetMaxLength(int length) {
        maxLength = length;
        if (maxLength > 0 && static_cast<int>(text.length()) > maxLength) {
            SetText(text.substr(0, maxLength));
        }
    }

    ValidationResult UltraCanvasTextInput::Validate() {
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

    void UltraCanvasTextInput::SetFormatter(const TextFormatter &textFormatter) {
        formatter = textFormatter;
        if (!placeholderText.empty() && !formatter.placeholder.empty()) {
            placeholderText = formatter.placeholder;
        }

        // Reformat current text
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
    }

    void UltraCanvasTextInput::SetSelection(size_t start, size_t end) {
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

    std::string UltraCanvasTextInput::GetSelectedText() const {
        if (!hasSelection) return "";
        return text.substr(selectionStart, selectionEnd - selectionStart);
    }

    void UltraCanvasTextInput::SetCaretPosition(size_t position) {
        caretPosition = std::min(position, text.length());
        ClearSelection();
        UpdateScrollOffset();
    }

    void UltraCanvasTextInput::Undo() {
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

    void UltraCanvasTextInput::Redo() {
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

    void UltraCanvasTextInput::Render() {
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

    bool UltraCanvasTextInput::OnEvent(const UCEvent &event) {
        if (!IsActive() || !IsVisible()) return false;;

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

            case UCEventType::FocusGained:
                HandleFocusGained(event);
                break;

            case UCEventType::FocusLost:
                HandleFocusLost(event);
                break;
        }
        return false;
    }

    void UltraCanvasTextInput::SaveState() {
        undoStack.emplace_back(text, caretPosition, selectionStart, selectionEnd);

        // Limit undo stack size
        if (static_cast<int>(undoStack.size()) > maxUndoStates) {
            undoStack.erase(undoStack.begin());
        }

        // Clear redo stack when new state is saved
        redoStack.clear();
    }

    void UltraCanvasTextInput::UpdateScrollOffset() {
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

    Rect2D UltraCanvasTextInput::GetTextArea() const {
        Rect2D bounds = GetBounds();
        return Rect2D(
                bounds.x + style.paddingLeft,
                bounds.y + style.paddingTop,
                bounds.width - style.paddingLeft - style.paddingRight,
                bounds.height - style.paddingTop - style.paddingBottom
        );
    }

    void UltraCanvasTextInput::RenderText(const Rect2D &area, const Color &color) {
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

    void UltraCanvasTextInput::RenderPlaceholder(const Rect2D &area) {
        TextStyle placeholderStyle;
        placeholderStyle.fontFamily = style.fontFamily;
        placeholderStyle.fontSize = style.fontSize;
        placeholderStyle.fontWeight = style.fontWeight;
        placeholderStyle.textColor = style.placeholderColor;
        placeholderStyle.alignment = style.textAlignment;
        SetTextStyle(placeholderStyle);

        DrawText(placeholderText, Point2D(area.x, area.y));
    }

    void UltraCanvasTextInput::RenderSelection(const Rect2D &area) {
        std::string displayText = GetDisplayText();
        float charWidth = GetAverageCharacterWidth();

        float selStartX = area.x + selectionStart * charWidth;
        float selEndX = area.x + selectionEnd * charWidth;

        Rect2D selectionRect(selStartX, area.y, selEndX - selStartX, area.height);
        SetFillColor(style.selectionColor);
        DrawRect(selectionRect);
    }

    void UltraCanvasTextInput::RenderCaret(const Rect2D &area) {
        std::string displayText = GetDisplayText();
        float charWidth = GetAverageCharacterWidth();

        float caretX = area.x + caretPosition * charWidth;

        SetStrokeColor(style.caretColor);
        SetStrokeWidth(style.caretWidth);
        DrawLine(Point2D(caretX, area.y + 2), Point2D(caretX, area.y + area.height - 2));
    }

    void UltraCanvasTextInput::RenderMultilineText(const Rect2D &area, const std::string &displayText,
                                                   const Point2D &startPos) {
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

    void UltraCanvasTextInput::RenderValidationFeedback(const Rect2D &bounds) const {
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

    void UltraCanvasTextInput::DrawShadow(const Rect2D &bounds) {
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

    std::vector<std::string> UltraCanvasTextInput::SplitTextIntoLines(const std::string &text, float maxWidth) {
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

    std::vector<std::string> UltraCanvasTextInput::WrapLine(const std::string &line, float maxWidth) {
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

    size_t UltraCanvasTextInput::GetTextPositionFromPoint(const Point2D &point) {
        Rect2D textArea = GetTextArea();

        if (!textArea.Contains(point)) {
            return point.x < textArea.x ? 0 : text.length();
        }

        float relativeX = point.x - textArea.x + scrollOffset;
        float charWidth = GetAverageCharacterWidth();

        size_t position = static_cast<size_t>(relativeX / charWidth);
        return std::min(position, text.length());
    }

    void UltraCanvasTextInput::HandleMouseDown(const UCEvent &event) {
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

    void UltraCanvasTextInput::HandleMouseMove(const UCEvent &event) {
        if (!isDragging) return;

        Point2D currentPoint(event.x, event.y);
        size_t currentPosition = GetTextPositionFromPoint(currentPoint);
        size_t startPosition = GetTextPositionFromPoint(dragStartPosition);

        SetSelection(startPosition, currentPosition);
    }

    void UltraCanvasTextInput::HandleMouseUp(const UCEvent &event) {
        isDragging = false;
    }

    void UltraCanvasTextInput::HandleKeyDown(const UCEvent &event) {
        if (readOnly) return;

        // Handle printable characters from KeyDown events
        // UCEvent already has 'character' and 'text' fields populated by X11
        if (event.character != 0 && event.character >= 32 && event.character < 127) {
            // Handle regular printable character input
            SaveState();

            if (hasSelection) {
                DeleteSelection();
            }

            std::string charStr(1, event.character);
            InsertText(charStr);
//            InvalidateLayout();
            return; // Exit early for character input
        }

        // Handle special keys
        switch (event.virtualKey) {
            case UCKeys::Return:
                if (inputType == TextInputType::Multiline) {
                    SaveState();
                    if (hasSelection) DeleteSelection();
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
                    SaveState();
                    DeleteSelection();
                } else if (caretPosition > 0) {
                    SaveState();
                    text.erase(caretPosition - 1, 1);
                    caretPosition--;
                    UpdateDisplayText();
                }
                UpdateScrollOffset();
                break;

            case UCKeys::Delete:
                if (hasSelection) {
                    SaveState();
                    DeleteSelection();
                } else if (caretPosition < text.length()) {
                    SaveState();
                    text.erase(caretPosition, 1);
                    UpdateDisplayText();
                }
                UpdateScrollOffset();
                break;

            case UCKeys::Left:
                if (event.shift) {
                    if (!hasSelection) selectionStart = caretPosition;
                    if (caretPosition > 0) caretPosition--;
                    selectionEnd = caretPosition;
                    hasSelection = true;
                } else {
                    if (caretPosition > 0) caretPosition--;
                    ClearSelection();
                }
                UpdateScrollOffset();
                break;

            case UCKeys::Right:
                if (event.shift) {
                    if (!hasSelection) selectionStart = caretPosition;
                    if (caretPosition < text.length()) caretPosition++;
                    selectionEnd = caretPosition;
                    hasSelection = true;
                } else {
                    if (caretPosition < text.length()) caretPosition++;
                    ClearSelection();
                }
                UpdateScrollOffset();
                break;

            case UCKeys::Up:
                if (inputType == TextInputType::Multiline) {
                    // Handle multiline navigation
                    if (event.shift) {
                        if (!hasSelection) selectionStart = caretPosition;
                        // Move up one line
                        // ... multiline logic here
                        hasSelection = true;
                    } else {
                        // Move up one line without selection
                        // ... multiline logic here
                        ClearSelection();
                    }
                }
                break;

            case UCKeys::Down:
                if (inputType == TextInputType::Multiline) {
                    // Handle multiline navigation
                    if (event.shift) {
                        if (!hasSelection) selectionStart = caretPosition;
                        // Move down one line
                        // ... multiline logic here
                        hasSelection = true;
                    } else {
                        // Move down one line without selection
                        // ... multiline logic here
                        ClearSelection();
                    }
                }
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
                    SaveState();
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
                        SaveState();
                        if (hasSelection) DeleteSelection();
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

            case UCKeys::Y:
                if (event.ctrl) {
                    Redo();
                }
                break;

            case UCKeys::Tab:
                if (inputType == TextInputType::Multiline) {
                    SaveState();
                    if (hasSelection) DeleteSelection();
                    InsertText("\t");
                }
                // Otherwise let Tab navigate to next control
                break;

            case UCKeys::Space:
                // Handle space as a regular character
                SaveState();
                if (hasSelection) DeleteSelection();
                InsertText(" ");
                break;

            default:
                // Check if it's a printable character using event.text field
                if (!event.text.empty()) {
                    // Filter to only allow printable characters
                    std::string filteredText;
                    for (char c : event.text) {
                        if (c >= 32 && c < 127) { // Printable ASCII
                            filteredText += c;
                        }
                    }

                    if (!filteredText.empty()) {
                        SaveState();
                        if (hasSelection) DeleteSelection();
                        InsertText(filteredText);
                    }
                }
                break;
        }

        // Update layout after any text changes
        //InvalidateLayout();
    }

    void UltraCanvasTextInput::HandleKeyUp(const UCEvent &event) {
        // KeyUp events mainly for modifier key tracking
        // Most text input logic is handled in KeyDown

        // Could be used for key repeat stopping, modifier state tracking, etc.
        // For now, we don't need specific KeyUp handling for text input
    }

    void UltraCanvasTextInput::HandleFocusGained(const UCEvent &event) {
        SetFocus(true);
        isCaretVisible = true;
        caretBlinkTimer = 0.0f;
//        InvalidateLayout();

        if (onFocusGained) onFocusGained();
    }

    void UltraCanvasTextInput::HandleFocusLost(const UCEvent &event) {
        SetFocus(false);
        isCaretVisible = false;
        isDragging = false;
//        InvalidateLayout();

        if (onFocusLost) onFocusLost();
    }

    void UltraCanvasTextInput::InsertText(const std::string &insertText) {
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

    void UltraCanvasTextInput::DeleteSelection() {
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

    void UltraCanvasTextInput::UpdateDisplayText() {
        displayText = formatter.formatFunction ? formatter.formatFunction(text) : text;
    }

    void UltraCanvasTextInput::CopyToClipboard(const std::string &text) {
        // Platform-specific clipboard implementation needed
    }

    std::string UltraCanvasTextInput::GetFromClipboard() {
        // Platform-specific clipboard implementation needed
        return "";
    }
}