// core/UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.1.2
// Last Modified: 2026-02-04
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstring>

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
        lines.push_back(std::string());

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
        style.fontStyle.fontSize = 11;
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
        return utf8_cp_to_byte(lines[lineIndex], graphemeColumn);
    }

    // Convert byte offset to grapheme column within a line
    int UltraCanvasTextArea::ByteToGraphemeColumn(int lineIndex, size_t byteOffset) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return static_cast<int>(utf8_byte_to_cp(lines[lineIndex], byteOffset));
    }

    // Get grapheme count for a line
    int UltraCanvasTextArea::GetLineGraphemeCount(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return static_cast<int>(utf8_length(lines[lineIndex]));
    }

    // Get total grapheme count (cached)
    int UltraCanvasTextArea::GetTotalGraphemeCount() const {
        if (cachedTotalGraphemes >= 0) {
            return cachedTotalGraphemes;
        }
        
        int total = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            total += static_cast<int>(utf8_length(lines[i]));
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
            int lineLength = static_cast<int>(utf8_length(lines[i]));
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
            position += static_cast<int>(utf8_length(lines[i])) + 1; // +1 for newline
        }

        if (line < static_cast<int>(lines.size())) {
            position += std::min(graphemeColumn, static_cast<int>(utf8_length(lines[line])));
        }

        return position;
    }

// ===== MOUSE-TO-TEXT POSITION HELPER =====

    // Convert mouse coordinates to grapheme position, line, and column
    int UltraCanvasTextArea::GetGraphemePositionFromPoint(int mouseX, int mouseY, int& outLine, int& outCol) const {
        int relativeX = mouseX - visibleTextArea.x;
        if (!wordWrap) relativeX += horizontalScrollOffset;
        int relativeY = mouseY - visibleTextArea.y;

        // Map Y to display line index
        int dlCount = GetDisplayLineCount();
        int displayLineIdx = firstVisibleLine + (relativeY / computedLineHeight);
        displayLineIdx = std::max(0, std::min(displayLineIdx, dlCount - 1));

        const auto& dl = displayLines[displayLineIdx];
        outLine = dl.logicalLine;
        outCol = dl.startGrapheme; // default

        if (outLine < static_cast<int>(lines.size())) {
            auto context = GetRenderContext();
            if (context) {
                context->PushState();
                context->SetFontStyle(style.fontStyle);
                context->SetFontWeight(FontWeight::Normal);

                // Get the substring for this display line
                int segLen = dl.endGrapheme - dl.startGrapheme;
                std::string segment;
                if (segLen > 0) {
                    if (dl.startGrapheme == 0 && dl.endGrapheme == GetLineGraphemeCount(dl.logicalLine)) {
                        segment = lines[outLine];
                    } else {
                        segment = utf8_substr(lines[outLine], dl.startGrapheme, segLen);
                    }
                }

                if (relativeX <= 0 || segment.empty()) {
                    outCol = dl.startGrapheme;
                } else {
                    int segWidth = context->GetTextLineWidth(segment);

                    if (relativeX >= segWidth) {
                        outCol = dl.endGrapheme;
                    } else {
                        // GetTextIndexForXY returns byte offset within the segment
                        int byteIndex = std::max(0, context->GetTextIndexForXY(segment, relativeX, 0));
                        int graphemeInSegment = static_cast<int>(utf8_byte_to_cp(segment, byteIndex));
                        outCol = dl.startGrapheme + graphemeInSegment;
                    }
                }

                context->PopState();
            } else {
                if (relativeX > 0) {
                    outCol = dl.endGrapheme;
                }
            }
        }

        return GetPositionFromLineColumn(outLine, outCol);
    }

