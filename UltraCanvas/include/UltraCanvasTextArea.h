// UltraCanvasTextArea.h
// Advanced text area component with syntax highlighting
// Version: 2.0.0
// Last Modified: 2024-12-20
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUI.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>

namespace UltraCanvas {

// Forward declarations
    class SyntaxTokenizer;
    enum class TokenType;

// Syntax highlighting mode
    enum class SyntaxMode {
        PlainText,      // No syntax highlighting
        Programming,    // Uses LanguageRules from UltraCanvasSyntaxHighlightingRules.h
        Markdown,       // Markdown formatting
        JSON,           // JSON syntax
        XML,            // XML/HTML syntax
        Custom          // User-defined rules
    };

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
        std::string fontFamily;
        int fontSize;
        int lineHeight;
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
        SyntaxMode syntaxMode;

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
            TokenStyle unknownStyle;
        } tokenStyles;
    };

// Text area control with integrated syntax highlighting
    class UltraCanvasTextArea : public UltraCanvasUIElement {
    public:
        // Constructor and destructor
        UltraCanvasTextArea(const std::string& name, int id, int x, int y, int width, int height);
        virtual ~UltraCanvasTextArea();

        bool AcceptsFocus() const override { return true; }
        // Render method
        virtual void Render() override;

        // Event handling
        virtual bool OnEvent(const UCEvent& event) override;

        // Text manipulation
        void SetText(const std::string& text);
        std::string GetText() const;
        void InsertText(const std::string& text);
        void InsertCharacter(char ch);
        void InsertNewLine();
        void InsertTab();
        void DeleteCharacterBackward();
        void DeleteCharacterForward();
        void DeleteSelection();
        void Clear() { SetText(""); }

        // Cursor movement
        void MoveCursorLeft(bool selecting = false);
        void MoveCursorRight(bool selecting = false);
        void MoveCursorUp(bool selecting = false);
        void MoveCursorDown(bool selecting = false);
        void MoveCursorToLineStart(bool selecting = false);
        void MoveCursorToLineEnd(bool selecting = false);
        void MoveCursorToStart(bool selecting = false);
        void MoveCursorToEnd(bool selecting = false);
        void SetCursorPosition(int position);
        int GetCursorPosition() const { return cursorPosition; }

        // Selection
        void SelectAll();
        void SelectLine(int lineIndex);
        void SelectWord();
        void SetSelection(int start, int end);
        void ClearSelection();
        bool HasSelection() const;
        std::string GetSelectedText() const;

        // Clipboard operations
        void CopySelection();
        void CutSelection();
        void PasteClipboard();

        // Syntax highlighting
        void SetSyntaxMode(SyntaxMode mode);
        SyntaxMode GetSyntaxMode() const { return style.syntaxMode; }
        void SetProgrammingLanguage(const std::string& language);
        void SetProgrammingLanguageByExtension(const std::string& extension);
        void SetSyntaxTheme(const std::string& theme);
        void UpdateSyntaxHighlighting();

        // Theme application
        void ApplyDarkTheme();
//        void ApplyLightTheme();
//        void ApplySolarizedTheme();
//        void ApplyMonokaiTheme();
        void ApplyCustomTheme(const TextAreaStyle& customStyle);

        // Quick style applications
        void ApplyCodeStyle(const std::string& language);
        void ApplyDarkCodeStyle(const std::string& language);
        void ApplyPlainTextStyle();
//        void ApplyMarkdownStyle();
//        void ApplyJSONStyle();
//        void ApplyXMLStyle();

        // Line operations
        void GoToLine(int lineNumber);
        int GetCurrentLine() const;
        int GetCurrentColumn() const;
        int GetLineCount() const;
        std::string GetLine(int lineIndex) const;
        void SetLine(int lineIndex, const std::string& text);

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
        void SetReadOnly(bool readOnly) { isReadOnly = readOnly; }
        bool IsReadOnly() const { return isReadOnly; }

        void SetWordWrap(bool wrap) { wordWrap = wrap; }
        bool GetWordWrap() const { return wordWrap; }

        void SetHighlightCurrentLine(bool highlight) { highlightCurrentLine = highlight; }
        bool GetHighlightCurrentLine() const { return highlightCurrentLine; }

        void SetShowLineNumbers(bool show) { style.showLineNumbers = show; }
        bool GetShowLineNumbers() const { return style.showLineNumbers; }

        void SetTabSize(int size) { tabSize = size; }
        int GetTabSize() const { return tabSize; }

        // Style access
        void SetStyle(const TextAreaStyle& newStyle) { style = newStyle; }
        TextAreaStyle& GetStyle() { return style; }
        const TextAreaStyle& GetStyle() const { return style; }

        // Font settings
        void SetFont(const std::string& family, int size);
        void SetFontFamily(const std::string& family);
        void SetFontSize(int size);

        // Color settings
        void SetTextColor(const Color& color) { style.fontColor = color; }
        void SetBackgroundColor(const Color& color) { style.backgroundColor = color; }
        void SetSelectionColor(const Color& color) { style.selectionColor = color; }
        void SetCursorColor(const Color& color) { style.cursorColor = color; }

        // Scrolling
        void ScrollTo(int line);
        void ScrollUp(int lines = 1);
        void ScrollDown(int lines = 1);
        void ScrollLeft(int chars = 1);
        void ScrollRight(int chars = 1);
        void EnsureCursorVisible();

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

//        // Code folding support
//        void FoldLine(int lineIndex);
//        void UnfoldLine(int lineIndex);
//        void FoldAll();
//        void UnfoldAll();
//        bool IsLineFolded(int lineIndex) const;

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

    protected:
        // Drawing methods
        void DrawBackground(IRenderContext* context);
        void DrawBorder(IRenderContext* context);
        void DrawLineNumbers(IRenderContext* context);
        void DrawText(IRenderContext* context);
        void DrawPlainText(IRenderContext* context);
        void DrawHighlightedText(IRenderContext* context);
        void DrawSelection(IRenderContext* context);
        void DrawCursor(IRenderContext* context);
        void DrawScrollbars(IRenderContext* context);
        void DrawAutoComplete(IRenderContext* context);
        void DrawMarkers(IRenderContext* context);

        // Event handlers
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseDoubleClick(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent &event);
        bool HandleMouseDrag(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyPress(const UCEvent& event);

        int MeasureTextWidth(const std::string& txt) const;
        // Helper methods
        std::pair<int, int> GetLineColumnFromPosition(int position) const;
        int GetPositionFromLineColumn(int line, int column) const;
        std::pair<int, int> GetLineColumnFromPoint(int x, int y) const;
        void UpdateVisibleArea();
        void RebuildText();
        int GetMaxLineLength() const;
        int GetVisibleCharactersPerLine() const;
        const TokenStyle& GetStyleForTokenType(TokenType type) const;

        // Clipboard helpers
        void SetClipboardText(const std::string& text);
        std::string GetClipboardText() const;

        // Initialization
        void ApplyDefaultStyle();
        bool IsNeedVerticalScrollbar();
        bool IsNeedHorizontalScrollbar();
        int GetMaxLineWidth();

    private:
        // Text data
        std::string text;
        std::vector<std::string> lines;

        // Cursor and selection
        int cursorPosition;
        int selectionStart;
        int selectionEnd;

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

        // Cursor animation
        double cursorBlinkTime;
        bool cursorVisible;

        // Properties
        bool isReadOnly;
        bool wordWrap;
        bool highlightCurrentLine;
        int currentLineIndex;
        int tabSize = 4;

        // Style
        TextAreaStyle style;

        // Syntax highlighter
        std::unique_ptr<SyntaxTokenizer> syntaxHighlighter;

        // Search state
        std::string lastSearchText;
        int lastSearchPosition;
        bool lastSearchCaseSensitive;

        // Undo/Redo stacks
        struct TextState {
            std::string text;
            int cursorPosition;
            int selectionStart;
            int selectionEnd;
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

        // Callbacks
        TextChangedCallback onTextChanged;
        CursorPositionChangedCallback onCursorPositionChanged;
        SelectionChangedCallback onSelectionChanged;
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