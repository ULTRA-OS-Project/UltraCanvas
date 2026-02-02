// core/UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.1.0
// Last Modified: 2026-02-02
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace UltraCanvas {

// Constructor
    UltraCanvasTextArea::UltraCanvasTextArea(const std::string& name, int id, int x, int y,
                                             int width, int height)
            : UltraCanvasUIElement(name, id, x, y, width, height),
              cursorGraphemePosition(0),
              selectionStartGrapheme(-1),
              selectionEndGrapheme(-1),
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

        // Initialize with empty line
        lines.push_back(UCString(""));

        // Initialize style with defaults
        ApplyDefaultStyle();

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
        style.lineHeight = 1.1;
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

// ===== UTF-8 HELPER METHODS =====

    // Convert grapheme column to byte offset within a line
    size_t UltraCanvasTextArea::GraphemeToByteOffset(int lineIndex, int graphemeColumn) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return lines[lineIndex].GraphemeToByteOffset(graphemeColumn);
    }

    // Convert byte offset to grapheme column within a line
    int UltraCanvasTextArea::ByteToGraphemeColumn(int lineIndex, size_t byteOffset) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return static_cast<int>(lines[lineIndex].ByteToGraphemeIndex(byteOffset));
    }

    // Get grapheme count for a line
    int UltraCanvasTextArea::GetLineGraphemeCount(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return static_cast<int>(lines[lineIndex].Length());
    }

    // Get total grapheme count (cached)
    int UltraCanvasTextArea::GetTotalGraphemeCount() const {
        if (cachedTotalGraphemes >= 0) {
            return cachedTotalGraphemes;
        }
        
        int total = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            total += static_cast<int>(lines[i].Length());
            if (i < lines.size() - 1) {
                total++; // Count newline as one grapheme
            }
        }
        cachedTotalGraphemes = total;
        return total;
    }

// ===== POSITION CONVERSION =====

    // Convert grapheme position to line/column (both in graphemes)
    std::pair<int, int> UltraCanvasTextArea::GetLineColumnFromPosition(int graphemePosition) const {
        int line = 0;
        int col = 0;
        int currentPos = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            int lineLength = static_cast<int>(lines[i].Length());
            if (currentPos + lineLength >= graphemePosition) {
                line = static_cast<int>(i);
                col = graphemePosition - currentPos;
                break;
            }
            currentPos += lineLength + 1; // +1 for newline (counts as 1 grapheme)
            line = static_cast<int>(i);
        }

        return {line, col};
    }

    // Convert line/column (in graphemes) to grapheme position
    int UltraCanvasTextArea::GetPositionFromLineColumn(int line, int graphemeColumn) const {
        int position = 0;

        for (int i = 0; i < line && i < static_cast<int>(lines.size()); i++) {
            position += static_cast<int>(lines[i].Length()) + 1; // +1 for newline
        }

        if (line < static_cast<int>(lines.size())) {
            position += std::min(graphemeColumn, static_cast<int>(lines[line].Length()));
        }

        return position;
    }

// ===== MOUSE-TO-TEXT POSITION HELPER =====

    // Convert mouse coordinates to grapheme position, line, and column
    int UltraCanvasTextArea::GetGraphemePositionFromPoint(int mouseX, int mouseY, int& outLine, int& outCol) const {
        int relativeX = mouseX - visibleTextArea.x + horizontalScrollOffset;
        int relativeY = mouseY - visibleTextArea.y;

        outLine = firstVisibleLine + (relativeY / computedLineHeight);
        outLine = std::max(0, std::min(outLine, static_cast<int>(lines.size()) - 1));

        outCol = 0;
        if (outLine < static_cast<int>(lines.size()) && relativeX > 0) {
            auto context = GetRenderContext();
            if (context) {
                context->PushState();
                context->SetFontStyle(style.fontStyle);
                context->SetFontWeight(FontWeight::Normal);

                const std::string& lineStr = lines[outLine].Data();
                int byteIndex = std::max(0, context->GetTextIndexForXY(lineStr, relativeX, 0));
                outCol = ByteToGraphemeColumn(outLine, byteIndex);

                context->PopState();
            }
        }

        return GetPositionFromLineColumn(outLine, outCol);
    }

// ===== TEXT MANIPULATION METHODS =====

    // void UltraCanvasTextArea::SetText(const std::string& text) {
    //     SetText(UCString(text));
    // }

    void UltraCanvasTextArea::SetText(const UCString& newText) {
        textContent = newText;
        InvalidateGraphemeCache();

        // Split into lines using newline character
        lines.clear();
        lines = textContent.Split(U'\n');

        // Ensure at least one line
        if (lines.empty()) {
            lines.push_back(UCString(""));
        }

        // Reset cursor and selection
        cursorGraphemePosition = 0;
        currentLineIndex = 0;
        selectionStartGrapheme = -1;
        selectionEndGrapheme = -1;

        isNeedRecalculateVisibleArea = true;
        RequestRedraw();

        // Trigger text changed callback
        if (onTextChanged) {
            onTextChanged(textContent.Data());
        }
    }

    std::string UltraCanvasTextArea::GetText() const {
        return textContent.Data();
    }

    UCString UltraCanvasTextArea::GetTextUC() const {
        return textContent;
    }

    void UltraCanvasTextArea::InsertText(const std::string& textToInsert) {
        InsertText(UCString(textToInsert));
    }

    void UltraCanvasTextArea::InsertText(const UCString& textToInsert) {
        if (isReadOnly) return;

        SaveState();
        if (HasSelection()) {
            DeleteSelection();
        }

        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (lines.empty()) {
            lines.push_back(UCString(""));
        }

        // Insert text at cursor position (grapheme-aware)
        size_t graphemeIndex = 0;
        size_t textLen = textToInsert.Length();
        
        while (graphemeIndex < textLen) {
            std::string grapheme = textToInsert.GetGrapheme(graphemeIndex);
            
            if (grapheme == "\n") {
                // Split line at cursor
                UCString currentLine = lines[line];
                lines[line] = currentLine.Substr(0, col);
                lines.insert(lines.begin() + line + 1, currentLine.Substr(col));
                line++;
                col = 0;
            } else {
                // Insert grapheme into current line
                lines[line].Insert(col, grapheme);
                col++;
            }
            graphemeIndex++;
        }

        // Update cursor position
        cursorGraphemePosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;
        InvalidateGraphemeCache();

        // Rebuild full text
        RebuildText();

        // Trigger callback
        if (onTextChanged) {
            onTextChanged(textContent.Data());
        }
    }

    void UltraCanvasTextArea::InsertCodepoint(char32_t codepoint) {
        if (isReadOnly) return;
        
        UCString cpStr(codepoint);
        InsertText(cpStr);
    }

    void UltraCanvasTextArea::InsertCharacter(char ch) {
        if (isReadOnly) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        if (lines.empty()) {
            lines.push_back(UCString(""));
        }
        if (line < static_cast<int>(lines.size())) {
            if (ch == '\n') {
                // Split line at cursor
                UCString currentLine = lines[line];
                lines[line] = currentLine.Substr(0, col);
                lines.insert(lines.begin() + line + 1, currentLine.Substr(col));
                currentLineIndex = line + 1;
                cursorGraphemePosition++;
            } else {
                // Insert character (single byte, treated as 1 grapheme for ASCII)
                std::string charStr(1, ch);
                lines[line].Insert(col, charStr);
                cursorGraphemePosition++;
            }
            InvalidateGraphemeCache();
            RebuildText();
        }
    }

    void UltraCanvasTextArea::InsertNewLine() {
        if (isReadOnly) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (lines.empty()) {
            lines.push_back(UCString(""));
        }

        // Split current line at cursor position
        UCString currentLine = lines[line];
        lines[line] = currentLine.Substr(0, col);
        lines.insert(lines.begin() + line + 1, currentLine.Substr(col));

        // Move cursor to beginning of new line
        cursorGraphemePosition = GetPositionFromLineColumn(line + 1, 0);
        currentLineIndex = line + 1;
        InvalidateGraphemeCache();

        RebuildText();
    }

    void UltraCanvasTextArea::InsertTab() {
        if (isReadOnly) return;

        // Insert spaces for tab
        std::string tabStr(tabSize, ' ');
        InsertText(tabStr);
    }

    // Delete one grapheme cluster backward (backspace)
    void UltraCanvasTextArea::DeleteCharacterBackward() {
        if (isReadOnly || cursorGraphemePosition == 0) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (col > 0) {
            // Delete one grapheme from current line
            lines[line].Erase(col - 1, 1);
            cursorGraphemePosition--;
        } else if (line > 0) {
            // Merge with previous line
            int prevLineLength = static_cast<int>(lines[line - 1].Length());
            lines[line - 1].Append(lines[line]);
            lines.erase(lines.begin() + line);
            currentLineIndex = line - 1;
            cursorGraphemePosition = GetPositionFromLineColumn(currentLineIndex, prevLineLength);
        }

        InvalidateGraphemeCache();
        RebuildText();
        if (onTextChanged) {
            onTextChanged(textContent.Data());
        }
    }

    // Delete one grapheme cluster forward (delete key)
    void UltraCanvasTextArea::DeleteCharacterForward() {
        if (isReadOnly) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size())) {
            int lineLen = static_cast<int>(lines[line].Length());
            if (col < lineLen) {
                // Delete one grapheme from current line
                lines[line].Erase(col, 1);
            } else if (line < static_cast<int>(lines.size()) - 1) {
                // Merge with next line
                lines[line].Append(lines[line + 1]);
                lines.erase(lines.begin() + line + 1);
            }

            InvalidateGraphemeCache();
            RebuildText();
            if (onTextChanged) {
                onTextChanged(textContent.Data());
            }
        }
    }

    void UltraCanvasTextArea::DeleteSelection() {
        if (!HasSelection() || isReadOnly) return;

        SaveState();
        int startPos = std::min(selectionStartGrapheme, selectionEndGrapheme);
        int endPos = std::max(selectionStartGrapheme, selectionEndGrapheme);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        if (startLine == endLine) {
            // Delete within same line (grapheme-based)
            lines[startLine].Erase(startCol, endCol - startCol);
        } else {
            // Delete across multiple lines
            UCString newLine = lines[startLine].Substr(0, startCol);
            newLine.Append(lines[endLine].Substr(endCol));
            lines[startLine] = newLine;
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
        }

        cursorGraphemePosition = startPos;
        currentLineIndex = startLine;
        selectionStartGrapheme = -1;
        selectionEndGrapheme = -1;
        InvalidateGraphemeCache();

        RebuildText();
        if (onTextChanged) {
            onTextChanged(textContent.Data());
        }
    }
