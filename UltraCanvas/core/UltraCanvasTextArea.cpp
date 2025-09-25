// UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting
// Version: 2.0.0
// Last Modified: 2024-12-20
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxHighlighter.h"
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
              isMultiLine(true),
              wordWrap(false),
              highlightCurrentLine(false),
              currentLineIndex(0),
              fontSize(14),
              lineHeight(20),
              fontFamily("Consolas"),
              leftMargin(10),
              characterWidth(8) {

        // Initialize style with defaults
        ApplyDefaultStyle();

        // Calculate visible lines
        UpdateVisibleLines();

        // Initialize syntax highlighter if needed
        if (style.syntaxMode == SyntaxMode::Programming) {
            syntaxHighlighter = std::make_unique<SyntaxTokenizer>();
        }
    }

// Destructor
    UltraCanvasTextArea::~UltraCanvasTextArea() = default;

// Initialize default style
    void UltraCanvasTextArea::ApplyDefaultStyle() {
        style.fontFamily = "Courier New";
        style.fontSize = 14;
        style.fontColor = {0, 0, 0, 255};
        style.backgroundColor = {255, 255, 255, 255};
        style.borderColor = {200, 200, 200, 255};
        style.selectionColor = {51, 153, 255, 100};
        style.cursorColor = {0, 0, 0, 255};
        style.currentLineColor = {240, 240, 240, 255};
        style.lineNumbersColor = {128, 128, 128, 255};
        style.lineNumbersBackgroundColor = {248, 248, 248, 255};
        style.borderWidth = 1;
        style.padding = 5;
        style.showLineNumbers = false;
        style.lineNumbersWidth = 50;
        style.syntaxMode = SyntaxMode::PlainText;

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
        auto ctx = GetRenderContext();
        // Draw background
        DrawBackground(ctx);

        // Draw line numbers if enabled
        if (style.showLineNumbers) {
            DrawLineNumbers(ctx);
        }

        // Draw text with syntax highlighting
        if (style.syntaxMode != SyntaxMode::PlainText && syntaxHighlighter) {
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
        if (NeedsScrollbars()) {
            DrawScrollbars(ctx);
        }
    }

// Draw background
    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFillColor(style.backgroundColor);
        context->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        // Draw current line highlight
        if (highlightCurrentLine && IsFocused()) {
            int lineY = bounds.y + style.padding + (currentLineIndex - firstVisibleLine) * lineHeight;
            if (lineY >= bounds.y && lineY < bounds.y + bounds.height) {
                context->SetFillColor(style.currentLineColor);
                int highlightX = style.showLineNumbers ? bounds.x + style.lineNumbersWidth : bounds.x;
                context->FillRectangle(highlightX, lineY, bounds.width - (style.showLineNumbers ? style.lineNumbersWidth : 0), lineHeight);
            }
        }
    }

// Draw border
    void UltraCanvasTextArea::DrawBorder(IRenderContext* context) {
        auto bounds = GetBounds();
        if (style.borderWidth > 0) {
            context->SetStrokeColor(style.borderColor);
            context->SetStrokeWidth(style.borderWidth);
            context->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
        }
    }

// Draw line numbers
    void UltraCanvasTextArea::DrawLineNumbers(IRenderContext* context) {
        // Draw line numbers background
        auto bounds = GetBounds();
        context->SetFillColor(style.lineNumbersBackgroundColor);
        context->FillRectangle(bounds.x, bounds.y, style.lineNumbersWidth, bounds.height);

        // Draw line numbers separator
        context->SetStrokeColor(style.borderColor);
        context->SetStrokeWidth(1);
        context->DrawLine(bounds.x + style.lineNumbersWidth, bounds.y,
                          bounds.x + style.lineNumbersWidth, bounds.y + bounds.height);

        // Draw line numbers text
        context->SetFont(style.fontFamily, style.fontSize);
        context->SetTextColor(style.lineNumbersColor);

        int lineNum = firstVisibleLine + 1;
        int y = bounds.y + style.padding + lineHeight;

        for (int i = 0; i < maxVisibleLines && lineNum <= static_cast<int>(lines.size()); i++, lineNum++) {
            std::string numStr = std::to_string(lineNum);
            int numWidth = numStr.length() * characterWidth;
            int numX = bounds.x + style.lineNumbersWidth - numWidth - 5;

            // Highlight current line number
            if (lineNum - 1 == currentLineIndex) {
                context->SetTextColor(style.fontColor);
                context->SetFontWeight(FontWeight::Bold);
            } else {
                context->SetTextColor(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }

            context->DrawText(numStr, numX, y);
            y += lineHeight;
        }
    }

// Draw plain text
    void UltraCanvasTextArea::DrawPlainText(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFont(style.fontFamily, style.fontSize);
        context->SetTextColor(style.fontColor);

        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);
        int textY = bounds.y + style.padding;

        for (int i = firstVisibleLine; i < firstVisibleLine + maxVisibleLines && i < static_cast<int>(lines.size()); i++) {
            const std::string& line = lines[i];
            if (!line.empty()) {
                // Apply horizontal scroll offset
                std::string visibleText = line;
                if (horizontalScrollOffset > 0 && horizontalScrollOffset < static_cast<int>(line.length())) {
                    visibleText = line.substr(horizontalScrollOffset);
                }
                context->DrawText(visibleText, textX, textY);
            }
            textY += lineHeight;
        }
    }

