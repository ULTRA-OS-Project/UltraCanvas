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
            : UltraCanvasUIElement(id, uid, x, y, w, h)
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
            , lastMeasuredSize(0.0f)
            , maxUndoStates(50)
            , isDragging(false)
            , autoCompleteMode(AutoComplete::Off)
            , showAutoComplete(false) {
        textWidthCache.clear();
        lastMeasuredFont.clear();
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
        IRenderContext *ctx = GetRenderContext();
        if (!IsVisible() && !ctx) return;

        ctx->PushState();

        // Update caret blinking
        UpdateCaretBlink();

        // Get colors based on state
        Color backgroundColor = GetBackgroundColor();
        Color borderColor = GetBorderColor();
        Color textColor = GetTextColor();

        Rect2Di bounds = GetBounds();

        // Draw background
        ctx->DrawFilledRectangle(bounds, backgroundColor, style.borderWidth, style.borderColor);

        // Get text area (excluding padding)
        Rect2Df textArea = GetTextArea();

        // Set clipping for text area ONLY
        ctx->SetClipRect(textArea);

        // Draw text content
        if (!text.empty()) {
            RenderText(textArea, textColor, ctx);
        } else if (!placeholderText.empty() && !IsFocused()) {
            RenderPlaceholder(textArea, ctx);
        }

        // Draw selection
        if (HasSelection() && IsFocused()) {
            RenderSelection(textArea, ctx);
        }

        // CRITICAL: Clear clipping BEFORE drawing caret
        ctx->ClearClipRect();

        // Draw caret WITHOUT clipping so it's always visible
        if (IsFocused() && isCaretVisible && !HasSelection()) {
            RenderCaret(textArea, ctx);
        }

        // Draw validation feedback
        if (showValidationState && lastValidationResult.state != ValidationState::NoValidation) {
            RenderValidationFeedback(bounds, ctx);
        }

        ctx->PopState();
    }

    bool UltraCanvasTextInput::OnEvent(const UCEvent &event) {
        if (!IsActive() || !IsVisible()) return false;;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
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
        auto ctx = GetRenderContext();
        if (!ctx) return;

        Rect2Df textArea = GetTextArea();
        float caretX = GetCaretXPosition();

        // Add some padding around caret for better UX
        float caretPadding = 10.0f;

        // Horizontal scrolling for both single-line and multiline
        if (caretX < scrollOffset + caretPadding) {
            scrollOffset = std::max(0.0f, caretX - caretPadding);
        } else if (caretX > scrollOffset + textArea.width - caretPadding) {
            scrollOffset = caretX - textArea.width + caretPadding;
        }

        // Ensure we don't scroll past the beginning
        scrollOffset = std::max(0.0f, scrollOffset);

        // For multiline, check current line width
        if (inputType == TextInputType::Multiline) {
            std::string displayText = GetDisplayText();
            size_t lineStart = caretPosition;
            while (lineStart > 0 && displayText[lineStart - 1] != '\n') {
                lineStart--;
            }

            size_t lineEnd = displayText.find('\n', lineStart);
            if (lineEnd == std::string::npos) {
                lineEnd = displayText.length();
            }

            std::string currentLine = displayText.substr(lineStart, lineEnd - lineStart);

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            float lineWidth = ctx->GetTextWidth(currentLine);
            float maxScroll = std::max(0.0f, lineWidth - textArea.width + style.paddingRight);
            scrollOffset = std::min(scrollOffset, maxScroll);
        } else {
            // Single line: check against total text width
            std::string displayText = GetDisplayText();

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            float totalTextWidth = ctx->GetTextWidth(displayText);
            float maxScroll = std::max(0.0f, totalTextWidth - textArea.width + style.paddingRight);
            scrollOffset = std::min(scrollOffset, maxScroll);
        }
        RequestRedraw();
    }

    Rect2Df UltraCanvasTextInput::GetTextArea() const {
        Rect2Di bounds = GetBounds();
        if (showValidationState && (lastValidationResult.state == ValidationState::Valid || lastValidationResult.state != ValidationState::Invalid)) {
            return Rect2Df(
                    bounds.x + style.paddingLeft,
                    bounds.y + style.paddingTop,
                    bounds.width - style.paddingLeft - style.paddingRight - 20,
                    bounds.height - style.paddingTop - style.paddingBottom
            );
        } else {
            return Rect2Df(
                    bounds.x + style.paddingLeft,
                    bounds.y + style.paddingTop,
                    bounds.width - style.paddingLeft - style.paddingRight,
                    bounds.height - style.paddingTop - style.paddingBottom
            );
        }
    }

    void UltraCanvasTextInput::RenderText(const Rect2Df &area, const Color &color, IRenderContext* ctx) {
        std::string renderText = passwordMode ?
                                 std::string(text.length(), '*') : GetDisplayText();

        if (renderText.empty()) return;

        // Set text style
        TextStyle textStyle;
        ctx->SetFontStyle(style.fontStyle);
        textStyle.textColor = color;
        textStyle.alignment = style.textAlignment;
        ctx->SetTextStyle(textStyle);

        if (inputType == TextInputType::Multiline) {
            // Start at baseline position
            Point2Di textPos(area.x - scrollOffset, area.y + (style.fontStyle.fontSize * 0.8f));
            RenderMultilineText(area, renderText, textPos, ctx);
        } else {
            // Match the baseline calculation used in GetCaretYPosition
            float lineHeight = style.fontStyle.fontSize * 1.2f;
            float centeredY = area.y + (area.height - lineHeight) / 2.0f;
            //float baselineY = centeredY + (style.fontSize * 0.8f);
            float baselineY = centeredY;

            Point2Di textPos(area.x - scrollOffset, baselineY);
            ctx->DrawText(renderText, textPos);
        }
    }

    void UltraCanvasTextInput::RenderPlaceholder(const Rect2Df &area, IRenderContext* ctx) {
        TextStyle placeholderStyle;
        placeholderStyle.textColor = style.placeholderColor;
        placeholderStyle.alignment = style.textAlignment;
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextStyle(placeholderStyle);

        ctx->DrawText(placeholderText, Point2Di(area.x, area.y));
    }

    void UltraCanvasTextInput::RenderSelection(const Rect2Df &area, IRenderContext* ctx) {
        if (!HasSelection()) return;

        std::string displayText = GetDisplayText();

        // Set proper text style for measurement
        ctx->SetFontStyle(style.fontStyle);

        // Get text segments for accurate measurement
        std::string textBeforeSelection = displayText.substr(0, selectionStart);
        std::string selectedText = displayText.substr(selectionStart, selectionEnd - selectionStart);

        float selStartX = area.x + ctx->GetTextWidth(textBeforeSelection);
        float selWidth = ctx->GetTextWidth(selectedText);

        // Calculate proper selection height based on font metrics
        float ascender = style.fontStyle.fontSize * 0.8f;
        float descender = style.fontStyle.fontSize * 0.2f;
        float selectionHeight = ascender + descender;
        float selectionY = area.y + (area.height - selectionHeight) / 2.0f;

        // Ensure selection is within visible area
        float visibleStartX = std::max(selStartX, area.x);
        float visibleEndX = std::min(selStartX + selWidth, area.x + area.width);

        if (visibleEndX > visibleStartX) {
            Rect2Df selectionRect(visibleStartX, selectionY, visibleEndX - visibleStartX, selectionHeight);
            ctx->SetFillPaint(style.selectionColor);
            ctx->FillRectangle(selectionRect);
        }
    }

    void UltraCanvasTextInput::RenderCaret(const Rect2Df &area, IRenderContext* ctx) {
        if (!IsFocused() || !isCaretVisible) return;

        // FIXED: Calculate X position to match text rendering exactly
        Rect2Df textArea = GetTextArea();
        float caretX;

        if (text.empty() || caretPosition == 0) {
            // When no text, caret should be at text start position
            caretX = textArea.x - scrollOffset;
        } else {
            // Calculate width of text up to caret position
            std::string displayText = GetDisplayText();
            std::string textUpToCaret;

            if (inputType == TextInputType::Multiline) {
                // For multiline: find start of current line
                size_t lineStart = caretPosition;
                while (lineStart > 0 && displayText[lineStart - 1] != '\n') {
                    lineStart--;
                }
                textUpToCaret = displayText.substr(lineStart, caretPosition - lineStart);
            } else {
                // For single line: text up to caret
                textUpToCaret = displayText.substr(0, std::min(caretPosition, displayText.length()));
            }

            // Set text style for accurate measurement
            ctx->SetFontStyle(style.fontStyle);

            float textWidth = ctx->GetTextWidth(textUpToCaret);
            // FIXED: Match text rendering position exactly
            caretX = textArea.x + textWidth - scrollOffset;
        }

        float lineHeight = style.fontStyle.fontSize * 1.4f;
        // Total height should be about lineHeight for visibility
        float caretStartY = GetCaretYPosition();
        float caretEndY = caretStartY + lineHeight;

        // Only hide if completely outside control bounds
        Rect2Di controlBounds = GetBounds();
        if (caretX < controlBounds.x - 10 || caretX > controlBounds.x + controlBounds.width + 10) {
            return;
        }

        ctx->SetStrokePaint(style.caretColor);
        ctx->SetStrokeWidth(style.caretWidth);

        // Draw caret line with proper height and position
         ctx->DrawLine(
                Point2Di(caretX, caretStartY),
                Point2Di(caretX, caretEndY)
        );
    }

    void UltraCanvasTextInput::RenderMultilineText(const Rect2Df &area, const std::string &displayText, const Point2Di &startPos, IRenderContext* ctx) {
        // Split text into lines
        std::vector<std::string> lines;
        std::string currentLine;

        for (char c : displayText) {
            if (c == '\n') {
                lines.push_back(currentLine);
                currentLine.clear();
            } else {
                currentLine += c;
            }
        }
        lines.push_back(currentLine);

        float lineHeight = style.fontStyle.fontSize * 1.2f;
        float currentBaselineY = startPos.y; // startPos.y is baseline

        for (const auto& line : lines) {
            if (currentBaselineY > area.y + area.height + lineHeight) break;
            if (currentBaselineY >= area.y - lineHeight) {
                ctx->DrawText(line, Point2Di(startPos.x, currentBaselineY));
            }
            currentBaselineY += lineHeight;
        }
    }

    void UltraCanvasTextInput::RenderValidationFeedback(const Rect2Di &bounds, IRenderContext* ctx) const {
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
        ctx->SetStrokePaint(feedbackColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawRectangle(bounds);

        // Draw validation icon (simplified)
        if (lastValidationResult.state == ValidationState::Valid) {
            // Draw checkmark
            Point2Di iconPos(bounds.x + bounds.width - 20, bounds.y + bounds.height / 2);
            ctx->SetStrokePaint(style.validBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLine(iconPos, Point2Di(iconPos.x + 4, iconPos.y + 4));
            ctx->DrawLine(Point2Di(iconPos.x + 4, iconPos.y + 4), Point2Di(iconPos.x + 12, iconPos.y - 4));
        } else if (lastValidationResult.state == ValidationState::Invalid) {
            // Draw X
            Point2Di iconPos(bounds.x + bounds.width - 20, bounds.y + bounds.height / 2 - 6);
            ctx->SetStrokePaint(style.invalidBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLine(iconPos, Point2Di(iconPos.x + 12, iconPos.y + 12));
            ctx->DrawLine(Point2Di(iconPos.x, iconPos.y + 12), Point2Di(iconPos.x + 12, iconPos.y));
        }
    }

    void UltraCanvasTextInput::DrawShadow(const Rect2Di &bounds, IRenderContext* ctx) {
        if (!style.showShadow) return;

        Rect2Di shadowRect(
                bounds.x + style.shadowOffset.x,
                bounds.y + style.shadowOffset.y,
                bounds.width,
                bounds.height
        );

        ctx->SetStrokePaint(style.shadowColor);
        ctx->DrawRectangle(shadowRect);
    }

    std::vector<std::string> UltraCanvasTextInput::SplitTextIntoLines(const std::string &text, float maxWidth) {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        auto ctx = GetRenderContext();
        while (std::getline(stream, line)) {
            if (ctx->GetTextWidth(line) <= maxWidth) {
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
        auto ctx = GetRenderContext();

        while (words >> word) {
            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;

            if (ctx->GetTextWidth(testLine) <= maxWidth) {
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

    size_t UltraCanvasTextInput::GetTextPositionFromPoint(const Point2Di& point) {
        IRenderContext *ctx = GetRenderContext();

        Rect2Df textArea = GetTextArea();

        if (inputType == TextInputType::Multiline) {
            // Calculate which line was clicked
            float lineHeight = style.fontStyle.fontSize * 1.2f;
            int clickedLine = static_cast<int>((point.y - textArea.y) / lineHeight);
            clickedLine = std::max(0, clickedLine);

            // Find the start position of the clicked line
            std::string displayText = GetDisplayText();
            size_t lineStartPos = 0;
            int currentLine = 0;

            for (size_t i = 0; i < displayText.length() && currentLine < clickedLine; i++) {
                if (displayText[i] == '\n') {
                    currentLine++;
                    lineStartPos = i + 1;
                }
            }

            // Find the end of the clicked line
            size_t lineEndPos = displayText.find('\n', lineStartPos);
            if (lineEndPos == std::string::npos) {
                lineEndPos = displayText.length();
            }

            // Get the text of the clicked line
            std::string lineText = displayText.substr(lineStartPos, lineEndPos - lineStartPos);

            // CRITICAL: account for scroll offset
            float relativeX = point.x - textArea.x + scrollOffset;

            if (relativeX <= 0) return lineStartPos;

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            // Binary search within the line
            size_t left = 0, right = lineText.length();

            while (left < right) {
                size_t mid = (left + right) / 2;
                std::string textToMid = lineText.substr(0, mid);
                float widthToMid = ctx->GetTextWidth(textToMid);

                if (widthToMid < relativeX) {
                    left = mid + 1;
                } else {
                    right = mid;
                }
            }

            return lineStartPos + std::min(left, lineText.length());
        } else {
            // Single line logic
            if (point.y < textArea.y || point.y > textArea.y + textArea.height) {
                return text.empty() ? 0 : text.length();
            }

            // CRITICAL: account for scroll offset
            float relativeX = point.x - textArea.x + scrollOffset;

            if (relativeX <= 0) return 0;

            std::string displayText = GetDisplayText();

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            // Binary search for position
            size_t left = 0, right = displayText.length();

            while (left < right) {
                size_t mid = (left + right) / 2;
                std::string textToMid = displayText.substr(0, mid);
                float widthToMid = ctx->GetTextWidth(textToMid);

                if (widthToMid < relativeX) {
                    left = mid + 1;
                } else {
                    right = mid;
                }
            }

            return std::min(left, displayText.length());
        }
    }

    bool UltraCanvasTextInput::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.x, event.y)) return false;

        SetFocus(true);

        Point2Di clickPoint(event.x, event.y);
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
        return true;
    }

    bool UltraCanvasTextInput::HandleMouseMove(const UCEvent &event) {
        if (!isDragging) return false;

        Point2Di currentPoint(event.x, event.y);
        size_t currentPosition = GetTextPositionFromPoint(currentPoint);
        size_t startPosition = GetTextPositionFromPoint(dragStartPosition);

        SetSelection(startPosition, currentPosition);
        return true;
    }

    void UltraCanvasTextInput::HandleMouseUp(const UCEvent &event) {
        if (isDragging) {
            isDragging = false;
        }
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
        RequestRedraw();
    }

    void UltraCanvasTextInput::CopyToClipboard(const std::string &text) {
        // Platform-specific clipboard implementation needed
    }

    std::string UltraCanvasTextInput::GetFromClipboard() {
        // Platform-specific clipboard implementation needed
        return "";
    }

    int UltraCanvasTextInput::GetCaretLineNumber() const {
        if (inputType != TextInputType::Multiline) return 0;

        std::string displayText = GetDisplayText();
        int lineNumber = 0;

        for (size_t i = 0; i < caretPosition && i < displayText.length(); i++) {
            if (displayText[i] == '\n') {
                lineNumber++;
            }
        }

        return lineNumber;
    }

    float UltraCanvasTextInput::GetLineYPosition(int lineNumber) const {
        Rect2Df textArea = GetTextArea();
        float lineHeight = style.fontStyle.fontSize * 1.2f;
        return textArea.y + (lineNumber * lineHeight);
    }

    float UltraCanvasTextInput::GetCaretXInLine() const {
        auto ctx = GetRenderContext();
        if (text.empty() || caretPosition == 0) {
            return style.paddingLeft;
        }

        std::string displayText = GetDisplayText();

        // Find start of current line
        size_t lineStart = caretPosition;
        while (lineStart > 0 && displayText[lineStart - 1] != '\n') {
            lineStart--;
        }

        // Get text from line start to caret
        std::string textInLine = displayText.substr(lineStart, caretPosition - lineStart);

        // Set text style for measurement
        ctx->SetFontStyle(style.fontStyle);

        float textWidth = ctx->GetTextWidth(textInLine);
        return style.paddingLeft + textWidth;
    }

    float UltraCanvasTextInput::GetCaretXPosition() {
        auto ctx = GetRenderContext();
        if (text.empty() || caretPosition == 0) {
            return style.paddingLeft;
        }

        std::string displayText = GetDisplayText();

        if (inputType == TextInputType::Multiline) {
            // For multiline: find start of current line
            size_t lineStart = caretPosition;
            while (lineStart > 0 && displayText[lineStart - 1] != '\n') {
                lineStart--;
            }

            // Get text from line start to caret
            std::string textInLine = displayText.substr(lineStart, caretPosition - lineStart);

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            float textWidth = ctx->GetTextWidth(textInLine);
            return style.paddingLeft + textWidth;
        } else {
            // For single line: measure text up to caret
            std::string textUpToCaret = displayText.substr(0, std::min(caretPosition, displayText.length()));

            // Set text style for measurement
            ctx->SetFontStyle(style.fontStyle);

            float textWidth = ctx->GetTextWidth(textUpToCaret);
            return style.paddingLeft + textWidth;
        }
    }

    float UltraCanvasTextInput::GetCaretYPosition() {
        Rect2Df textArea = GetTextArea();

        if (inputType == TextInputType::Multiline) {
            // Count line number where caret is
            std::string displayText = GetDisplayText();
            int lineNumber = 0;

            for (size_t i = 0; i < caretPosition && i < displayText.length(); i++) {
                if (displayText[i] == '\n') {
                    lineNumber++;
                }
            }

            float lineHeight = style.fontStyle.fontSize * 1.2f;
            // CRITICAL: Return baseline position, not top of line
            return textArea.y + (lineNumber * lineHeight) + (style.fontStyle.fontSize * 0.8f);
        } else {
            // Single line: match baseline positioning
            float lineHeight = style.fontStyle.fontSize * 1.2f;
            float centeredY = textArea.y + (textArea.height - lineHeight) / 2.0f;
            return centeredY;
        }
    }}