// UltraCanvasTextArea.h
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.1.0
// Last Modified: 2026-02-02
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUI.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasString.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
#include <chrono>

namespace UltraCanvas {

// Forward declarations
    class SyntaxTokenizer;
    enum class TokenType;

// Syntax highlighting mode
    struct TokenStyle {
        Color color = Color(0, 0, 0);
        bool bold = false;
        bool italic = false;
        bool underline = false;

        TokenStyle() = default;

        TokenStyle(const Color &c, bool b = false, bool i = false, bool u = false)
                : color(c), bold(b), italic(i), underline(u) {}
    };

// Text area style structure
    struct TextAreaStyle {
        // Font properties
        FontStyle fontStyle;
        float lineHeight;
        Color fontColor;

        // Background and borders
        Color backgroundColor;
        Color borderColor;
        int borderWidth;
        int padding;

        // Selection and cursor
        Color selectionColor;
        Color currentLineHighlightColor;
        Color cursorColor;

        // Line numbers
        bool showLineNumbers;
        int lineNumbersWidth;
        Color lineNumbersColor;
        Color lineNumbersBackgroundColor;

        // Current line highlighting
        Color currentLineColor;

        // Syntax highlighting mode
        bool highlightSyntax;

        Color scrollbarTrackColor;
        Color scrollbarColor;

        // Syntax highlighting colors
        struct TokenStyles {
            TokenStyle keywordStyle;
            TokenStyle typeStyle;
            TokenStyle functionStyle;
            TokenStyle numberStyle;
            TokenStyle stringStyle;
            TokenStyle characterStyle;
            TokenStyle commentStyle;
            TokenStyle operatorStyle;
            TokenStyle punctuationStyle;
            TokenStyle preprocessorStyle;
            TokenStyle constantStyle;
            TokenStyle identifierStyle;
            TokenStyle builtinStyle;
            TokenStyle assemblyStyle;
            TokenStyle registerStyle;
            TokenStyle defaultStyle;
        } tokenStyles;
    };

// Text area control with integrated syntax highlighting and full UTF-8 support
    class UltraCanvasTextArea : public UltraCanvasUIElement {
    public:
        // Constructor and destructor
        UltraCanvasTextArea(const std::string& name, int id, int x, int y, int width, int height);
        virtual ~UltraCanvasTextArea();

        bool AcceptsFocus() const override { return true; }
        // Render method
        virtual void Render(IRenderContext* ctx) override;

        // Event handling
        virtual bool OnEvent(const UCEvent& event) override;

        void Invalidate();

        // Text manipulation - now UTF-8 aware
        //void SetText(const std::string& text);
        void SetText(const UCString& text);
        std::string GetText() const;
        UCString GetTextUC() const;
        void InsertText(const std::string& text);
        void InsertText(const UCString& text);
        void InsertCodepoint(char32_t codepoint);
        void InsertNewLine();
        void InsertTab();
        void DeleteCharacterBackward();  // Delete one grapheme cluster backward
        void DeleteCharacterForward();   // Delete one grapheme cluster forward
        void DeleteSelection();
        void Clear() { SetText(""); }

        // Legacy single-byte insert (for ASCII only)
        void InsertCharacter(char ch);

        // Cursor movement - now grapheme-aware
        void MoveCursorLeft(bool selecting = false);
        void MoveCursorRight(bool selecting = false);
        void MoveCursorWordLeft(bool selecting = false);
        void MoveCursorWordRight(bool selecting = false);
        void MoveCursorUp(bool selecting = false);
        void MoveCursorDown(bool selecting = false);        
        void MoveCursorToLineStart(bool selecting = false);
        void MoveCursorToLineEnd(bool selecting = false);
        void MoveCursorToStart(bool selecting = false);
        void MoveCursorToEnd(bool selecting = false);
        void MoveCursorPageDown(bool selecting = false);
        void MoveCursorPageUp(bool selecting = false);
        void SetCursorPosition(int graphemePosition);
        int GetCursorPosition() const { return cursorGraphemePosition; }

        // Selection - grapheme-based positions
        void SelectAll();
        void SelectLine(int lineIndex);
        void SelectWord();
        void SetSelection(int startGrapheme, int endGrapheme);
        void ClearSelection();
        bool HasSelection() const;
        std::string GetSelectedText() const;
        UCString GetSelectedTextUC() const;

        // Clipboard operations
        void CopySelection();
        void CutSelection();
        void PasteClipboard();

        // Syntax highlighting
        void SetHighlightSyntax(bool);
        bool GetHighlightSyntax() const { return style.highlightSyntax; }
        void SetProgrammingLanguage(const std::string& language);
        bool SetProgrammingLanguageByExtension(const std::string& extension);
        const std::string GetCurrentProgrammingLanguage();
        
        void SetSyntaxTheme(const std::string& theme);
        void UpdateSyntaxHighlighting();

        // Theme application
        void ApplyDarkTheme();
        void ApplyCustomTheme(const TextAreaStyle& customStyle);

        // Quick style applications
        void ApplyCodeStyle(const std::string& language);
        void ApplyDarkCodeStyle(const std::string& language);
        void ApplyPlainTextStyle();