// ===== CURSOR MOVEMENT METHODS (Grapheme-aware) =====

    void UltraCanvasTextArea::MoveCursorLeft(bool selecting) {
        if (cursorGraphemePosition > 0) {
            cursorGraphemePosition--;
            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition + 1;
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorRight(bool selecting) {
        int totalGraphemes = GetTotalGraphemeCount();
        if (cursorGraphemePosition < totalGraphemes) {
            cursorGraphemePosition++;
            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition - 1;
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorUp(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line > 0) {
            line--;
            col = std::min(col, static_cast<int>(lines[line].Length()));
            cursorGraphemePosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = GetPositionFromLineColumn(line + 1, col);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorDown(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size()) - 1) {
            line++;
            col = std::min(col, static_cast<int>(lines[line].Length()));
            cursorGraphemePosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = GetPositionFromLineColumn(line - 1, col);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

     void UltraCanvasTextArea::MoveCursorWordLeft(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line >= static_cast<int>(lines.size())) return;

        // If at the beginning of a line, move to end of previous line
        if (col == 0) {
            if (line > 0) {
                line--;
                col = static_cast<int>(lines[line].Length());
                cursorGraphemePosition = GetPositionFromLineColumn(line, col);
                currentLineIndex = line;
            } else {
                // Already at the very start of the text
                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                return;
            }
        }

        const UCString& currentLine = lines[line];
        int lineLen = static_cast<int>(currentLine.Length());

        // Skip whitespace/non-word characters going left
        while (col > 0) {
            uint32_t cp = currentLine.GetCodepoint(col - 1);
            if (Unicode::IsAlphanumeric(cp) || cp == '_') break;
            col--;
        }

        // Skip word characters going left to find word start
        while (col > 0) {
            uint32_t cp = currentLine.GetCodepoint(col - 1);
            if (!Unicode::IsAlphanumeric(cp) && cp != '_') break;
            col--;
        }

        int oldPosition = cursorGraphemePosition;
        cursorGraphemePosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;

        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPosition;
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }

        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, col);
        }
    }

    void UltraCanvasTextArea::MoveCursorWordRight(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line >= static_cast<int>(lines.size())) return;

        const UCString& currentLine = lines[line];
        int lineLen = static_cast<int>(currentLine.Length());

        // If at the end of a line, move to start of next line
        if (col >= lineLen) {
            if (line < static_cast<int>(lines.size()) - 1) {
                line++;
                col = 0;
                cursorGraphemePosition = GetPositionFromLineColumn(line, col);
                currentLineIndex = line;
            } else {
                // Already at the very end of the text
                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                return;
            }
        }

        // Re-read after potential line change
        const UCString& activeLine = lines[line];
        int activeLineLen = static_cast<int>(activeLine.Length());

        // Skip whitespace/non-word characters going right
        while (col < activeLineLen) {
            uint32_t cp = activeLine.GetCodepoint(col);
            if (Unicode::IsAlphanumeric(cp) || cp == '_') break;
            col++;
        }

        // Skip word characters going right to find word end
        while (col < activeLineLen) {
            uint32_t cp = activeLine.GetCodepoint(col);
            if (!Unicode::IsAlphanumeric(cp) && cp != '_') break;
            col++;
        }

        int oldPosition = cursorGraphemePosition;
        cursorGraphemePosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;

        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPosition;
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }

        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, col);
        }
    }

    void UltraCanvasTextArea::MoveCursorPageUp(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line > 0) {
            line = std::max(0, line - maxVisibleLines);
            col = std::min(col, static_cast<int>(lines[line].Length()));
            cursorGraphemePosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = GetPositionFromLineColumn(line + maxVisibleLines, col);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorPageDown(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size() - 1)) {
            line = std::min(static_cast<int>(lines.size() - 1), line + maxVisibleLines);
            col = std::min(col, static_cast<int>(lines[line].Length()));
            cursorGraphemePosition = GetPositionFromLineColumn(line, col);
            currentLineIndex = line;

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = GetPositionFromLineColumn(line - maxVisibleLines, col);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, col);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineStart(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
            cursorGraphemePosition = GetPositionFromLineColumn(line, 0);
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            cursorGraphemePosition = GetPositionFromLineColumn(line, 0);
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, 0);
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineEnd(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size())) {
            int lineLength = static_cast<int>(lines[line].Length());

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
                cursorGraphemePosition = GetPositionFromLineColumn(line, lineLength);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                cursorGraphemePosition = GetPositionFromLineColumn(line, lineLength);
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, lineLength);
            }
        }
    }

    void UltraCanvasTextArea::MoveCursorToStart(bool selecting) {
        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
            cursorGraphemePosition = 0;
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            cursorGraphemePosition = 0;
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }
        currentLineIndex = 0;
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(0, 0);
        }
    }

    void UltraCanvasTextArea::MoveCursorToEnd(bool selecting) {
        int toLine = std::max(static_cast<int>(lines.size()) - 1, 0);
        int lineLength = static_cast<int>(lines[toLine].Length());
        
        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
            cursorGraphemePosition = GetPositionFromLineColumn(toLine, lineLength);
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            cursorGraphemePosition = GetPositionFromLineColumn(toLine, lineLength);
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }
        currentLineIndex = toLine;
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(toLine, lineLength);
        }
    }

    void UltraCanvasTextArea::SetCursorPosition(int graphemePosition) {
        cursorGraphemePosition = std::max(0, std::min(graphemePosition, GetTotalGraphemeCount()));
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        currentLineIndex = line;
        RequestRedraw();
    }

