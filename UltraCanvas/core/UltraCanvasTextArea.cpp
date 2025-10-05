// core/UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting
// Version: 2.0.0
// Last Modified: 2024-12-20
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace UltraCanvas {

// Constructor
    UltraCanvasTextArea::UltraCanvasTextArea(const std::string& name, int id, int x, int y,
                                             int width, int height)
            : UltraCanvasUIElement(name, id, x, y, width, height),
              cursorPosition(0),
              selectionStart(-1),
              selectionEnd(-1),
              horizontalScrollOffset(0),
              verticalScrollOffset(0),
              firstVisibleLine(0),
              maxVisibleLines(0),
              cursorBlinkTime(0),
              cursorVisible(true),
              isReadOnly(false),
              wordWrap(false),
              highlightCurrentLine(false),
              isNeedRecalculateVisibleArea(true),
              currentLineIndex(0) {

        // Initialize style with defaults
        ApplyDefaultStyle();

        // Calculate visible lines
        CalculateVisibleArea();

        // Initialize syntax highlighter if needed
        if (style.highlightSyntax) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
    }

// Destructor
    UltraCanvasTextArea::~UltraCanvasTextArea() = default;

// Initialize default style
    void UltraCanvasTextArea::ApplyDefaultStyle() {
        style.fontStyle.fontFamily = "DejaVu Sans Mono";
        style.fontStyle.fontSize = 14;
        style.fontColor = {0, 0, 0, 255};
        style.lineHeight = 20;
        style.backgroundColor = {255, 255, 255, 255};
        style.borderColor = {200, 200, 200, 255};
        style.selectionColor = {51, 153, 255, 100};
        style.cursorColor = {0, 0, 0, 255};
        style.currentLineColor = {240, 240, 240, 255};
        style.lineNumbersColor = {128, 128, 128, 255};
        style.lineNumbersBackgroundColor = {248, 248, 248, 255};
        style.currentLineHighlightColor = {255, 255, 0, 30};
        style.scrollbarTrackColor = {128, 128, 128, 255};
        style.scrollbarColor = {200, 200, 200, 255};
        style.borderWidth = 1;
        style.padding = 5;
        style.showLineNumbers = false;
        style.lineNumbersWidth = 50;
        style.highlightSyntax = false;

        // Syntax highlighting colors
        style.tokenStyles.keywordStyle.color = {0, 0, 255, 255};
        style.tokenStyles.functionStyle.color = {128, 0, 128, 255};
        style.tokenStyles.stringStyle.color = {0, 128, 0, 255};
        style.tokenStyles.characterStyle.color = {0, 128, 0, 255};
        style.tokenStyles.commentStyle.color = {128, 128, 128, 255};
        style.tokenStyles.numberStyle.color = {255, 128, 0, 255};
        style.tokenStyles.identifierStyle.color = {0, 128, 128, 255};
        style.tokenStyles.operatorStyle.color = {128, 0, 0, 255};
        style.tokenStyles.constantStyle.color = {0, 0, 128, 255};
        style.tokenStyles.preprocessorStyle.color = {64, 128, 128, 255};
        style.tokenStyles.builtinStyle.color = {128, 0, 255, 255};
    }

// Render implementation
    void UltraCanvasTextArea::Render() {
        if (!IsVisible()) return;

        if (isNeedRecalculateVisibleArea) {
            CalculateVisibleArea();
        }

        auto ctx = GetRenderContext();
        ctx->PushState();
        // Draw background
        DrawBackground(ctx);

        // Draw line numbers if enabled
        if (style.showLineNumbers) {
            DrawLineNumbers(ctx);
        }

        // Draw text with syntax highlighting
        if (style.highlightSyntax && syntaxTokenizer) {
            DrawHighlightedText(ctx);
        } else {
            DrawPlainText(ctx);
        }

        // Draw selection
        if (HasSelection()) {
            DrawSelection(ctx);
        }

        // Draw cursor
        if (IsFocused() && cursorVisible && !isReadOnly) {
            DrawCursor(ctx);
        }

        // Draw border
        DrawBorder(ctx);

        // Draw scrollbars if needed
        DrawScrollbars(ctx);
        ctx->PopState();
    }

// Draw background
    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFillPaint(style.backgroundColor);
        context->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        // Draw current line highlight
        if (highlightCurrentLine && currentLineIndex >= firstVisibleLine &&
            currentLineIndex < firstVisibleLine + maxVisibleLines) {
            context->SetFillPaint(style.currentLineHighlightColor);
            int lineY = bounds.y + style.padding + (currentLineIndex - firstVisibleLine) * style.lineHeight;
            int highlightX = style.showLineNumbers ? bounds.x + style.lineNumbersWidth : bounds.x;
            context->FillRectangle(highlightX, lineY,
                                   bounds.width - (style.showLineNumbers ? style.lineNumbersWidth : 0),
                                   style.lineHeight);
        }
    }

// Draw border
    void UltraCanvasTextArea::DrawBorder(IRenderContext* context) {
        auto bounds = GetBounds();
        if (style.borderWidth > 0) {
            context->DrawFilledRectangle(bounds, Colors::Transparent, style.borderWidth, style.borderColor);
        }
    }