        // Line operations - line indices remain integer-based
        void GoToLine(int lineNumber);
        int GetCurrentLine() const;
        int GetCurrentColumn() const;  // Returns grapheme column
        int GetLineCount() const;
        std::string GetLine(int lineIndex) const;
        UCString GetLineUC(int lineIndex) const;
        void SetLine(int lineIndex, const std::string& text);
        void SetLine(int lineIndex, const UCString& text);

        // Search and replace
        void FindText(const std::string& searchText, bool caseSensitive = false);
        void FindNext();
        void FindPrevious();
        void ReplaceText(const std::string& findText, const std::string& replaceText, bool all = false);
        void HighlightMatches(const std::string& searchText);
        void ClearHighlights();

        // Undo/Redo (basic support)
        void Undo();
        void Redo();
        bool CanUndo() const;
        bool CanRedo() const;

        // Properties
        void SetReadOnly(bool readOnly) { isReadOnly = readOnly; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        bool IsReadOnly() const { return isReadOnly; }

        void SetWordWrap(bool wrap) { wordWrap = wrap; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        bool GetWordWrap() const { return wordWrap; }

        void SetHighlightCurrentLine(bool highlight) { highlightCurrentLine = highlight; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        bool GetHighlightCurrentLine() const { return highlightCurrentLine; }

        void SetShowLineNumbers(bool show) { style.showLineNumbers = show; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        bool GetShowLineNumbers() const { return style.showLineNumbers; }

        void SetTabSize(int size) { tabSize = size; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        int GetTabSize() const { return tabSize; }

        std::pair<int, int> GetLineColumnFromPosition(int graphemePosition) const;

        // Style access
        void SetStyle(const TextAreaStyle& newStyle) { style = newStyle; }
        TextAreaStyle& GetStyle() { return style; }
        const TextAreaStyle& GetStyle() const { return style; }

        // Font settings
        void SetFont(const std::string& family, float size) { style.fontStyle.fontFamily = family, style.fontStyle.fontSize = size; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        void SetFontFamily(const std::string& family) { style.fontStyle.fontFamily = family; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        const std::string& GetFontFamily() { return style.fontStyle.fontFamily; }
        void SetFontSize(float size) { style.fontStyle.fontSize = size; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        float GetFontSize() { return style.fontStyle.fontSize; }

        // Color settings
        void SetTextColor(const Color& color) { style.fontColor = color;  RequestRedraw(); }
        void SetBackgroundColor(const Color& color) { style.backgroundColor = color; RequestRedraw(); }
        void SetSelectionColor(const Color& color) { style.selectionColor = color; RequestRedraw(); }
        void SetCursorColor(const Color& color) { style.cursorColor = color; RequestRedraw(); }

        // Scrolling
        void ScrollTo(int line);
        void ScrollUp(int lines = 1);
        void ScrollDown(int lines = 1);
        void ScrollLeft(int chars = 1);
        void ScrollRight(int chars = 1);
        void EnsureCursorVisible();
        void SetFirstVisibleLine(int line);

        // Callbacks
        using TextChangedCallback = std::function<void(const std::string&)>;
        using CursorPositionChangedCallback = std::function<void(int line, int column)>;
        using SelectionChangedCallback = std::function<void(int start, int end)>;

        void SetOnTextChanged(TextChangedCallback callback) { onTextChanged = callback; }
        void SetOnCursorPositionChanged(CursorPositionChangedCallback callback) { onCursorPositionChanged = callback; }
        void SetOnSelectionChanged(SelectionChangedCallback callback) { onSelectionChanged = callback; }

        // Auto-completion support
        void ShowAutoComplete(const std::vector<std::string>& suggestions);
        void HideAutoComplete();
        void AcceptAutoComplete();

        // Bracket matching
        void HighlightMatchingBrackets();
        void JumpToMatchingBracket();

        // Indentation
        void IndentSelection();
        void UnindentSelection();
        void AutoIndentLine(int lineIndex);

        // Comments
        void ToggleLineComment();
        void ToggleBlockComment();

        // Bookmarks
        void ToggleBookmark(int lineIndex);
        void NextBookmark();
        void PreviousBookmark();
        void ClearAllBookmarks();

        // Error markers
        void AddErrorMarker(int lineIndex, const std::string& message);
        void AddWarningMarker(int lineIndex, const std::string& message);
        void ClearMarkers();

        // Callbacks
        TextChangedCallback onTextChanged;
        CursorPositionChangedCallback onCursorPositionChanged;
        SelectionChangedCallback onSelectionChanged;

    protected:
        // Drawing methods
        void DrawBackground(IRenderContext* context);
        void DrawBorder(IRenderContext* context);
        void DrawLineNumbers(IRenderContext* context);
        void DrawText(IRenderContext* context);
        void DrawPlainText(IRenderContext* context);
        void DrawHighlightedText(IRenderContext* context);
        void DrawSelection(IRenderContext* context);
        void DrawSearchHighlights(IRenderContext* context);
        void DrawCursor(IRenderContext* context);
        void DrawScrollbars(IRenderContext* context);
        void DrawAutoComplete(IRenderContext* context);
        void DrawMarkers(IRenderContext* context);

        // Event handlers
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseDoubleClick(const UCEvent& event);
        bool HandleMouseTripleClick(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent &event);
        bool HandleMouseDrag(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        int MeasureTextWidth(const std::string& txt) const;
        int MeasureTextWidth(const UCString& txt) const;
        
        // Helper methods - updated for UTF-8 support
        int GetPositionFromLineColumn(int line, int graphemeColumn) const;
        std::pair<int, int> GetLineColumnFromPoint(int x, int y) const;
        void CalculateVisibleArea();
        void RebuildText();
        int GetMaxLineLength() const;
        int GetVisibleCharactersPerLine() const;
        const TokenStyle& GetStyleForTokenType(TokenType type) const;

        // UTF-8 conversion helpers
        size_t GraphemeToByteOffset(int lineIndex, int graphemeColumn) const;
        int ByteToGraphemeColumn(int lineIndex, size_t byteOffset) const;
        int GetLineGraphemeCount(int lineIndex) const;

        // Mouse-to-text position helper
        int GetGraphemePositionFromPoint(int mouseX, int mouseY, int& outLine, int& outCol) const;

        // Initialization
        void ApplyDefaultStyle();
        bool IsNeedVerticalScrollbar();
        bool IsNeedHorizontalScrollbar();
        int GetMaxLineWidth();
        
        // State management
        void SaveState();

    private:
        // Text data - using UCString for proper UTF-8 handling
        UCString textContent;                  // Full text content as UCString
        std::vector<UCString> lines;           // Lines as UCString for grapheme-aware operations

        // Cursor and selection - grapheme-based positions
        int cursorGraphemePosition;            // Cursor position in graphemes from start
        int selectionStartGrapheme;            // Selection start in graphemes (-1 if no selection)
        int selectionEndGrapheme;              // Selection end in graphemes (-1 if no selection)
        int computedLineHeight = 12;

        // Scrolling
        int horizontalScrollOffset;
        int verticalScrollOffset;
        int firstVisibleLine;
        int maxVisibleLines;
        int maxLineWidth;
        Rect2Di visibleTextArea;
        Rect2Di horizontalScrollThumb;
        Rect2Di verticalScrollThumb;
        Point2Di dragStartOffset;
        bool isDraggingHorizontalThumb = false;
        bool isDraggingVerticalThumb = false;

        // Mouse text selection state
        bool isSelectingText = false;
        int selectionAnchorGrapheme = -1;

        // Click counting for double/triple click detection
        int clickCount = 0;
        std::chrono::steady_clock::time_point lastClickTime;
        int lastClickX = 0;
        int lastClickY = 0;
        static constexpr int MultiClickDistanceThreshold = 5;
        static constexpr int MultiClickTimeThresholdMs = 400;

        // Cursor animation
        double cursorBlinkTime;
        bool cursorVisible;

        // Properties
        bool isNeedRecalculateVisibleArea;
        bool isReadOnly;
        bool wordWrap;
        bool highlightCurrentLine;
        int currentLineIndex;
        int tabSize = 4;

        // Style
        TextAreaStyle style;

        // Syntax highlighter
        std::unique_ptr<SyntaxTokenizer> syntaxTokenizer;

        // Search state
        std::string lastSearchText;
        int lastSearchPosition;
        bool lastSearchCaseSensitive;
        
        // Search highlights (grapheme positions: start, end)
        std::vector<std::pair<int, int>> searchHighlights;

        // Undo/Redo stacks
        struct TextState {
            UCString text;
            int cursorGraphemePosition;
            int selectionStartGrapheme;
            int selectionEndGrapheme;
        };
        std::vector<TextState> undoStack;
        std::vector<TextState> redoStack;
        const size_t maxUndoStackSize = 100;

        // Bookmarks
        std::vector<int> bookmarks;

        // Error/Warning markers
        struct Marker {
            enum Type { Error, Warning, Info };
            Type type;
            int line;
            std::string message;
        };
        std::vector<Marker> markers;
        
        // Cache for total grapheme count
        mutable int cachedTotalGraphemes = -1;
        void InvalidateGraphemeCache() { cachedTotalGraphemes = -1; }
        int GetTotalGraphemeCount() const;
    };

// Factory functions for quick creation
    std::shared_ptr<UltraCanvasTextArea> CreateCodeEditor(
            const std::string& name, int id, int x, int y, int width, int height,
            const std::string& language = "C++");

    std::shared_ptr<UltraCanvasTextArea> CreateDarkCodeEditor(
            const std::string& name, int id, int x, int y, int width, int height,
            const std::string& language = "C++");

    std::shared_ptr<UltraCanvasTextArea> CreatePlainTextEditor(
            const std::string& name, int id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(
            const std::string& name, int id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasTextArea> CreateJSONEditor(
            const std::string& name, int id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasTextArea> CreateXMLEditor(
            const std::string& name, int id, int x, int y, int width, int height);

} // namespace UltraCanvas