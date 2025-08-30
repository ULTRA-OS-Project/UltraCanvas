// UltraCanvasTextArea.h
// Multi-line text editor component with scrollbars, line numbers, and syntax highlighting
// Version: 1.3.0
// Last Modified: 2025-08-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== TEXT AREA CONFIGURATION =====
    enum class LineWrapMode {
        NoneWrap,
        Word,
        Character
    };

    enum class ScrollBarMode {
        Auto,
        AlwaysShow,  // Changed from "Always" to avoid X11 conflict
        Never
    };

    struct TextAreaStyle {
        UltraCanvas::Color backgroundColor = UltraCanvas::Colors::White;
        UltraCanvas::Color textColor = UltraCanvas::Colors::Black;
        UltraCanvas::Color borderColor = UltraCanvas::Colors::Gray;
        UltraCanvas::Color selectionColor = UltraCanvas::Color(173, 216, 230, 128); // Light blue with transparency
        UltraCanvas::Color lineNumberColor = UltraCanvas::Colors::Gray;
        UltraCanvas::Color lineNumberBackgroundColor = UltraCanvas::Color(248, 248, 248);
        UltraCanvas::Color scrollbarColor = UltraCanvas::Color(200, 200, 200);
        UltraCanvas::Color scrollbarThumbColor = UltraCanvas::Color(160, 160, 160);

        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        int lineHeight = 16;

        bool showLineNumbers = false;
        bool showScrollbars = true;
        LineWrapMode wrapMode = LineWrapMode::NoneWrap;
        ScrollBarMode verticalScrollMode = ScrollBarMode::Auto;
        ScrollBarMode horizontalScrollMode = ScrollBarMode::Auto;

        int tabSize = 4;
        bool convertTabsToSpaces = false;

        static TextAreaStyle Default() {
            return TextAreaStyle();
        }

        static TextAreaStyle Code() {
            TextAreaStyle style;
            style.fontFamily = "Courier New";
            style.showLineNumbers = true;
            style.backgroundColor = UltraCanvas::Color(248, 248, 248);
            style.lineNumberBackgroundColor = UltraCanvas::Color(240, 240, 240);
            return style;
        }
    };

// ===== MAIN TEXT AREA CLASS =====
    class UltraCanvasTextArea : public UltraCanvasElement {
    private:
        // Text content
        std::vector<std::string> lines;

        // Cursor and selection
        int cursorLine = 0;
        int cursorColumn = 0;
        bool hasSelection = false;
        int selectionStartLine = 0;
        int selectionStartColumn = 0;
        int selectionEndLine = 0;
        int selectionEndColumn = 0;

        // Scrolling
        int scrollOffsetX = 0;
        int scrollOffsetY = 0;
        int maxVisibleLines = 0;
        int maxVisibleColumns = 0;

        // Scrollbars
        bool hasVerticalScrollbar = false;
        bool hasHorizontalScrollbar = false;
        UltraCanvas::Rect2Di verticalScrollThumb;
        UltraCanvas::Rect2Di horizontalScrollThumb;
        bool isDraggingVerticalThumb = false;
        bool isDraggingHorizontalThumb = false;

        // Style and configuration
        TextAreaStyle style;
        bool readOnly = false;
        bool isCaretVisible = true;

        // Text measurement cache for performance
        struct TextMeasurement {
            std::string text;
            std::string fontFamily;
            float fontSize;
            float width;
            bool valid = false;
        };
        mutable TextMeasurement lastMeasurement;

    public:
        // Constructor
        UltraCanvasTextArea(const std::string& id, long uid, long x, long y, long h, long w, const TextAreaStyle& textStyle = TextAreaStyle::Default());

        // ===== CORE TEXT MEASUREMENT FUNCTIONS =====

        // Get accurate text width using the rendering system
        float GetTextWidth(const std::string& text) const;

        // Get character width at specific position (for proportional fonts)
        float GetCharacterWidth(const std::string& text, size_t position) const;

        // Convert pixel X position to column position in a line
        int GetColumnFromPixelX(const std::string& lineText, float pixelX) const;

        // Convert column position to pixel X position in a line
        float GetPixelXFromColumn(const std::string& lineText, int column) const;

        // ===== CURSOR POSITIONING FUNCTIONS =====

        UltraCanvas::Point2Di GetCursorScreenPosition() const;
        float GetLineNumberWidth() const;

        // ===== TEXT CONTENT MANAGEMENT =====

        void SetText(const std::string& text);
        std::string GetText() const;
        void InsertText(const std::string& text);

        // ===== CURSOR AND SELECTION MANAGEMENT =====

        void SetCursorPosition(int line, int column);

        void SetSelection(int startLine, int startColumn, int endLine, int endColumn);
        void ClearSelection();
        void DeleteSelection();

        // ===== SCROLLING AND VISIBILITY =====

        void EnsureCursorVisible();
        void UpdateScrollBars();

        // ===== RENDERING FUNCTIONS =====

        void Render() override;
        bool OnEvent(const UCEvent& event) override;

    private:
        void DrawText();
        void DrawSelection();
        void DrawCursor();
        void DrawLineNumbers();
        void DrawScrollBars();

        void InvalidateTextMeasurementCache() { lastMeasurement.valid = false; }

        bool HandleMouseDown(const UCEvent& event);
        void HandleMouseMove(const UCEvent& event);
        void HandleMouseUp(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);
        void HandlMouseWheel(const UCEvent& event);
        bool HandleTextInput(const std::string& text);

        // ===== CURSOR MOVEMENT HELPERS =====

        void MoveCursor(int deltaColumn, int deltaLine, bool extendSelection);
        void MoveCursorToLineStart(bool extendSelection);
        void MoveCursorToLineEnd(bool extendSelection);
        void MoveCursorByWord(int direction, bool extendSelection);
        void SetCursorFromPoint(const UltraCanvas::Point2Di& point);
        // ===== TEXT EDITING OPERATIONS =====

        void InsertNewLine();
        void InsertTab();

        void DeleteCharacterBeforeCursor();
        void DeleteCharacterAfterCursor();

    public:
        // ===== ACCESSORS AND MUTATORS =====
        void UnindentLine();
        void Undo();
        void Redo();

        void SelectAll();
        void CopySelection();
        void PasteFromClipboard();
        void CutSelection();

        void SetStyle(const TextAreaStyle& newStyle);
        const TextAreaStyle& GetStyle() const { return style; }

        void SetReadOnly(bool readOnlyMode);
        bool IsReadOnly() const { return readOnly;}

        void SetCaretVisible(bool visible);
        bool IsCaretVisible() const { return isCaretVisible; }

        int GetLineCount() const {
            return static_cast<int>(lines.size());
        }
        std::string GetLine(int lineIndex) const;
        void SetLine(int lineIndex, const std::string& lineText);

        // Get current cursor position
        std::pair<int, int> GetCursorPosition() const { return std::make_pair(cursorLine, cursorColumn); }
        // Get selection bounds
        std::tuple<int, int, int, int> GetSelection() const;
        bool HasSelection() const { return hasSelection; }
        std::string GetSelectedText() const;

        virtual bool AcceptsFocus() const override { return true; }
    };

// ===== FACTORY FUNCTIONS =====

    inline std::shared_ptr<UltraCanvasTextArea> CreateTextArea(const std::string& id, long uid, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasTextArea>(id, uid, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasTextArea> CreateTextArea(const std::string& id, long uid, const Rect2Di& bounds) {
        return std::make_shared<UltraCanvasTextArea>(id, uid, bounds.x, bounds.y, bounds.width, bounds.height);
    }

} // namespace UltraCanvas