// Draw line numbers
    void UltraCanvasTextArea::DrawLineNumbers(IRenderContext* context) {
        auto bounds = GetBounds();

        // Draw line numbers background
        context->SetFillPaint(style.lineNumbersBackgroundColor);
        context->FillRectangle(bounds.x, bounds.y, style.lineNumbersWidth, bounds.height);

        // Draw line numbers separator
        context->SetStrokePaint(style.borderColor);
        context->SetStrokeWidth(1);
        context->DrawLine(bounds.x + style.lineNumbersWidth, bounds.y,
                          bounds.x + style.lineNumbersWidth, bounds.y + bounds.height);

        // Draw line numbers text
        context->SetFontStyle(style.fontStyle);

        for (int i = 0; i < maxVisibleLines && firstVisibleLine + i < static_cast<int>(lines.size()); i++) {
            int lineNum = firstVisibleLine + i + 1;
            std::string numStr = std::to_string(lineNum);

            // Measure the actual width of the line number
            int numY = visibleTextArea.y + i * style.lineHeight;

            // Highlight current line number
            if (firstVisibleLine + i == currentLineIndex) {
                context->SetTextPaint(style.fontColor);
                context->SetFontWeight(FontWeight::Bold);
            } else {
                context->SetTextPaint(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }
            context->SetTextAlignment(TextAlignment::Right);
            context->DrawTextInRect(numStr, bounds.x, numY, style.lineNumbersWidth, style.lineHeight);
        }
    }


    // Draw plain text
    void UltraCanvasTextArea::DrawPlainText(IRenderContext* context) {
        auto bounds = GetBounds();
        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetTextPaint(style.fontColor);
        context->SetClipRect(visibleTextArea);

        for (int i = 0; i < maxVisibleLines && firstVisibleLine + i < static_cast<int>(lines.size()); i++) {
            int lineIndex = firstVisibleLine + i;
            const std::string& line = lines[lineIndex];

            if (!line.empty()) {
                int y = visibleTextArea.y + i * style.lineHeight;

                // Apply horizontal scroll offset if needed
                if (horizontalScrollOffset > 0) {
                    context->DrawText(line, visibleTextArea.x - horizontalScrollOffset, y);
                } else {
                    context->DrawText(line, visibleTextArea.x, y);
                }
            }
        }
        context->PopState();
    }

// Draw text with syntax highlighting
    void UltraCanvasTextArea::DrawHighlightedText(IRenderContext* context) {
        if (!syntaxTokenizer) return;
        context->PushState();
        context->SetClipRect(visibleTextArea);
        context->SetFontStyle(style.fontStyle);

        int textX = visibleTextArea.x;
        int textY = visibleTextArea.y;

        for (int i = firstVisibleLine; i < firstVisibleLine + maxVisibleLines && i < static_cast<int>(lines.size()); i++) {
            const std::string& line = lines[i];
            if (!line.empty()) {
                // Tokenize line
                auto tokens = syntaxTokenizer->TokenizeLine(line);

                int x = textX;
                for (const auto& token : tokens) {
                    // Set color based on token type
                    TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                    context->SetTextPaint(tokenStyle.color);
                    context->SetFontWeight(tokenStyle.bold ? FontWeight::Bold : FontWeight::Normal);

                    // Draw token text
                    context->DrawText(token.text, x, textY);
                    x += context->GetTextWidth(token.text);
                }
            }
            textY += style.lineHeight;
        }
        context->PopState();
    }

// Get color for token type
    const TokenStyle& UltraCanvasTextArea::GetStyleForTokenType(TokenType type) const {
        switch (type) {
            case TokenType::Keyword:
                return style.tokenStyles.keywordStyle;
            case TokenType::Function:
                return style.tokenStyles.functionStyle;
            case TokenType::String:
                return style.tokenStyles.stringStyle;
            case TokenType::Character:
                return style.tokenStyles.characterStyle;
            case TokenType::Comment:
                return style.tokenStyles.commentStyle;
            case TokenType::Number:
                return style.tokenStyles.numberStyle;
            case TokenType::Identifier:
                return style.tokenStyles.identifierStyle;
            case TokenType::Operator:
                return style.tokenStyles.operatorStyle;
            case TokenType::Constant:
                return style.tokenStyles.constantStyle;
            case TokenType::Preprocessor:
                return style.tokenStyles.preprocessorStyle;
            case TokenType::Punctuation:
                return style.tokenStyles.punctuationStyle;
            case TokenType::Type:
                return style.tokenStyles.typeStyle;
            case TokenType::Builtin:
                return style.tokenStyles.builtinStyle;
            case TokenType::Assembly:
                return style.tokenStyles.assemblyStyle;
            case TokenType::Register:
                return style.tokenStyles.registerStyle;
            default:
                return style.tokenStyles.defaultStyle;
        }
    }

// Draw selection
    void UltraCanvasTextArea::DrawSelection(IRenderContext* context) {
        if (!HasSelection()) return;

        auto bounds = GetBounds();

        int startPos = std::min(selectionStart, selectionEnd);
        int endPos = std::max(selectionStart, selectionEnd);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        int textX = visibleTextArea.x;

        for (int line = startLine; line <= endLine; line++) {
            if (line < firstVisibleLine || line >= firstVisibleLine + maxVisibleLines) continue;
            if (line >= static_cast<int>(lines.size())) break;

            int lineY = bounds.y + style.padding + (line - firstVisibleLine) * style.lineHeight;

            int selStart = (line == startLine) ? startCol : 0;
            int selEnd = (line == endLine) ? endCol : lines[line].length();

            // FIXED: Measure actual text widths
            int selX = textX - horizontalScrollOffset;
            if (selStart > 0) {
                std::string textBeforeSelection = lines[line].substr(0, selStart);
                selX += MeasureTextWidth(textBeforeSelection);
            }

            int selWidth = 0;
            if (selEnd > selStart) {
                std::string selectedText = lines[line].substr(selStart, selEnd - selStart);
                selWidth = MeasureTextWidth(selectedText);
            }

            context->SetFillPaint(style.selectionColor);
            context->FillRectangle(selX, lineY, selWidth, style.lineHeight);
        }
    }

    // FIXED: Draw cursor with proper text measurement
    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);
        if (line < firstVisibleLine || line >= firstVisibleLine + maxVisibleLines) return;

        int cursorX = visibleTextArea.x - horizontalScrollOffset;
        if (col > 0 && line < static_cast<int>(lines.size())) {
            std::string textBeforeCursor = lines[line].substr(0, col);
            cursorX += MeasureTextWidth(textBeforeCursor);
        }
        if (cursorX > visibleTextArea.x + visibleTextArea.width) return;
        int cursorY = visibleTextArea.y + (line - firstVisibleLine) * style.lineHeight;

        context->SetStrokeWidth(2);
        context->DrawLine(cursorX, cursorY, cursorX, cursorY + style.lineHeight, style.cursorColor);
    }

    bool UltraCanvasTextArea::IsNeedVerticalScrollbar() {
        return lines.size() > static_cast<size_t>(maxVisibleLines);
    }

    bool UltraCanvasTextArea::IsNeedHorizontalScrollbar() {
        return maxLineWidth > visibleTextArea.width;
    }

    int UltraCanvasTextArea::GetMaxLineWidth() {
        maxLineWidth = 0;
        for (const auto& line : lines) {
            maxLineWidth = std::max(maxLineWidth, MeasureTextWidth(line));
        }
        return maxLineWidth;
    }


