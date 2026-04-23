// core/UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.3.0
// Last Modified: 2026-04-20
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstring>

namespace UltraCanvas {
// Constructor
    constexpr int lineShardSoftLimit = 4000;
    constexpr int lineShardHardLimit = 12000;

    UltraCanvasTextArea::UltraCanvasTextArea(const std::string& name, int id, int x, int y,
                                             int width, int height)
            : UltraCanvasUIElement(name, id, x, y, width, height),
              horizontalScrollOffset(0),
              verticalScrollOffset(0),
              cursorBlinkTime(0),
              cursorVisible(true),
              isReadOnly(false),
              wordWrap(false),
              highlightCurrentLine(false),
              isNeedRecalculateVisibleArea(true)  {

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
        auto* app = UltraCanvasApplication::GetInstance();
        if (app) {
            style.fontStyle.fontFamily = app->GetSystemFontStyle().fontFamily;
            style.fixedFontStyle.fontFamily = app->GetDefaultMonospacedFontStyle().fontFamily;
        }
        style.fontStyle.fontSize = 11;
        style.fixedFontStyle.fontSize = 11;
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

// ===== LINE ENDING HELPERS =====

    LineEndingType UltraCanvasTextArea::GetSystemDefaultLineEnding() {
#ifdef _WIN32
        return LineEndingType::CRLF;
#else
        return LineEndingType::LF;
#endif
    }

    LineEndingType UltraCanvasTextArea::DetectLineEnding(const std::string& text) {
        int lfCount = 0;
        int crlfCount = 0;
        int crCount = 0;

        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    crlfCount++;
                    i++; // skip the \n
                } else {
                    crCount++;
                }
            } else if (text[i] == '\n') {
                lfCount++;
            }
        }

        // No line endings found — use system default
        if (lfCount == 0 && crlfCount == 0 && crCount == 0) {
            return GetSystemDefaultLineEnding();
        }

        // Return the dominant type
        if (crlfCount >= lfCount && crlfCount >= crCount) return LineEndingType::CRLF;
        if (crCount >= lfCount && crCount >= crlfCount) return LineEndingType::CR;
        return LineEndingType::LF;
    }

    std::string UltraCanvasTextArea::LineEndingToString(LineEndingType type) {
        switch (type) {
            case LineEndingType::LF:   return "LF";
            case LineEndingType::CRLF: return "CRLF";
            case LineEndingType::CR:   return "CR";
        }
        return "LF";
    }

    std::string UltraCanvasTextArea::LineEndingSequence(LineEndingType type) {
        switch (type) {
            case LineEndingType::LF:   return "\n";
            case LineEndingType::CRLF: return "\r\n";
            case LineEndingType::CR:   return "\r";
        }
        return "\n";
    }

    void UltraCanvasTextArea::SetLineEnding(LineEndingType type) {
        if (lineEndingType != type) {
            lineEndingType = type;
            RebuildText();
            if (onLineEndingChanged) {
                onLineEndingChanged(lineEndingType);
            }
        }
    }

// ===== POSITION CONVERSION =====

    // Convert grapheme position to line/column (both in graphemes). Terminal segments
    // contribute visibleLen + 1 (the \n is one grapheme); continuation segments
    // contribute only their visible length (no boundary between them).
    std::pair<int, int> UltraCanvasTextArea::GetLineColumnFromPosition(int graphemePosition) const {
        int line = 0;
        int col = 0;
        int currentPos = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            int vis = GetLineVisibleLength(static_cast<int>(i));
            int span = vis + (LineHasNewline(static_cast<int>(i)) ? 1 : 0);
            if (currentPos + vis >= graphemePosition) {
                line = static_cast<int>(i);
                col = graphemePosition - currentPos;
                return {line, col};
            }
            currentPos += span;
            line = static_cast<int>(i);
        }
        return {line, col};
    }

    // Convert line/column (in graphemes) to absolute grapheme position.
    int UltraCanvasTextArea::GetPositionFromLineColumn(int line, int graphemeColumn) const {
        int position = 0;

        for (int i = 0; i < line && i < static_cast<int>(lines.size()); i++) {
            int vis = GetLineVisibleLength(i);
            position += vis + (LineHasNewline(i) ? 1 : 0);
        }

        if (line < static_cast<int>(lines.size())) {
            position += std::min(graphemeColumn, GetLineVisibleLength(line));
        }

        return position;
    }

// ===== TEXT MANIPULATION METHODS =====

    void UltraCanvasTextArea::SetText(const std::string& newText, bool runNotifications) {
        // Detect and set line ending type from content (used only for save-side conversion)
        LineEndingType detectedEOL = DetectLineEnding(newText);
        bool eolChanged = (detectedEOL != lineEndingType);
        lineEndingType = detectedEOL;

        // Normalize to LF, keep \n inside terminal segments, shard long logical lines.
        lines = utf8_split_lines_sharded(newText, lineShardSoftLimit, lineShardHardLimit);
        if (lines.empty()) {
            lines.push_back(std::string());
        }

        // textContent is the verbatim concatenation of all segments.
        textContent.clear();
        size_t total = 0;
        for (const auto& l : lines) total += l.size();
        textContent.reserve(total);
        for (const auto& l : lines) textContent.append(l);

        // Full text replacement: invalidate the layout cache + MD index.
        lineLayouts.clear();
        lineLayouts.resize(lines.size());
        markdownIndexDirty = true;
        SetCursorPosition({0, 0});
        currentLine.reset();
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
        if (runNotifications && onTextChanged) {
            onTextChanged(textContent);
        }
        if (eolChanged && onLineEndingChanged) {
            onLineEndingChanged(lineEndingType);
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

        int line = std::max(0, cursorPosition.lineIndex);
        int col  = std::max(0, cursorPosition.columnIndex);
        int startLine = line;

        if (lines.empty()) {
            lines.push_back(std::string());
            InsertLineLayoutEntry(0);
            line = 0;
            col  = 0;
        }

        // Ensure cursor sits inside the visible portion, not at/after the trailing \n.
        if (col > GetLineVisibleLength(line)) col = GetLineVisibleLength(line);

        const char* p   = textToInsert.c_str();
        const char* end = p + textToInsert.size();
        while (p < end) {
            if (*p == '\r' || *p == '\n') {
                bool isCRLF = (*p == '\r' && p + 1 < end && *(p + 1) == '\n');
                int vis = GetLineVisibleLength(line);
                bool wasTerminal = LineHasNewline(line);
                std::string cur = lines[line];
                std::string left  = utf8_substr(cur, 0, col);
                std::string right = utf8_substr(cur, col, vis - col);
                if (wasTerminal) right.append("\n");
                left.append("\n");
                lines[line] = std::move(left);
                lines.insert(lines.begin() + line + 1, std::move(right));
                InsertLineLayoutEntry(line + 1);
                line++;
                col = 0;
                p += isCRLF ? 2 : 1;
            } else {
                const char* next = g_utf8_next_char(p);
                std::string ch(p, static_cast<size_t>(next - p));
                utf8_insert(lines[line], col, ch);
                col++;
                p = next;
            }
        }

        // Remember cursor as absolute grapheme position, then re-shard touched range
        // (which may insert new entries and shift line indices), then resolve cursor.
        int cursorAbs = GetPositionFromLineColumn(line, col);

        for (int i = startLine; i <= line && i < static_cast<int>(lines.size()); ) {
            InvalidateLineLayout(i);
            int added = ReshardSegment(i);
            line += added;
            i += 1 + added;
        }

        auto lc = GetLineColumnFromPosition(cursorAbs);
        SetCursorPosition({lc.first, lc.second});

        RebuildText();
    }

    void UltraCanvasTextArea::InsertCodepoint(char32_t codepoint) {
        if (isReadOnly) return;

        InsertText(utf8_encode(codepoint));
    }

    void UltraCanvasTextArea::InsertNewLine() {
        if (isReadOnly) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        if (lines.empty()) {
            lines.push_back(std::string());
            InsertLineLayoutEntry(0);
            line = 0;
            col  = 0;
        }

        int vis = GetLineVisibleLength(line);
        if (col > vis) col = vis;
        bool wasTerminal = LineHasNewline(line);

        std::string cur = lines[line];
        std::string left  = utf8_substr(cur, 0, col);
        std::string right = utf8_substr(cur, col, vis - col);
        if (wasTerminal) right.append("\n");
        left.append("\n");
        lines[line] = std::move(left);
        lines.insert(lines.begin() + line + 1, std::move(right));
        InvalidateLineLayout(line);
        InsertLineLayoutEntry(line + 1);

        SetCursorPosition({line + 1, 0});

        RebuildText();
    }

    void UltraCanvasTextArea::InsertTab() {
        if (isReadOnly) return;

        // Insert spaces for tab
        std::string tabStr(tabSize, ' ');
        InsertText(tabStr);
    }

    // Delete one codepoint backward (backspace)
    void UltraCanvasTextArea::DeleteCharacterBackward() {
        if (isReadOnly) return;
        if (cursorPosition.lineIndex == 0 && cursorPosition.columnIndex == 0) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        if (col > 0) {
            utf8_erase(lines[line], col - 1, 1);
            InvalidateLineLayout(line);
            SetCursorPosition({line, col - 1});
        } else if (line > 0) {
            int prevVis = GetLineVisibleLength(line - 1);
            bool prevTerminal = LineHasNewline(line - 1);
            if (prevTerminal) {
                // Strip \n from previous segment (the "deleted" character)
                lines[line - 1].pop_back();
            }
            // Current segment carries its own \n forward if it is terminal.
            lines[line - 1].append(lines[line]);
            lines.erase(lines.begin() + line);
            InvalidateLineLayout(line - 1);
            RemoveLineLayoutEntry(line);
            SetCursorPosition({line - 1, prevVis});
            ReshardSegment(line - 1);
        }

        RebuildText();
    }

    // Delete one codepoint forward (delete key)
    void UltraCanvasTextArea::DeleteCharacterForward() {
        if (isReadOnly) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        if (line < static_cast<int>(lines.size())) {
            int vis = GetLineVisibleLength(line);
            if (col < vis) {
                utf8_erase(lines[line], col, 1);
                InvalidateLineLayout(line);
            } else if (line < static_cast<int>(lines.size()) - 1) {
                if (LineHasNewline(line)) {
                    lines[line].pop_back();  // strip \n (the "deleted" character)
                }
                lines[line].append(lines[line + 1]);
                lines.erase(lines.begin() + line + 1);
                InvalidateLineLayout(line);
                RemoveLineLayoutEntry(line + 1);
                ReshardSegment(line);
            }

            RebuildText();
        }
    }

    void UltraCanvasTextArea::DeleteSelection() {
        if (!HasSelection() || isReadOnly) return;

        SaveState();
        // Lexicographic ordering on (lineIndex, columnIndex).
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) {
            std::swap(a, b);
        }
        int startLine = a.lineIndex, startCol = a.columnIndex;
        int endLine   = b.lineIndex, endCol   = b.columnIndex;

        int sv = GetLineVisibleLength(startLine);
        int ev = GetLineVisibleLength(endLine);
        if (startCol > sv) startCol = sv;
        if (endCol > ev) endCol = ev;

        if (startLine == endLine) {
            utf8_erase(lines[startLine], startCol, endCol - startCol);
            InvalidateLineLayout(startLine);
        } else {
            bool endTerminal = LineHasNewline(endLine);
            std::string newLine = utf8_substr(lines[startLine], 0, startCol);
            std::string tail = utf8_substr(lines[endLine], endCol, ev - endCol);
            if (endTerminal) tail.append("\n");
            newLine.append(tail);
            lines[startLine] = std::move(newLine);
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
            InvalidateLineLayout(startLine);
            for (int i = endLine; i > startLine; i--) {
                RemoveLineLayoutEntry(i);
            }
        }

        SetCursorPosition({startLine, startCol});
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;

        ReshardSegment(startLine);
        RebuildText();
    }