// Draw text with syntax highlighting
    void UltraCanvasTextArea::DrawHighlightedText(IRenderContext* context) {
        if (!syntaxHighlighter) return;
        auto bounds = GetBounds();
        context->SetFont(style.fontFamily, style.fontSize);

        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);
        int textY = bounds.y + style.padding;

        for (int i = firstVisibleLine; i < firstVisibleLine + maxVisibleLines && i < static_cast<int>(lines.size()); i++) {
            const std::string& line = lines[i];
            if (!line.empty()) {
                // Tokenize line
                auto tokens = syntaxHighlighter->TokenizeLine(line);

                int x = textX;
                for (const auto& token : tokens) {
                    // Set color based on token type
                    TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                    context->SetTextColor(tokenStyle.color);
                    context->SetFontWeight(tokenStyle.bold ? FontWeight::Bold : FontWeight::Normal);

                    // Draw token text
                    context->DrawText(token.text, x, textY);
                    x += token.text.length() * characterWidth;
                }
            }
            textY += lineHeight;
        }
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
                return style.tokenStyles.unknownStyle;
        }
    }

// Draw selection
    void UltraCanvasTextArea::DrawSelection(IRenderContext* context) {
        if (selectionStart < 0 || selectionEnd < 0) return;
        auto bounds = GetBounds();
        context->SetFillColor(style.selectionColor);

        int startPos = std::min(selectionStart, selectionEnd);
        int endPos = std::max(selectionStart, selectionEnd);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);

        for (int line = startLine; line <= endLine; line++) {
            if (line < firstVisibleLine || line >= firstVisibleLine + maxVisibleLines) continue;
            if (line >= static_cast<int>(lines.size())) break;

            int lineY = bounds.y + style.padding + (line - firstVisibleLine) * lineHeight;

            int selStart = (line == startLine) ? startCol : 0;
            int selEnd = (line == endLine) ? endCol : lines[line].length();

            int selX = textX + selStart * characterWidth;
            int selWidth = (selEnd - selStart) * characterWidth;

            context->FillRectangle(selX, lineY, selWidth, lineHeight);
        }
    }

// Draw cursor
    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        auto [line, col] = GetLineColumnFromPosition(cursorPosition);
        auto bounds = GetBounds();
        if (line < firstVisibleLine || line >= firstVisibleLine + maxVisibleLines) return;

        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);
        int cursorX = textX + col * characterWidth;
        int cursorY = bounds.y + style.padding + (line - firstVisibleLine) * lineHeight;

        context->SetStrokeColor(style.cursorColor);
        context->SetStrokeWidth(2);
        context->DrawLine(cursorX, cursorY, cursorX, cursorY + lineHeight);
    }

// Draw scrollbars
    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetBounds();
        // Vertical scrollbar
        if (lines.size() > static_cast<size_t>(maxVisibleLines)) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height;
            int thumbHeight = (maxVisibleLines * scrollbarHeight) / lines.size();
            int thumbY = bounds.y + (firstVisibleLine * scrollbarHeight) / lines.size();

            // Draw scrollbar track
            context->SetFillColor({240, 240, 240, 255});
            context->FillRectangle(scrollbarX, bounds.y, 15, scrollbarHeight);

            // Draw scrollbar thumb
            context->SetFillColor({180, 180, 180, 255});
            context->FillRectangle(scrollbarX + 2, thumbY, 11, thumbHeight);
        }

        // Horizontal scrollbar (if needed)
        if (GetMaxLineLength() * characterWidth > bounds.width - style.padding * 2) {
            int scrollbarY = bounds.y + bounds.height - 15;
            int scrollbarWidth = bounds.width;
            int maxLength = GetMaxLineLength();
            int visibleChars = (bounds.width - style.padding * 2) / characterWidth;
            int thumbWidth = (visibleChars * scrollbarWidth) / maxLength;
            int thumbX = bounds.x + (horizontalScrollOffset * scrollbarWidth) / maxLength;

            // Draw scrollbar track
            context->SetFillColor({240, 240, 240, 255});
            context->FillRectangle(bounds.x, scrollbarY, scrollbarWidth, 15);

            // Draw scrollbar thumb
            context->SetFillColor({180, 180, 180, 255});
            context->FillRectangle(thumbX, scrollbarY + 2, thumbWidth, 11);
        }
    }