// Draw scrollbars
    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetBounds();

        // Vertical scrollbar
        if (IsNeedVerticalScrollbar()) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height;
            int thumbHeight = std::max(20, (maxVisibleLines * scrollbarHeight) / static_cast<int>(lines.size()));
            int thumbY = bounds.y + (firstVisibleLine * (scrollbarHeight - thumbHeight)) /
                                    std::max(1, static_cast<int>(lines.size()) - maxVisibleLines);

            // Draw scrollbar track
            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(scrollbarX, bounds.y, 15, scrollbarHeight);

            verticalScrollThumb.x = scrollbarX;
            verticalScrollThumb.y = thumbY;
            verticalScrollThumb.width = 15;
            verticalScrollThumb.height = thumbHeight;

            // Draw scrollbar thumb
            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(scrollbarX + 2, thumbY, 11, thumbHeight);
        }

        // Horizontal scrollbar
        if (IsNeedHorizontalScrollbar()) {
            float scrollbarY = static_cast<float>(bounds.y + bounds.height - 15);
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));

            float thumbWidthRatio = static_cast<float>(visibleTextArea.width) / static_cast<float>(maxLineWidth);
            float thumbWidth = std::min(scrollbarWidth, scrollbarWidth * thumbWidthRatio);

            float maxScroll = static_cast<float>(maxLineWidth - visibleTextArea.width);
            float scrollRatio = static_cast<float>(horizontalScrollOffset) / maxScroll;
            float thumbX = static_cast<float>(bounds.x) + scrollRatio * (scrollbarWidth - thumbWidth);

            // Draw scrollbar track
            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(static_cast<float>(bounds.x), scrollbarY, scrollbarWidth, 15.0f);

            horizontalScrollThumb.x = thumbX;
            horizontalScrollThumb.y = scrollbarY;
            horizontalScrollThumb.width = thumbWidth;
            horizontalScrollThumb.height = 15;

            // Draw scrollbar thumb
            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(thumbX, scrollbarY + 2, thumbWidth, 11.0f);
        }
    }

// Handle events
    bool UltraCanvasTextArea::OnEvent(const UCEvent& event) {
        if (!IsEnabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
//            case UCEventType::MouseDrag:
//                return HandleMouseDrag(event);
            case UCEventType::KeyUp:
                return HandleKeyPress(event);
//            case UCEventType::TextInput:
//                return HandleTextInput(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            case UCEventType::FocusGained:
                cursorVisible = true;
                cursorBlinkTime = 0;
                return true;
            case UCEventType::FocusLost:
                cursorVisible = false;
                return true;
            default:
                return false;
        }
    }