// ===== CURSOR MOVEMENT METHODS (Grapheme-aware) =====

    void UltraCanvasTextArea::MoveCursorLeft(bool selecting) {
        if (cursorPosition.lineIndex == 0 && cursorPosition.columnIndex == 0) return;

        LineColumnIndex oldPos = cursorPosition;
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        // Try layout-based cursor movement (correct bidi/complex-script handling).
        if (col > 0) {
            SetCursorPosition({line, col - 1});
        } else if (line > 0) {
            SetCursorPosition({line - 1, GetLineVisibleLength(line - 1)});
        }

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(cursorPosition);
        }
    }

    void UltraCanvasTextArea::MoveCursorRight(bool selecting) {
        int totalLines = (int)lines.size();
        if (cursorPosition.lineIndex >= totalLines - 1 &&
            (totalLines == 0 || cursorPosition.columnIndex >= GetLineVisibleLength(cursorPosition.lineIndex))) {
            return;
        }

        LineColumnIndex oldPos = cursorPosition;
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        int lineCps = GetLineVisibleLength(line);
        if (col < lineCps) {
            SetCursorPosition({line, col + 1});
        } else if (line + 1 < totalLines) {
            SetCursorPosition({line + 1, 0});
        }

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(cursorPosition);
        }
    }

    // Vertical cursor movement uses the layout-based cursor rect to walk across wrapped sub-lines
    // inside a single ITextLayout (handled by PosToLineColumn → Pango XYToIndex) as well as
    // across logical line boundaries. No displayLines table needed.
    void UltraCanvasTextArea::MoveCursorUp(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetX = curRect.x;
            int targetY = curRect.y - 2;
            LineColumnIndex hit = PosToLineColumn({targetX, targetY});
            if (hit.lineIndex >= 0 &&
                (hit.lineIndex < cursorPosition.lineIndex ||
                 (hit.lineIndex == cursorPosition.lineIndex && hit.columnIndex < cursorPosition.columnIndex))) {
                newPos = hit;
            } else if (cursorPosition.lineIndex > 0) {
                newPos.lineIndex = cursorPosition.lineIndex - 1;
                newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                              GetLineVisibleLength(newPos.lineIndex));
            }
        } else if (cursorPosition.lineIndex > 0) {
            newPos.lineIndex = cursorPosition.lineIndex - 1;
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          GetLineVisibleLength(newPos.lineIndex));
        } else {
            return;
        }
        SetCursorPosition(newPos);
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(newPos);
        }
    }

    void UltraCanvasTextArea::MoveCursorDown(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetX = curRect.x;
            int targetY = curRect.y + curRect.height + 2;
            LineColumnIndex hit = PosToLineColumn({targetX, targetY});
            if (hit.lineIndex >= 0 &&
                (hit.lineIndex > cursorPosition.lineIndex ||
                 (hit.lineIndex == cursorPosition.lineIndex && hit.columnIndex > cursorPosition.columnIndex))) {
                newPos = hit;
            } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
                newPos.lineIndex = cursorPosition.lineIndex + 1;
                newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                              GetLineVisibleLength(newPos.lineIndex));
            }
        } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
            newPos.lineIndex = cursorPosition.lineIndex + 1;
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          GetLineVisibleLength(newPos.lineIndex));
        } else {
            return;
        }
        SetCursorPosition(newPos);
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(newPos);
        }
    }

    void UltraCanvasTextArea::MoveCursorWordLeft(bool selecting) {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        LineColumnIndex oldPos = cursorPosition;

        if (col == 0) {
            if (line > 0) {
                line--;
                col = GetLineVisibleLength(line);
            } else {
                if (selecting) {
                    if (selectionStart.lineIndex < 0) selectionStart = cursorPosition;
                    selectionEnd = cursorPosition;
                } else {
                    selectionStart = LineColumnIndex::INVALID;
                    selectionEnd   = LineColumnIndex::INVALID;
                }
                return;
            }
        } else {
            const std::string& currentLine = lines[line];
            while (col > 0) {
                gunichar cp = utf8_get_cp(currentLine, col - 1);
                if (g_unichar_isalnum(cp) || cp == '_') break;
                col--;
            }
            while (col > 0) {
                gunichar cp = utf8_get_cp(currentLine, col - 1);
                if (!g_unichar_isalnum(cp) && cp != '_') break;
                col--;
            }
        }

        SetCursorPosition({line, col});

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(cursorPosition);
    }

    void UltraCanvasTextArea::MoveCursorWordRight(bool selecting) {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        LineColumnIndex oldPos = cursorPosition;
        const std::string& currentLine = lines[line];
        int lineLen = GetLineVisibleLength(line);

        if (col >= lineLen) {
            if (line + 1 < static_cast<int>(lines.size())) {
                line++;
                col = 0;
            } else {
                if (selecting) {
                    if (selectionStart.lineIndex < 0) selectionStart = cursorPosition;
                    selectionEnd = cursorPosition;
                } else {
                    selectionStart = LineColumnIndex::INVALID;
                    selectionEnd   = LineColumnIndex::INVALID;
                }
                return;
            }
        } else {
            while (col < lineLen) {
                gunichar cp = utf8_get_cp(currentLine, col);
                if (g_unichar_isalnum(cp) || cp == '_') break;
                col++;
            }
            while (col < lineLen) {
                gunichar cp = utf8_get_cp(currentLine, col);
                if (!g_unichar_isalnum(cp) && cp != '_') break;
                col++;
            }
        }

        SetCursorPosition({line, col});

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(cursorPosition);
    }

    void UltraCanvasTextArea::MoveCursorPageUp(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetY = curRect.y - visibleTextArea.height;
            LineColumnIndex hit = PosToLineColumn({curRect.x, targetY});
            if (hit.lineIndex >= 0) newPos = hit;
        } else if (cursorPosition.lineIndex > 0) {
            newPos.lineIndex  = std::max(0, cursorPosition.lineIndex - 10);
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          GetLineVisibleLength(newPos.lineIndex));
        } else {
            return;
        }
        SetCursorPosition(newPos);
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(newPos);
    }

    void UltraCanvasTextArea::MoveCursorPageDown(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetY = curRect.y + visibleTextArea.height;
            LineColumnIndex hit = PosToLineColumn({curRect.x, targetY});
            if (hit.lineIndex >= 0) newPos = hit;
        } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
            newPos.lineIndex  = std::min((int)lines.size() - 1, cursorPosition.lineIndex + 10);
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          GetLineVisibleLength(newPos.lineIndex));
        } else {
            return;
        }

        SetCursorPosition(newPos);
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        if (onCursorPositionChanged) onCursorPositionChanged(newPos);
    }

    void UltraCanvasTextArea::MoveCursorToLineStart(bool selecting) {
        int line = cursorPosition.lineIndex;
        LineColumnIndex oldPos = cursorPosition;
        int targetCol = 0;
        if (wordWrap) {
            Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
            if (curRect.IsValid()) {
                LineColumnIndex hit = PosToLineColumn({visibleTextArea.x, curRect.y + 3});
                if (hit.lineIndex == line) targetCol = hit.columnIndex;
            }
        }
        SetCursorPosition({line, targetCol});
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(cursorPosition);
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineEnd(bool selecting) {
        int line = cursorPosition.lineIndex;
        LineColumnIndex oldPos = cursorPosition;

        if (line < static_cast<int>(lines.size())) {
            int vis = GetLineVisibleLength(line);
            int targetCol = vis;
            if (wordWrap) {
                Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
                if (curRect.IsValid()) {
                    int farRight = 0x7fffffff / 1024;
                    LineColumnIndex hit = PosToLineColumn({farRight, curRect.y + 3});
                    if (hit.lineIndex == line) targetCol = std::min(hit.columnIndex, vis);
                }
            }
            SetCursorPosition({line, targetCol});
            if (selecting) {
                if (selectionStart.lineIndex < 0) selectionStart = oldPos;
                selectionEnd = cursorPosition;
            } else {
                selectionStart = LineColumnIndex::INVALID;
                selectionEnd   = LineColumnIndex::INVALID;
            }

            RequestRedraw();
            if (onCursorPositionChanged) onCursorPositionChanged(cursorPosition);
        }
    }

    void UltraCanvasTextArea::MoveCursorToStart(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        SetCursorPosition({0, 0});
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        if (onCursorPositionChanged) onCursorPositionChanged({0, 0});
    }

    void UltraCanvasTextArea::MoveCursorToEnd(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        int toLine = std::max(static_cast<int>(lines.size()) - 1, 0);
        int lineLength = GetLineVisibleLength(toLine);
        SetCursorPosition({toLine, lineLength});
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }

        if (onCursorPositionChanged) onCursorPositionChanged(cursorPosition);
    }

// ===== SELECTION METHODS =====

    void UltraCanvasTextArea::SelectAll() {
        if (lines.empty()) {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd = LineColumnIndex::INVALID;
            return;
        }
        selectionStart = {0, 0};
        int last = (int)lines.size() - 1;
        selectionEnd  = {last, GetLineVisibleLength(last)};
        SetCursorPosition(selectionEnd);

        RequestRedraw();
    }

    void UltraCanvasTextArea::SelectLine(int lineIndex) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            selectionStart = {lineIndex, 0};
            selectionEnd   = {lineIndex, GetLineVisibleLength(lineIndex)};
            SetCursorPosition(selectionEnd);
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::SelectWord() {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        const std::string& currentLine = lines[line];
        int lineLen = GetLineVisibleLength(line);
        if (lineLen == 0) return;

        int wordStart = col;
        while (wordStart > 0) {
            gunichar cp = utf8_get_cp(currentLine, wordStart - 1);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
            wordStart--;
        }
        int wordEnd = col;
        while (wordEnd < lineLen) {
            gunichar cp = utf8_get_cp(currentLine, wordEnd);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
            wordEnd++;
        }

        selectionStart = {line, wordStart};
        selectionEnd   = {line, wordEnd};
        SetCursorPosition(selectionEnd);
    }

    // Public API: arguments are flat codepoint positions.
    void UltraCanvasTextArea::SetSelection(int startGrapheme, int endGrapheme) {
        auto [sLine, sCol] = GetLineColumnFromPosition(startGrapheme);
        auto [eLine, eCol] = GetLineColumnFromPosition(endGrapheme);
        selectionStart = {sLine, sCol};
        selectionEnd   = {eLine, eCol};
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearSelection() {
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::HasSelection() const {
        if (selectionStart.lineIndex < 0 || selectionEnd.lineIndex < 0) return false;
        return !(selectionStart.lineIndex == selectionEnd.lineIndex &&
                 selectionStart.columnIndex == selectionEnd.columnIndex);
    }

    int UltraCanvasTextArea::GetSelectionMinGrapheme() const {
        if (!HasSelection()) return -1;
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        return GetPositionFromLineColumn(a.lineIndex, a.columnIndex);
    }

    std::string UltraCanvasTextArea::GetSelectedText() const {
        if (!HasSelection()) return std::string();
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, startCol = a.columnIndex;
        int endLine   = b.lineIndex, endCol   = b.columnIndex;

        std::string result;
        for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); i++) {
            int vis = GetLineVisibleLength(i);
            int colStart = (i == startLine) ? std::min(startCol, vis) : 0;
            int colEnd   = (i == endLine)   ? std::min(endCol, vis)   : vis;
            result.append(utf8_substr(lines[i], colStart, colEnd - colStart));
            // Only emit \n between terminal segments. Continuation segments flow directly
            // into the next piece, so the copied text spans shard boundaries seamlessly.
            if (i < endLine && LineHasNewline(i)) {
                result.append("\n");
            }
        }
        return result;
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
        return cursorPosition.lineIndex;
    }

    int UltraCanvasTextArea::GetCurrentColumn() const {
        return cursorPosition.columnIndex;
    }

    int UltraCanvasTextArea::GetLineCount() const {
        return static_cast<int>(lines.size());
    }

    std::string UltraCanvasTextArea::GetLine(int lineIndex) const {
        // Callers shouldn't see the internal \n marker.
        return GetLineContent(lineIndex);
    }

    void UltraCanvasTextArea::SetLine(int lineIndex, const std::string& text) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            bool wasTerminal = LineHasNewline(lineIndex);
            lines[lineIndex] = text;
            // Preserve this segment's terminal/continuation status.
            if (wasTerminal) lines[lineIndex].append("\n");
            InvalidateLineLayout(lineIndex);
            ReshardSegment(lineIndex);
            RebuildText();
        }
    }

    void UltraCanvasTextArea::GoToLine(int lineNumber) {
        int lineIndex = std::max(0, std::min(lineNumber, static_cast<int>(lines.size()) - 1));
        SetCursorPosition({lineIndex, 0});
    }