// ===== SELECTION METHODS =====

    void UltraCanvasTextArea::SelectAll() {
        selectionStartGrapheme = 0;
        selectionEndGrapheme = GetTotalGraphemeCount();
        cursorGraphemePosition = selectionEndGrapheme;
        RequestRedraw();
    }

    void UltraCanvasTextArea::SelectLine(int lineIndex) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            selectionStartGrapheme = GetPositionFromLineColumn(lineIndex, 0);
            selectionEndGrapheme = GetPositionFromLineColumn(lineIndex, static_cast<int>(lines[lineIndex].Length()));
            cursorGraphemePosition = selectionEndGrapheme;
            currentLineIndex = lineIndex;
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::SelectWord() {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        
        if (line >= static_cast<int>(lines.size())) return;
        
        const UCString& currentLine = lines[line];
        int lineLen = static_cast<int>(currentLine.Length());
        
        if (lineLen == 0) return;

        // Find word start
        int wordStart = col;
        while (wordStart > 0) {
            uint32_t cp = currentLine.GetCodepoint(wordStart - 1);
            if (!Unicode::IsAlphanumeric(cp)  && cp != '_') break;
            wordStart--;
        }

        // Find word end
        int wordEnd = col;
        while (wordEnd < lineLen) {
            uint32_t cp = currentLine.GetCodepoint(wordEnd);
            if (!Unicode::IsAlphanumeric(cp) && cp != '_') break;
            wordEnd++;
        }

        // Set selection
        selectionStartGrapheme = GetPositionFromLineColumn(line, wordStart);
        selectionEndGrapheme = GetPositionFromLineColumn(line, wordEnd);
        cursorGraphemePosition = selectionEndGrapheme;

        RequestRedraw();
    }

    void UltraCanvasTextArea::SetSelection(int startGrapheme, int endGrapheme) {
        selectionStartGrapheme = startGrapheme;
        selectionEndGrapheme = endGrapheme;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearSelection() {
        selectionStartGrapheme = -1;
        selectionEndGrapheme = -1;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::HasSelection() const {
        return selectionStartGrapheme >= 0 && selectionEndGrapheme >= 0 && 
               selectionStartGrapheme != selectionEndGrapheme;
    }
    
    std::string UltraCanvasTextArea::GetSelectedText() const {
        return GetSelectedTextUC().Data();
    }

    UCString UltraCanvasTextArea::GetSelectedTextUC() const {
        if (!HasSelection()) return UCString("");
        
        int startPos = std::min(selectionStartGrapheme, selectionEndGrapheme);
        int endPos = std::max(selectionStartGrapheme, selectionEndGrapheme);
        
        return textContent.Substr(startPos, endPos - startPos);
    }

// ===== CLIPBOARD OPERATIONS =====

    void UltraCanvasTextArea::CopySelection() {
        if (!HasSelection()) return;

        std::string selectedText = GetSelectedText();
        SetClipboardText(selectedText);
    }

    void UltraCanvasTextArea::CutSelection() {
        if (!HasSelection() || isReadOnly) return;

        CopySelection();
        DeleteSelection();
    }

    void UltraCanvasTextArea::PasteClipboard() {
        if (isReadOnly) return;

        std::string clipboardText;
        auto result = GetClipboardText(clipboardText);
        if (result && !clipboardText.empty()) {
            InsertText(clipboardText);
        }
    }

// ===== LINE OPERATIONS =====

    int UltraCanvasTextArea::GetCurrentLine() const {
        return currentLineIndex;
    }

    int UltraCanvasTextArea::GetCurrentColumn() const {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        return col;
    }

    int UltraCanvasTextArea::GetLineCount() const {
        return static_cast<int>(lines.size());
    }

    std::string UltraCanvasTextArea::GetLine(int lineIndex) const {
        return GetLineUC(lineIndex).Data();
    }

    UCString UltraCanvasTextArea::GetLineUC(int lineIndex) const {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            return lines[lineIndex];
        }
        return UCString("");
    }

    void UltraCanvasTextArea::SetLine(int lineIndex, const std::string& text) {
        SetLine(lineIndex, UCString(text));
    }

    void UltraCanvasTextArea::SetLine(int lineIndex, const UCString& text) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            lines[lineIndex] = text;
            InvalidateGraphemeCache();
            RebuildText();
        }
    }

    void UltraCanvasTextArea::GoToLine(int lineNumber) {
        int lineIndex = std::max(0, std::min(lineNumber - 1, static_cast<int>(lines.size()) - 1));
        cursorGraphemePosition = GetPositionFromLineColumn(lineIndex, 0);
        currentLineIndex = lineIndex;
        EnsureCursorVisible();
        RequestRedraw();
    }