// Handle mouse click
    bool UltraCanvasTextArea::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;
        auto bounds = GetBounds();

        if (IsNeedVerticalScrollbar() && verticalScrollThumb.Contains(event.x, event.y)) {
            isDraggingVerticalThumb = true;
            // Store the offset from the thumb top to where the mouse clicked
            dragStartOffset.y = event.y - verticalScrollThumb.y;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        if (IsNeedHorizontalScrollbar() && horizontalScrollThumb.Contains(event.x, event.y)) {
            isDraggingHorizontalThumb = true;
            // Store the offset from the thumb left to where the mouse clicked
            dragStartOffset.x = event.x - horizontalScrollThumb.x;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }


        SetFocus(true);

        // Calculate clicked position
        int relativeX = event.x - visibleTextArea.x + horizontalScrollOffset;
        int relativeY = event.y - visibleTextArea.y;

        int clickedLine = firstVisibleLine + (relativeY / style.lineHeight);

        // Clamp to valid range
        clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(lines.size()) - 1));

        int clickedCol = 0;
        if (clickedLine < static_cast<int>(lines.size()) && relativeX > 0) {
            auto context = GetRenderContext();
            if (context) {
                context->PushState();
                context->SetFontStyle(style.fontStyle);
                context->SetFontWeight(FontWeight::Normal);

                // Use the built-in GetTextIndexForXY function
                clickedCol = std::max(0, context->GetTextIndexForXY(lines[clickedLine], relativeX, 0));
                context->PopState();
            }
        }

        // Update cursor position
        cursorPosition = GetPositionFromLineColumn(clickedLine, clickedCol);
        currentLineIndex = clickedLine;

        // Handle selection with shift key
        if (event.shift && selectionStart >= 0) {
            selectionEnd = cursorPosition;
        } else {
            // Clear selection
            selectionStart = -1;
            selectionEnd = -1;
        }

        RequestRedraw();
        return true;
    }

    // Handle mouse double-click (select word)
    bool UltraCanvasTextArea::HandleMouseDoubleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // TODO: Implement word selection

        return true;
    }


    // Handle mouse move
    bool UltraCanvasTextArea::HandleMouseMove(const UCEvent& event) {
        // Handle vertical scrollbar dragging
        if (isDraggingVerticalThumb) {
            Rect2Di bounds = GetBounds();
            int thumbHeight = verticalScrollThumb.height;
            int maxThumbY = bounds.height - thumbHeight;

            // Calculate new thumb position
            int newThumbY = event.y - bounds.y - dragStartOffset.y;
            newThumbY = std::max(0, std::min(newThumbY, maxThumbY));

            // Convert thumb position to scroll offset
            int totalLines = static_cast<int>(lines.size());
            int visibleLines = maxVisibleLines;
            if (totalLines > visibleLines && maxThumbY > 0) {
                // Use the same formula as in UpdateScrollBars but inverted
                verticalScrollOffset = (newThumbY * (totalLines - visibleLines)) / maxThumbY;
                verticalScrollOffset = std::max(0, std::min(verticalScrollOffset, totalLines - visibleLines));
            } else {
                verticalScrollOffset = 0;
            }

            RequestRedraw();
            return true;
        }

        // Handle horizontal scrollbar dragging
        if (isDraggingHorizontalThumb) {
            Rect2Di bounds = GetBounds();
            float lineNumberWidth = style.showLineNumbers ? style.lineNumbersWidth : 0.0f;
            int thumbWidth = horizontalScrollThumb.width;
            int maxThumbX = bounds.width - thumbWidth - static_cast<int>(lineNumberWidth) - (IsNeedVerticalScrollbar() ? 15 : 0);

            // Calculate new thumb position
            int newThumbX = event.x - bounds.x - static_cast<int>(lineNumberWidth) - dragStartOffset.x;
            newThumbX = std::max(0, std::min(newThumbX, maxThumbX));

            // Convert thumb position to scroll offset

            int visibleWidth = visibleTextArea.width;
            if (maxLineWidth > visibleWidth && maxThumbX > 0) {
                horizontalScrollOffset = static_cast<int>((newThumbX * (maxLineWidth - visibleWidth)) / maxThumbX);
                horizontalScrollOffset = std::max(0, horizontalScrollOffset);
            } else {
                horizontalScrollOffset = 0;
            }

            RequestRedraw();
            return true;
        }

        // Handle text selection by dragging
        if (!IsFocused()) return false;

        // TODO: Implement text selection dragging
        return false;
    }

    bool UltraCanvasTextArea::HandleMouseUp(const UCEvent &event) {
        bool wasHandled = false;

        // Stop scrollbar dragging
        if (isDraggingVerticalThumb || isDraggingHorizontalThumb) {
            isDraggingVerticalThumb = false;
            isDraggingHorizontalThumb = false;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
            wasHandled = true;
        }

        return wasHandled;
    }