// ===== RENDERING METHODS =====
    void UltraCanvasTextArea::Render(IRenderContext* ctx) {
        // UpdateGeometry runs CalculateVisibleArea + UpdateLineLayouts. It's idempotent
        // if layouts are already built; calling here guards against frames where the framework
        // skipped the geometry pass.
        UpdateGeometry(ctx);

        if (isCursorMoved) {
            EnsureCursorVisible();
            isCursorMoved = false;
        }

        DrawBackground(ctx);

        if (editingMode == TextAreaEditingMode::Hex) {
            DrawHexView(ctx);
        } else {
            if (style.showLineNumbers) {
                DrawLineNumbers(ctx);
            }

            // New render path: iterate the per-line layout cache and dispatch each visible line
            // through RenderLineLayout. Selection backgrounds and syntax colors are already
            // baked in as ITextLayout attributes (Step 4 / 4b / 6).
            ctx->PushState();
            ctx->ClipRect(visibleTextArea);
            int viewportTop    = verticalScrollOffset;
            int viewportBottom = viewportTop + visibleTextArea.height;
            LineLayoutBase* ll;
            for (int i = 0; i < (int)lineLayouts.size(); i++) {
                ll = GetActualLineLayout(i);
                if (!ll) continue;
                int lineTop    = ll->bounds.y;
                int lineBottom = lineTop + ll->bounds.height;
                if (lineBottom < viewportTop)  continue;
                if (lineTop    > viewportBottom) break;

                RenderLineLayout(ctx, ll);
            }
            ctx->PopState();

//            if (IsFocused() && cursorVisible && !isReadOnly) {
                DrawCursor(ctx);
//            }
        }

        DrawBorder(ctx);
        DrawScrollbars(ctx);
    }

    void UltraCanvasTextArea::DrawBorder(IRenderContext* context) {
        auto bounds = GetElementLocalBounds();
        if (style.borderWidth > 0) {
            context->DrawFilledRectangle(bounds, Colors::Transparent, style.borderWidth, style.borderColor);
        }
    }

    // DrawPlainText / DrawHighlightedText removed in Step 8 — rendering goes through
    // RenderLineLayout driven from Render().

    void UltraCanvasTextArea::DrawLineNumbers(IRenderContext* context) {
        auto bounds = GetElementLocalBounds();
        int gutterW = computedLineNumbersWidth;

        context->SetFillPaint(style.lineNumbersBackgroundColor);
        context->FillRectangle(Rect2Df(bounds.x, bounds.y, gutterW, bounds.height));

        context->SetStrokePaint(style.borderColor);
        context->SetStrokeWidth(1);
        context->DrawLine({bounds.x + gutterW, bounds.y},
                          {bounds.x + gutterW, bounds.y + bounds.height});

        context->SetFontFace(style.fontStyle.fontFamily, style.fontStyle.fontWeight, style.fontStyle.fontSlant);
        context->SetFontSize(style.fontStyle.fontSize);
        context->SetTextAlignment(TextAlignment::Right);

        Rect2Df gutterClip{bounds.x, visibleTextArea.y, gutterW, visibleTextArea.height};
        context->PushState();
        context->ClipRect(gutterClip);

        // Iterate the per-line layout cache; draw the number for each logical line at the Y of
        // its layout's bounds.
        int viewportTop    = verticalScrollOffset;
        int viewportBottom = viewportTop + visibleTextArea.height;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            const auto& ll = GetActualLineLayout(i);
            if (!ll || !ll->layout) continue;
            int lineTop    = ll->bounds.y;
            int lineBottom = lineTop + ll->bounds.height;
            if (lineBottom < viewportTop) continue;
            if (lineTop    > viewportBottom) break;
            int numY = visibleTextArea.y + lineTop - verticalScrollOffset;
            if (IsFocused() && i == cursorPosition.lineIndex) {
                Rect2Di cursorRect = LineColumnToCursorPos(cursorPosition);
                context->SetFillPaint(Color(255, 128, 128, 255));
                context->FillRectangle(Rect2Df(bounds.x+1, cursorRect.y, gutterW, cursorRect.height));
                context->SetTextPaint(style.fontColor);
                context->SetFontWeight(FontWeight::Normal);
            } else {
                context->SetTextPaint(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }
            if (ll->logicalLineNumber) {
                context->DrawTextInRect(std::to_string(ll->logicalLineNumber),
                                        Rect2Df(bounds.x, numY, gutterW - 4, ll->bounds.height));
            }
        }
        context->PopState();
    }

    // DrawSelection removed in Step 8 — selection is now rendered as a Pango background-color
    // attribute inside each line's ITextLayout (applied in ApplyLineSelectionBackground).

    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetElementLocalBounds();
        context->SetFillPaint(style.backgroundColor);
        context->FillRectangle(bounds);

        if (highlightCurrentLine && IsFocused() && currentLine) {
            int highlightX = style.showLineNumbers ? bounds.x + computedLineNumbersWidth : bounds.x;
            int highlightW = bounds.width - (style.showLineNumbers ? computedLineNumbersWidth : 0);
            int highlightY = visibleTextArea.y + currentLine->bounds.y - verticalScrollOffset;
            context->PushState();
            context->ClipRect(visibleTextArea);
            context->SetFillPaint(style.currentLineHighlightColor);
            context->FillRectangle(Rect2Df(highlightX, highlightY, highlightW, currentLine->bounds.height));
            context->PopState();
        }
    }

    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        Rect2Di cursorRect = LineColumnToCursorPos(cursorPosition);
        if (!cursorRect.IsValid()) return;

        // Clip to visible text area.
        if (cursorRect.y + cursorRect.height < visibleTextArea.y) return;
        if (cursorRect.y > visibleTextArea.y + visibleTextArea.height) return;
        if (cursorRect.x > visibleTextArea.x + visibleTextArea.width) return;
        if (cursorRect.x < visibleTextArea.x) return;

        int caretHeight = cursorRect.height > 0 ? cursorRect.height
                                                : (computedLineHeight > 0 ? computedLineHeight : 16);
        context->PushState();
        context->SetStrokeWidth(2);
        context->DrawLine({cursorRect.x, cursorRect.y},
                          {cursorRect.x, cursorRect.y + caretHeight}, style.cursorColor);
        context->PopState();
    }

    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetElementLocalBounds();

        if (IsNeedVerticalScrollbar()) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);

            // Hex mode still uses row-index scrolling; text mode uses pixel content height
            // derived from the built line layouts.
            int thumbHeight, thumbY = bounds.y, maxThumbY;
            if (editingMode == TextAreaEditingMode::Hex) {
                int totalLines  = hexTotalRows;
                int visibleLines = hexMaxVisibleRows;
                int firstLine    = hexFirstVisibleRow;
                thumbHeight = std::max(20, (visibleLines * scrollbarHeight) / std::max(1, totalLines));
                maxThumbY = scrollbarHeight - thumbHeight;
                if (totalLines > visibleLines && maxThumbY > 0) {
                    thumbY = bounds.y + (firstLine * maxThumbY) / (totalLines - visibleLines);
                }
            } else {
                int contentHeight = 0;
                if (!lineLayouts.empty() && lineLayouts.back()) {
                    contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
                } else {
                    contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
                }
                int viewportH = visibleTextArea.height;
                thumbHeight = std::max(20, (viewportH * scrollbarHeight) / std::max(1, contentHeight));
                maxThumbY = scrollbarHeight - thumbHeight;
                if (contentHeight > viewportH && maxThumbY > 0) {
                    thumbY = bounds.y + (verticalScrollOffset * maxThumbY) / (contentHeight - viewportH);
                }
            }

            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(Rect2Df(scrollbarX, bounds.y, 15, scrollbarHeight));

            verticalScrollThumb = {scrollbarX, thumbY, 15, thumbHeight};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(Rect2Df(scrollbarX + 2, thumbY + 2, 11, thumbHeight - 4));
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
            context->FillRectangle(Rect2Df(static_cast<float>(bounds.x), scrollbarY, scrollbarWidth, 15.0f));

            horizontalScrollThumb = {static_cast<int>(thumbX), static_cast<int>(scrollbarY), static_cast<int>(thumbWidth), 15};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(Rect2Df(thumbX + 2, scrollbarY + 2, thumbWidth - 4, 11.0f));
        }
    }

