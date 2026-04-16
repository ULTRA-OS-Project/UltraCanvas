// UltraCanvasTextArea.h
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.2.4
// Last Modified: 2026-04-16
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUI.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace UltraCanvas {

    // Forward declarations
    class SyntaxTokenizer;
    enum class TokenType;

    // ===== HIT RECT FOR CLICKABLE ELEMENTS =====
    // Tracks clickable regions for links and images
    struct MarkdownHitRect {
        Rect2Di bounds;
        std::string url;
        std::string altText;
        bool isImage = false;
        bool isAbbreviation = false;
        bool isFootnote = false;
        bool isAnchorReturn = false;   // ↩ return icon on {#id} headings — scrolls back to jump source
    };

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


// ===== MARKDOWN HYBRID STYLE CONFIGURATION =====
// Configurable styling for markdown rendering within TextArea.
// Mirrors relevant properties from MarkdownStyle in UltraCanvasMarkdown.h.
    struct MarkdownHybridStyle {
        // Header colors per level (H1-H6)
        std::array<Color, 6> headerColors = {
                Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
                Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
        };

        // Header font size multipliers (relative to base font size)
        std::array<float, 6> headerSizeMultipliers = {1.8f, 1.5f, 1.3f, 1.2f, 1.1f, 1.0f};

        // Code styling
        Color codeTextColor = Color(200, 50, 50);
        Color codeBackgroundColor = Color(245, 245, 245);
        Color codeBlockBackgroundColor = Color(248, 248, 248);
        Color codeBlockBorderColor = Color(120, 120, 120);
        Color codeBlockTextColor = Color(50, 50, 50);       // Default text color inside code blocks
        Color codeBlockCommentColor = Color(106, 153, 85);   // Green for comments in code blocks
        Color codeBlockKeywordColor = Color(0, 0, 200);      // Blue for keywords in code blocks
        Color codeBlockStringColor = Color(163, 21, 21);     // Red for strings in code blocks
        Color codeBlockNumberColor = Color(9, 134, 88);      // Green for numbers in code blocks
        // Small label rendered inside a fenced code block's top edge (e.g. "cpp", "python")
        Color codeBlockLanguageLabelColor = Color(100, 100, 100);
        std::string codeFont = "Courier New";

        // Link styling
        Color linkColor = Color(0, 102, 204);
        Color linkHoverColor = Color(0, 80, 160);
        bool linkUnderline = true;

        // List styling
        int listIndent = 20;
        std::string bulletCharacter = "\xe2\x80\xa2"; // UTF-8 bullet • (kept for compatibility)
        // Nested bullet characters per nesting level (0=•, 1=◦, 2=▪, deeper levels repeat ▪)
        std::array<std::string, 3> nestedBulletCharacters = {
            "\xe2\x80\xa2",         // level 0: • (U+2022 BULLET)
            "\xe2\x97\xa6",         // level 1: ◦ (U+25E6 WHITE BULLET)
            "\xe2\x96\xaa"          // level 2: ▪ (U+25AA BLACK SMALL SQUARE)
        };
        Color bulletColor = Color(80, 80, 80);

        // Quote styling
        Color quoteBarColor = Color(200, 200, 200);
        Color quoteBackgroundColor = Color(250, 250, 250);
        Color quoteTextColor = Color(100, 100, 100);
        int quoteBarWidth = 3;          // Thin bar — matches reference visual
        int quoteBarGap = 10;           // Gap between bar and text (new)
        int quoteIndent = 26;           // Total indent from bar left edge to text
        int quoteNestingStep = 20;      // Horizontal step per nesting level

        // Horizontal rule
        Color horizontalRuleColor = Color(200, 200, 200);
        float horizontalRuleHeight = 2.0f;
        // Vertical inset for block backgrounds (code blocks, blockquotes)
        // Top border starts this many pixels lower on the first line of a block,
        // bottom border ends this many pixels earlier on the last line.
        int blockVerticalInset = 5;

        // Table styling
        Color tableBorderColor = Color(200, 200, 200);
        Color tableHeaderBackground = Color(240, 240, 240);
        float tableCellPadding = 4.0f;

        // Strikethrough
        Color strikethroughColor = Color(120, 120, 120);

        // Highlight
        Color highlightBackground = Color(255, 255, 0, 100);

        // Abbreviation styling
        Color abbreviationBackground = Color(230, 230, 230, 120);
        Color abbreviationUnderlineColor = Color(150, 150, 150);
        float abbreviationUnderlineDashLength = 2.0f;
        float abbreviationUnderlineGapLength = 2.0f;

        // Footnote reference styling
        Color footnoteRefColor = Color(0, 102, 204);

        // Task list checkbox
        Color checkboxBorderColor = Color(150, 150, 150);
        Color checkboxCheckedColor = Color(0, 120, 215);
        Color checkboxCheckmarkColor = Color(255, 255, 255);
        int checkboxSize = 12;

        // Image placeholder
        Color imagePlaceholderBackground = Color(230, 230, 240);
        Color imagePlaceholderBorderColor = Color(180, 180, 200);
        Color imagePlaceholderTextColor = Color(100, 100, 140);

        // Math formula styling
        Color mathTextColor = Color(0, 120, 60);
        Color mathBackgroundColor = Color(240, 248, 240, 180);

        static MarkdownHybridStyle Default() {
            return MarkdownHybridStyle();
        }

        static MarkdownHybridStyle DarkMode() {
            MarkdownHybridStyle s;
            s.headerColors = {
                    Color(240, 240, 240), Color(220, 220, 240), Color(200, 200, 220),
                    Color(190, 190, 210), Color(180, 180, 200), Color(170, 170, 190)
            };
            s.codeTextColor = Color(255, 150, 150);
            s.codeBackgroundColor = Color(50, 50, 50);
            s.codeBlockBackgroundColor = Color(40, 40, 40);
            s.codeBlockBorderColor = Color(80, 80, 80);
            s.codeBlockTextColor = Color(210, 210, 210);
            s.codeBlockCommentColor = Color(106, 153, 85);
            s.codeBlockKeywordColor = Color(86, 156, 214);
            s.codeBlockStringColor = Color(206, 145, 120);
            s.codeBlockNumberColor = Color(181, 206, 168);
            s.codeBlockLanguageLabelColor = Color(160, 160, 160);
            s.linkColor = Color(100, 180, 255);
            s.linkHoverColor = Color(150, 200, 255);
            s.quoteBarColor = Color(100, 100, 100);
            s.quoteBackgroundColor = Color(40, 40, 40);
            s.quoteTextColor = Color(170, 170, 170);
            s.horizontalRuleColor = Color(100, 100, 100);
            s.tableBorderColor = Color(80, 80, 80);
            s.tableHeaderBackground = Color(50, 50, 50);
            s.strikethroughColor = Color(170, 170, 170);
            s.highlightBackground = Color(100, 100, 0, 100);
            s.bulletColor = Color(170, 170, 170);
            s.checkboxBorderColor = Color(120, 120, 120);
            s.imagePlaceholderBackground = Color(50, 50, 60);
            s.imagePlaceholderBorderColor = Color(80, 80, 100);
            s.imagePlaceholderTextColor = Color(140, 140, 170);
            s.mathTextColor = Color(100, 220, 140);
            s.mathBackgroundColor = Color(30, 50, 35, 150);
            s.abbreviationBackground = Color(70, 70, 80, 120);
            s.abbreviationUnderlineColor = Color(140, 140, 160);
            s.footnoteRefColor = Color(100, 180, 255);
            s.nestedBulletCharacters = {
                "\xe2\x80\xa2",     // level 0: •
                "\xe2\x97\xa6",     // level 1: ◦
                "\xe2\x96\xaa"      // level 2: ▪
            };
            return s;
        }
    };


// Text area style structure
    struct TextAreaStyle {
        // Font properties
        FontStyle fontStyle;
        FontStyle fixedFontStyle;
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

// Line ending type
    enum class LineEndingType {
        LF,     // Unix/macOS (\n)
        CRLF,   // Windows (\r\n)
        CR      // Classic Mac OS (\r)
    };

// Editing mode for text area (mutually exclusive)
    enum class TextAreaEditingMode {
        PlainText,      // Default text editing (with optional syntax highlighting)
        MarkdownHybrid, // Hybrid markdown rendering
        Hex             // Hex editor mode
    };

    // line layouts
    struct LineColumnIndex {
        int lineIndex = -1; // line index in lineLayout/lines vectors
        int columnIndex = -1; // codepoint index in line, should point to real line position not layout text position.
        bool IsValid() const {
            return (lineIndex >=0 && columnIndex >= 0);
        }
        static LineColumnIndex const INVALID;
    };
    constexpr LineColumnIndex const LineColumnIndex::INVALID = {-1, -1};

    enum LineLayoutType {
        PlainLine,
        MarkDownLine,
        HorizontalLine,
        Blockquote,
        CodeblockStart,
        CodeblockEnd,
        CodeblockContent,
        UnorderedListItem,
        OrderedListItem,
        DefinitionTerm,
        DefinitionContinuation,
        TableHederRow,
        TableSeparatorRow,
        TableRow
    };

    struct LineLayoutBase {
        LineLayoutType layoutType = LineLayoutType::PlainLine;
        Rect2Di bounds = {0,0,0,0};
        std::vector<MarkdownHitRect> hitRects; // bounds inside layout
        std::unique_ptr<ITextLayout> layout;
        Point2Di layoutShift = {0, 0}; // for MD mode some elements render text layout shifted to right or bottom
        int layoutTextStartIndex = 0; // for MD mode some part of real line text is not displayed (it is part of markup)
        virtual ~LineLayoutBase() {};
    };

    struct OrderedListItemLayout : LineLayoutBase {
        int orderedItemNumber = 0;
        int listDepth = 0;
    };
    struct UnorderedListItemLayout : LineLayoutBase {
        int listDepth = 0;
    };

    struct BlockquoteLayout : LineLayoutBase {
        int quoteLevel = 0;
    };

    struct CodeLayout : LineLayoutBase {
        std::string codeblockLanguage;
        bool isIndentedCode = false;    // true: indented (4-space / tab) block, not fenced
    };

    struct TableLineLayout : LineLayoutBase {
        int tableColumnCount = 0;
        bool lastTableRow = false;
        // Per-column alignment parsed from the separator row's `|:---|:--:|---:|` markers.
        // Populated on the separator row itself; NormalizeTableGroupWidths copies it to all
        // other rows in the group so every row can render with the correct alignment.
        std::vector<TextAlignment> columnAlignments;
        std::vector<std::unique_ptr<LineLayoutBase>> cellsLayouts;
    };

    // Inline styling run emitted by the markdown/plain-text parser and consumed by MakeLineLayout
    // to drive ITextLayout attribute insertion. All offsets are codepoint indices in the VISIBLE
    // layout text (i.e. after layoutTextStartIndex has stripped markup prefix characters).
    struct TextStyleSpan {
        int startCp = 0;
        int endCp = 0;
        TokenStyle style;
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

        // Drives the per-line layout cache (UpdateLineLayouts) so layouts are ready
        // before Render runs. Called by the framework when RequestUpdateGeometry() has been set.
        virtual void UpdateGeometry(IRenderContext* ctx) override;

        // Override SetBounds to trigger layout recalculation on resize
        void SetBounds(const Rect2Di& b) override {
            if (b.width != GetWidth() || b.height != GetHeight()) {
                isNeedRecalculateVisibleArea = true;
            }
            UltraCanvasUIElement::SetBounds(b);
        }

        // Event handling
        virtual bool OnEvent(const UCEvent& event) override;

        void Invalidate();

        // Text manipulation - now UTF-8 aware
        void SetText(const std::string& text, bool runNotifications = true);
        std::string GetText() const;
        void InsertText(const std::string& text);
        void InsertCodepoint(char32_t codepoint);
        void InsertNewLine();
        void InsertTab();
        void DeleteCharacterBackward();  // Delete one grapheme cluster backward
        void DeleteCharacterForward();   // Delete one grapheme cluster forward
        void DeleteSelection();
        void Clear() { SetText(""); }

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
        int GetCursorPosition() const {
            return (cursorPosition.lineIndex < 0) ? 0
                : GetPositionFromLineColumn(cursorPosition.lineIndex, cursorPosition.columnIndex);
        }

        // Selection - grapheme-based positions
        void SelectAll();
        void SelectLine(int lineIndex);
        void SelectWord();
        void SetSelection(int startGrapheme, int endGrapheme);
        void ClearSelection();
        bool HasSelection() const;
        std::string GetSelectedText() const;
        int GetSelectionMinGrapheme() const;

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
        std::vector<std::string> GetSupportedLanguages();

        void SetSyntaxTheme(const std::string& theme);

        // Theme application
        void ApplyDarkTheme();
        void ApplyLightTheme();
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
        void SetLine(int lineIndex, const std::string& text);

        // Search and replace
        void SetTextToFind(const std::string& searchText, bool caseSensitive = false);
        void FindFirst();
        void FindNext();
        void FindPrevious();
        void ReplaceText(const std::string& findText, const std::string& replaceText, bool all = false);
        void HighlightMatches(const std::string& searchText);
        void ClearHighlights();

        /// Count total occurrences of search text in document
        /// @return Total number of matches
        int CountMatches(const std::string& searchText, bool caseSensitive = false) const;

        /// Get the index (1-based) of the current match among all matches
        /// @return Current match index (1-based), or 0 if no current match
        int GetCurrentMatchIndex(const std::string& searchText, bool caseSensitive = false) const;

        // Undo/Redo (basic support)
        void Undo();
        void Redo();
        bool CanUndo() const;
        bool CanRedo() const;

        // Properties
        void SetReadOnly(bool readOnly) { isReadOnly = readOnly; isNeedRecalculateVisibleArea = true; RequestRedraw(); }
        bool IsReadOnly() const { return isReadOnly; }

        void SetWordWrap(bool wrap);
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

        // Markdown hybrid style access. Setter invalidates layouts because indent/multiplier
        // fields are baked into MakeMarkdownLineLayout when each line is built.
        const MarkdownHybridStyle& GetMarkdownStyle() const { return markdownStyle; }
        MarkdownHybridStyle& GetMarkdownStyleMutable() { return markdownStyle; }
        void SetMarkdownStyle(const MarkdownHybridStyle& s) {
            markdownStyle = s;
            InvalidateAllLineLayouts();
            RequestRedraw();
        }

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

        // Line ending
        void SetLineEnding(LineEndingType type);
        LineEndingType GetLineEnding() const { return lineEndingType; }
        static LineEndingType DetectLineEnding(const std::string& text);
        static std::string LineEndingToString(LineEndingType type);
        static std::string LineEndingSequence(LineEndingType type);
        static LineEndingType GetSystemDefaultLineEnding();

        using LineEndingChangedCallback = std::function<void(LineEndingType)>;
        LineEndingChangedCallback onLineEndingChanged;

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
        void DrawCursor(IRenderContext* context);
        void DrawScrollbars(IRenderContext* context);

        // Event handlers
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseDoubleClick(const UCEvent& event);
        bool HandleMouseTripleClick(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent &event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        int MeasureTextWidth(const std::string& txt) const;
        
        // Helper methods - updated for UTF-8 support
        int GetPositionFromLineColumn(int line, int graphemeColumn) const;
        void CalculateVisibleArea();
        int CalculateLineNumbersWidth(IRenderContext* ctx);
        void RebuildText();
        const TokenStyle& GetStyleForTokenType(TokenType type) const;

        // Initialization
        void ApplyDefaultStyle();
        bool IsNeedVerticalScrollbar();
        bool IsNeedHorizontalScrollbar();

        // State management
        void SaveState();


        // parse the line text and create/update line layout
        void UpdateLineLayouts(IRenderContext* ctx);

        // parse the line text and create line layout
        std::unique_ptr<LineLayoutBase> MakeLineLayout(IRenderContext* ctx, int lineIndex);
        // parse the plain line text and setup line layout
        std::unique_ptr<LineLayoutBase> MakePlainLineLayout(IRenderContext* ctx, int lineIndex);

        // MakeLineLayout dispatches here for MarkdownHybrid lines. Inspects lineLayouts[lineIndex-1]
        // for state like open fenced-code-block and returns the appropriate derived LineLayoutBase.
        std::unique_ptr<LineLayoutBase> MakeMarkdownLineLayout(IRenderContext* ctx, int lineIndex);

        // Doc-wide pre-scan: populates markdownAbbreviations, markdownFootnotes, markdownAnchors,
        // and markdownAnchorBacklinks. Run lazily when markdownIndexDirty is true — which
        // RebuildText() and SetText() both raise.
        void RebuildMarkdownIndex();
        bool markdownIndexDirty = true;

        // Normalize per-column widths across every row of a contiguous table span. Each cell's
        // natural width is measured by temporarily clearing its wrap, then per-column maxes are
        // collected; if the total fits the available width the leftover is distributed evenly,
        // otherwise columns shrink proportionally. Row heights are re-measured after rewrap.
        void NormalizeTableGroupWidths(int startLine, int endLine);

        // Inline markdown run (produced by ParseInlineMarkdownRuns) — byte offsets are within the
        // *visible* (markup-stripped) text, not the raw line.
        struct InlineRun {
            enum Kind { Bold, Italic, Code, Strike, Subscript, Superscript, Math, Link, Image, Footnote };
            int startByte = 0;
            int endByte   = 0;
            Kind kind = Bold;
            std::string url;    // link target (footnote: label after `^`)
            std::string alt;    // image alt text
        };
        // Walk rawLine emitting visible text (markup stripped) and a list of styling runs in
        // visible-text byte coords. Handles **bold**, *italic* / _italic_, `code`, ~~strike~~,
        // ~subscript~, ^superscript^, `$math$` (LaTeX command → Unicode substitution),
        // [text](url), ![alt](url), [^footnote], `:shortcode:` emoji, ASCII emoticons
        // (`:-)`, `;)`, `8-)`, `<3`, …), and backslash escapes. Sub/superscript content must
        // be non-empty and contain no whitespace. No nested inline.
        static void ParseInlineMarkdownRuns(const std::string& rawLine,
                                            std::string& visibleText,
                                            std::vector<InlineRun>& runs);

        // Apply inline-run attributes (bold/italic/code/strike/link/image/footnote) to `layout`
        // and record link/image/footnote hit rects on `outHitRects`. Also scans `visibleText`
        // for known abbreviations (from markdownAbbreviations) and adds underline + hit rects
        // for each match. Byte offsets in `runs` and `layout` must both refer to `visibleText`.
        void ApplyInlineRunsAndAbbreviations(ITextLayout* layout,
                                             const std::string& visibleText,
                                             const std::vector<InlineRun>& runs,
                                             std::vector<MarkdownHitRect>& outHitRects);

        // handle cursor position
        Rect2Di LineColumnToCursorPos(const LineColumnIndex& idx);
        LineColumnIndex PosToLineColumn(const Point2Di& pos);
        LineLayoutBase *GetLineLayoutForPos(const Point2Di& pos);
        LineLayoutBase *GetActualLineLayout(int idx);

        void RenderLineLayout(IRenderContext *ctx, LineLayoutBase* line);

    private:
        // Text data - std::string with GLib g_utf8_* for UTF-8 handling
        std::string textContent;
        std::vector<std::string> lines;

        std::vector<std::unique_ptr<LineLayoutBase>> lineLayouts; // cached layouts for each line
        int lineLayoutsWrapWidth = -1; // width the cached layouts were built against; -1 = no cache
        LineColumnIndex cursorPosition = {0,0}; // cursor position for lineIndex and codepoint index in Line
        LineColumnIndex selectionStart;
        LineColumnIndex selectionEnd;
        // Last selection state observed by ReconcileLayoutState — used to invalidate
        // lines whose selection membership changed so MakeLineLayout reapplies background attrs.
        LineColumnIndex lastAppliedSelectionStart = {-1, -1};
        LineColumnIndex lastAppliedSelectionEnd = {-1, -1};

        // MarkdownHybrid current line. When the cursor enters a line, this line gets text
        // and used in rendering and measuring
        std::unique_ptr<LineLayoutBase> currentLine;

        // Cursor / selection state is LineColumnIndex (codepoint-based); see fields above.
        int computedLineHeight = 12;
        int computedLineNumbersWidth = 40;

        // Scrolling
        int horizontalScrollOffset;
        int verticalScrollOffset;
        // firstVisibleLine / maxVisibleLines removed in Step 8b — pixel-based verticalScrollOffset
        // is now the authoritative scroll state.
        int maxLineWidth;
        Rect2Di visibleTextArea;
        Rect2Di horizontalScrollThumb;
        Rect2Di verticalScrollThumb;
        Point2Di dragStartOffset;
        bool isDraggingHorizontalThumb = false;
        bool isDraggingVerticalThumb = false;

        // Mouse text selection state
        bool isSelectingText = false;
        LineColumnIndex selectionAnchor = {-1, -1};

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
        // needFirstVisibleLineFixup removed in Step 8b (pixel scroll has no analogous fixup).
        int tabSize = 4;
 
        // Style
        TextAreaStyle style;
        MarkdownHybridStyle markdownStyle;

        // Syntax highlighter
        std::unique_ptr<SyntaxTokenizer> syntaxTokenizer;

        // Cached syntax tokenizer for markdown code block highlighting
        std::unique_ptr<SyntaxTokenizer> codeBlockTokenizer;
        std::string codeBlockTokenizerLang;

        void InvalidateAllLineLayouts();
        void InvalidateLineLayout(int logicalLine);
        void InvalidateLineLayoutsFrom(int fromLine);
        void InsertLineLayoutEntry(int logicalLine);  // insert nullptr at index (new line)
        void RemoveLineLayoutEntry(int logicalLine);  // erase entry (line deleted/merged)

        // Selection rendering via Pango background-color attributes on each affected layout.
        // ApplyLineSelectionBackground is called from MakeLineLayout after layout construction;
        // it inserts a background-color attribute on the byte range of this line that falls
        // inside the current selection. When selection changes, ReconcileLayoutState
        // invalidates the union of old+new selected line ranges so MakeLineLayout runs again.
        void ApplyLineSelectionBackground(LineLayoutBase* ll, int lineIndex);
        // Kept as no-op stubs — attribute application happens during rebuild, not in-place patching.
        void ApplySelectionAttributes();
        void ClearSelectionAttributes(int logicalLine);

        // Called at the top of UpdateLineLayouts each frame. Detects cursor-line
        // changes (to swap in/out MarkdownHybrid raw-editing mode) and selection-range changes
        // (to rebuild affected line layouts with fresh background-color attributes).
        void ReconcileLayoutState();

        // If editedLine holds a stash, move it back into lineLayouts[editedLineIndex] when
        // the raw text hasn't changed, or discard it otherwise (forcing a rebuild). Called on
        // cursor-line change and on focus loss.
        void RestoreEditedLine();

        // Search state
        std::string lastSearchText;
        int lastSearchPosition;
        bool lastSearchCaseSensitive;
        
        // Search highlights (grapheme positions: start, end)
        std::vector<std::pair<int, int>> searchHighlights;

        // Undo/Redo stacks. Positions are stored as LineColumnIndex (authoritative since Step 8b).
        struct TextState {
            std::string text;
            LineColumnIndex cursorPosition{0, 0};
            LineColumnIndex selectionStart{-1, -1};
            LineColumnIndex selectionEnd{-1, -1};
        };
        std::vector<TextState> undoStack;
        std::vector<TextState> redoStack;
        const size_t maxUndoStackSize = 100;

        // Line ending type
        LineEndingType lineEndingType = GetSystemDefaultLineEnding();

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
        
    public:
        // Markdown interaction callbacks
        using MarkdownLinkClickCallback = std::function<void(const std::string& url)>;
        using MarkdownImageClickCallback = std::function<void(const std::string& imagePath, const std::string& altText)>;
        MarkdownLinkClickCallback onMarkdownLinkClick;
        MarkdownImageClickCallback onMarkdownImageClick;
        /**
         * @brief Check if hybrid markdown mode is enabled
         * @return true if hybrid markdown mode is active
         */
        bool IsMarkdownHybridMode() const { return editingMode == TextAreaEditingMode::MarkdownHybrid; }
        /**
         * @brief Handle click on markdown link or image
         * @param mouseX Mouse X coordinate
         * @param mouseY Mouse Y coordinate
         * @return true if a clickable markdown element was hit
         */
        bool HandleMarkdownClick(int mouseX, int mouseY);

        /**
         * @brief Handle hover over markdown elements (updates cursor)
         * @param mouseX Mouse X coordinate
         * @param mouseY Mouse Y coordinate
         * @return true if hovering over a clickable element
         */
        bool HandleMarkdownHover(int mouseX, int mouseY);

        void SetDocumentFilePath(const std::string& path) { documentFilePath = path; }
        std::string GetDocumentFilePath() const { return documentFilePath; }

        // ===== EDITING MODE PUBLIC API =====
        void SetEditingMode(TextAreaEditingMode mode);
        bool IsHexMode() const { return editingMode == TextAreaEditingMode::Hex; }
        TextAreaEditingMode GetEditingMode() const { return editingMode; }
        void SetRawBytes(const std::vector<uint8_t>& bytes);
        std::vector<uint8_t> GetRawBytes() const;
        int GetHexCursorByteOffset() const { return hexCursorByteOffset; }

    protected:
        /**
         * @brief Draw text with hybrid markdown rendering
         * 
         * Renders current line as syntax-highlighted raw text,
         * and all other lines as formatted markdown.
         */
        // DrawMarkdownHybridText removed in Step 8 — MD block rendering runs through
        // MakeMarkdownLineLayout + RenderLineLayout.
        
        // Iterates each cached LineLayoutBase's hitRects (layout-local) and translates them to
        // screen coords to perform hit-testing at the given screen position. Returns a pointer
        // to the matching rect or nullptr. The pointer is only valid until the layout cache is
        // invalidated, so callers should use it immediately and not store it.
        const MarkdownHitRect* FindHitRectAtScreenPos(int mouseX, int mouseY) const;

        /**
         * @brief Check if a line is a markdown list item
         * @param line Line to check
         * @return true if line starts with list marker
         */
        bool IsMarkdownListItem(const std::string& line) const;
        
        /**
         * @brief Trim whitespace from string
         * @param str String to trim
         * @return Trimmed string
         */
        std::string TrimWhitespace(const std::string& str) const;

        // ===== HEX MODE PROTECTED METHODS =====
        // Hex rendering
        void DrawHexView(IRenderContext* ctx);
        void DrawHexAddresses(IRenderContext* ctx);
        void DrawHexBytes(IRenderContext* ctx);
        void DrawHexAscii(IRenderContext* ctx);
        void DrawHexSelection(IRenderContext* ctx);
        void DrawHexCrossHighlight(IRenderContext* ctx);
        void DrawHexSearchHighlights(IRenderContext* ctx);
        void DrawHexCursor(IRenderContext* ctx);
        void DrawHexCurrentRowHighlight(IRenderContext* ctx);
        void CalculateHexLayout();

        // Hex events
        bool HandleHexKeyDown(const UCEvent& event);
        bool HandleHexMouseDown(const UCEvent& event);
        bool HandleHexMouseMove(const UCEvent& event);

        // Hex editing
        void HexOverwriteNibble(int nibbleValue);
        void HexOverwriteAscii(char ch);
        void HexDeleteByte();
        void HexDeleteByteBackward();
        void HexSaveState();
        void HexUndo();
        void HexRedo();

        // Hex cursor
        void HexMoveCursorLeft(bool selecting = false);
        void HexMoveCursorRight(bool selecting = false);
        void HexMoveCursorUp(bool selecting = false);
        void HexMoveCursorDown(bool selecting = false);
        void HexMoveCursorPageUp(bool selecting = false);
        void HexMoveCursorPageDown(bool selecting = false);
        void HexMoveCursorToRowStart(bool selecting = false);
        void HexMoveCursorToRowEnd(bool selecting = false);
        void HexEnsureCursorVisible();

        // Hex helpers
        int HexGetRowForByte(int byteOffset) const;
        int HexGetColumnForByte(int byteOffset) const;
        std::pair<int,int> HexHitTestPoint(int mouseX, int mouseY) const;
        static std::string HexFormatAddress(int offset);
        static std::string HexFormatByte(uint8_t byte);
        static char HexPrintableChar(uint8_t byte);

    private:
        // Editing mode (PlainText, MarkdownHybrid, Hex)
        TextAreaEditingMode editingMode = TextAreaEditingMode::PlainText;
        // Clickable hit regions now live on each LineLayoutBase's `hitRects` vector in layout-local
        // coords. FindHitRectAtScreenPos translates + tests them. Legacy markdownHitRects removed.
        // Abbreviation definitions: abbreviation -> expansion text
        std::unordered_map<std::string, std::string> markdownAbbreviations;
        // Per-logical-line flag: true if line is an abbreviation definition (hidden from rendering)
        std::vector<bool> isAbbreviationDefinitionLine;
        // Footnote definitions: label -> content text
        std::unordered_map<std::string, std::string> markdownFootnotes;
        // Per-logical-line flag: true if line is a footnote definition (hidden from rendering)
        std::vector<bool> isFootnoteDefinitionLine;
        // Markdown anchor registry: anchor ID -> logical line number (rebuilt each render frame)
        std::unordered_map<std::string, int> markdownAnchors;
        // Backlink map: anchor ID -> source logical line (only for anchors with exactly 1 reference)
        std::unordered_map<std::string, int> markdownAnchorBacklinks;
        // Per-logical-line flag: true if line is a definition list term (rendered bold)
        std::vector<bool> isDefinitionTermLine;
        // Per-logical-line flag: true if line is hidden (lazy continuation merged into definition)
        std::vector<bool> isDefinitionHiddenLine;
        // Per-logical-line flag: true if line is a continuation paragraph of a definition
        std::vector<bool> isDefinitionContinuationLine;
        // Per-logical-line: for ": " lines, the full merged text (including lazy continuations)
        std::vector<std::string> definitionMergedText;
        // Path of the currently loaded document (for resolving relative image paths)
        std::string documentFilePath;
        // Per-display-line cumulative Y offset from block images (rebuilt each frame)
        std::vector<int> markdownLineYOffsets;

        // ===== HEX MODE STATE =====
        std::vector<uint8_t> hexBuffer;
        int hexCursorByteOffset = 0;
        bool hexCursorInAsciiPanel = false;   // false=hex panel, true=ASCII panel
        int hexCursorNibble = 0;              // 0=high, 1=low nibble (hex panel only)
        int hexSelectionStart = -1;
        int hexSelectionEnd = -1;
        bool hexIsSelectingWithMouse = false;
        int hexSelectionAnchor = -1;

        // Hex layout (recomputed on resize)
        int hexBytesPerRow = 16;
        int hexAddressWidth = 0;
        int hexByteWidth = 0;
        int hexAsciiCharWidth = 0;
        int hexPanelStartX = 0;
        int hexAsciiPanelStartX = 0;
        int hexRowHeight = 0;
        int hexTotalRows = 0;
        int hexFirstVisibleRow = 0;
        int hexMaxVisibleRows = 0;
        Rect2Di hexVisibleArea;

        // Hex undo
        struct HexState {
            std::vector<uint8_t> data;
            int cursorByteOffset;
            bool cursorInAsciiPanel;
            int selectionStart;
            int selectionEnd;
        };
        std::vector<HexState> hexUndoStack;
        std::vector<HexState> hexRedoStack;

    };
} // namespace UltraCanvas