// Handle mouse drag
    bool UltraCanvasTextArea::HandleMouseDrag(const UCEvent& event) {
        if (!IsFocused()) return false;
        auto bounds = GetBounds();

        // Start selection if not started
        if (selectionStart < 0) {
            selectionStart = cursorPosition;
        }

        // Calculate dragged position
        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);
        float relativeX = event.x - textX + horizontalScrollOffset;
        int relativeY = event.y - bounds.y - style.padding;

        int draggedLine = firstVisibleLine + (relativeY / style.lineHeight);
        draggedLine = std::max(0, std::min(draggedLine, static_cast<int>(lines.size()) - 1));

        int draggedCol = 0;
        if (draggedLine < static_cast<int>(lines.size()) && relativeX > 0) {
            auto context = GetRenderContext();
            if (context) {
                context->PushState();
                context->SetFontStyle(style.fontStyle);
                context->SetFontWeight(FontWeight::Normal);

                draggedCol = std::max(0, context->GetTextIndexForXY(lines[draggedLine], relativeX, 0));

                context->PopState();
            }
        }

        selectionEnd = GetPositionFromLineColumn(draggedLine, draggedCol);
        cursorPosition = selectionEnd;
        currentLineIndex = draggedLine;

        return true;
    }

    bool UltraCanvasTextArea::HandleMouseWheel(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        if (event.shift) {
            auto ctx = GetRenderContext();
            // Horizontal scroll with Shift+Wheel

            if (IsNeedHorizontalScrollbar()) {
                horizontalScrollOffset -= event.wheelDelta * 20;
                horizontalScrollOffset = std::max(0,
                                                  std::min(horizontalScrollOffset, maxLineWidth - visibleTextArea.width));
            }
        } else {
            // Vertical scroll
            int scrollAmount = (event.wheelDelta > 0) ? -2 : 2;
            firstVisibleLine += scrollAmount;
            firstVisibleLine = std::max(0,
                                        std::min(firstVisibleLine,
                                                 static_cast<int>(lines.size()) - maxVisibleLines));
        }

        RequestRedraw();
        return true;
    }

// Handle key press
    bool UltraCanvasTextArea::HandleKeyPress(const UCEvent& event) {
        if (!IsFocused() || isReadOnly) return false;

        bool handled = true;

        switch (event.virtualKey) {
            case UCKeys::Backspace:
                DeleteCharacterBackward();
                break;
            case UCKeys::Delete:
                DeleteCharacterForward();
                break;
            case UCKeys::Left:
                MoveCursorLeft(event.shift);
                break;
            case UCKeys::Right:
                MoveCursorRight(event.shift);
                break;
            case UCKeys::Up:
                MoveCursorUp(event.shift);
                break;
            case UCKeys::Down:
                MoveCursorDown(event.shift);
                break;
            case UCKeys::Home:
                MoveCursorToLineStart(event.shift);
                break;
            case UCKeys::End:
                MoveCursorToLineEnd(event.shift);
                break;
            case UCKeys::Enter:
                InsertCharacter('\n');
                break;
            case UCKeys::A:
                if (event.ctrl) {
                    SelectAll();
                }
                break;
            case UCKeys::C:
                if (event.ctrl) {
                    CopySelection();
                }
                break;
            case UCKeys::V:
                if (event.ctrl) {
                    PasteClipboard();
                }
                break;
            case UCKeys::X:
                if (event.ctrl) {
                    CutSelection();
                }
                break;
            default:
                if (event.character && !event.ctrl && !event.alt && !event.meta) {
                    if (HasSelection()) {
                        DeleteSelection();
                    }

                    InsertCharacter(event.character);
                } else {
                    handled = false;
                }
                break;
        }

        if (handled) {
            EnsureCursorVisible();
        }

        return handled;
    }