// ===== EVENT HANDLING =====

    bool UltraCanvasTextArea::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        // Hex mode delegates to its own handlers
        if (editingMode == TextAreaEditingMode::Hex) {
            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleHexMouseDown(event);
                case UCEventType::MouseUp:
                    if (isDraggingVerticalThumb) {
                        isDraggingVerticalThumb = false;
                        UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
                        return true;
                    }
                    if (hexIsSelectingWithMouse) {
                        hexIsSelectingWithMouse = false;
                        hexSelectionAnchor = -1;
                        UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
                        RequestRedraw();
                    }
                    return true;
                case UCEventType::MouseMove:
                    return HandleHexMouseMove(event);
                case UCEventType::KeyDown:
                    return HandleHexKeyDown(event);
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
                RequestRedraw();
                return true;
            case UCEventType::FocusLost:
                cursorVisible = false;
                // Return the in-edit line to its formatted MD layout while the widget is idle
                // (or discard the stash if the line was modified). On focus regain, the swap
                // re-runs via ReconcileLayoutState.
                currentLine.reset();
                RequestRedraw();
                return true;
            default:
                return false;
        }
    }

    bool UltraCanvasTextArea::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        // Scrollbar thumb dragging takes priority
        // dragStartOffset is the pixel offset between the pointer and the thumb origin,
        // stored as (global pointer) - (local thumb coordinate) so the drag math in
        // HandleMouseMove (which mixes global pointer and local bounds) stays consistent.
        if (IsNeedVerticalScrollbar() && verticalScrollThumb.Contains(event.pointer)) {
            isDraggingVerticalThumb = true;
            // local_thumb_y = pointerGlobal.y - GetYInWindow() - (pointer.y - verticalScrollThumb.y)
            // We want newThumbY = pointerGlobal.y - dragStartOffset.y - GetYInWindow().
            // Solve: dragStartOffset.y = pointerGlobal.y - GetYInWindow() - verticalScrollThumb.y
            dragStartOffset.y = event.pointerGlobal.y - GetYInWindow() - verticalScrollThumb.y;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        if (IsNeedHorizontalScrollbar() && horizontalScrollThumb.Contains(event.pointer)) {
            isDraggingHorizontalThumb = true;
            dragStartOffset.x = event.pointerGlobal.x - GetXInWindow() - horizontalScrollThumb.x;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        SetFocus(true);

        // --- Markdown link/image click: intercept before cursor move ---
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && HandleMarkdownClick(event.pointer.x, event.pointer.y)) {
            return true;
        }

        // --- Click counting for single / double / triple click ---
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();
        int dx = std::abs(event.pointer.x - lastClickX);
        int dy = std::abs(event.pointer.y - lastClickY);

        if (elapsed <= MultiClickTimeThresholdMs &&
            dx <= MultiClickDistanceThreshold &&
            dy <= MultiClickDistanceThreshold) {
            clickCount++;
        } else {
            clickCount = 1;
        }

        lastClickTime = now;
        lastClickX = event.pointer.x;
        lastClickY = event.pointer.y;

        // --- Compute clicked text position ---
        LineColumnIndex clicked = PosToLineColumn({event.pointer.x, event.pointer.y});
        int clickedLine = std::max(0, clicked.lineIndex);
        int clickedCol  = std::max(0, clicked.columnIndex);

        // --- Act on click count ---
        if (clickCount >= 3) {
            // Triple click: select entire line
            clickCount = 3; // Cap to prevent quad-click escalation
            HandleMouseTripleClick(event);
            return true;
        }

        if (clickCount == 2) {
            // cursorPosition = {clickedLine, clickedCol};
            // currentLineIndex = clickedLine;
            // SelectWord();
            // selectionAnchor = selectionStart;
            // isSelectingText = true;
            // UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        // --- Single click ---
        SetCursorPosition({clickedLine, clickedCol});

        if (event.shift && selectionStart.lineIndex >= 0) {
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
            selectionAnchor = cursorPosition;
        }

        // Begin text drag selection tracking
        isSelectingText = true;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);

        RequestRedraw();
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseDoubleClick(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        LineColumnIndex clicked = PosToLineColumn({event.pointer.x, event.pointer.y});
        if (clicked.lineIndex < 0) return true;
        SetCursorPosition(clicked);

        SelectWord();
        selectionAnchor = selectionStart;
        //isSelectingText = true;
        clickCount = 2;
        //UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseTripleClick(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        LineColumnIndex clicked = PosToLineColumn({event.pointer.x, event.pointer.y});
        if (clicked.lineIndex < 0) return true;
        SelectLine(clicked.lineIndex);
        selectionAnchor = selectionStart;
        //isSelectingText = true;
        //UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseMove(const UCEvent& event) {
        // Scrollbar thumb dragging (pixel-based).
        if (isDraggingVerticalThumb) {
            auto bounds = GetElementLocalBounds();
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);
            int thumbHeight = verticalScrollThumb.height;
            int maxThumbY = scrollbarHeight - thumbHeight;

            int contentHeight = 0;
            if (!lineLayouts.empty() && lineLayouts.back()) {
                contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
            } else {
                contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
            }
            int viewportH = visibleTextArea.height;

            // Result is in element-local space; dragStartOffset was computed accordingly.
            int newThumbY = event.pointerGlobal.y - dragStartOffset.y - GetYInWindow();
            newThumbY = std::max(0, std::min(newThumbY, maxThumbY));

            if (maxThumbY > 0 && contentHeight > viewportH) {
                verticalScrollOffset =
                    (newThumbY * (contentHeight - viewportH)) / maxThumbY;
                verticalScrollOffset = std::max(0, std::min(verticalScrollOffset, contentHeight - viewportH));
            }

            RequestRedraw();
            return true;
        }

        if (isDraggingHorizontalThumb) {
            auto bounds = GetElementLocalBounds();
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));
            float thumbWidth = static_cast<float>(horizontalScrollThumb.width);
            float maxThumbX = scrollbarWidth - thumbWidth;

            float newThumbX = static_cast<float>(event.pointerGlobal.x - dragStartOffset.x - GetXInWindow());
            newThumbX = std::max(0.0f, std::min(newThumbX, maxThumbX));

            if (maxThumbX > 0 && maxLineWidth > visibleTextArea.width) {
                horizontalScrollOffset = static_cast<int>((newThumbX / maxThumbX) *
                                                          static_cast<float>(maxLineWidth - visibleTextArea.width));
                horizontalScrollOffset = std::max(0, std::min(horizontalScrollOffset, maxLineWidth - visibleTextArea.width));
            }

            RequestRedraw();
            return true;
        }

        // --- Scrollbar hover cursor --- (element-local coordinates)
        if (!isSelectingText) {
            auto bounds = GetElementLocalBounds();
            if (IsNeedVerticalScrollbar() &&
                event.pointer.x >= bounds.width - 15 && event.pointer.x <= bounds.width &&
                event.pointer.y >= 0 && event.pointer.y <= bounds.height) {
                SetMouseCursor(UCMouseCursor::SizeNS);
                return true;
            }
            if (IsNeedHorizontalScrollbar() &&
                event.pointer.y >= bounds.height - 15 && event.pointer.y <= bounds.height &&
                event.pointer.x >= 0 && event.pointer.x <= bounds.width) {
                SetMouseCursor(UCMouseCursor::SizeWE);
                return true;
            }
            SetMouseCursor(UCMouseCursor::Text);
        }

        // --- Markdown hover: update cursor for links/images ---
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && !isSelectingText) {
            if (!HandleMarkdownHover(event.pointer.x, event.pointer.y)) {
                SetMouseCursor(UCMouseCursor::Text);
            }
        }

        // --- Text drag selection ---
        if (isSelectingText && selectionAnchor.lineIndex >= 0) {
            LineColumnIndex drag = PosToLineColumn({event.pointer.x, event.pointer.y});
            if (drag.lineIndex < 0) return true;

            selectionStart = selectionAnchor;
            selectionEnd   = drag;
            SetCursorPosition(selectionEnd);

            if (event.pointer.y < visibleTextArea.y)                             ScrollUp(1);
            else if (event.pointer.y > visibleTextArea.y + visibleTextArea.height) ScrollDown(1);
            if (event.pointer.x < visibleTextArea.x)                             ScrollLeft(1);
            else if (event.pointer.x > visibleTextArea.x + visibleTextArea.width) ScrollRight(1);

            if (onSelectionChanged) {
                onSelectionChanged();
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
            selectionAnchor = LineColumnIndex::INVALID;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);

            // If start equals end, there's no real selection — clear it
            if (selectionStart.lineIndex >= 0 &&
                selectionStart.lineIndex  == selectionEnd.lineIndex &&
                selectionStart.columnIndex == selectionEnd.columnIndex) {
                selectionStart = LineColumnIndex::INVALID;
                selectionEnd   = LineColumnIndex::INVALID;
            }

            RequestRedraw();
            wasHandled = true;
        }

        return wasHandled;
    }

    bool UltraCanvasTextArea::HandleMouseWheel(const UCEvent& event) {
        if (!Contains(event.pointer)) return false;

        int scrollAmount = 1;

        if (editingMode == TextAreaEditingMode::Hex) {
            if (event.wheelDelta > 0) {
                hexFirstVisibleRow = std::max(0, hexFirstVisibleRow - scrollAmount);
            } else {
                int maxFirstRow = std::max(0, hexTotalRows - hexMaxVisibleRows);
                hexFirstVisibleRow = std::min(maxFirstRow, hexFirstVisibleRow + scrollAmount);
            }
        } else {
            int h = std::max(1, computedLineHeight);
            if (event.wheelDelta > 0) {
                ScrollUp(3);
            } else {
                ScrollDown(3);
            }
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
                break;
            case UCKeys::Right:
                if (event.ctrl && !event.alt) {
                    MoveCursorWordRight(event.shift);
                } else {
                    MoveCursorRight(event.shift);
                }
                break;
            case UCKeys::Up:
                if (event.ctrl && !event.alt) {
                    ScrollUp(1);
                } else {
                    MoveCursorUp(event.shift);
                }
                break;
            case UCKeys::Down:
                if (event.ctrl && !event.alt) {
                    ScrollDown(1);
                } else {
                    MoveCursorDown(event.shift);
                }
                break;
            case UCKeys::Home:
                if (event.ctrl) {
                    MoveCursorToStart(event.shift);
                } else {
                    MoveCursorToLineStart(event.shift);
                }
                break;
            case UCKeys::End:
                if (event.ctrl) {
                    MoveCursorToEnd(event.shift);
                } else {
                    MoveCursorToLineEnd(event.shift);
                }
                break;
            case UCKeys::PageUp:
                MoveCursorPageUp(event.shift);
                break;
            case UCKeys::PageDown:
                MoveCursorPageDown(event.shift);
                break;
            case UCKeys::Backspace:
                if (HasSelection()) DeleteSelection();
                else DeleteCharacterBackward();
                break;
            case UCKeys::Delete:
                if (!event.shift && event.ctrl && !event.alt) {
                    CutSelection();
                } else if (HasSelection()) {
                    DeleteSelection();
                } else {
                    DeleteCharacterForward();
                }
                break;
            case UCKeys::Enter:
                if (HasSelection()) DeleteSelection();
                InsertNewLine();
                break;
            case UCKeys::Tab:
                if (!event.alt && !event.ctrl && !event.shift) {
                    if (HasSelection()) DeleteSelection();
                    InsertTab();
                } else {
                    handled = false;
                }
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
                } else {
                    handled = false;
                }
                break;
            case UCKeys::Insert:
                if (event.shift && !event.ctrl && !event.alt) { 
                    PasteClipboard();
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
                } else handled = false;
                break;
            case UCKeys::Y:
                if (event.ctrl) {
                    Redo();
                }
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
            std::string insertTxt = event.text;
            if (insertTxt.empty() && event.character != 0 && std::isprint(event.character)) {
                std::string charStr(1, event.character);
                insertTxt = charStr;
            }
            if (!event.text.empty()) {
                if (HasSelection()) {
                    DeleteSelection();
                }
                InsertText(event.text);
                handled = true;
            }
        }

        return handled;
    }