// ===== RENDERING METHODS =====

    void UltraCanvasTextArea::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        if (isNeedRecalculateVisibleArea) {
            CalculateVisibleArea();
        }

        ctx->PushState();
        DrawBackground(ctx);

        if (style.showLineNumbers) {
            DrawLineNumbers(ctx);
        }

        if (style.highlightSyntax && syntaxTokenizer) {
            DrawHighlightedText(ctx);
        } else {
            DrawPlainText(ctx);
        }

        if (HasSelection()) {
            DrawSelection(ctx);
        }
        
        if (!searchHighlights.empty()) {
            DrawSearchHighlights(ctx);
        }

        if (IsFocused() && cursorVisible && !isReadOnly) {
            DrawCursor(ctx);
        }

        DrawBorder(ctx);
        DrawScrollbars(ctx);
        ctx->PopState();
    }

    void UltraCanvasTextArea::DrawBorder(IRenderContext* context) {
        auto bounds = GetBounds();
        if (style.borderWidth > 0) {
            context->DrawFilledRectangle(bounds, Colors::Transparent, style.borderWidth, style.borderColor);
        }
    }

    void UltraCanvasTextArea::DrawPlainText(IRenderContext* context) {
        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetTextPaint(style.fontColor);
        context->ClipRect(visibleTextArea);

        int startLine = std::max(0, firstVisibleLine - 1);
        int endLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;

        for (int i = startLine; i < endLine; i++) {
            const UCString& line = lines[i];
            if (!line.Empty()) {
                int y = baseY + (i - startLine) * computedLineHeight;
                const std::string& lineStr = line.Data();
                
                if (horizontalScrollOffset > 0) {
                    context->DrawText(lineStr, visibleTextArea.x - horizontalScrollOffset, y);
                } else {
                    context->DrawText(lineStr, visibleTextArea.x, y);
                }
            }
        }
        context->PopState();
    }

    void UltraCanvasTextArea::DrawHighlightedText(IRenderContext* context) {
        if (!syntaxTokenizer) return;
        context->PushState();
        context->ClipRect(visibleTextArea);
        context->SetFontStyle(style.fontStyle);

        int startLine = std::max(0, firstVisibleLine - 1);
        int endLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;

        for (int i = startLine; i < endLine; i++) {
            const UCString& line = lines[i];
            int textY = baseY + (i - startLine) * computedLineHeight;

            if (!line.Empty()) {
                const std::string& lineStr = line.Data();
                auto tokens = syntaxTokenizer->TokenizeLine(lineStr);
                int x = visibleTextArea.x - horizontalScrollOffset;

                for (const auto& token : tokens) {
                    context->SetFontWeight(GetStyleForTokenType(token.type).bold ? FontWeight::Bold : FontWeight::Normal);
                    int tokenWidth = context->GetTextLineWidth(token.text);

                    if (x + tokenWidth >= visibleTextArea.x && x <= visibleTextArea.x + visibleTextArea.width) {
                        TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                        context->SetTextPaint(tokenStyle.color);
                        context->DrawText(token.text, x, textY);
                    }
                    x += tokenWidth;
                }
            }
        }
        context->PopState();
    }

    void UltraCanvasTextArea::DrawLineNumbers(IRenderContext* context) {
        auto bounds = GetBounds();

        context->SetFillPaint(style.lineNumbersBackgroundColor);
        context->FillRectangle(bounds.x, bounds.y, style.lineNumbersWidth, bounds.height);

        context->SetStrokePaint(style.borderColor);
        context->SetStrokeWidth(1);
        context->DrawLine(bounds.x + style.lineNumbersWidth, bounds.y,
                          bounds.x + style.lineNumbersWidth, bounds.y + bounds.height);

        context->SetFontStyle(style.fontStyle);

        Rect2Di lineNumberClipRect = {bounds.x, visibleTextArea.y, style.lineNumbersWidth, visibleTextArea.height};
        context->PushState();
        context->ClipRect(lineNumberClipRect);

        int startLine = std::max(0, firstVisibleLine - 1);
        int endLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;

        for (int i = startLine; i < endLine; i++) {
            int numY = baseY + (i - startLine) * computedLineHeight;

            if (i == currentLineIndex) {
                context->SetTextPaint(style.fontColor);
                context->SetFontWeight(FontWeight::Bold);
            } else {
                context->SetTextPaint(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }
            context->SetTextAlignment(TextAlignment::Right);
            context->DrawTextInRect(std::to_string(i + 1), bounds.x, numY, style.lineNumbersWidth, computedLineHeight);
        }
        context->PopState();
    }

    void UltraCanvasTextArea::DrawSelection(IRenderContext* context) {
        if (!HasSelection()) return;

        int startPos = std::min(selectionStartGrapheme, selectionEndGrapheme);
        int endPos = std::max(selectionStartGrapheme, selectionEndGrapheme);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        int textX = visibleTextArea.x;
        int visibleStartLine = std::max(0, firstVisibleLine - 1);
        int visibleEndLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);

        for (int line = startLine; line <= endLine; line++) {
            if (line < visibleStartLine || line >= visibleEndLine) continue;
            if (line >= static_cast<int>(lines.size())) break;

            int lineY = visibleTextArea.y + (line - firstVisibleLine) * computedLineHeight;

            int selStart = (line == startLine) ? startCol : 0;
            int selEnd = (line == endLine) ? endCol : static_cast<int>(lines[line].Length());

            int selX = textX - horizontalScrollOffset;
            if (selStart > 0) {
                UCString textBeforeSelection = lines[line].Substr(0, selStart);
                selX += MeasureTextWidth(textBeforeSelection);
            }

            int selWidth = 0;
            if (selEnd > selStart) {
                UCString selectedText = lines[line].Substr(selStart, selEnd - selStart);
                selWidth = MeasureTextWidth(selectedText);
            }

            context->SetFillPaint(style.selectionColor);
            context->FillRectangle(selX, lineY, selWidth, computedLineHeight);
        }
    }

    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFillPaint(style.backgroundColor);
        context->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        if (highlightCurrentLine) {
            int visibleStartLine = firstVisibleLine - 1;
            int visibleEndLine = firstVisibleLine + maxVisibleLines;

            if (currentLineIndex >= visibleStartLine && currentLineIndex <= visibleEndLine) {
                context->SetFillPaint(style.currentLineHighlightColor);
                int lineY = visibleTextArea.y + (currentLineIndex - firstVisibleLine) * computedLineHeight;
                int highlightX = style.showLineNumbers ? bounds.x + style.lineNumbersWidth : bounds.x;

                context->PushState();
                context->ClipRect(visibleTextArea);
                context->FillRectangle(highlightX, lineY,
                                       bounds.width - (style.showLineNumbers ? style.lineNumbersWidth : 0),
                                       computedLineHeight);
                context->PopState();
            }
        }
    }

    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        int visibleStartLine = firstVisibleLine - 1;
        int visibleEndLine = firstVisibleLine + maxVisibleLines;

        if (line < visibleStartLine || line > visibleEndLine) return;

        int cursorX = visibleTextArea.x - horizontalScrollOffset;
        if (col > 0 && line < static_cast<int>(lines.size())) {
            UCString textBeforeCursor = lines[line].Substr(0, col);
            cursorX += MeasureTextWidth(textBeforeCursor);
        }
        if (cursorX > visibleTextArea.x + visibleTextArea.width) return;

        int cursorY = visibleTextArea.y + (line - firstVisibleLine) * computedLineHeight;

        context->PushState();
        context->ClipRect(visibleTextArea);
        context->SetStrokeWidth(2);
        context->DrawLine(cursorX, cursorY, cursorX, cursorY + computedLineHeight, style.cursorColor);
        context->PopState();
    }

    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetBounds();

        if (IsNeedVerticalScrollbar()) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);
            int totalLines = static_cast<int>(lines.size());
            int visibleLines = maxVisibleLines;

            int thumbHeight = std::max(20, (visibleLines * scrollbarHeight) / totalLines);
            int maxThumbY = scrollbarHeight - thumbHeight;
            int thumbY = bounds.y;

            if (totalLines > visibleLines && maxThumbY > 0) {
                thumbY = bounds.y + (firstVisibleLine * maxThumbY) / (totalLines - visibleLines);
            }

            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(scrollbarX, bounds.y, 15, scrollbarHeight);

            verticalScrollThumb = {scrollbarX, thumbY, 15, thumbHeight};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(scrollbarX + 2, thumbY + 2, 11, thumbHeight - 4);
        }

        if (IsNeedHorizontalScrollbar()) {
            float scrollbarY = static_cast<float>(bounds.y + bounds.height - 15);
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));

            float thumbWidthRatio = static_cast<float>(visibleTextArea.width) / static_cast<float>(maxLineWidth);
            float thumbWidth = std::max(20.0f, scrollbarWidth * thumbWidthRatio);

            float maxThumbX = scrollbarWidth - thumbWidth;
            float thumbX = static_cast<float>(bounds.x);

            if (maxLineWidth > visibleTextArea.width && maxThumbX > 0) {
                float scrollRatio = static_cast<float>(horizontalScrollOffset) /
                                    static_cast<float>(maxLineWidth - visibleTextArea.width);
                thumbX = static_cast<float>(bounds.x) + scrollRatio * maxThumbX;
            }

            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(static_cast<float>(bounds.x), scrollbarY, scrollbarWidth, 15.0f);

            horizontalScrollThumb = {static_cast<int>(thumbX), static_cast<int>(scrollbarY), static_cast<int>(thumbWidth), 15};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(thumbX + 2, scrollbarY + 2, thumbWidth - 4, 11.0f);
        }
    }

    void UltraCanvasTextArea::DrawSearchHighlights(IRenderContext* context) {
        if (searchHighlights.empty()) return;
        
        Color highlightColor = {255, 255, 0, 100};
        int visibleStartLine = std::max(0, firstVisibleLine - 1);
        int visibleEndLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);

        for (const auto& [startPos, endPos] : searchHighlights) {
            auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
            auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

            for (int line = startLine; line <= endLine; line++) {
                if (line < visibleStartLine || line >= visibleEndLine) continue;
                if (line >= static_cast<int>(lines.size())) break;

                int lineY = visibleTextArea.y + (line - firstVisibleLine) * computedLineHeight;

                int hlStart = (line == startLine) ? startCol : 0;
                int hlEnd = (line == endLine) ? endCol : static_cast<int>(lines[line].Length());

                int hlX = visibleTextArea.x - horizontalScrollOffset;
                if (hlStart > 0) {
                    UCString textBeforeHighlight = lines[line].Substr(0, hlStart);
                    hlX += MeasureTextWidth(textBeforeHighlight);
                }

                int hlWidth = 0;
                if (hlEnd > hlStart) {
                    UCString highlightedText = lines[line].Substr(hlStart, hlEnd - hlStart);
                    hlWidth = MeasureTextWidth(highlightedText);
                }

                context->SetFillPaint(highlightColor);
                context->FillRectangle(hlX, lineY, hlWidth, computedLineHeight);
            }
        }
    }

    void UltraCanvasTextArea::DrawAutoComplete(IRenderContext* context) {
        // Placeholder for auto-complete rendering
    }

    void UltraCanvasTextArea::DrawMarkers(IRenderContext* context) {
        // Placeholder for error/warning marker rendering
    }