// Text manipulation methods
    void UltraCanvasTextArea::SetText(const std::string& newText) {
        text = newText;
        lines.clear();

        // Split text into lines
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }

        if (lines.empty()) {
            lines.push_back("");
        }

        // Reset cursor and selection
        cursorPosition = 0;
        currentLineIndex = 0;
        selectionStart = -1;
        selectionEnd = -1;

        RebuildText();

        // Trigger text changed callback
        if (onTextChanged) {
            onTextChanged(text);
        }
    }

    std::string UltraCanvasTextArea::GetText() const {
        return text;
    }

    void UltraCanvasTextArea::InsertText(const std::string& textToInsert) {
        if (isReadOnly) return;

        if (HasSelection()) {
            DeleteSelection();
        }

        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (lines.empty()) {
            lines.push_back("");
        }

        // Insert text at cursor position
        for (char ch : textToInsert) {
            if (ch == '\n') {
                // Split line at cursor
                std::string currentLine = lines[line];
                lines[line] = currentLine.substr(0, col);
                lines.insert(lines.begin() + line + 1, currentLine.substr(col));
                line++;
                col = 0;
            } else {
                lines[line].insert(col, 1, ch);
                col++;
            }
        }

        // Update cursor position
        cursorPosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;

        // Rebuild full text
        RebuildText();

        // Trigger callback
        if (onTextChanged) {
            onTextChanged(text);
        }
    }

    void UltraCanvasTextArea::InsertCharacter(char ch) {
        if (isReadOnly) return;

        auto [line, col] = GetLineColumnFromPosition(cursorPosition);
        if (lines.empty()) {
            lines.push_back("");
        }
        if (line < static_cast<int>(lines.size())) {
            if (ch == '\n') {
                // Split line at cursor
                std::string currentLine = lines[line];
                lines[line] = currentLine.substr(0, col);
                lines.insert(lines.begin() + line + 1, currentLine.substr(col));
                currentLineIndex = line + 1;
                cursorPosition++;
            } else {
                // Insert character in current line
                lines[line].insert(col, 1, ch);
                cursorPosition++;
            }
            RebuildText();
        }
    }

    void UltraCanvasTextArea::DeleteCharacterBackward() {
        if (isReadOnly || cursorPosition == 0) return;

        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (col > 0) {
            lines[line].erase(col - 1, 1);
            cursorPosition--;
        } else if (line > 0) {
            // Merge with previous line
            int prevLineLength = lines[line - 1].length();
            lines[line - 1] += lines[line];
            lines.erase(lines.begin() + line);
            currentLineIndex = line - 1;
            cursorPosition = GetPositionFromLineColumn(currentLineIndex, prevLineLength);
        }

        RebuildText();
        if (onTextChanged) {
            onTextChanged(text);
        }
    }

    void UltraCanvasTextArea::DeleteCharacterForward() {
        if (isReadOnly) return;

        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (line < static_cast<int>(lines.size())) {
            if (col < static_cast<int>(lines[line].length())) {
                lines[line].erase(col, 1);
            } else if (line < static_cast<int>(lines.size()) - 1) {
                // Merge with next line
                lines[line] += lines[line + 1];
                lines.erase(lines.begin() + line + 1);
            }

            RebuildText();
            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    void UltraCanvasTextArea::DeleteSelection() {
        if (!HasSelection() || isReadOnly) return;

        int startPos = std::min(selectionStart, selectionEnd);
        int endPos = std::max(selectionStart, selectionEnd);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        if (startLine == endLine) {
            // Delete within same line
            lines[startLine].erase(startCol, endCol - startCol);
        } else {
            // Delete across multiple lines
            lines[startLine] = lines[startLine].substr(0, startCol) + lines[endLine].substr(endCol);
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
        }

        cursorPosition = startPos;
        currentLineIndex = startLine;
        selectionStart = -1;
        selectionEnd = -1;

        RebuildText();
        if (onTextChanged) {
            onTextChanged(text);
        }
    }

// Cursor movement methods
    void UltraCanvasTextArea::MoveCursorLeft(bool selecting) {
        if (cursorPosition > 0) {
            cursorPosition--;
            auto [line, col] = GetLineColumnFromPosition(cursorPosition);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStart < 0) selectionStart = cursorPosition + 1;
                selectionEnd = cursorPosition;
            } else {
                selectionStart = -1;
                selectionEnd = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorRight(bool selecting) {
        if (cursorPosition < static_cast<int>(text.length())) {
            cursorPosition++;
            auto [line, col] = GetLineColumnFromPosition(cursorPosition);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStart < 0) selectionStart = cursorPosition - 1;
                selectionEnd = cursorPosition;
            } else {
                selectionStart = -1;
                selectionEnd = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorUp(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (line > 0) {
            line--;
            col = std::min(col, static_cast<int>(lines[line].length()));
            cursorPosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStart < 0) selectionStart = GetPositionFromLineColumn(line + 1, col);
                selectionEnd = cursorPosition;
            } else {
                selectionStart = -1;
                selectionEnd = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorDown(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (line < static_cast<int>(lines.size()) - 1) {
            line++;
            col = std::min(col, static_cast<int>(lines[line].length()));
            cursorPosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStart < 0) selectionStart = GetPositionFromLineColumn(line - 1, col);
                selectionEnd = cursorPosition;
            } else {
                selectionStart = -1;
                selectionEnd = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineStart(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (selecting) {
            if (selectionStart < 0) selectionStart = cursorPosition;
            cursorPosition = GetPositionFromLineColumn(line, 0);
            selectionEnd = cursorPosition;
        } else {
            cursorPosition = GetPositionFromLineColumn(line, 0);
            selectionStart = -1;
            selectionEnd = -1;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, 0);
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineEnd(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        if (line < static_cast<int>(lines.size())) {
            int lineLength = lines[line].length();

            if (selecting) {
                if (selectionStart < 0) selectionStart = cursorPosition;
                cursorPosition = GetPositionFromLineColumn(line, lineLength);
                selectionEnd = cursorPosition;
            } else {
                cursorPosition = GetPositionFromLineColumn(line, lineLength);
                selectionStart = -1;
                selectionEnd = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, lineLength);
            }
        }
    }

// Selection methods
    void UltraCanvasTextArea::SelectAll() {
        selectionStart = 0;
        selectionEnd = text.length();
        cursorPosition = selectionEnd;
        RequestRedraw();
    }

    void UltraCanvasTextArea::SelectLine(int lineIndex) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            selectionStart = GetPositionFromLineColumn(lineIndex, 0);
            selectionEnd = GetPositionFromLineColumn(lineIndex, lines[lineIndex].length());
            cursorPosition = selectionEnd;
            currentLineIndex = lineIndex;
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::ClearSelection() {
        selectionStart = -1;
        selectionEnd = -1;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::HasSelection() const {
        return selectionStart >= 0 && selectionEnd >= 0 && selectionStart != selectionEnd;
    }

// Clipboard operations
    void UltraCanvasTextArea::CopySelection() {
        if (!HasSelection()) return;

        int startPos = std::min(selectionStart, selectionEnd);
        int endPos = std::max(selectionStart, selectionEnd);

        std::string selectedText = text.substr(startPos, endPos - startPos);
//        SetClipboardText(selectedText);
    }

    void UltraCanvasTextArea::CutSelection() {
        if (!HasSelection() || isReadOnly) return;

        CopySelection();
        DeleteSelection();
    }

    void UltraCanvasTextArea::PasteClipboard() {
        if (isReadOnly) return;

//        std::string clipboardText = GetClipboardText();
        std::string clipboardText = "Clipboard content";
        if (!clipboardText.empty()) {
            InsertText(clipboardText);
        }
    }

// Syntax highlighting methods
    void UltraCanvasTextArea::SetHighlightSyntax(bool on) {
        style.highlightSyntax = on;

        if (on && !syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetProgrammingLanguage(const std::string& language) {
        if (style.highlightSyntax) {
            if (!syntaxTokenizer) {
                syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
            }
            syntaxTokenizer->SetLanguage(language);
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::SetProgrammingLanguageByExtension(const std::string& extension) {
        if (style.highlightSyntax) {
            if (!syntaxTokenizer) {
                syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
            }
            syntaxTokenizer->SetLanguageByExtension(extension);
            RequestRedraw();
        }
    }

// Theme application methods
    void UltraCanvasTextArea::ApplyDarkTheme() {
        style.backgroundColor = {30, 30, 30, 255};
        style.fontColor = {210, 210, 210, 255};
        style.currentLineColor = {60, 60, 60, 255};
        style.lineNumbersColor = {100, 100, 100, 255};
        style.lineNumbersBackgroundColor = {25, 25, 25, 255};
        style.selectionColor = {60, 90, 150, 100};
        style.cursorColor = {255, 255, 255, 255};

        style.tokenStyles.keywordStyle.color = {0x4c, 0xbb, 0xc9, 255};
        style.tokenStyles.functionStyle.color = {0xdc, 0xd6, 0xa2, 255};
        style.tokenStyles.stringStyle.color = {0xce, 0x91, 0x78, 255};
        style.tokenStyles.characterStyle.color = {0xce, 0x91, 0x78, 255};
        style.tokenStyles.commentStyle.color = {0x68, 0x86, 0x42, 255};
        style.tokenStyles.numberStyle.color = {0xa2, 0xcc, 0x9d, 255};
        style.tokenStyles.identifierStyle.color = {0xdc, 0xd6, 0xa2, 255};
        style.tokenStyles.operatorStyle.color = {0xce, 0xbd, 0x88, 255};
        style.tokenStyles.constantStyle.color = {0x4e, 0xb8, 0xbe, 255};
        style.tokenStyles.preprocessorStyle.color = {128, 240, 128, 255};
        style.tokenStyles.builtinStyle.color = {0xdc, 0xd6, 0xa2, 255};
        style.tokenStyles.defaultStyle.color = {0xe0, 0xe0, 0xe0, 255};

        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

// Style application methods for specific languagesRules
    void UltraCanvasTextArea::ApplyCodeStyle(const std::string& language) {
        ApplyDefaultStyle();
        SetHighlightSyntax(true);
        SetProgrammingLanguage(language);
        style.showLineNumbers = true;
        style.fontStyle.fontFamily = "DejaVu Sans Mono";
        highlightCurrentLine = true;

        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ApplyDarkCodeStyle(const std::string& language) {
        ApplyDefaultStyle();
        ApplyDarkTheme();
        SetHighlightSyntax(true);
        SetProgrammingLanguage(language);
        style.showLineNumbers = true;
        style.fontStyle.fontFamily = "DejaVu Sans Mono";
        highlightCurrentLine = true;

        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ApplyPlainTextStyle() {
        ApplyDefaultStyle();
        SetHighlightSyntax(false);
        style.showLineNumbers = false;
        style.fontStyle.fontFamily = "Arial";
        highlightCurrentLine = false;

        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

// Line operations
    void UltraCanvasTextArea::GoToLine(int lineNumber) {
        if (lineNumber > 0 && lineNumber <= static_cast<int>(lines.size())) {
            currentLineIndex = lineNumber - 1;
            cursorPosition = GetPositionFromLineColumn(currentLineIndex, 0);
            EnsureCursorVisible();

            if (onCursorPositionChanged) {
                onCursorPositionChanged(currentLineIndex, 0);
            }
        }
    }

    int UltraCanvasTextArea::GetCurrentLine() const {
        return currentLineIndex + 1;
    }

    int UltraCanvasTextArea::GetLineCount() const {
        return lines.size();
    }

// Search and replace
    void UltraCanvasTextArea::FindText(const std::string& searchText, bool caseSensitive) {
        if (searchText.empty()) return;

        std::string haystack = caseSensitive ? text : ToLowerCase(text);
        std::string needle = caseSensitive ? searchText : ToLowerCase(searchText);

        size_t pos = haystack.find(needle, cursorPosition);
        if (pos != std::string::npos) {
            selectionStart = pos;
            selectionEnd = pos + needle.length();
            cursorPosition = selectionEnd;

            auto [line, col] = GetLineColumnFromPosition(cursorPosition);
            currentLineIndex = line;
            EnsureCursorVisible();
        }
    }

    void UltraCanvasTextArea::ReplaceText(const std::string& findText, const std::string& replaceText, bool all) {
        if (isReadOnly || findText.empty()) return;

        if (all) {
            size_t pos = 0;
            while ((pos = text.find(findText, pos)) != std::string::npos) {
                text.replace(pos, findText.length(), replaceText);
                pos += replaceText.length();
            }
            SetText(text);
        } else {
            if (HasSelection()) {
                int startPos = std::min(selectionStart, selectionEnd);
                int endPos = std::max(selectionStart, selectionEnd);
                std::string selected = text.substr(startPos, endPos - startPos);

                if (selected == findText) {
                    text.replace(startPos, findText.length(), replaceText);
                    SetText(text);
                    cursorPosition = startPos + replaceText.length();
                }
            }
        }
    }

// Helper methods
    std::pair<int, int> UltraCanvasTextArea::GetLineColumnFromPosition(int position) const {
        int line = 0;
        int col = 0;
        int currentPos = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            int lineLength = lines[i].length();
            if (currentPos + lineLength >= position) {
                line = i;
                col = position - currentPos;
                break;
            }
            currentPos += lineLength + 1; // +1 for newline
            line = i;
        }

        return {line, col};
    }

    int UltraCanvasTextArea::GetPositionFromLineColumn(int line, int column) const {
        int position = 0;

        for (int i = 0; i < line && i < static_cast<int>(lines.size()); i++) {
            position += lines[i].length() + 1; // +1 for newline
        }

        if (line < static_cast<int>(lines.size())) {
            position += std::min(column, static_cast<int>(lines[line].length()));
        }

        return position;
    }

    void UltraCanvasTextArea::EnsureCursorVisible() {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);

        // Vertical scrolling
        if (line < firstVisibleLine) {
            firstVisibleLine = line;
        } else if (line >= firstVisibleLine + maxVisibleLines) {
            firstVisibleLine = line - maxVisibleLines + 1;
        }
        // Horizontal scrolling
        if (col > 0 && line < static_cast<int>(lines.size())) {
            std::string textToCursor = lines[line].substr(0, col);
            int cursorX = MeasureTextWidth(textToCursor);
            int visibleWidth = visibleTextArea.width;

            if (cursorX < horizontalScrollOffset) {
                horizontalScrollOffset = cursorX;
            } else if (cursorX > horizontalScrollOffset + visibleWidth) {
                horizontalScrollOffset = cursorX - visibleWidth;
            }
            if (maxLineWidth > visibleWidth) {
                horizontalScrollOffset = std::min(horizontalScrollOffset, maxLineWidth - visibleWidth);
            }
        } else {
            horizontalScrollOffset = 0;
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::CalculateVisibleArea() {
        visibleTextArea = GetBounds();
        visibleTextArea.x += style.padding;
        visibleTextArea.y += style.padding;
        visibleTextArea.width -= style.padding * 2;
        visibleTextArea.height -= style.padding * 2;
        if (style.showLineNumbers) {
            visibleTextArea.x += (style.lineNumbersWidth + 5);
            visibleTextArea.width -= (style.lineNumbersWidth + 5);
        }

        maxLineWidth = 0;
        for (const auto& line : lines) {
            maxLineWidth = std::max(maxLineWidth, MeasureTextWidth(line));
        }

        if (IsNeedHorizontalScrollbar()) {
            visibleTextArea.height -= 15; // Reserve space for horizontal scrollbar
            maxVisibleLines = std::max(1, visibleTextArea.height / style.lineHeight);
            if (IsNeedVerticalScrollbar()) {
                visibleTextArea.width -= 15;
            }
        } else {
            maxVisibleLines = std::max(1, visibleTextArea.height / style.lineHeight);
            if (IsNeedVerticalScrollbar()) {
                visibleTextArea.width -= 15;
                if (IsNeedHorizontalScrollbar()) {
                    visibleTextArea.height -= 15; // Reserve space for horizontal scrollbar
                    maxVisibleLines = std::max(1, visibleTextArea.height / style.lineHeight);
                }
            }
        }
        isNeedRecalculateVisibleArea = false;
    }


    void UltraCanvasTextArea::RebuildText() {
        text.clear();
        for (size_t i = 0; i < lines.size(); i++) {
            text += lines[i];
            if (i < lines.size() - 1) {
                text += '\n';
            }
        }

        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(text);
        }
    }

//    int UltraCanvasTextArea::GetMaxLineLength() const {
//        // Return in approximate character count
//        float avgCharWidth = static_cast<float>(MeasureTextWidth("m"));
//        return static_cast<int>(maxLineWidth / avgCharWidth);
//    }

//    int UltraCanvasTextArea::GetVisibleCharactersPerLine() const {
//        float textWidth = GetWidth() - style.padding * 2;
//        if (style.showLineNumbers) {
//            textWidth -= style.lineNumbersWidth + 5;
//        }
//        float avgCharWidth = static_cast<float>(MeasureTextWidth("m"));
//        return static_cast<int>(textWidth / avgCharWidth);
//    }

    int UltraCanvasTextArea::MeasureTextWidth(const std::string& text) const {
        auto context = GetRenderContext();
        if (!context || text.empty()) return 0.0f;

        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetFontWeight(FontWeight::Normal);

        int width = context->GetTextWidth(text);

        context->PopState();
        return width;
    }

    // Factory functions
    std::shared_ptr<UltraCanvasTextArea> CreateCodeEditor(const std::string& name, int id,
                                                          int x, int y, int width, int height,
                                                          const std::string& language) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle(language);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateDarkCodeEditor(const std::string& name, int id,
                                                              int x, int y, int width, int height,
                                                              const std::string& language) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyDarkCodeStyle(language);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreatePlainTextEditor(const std::string& name, int id,
                                                               int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyPlainTextStyle();
        return editor;
    }

} // namespace UltraCanvas