// ===== HELPER METHODS =====

    void UltraCanvasTextArea::EnsureCursorVisible() {
        int cursorContentTop = 0;
        int cursorContentBottom = 0;
        int cursorX = 0;

        Rect2Di cursorRect = LineColumnToCursorPos(cursorPosition);
        if (cursorRect.IsValid()) {
            // LineColumnToCursorPos returns screen coords; convert back to content coords.
            cursorContentTop = cursorRect.y - visibleTextArea.y + verticalScrollOffset;
            cursorContentBottom = cursorContentTop + cursorRect.height;
            cursorX = cursorRect.x - visibleTextArea.x + horizontalScrollOffset;
        } else {
            int li = cursorPosition.lineIndex;
            if (li >= 0 && li < static_cast<int>(lineLayouts.size()) && lineLayouts[li]) {
                cursorContentTop = lineLayouts[li]->bounds.y;
                cursorContentBottom = cursorContentTop + lineLayouts[li]->bounds.height;
            } else if (currentLine) {
                cursorContentTop = currentLine->bounds.y;
                cursorContentBottom = cursorContentTop + currentLine->bounds.height;
            } else {
                return;
            }
        }

        if (cursorContentTop < verticalScrollOffset) {
            verticalScrollOffset = cursorContentTop;
        } else if (cursorContentBottom > verticalScrollOffset + visibleTextArea.height) {
            verticalScrollOffset = cursorContentBottom - visibleTextArea.height;
        }
        if (verticalScrollOffset < 0) verticalScrollOffset = 0;

        // Horizontal scroll (only when not word-wrapping).
        if (!wordWrap) {
            int line = cursorPosition.lineIndex;
            int col  = cursorPosition.columnIndex;
            if (col > 0 && line < static_cast<int>(lines.size())) {
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

        // visibleTextArea is in element-local space (ctx is translated to element origin)
        visibleTextArea = GetElementLocalBounds();
        visibleTextArea.x += style.padding;
        visibleTextArea.y += style.padding;
        visibleTextArea.width -= style.padding * 2;
        visibleTextArea.height -= style.padding * 2;
        
        if (style.showLineNumbers) {
            computedLineNumbersWidth = CalculateLineNumbersWidth(ctx);
            visibleTextArea.x += (computedLineNumbersWidth + 5);
            visibleTextArea.width -= (computedLineNumbersWidth + 5);
        }

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        computedLineHeight = static_cast<int>(static_cast<float>(ctx->GetTextLineHeight("M")) * style.lineHeight);
        if (wordWrap) {
            maxLineWidth = visibleTextArea.width;
        } else {
            maxLineWidth = 0;
            for (int i = 0; i < static_cast<int>(lines.size()); i++) {
                // Don't measure the trailing \n — it wouldn't add width, but Pango may
                // treat it as a mandatory break and return a misleading result.
                maxLineWidth = std::max(maxLineWidth,
                                        ctx->GetTextLineWidth(std::string(GetLineContentView(i))));
            }
        }
        ctx->PopState();
        // Reserve space for the scrollbars. visibleTextArea gets shrunk so downstream layout
        // width/height calculations use the true text area.
        bool needVerticalScrollbar   = false;
        bool needHorizontalScrollbar = false;
        if (IsNeedHorizontalScrollbar()) {
            needHorizontalScrollbar = true;
            visibleTextArea.height -= 15;
            if (IsNeedVerticalScrollbar()) {
                needVerticalScrollbar = true;
                visibleTextArea.width -= 15;
            }
        } else if (IsNeedVerticalScrollbar()) {
            needVerticalScrollbar = true;
            visibleTextArea.width -= 15;
            if (IsNeedHorizontalScrollbar()) {
                needHorizontalScrollbar = true;
                visibleTextArea.height -= 15;
            }
        }

        // UpdateLineLayouts detects wrap-width changes via lineLayoutsWrapWidth and
        // invalidates the cache automatically.

        if (!needVerticalScrollbar && IsNeedVerticalScrollbar()) {
            visibleTextArea.width -= 15;
            if (!needHorizontalScrollbar && IsNeedHorizontalScrollbar()) {
                visibleTextArea.height -= 15;
            }
        }

        isNeedRecalculateVisibleArea = false;
    }

    void UltraCanvasTextArea::SetWordWrap(bool wrap) {
        if (wordWrap == wrap) return;
        wordWrap = wrap;
        horizontalScrollOffset = 0;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    // ===== LINE LAYOUT CACHE =====

    void UltraCanvasTextArea::InvalidateAllLineLayouts() {
        for (auto& ll : lineLayouts) {
            ll.reset();
        }
    }

    void UltraCanvasTextArea::InvalidateLineLayout(int logicalLine) {
        if (logicalLine >= 0 && logicalLine < static_cast<int>(lineLayouts.size())) {
            lineLayouts[logicalLine].reset();
            if (logicalLine == cursorPosition.lineIndex) {
                currentLine.reset();
            }
        }
    }

    void UltraCanvasTextArea::InsertLineLayoutEntry(int logicalLine) {
        if (logicalLine < 0) return;
        if (logicalLine > static_cast<int>(lineLayouts.size())) {
            lineLayouts.resize(logicalLine);
        }
        lineLayouts.insert(lineLayouts.begin() + logicalLine, nullptr);
    }

    void UltraCanvasTextArea::RemoveLineLayoutEntry(int logicalLine) {
        if (logicalLine >= 0 && logicalLine < static_cast<int>(lineLayouts.size())) {
            lineLayouts.erase(lineLayouts.begin() + logicalLine);
        }
    }

    // ===== SEGMENT HELPERS =====

    bool UltraCanvasTextArea::LineHasNewline(int i) const {
        if (i < 0 || i >= static_cast<int>(lines.size())) return false;
        const std::string& s = lines[i];
        return !s.empty() && s.back() == '\n';
    }

    int UltraCanvasTextArea::GetLineVisibleLength(int i) const {
        if (i < 0 || i >= static_cast<int>(lines.size())) return 0;
        int n = utf8_length(lines[i]);
        return LineHasNewline(i) ? n - 1 : n;
    }

    std::string UltraCanvasTextArea::GetLineContent(int i) const {
        if (i < 0 || i >= static_cast<int>(lines.size())) return {};
        const std::string& s = lines[i];
        if (!s.empty() && s.back() == '\n') {
            return s.substr(0, s.size() - 1);
        }
        return s;
    }

    std::string_view UltraCanvasTextArea::GetLineContentView(int i) const {
        if (i < 0 || i >= static_cast<int>(lines.size())) return {};
        const std::string& s = lines[i];
        size_t sz = s.size();
        if (sz > 0 && s[sz - 1] == '\n') sz--;
        return std::string_view(s.data(), sz);
    }

    int UltraCanvasTextArea::ReshardSegment(int i) {
        if (i < 0 || i >= static_cast<int>(lines.size())) return 0;

        if (GetLineVisibleLength(i) <= lineShardHardLimit) return 0;

        bool wasTerminal = LineHasNewline(i);
        std::string body = wasTerminal
            ? lines[i].substr(0, lines[i].size() - 1)
            : lines[i];

        std::vector<std::string> shards = utf8_split_lines_sharded(body, lineShardSoftLimit, lineShardHardLimit);
        if (shards.empty()) shards.emplace_back();
        if (wasTerminal) shards.back().append("\n");

        lines[i] = std::move(shards[0]);
        InvalidateLineLayout(i);
        for (int k = 1; k < static_cast<int>(shards.size()); k++) {
            lines.insert(lines.begin() + i + k, std::move(shards[k]));
            InsertLineLayoutEntry(i + k);
        }
        return static_cast<int>(shards.size()) - 1;
    }

    std::string UltraCanvasTextArea::GetTextForSave() const {
        if (lineEndingType == LineEndingType::LF) {
            return textContent;  // already normalized
        }
        std::string eol = LineEndingSequence(lineEndingType);
        std::string out;
        out.reserve(textContent.size() + 32);
        for (char c : textContent) {
            if (c == '\n') out.append(eol);
            else out.push_back(c);
        }
        return out;
    }

    // Step 6 (pending): wire selection-background attributes.
    void UltraCanvasTextArea::ApplySelectionAttributes() {
    }

    void UltraCanvasTextArea::ClearSelectionAttributes(int /*logicalLine*/) {
    }

    // Called at the top of UpdateLineLayouts each frame. Detects cursor-line changes
    // (so MarkdownHybrid can swap in/out raw editing) and selection-range changes (so affected
    // line layouts rebuild with fresh background-color attributes).
    void UltraCanvasTextArea::ReconcileLayoutState() {
        // If the selection endpoints changed since the attributes were last applied, invalidate
        // the union of old & new line ranges so MakeLineLayout rebuilds them with fresh
        // background-color attributes (Step 6).
        bool selectionMoved =
            !(lastAppliedSelectionStart.lineIndex  == selectionStart.lineIndex &&
              lastAppliedSelectionStart.columnIndex == selectionStart.columnIndex &&
              lastAppliedSelectionEnd.lineIndex    == selectionEnd.lineIndex &&
              lastAppliedSelectionEnd.columnIndex  == selectionEnd.columnIndex);
        if (selectionMoved) {
            auto lineRange = [](LineColumnIndex s, LineColumnIndex e, int& first, int& last) {
                if (s.lineIndex < 0 || e.lineIndex < 0) { first = last = -1; return; }
                first = std::min(s.lineIndex, e.lineIndex);
                last  = std::max(s.lineIndex, e.lineIndex);
            };
            int oldFirst, oldLast, newFirst, newLast;
            lineRange(lastAppliedSelectionStart, lastAppliedSelectionEnd, oldFirst, oldLast);
            lineRange(selectionStart, selectionEnd, newFirst, newLast);
            int invStart = -1, invEnd = -1;
            if (oldFirst >= 0) { invStart = oldFirst; invEnd = oldLast; }
            if (newFirst >= 0) {
                invStart = (invStart < 0) ? newFirst : std::min(invStart, newFirst);
                invEnd   = (invEnd   < 0) ? newLast  : std::max(invEnd,   newLast);
            }
            for (int i = invStart; invStart >= 0 && i <= invEnd && i < (int)lineLayouts.size(); i++) {
                lineLayouts[i].reset();
            }
            lastAppliedSelectionStart = selectionStart;
            lastAppliedSelectionEnd   = selectionEnd;
        }
    }

    // Apply the selection background-color attribute to the portion of this line's layout text
    // that falls inside the current selection. Coordinates:
    //   selection* is in real-line codepoints (columnIndex); layout text may have a markup prefix
    //   stripped (layoutTextStartIndex in codepoints), so we subtract that before converting to
    //   layout-text bytes via utf8_cp_to_byte.
    void UltraCanvasTextArea::ApplyLineSelectionBackground(LineLayoutBase* ll, int lineIndex) {
        if (!ll || !ll->layout) return;
        if (selectionStart.lineIndex < 0 || selectionEnd.lineIndex < 0) return;

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) {
            std::swap(a, b);
        }
        if (lineIndex < a.lineIndex || lineIndex > b.lineIndex) return;
        if (a.lineIndex == b.lineIndex && a.columnIndex == b.columnIndex) return;

        int realLineCpCount = GetLineVisibleLength(lineIndex);
        int startCp = (lineIndex == a.lineIndex) ? a.columnIndex : 0;
        int endCp   = (lineIndex == b.lineIndex) ? b.columnIndex : realLineCpCount;

        // Translate from real-line codepoints to layout-text codepoints.
        startCp -= ll->layoutTextStartIndex;
        endCp   -= ll->layoutTextStartIndex;
        if (startCp < 0) startCp = 0;
        std::string layoutText = ll->layout->GetText();
        int layoutCpCount = utf8_length(layoutText);
        if (endCp > layoutCpCount) endCp = layoutCpCount;
        if (startCp >= endCp) return;

        int startByte = static_cast<int>(utf8_cp_to_byte(layoutText, startCp));
        int endByte   = static_cast<int>(utf8_cp_to_byte(layoutText, endCp));

        auto bgAttr = TextAttributeFactory::CreateBackground(style.selectionColor);
        bgAttr->SetRange(startByte, endByte);
        ll->layout->ChangeAttribute(std::move(bgAttr));
        //debugOutput << "Layout attrs: " << ll->layout->GetAttributesAsString() << " ltype: " << ll->layoutType << " str:" << layoutText << " startbyte:" << startByte << " endbyte:" << endByte << std::endl;
    }

    // BuildLineLayout removed in Step 8b — the single layout cache is populated by
    // MakeLineLayout via UpdateLineLayouts, which also applies syntax highlighting.

    // RecalculateDisplayLines / GetDisplayLineForCursor / GetDisplayLineCount removed in Step 8b —
    // wrapping and line-range queries go through each ITextLayout's GetLineByteRanges directly,
    // and vertical scroll is pixel-based from lineLayouts[i]->bounds.y.

    void UltraCanvasTextArea::RebuildText() {
        textContent.clear();
        size_t total = 0;
        for (const auto& l : lines) total += l.size();
        textContent.reserve(total);
        for (const auto& l : lines) textContent.append(l);
        markdownIndexDirty = true;
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
        if (editingMode == TextAreaEditingMode::Hex) {
            return hexTotalRows > hexMaxVisibleRows;
        }
        // Total content height > viewport height.
        int contentHeight = 0;
        if (!lineLayouts.empty() && lineLayouts.back()) {
            contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
        } else {
            contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
        }
        return contentHeight > visibleTextArea.height;
    }

    bool UltraCanvasTextArea::IsNeedHorizontalScrollbar() {
        if (editingMode == TextAreaEditingMode::Hex) return false;
        if (wordWrap) return false;
        return maxLineWidth > visibleTextArea.width;
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
        // Pixel scroll: find the content Y of the requested line from the layout cache.
        if (line < 0) line = 0;
        int targetY = 0;
        auto lineLayout = GetActualLineLayout(line);
        if (lineLayout) {
            targetY = lineLayout->bounds.y;
        }
        verticalScrollOffset = std::max(0, targetY);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollUp(int lineCount) {
        int h = static_cast<int>(style.fontStyle.fontSize * 1.3f);
        verticalScrollOffset = std::max(0, verticalScrollOffset - lineCount * h);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollDown(int lineCount) {
        int h = static_cast<int>(style.fontStyle.fontSize * 1.3f);
        verticalScrollOffset += lineCount * h;
        if (verticalScrollOffset < 0) verticalScrollOffset = 0;
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
        // Public API: pixel-scroll to the given logical line's top.
        ScrollTo(line);
    }
// ===== UNDO/REDO =====

    void UltraCanvasTextArea::SaveState() {
        TextState state;
        state.text           = textContent;
        state.cursorPosition = cursorPosition;
        state.selectionStart = selectionStart;
        state.selectionEnd   = selectionEnd;

        undoStack.push_back(state);
        if (undoStack.size() > maxUndoStackSize) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void UltraCanvasTextArea::Undo() {
        if (editingMode == TextAreaEditingMode::Hex) { HexUndo(); return; }
        if (undoStack.empty()) return;

        TextState currentState;
        currentState.text           = textContent;
        currentState.cursorPosition = cursorPosition;
        currentState.selectionStart = selectionStart;
        currentState.selectionEnd   = selectionEnd;
        redoStack.push_back(currentState);

        TextState previousState = undoStack.back();
        undoStack.pop_back();

        SetText(previousState.text);
        SetCursorPosition(previousState.cursorPosition);
        selectionStart   = previousState.selectionStart;
        selectionEnd     = previousState.selectionEnd;
        RequestRedraw();
    }

    void UltraCanvasTextArea::Redo() {
        if (editingMode == TextAreaEditingMode::Hex) { HexRedo(); return; }
        if (redoStack.empty()) return;

        TextState currentState;
        currentState.text           = textContent;
        currentState.cursorPosition = cursorPosition;
        currentState.selectionStart = selectionStart;
        currentState.selectionEnd   = selectionEnd;
        undoStack.push_back(currentState);

        TextState nextState = redoStack.back();
        redoStack.pop_back();

        SetText(nextState.text);
        SetCursorPosition(nextState.cursorPosition);
        selectionStart   = nextState.selectionStart;
        selectionEnd     = nextState.selectionEnd;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::CanUndo() const {
        if (editingMode == TextAreaEditingMode::Hex) return !hexUndoStack.empty();
        return !undoStack.empty();
    }
    bool UltraCanvasTextArea::CanRedo() const {
        if (editingMode == TextAreaEditingMode::Hex) return !hexRedoStack.empty();
        return !redoStack.empty();
    }

// ===== SYNTAX HIGHLIGHTING =====

    void UltraCanvasTextArea::SetHighlightSyntax(bool on) {
        style.highlightSyntax = on;
        if (on && !syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        InvalidateAllLineLayouts();
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetProgrammingLanguage(const std::string& language) {
        if (style.highlightSyntax) {
            if (!syntaxTokenizer) {
                syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
            }
            syntaxTokenizer->SetLanguage(language);
            InvalidateAllLineLayouts();
            RequestRedraw();
        }
    }

    bool UltraCanvasTextArea::SetProgrammingLanguageByExtension(const std::string& extension) {
        if (!syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        auto result = syntaxTokenizer->SetLanguageByExtension(extension);
        if (style.highlightSyntax) {
            InvalidateAllLineLayouts();
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

// ===== THEMES =====

    void UltraCanvasTextArea::ApplyDarkTheme() {
        style.backgroundColor = {30, 30, 30, 255};
        style.fontColor = {210, 210, 210, 255};
        style.currentLineColor = {60, 60, 60, 255};
        style.lineNumbersColor = {80, 80, 80, 255};           // Dimmer — less visual noise in dark mode
        style.lineNumbersBackgroundColor = {35, 35, 35, 255}; // Dark gutter background
        style.selectionColor = {60, 90, 150, 100};
        style.cursorColor = {255, 255, 255, 255};
        style.scrollbarTrackColor = {50, 50, 50, 255};
        style.scrollbarColor = {100, 100, 100, 255};
        style.lineNumbersColor = {160, 160, 160, 255};  // Match tab title brightness

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

        SetMarkdownStyle(MarkdownHybridStyle::DarkMode());
        RequestRedraw();
    }
    void UltraCanvasTextArea::ApplyLightTheme() {
        style.fontColor = {0, 0, 0, 255};
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
        style.scrollbarTrackColor = {128, 128, 128, 255};
        style.scrollbarColor = {200, 200, 200, 255};

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

        SetMarkdownStyle(MarkdownHybridStyle::Default());
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
        // lastSearchPosition is semantically "start of the last match", not cursor.
        // After FindNext the cursor sits at match END (selectionEnd); seeding from cursor
        // would cause a subsequent FindPrevious to re-select the same match. Prefer the
        // selection start when available — that equals the match start after any Find.
        if (editingMode == TextAreaEditingMode::Hex) {
            lastSearchPosition = (hexSelectionStart >= 0) ? hexSelectionStart : hexCursorByteOffset;
        } else {
            int selMin = GetSelectionMinGrapheme();
            lastSearchPosition = (selMin >= 0)
                ? selMin
                : GetPositionFromLineColumn(cursorPosition.lineIndex, cursorPosition.columnIndex);
        }
    }

    void UltraCanvasTextArea::FindFirst() {
        lastSearchPosition = -1;
        FindNext();
    }

    void UltraCanvasTextArea::FindNext() {
        if (lastSearchText.empty()) return;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = lastSearchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            size_t startFrom = (lastSearchPosition >= 0) ? lastSearchPosition + 1 : 0;
            size_t found = haystack.find(needle, startFrom);
            if (found == std::string::npos && lastSearchPosition > 0) {
                found = haystack.find(needle, 0);
            }
            if (found != std::string::npos) {
                int pos = static_cast<int>(found);
                int len = static_cast<int>(lastSearchText.size());
                hexSelectionStart = pos;
                hexSelectionEnd = pos + len;
                hexCursorByteOffset = pos;
                lastSearchPosition = pos;
                HexEnsureCursorVisible();
                RequestRedraw();
            }
            return;
        }

        int foundPos = utf8_find(textContent, lastSearchText, lastSearchPosition + 1, lastSearchCaseSensitive);

        if (foundPos < 0 && lastSearchPosition > 0) {
            foundPos = utf8_find(textContent, lastSearchText, 0, lastSearchCaseSensitive);
        }

        if (foundPos >= 0) {
            int searchLen = utf8_length(lastSearchText);
            auto [sLine, sCol] = GetLineColumnFromPosition(foundPos);
            auto [eLine, eCol] = GetLineColumnFromPosition(foundPos + searchLen);
            selectionStart = {sLine, sCol};
            selectionEnd   = {eLine, eCol};
            SetCursorPosition(selectionEnd);
            lastSearchPosition = foundPos;
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::FindPrevious() {
        if (lastSearchText.empty()) return;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = lastSearchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            size_t found = std::string::npos;
            if (lastSearchPosition > 0) {
                found = haystack.rfind(needle, lastSearchPosition - 1);
            }
            if (found == std::string::npos) {
                found = haystack.rfind(needle);
                if (found != std::string::npos && static_cast<int>(found) == lastSearchPosition) {
                    return; // Only one match, already selected
                }
            }
            if (found != std::string::npos) {
                int pos = static_cast<int>(found);
                int len = static_cast<int>(lastSearchText.size());
                hexSelectionStart = pos;
                hexSelectionEnd = pos + len;
                hexCursorByteOffset = pos;
                lastSearchPosition = pos;
                HexEnsureCursorVisible();
                RequestRedraw();
            }
            return;
        }

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
            auto [sLine, sCol] = GetLineColumnFromPosition(foundPos);
            auto [eLine, eCol] = GetLineColumnFromPosition(foundPos + searchLen);
            selectionStart = {sLine, sCol};
            selectionEnd   = {eLine, eCol};
            SetCursorPosition(selectionStart);
            lastSearchPosition = foundPos;
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

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int searchLen = static_cast<int>(searchText.size());
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                searchHighlights.push_back({static_cast<int>(pos), static_cast<int>(pos) + searchLen});
                pos += searchLen;
            }
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
            if (bm > cursorPosition.lineIndex) {
                GoToLine(bm + 1);
                return;
            }
        }
        GoToLine(bookmarks[0] + 1);
    }

    void UltraCanvasTextArea::PreviousBookmark() {
        if (bookmarks.empty()) return;
        for (auto it = bookmarks.rbegin(); it != bookmarks.rend(); ++it) {
            if (*it < cursorPosition.lineIndex) {
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

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, endLine = b.lineIndex;

        SaveState();
        std::string indent(tabSize, ' ');
        for (int i = startLine; i <= endLine; ) {
            lines[i].insert(0, indent);
            InvalidateLineLayout(i);
            int added = ReshardSegment(i);
            endLine += added;
            i += 1 + added;
        }
        RebuildText();
    }

    void UltraCanvasTextArea::UnindentSelection() {
        if (!HasSelection()) return;

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, endLine = b.lineIndex;

        SaveState();
        
        for (int i = startLine; i <= endLine; i++) {
            int spacesToRemove = 0;
            for (int j = 0; j < tabSize && j < GetLineVisibleLength(i); j++) {
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
                InvalidateLineLayout(i);
            }
        }

        RebuildText();
    }

    int UltraCanvasTextArea::CalculateLineNumbersWidth(IRenderContext* ctx) {
        if (!style.showLineNumbers) return 0;

        int maxLineNumber = static_cast<int>(lines.size());

        // Count digits needed
        int digits = 1;
        int temp = maxLineNumber;
        while (temp >= 10) {
            digits++;
            temp /= 10;
        }

        // Minimum 2 digits (lines 1-99 should not produce a tiny gutter)
        digits = std::max(digits, 2);

        // Measure the width of the widest digit string at current font
        // Use '9' repeated since it's typically the widest digit
        std::string maxNumberStr(digits, '9');

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        int textWidth = ctx->GetTextLineWidth(maxNumberStr);
        ctx->PopState();

        // Add padding: left margin + right margin before the separator line
        int padding = 8; // 4px left + 4px right
        return textWidth + padding;
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

    int UltraCanvasTextArea::CountMatches(const std::string& searchText, bool caseSensitive) const {
        if (searchText.empty()) return 0;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!caseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int count = 0;
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                count++;
                pos += needle.size();
            }
            return count;
        }

        int count = 0;
        int pos = 0;
        int searchLen = utf8_length(searchText);

        while ((pos = utf8_find(textContent, searchText, pos, caseSensitive)) >= 0) {
            count++;
            pos += searchLen;
        }
        return count;
    }

    int UltraCanvasTextArea::GetCurrentMatchIndex(const std::string& searchText, bool caseSensitive) const {
        if (searchText.empty()) return 0;

        if (editingMode == TextAreaEditingMode::Hex) {
            if (hexSelectionStart < 0 || hexSelectionEnd < 0) return 0;
            int currentPos = std::min(hexSelectionStart, hexSelectionEnd);

            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!caseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int index = 0;
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                index++;
                if (static_cast<int>(pos) == currentPos) return index;
                pos += needle.size();
            }
            return 0;
        }

        if (!HasSelection()) return 0;

        // The current match is the one at the lesser selection endpoint.
        int currentPos = GetSelectionMinGrapheme();
        if (currentPos < 0) currentPos = 0;

        int index = 0;
        int pos = 0;
        int searchLen = utf8_length(searchText);

        while ((pos = utf8_find(textContent, searchText, pos, caseSensitive)) >= 0) {
            index++;
            if (pos == currentPos) {
                return index;
            }
            pos += searchLen;
        }
        return 0; // Current selection doesn't match any occurrence
    }

// ===== EDITING MODE SWITCHING =====

    void UltraCanvasTextArea::SetEditingMode(TextAreaEditingMode mode) {
        if (editingMode == mode) return;

        TextAreaEditingMode oldMode = editingMode;

        // Leaving hex mode: convert buffer back to text
        if (oldMode == TextAreaEditingMode::Hex && mode != TextAreaEditingMode::Hex) {
            textContent = std::string(hexBuffer.begin(), hexBuffer.end());
            lineEndingType = DetectLineEnding(textContent);
            lines = utf8_split_lines(textContent);
            if (lines.empty()) lines.push_back(std::string());

            SetCursorPosition({0, 0});
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
            hexUndoStack.clear();
            hexRedoStack.clear();
        }

        // Entering hex mode: convert text to buffer
        if (mode == TextAreaEditingMode::Hex && oldMode != TextAreaEditingMode::Hex) {
            hexBuffer.assign(textContent.begin(), textContent.end());
            hexCursorByteOffset = 0;
            hexCursorInAsciiPanel = false;
            hexCursorNibble = 0;
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
            hexFirstVisibleRow = 0;
            hexUndoStack.clear();
            hexRedoStack.clear();
        }

        // Entering markdown mode: set up syntax highlighting for raw markdown
        if (mode == TextAreaEditingMode::MarkdownHybrid && oldMode != TextAreaEditingMode::MarkdownHybrid) {
            SetHighlightSyntax(true);
            if (syntaxTokenizer) {
                syntaxTokenizer->SetLanguage("Markdown");
            }
        }

        editingMode = mode;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    // Line Layouts — all coordinates in these helpers are SCREEN coords (relative to the root
    // canvas), not widget-local or content-local. The translation chain is:
    //   screen.x = visibleTextArea.x + lineLayout.bounds.x + lineLayout.layoutShift.x
    //   screen.y = visibleTextArea.y + lineLayout.bounds.y + lineLayout.layoutShift.y
    //              - verticalScrollOffset
    // Real-line codepoints translate to layout-text bytes via:
    //   layoutCp = columnIndex - layoutTextStartIndex  (clamped to [0, layout-cp-count])
    //   layoutByte = utf8_cp_to_byte(layout.GetText(), layoutCp)
    LineLayoutBase *UltraCanvasTextArea::GetLineLayoutForPos(const Point2Di& pos) {
        int contentY = pos.y - visibleTextArea.y + verticalScrollOffset;
        for (auto const& ll : lineLayouts) {
            if (!ll) continue;
            if (ll->bounds.y > contentY) break;
            if (contentY < ll->bounds.y + ll->bounds.height) return ll.get();
        }
        return nullptr;
    }

    Rect2Di UltraCanvasTextArea::LineColumnToCursorPos(const LineColumnIndex& idx) {
        if (!idx.IsValid()) {
            return Rect2Di::INVALID;
        }
        LineLayoutBase* line = GetActualLineLayout(idx.lineIndex);

        // make plain line for formatted line as plain line will be edited then
        std::unique_ptr<LineLayoutBase> tmplPLainLineLayoutPtr;
        if (line->layoutType != LineLayoutType::PlainLine) {
            tmplPLainLineLayoutPtr = MakePlainLineLayout(GetRenderContext(), idx.lineIndex);
            tmplPLainLineLayoutPtr->bounds.x = line->bounds.x;
            tmplPLainLineLayoutPtr->bounds.y = line->bounds.y;
            line = tmplPLainLineLayoutPtr.get();
        }

        if (!line || !line->layout) return Rect2Di::INVALID;

        std::string layoutText = line->layout->GetText();
        int layoutCpCount = utf8_length(layoutText);
        int layoutCol = std::max(0, std::min(idx.columnIndex - line->layoutTextStartIndex,
                                             layoutCpCount));
        int byteIndex = static_cast<int>(utf8_cp_to_byte(layoutText, layoutCol));

        Rect2Di cursorRect = line->layout->GetCursorPos(byteIndex).strongPos;
        cursorRect.x += visibleTextArea.x + line->bounds.x + line->layoutShift.x
                        - horizontalScrollOffset;
        cursorRect.y += visibleTextArea.y + line->bounds.y + line->layoutShift.y
                        - verticalScrollOffset;
        return cursorRect;
    }

    LineColumnIndex UltraCanvasTextArea::PosToLineColumn(const Point2Di& pos) {
        // Convert screen → content coords (pre-scroll, relative to visibleTextArea top-left).
        int contentY = pos.y - visibleTextArea.y + verticalScrollOffset;
        int contentX = pos.x - visibleTextArea.x + horizontalScrollOffset;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            auto* ll = GetActualLineLayout(i);
            if (!ll) continue;
            if (ll->bounds.y > contentY) {
                // Clicked above this line but past all prior lines → snap to start of this line.
                return {i, ll->layoutTextStartIndex};
            }
            if (contentY >= ll->bounds.y + ll->bounds.height) continue;
            if (!ll->layout) return {i, ll->layoutTextStartIndex};

            // make plain line for formatted line as plain line will be edited then
            std::unique_ptr<LineLayoutBase> lineLayoutPtr;
            if (ll->layoutType != LineLayoutType::PlainLine) {
                lineLayoutPtr = MakePlainLineLayout(GetRenderContext(), i);
                lineLayoutPtr->bounds.y = ll->bounds.y;
                lineLayoutPtr->bounds.x = ll->bounds.x;
                ll = lineLayoutPtr.get();
            }
            // Translate to layout-local coords.
            int layoutX = contentX - (ll->bounds.x + ll->layoutShift.x);
            int layoutY = contentY - (ll->bounds.y + ll->layoutShift.y);
            auto hit = ll->layout->XYToIndex(std::max(0, layoutX), std::max(0, layoutY));

            std::string layoutText = ll->layout->GetText();
            int layoutCp = utf8_byte_to_cp(layoutText, hit.index) + hit.trailing;
            return {i, layoutCp + ll->layoutTextStartIndex};
        }
        // Past the last line — land at the end of the document.
        if (!lineLayouts.empty() && lineLayouts.back()) {
            int last = (int)lineLayouts.size() - 1;
            return {last, GetLineVisibleLength(last)};
        }
        return LineColumnIndex::INVALID;
    }

    LineLayoutBase* UltraCanvasTextArea::GetActualLineLayout(int idx) {
        LineLayoutBase* line;
        if (currentLine && cursorPosition.lineIndex == idx) {
            return currentLine.get();
        }
        if (idx >= 0 && idx <= (int)lineLayouts.size()) {
            return lineLayouts[idx].get();
        }
        return nullptr;
    }

    void UltraCanvasTextArea::UpdateLineLayouts(IRenderContext* ctx) {
        // Refresh the markdown-wide index (abbreviations / footnotes / anchors / backlinks)
        // before line layouts query it. Lazy — only reruns when content changed.
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && markdownIndexDirty) {
            RebuildMarkdownIndex();
            // Index shifts may change which headings show the ↩ icon or which abbreviations
            // apply; throw all MD layouts including the edit-mode stash.
            for (auto& ll : lineLayouts) ll.reset();
        }

        ReconcileLayoutState();

        // If the wrap width changed (resize, word-wrap toggle), every cached layout's line-break
        // positions and heights are stale. Invalidate the whole cache before rebuilding.
        int currentWrapWidth = wordWrap ? visibleTextArea.width : 0;
        if (currentWrapWidth != lineLayoutsWrapWidth) {
            for (auto& ll : lineLayouts) ll.reset();
            lineLayoutsWrapWidth = currentWrapWidth;
        }

        lineLayouts.resize(lines.size());

        // Pass 1: build any missing line layouts. Track contiguous table row spans so we can
        // normalize their column widths in a second pass (each table's cell widths depend on
        // the widest content across all its rows, not just one line).
        //
        // Grouping uses the RAW line text (trimmed leading `|`) rather than the cached layout
        // type. Under MarkdownHybrid, the cursor line is built as PlainLine (raw-edit mode),
        // which would otherwise split a table into two groups — one above and one below the
        // cursor — and they'd negotiate different column widths. Using raw text keeps the
        // table contiguous; NormalizeTableGroupWidths skips rows that aren't TableLineLayout.
        std::vector<std::pair<int,int>> tableSpans;   // inclusive [start, end] line indices
        int groupStart = -1;
        auto isTableRowByText = [&](int idx) -> bool {
            if (idx < 0 || idx >= (int)lines.size()) return false;
            std::string t = TrimWhitespace(GetLineContent(idx));
            return !t.empty() && t[0] == '|';
        };
        int prevLogicalLineNumber = 0;
        int currentLogicalLineNumber = 1;
        for(int i = 0; i < (int)lines.size(); i++) {
            if (!lineLayouts[i]) {
                lineLayouts[i] = MakeLineLayout(ctx, i);
            }
            if (prevLogicalLineNumber != currentLogicalLineNumber) {
                lineLayouts[i]->logicalLineNumber = currentLogicalLineNumber;
                prevLogicalLineNumber = currentLogicalLineNumber;
            } else {
                lineLayouts[i]->logicalLineNumber = 0;
            }
            if (LineHasNewline(i)) {
                currentLogicalLineNumber++;
            }
            bool inTable = isTableRowByText(i);
            if (inTable) {
                if (groupStart < 0) groupStart = i;
            } else if (groupStart >= 0) {
                tableSpans.push_back({groupStart, i - 1});
                groupStart = -1;
            }
        }

        if (cursorPosition.lineIndex >= 0) {
            currentLine = MakePlainLineLayout(ctx, cursorPosition.lineIndex);
            currentLine->logicalLineNumber = lineLayouts[cursorPosition.lineIndex]->logicalLineNumber;
        } else {
            currentLine.reset();
        }

        if (groupStart >= 0) tableSpans.push_back({groupStart, (int)lines.size() - 1});

        // Pass 2: normalize per-table column widths. Updates each cell's layout wrap width,
        // bounds.x / width, and the row's bounds.height (so Pass 3 sees correct heights).
        for (auto [s, e] : tableSpans) {
            NormalizeTableGroupWidths(s, e);
        }

        // Pass 3: cascade cumulative Y positions. Must run after table heights are finalized.
        int prevLineBottomPos = 0;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            if (!lineLayouts[i]) continue;

            lineLayouts[i]->bounds.y = prevLineBottomPos;
            if (cursorPosition.lineIndex == i) {
                currentLine->bounds.y = prevLineBottomPos;
                prevLineBottomPos += currentLine->bounds.height;
            } else {
                prevLineBottomPos += lineLayouts[i]->bounds.height;
            }
        }
    }

    void UltraCanvasTextArea::UpdateGeometry(IRenderContext* ctx) {
        // Base class sets needsUpdateGeometry = false via RequestUpdateGeometry tracking;
        // we only need to drive our own caches here. Parent bounds changes land through
        // SetBounds → isNeedRecalculateVisibleArea, which the legacy Render path consumes first.
        UpdateLineLayouts(ctx);

        if (isNeedRecalculateVisibleArea) {
            ctx->PushState();
            if (editingMode == TextAreaEditingMode::Hex) {
                CalculateHexLayout();
            } else {
                CalculateVisibleArea();
            }
            ctx->PopState();
            // Wrap width probably changed — let UpdateLineLayouts detect and reset.
        }
        UltraCanvasUIElement::UpdateGeometry(ctx);
    }

    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakePlainLineLayout(IRenderContext* ctx, int lineIndex) {
        auto ll = std::make_unique<LineLayoutBase>();
        // Content without the trailing \n — Pango must never see the line terminator,
        // or it would generate a phantom wrapped line under the segment.
        std::string txt = GetLineContent(lineIndex);

        ll->layoutType = LineLayoutType::PlainLine;
        ll->layout = ctx->CreateTextLayout(txt, false);
        ll->layout->SetFontStyle(style.fontStyle);
        //ll->layout->SetLineSpacing(style.lineHeight);
        if (wordWrap && visibleTextArea.width > 0) {
            ll->layout->SetExplicitWidth(visibleTextArea.width);
            ll->layout->SetWrap(TextWrap::WrapWordChar);
        }

        if (style.highlightSyntax && syntaxTokenizer) {
            auto tokens = syntaxTokenizer->TokenizeLine(txt);  // txt is already content-only

            int currentByte = 0;
            for (const auto& token : tokens) {
                int startByte = currentByte;
                int endByte = currentByte + static_cast<int>(token.text.size());
                const TokenStyle& tokenStyle = GetStyleForTokenType(token.type);

                auto fgAttr = TextAttributeFactory::CreateForeground(tokenStyle.color);
                fgAttr->SetRange(startByte, endByte);
                ll->layout->InsertAttribute(std::move(fgAttr));

                if (tokenStyle.bold) {
                    auto wAttr = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                    wAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(wAttr));
                }
                if (tokenStyle.italic) {
                    auto sAttr = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                    sAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(sAttr));
                }
                if (tokenStyle.underline) {
                    auto uAttr = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                    uAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(uAttr));
                }
                currentByte = endByte;
            }
        }
        ApplyLineSelectionBackground(ll.get(), lineIndex);

        ll->bounds.width = ll->layout->GetLayoutWidth();
        ll->bounds.height = ll->layout->GetLayoutHeight();
        return ll;
    }

    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakeLineLayout(IRenderContext* ctx, int lineIndex) {
        if (lineIndex < 0 || lineIndex >= (int)lines.size()) return nullptr;

        if (editingMode == TextAreaEditingMode::MarkdownHybrid) {
            auto md = MakeMarkdownLineLayout(ctx, lineIndex);
            ApplyLineSelectionBackground(md.get(), lineIndex);
            return md;
        }

        // PlainText path: full-line layout with optional syntax highlighting attributes.
        return MakePlainLineLayout(ctx, lineIndex);
    }

    void UltraCanvasTextArea::RenderLineLayout(IRenderContext *ctx, LineLayoutBase* line) {
        if (!line) return;

        // All style values sourced from markdownStyle so callers can customize via
        // SetMarkdownStyle() / GetMarkdownStyleMutable().
        const int quoteBarWidth    = markdownStyle.quoteBarWidth;
        const int quoteNestingStep = markdownStyle.quoteNestingStep;
        const int listIndentPx     = markdownStyle.listIndent;
        const Color& quoteBarColor = markdownStyle.quoteBarColor;
        const Color& codeBgColor   = markdownStyle.codeBlockBackgroundColor;
        const Color& hrColor       = markdownStyle.horizontalRuleColor;
        const Color& markerColor   = markdownStyle.bulletColor;

        // bounds.x/y are content-relative (top of text area, before scroll). verticalScrollOffset
        // is the pixel Y-offset into the content that corresponds to the top of visibleTextArea.
        Point2Di posOrigin{visibleTextArea.x + line->bounds.x - horizontalScrollOffset,
                              visibleTextArea.y + line->bounds.y - verticalScrollOffset};
        Point2Di layoutOrigin{posOrigin.x + line->layoutShift.x,
                              posOrigin.y + line->layoutShift.y};

        auto drawLayout = [&]() {
            if (line->layout) {
                ctx->SetCurrentPaint(style.fontColor);
                ctx->DrawTextLayout(*line->layout,
                    Point2Df(static_cast<float>(layoutOrigin.x),
                             static_cast<float>(layoutOrigin.y)));
            }
        };

        switch (line->layoutType) {
            case LineLayoutType::PlainLine:
            case LineLayoutType::MarkDownLine: {
                drawLayout();
                return;
            }
            case LineLayoutType::HorizontalLine: {
                // Horizontal rule: draw a single line centered in the bounds.
                int centerY = posOrigin.y + line->bounds.height / 2;
                ctx->DrawLine(
                        Point2Df(static_cast<float>(posOrigin.x),
                                 static_cast<float>(centerY)),
                        Point2Df(static_cast<float>(posOrigin.x + line->bounds.width),
                                 static_cast<float>(centerY)),
                        hrColor);
                return;
            }
            case LineLayoutType::Blockquote: {
                auto bq = dynamic_cast<BlockquoteLayout*>(line);
                int depth = bq ? bq->quoteLevel : 1;
                for (int d = 1; d <= depth; d++) {
                    int barX = posOrigin.x + (d - 1) * quoteNestingStep;
                    ctx->DrawFilledRectangle(
                        Rect2Df(static_cast<float>(barX),
                                static_cast<float>(posOrigin.y),
                                static_cast<float>(quoteBarWidth),
                                static_cast<float>(line->bounds.height)),
                        quoteBarColor, 0.0f, Colors::Transparent);
                }
                drawLayout();
                return;
            }
            case LineLayoutType::CodeblockStart:
            case LineLayoutType::CodeblockEnd:
            case LineLayoutType::CodeblockContent: {
                // Background always spans the full visible text area width, not just the
                // measured content width — so contiguous code rows visually merge into one
                // uniform block regardless of per-line text length.

                double borderTop = (line->layoutType == LineLayoutType::CodeblockStart) ? 1 : 0;
                double borderBottom = (line->layoutType == LineLayoutType::CodeblockEnd) ? 1 : 0;
                double y = (line->layoutType == LineLayoutType::CodeblockStart)
                        ? (posOrigin.y + line->bounds.height / 2 + 2) : posOrigin.y;
                double h = (line->layoutType == LineLayoutType::CodeblockEnd || line->layoutType == LineLayoutType::CodeblockStart)
                        ? line->bounds.height / 2 : line->bounds.height;
                ctx->SetFillPaint(codeBgColor);
                const Color& cbBorder = markdownStyle.codeBlockBorderColor;
                ctx->DrawRoundedRectangleWidthBorders(
                    Rect2Df(visibleTextArea.x,
                            y,
                            visibleTextArea.width,
                            h),
                    true,
                    1, 1, borderTop, borderBottom,
                    cbBorder, cbBorder,
                    cbBorder, cbBorder,
                    0,0,0,0,
                    UCDashPattern::EMPTY, UCDashPattern::EMPTY,
                    UCDashPattern::EMPTY, UCDashPattern::EMPTY
                );
                if (line->layout) {
                    ctx->SetCurrentPaint(markdownStyle.codeTextColor);
                    ctx->DrawTextLayout(*line->layout,
                        Point2Df(static_cast<float>(layoutOrigin.x),
                                static_cast<float>(layoutOrigin.y)));
                }
                return;
            }
            case LineLayoutType::UnorderedListItem: {
                auto ul = dynamic_cast<UnorderedListItemLayout*>(line);
                int depth = ul ? ul->listDepth : 1;
                const auto& bullets = markdownStyle.nestedBulletCharacters;
                int bulletIdx = std::min(std::max(depth - 1, 0),
                                         static_cast<int>(bullets.size()) - 1);
                const std::string& bullet = bullets[bulletIdx];
                int markerX = posOrigin.x + (depth - 1) * listIndentPx;
                ctx->PushState();
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(markerColor);
                ctx->DrawText(bullet, Point2Df(static_cast<float>(markerX),
                                               static_cast<float>(posOrigin.y)));
                ctx->PopState();
                drawLayout();
                return;
            }
            case LineLayoutType::OrderedListItem: {
                auto ol = dynamic_cast<OrderedListItemLayout*>(line);
                int number = ol ? ol->orderedItemNumber : 1;
                int depth = ol ? ol->listDepth : 1;
                std::string numStr = std::to_string(number) + ".";
                int markerX = posOrigin.x + (depth - 1) * listIndentPx;
                ctx->PushState();
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(markerColor);
                ctx->DrawText(numStr, Point2Df(static_cast<float>(markerX),
                                               static_cast<float>(posOrigin.y)));
                ctx->PopState();
                drawLayout();
                return;
            }
            case LineLayoutType::TableHederRow:
            case LineLayoutType::TableSeparatorRow:
            case LineLayoutType::TableRow: {
                auto tbl = dynamic_cast<TableLineLayout*>(line);
                if (!tbl) {
                    drawLayout(); return;
                }
                const Color& tableBorderColor = markdownStyle.tableBorderColor;
                const Color& tableHeaderBg    = markdownStyle.tableHeaderBackground;

                // Header row background tint.
                if (line->layoutType == LineLayoutType::TableHederRow) {
                    ctx->DrawFilledRectangle(
                        Rect2Df(static_cast<float>(posOrigin.x),
                                static_cast<float>(posOrigin.y),
                                static_cast<float>(tbl->bounds.width),
                                static_cast<float>(tbl->bounds.height)),
                        tableHeaderBg, 0.0f, Colors::Transparent);
                }

                if (line->layoutType == LineLayoutType::TableSeparatorRow) {
                    int midY = posOrigin.y + tbl->bounds.height / 2;
                    ctx->DrawLine(
                        Point2Df(static_cast<float>(posOrigin.x),
                                 static_cast<float>(midY)),
                        Point2Df(static_cast<float>(posOrigin.x + tbl->bounds.width),
                                 static_cast<float>(midY)),
                        tableBorderColor);
                } else {
                    // Render each cell at its layout-local origin.
                    for (const auto& cell : tbl->cellsLayouts) {
                        if (!cell || !cell->layout) continue;
                        ctx->SetCurrentPaint(style.fontColor);
                        ctx->DrawTextLayout(*cell->layout,
                            Point2Df(static_cast<float>(posOrigin.x + cell->bounds.x),
                                     static_cast<float>(posOrigin.y + cell->bounds.y)));
                    }
                }

                // Vertical dividers between columns. Each cell's right edge (in layout-local
                // coords) is `cell->bounds.x + cell->bounds.width + cellPadding`; we place a
                // divider there for every cell except the last.
                const int cellPadding = static_cast<int>(markdownStyle.tableCellPadding);
                for (size_t c = 0; c + 1 < tbl->cellsLayouts.size(); c++) {
                    const auto& cell = tbl->cellsLayouts[c];
                    if (!cell) continue;
                    int bx = posOrigin.x + cell->bounds.x + cell->bounds.width + cellPadding;
                    ctx->DrawLine(
                        Point2Df(static_cast<float>(bx),
                                 static_cast<float>(posOrigin.y)),
                        Point2Df(static_cast<float>(bx),
                                 static_cast<float>(posOrigin.y + tbl->bounds.height))
                        );
                }
                if (line->layoutType != LineLayoutType::TableSeparatorRow) {
                    if (tbl->lastTableRow) {
                        int by = posOrigin.y + tbl->bounds.height;
                        ctx->DrawLine(
                                Point2Df(static_cast<float>(posOrigin.x),
                                         static_cast<float>(by)),
                                Point2Df(static_cast<float>(posOrigin.x + tbl->bounds.width),
                                         static_cast<float>(by)),
                                tableBorderColor);
                    }
                }
                return;
            }
            case LineLayoutType::DefinitionTerm:
            case LineLayoutType::DefinitionContinuation:
                // Definition term / continuation share the paragraph render path. The term has
                // a bold-weight attribute applied at build time; the continuation uses layoutShift
                // for its indent. No additional decoration needed.
                drawLayout();
                return;
        }
    }
} // namespace UltraCanvas