// ===== TEXT MANIPULATION METHODS =====

    void UltraCanvasTextArea::SetText(const std::string& newText, bool runNotifications) {
        textContent = newText;
        InvalidateGraphemeCache();
        lines = utf8_split(textContent, '\n');
        if (lines.empty()) {
            lines.push_back(std::string());
        }
        cursorGraphemePosition = 0;
        currentLineIndex = 0;
        selectionStartGrapheme = -1;
        selectionEndGrapheme = -1;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
        if (runNotifications && onTextChanged) {
            onTextChanged(textContent);
        }
    }

    std::string UltraCanvasTextArea::GetText() const {
        return textContent;
    }

    void UltraCanvasTextArea::InsertText(const std::string& textToInsert) {
        if (isReadOnly) return;

        SaveState();
        if (HasSelection()) {
            DeleteSelection();
        }

        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (lines.empty()) {
            lines.push_back(std::string());
        }

        const char* p = textToInsert.c_str();
        const char* end = p + textToInsert.size();
        while (p < end) {
            const char* next = g_utf8_next_char(p);
            std::string ch(p, static_cast<size_t>(next - p));
            if (ch == "\n") {
                std::string currentLine = lines[line];
                lines[line] = utf8_substr(currentLine, 0, col);
                lines.insert(lines.begin() + line + 1, utf8_substr(currentLine, col));
                line++;
                col = 0;
            } else {
                utf8_insert(lines[line], col, ch);
                col++;
            }
            p = next;
        }

        cursorGraphemePosition = GetPositionFromLineColumn(line, col);
        currentLineIndex = line;
        InvalidateGraphemeCache();

        RebuildText();
    }

    void UltraCanvasTextArea::InsertCodepoint(char32_t codepoint) {
        if (isReadOnly) return;

        InsertText(utf8_encode(codepoint));
    }

    void UltraCanvasTextArea::InsertCharacter(char ch) {
        if (isReadOnly) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        if (lines.empty()) {
            lines.push_back(std::string());
        }
        if (line < static_cast<int>(lines.size())) {
            if (ch == '\n') {
                // Split line at cursor
                std::string currentLine = lines[line];
                lines[line] = utf8_substr(currentLine, 0, col);
                lines.insert(lines.begin() + line + 1, utf8_substr(currentLine, col));
                currentLineIndex = line + 1;
                cursorGraphemePosition++;
            } else {
                // Insert character (single byte, treated as 1 grapheme for ASCII)
                std::string charStr(1, ch);
                utf8_insert(lines[line], col, charStr);
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
            lines.push_back(std::string());
        }

        // Split current line at cursor position
        std::string currentLine = lines[line];
        lines[line] = utf8_substr(currentLine, 0, col);
        lines.insert(lines.begin() + line + 1, utf8_substr(currentLine, col));

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
            utf8_erase(lines[line], col - 1, 1);
            cursorGraphemePosition--;
        } else if (line > 0) {
            // Merge with previous line
            int prevLineLength = static_cast<int>(utf8_length(lines[line - 1]));
            lines[line - 1].append(lines[line]);
            lines.erase(lines.begin() + line);
            currentLineIndex = line - 1;
            cursorGraphemePosition = GetPositionFromLineColumn(currentLineIndex, prevLineLength);
        }

        InvalidateGraphemeCache();
        RebuildText();
    }

    // Delete one grapheme cluster forward (delete key)
    void UltraCanvasTextArea::DeleteCharacterForward() {
        if (isReadOnly) return;

        SaveState();
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size())) {
            int lineLen = static_cast<int>(utf8_length(lines[line]));
            if (col < lineLen) {
                // Delete one grapheme from current line
                utf8_erase(lines[line], col, 1);
            } else if (line < static_cast<int>(lines.size()) - 1) {
                // Merge with next line
                lines[line].append(lines[line + 1]);
                lines.erase(lines.begin() + line + 1);
            }

            InvalidateGraphemeCache();
            RebuildText();
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
            utf8_erase(lines[startLine], startCol, endCol - startCol);
        } else {
            // Delete across multiple lines
            std::string newLine = utf8_substr(lines[startLine], 0, startCol);
            newLine.append(utf8_substr(lines[endLine], endCol));
            lines[startLine] = newLine;
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
        }

        cursorGraphemePosition = startPos;
        currentLineIndex = startLine;
        selectionStartGrapheme = -1;
        selectionEndGrapheme = -1;
        InvalidateGraphemeCache();

        RebuildText();
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

        if (wordWrap) {
            int curDL = GetDisplayLineForCursor(line, col);
            if (curDL > 0) {
                int oldPos = cursorGraphemePosition;
                const auto& curDLInfo = displayLines[curDL];
                const auto& prevDL = displayLines[curDL - 1];
                int colInDisplayLine = col - curDLInfo.startGrapheme;
                int prevDLLength = prevDL.endGrapheme - prevDL.startGrapheme;
                // If prev display line has a continuation (current display line is on same logical line),
                // clamp to avoid landing at the wrap boundary which maps to the next segment
                bool prevHasContinuation = (curDLInfo.logicalLine == prevDL.logicalLine);
                int maxOffset = prevHasContinuation ? std::max(0, prevDLLength - 1) : prevDLLength;
                int targetCol = prevDL.startGrapheme + std::min(colInDisplayLine, maxOffset);

                cursorGraphemePosition = GetPositionFromLineColumn(prevDL.logicalLine, targetCol);
                currentLineIndex = prevDL.logicalLine;

                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPos;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                RequestRedraw();
                auto [newL, newC] = GetLineColumnFromPosition(cursorGraphemePosition);
                if (onCursorPositionChanged) {
                    onCursorPositionChanged(newL, newC);
                }
            }
        } else {
            if (line > 0) {
                line--;
                col = std::min(col, utf8_length(lines[line]));
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
    }

    void UltraCanvasTextArea::MoveCursorDown(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (wordWrap) {
            int curDL = GetDisplayLineForCursor(line, col);
            if (curDL < GetDisplayLineCount() - 1) {
                int oldPos = cursorGraphemePosition;
                const auto& curDLInfo = displayLines[curDL];
                const auto& nextDL = displayLines[curDL + 1];
                int colInDisplayLine = col - curDLInfo.startGrapheme;
                int nextDLLength = nextDL.endGrapheme - nextDL.startGrapheme;
                // If next display line has a continuation, clamp to avoid boundary
                bool nextHasContinuation = (curDL + 2 < static_cast<int>(displayLines.size()) &&
                                            displayLines[curDL + 2].logicalLine == nextDL.logicalLine);
                int maxOffset = nextHasContinuation ? std::max(0, nextDLLength - 1) : nextDLLength;
                int targetCol = nextDL.startGrapheme + std::min(colInDisplayLine, maxOffset);

                cursorGraphemePosition = GetPositionFromLineColumn(nextDL.logicalLine, targetCol);
                currentLineIndex = nextDL.logicalLine;

                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPos;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                RequestRedraw();
                auto [newL, newC] = GetLineColumnFromPosition(cursorGraphemePosition);
                if (onCursorPositionChanged) {
                    onCursorPositionChanged(newL, newC);
                }
            }
        } else {
            if (line < static_cast<int>(lines.size()) - 1) {
                line++;
                col = std::min(col, utf8_length(lines[line]));
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
    }

     void UltraCanvasTextArea::MoveCursorWordLeft(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line >= static_cast<int>(lines.size())) return;

        // If at the beginning of a line, move to end of previous line
        if (col == 0) {
            if (line > 0) {
                line--;
                col = utf8_length(lines[line]);
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

        const std::string& currentLine = lines[line];
        int lineLen = utf8_length(currentLine);

        // Skip whitespace/non-word characters going left
        while (col > 0) {
            gunichar cp = utf8_get_cp(currentLine, col - 1);
            if (g_unichar_isalnum(cp) || cp == '_') break;
            col--;
        }

        // Skip word characters going left to find word start
        while (col > 0) {
            gunichar cp = utf8_get_cp(currentLine, col - 1);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
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

        const std::string& currentLine = lines[line];
        int lineLen = utf8_length(currentLine);

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
        const std::string& activeLine = lines[line];
        int activeLineLen = utf8_length(activeLine);

        // Skip whitespace/non-word characters going right
        while (col < activeLineLen) {
            gunichar cp = utf8_get_cp(activeLine, col);
            if (g_unichar_isalnum(cp) || cp == '_') break;
            col++;
        }

        // Skip word characters going right to find word end
        while (col < activeLineLen) {
            gunichar cp = utf8_get_cp(activeLine, col);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
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

        if (wordWrap) {
            int curDL = GetDisplayLineForCursor(line, col);
            int targetDL = std::max(0, curDL - maxVisibleLines);
            if (targetDL != curDL) {
                int oldPos = cursorGraphemePosition;
                const auto& curDLInfo = displayLines[curDL];
                const auto& dl = displayLines[targetDL];
                int colInDisplayLine = col - curDLInfo.startGrapheme;
                int targetCol = dl.startGrapheme +
                    std::min(colInDisplayLine, dl.endGrapheme - dl.startGrapheme);

                cursorGraphemePosition = GetPositionFromLineColumn(dl.logicalLine, targetCol);
                currentLineIndex = dl.logicalLine;

                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPos;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                RequestRedraw();
                auto [newL, newC] = GetLineColumnFromPosition(cursorGraphemePosition);
                if (onCursorPositionChanged) {
                    onCursorPositionChanged(newL, newC);
                }
            }
        } else {
            if (line > 0) {
                line = std::max(0, line - maxVisibleLines);
                col = std::min(col, utf8_length(lines[line]));
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
    }

    void UltraCanvasTextArea::MoveCursorPageDown(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (wordWrap) {
            int curDL = GetDisplayLineForCursor(line, col);
            int targetDL = std::min(GetDisplayLineCount() - 1, curDL + maxVisibleLines);
            if (targetDL != curDL) {
                int oldPos = cursorGraphemePosition;
                const auto& curDLInfo = displayLines[curDL];
                const auto& dl = displayLines[targetDL];
                int colInDisplayLine = col - curDLInfo.startGrapheme;
                int targetCol = dl.startGrapheme +
                    std::min(colInDisplayLine, dl.endGrapheme - dl.startGrapheme);

                cursorGraphemePosition = GetPositionFromLineColumn(dl.logicalLine, targetCol);
                currentLineIndex = dl.logicalLine;

                if (selecting) {
                    if (selectionStartGrapheme < 0) selectionStartGrapheme = oldPos;
                    selectionEndGrapheme = cursorGraphemePosition;
                } else {
                    selectionStartGrapheme = -1;
                    selectionEndGrapheme = -1;
                }
                RequestRedraw();
                auto [newL, newC] = GetLineColumnFromPosition(cursorGraphemePosition);
                if (onCursorPositionChanged) {
                    onCursorPositionChanged(newL, newC);
                }
            }
        } else {
            if (line < static_cast<int>(lines.size() - 1)) {
                line = std::min(static_cast<int>(lines.size() - 1), line + maxVisibleLines);
                col = std::min(col, utf8_length(lines[line]));
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
    }

    void UltraCanvasTextArea::MoveCursorToLineStart(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        int targetCol;
        if (wordWrap) {
            int curDL = GetDisplayLineForCursor(line, col);
            targetCol = displayLines[curDL].startGrapheme;
        } else {
            targetCol = 0;
        }

        if (selecting) {
            if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
            cursorGraphemePosition = GetPositionFromLineColumn(line, targetCol);
            selectionEndGrapheme = cursorGraphemePosition;
        } else {
            cursorGraphemePosition = GetPositionFromLineColumn(line, targetCol);
            selectionStartGrapheme = -1;
            selectionEndGrapheme = -1;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, targetCol);
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineEnd(bool selecting) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        if (line < static_cast<int>(lines.size())) {
            int targetCol;
            if (wordWrap) {
                int curDL = GetDisplayLineForCursor(line, col);
                // Check if this is the last display line of the logical line
                bool isLastSegment = (curDL + 1 >= static_cast<int>(displayLines.size()) ||
                                      displayLines[curDL + 1].logicalLine != line);
                if (isLastSegment) {
                    targetCol = displayLines[curDL].endGrapheme;  // end of logical line
                } else {
                    targetCol = displayLines[curDL].endGrapheme - 1;  // last char of this segment
                }
            } else {
                targetCol = utf8_length(lines[line]);
            }

            if (selecting) {
                if (selectionStartGrapheme < 0) selectionStartGrapheme = cursorGraphemePosition;
                cursorGraphemePosition = GetPositionFromLineColumn(line, targetCol);
                selectionEndGrapheme = cursorGraphemePosition;
            } else {
                cursorGraphemePosition = GetPositionFromLineColumn(line, targetCol);
                selectionStartGrapheme = -1;
                selectionEndGrapheme = -1;
            }
            RequestRedraw();
            if (onCursorPositionChanged) {
                onCursorPositionChanged(line, targetCol);
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
        int lineLength = utf8_length(lines[toLine]);
        
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
            selectionEndGrapheme = GetPositionFromLineColumn(lineIndex, utf8_length(lines[lineIndex]));
            cursorGraphemePosition = selectionEndGrapheme;
            currentLineIndex = lineIndex;
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::SelectWord() {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
        
        if (line >= static_cast<int>(lines.size())) return;
        
        const std::string& currentLine = lines[line];
        int lineLen = utf8_length(currentLine);

        if (lineLen == 0) return;

        // Find word start
        int wordStart = col;
        while (wordStart > 0) {
            gunichar cp = utf8_get_cp(currentLine, wordStart - 1);
            if (!g_unichar_isalnum(cp)  && cp != '_') break;
            wordStart--;
        }

        // Find word end
        int wordEnd = col;
        while (wordEnd < lineLen) {
            gunichar cp = utf8_get_cp(currentLine, wordEnd);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
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
        if (!HasSelection()) return std::string();
        int startPos = std::min(selectionStartGrapheme, selectionEndGrapheme);
        int endPos = std::max(selectionStartGrapheme, selectionEndGrapheme);
        return utf8_substr(textContent, startPos, endPos - startPos);
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
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            return lines[lineIndex];
        }
        return std::string();
    }

    void UltraCanvasTextArea::SetLine(int lineIndex, const std::string& text) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            lines[lineIndex] = text;
            InvalidateGraphemeCache();
            RebuildText();
        }
    }

    void UltraCanvasTextArea::GoToLine(int lineNumber) {
        int lineIndex = std::max(0, std::min(lineNumber, static_cast<int>(lines.size()) - 1));
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

        if (markdownHybridMode) {
            DrawMarkdownHybridText(ctx);
        } else if (style.highlightSyntax && syntaxTokenizer) {
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

        int dlCount = GetDisplayLineCount();
        int startDL = std::max(0, firstVisibleLine - 1);
        int endDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startDL) * computedLineHeight;

        for (int di = startDL; di < endDL; di++) {
            const auto& dl = displayLines[di];
            int y = baseY + (di - startDL) * computedLineHeight;
            int segLen = dl.endGrapheme - dl.startGrapheme;
            if (segLen <= 0) continue;

            std::string segment;
            if (dl.startGrapheme == 0 && dl.endGrapheme == GetLineGraphemeCount(dl.logicalLine)) {
                segment = lines[dl.logicalLine]; // full line, no substr needed
            } else {
                segment = utf8_substr(lines[dl.logicalLine], dl.startGrapheme, segLen);
            }

            int x = visibleTextArea.x;
            if (!wordWrap) {
                x -= horizontalScrollOffset;
            }
            context->DrawText(segment, x, y);
        }
        context->PopState();
    }

    void UltraCanvasTextArea::DrawHighlightedText(IRenderContext* context) {
        if (!syntaxTokenizer) return;
        context->PushState();
        context->ClipRect(visibleTextArea);
        context->SetFontStyle(style.fontStyle);

        int dlCount = GetDisplayLineCount();
        int startDL = std::max(0, firstVisibleLine - 1);
        int endDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startDL) * computedLineHeight;

        // Cache tokenized lines to avoid re-tokenizing the same logical line
        int lastTokenizedLogicalLine = -1;
        std::vector<SyntaxTokenizer::Token> cachedTokens;

        for (int di = startDL; di < endDL; di++) {
            const auto& dl = displayLines[di];
            int textY = baseY + (di - startDL) * computedLineHeight;

            const std::string& fullLine = lines[dl.logicalLine];
            if (fullLine.empty()) continue;

            // Tokenize logical line (cache to avoid re-tokenizing for wrapped segments)
            if (dl.logicalLine != lastTokenizedLogicalLine) {
                cachedTokens = syntaxTokenizer->TokenizeLine(fullLine);
                lastTokenizedLogicalLine = dl.logicalLine;
            }

            int x = visibleTextArea.x;
            if (!wordWrap) x -= horizontalScrollOffset;

            // Walk tokens, tracking grapheme position within the logical line
            int tokenStartGrapheme = 0;
            for (const auto& token : cachedTokens) {
                int tokenLen = static_cast<int>(utf8_length(token.text));
                int tokenEndGrapheme = tokenStartGrapheme + tokenLen;

                // Check overlap with this display line's grapheme range
                int overlapStart = std::max(tokenStartGrapheme, dl.startGrapheme);
                int overlapEnd = std::min(tokenEndGrapheme, dl.endGrapheme);

                if (overlapStart < overlapEnd) {
                    // Extract the visible portion of this token
                    std::string visibleText;
                    if (overlapStart == tokenStartGrapheme && overlapEnd == tokenEndGrapheme) {
                        visibleText = token.text;
                    } else {
                        int localStart = overlapStart - tokenStartGrapheme;
                        int localLen = overlapEnd - overlapStart;
                        visibleText = utf8_substr(token.text, localStart, localLen);
                    }

                    context->SetFontWeight(GetStyleForTokenType(token.type).bold ?
                                           FontWeight::Bold : FontWeight::Normal);
                    int tokenWidth = context->GetTextLineWidth(visibleText);

                    if (x + tokenWidth >= visibleTextArea.x &&
                        x <= visibleTextArea.x + visibleTextArea.width) {
                        TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                        context->SetTextPaint(tokenStyle.color);
                        context->DrawText(visibleText, x, textY);
                    }
                    x += tokenWidth;
                }

                tokenStartGrapheme = tokenEndGrapheme;

                // Early exit if we've passed the display line's range
                if (tokenStartGrapheme >= dl.endGrapheme) break;
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

        int dlCount = GetDisplayLineCount();
        int startDL = std::max(0, firstVisibleLine - 1);
        int endDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);
        int baseY = visibleTextArea.y - (firstVisibleLine - startDL) * computedLineHeight;

        for (int di = startDL; di < endDL; di++) {
            const auto& dl = displayLines[di];
            int numY = baseY + (di - startDL) * computedLineHeight;

            // Only show line number on the first display line of each logical line
            if (dl.startGrapheme != 0) continue;

            if (dl.logicalLine == currentLineIndex) {
                context->SetTextPaint(style.fontColor);
                context->SetFontWeight(FontWeight::Bold);
            } else {
                context->SetTextPaint(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }
            context->SetTextAlignment(TextAlignment::Right);
            context->DrawTextInRect(std::to_string(dl.logicalLine + 1), bounds.x, numY, style.lineNumbersWidth, computedLineHeight);
        }
        context->PopState();
    }

    void UltraCanvasTextArea::DrawSelection(IRenderContext* context) {
        if (!HasSelection()) return;

        int startPos = std::min(selectionStartGrapheme, selectionEndGrapheme);
        int endPos = std::max(selectionStartGrapheme, selectionEndGrapheme);

        auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
        auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

        int dlCount = GetDisplayLineCount();
        int visStartDL = std::max(0, firstVisibleLine - 1);
        int visEndDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);

        for (int di = visStartDL; di < visEndDL; di++) {
            const auto& dl = displayLines[di];
            int logLine = dl.logicalLine;

            // Check if this display line's logical line is within selection range
            if (logLine < startLine || logLine > endLine) continue;

            int lineY = visibleTextArea.y + (di - firstVisibleLine) * computedLineHeight;

            // Determine the selection range in grapheme coords within this logical line
            int selStartInLine = (logLine == startLine) ? startCol : 0;
            int selEndInLine = (logLine == endLine) ? endCol : GetLineGraphemeCount(logLine);

            // Clip to this display line's grapheme range
            int dlSelStart = std::max(selStartInLine, dl.startGrapheme);
            int dlSelEnd = std::min(selEndInLine, dl.endGrapheme);

            if (dlSelStart >= dlSelEnd) continue;

            // Compute X positions relative to display line start
            int selX = visibleTextArea.x;
            if (!wordWrap) selX -= horizontalScrollOffset;

            if (dlSelStart > dl.startGrapheme) {
                std::string before = utf8_substr(lines[logLine], dl.startGrapheme,
                                                  dlSelStart - dl.startGrapheme);
                selX += MeasureTextWidth(before);
            }

            std::string selectedSegment = utf8_substr(lines[logLine], dlSelStart,
                                                        dlSelEnd - dlSelStart);
            int selWidth = MeasureTextWidth(selectedSegment);

            context->SetFillPaint(style.selectionColor);
            context->FillRectangle(selX, lineY, selWidth, computedLineHeight);
        }
    }

    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFillPaint(style.backgroundColor);
        context->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        if (highlightCurrentLine) {
            int visStartDL = firstVisibleLine - 1;
            int visEndDL = firstVisibleLine + maxVisibleLines;
            int highlightX = style.showLineNumbers ? bounds.x + style.lineNumbersWidth : bounds.x;
            int highlightW = bounds.width - (style.showLineNumbers ? style.lineNumbersWidth : 0);

            context->PushState();
            context->ClipRect(visibleTextArea);
            context->SetFillPaint(style.currentLineHighlightColor);

            // Highlight all display lines belonging to the current logical line
            for (int di = std::max(0, visStartDL); di <= visEndDL && di < GetDisplayLineCount(); di++) {
                if (displayLines[di].logicalLine == currentLineIndex) {
                    int lineY = visibleTextArea.y + (di - firstVisibleLine) * computedLineHeight;
                    context->FillRectangle(highlightX, lineY, highlightW, computedLineHeight);
                }
            }
            context->PopState();
        }
    }

    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);

        int displayLine = GetDisplayLineForCursor(line, col);
        int visStartDL = firstVisibleLine - 1;
        int visEndDL = firstVisibleLine + maxVisibleLines;

        if (displayLine < visStartDL || displayLine > visEndDL) return;

        const auto& dl = displayLines[displayLine];

        // X position: measure from display line start to cursor column
        int cursorX = visibleTextArea.x;
        if (!wordWrap) cursorX -= horizontalScrollOffset;

        int colWithinDisplayLine = col - dl.startGrapheme;
        if (colWithinDisplayLine > 0 && line < static_cast<int>(lines.size())) {
            std::string textBeforeCursor = utf8_substr(lines[line], dl.startGrapheme, colWithinDisplayLine);
            cursorX += MeasureTextWidth(textBeforeCursor);
        }
        if (cursorX > visibleTextArea.x + visibleTextArea.width) return;

        int cursorY = visibleTextArea.y + (displayLine - firstVisibleLine) * computedLineHeight;

        context->PushState();
        context->SetStrokeWidth(2);
        context->DrawLine(cursorX, cursorY, cursorX, cursorY + computedLineHeight, style.cursorColor);
        context->PopState();
    }

    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetBounds();

        if (IsNeedVerticalScrollbar()) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);
            int totalLines = GetDisplayLineCount();
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
        int dlCount = GetDisplayLineCount();
        int visStartDL = std::max(0, firstVisibleLine - 1);
        int visEndDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);

        for (const auto& [startPos, endPos] : searchHighlights) {
            auto [startLine, startCol] = GetLineColumnFromPosition(startPos);
            auto [endLine, endCol] = GetLineColumnFromPosition(endPos);

            for (int di = visStartDL; di < visEndDL; di++) {
                const auto& dl = displayLines[di];
                int logLine = dl.logicalLine;

                if (logLine < startLine || logLine > endLine) continue;

                int lineY = visibleTextArea.y + (di - firstVisibleLine) * computedLineHeight;

                int hlStartInLine = (logLine == startLine) ? startCol : 0;
                int hlEndInLine = (logLine == endLine) ? endCol : GetLineGraphemeCount(logLine);

                // Clip to this display line's grapheme range
                int dlHlStart = std::max(hlStartInLine, dl.startGrapheme);
                int dlHlEnd = std::min(hlEndInLine, dl.endGrapheme);

                if (dlHlStart >= dlHlEnd) continue;

                int hlX = visibleTextArea.x;
                if (!wordWrap) hlX -= horizontalScrollOffset;

                if (dlHlStart > dl.startGrapheme) {
                    std::string before = utf8_substr(lines[logLine], dl.startGrapheme,
                                                      dlHlStart - dl.startGrapheme);
                    hlX += MeasureTextWidth(before);
                }

                std::string hlText = utf8_substr(lines[logLine], dlHlStart, dlHlEnd - dlHlStart);
                int hlWidth = MeasureTextWidth(hlText);

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
            int totalLines = GetDisplayLineCount();
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
            int maxFirstLine = std::max(0, GetDisplayLineCount() - maxVisibleLines);
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
            case UCKeys::Escape:
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

        // Find the cursor's display line
        int displayLine = GetDisplayLineForCursor(line, col);

        // Vertical scroll using display line index
        if (displayLine < firstVisibleLine) {
            firstVisibleLine = displayLine;
        } else if (displayLine >= firstVisibleLine + maxVisibleLines) {
            firstVisibleLine = displayLine - maxVisibleLines + 1;
        }

        // Horizontal scroll (only when not word-wrapping)
        if (!wordWrap) {
            if (col > 0 && line < static_cast<int>(lines.size())) {
                std::string textToCursor = utf8_substr(lines[line], 0, col);
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
        if (wordWrap) {
            maxLineWidth = visibleTextArea.width;
        } else {
            maxLineWidth = 0;
            for (const auto& line : lines) {
                maxLineWidth = std::max(maxLineWidth, ctx->GetTextLineWidth(line));
            }
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

        RecalculateDisplayLines();
        isNeedRecalculateVisibleArea = false;
    }

// ===== WORD WRAP DISPLAY LINE CALCULATION =====

    void UltraCanvasTextArea::SetWordWrap(bool wrap) {
        if (wordWrap == wrap) return;

        // Remember which logical line is at the top of the viewport
        int topLogicalLine = 0;
        if (!displayLines.empty() && firstVisibleLine < static_cast<int>(displayLines.size())) {
            topLogicalLine = displayLines[firstVisibleLine].logicalLine;
        }

        wordWrap = wrap;
        horizontalScrollOffset = 0;
        isNeedRecalculateVisibleArea = true;

        // Temporarily set firstVisibleLine to the logical line index;
        // RecalculateDisplayLines will convert it to the display line index.
        firstVisibleLine = topLogicalLine;
        needFirstVisibleLineFixup = true;

        RequestRedraw();
    }

    void UltraCanvasTextArea::RecalculateDisplayLines() {
        displayLines.clear();

        if (!wordWrap) {
            // 1:1 mapping: each logical line is one display line
            for (int i = 0; i < static_cast<int>(lines.size()); i++) {
                displayLines.push_back({i, 0, GetLineGraphemeCount(i)});
            }
            return;
        }

        // Word wrap mode: wrap long lines
        int wrapWidth = visibleTextArea.width;
        if (wrapWidth <= 0) {
            // Fallback: 1:1 mapping
            for (int i = 0; i < static_cast<int>(lines.size()); i++) {
                displayLines.push_back({i, 0, GetLineGraphemeCount(i)});
            }
            return;
        }

        auto context = GetRenderContext();
        if (!context) {
            for (int i = 0; i < static_cast<int>(lines.size()); i++) {
                displayLines.push_back({i, 0, GetLineGraphemeCount(i)});
            }
            return;
        }

        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetFontWeight(FontWeight::Normal);

        for (int logLine = 0; logLine < static_cast<int>(lines.size()); logLine++) {
            const std::string& line = lines[logLine];
            int lineGraphemeCount = GetLineGraphemeCount(logLine);

            // Empty line: still produces one display line
            if (lineGraphemeCount == 0) {
                displayLines.push_back({logLine, 0, 0});
                continue;
            }

            int lineWidth = context->GetTextLineWidth(line);
            if (lineWidth <= wrapWidth) {
                // Line fits: single display line
                displayLines.push_back({logLine, 0, lineGraphemeCount});
                continue;
            }

            // Line needs wrapping
            int startG = 0;
            while (startG < lineGraphemeCount) {
                int remaining = lineGraphemeCount - startG;

                // Check if the rest fits
                std::string restSegment = utf8_substr(line, startG, remaining);
                int restWidth = context->GetTextLineWidth(restSegment);
                if (restWidth <= wrapWidth) {
                    displayLines.push_back({logLine, startG, lineGraphemeCount});
                    break;
                }

                // Binary search for max graphemes that fit in wrapWidth
                int lo = 1, hi = remaining;
                int bestFit = 1; // At minimum, one grapheme per display line

                while (lo <= hi) {
                    int mid = (lo + hi) / 2;
                    std::string segment = utf8_substr(line, startG, mid);
                    int segWidth = context->GetTextLineWidth(segment);

                    if (segWidth <= wrapWidth) {
                        bestFit = mid;
                        lo = mid + 1;
                    } else {
                        hi = mid - 1;
                    }
                }

                int breakAt = startG + bestFit;

                // If we didn't consume the entire remaining line, try word boundary
                if (breakAt < lineGraphemeCount && bestFit > 1) {
                    int wordBreak = -1;
                    for (int g = breakAt - 1; g > startG; g--) {
                        gunichar cp = utf8_get_cp(line, g);
                        if (cp == ' ' || cp == '\t' || cp == '-') {
                            wordBreak = g + 1; // break after the space/hyphen
                            break;
                        }
                    }
                    if (wordBreak > startG) {
                        breakAt = wordBreak;
                    }
                }

                displayLines.push_back({logLine, startG, breakAt});
                startG = breakAt;
            }
        }

        context->PopState();

        // Fix up firstVisibleLine after SetWordWrap toggle:
        // Convert from logical line index to display line index
        if (needFirstVisibleLineFixup) {
            needFirstVisibleLineFixup = false;
            int targetLogicalLine = firstVisibleLine;
            for (int i = 0; i < static_cast<int>(displayLines.size()); i++) {
                if (displayLines[i].logicalLine >= targetLogicalLine) {
                    firstVisibleLine = i;
                    break;
                }
            }
        }
        // Clamp to valid display line range
        int maxFirst = std::max(0, static_cast<int>(displayLines.size()) - maxVisibleLines);
        firstVisibleLine = std::max(0, std::min(firstVisibleLine, maxFirst));
    }

    int UltraCanvasTextArea::GetDisplayLineForCursor(int logicalLine, int graphemeCol) const {
        for (int i = 0; i < static_cast<int>(displayLines.size()); i++) {
            const auto& dl = displayLines[i];
            if (dl.logicalLine == logicalLine) {
                // Cursor at the end of a display line segment belongs to this display line
                // unless there's a next segment starting at exactly this position
                if (graphemeCol >= dl.startGrapheme && graphemeCol < dl.endGrapheme) {
                    return i;
                }
                // Cursor at endGrapheme: belongs to this line if it's the last segment
                // of the logical line, or if graphemeCol == endGrapheme and no next segment
                if (graphemeCol == dl.endGrapheme) {
                    if (i + 1 >= static_cast<int>(displayLines.size()) ||
                        displayLines[i + 1].logicalLine != logicalLine) {
                        return i; // Last segment of this logical line
                    }
                    // There's a next segment; cursor goes to start of next display line
                    // unless it's exactly at end of logical line content
                    if (graphemeCol == GetLineGraphemeCount(logicalLine)) {
                        return i; // At the very end of the logical line
                    }
                }
            }
            if (dl.logicalLine > logicalLine) break;
        }
        // Fallback: find last display line for this logical line
        for (int i = static_cast<int>(displayLines.size()) - 1; i >= 0; i--) {
            if (displayLines[i].logicalLine == logicalLine) return i;
        }
        return 0;
    }

    int UltraCanvasTextArea::GetDisplayLineCount() const {
        return static_cast<int>(displayLines.size());
    }

    void UltraCanvasTextArea::RebuildText() {
        textContent.clear();
        for (size_t i = 0; i < lines.size(); i++) {
            textContent.append(lines[i]);
            if (i < lines.size() - 1) {
                textContent.push_back('\n');
            }
        }
        InvalidateGraphemeCache();
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(textContent);
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



    bool UltraCanvasTextArea::IsNeedVerticalScrollbar() {
        return GetDisplayLineCount() > maxVisibleLines;
    }

    bool UltraCanvasTextArea::IsNeedHorizontalScrollbar() {
        if (wordWrap) return false;
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
        firstVisibleLine = std::max(0, std::min(line, GetDisplayLineCount() - maxVisibleLines));
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollUp(int lineCount) {
        firstVisibleLine = std::max(0, firstVisibleLine - lineCount);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollDown(int lineCount) {
        int maxFirstLine = std::max(0, GetDisplayLineCount() - maxVisibleLines);
        firstVisibleLine = std::min(maxFirstLine, firstVisibleLine + lineCount);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollLeft(int chars) {
        if (wordWrap) return; // No horizontal scrolling when word wrap is on
        horizontalScrollOffset = std::max(0, horizontalScrollOffset - chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollRight(int chars) {
        if (wordWrap) return; // No horizontal scrolling when word wrap is on
        int maxOffset = std::max(0, maxLineWidth - visibleTextArea.width);
        horizontalScrollOffset = std::min(maxOffset, horizontalScrollOffset + chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetFirstVisibleLine(int line) {
        firstVisibleLine = std::max(0, std::min(line, GetDisplayLineCount() - 1));
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

    std::vector<std::string> UltraCanvasTextArea::GetSupportedLanguages() {
        if (!syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        return syntaxTokenizer->GetSupportedLanguages();
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

    void UltraCanvasTextArea::SetTextToFind(const std::string& searchText, bool caseSensitive) {
        lastSearchText = searchText;
        lastSearchCaseSensitive = caseSensitive;
        lastSearchPosition = cursorGraphemePosition;
        // FindNext();
    }

    void UltraCanvasTextArea::FindNext() {
        if (lastSearchText.empty()) return;

        int foundPos = utf8_find(textContent, lastSearchText, lastSearchPosition + 1, lastSearchCaseSensitive);

        if (foundPos < 0 && lastSearchPosition > 0) {
            foundPos = utf8_find(textContent, lastSearchText, 0, lastSearchCaseSensitive);
        }

        if (foundPos >= 0) {
            int searchLen = utf8_length(lastSearchText);
            selectionStartGrapheme = foundPos;
            selectionEndGrapheme = foundPos + searchLen;
            cursorGraphemePosition = selectionEndGrapheme;
            lastSearchPosition = foundPos;

            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;
            EnsureCursorVisible();
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::FindPrevious() {
        if (lastSearchText.empty()) return;

        int foundPos = -1;

        // Search backwards from position BEFORE the current match start
        if (lastSearchPosition > 0) {
            foundPos = utf8_rfind(textContent, lastSearchText, lastSearchPosition - 1, lastSearchCaseSensitive);
        }

        // Wrap around to end of document if nothing found before current position
        if (foundPos < 0) {
            foundPos = utf8_rfind(textContent, lastSearchText, -1, lastSearchCaseSensitive);

            // Don't accept if it's the same position we started from (no other match exists)
            if (foundPos >= 0 && foundPos == lastSearchPosition) {
                return; // Only one match in document, already selected
            }
        }

        if (foundPos >= 0) {
            int searchLen = utf8_length(lastSearchText);
            selectionStartGrapheme = foundPos;
            selectionEndGrapheme = foundPos + searchLen;
            cursorGraphemePosition = selectionStartGrapheme;
            lastSearchPosition = foundPos;

            auto [line, col] = GetLineColumnFromPosition(cursorGraphemePosition);
            currentLineIndex = line;
            EnsureCursorVisible();
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::ReplaceText(const std::string& findText, const std::string& replaceText, bool all) {
        if (findText.empty()) return;

        SaveState();
        int findLen = utf8_length(findText);
        int replaceLen = utf8_length(replaceText);

        if (all) {
            int pos = 0;
            while ((pos = utf8_find(textContent, findText, pos, lastSearchCaseSensitive)) >= 0) {
                utf8_replace(textContent, pos, findLen, replaceText);
                pos += replaceLen;
            }
            SetText(textContent);
        } else {
            if (HasSelection()) {
                std::string selected = GetSelectedText();
                // Case-insensitive comparison for single replace
                bool match;
                if (lastSearchCaseSensitive) {
                    match = (selected == findText);
                } else {
                    gchar* lSel = g_utf8_strdown(selected.c_str(), -1);
                    gchar* lFind = g_utf8_strdown(findText.c_str(), -1);
                    match = (strcmp(lSel, lFind) == 0);
                    g_free(lSel);
                    g_free(lFind);
                }
                if (match) {
                    DeleteSelection();
                    InsertText(replaceText);
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

        int searchLen = utf8_length(searchText);
        int pos = 0;
        while ((pos = utf8_find(textContent, searchText, pos, lastSearchCaseSensitive)) >= 0) {
            searchHighlights.push_back({pos, pos + searchLen});
            pos += searchLen;
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
            lines[i].insert(0, indent);
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
            for (int j = 0; j < tabSize && j < utf8_length(lines[i]); j++) {
                std::string ch = utf8_char_at(lines[i], j);
                if (ch == " " || ch == "\t") {
                    spacesToRemove++;
                } else {
                    break;
                }
            }
            if (spacesToRemove > 0) {
                // Leading whitespace is ASCII, so byte erase at 0 is safe
                lines[i].erase(0, spacesToRemove);
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



    void UltraCanvasTextArea::SetMarkdownHybridMode(bool enable) {
        markdownHybridMode = enable;
        markdownHitRects.clear();
        if (enable) {
            // Ensure syntax highlighting is enabled for raw markdown on current line
            SetHighlightSyntax(true);
            if (syntaxTokenizer) {
                syntaxTokenizer->SetLanguage("Markdown");
            }
        }
        
        RequestRedraw();
    }

// ===== FACTORY FUNCTIONS =====
    std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(
            const std::string& name, int id, int x, int y, int width, int height) {
        
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        
        // Enable hybrid markdown mode
        editor->SetMarkdownHybridMode(true);
        
        // Apply markdown-friendly styling
        editor->ApplyPlainTextStyle();
        editor->SetWordWrap(true);
        
        return editor;
    }

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

    // std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(const std::string& name, int id,
    //                                                            int x, int y, int width, int height) {
    //     auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
    //     editor->ApplyCodeStyle("Markdown");
    //     return editor;
    // }

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