// ===== EVENT HANDLING =====

    bool UltraCanvasTextArea::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseDoubleClick:
                return HandleMouseDoubleClick(event);
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
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

    bool UltraCanvasTextArea::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // Scrollbar thumb dragging takes priority
        if (IsNeedVerticalScrollbar() && verticalScrollThumb.Contains(event.x, event.y)) {
            isDraggingVerticalThumb = true;
            dragStartOffset.y = event.globalY - verticalScrollThumb.y;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        if (IsNeedHorizontalScrollbar() && horizontalScrollThumb.Contains(event.x, event.y)) {
            isDraggingHorizontalThumb = true;
            dragStartOffset.x = event.globalX - horizontalScrollThumb.x;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        SetFocus(true);

        // --- Click counting for single / double / triple click ---
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();
        int dx = std::abs(event.x - lastClickX);
        int dy = std::abs(event.y - lastClickY);

        if (elapsed <= MultiClickTimeThresholdMs &&
            dx <= MultiClickDistanceThreshold &&
            dy <= MultiClickDistanceThreshold) {
            clickCount++;
        } else {
            clickCount = 1;
        }

        lastClickTime = now;
        lastClickX = event.x;
        lastClickY = event.y;

        // --- Compute clicked text position ---
        int clickedLine = 0;
        int clickedCol = 0;
        int clickedGraphemePos = GetGraphemePositionFromPoint(event.x, event.y, clickedLine, clickedCol);

        // --- Act on click count ---
        if (clickCount >= 3) {
            // Triple click: select entire line
            clickCount = 3; // Cap to prevent quad-click escalation
            HandleMouseTripleClick(event);
            return true;
        }

        if (clickCount == 2) {
            // Double click: select word (position cursor first so SelectWord works)
            cursorGraphemePosition = clickedGraphemePos;
            currentLineIndex = clickedLine;
            SelectWord();
            // Set anchor to selection start for potential drag-extend
            selectionAnchorGrapheme = selectionStartGrapheme;
            isSelectingText = true;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        // --- Single click ---
        cursorGraphemePosition = clickedGraphemePos;
        currentLineIndex = clickedLine;

        if (event.shift && selectionStartGrapheme >= 0) {
            // Shift-click: extend existing selection
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            // Plain click: clear selection, start new potential drag selection
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
            selectionAnchorGrapheme = cursorGraphemePosition;
        }

        // Begin text drag selection tracking
        isSelectingText = true;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);

        RequestRedraw();
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseDoubleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // Position cursor at click point, then select word
        int clickedLine = 0;
        int clickedCol = 0;
        cursorGraphemePosition = GetGraphemePositionFromPoint(event.x, event.y, clickedLine, clickedCol);
        currentLineIndex = clickedLine;

        SelectWord();

        // Prepare for potential drag-extend after double-click
        selectionAnchorGrapheme = selectionStartGrapheme;
        isSelectingText = true;
        clickCount = 2;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseTripleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // Position cursor at click point, then select entire line
        int clickedLine = 0;
        int clickedCol = 0;
        GetGraphemePositionFromPoint(event.x, event.y, clickedLine, clickedCol);

        SelectLine(clickedLine);

        // Prepare for potential drag-extend after triple-click
        selectionAnchorGrapheme = selectionStartGrapheme;
        isSelectingText = true;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseMove(const UCEvent& event) {
        // Scrollbar thumb dragging
        if (isDraggingVerticalThumb) {
            auto bounds = GetBounds();
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);
            int thumbHeight = verticalScrollThumb.height;
            int maxThumbY = scrollbarHeight - thumbHeight;
            int totalLines = static_cast<int>(lines.size());
            int visibleLines = maxVisibleLines;

            int newThumbY = event.globalY - dragStartOffset.y - bounds.y;
            newThumbY = std::max(0, std::min(newThumbY, maxThumbY));

            if (maxThumbY > 0 && totalLines > visibleLines) {
                firstVisibleLine = (newThumbY * (totalLines - visibleLines)) / maxThumbY;
                firstVisibleLine = std::max(0, std::min(firstVisibleLine, totalLines - visibleLines));
            }

            RequestRedraw();
            return true;
        }

        if (isDraggingHorizontalThumb) {
            auto bounds = GetBounds();
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));
            float thumbWidth = static_cast<float>(horizontalScrollThumb.width);
            float maxThumbX = scrollbarWidth - thumbWidth;

            float newThumbX = static_cast<float>(event.globalX - dragStartOffset.x - bounds.x);
            newThumbX = std::max(0.0f, std::min(newThumbX, maxThumbX));

            if (maxThumbX > 0 && maxLineWidth > visibleTextArea.width) {
                horizontalScrollOffset = static_cast<int>((newThumbX / maxThumbX) *
                                                          static_cast<float>(maxLineWidth - visibleTextArea.width));
                horizontalScrollOffset = std::max(0, std::min(horizontalScrollOffset, maxLineWidth - visibleTextArea.width));
            }

            RequestRedraw();
            return true;
        }

        // --- Text drag selection ---
        if (isSelectingText && selectionAnchorGrapheme >= 0) {
            int dragLine = 0;
            int dragCol = 0;
            int dragGraphemePos = GetGraphemePositionFromPoint(event.x, event.y, dragLine, dragCol);

            // Update selection from anchor to current drag position
            selectionStartGrapheme = selectionAnchorGrapheme;
            selectionEndGrapheme = dragGraphemePos;
            cursorGraphemePosition = dragGraphemePos;
            currentLineIndex = dragLine;

            // Auto-scroll when dragging near edges
            if (event.y < visibleTextArea.y) {
                // Mouse above visible area — scroll up
                ScrollUp(1);
            } else if (event.y > visibleTextArea.y + visibleTextArea.height) {
                // Mouse below visible area — scroll down
                ScrollDown(1);
            }

            if (event.x < visibleTextArea.x) {
                // Mouse left of visible area — scroll left
                ScrollLeft(1);
            } else if (event.x > visibleTextArea.x + visibleTextArea.width) {
                // Mouse right of visible area — scroll right
                ScrollRight(1);
            }

            RequestRedraw();

            if (onSelectionChanged) {
                onSelectionChanged(selectionStartGrapheme, selectionEndGrapheme);
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasTextArea::HandleMouseUp(const UCEvent& event) {
        bool wasHandled = false;

        // Finalize scrollbar dragging
        if (isDraggingVerticalThumb || isDraggingHorizontalThumb) {
            isDraggingVerticalThumb = false;
            isDraggingHorizontalThumb = false;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
            return true;
        }

        // Finalize text selection dragging
        if (isSelectingText) {
            isSelectingText = false;
            selectionAnchorGrapheme = -1;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);

            // If start equals end, there's no real selection — clear it
            if (selectionStartGrapheme >= 0 && selectionStartGrapheme == selectionEndGrapheme) {
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }

            RequestRedraw();
            wasHandled = true;
        }

        return wasHandled;
    }

    bool UltraCanvasTextArea::HandleMouseDrag(const UCEvent& event) {
        return HandleMouseMove(event);
    }

    bool UltraCanvasTextArea::HandleMouseWheel(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        int scrollAmount = 3;

        if (event.wheelDelta > 0) {
            firstVisibleLine = std::max(0, firstVisibleLine - scrollAmount);
        } else {
            int maxFirstLine = std::max(0, static_cast<int>(lines.size()) - maxVisibleLines);
            firstVisibleLine = std::min(maxFirstLine, firstVisibleLine + scrollAmount);
        }

        RequestRedraw();
        return true;
    }

    bool UltraCanvasTextArea::HandleKeyDown(const UCEvent& event) {
        bool handled = true;

        switch (event.virtualKey) {
            case UCKeys::Left:
                if (event.ctrl && !event.alt) {
                    MoveCursorWordLeft(event.shift);
                } else {
                    MoveCursorLeft(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Right:
                if (event.ctrl && !event.alt) {
                    MoveCursorWordRight(event.shift);
                } else {
                    MoveCursorRight(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Up:
                if (event.ctrl && !event.alt) {
                    ScrollUp(1);
                } else {
                    MoveCursorUp(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Down:
                if (event.ctrl && !event.alt) {
                    ScrollDown(1);
                } else {
                    MoveCursorDown(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Home:
                if (event.ctrl) {
                    MoveCursorToStart(event.shift);
                } else {
                    MoveCursorToLineStart(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::End:
                if (event.ctrl) {
                    MoveCursorToEnd(event.shift);
                } else {
                    MoveCursorToLineEnd(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::PageUp:
                MoveCursorPageUp(event.shift);
                EnsureCursorVisible();
                break;
            case UCKeys::PageDown:
                MoveCursorPageDown(event.shift);
                EnsureCursorVisible();
                break;
            case UCKeys::Backspace:
                if (HasSelection()) DeleteSelection();
                else DeleteCharacterBackward();
                EnsureCursorVisible();
                break;
            case UCKeys::Delete:
                if (!event.shift && event.ctrl && !event.alt) {
                    CutSelection();
                } else if (HasSelection()) {
                    DeleteSelection();
                } else {
                    DeleteCharacterForward();
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Enter:
                if (HasSelection()) DeleteSelection();
                InsertNewLine();
                EnsureCursorVisible();
                break;
            case UCKeys::Tab:
                if (HasSelection()) DeleteSelection();
                InsertTab();
                EnsureCursorVisible();
                break;
            case UCKeys::A:
                if (!event.shift && event.ctrl && !event.alt) {
                    SelectAll();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::C:
                if (!event.shift && event.ctrl && !event.alt) {
                    CopySelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::X:
                if (!event.shift && event.ctrl && !event.alt) {
                    CutSelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::V:
                if (!event.shift && event.ctrl && !event.alt) { 
                    PasteClipboard(); 
                    EnsureCursorVisible(); 
                } else {
                    handled = false;
                }
                break;
            case UCKeys::Insert:
                if (event.shift && !event.ctrl && !event.alt) { 
                    PasteClipboard(); EnsureCursorVisible(); 
                } else if (!event.shift && event.ctrl && !event.alt) {
                    CopySelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::Z:
                if (event.ctrl) {
                    if (event.shift) Redo();
                    else Undo();
                    EnsureCursorVisible();
                } else handled = false;
                break;
            case UCKeys::Y:
                if (event.ctrl) { Redo(); EnsureCursorVisible(); }
                else handled = false;
                break;
            default:
                handled = false;
                break;
        }

        // Handle UTF-8 text input
        if (!handled && !event.ctrl && !event.alt) {
            if (!event.text.empty()) {
                if (HasSelection()) {
                    DeleteSelection();
                }
                InsertText(event.text);
                EnsureCursorVisible();
                handled = true;
            } else if (event.character != 0 && std::isprint(event.character)) {
                if (HasSelection()) {
                    DeleteSelection();
                }
                InsertCharacter(static_cast<char>(event.character));
                EnsureCursorVisible();
                handled = true;
            }
        }

        return handled;
    }

// ===== HELPER METHODS =====

    void UltraCanvasTextArea::EnsureCursorVisible() {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < firstVisibleLine) {
            firstVisibleLine = line;
        } else if (line >= firstVisibleLine + maxVisibleLines) {
            firstVisibleLine = line - maxVisibleLines + 1;
        }
        
        if (col > 0 && line < static_cast<int>(lines.size())) {
            UCString textToCursor = lines[line].Substr(0, col);
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
        auto ctx = GetRenderContext();
        if (!ctx) return;
        
        visibleTextArea = GetBounds();
        visibleTextArea.x += style.padding;
        visibleTextArea.y += style.padding;
        visibleTextArea.width -= style.padding * 2;
        visibleTextArea.height -= style.padding * 2;
        
        if (style.showLineNumbers) {
            visibleTextArea.x += (style.lineNumbersWidth + 5);
            visibleTextArea.width -= (style.lineNumbersWidth + 5);
        }

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        computedLineHeight = static_cast<int>(static_cast<float>(ctx->GetTextLineHeight("M")) * style.lineHeight);
        maxLineWidth = 0;
        for (const auto& line : lines) {
            maxLineWidth = std::max(maxLineWidth, ctx->GetTextLineWidth(line.Data()));
        }
        ctx->PopState();

        if (IsNeedHorizontalScrollbar()) {
            visibleTextArea.height -= 15;
            maxVisibleLines = std::max(1, visibleTextArea.height / computedLineHeight);
            if (IsNeedVerticalScrollbar()) {
                visibleTextArea.width -= 15;
            }
        } else {
            maxVisibleLines = std::max(1, visibleTextArea.height / computedLineHeight);
            if (IsNeedVerticalScrollbar()) {
                visibleTextArea.width -= 15;
                if (IsNeedHorizontalScrollbar()) {
                    visibleTextArea.height -= 15;
                    maxVisibleLines = std::max(1, visibleTextArea.height / computedLineHeight);
                }
            }
        }
        isNeedRecalculateVisibleArea = false;
    }

    void UltraCanvasTextArea::RebuildText() {
        textContent.Clear();
        for (size_t i = 0; i < lines.size(); i++) {
            textContent.Append(lines[i]);
            if (i < lines.size() - 1) {
                textContent.Append(U'\n');
            }
        }
        InvalidateGraphemeCache();
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(textContent.Data());
        }
    }

    int UltraCanvasTextArea::MeasureTextWidth(const std::string& txt) const {
        auto context = GetRenderContext();
        if (!context || txt.empty()) return 0;

        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetFontWeight(FontWeight::Normal);
        int width = context->GetTextLineWidth(txt);
        context->PopState();
        return width;
    }

    int UltraCanvasTextArea::MeasureTextWidth(const UCString& txt) const {
        return MeasureTextWidth(txt.Data());
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

    const TokenStyle& UltraCanvasTextArea::GetStyleForTokenType(TokenType type) const {
        switch (type) {
            case TokenType::Keyword: return style.tokenStyles.keywordStyle;
            case TokenType::Function: return style.tokenStyles.functionStyle;
            case TokenType::String: return style.tokenStyles.stringStyle;
            case TokenType::Character: return style.tokenStyles.characterStyle;
            case TokenType::Comment: return style.tokenStyles.commentStyle;
            case TokenType::Number: return style.tokenStyles.numberStyle;
            case TokenType::Identifier: return style.tokenStyles.identifierStyle;
            case TokenType::Operator: return style.tokenStyles.operatorStyle;
            case TokenType::Constant: return style.tokenStyles.constantStyle;
            case TokenType::Preprocessor: return style.tokenStyles.preprocessorStyle;
            case TokenType::Punctuation: return style.tokenStyles.punctuationStyle;
            case TokenType::Builtin: return style.tokenStyles.builtinStyle;
            case TokenType::Assembly: return style.tokenStyles.assemblyStyle;
            case TokenType::Register: return style.tokenStyles.registerStyle;
            default: return style.tokenStyles.defaultStyle;
        }
    }

    void UltraCanvasTextArea::Invalidate() {
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

// ===== SCROLLING =====

    void UltraCanvasTextArea::ScrollTo(int line) {
        firstVisibleLine = std::max(0, std::min(line, static_cast<int>(lines.size()) - maxVisibleLines));
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollUp(int lineCount) {
        firstVisibleLine = std::max(0, firstVisibleLine - lineCount);
        RequestRedraw();
    }

void UltraCanvasTextArea::ScrollDown(int lineCount) {
        int maxFirstLine = std::max(0, static_cast<int>(lines.size()) - maxVisibleLines);
        firstVisibleLine = std::min(maxFirstLine, firstVisibleLine + lineCount);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollLeft(int chars) {
        horizontalScrollOffset = std::max(0, horizontalScrollOffset - chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollRight(int chars) {
        int maxOffset = std::max(0, maxLineWidth - visibleTextArea.width);
        horizontalScrollOffset = std::min(maxOffset, horizontalScrollOffset + chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetFirstVisibleLine(int line) {
        firstVisibleLine = std::max(0, std::min(line, static_cast<int>(lines.size()) - 1));
        RequestRedraw();
    }
// ===== UNDO/REDO =====

    void UltraCanvasTextArea::SaveState() {
        TextState state;
        state.text = textContent;
        state.cursorGraphemePosition = cursorGraphemePosition;
        state.selectionStartGrapheme = selectionStartGrapheme;
        state.selectionEndGrapheme = selectionEndGrapheme;

        undoStack.push_back(state);
        if (undoStack.size() > maxUndoStackSize) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void UltraCanvasTextArea::Undo() {
        if (undoStack.empty()) return;

        TextState currentState;
        currentState.text = textContent;
        currentState.cursorGraphemePosition = cursorGraphemePosition;
        currentState.selectionStartGrapheme = selectionStartGrapheme;
        currentState.selectionEndGrapheme = selectionEndGrapheme;
        redoStack.push_back(currentState);

        TextState previousState = undoStack.back();
        undoStack.pop_back();

        SetText(previousState.text);
        cursorGraphemePosition = previousState.cursorGraphemePosition;
        selectionStartGrapheme = previousState.selectionStartGrapheme;
        selectionEndGrapheme = previousState.selectionEndGrapheme;

        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        currentLineIndex = line;
        RequestRedraw();
    }

    void UltraCanvasTextArea::Redo() {
        if (redoStack.empty()) return;

        TextState currentState;
        currentState.text = textContent;
        currentState.cursorGraphemePosition = cursorGraphemePosition;
        currentState.selectionStartGrapheme = selectionStartGrapheme;
        currentState.selectionEndGrapheme = selectionEndGrapheme;
        undoStack.push_back(currentState);

        TextState nextState = redoStack.back();
        redoStack.pop_back();

        SetText(nextState.text);
        cursorGraphemePosition = nextState.cursorGraphemePosition;
        selectionStartGrapheme = nextState.selectionStartGrapheme;
        selectionEndGrapheme = nextState.selectionEndGrapheme;

        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        currentLineIndex = line;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::CanUndo() const { return !undoStack.empty(); }
    bool UltraCanvasTextArea::CanRedo() const { return !redoStack.empty(); }

// ===== SYNTAX HIGHLIGHTING =====

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

    bool UltraCanvasTextArea::SetProgrammingLanguageByExtension(const std::string& extension) {
        if (!syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        auto result = syntaxTokenizer->SetLanguageByExtension(extension);
        if (style.highlightSyntax) {
            RequestRedraw();
        }
        return result;
    }

    const std::string UltraCanvasTextArea::GetCurrentProgrammingLanguage() {
        if (!syntaxTokenizer) return "Plain text";
        return syntaxTokenizer->GetCurrentProgrammingLanguage();
    }

    void UltraCanvasTextArea::SetSyntaxTheme(const std::string& theme) {
        // Theme application would go here
        RequestRedraw();
    }

    void UltraCanvasTextArea::UpdateSyntaxHighlighting() {
        RequestRedraw();
    }

// ===== THEMES =====

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
        style.tokenStyles.constantStyle.color = {0xa2, 0xcc, 0x9d, 255};
        style.tokenStyles.preprocessorStyle.color = {0xb5, 0x89, 0xbd, 255};
        style.tokenStyles.builtinStyle.color = {0x4c, 0xbb, 0xc9, 255};
        style.tokenStyles.defaultStyle.color = {210, 210, 210, 255};

        RequestRedraw();
    }

    void UltraCanvasTextArea::ApplyCustomTheme(const TextAreaStyle& customStyle) {
        style = customStyle;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ApplyCodeStyle(const std::string& language) {
        SetHighlightSyntax(true);
        SetShowLineNumbers(true);
        SetHighlightCurrentLine(true);
        SetProgrammingLanguage(language);
    }

    void UltraCanvasTextArea::ApplyDarkCodeStyle(const std::string& language) {
        ApplyDarkTheme();
        ApplyCodeStyle(language);
    }

    void UltraCanvasTextArea::ApplyPlainTextStyle() {
        SetHighlightSyntax(false);
        SetShowLineNumbers(false);
        SetHighlightCurrentLine(false);
    }

// ===== SEARCH =====

    void UltraCanvasTextArea::FindText(const std::string& searchText, bool caseSensitive) {
        lastSearchText = searchText;
        lastSearchCaseSensitive = caseSensitive;
        lastSearchPosition = cursorGraphemePosition;
        FindNext();
    }

    void UltraCanvasTextArea::FindNext() {
        if (lastSearchText.empty()) return;

        UCString searchUC(lastSearchText);
        size_t foundPos = textContent.Find(searchUC, lastSearchPosition + 1);
        
        if (foundPos == UCString::npos && lastSearchPosition > 0) {
            foundPos = textContent.Find(searchUC, 0);
        }

        if (foundPos != UCString::npos) {
            selectionStartGrapheme = static_cast<int>(foundPos);
            selectionEndGrapheme = static_cast<int>(foundPos + searchUC.Length());
            cursorGraphemePosition = selectionEndGrapheme;
            lastSearchPosition = static_cast<int>(foundPos);
            
            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;
            EnsureCursorVisible();
        }
    }

    void UltraCanvasTextArea::FindPrevious() {
        if (lastSearchText.empty()) return;

        UCString searchUC(lastSearchText);
        size_t searchFrom = lastSearchPosition > 0 ? lastSearchPosition - 1 : textContent.Length();
        size_t foundPos = textContent.RFind(searchUC, searchFrom);
        
        if (foundPos == UCString::npos && lastSearchPosition < static_cast<int>(textContent.Length())) {
            foundPos = textContent.RFind(searchUC);
        }

        if (foundPos != UCString::npos) {
            selectionStartGrapheme = static_cast<int>(foundPos);
            selectionEndGrapheme = static_cast<int>(foundPos + searchUC.Length());
            cursorGraphemePosition = selectionEndGrapheme;
            lastSearchPosition = static_cast<int>(foundPos);
            
            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;
            EnsureCursorVisible();
        }
    }

    void UltraCanvasTextArea::ReplaceText(const std::string& findText, const std::string& replaceText, bool all) {
        if (findText.empty()) return;

        SaveState();
        UCString findUC(findText);
        UCString replaceUC(replaceText);

        if (all) {
            size_t pos = 0;
            while ((pos = textContent.Find(findUC, pos)) != UCString::npos) {
                textContent.Replace(pos, findUC.Length(), replaceUC);
                pos += replaceUC.Length();
            }
            SetText(textContent);
        } else {
            if (HasSelection()) {
                UCString selected = GetSelectedTextUC();
                if (selected == findUC) {
                    DeleteSelection();
                    InsertText(replaceUC);
                }
            }
            FindNext();
        }
    }

    void UltraCanvasTextArea::HighlightMatches(const std::string& searchText) {
        searchHighlights.clear();
        if (searchText.empty()) {
            RequestRedraw();
            return;
        }

        UCString searchUC(searchText);
        size_t pos = 0;
        while ((pos = textContent.Find(searchUC, pos)) != UCString::npos) {
            searchHighlights.push_back({static_cast<int>(pos), static_cast<int>(pos + searchUC.Length())});
            pos += searchUC.Length();
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearHighlights() {
        searchHighlights.clear();
        RequestRedraw();
    }

// ===== BOOKMARKS =====

    void UltraCanvasTextArea::ToggleBookmark(int lineIndex) {
        auto it = std::find(bookmarks.begin(), bookmarks.end(), lineIndex);
        if (it != bookmarks.end()) {
            bookmarks.erase(it);
        } else {
            bookmarks.push_back(lineIndex);
            std::sort(bookmarks.begin(), bookmarks.end());
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::NextBookmark() {
        if (bookmarks.empty()) return;
        for (int bm : bookmarks) {
            if (bm > currentLineIndex) {
                GoToLine(bm + 1);
                return;
            }
        }
        GoToLine(bookmarks[0] + 1);
    }

    void UltraCanvasTextArea::PreviousBookmark() {
        if (bookmarks.empty()) return;
        for (auto it = bookmarks.rbegin(); it != bookmarks.rend(); ++it) {
            if (*it < currentLineIndex) {
                GoToLine(*it + 1);
                return;
            }
        }
        GoToLine(bookmarks.back() + 1);
    }

    void UltraCanvasTextArea::ClearAllBookmarks() {
        bookmarks.clear();
        RequestRedraw();
    }

// ===== MARKERS =====

    void UltraCanvasTextArea::AddErrorMarker(int lineIndex, const std::string& message) {
        markers.push_back({Marker::Error, lineIndex, message});
        RequestRedraw();
    }

    void UltraCanvasTextArea::AddWarningMarker(int lineIndex, const std::string& message) {
        markers.push_back({Marker::Warning, lineIndex, message});
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearMarkers() {
        markers.clear();
        RequestRedraw();
    }

// ===== AUTO-COMPLETE =====

    void UltraCanvasTextArea::ShowAutoComplete(const std::vector<std::string>& suggestions) {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::HideAutoComplete() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::AcceptAutoComplete() {
        // Implementation placeholder
    }

// ===== BRACKET MATCHING =====

    void UltraCanvasTextArea::HighlightMatchingBrackets() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::JumpToMatchingBracket() {
        // Implementation placeholder
    }

// ===== INDENTATION =====

    void UltraCanvasTextArea::IndentSelection() {
        if (!HasSelection()) return;
        
        auto [startLine, startCol] = GetLineColumnFromPosition(std::min(selectionStartGrapheme, selectionEndGrapheme));
        auto [endLine, endCol] = GetLineColumnFromPosition(std::max(selectionStartGrapheme, selectionEndGrapheme));

        SaveState();
        std::string indent(tabSize, ' ');
        
        for (int i = startLine; i <= endLine; i++) {
            lines[i].Insert(0, indent);
        }
        
        InvalidateGraphemeCache();
        RebuildText();
    }

    void UltraCanvasTextArea::UnindentSelection() {
        if (!HasSelection()) return;
        
        auto [startLine, startCol] = GetLineColumnFromPosition(std::min(selectionStartGrapheme, selectionEndGrapheme));
        auto [endLine, endCol] = GetLineColumnFromPosition(std::max(selectionStartGrapheme, selectionEndGrapheme));

        SaveState();
        
        for (int i = startLine; i <= endLine; i++) {
            int spacesToRemove = 0;
            for (int j = 0; j < tabSize && j < static_cast<int>(lines[i].Length()); j++) {
                std::string grapheme = lines[i].GetGrapheme(j);
                if (grapheme == " " || grapheme == "\t") {
                    spacesToRemove++;
                } else {
                    break;
                }
            }
            if (spacesToRemove > 0) {
                lines[i].Erase(0, spacesToRemove);
            }
        }
        
        InvalidateGraphemeCache();
        RebuildText();
    }

    void UltraCanvasTextArea::AutoIndentLine(int lineIndex) {
        // Implementation placeholder
    }

// ===== COMMENTS =====

    void UltraCanvasTextArea::ToggleLineComment() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::ToggleBlockComment() {
        // Implementation placeholder
    }

// ===== FACTORY FUNCTIONS =====

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

    std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(const std::string& name, int id,
                                                               int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle("Markdown");
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateJSONEditor(const std::string& name, int id,
                                                           int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle("JSON");
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateXMLEditor(const std::string& name, int id,
                                                          int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle("XML");
        return editor;
    }

} // namespace UltraCanvas