// Handle events
    bool UltraCanvasTextArea::OnEvent(const UCEvent& event) {
        if (!IsEnabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseClick(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
//            case UCEventType::MouseDrag:
//                return HandleMouseDrag(event);
            case UCEventType::KeyUp:
                return HandleKeyPress(event);
//            case UCEventType::TextInput:
//                return HandleTextInput(event);
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
    bool UltraCanvasTextArea::HandleMouseClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;
        auto bounds = GetBounds();

        SetFocus(true);

        // Calculate clicked position
        int textX = bounds.x + style.padding + (style.showLineNumbers ? style.lineNumbersWidth + 5 : 0);
        int relativeX = event.x - textX;
        int relativeY = event.y - bounds.y - style.padding;

        int clickedLine = firstVisibleLine + (relativeY / lineHeight);
        int clickedCol = relativeX / characterWidth;

        // Clamp to valid range
        clickedLine = std::max(0, std::min(clickedLine, static_cast<int>(lines.size()) - 1));
        if (clickedLine < static_cast<int>(lines.size())) {
            clickedCol = std::max(0, std::min(clickedCol, static_cast<int>(lines[clickedLine].length())));
        }

        // Update cursor position
        cursorPosition = GetPositionFromLineColumn(clickedLine, clickedCol);
        currentLineIndex = clickedLine;

        // Clear selection
        selectionStart = -1;
        selectionEnd = -1;

        return true;
    }

// Handle mouse move
    bool UltraCanvasTextArea::HandleMouseMove(const UCEvent& event) {
        if (!Contains(event.x, event.y)) {
            //SetCursor(CursorType::Arrow);
            return false;
        }

        //SetCursor(CursorType::Text);
        return true;
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
        int relativeX = event.x - textX;
        int relativeY = event.y - bounds.y - style.padding;

        int draggedLine = firstVisibleLine + (relativeY / lineHeight);
        int draggedCol = relativeX / characterWidth;

        // Clamp to valid range
        draggedLine = std::max(0, std::min(draggedLine, static_cast<int>(lines.size()) - 1));
        if (draggedLine < static_cast<int>(lines.size())) {
            draggedCol = std::max(0, std::min(draggedCol, static_cast<int>(lines[draggedLine].length())));
        }

        // Update selection end
        selectionEnd = GetPositionFromLineColumn(draggedLine, draggedCol);
        cursorPosition = selectionEnd;
        currentLineIndex = draggedLine;

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
                if (isMultiLine) {
                    InsertNewLine();
                }
                break;
            case UCKeys::Tab:
                InsertTab();
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
                if (event.character) {
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
            UpdateSyntaxHighlighting();
            EnsureCursorVisible();
        }

        return handled;
    }

// Handle text input
    bool UltraCanvasTextArea::HandleTextInput(const UCEvent& event) {
        if (!IsFocused() || isReadOnly) return false;

        if (HasSelection()) {
            DeleteSelection();
        }

        InsertCharacter(event.character);

        return true;
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

        // Update syntax highlighting
        UpdateSyntaxHighlighting();

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

        // Insert text at cursor position
        for (char ch : textToInsert) {
            if (ch == '\n' && isMultiLine) {
                // Split line at cursor
                std::string currentLine = lines[line];
                lines[line] = currentLine.substr(0, col);
                lines.insert(lines.begin() + line + 1, currentLine.substr(col));
                line++;
                col = 0;
            } else if (ch != '\n') {
                lines[line].insert(col, 1, ch);
                col++;
            }
        }

        // Update cursor position
        cursorPosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;

        // Rebuild full text
        RebuildText();

        // Update syntax highlighting
        UpdateSyntaxHighlighting();

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
            lines[line].insert(col, 1, ch);
            cursorPosition++;

            RebuildText();

            RequestRedraw();
            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    void UltraCanvasTextArea::InsertNewLine() {
        if (!isMultiLine || isReadOnly) return;

        auto [line, col] = GetLineColumnFromPosition(cursorPosition);
        if (lines.empty()) {
            lines.push_back("");
        }
        if (line < static_cast<int>(lines.size())) {
            std::string currentLine = lines[line];
            lines[line] = currentLine.substr(0, col);
            lines.insert(lines.begin() + line + 1, currentLine.substr(col));

            currentLineIndex = line + 1;
            cursorPosition = GetPositionFromLineColumn(currentLineIndex, 0);

            RebuildText();

            RequestRedraw();
            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    void UltraCanvasTextArea::InsertTab() {
        if (isReadOnly) return;

        // Insert 4 spaces for tab
        for (int i = 0; i < 4; i++) {
            InsertCharacter(' ');
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
        RequestRedraw();
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
            RequestRedraw();
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
        RequestRedraw();
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
    void UltraCanvasTextArea::SetSyntaxMode(SyntaxMode mode) {
        style.syntaxMode = mode;

        if (mode == SyntaxMode::Programming && !syntaxHighlighter) {
            syntaxHighlighter = std::make_unique<SyntaxTokenizer>();
        }

        UpdateSyntaxHighlighting();
    }

    void UltraCanvasTextArea::SetProgrammingLanguage(const std::string& language) {
        if (style.syntaxMode == SyntaxMode::Programming) {
            if (!syntaxHighlighter) {
                syntaxHighlighter = std::make_unique<SyntaxTokenizer>();
            }
            syntaxHighlighter->SetLanguage(language);
            UpdateSyntaxHighlighting();
        }
    }

    void UltraCanvasTextArea::SetProgrammingLanguageByExtension(const std::string& extension) {
        if (style.syntaxMode == SyntaxMode::Programming) {
            if (!syntaxHighlighter) {
                syntaxHighlighter = std::make_unique<SyntaxTokenizer>();
            }
            syntaxHighlighter->SetLanguageByExtension(extension);
            UpdateSyntaxHighlighting();
        }
    }

    void UltraCanvasTextArea::UpdateSyntaxHighlighting() {
        if (style.syntaxMode == SyntaxMode::Programming && syntaxHighlighter) {
            // Syntax highlighter will tokenize during rendering
            //Invalidate();
            RequestRedraw();
        }
    }

// Theme application methods
    void UltraCanvasTextArea::ApplyDarkTheme() {
        style.backgroundColor = {30, 30, 30, 255};
        style.fontColor = {220, 220, 220, 255};
        style.currentLineColor = {40, 40, 40, 255};
        style.lineNumbersColor = {100, 100, 100, 255};
        style.lineNumbersBackgroundColor = {25, 25, 25, 255};
        style.selectionColor = {60, 90, 150, 100};

        style.tokenStyles.keywordStyle.color = {86, 156, 214, 255};
        style.tokenStyles.functionStyle.color = {220, 220, 170, 255};
        style.tokenStyles.stringStyle.color = {206, 145, 120, 255};
        style.tokenStyles.commentStyle.color = {106, 153, 85, 255};
        style.tokenStyles.numberStyle.color = {181, 206, 168, 255};
    }

// Style application methods for specific languages
    void UltraCanvasTextArea::ApplyCodeStyle(const std::string& language) {
        ApplyDefaultStyle();
        SetSyntaxMode(SyntaxMode::Programming);
        SetProgrammingLanguage(language);
        style.showLineNumbers = true;
        style.fontFamily = "Courier New";
        highlightCurrentLine = true;
    }

    void UltraCanvasTextArea::ApplyDarkCodeStyle(const std::string& language) {
        ApplyDefaultStyle();
        ApplyDarkTheme();
        SetSyntaxMode(SyntaxMode::Programming);
        SetProgrammingLanguage(language);
        style.showLineNumbers = true;
        style.fontFamily = "Courier New";
        highlightCurrentLine = true;
    }

    void UltraCanvasTextArea::ApplyPlainTextStyle() {
        ApplyDefaultStyle();
        SetSyntaxMode(SyntaxMode::PlainText);
        style.showLineNumbers = false;
        style.fontFamily = "Arial";
        highlightCurrentLine = false;
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
        if (col < horizontalScrollOffset) {
            horizontalScrollOffset = col;
        } else if (col > horizontalScrollOffset + GetVisibleCharactersPerLine()) {
            horizontalScrollOffset = col - GetVisibleCharactersPerLine() + 1;
        }
    }

    void UltraCanvasTextArea::UpdateVisibleLines() {
        int textAreaHeight = GetHeight() - style.padding * 2;
        maxVisibleLines = textAreaHeight / lineHeight;
    }

    void UltraCanvasTextArea::RebuildText() {
        text.clear();
        for (size_t i = 0; i < lines.size(); i++) {
            text += lines[i];
            if (i < lines.size() - 1) {
                text += '\n';
            }
        }
    }

    int UltraCanvasTextArea::GetMaxLineLength() const {
        int maxLength = 0;
        for (const auto& line : lines) {
            maxLength = std::max(maxLength, static_cast<int>(line.length()));
        }
        return maxLength;
    }

    int UltraCanvasTextArea::GetVisibleCharactersPerLine() const {
        int textWidth = GetWidth() - style.padding * 2;
        if (style.showLineNumbers) {
            textWidth -= style.lineNumbersWidth;
        }
        return textWidth / characterWidth;
    }

    bool UltraCanvasTextArea::NeedsScrollbars() const {
        return lines.size() > static_cast<size_t>(maxVisibleLines) ||
               GetMaxLineLength() * characterWidth > GetWidth() - style.padding * 2;
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