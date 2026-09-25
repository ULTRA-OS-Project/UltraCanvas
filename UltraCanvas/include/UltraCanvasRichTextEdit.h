// include/UltraCanvasRichTextEdit.h
// UltraCanvasRichTextEdit — the WYSIWYG editing element: the caret sits in
// rendered text, and bold is a state of the selection rather than two
// asterisks in the buffer.
//
// It edits a UCRichDocument through UCRichDocumentEditor (which owns every
// editing rule and the undo history, UI-free) and renders each block through
// one ITextLayout carrying the runs' formatting as text attributes. That is
// what separates it from UltraCanvasTextArea: the TextArea edits plain text
// and re-derives formatting from Markdown every frame, so anything Markdown
// cannot spell cannot be typed; here the formatting lives in the document.
//
// The element performs no file I/O, exactly like the TextArea: applications
// load and save through UltraCanvasFileLoader::LoadTextDocument /
// UCWordDocumentIO and hand the resulting document over with SetDocument.
//
// Toolbars are NOT drawn by this element — build them from UltraCanvasToolbar,
// UltraCanvasDropdown, UltraCanvasButton and UltraCanvasColorPicker, and drive
// them from GetFormatState() plus the formatting methods below.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRichDocumentEditor.h"
#include "UltraCanvasSpellChecker.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== STYLE =====

struct RichTextEditStyle {
    // Base typography. A run with no fontFamily / fontSizePt of its own
    // inherits these, which is what "inherit" means in UCRichDocument.
    FontStyle baseFont;
    std::string codeFontFamily = "Courier New";

    Color textColor = Color(20, 20, 20);
    Color backgroundColor = Color(255, 255, 255);
    Color selectionColor = Color(180, 212, 253);
    Color cursorColor = Color(0, 0, 0);
    Color linkColor = Color(0, 102, 204);
    Color quoteBarColor = Color(200, 200, 200);
    Color quoteTextColor = Color(90, 90, 90);
    Color codeTextColor = Color(50, 50, 50);
    Color codeBackgroundColor = Color(246, 246, 246);
    Color codeBorderColor = Color(220, 220, 220);
    Color ruleColor = Color(200, 200, 200);
    Color pageBreakColor = Color(170, 170, 170);
    Color listMarkerColor = Color(80, 80, 80);
    Color tableBorderColor = Color(200, 200, 200);   // grid of tables without document borders
    Color tableGuideColor = Color(225, 225, 225);    // borderless document cells, editable view only
    Color imagePlaceholderColor = Color(150, 150, 150);
    Color borderColor = Color(170, 170, 170);

    // Heading sizes as multiples of the base font size (H1..H6).
    std::array<float, 6> headingSizeMultipliers = {2.0f, 1.6f, 1.35f, 1.2f, 1.1f, 1.0f};
    bool headingsBold = true;

    // Nested bullet characters (level 0, 1, 2+; UTF-8).
    std::array<std::string, 3> bulletCharacters = {
        "\xe2\x80\xa2",         // •
        "\xe2\x97\xa6",         // ◦
        "\xe2\x96\xaa"          // ▪
    };

    float padding = 8.0f;             // inside the element, around the page
    float listIndent = 24.0f;         // per nesting level
    float quoteIndent = 16.0f;
    float codeIndent = 12.0f;
    float blockSpacing = 6.0f;        // between blocks that state no spacing of their own
    float defaultTabStop = 36.0f;     // tab interval when the document states none
    float paragraphLeading = 0.0f;    // extra leading inside a paragraph
    float scrollbarWidth = 15.0f;
    bool drawBorder = true;
};

// A clickable region inside a rendered block (hyperlinks today).
struct RichTextHitRect {
    Rect2Df bounds;                   // content coordinates, before scrolling
    std::string linkTarget;
    int blockIndex = 0;
};

// ===== THE ELEMENT =====

class UltraCanvasRichTextEdit : public UltraCanvasUIElement {
public:
    UltraCanvasRichTextEdit(const std::string& name, float x, float y, float width, float height);
    UltraCanvasRichTextEdit(const std::string& name, float width, float height)
        : UltraCanvasRichTextEdit(name, -1, -1, width, height) {}
    explicit UltraCanvasRichTextEdit(const std::string& name)
        : UltraCanvasRichTextEdit(name, -1, -1, -1, -1) {}
    ~UltraCanvasRichTextEdit() override;

    // ===== ELEMENT PROTOCOL =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
    bool AcceptsFocus() const override { return !readOnly; }

    // ===== DOCUMENT =====
    // The element shares ownership of the document, so an application can keep
    // holding it (to save it, or to re-emit blocks the editor never touched).
    void SetDocument(std::shared_ptr<UCRichDocument> document);
    const std::shared_ptr<UCRichDocument>& GetDocument() const { return editor.GetDocument(); }
    // Convenience for simple cases and for tests.
    void SetMarkdown(const std::string& markdown, const std::string& baseDirectory = "");
    std::string GetMarkdown() const { return editor.GetMarkdown(); }
    std::string GetPlainText() const { return editor.GetPlainText(); }
    void Clear();

    // The editing core, for callers that need positions or block access
    // directly. Mutating it is fine; call InvalidateDocument() afterwards.
    UCRichDocumentEditor& GetEditor() { return editor; }
    const UCRichDocumentEditor& GetEditor() const { return editor; }
    // Drops every cached layout and re-measures on the next render.
    void InvalidateDocument();
    // Drops the cached layout of one block (after an edit inside it).
    void InvalidateBlock(int blockIndex);

    bool IsModified() const { return editor.IsModified(); }
    void SetModified(bool modified) { editor.SetModified(modified); }

    // ===== MODE =====
    void SetReadOnly(bool value);
    bool IsReadOnly() const { return readOnly; }

    // ===== STYLE =====
    const RichTextEditStyle& GetStyle() const { return style; }
    RichTextEditStyle& GetStyleMutable() { return style; }
    void SetStyle(const RichTextEditStyle& s);

    // ===== EDITING (also the toolbar surface) =====
    void InsertText(const std::string& utf8);
    void SelectAll();
    bool HasSelection() const { return editor.HasSelection(); }
    std::string GetSelectedText() const;
    void Cut();
    void Copy();
    void Paste();
    void DeleteSelection();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return editor.CanUndo(); }
    bool CanRedo() const { return editor.CanRedo(); }

    // Character formatting.
    void ToggleBold();
    void ToggleItalic();
    void ToggleUnderline();
    void ToggleStrikethrough();
    void ToggleInlineCode();
    void ToggleSubscript();
    void ToggleSuperscript();
    void SetFontFamily(const std::string& family);
    void SetFontSize(float pt);
    void SetTextColor(const std::string& hexColor);
    void SetLink(const std::string& target);
    void ClearFormatting();
    // What a toolbar should show for the current selection.
    RichCharFormatState GetFormatState() const { return editor.GetFormatState(); }

    // Paragraph formatting.
    void SetHeadingLevel(int level);          // 0 = body text
    void SetAlignment(RichTextAlign align);
    void ToggleBulletList();
    void ToggleNumberedList();
    void IndentList();
    void OutdentList();
    void ToggleBlockQuote();
    void ToggleCodeBlock(const std::string& language = "");
    // The block type at the caret, for a style dropdown.
    RichBlockType GetCurrentBlockType() const;
    int GetCurrentHeadingLevel() const;

    // Structure.
    void InsertHorizontalRule();
    void InsertPageBreak();
    bool InsertImageFromFile(const std::string& path, const std::string& altText = "");
    void InsertImageFromMemory(const std::string& name, const std::string& mimeType,
                               const std::vector<uint8_t>& data,
                               const std::string& altText = "");
    // The same two, but placing the picture INSIDE the line at the caret
    // rather than as a paragraph of its own.
    bool InsertInlineImageFromFile(const std::string& path, const std::string& altText = "");
    void InsertInlineImageFromMemory(const std::string& name, const std::string& mimeType,
                                     const std::vector<uint8_t>& data,
                                     const std::string& altText = "");

    // ===== TABLES =====
    // Structural table editing, addressed from the caret: a menu item says
    // "insert row below" and means the row the caret is in, so the element
    // resolves the caret's cell to its grid position and the editing core does
    // the span bookkeeping. Each returns false when the caret is not in a
    // table (or the operation does not apply), which is also what tells a menu
    // whether to offer the item.
    void InsertTable(int rows, int columns, bool headerRow = false);
    bool IsCaretInTable() const;
    // Geometry of the table the caret is in, for a menu that wants to say
    // "Delete row 2 of 5". Zeroes when the caret is not in a table.
    bool CaretTableGeometry(int& outRows, int& outColumns,
                            int& outRow, int& outColumn) const;

    bool InsertRowAbove();
    bool InsertRowBelow();
    bool InsertColumnLeft();
    bool InsertColumnRight();
    bool DeleteCurrentRow();
    bool DeleteCurrentColumn();
    // Merges the caret's cell with the one to its right / below it. False when
    // there is nothing there, or when the neighbour's own span would have to be
    // cut in half to do it.
    bool MergeWithCellRight();
    bool MergeWithCellBelow();
    bool CanSplitCurrentCell() const;
    bool SplitCurrentCell();

    // ===== SEARCH =====
    // FindNext starts at the end of the selection (so repeated calls walk
    // forwards through matches) and FindPrevious at its start. A match becomes
    // the selection and is scrolled into view.
    void SetFindOptions(const RichFindOptions& options) { findOptions = options; }
    const RichFindOptions& GetFindOptions() const { return findOptions; }
    bool FindNext(const std::string& needle);
    bool FindPrevious(const std::string& needle);
    // Replaces the selection when it already holds a match of `needle`, then
    // moves to the next one — the usual behaviour of a Replace button, which
    // does nothing destructive when the user has not found anything yet.
    bool ReplaceCurrent(const std::string& needle, const std::string& replacement);
    int ReplaceAll(const std::string& needle, const std::string& replacement);
    // How many matches the document holds, for a "3 of 12" readout.
    int CountMatches(const std::string& needle) const;

    // ===== SPELL CHECKING =====
    // Checking runs on the shared UltraCanvasSpellChecker worker thread, over
    // the document's text with blocks joined by '\n' — one job for the
    // document, not one per block. Results are drained while rendering.
    void SetSpellCheckEnabled(bool enabled);
    bool IsSpellCheckEnabled() const { return spellCheckEnabled; }
    void SetSpellCheckOptions(const SpellCheckOptions& options);
    const SpellCheckOptions& GetSpellCheckOptions() const { return spellOptions; }
    // Re-checks now (after changing dictionary or user words).
    void RunSpellCheck();
    const std::vector<SpellError>& GetSpellErrors() const { return spellErrors; }
    // The error under an element-local point, or null.
    const SpellError* GetSpellErrorAtPosition(int x, int y);
    // Replaces the flagged word and re-queues a check.
    bool ApplySpellSuggestion(const SpellError& error, const std::string& replacement);
    // Opens the built-in suggestion popup for a right-click; false when the
    // click was not on a flagged word, so a host can show its own menu.
    bool ShowSpellSuggestionMenu(const UCEvent& event);

    // ===== SCROLLING =====
    void ScrollToTop();
    void ScrollToCaret();
    float GetScrollOffset() const { return scrollOffset; }
    void SetScrollOffset(float offset);
    float GetContentHeight() const { return contentHeight; }

    // ===== CALLBACKS =====
    std::function<void()> onDocumentChanged;
    std::function<void()> onSelectionChanged;
    // Return true to consume a link click (otherwise it is ignored; the
    // element never launches a browser on its own).
    std::function<bool(const std::string& target)> onLinkClicked;
    // Right-click, before the built-in spell popup. Return true to consume it,
    // which is how a host puts the suggestions inside its own context menu.
    std::function<bool(const UCEvent& event)> onContextMenu;
    // Called with a copy of the options and the text about to be checked, so a
    // host can fill in SpellCheckOptions::shouldSkipRange for that text.
    // THREADING: see SpellCheckOptions::shouldSkipRange — the hook it installs
    // runs on the spell worker thread.
    std::function<void(SpellCheckOptions&, const std::string&)> onPrepareSpellCheck;

private:
    // ===== BLOCK LAYOUT CACHE =====
    // One entry per document block. Entries are built lazily and only for
    // blocks the viewport needs, so a long document does not pay for layouts
    // nobody sees; `bounds` is in content coordinates (before scrolling).
    struct BlockLayout {
        std::unique_ptr<ITextLayout> layout;      // null for image/rule/break/table
        Rect2Df bounds{0, 0, 0, 0};
        float textLeft = 0.0f;                    // indent of the text inside bounds
        float markerLeft = 0.0f;                  // list bullet / number position
        std::string markerText;
        std::vector<RichTextHitRect> hitRects;
        // Table blocks: one layout per cell, plus the geometry to draw them.
        std::vector<std::unique_ptr<BlockLayout>> cells;
        std::vector<int> cellColumns;
        std::vector<int> cellRows;
        // Table cells: where the text sits inside `bounds` (padding plus
        // vertical alignment) - used alike by drawing, caret and hit testing.
        float textTop = 0.0f;
        float textHeight = 0.0f;
        float textBottomPad = 0.0f;
        RichVerticalAlign verticalAlign = RichVerticalAlign::Top;
        std::shared_ptr<UCImage> image;           // image blocks
        // Pictures sitting inside this block's text. The layout reserves a box
        // for each (a CreateShape attribute over its U+FFFC placeholder) and
        // leaves the drawing to the caller, which is what these record.
        struct InlineImage {
            int byteOffset = 0;                   // the placeholder, in layout text
            float width = 0.0f;
            float height = 0.0f;
            std::shared_ptr<UCImage> image;
            std::string altText;
        };
        std::vector<InlineImage> inlineImages;
        bool valid = false;
    };

    // ===== LAYOUT =====
    void EnsureLayouts(IRenderContext* ctx);
    void BuildBlockLayout(IRenderContext* ctx, int blockIndex);
    std::unique_ptr<ITextLayout> MakeRunsLayout(IRenderContext* ctx,
                                                const RichDocBlock& block,
                                                const std::vector<RichTextRun>& runs,
                                                float wrapWidth,
                                                std::vector<RichTextHitRect>* outHits,
                                                int blockIndex,
                                                std::vector<BlockLayout::InlineImage>* outInlineImages = nullptr,
                                                float paragraphOriginX = -1.0f) const;
    void ApplyParagraphGeometry(ITextLayout* layout, const RichDocBlock& block,
                                const std::string& text, float originX, float wrapWidth) const;
    float GapAfterBlock(int index) const;
    FontStyle MarkerFontFor(const RichDocBlock& block) const;
    float WidestSiblingLabel(IRenderContext* ctx, int blockIndex) const;
    void DrawDocumentCellFrame(IRenderContext* ctx, const RichTableCell& cell, const Rect2Dd& rect) const;
    void DrawParagraphFrame(IRenderContext* ctx, int blockIndex, const BlockLayout& bl,
                            float originX, float originY) const;
    void ApplyRunAttributes(ITextLayout* layout, const RichDocBlock& block,
                            const std::vector<RichTextRun>& runs,
                            std::vector<RichTextHitRect>* outHits, int blockIndex,
                            std::vector<BlockLayout::InlineImage>* outInlineImages = nullptr) const;
    // cellRow/cellColumn identify a table cell's layout; -1/-1 is a block's own.
    void ApplySelectionAttributes(ITextLayout* layout, int blockIndex,
                                  int cellRow = -1, int cellColumn = -1) const;
    float BlockIndentFor(const RichDocBlock& block) const;
    FontStyle FontForBlock(const RichDocBlock& block) const;
    void RecalculateVisibleArea();

    // ===== RENDERING =====
    void RenderBlock(IRenderContext* ctx, int blockIndex, const BlockLayout& bl);
    void DrawInlineImages(IRenderContext* ctx, const BlockLayout& bl,
                          float originX, float originY) const;
    void DrawSelectionForNonTextBlock(IRenderContext* ctx, int blockIndex, const BlockLayout& bl);
    void DrawScrollbar(IRenderContext* ctx);
    void UpdateCaret();

    // ===== SEARCH / SPELL INTERNALS =====
    // The whole document as one string, blocks joined by '\n', plus the start
    // offset of each block in it. This is what the spell checker is given, and
    // what maps its byte offsets back onto {blockIndex, byteOffset}.
    std::string BuildSpellText(std::vector<int>& outBlockStarts) const;
    RichDocPosition SpellBytePosition(size_t byteOffset) const;
    // Element-local rectangles covering a byte range of one block, one per
    // visual line the range crosses.
    std::vector<Rect2Df> BlockRangeRects(int blockIndex, int startByte, int endByte) const;
    void QueueSpellCheck();
    void DropStaleSpellErrors();
    void RegisterSpellResultNotifier();
    void DrawSpellErrorMarks(IRenderContext* ctx);
    // Selects `match`, scrolls it into view and notifies.
    void SelectMatch(const RichDocRange& match);

    // The laid-out cell a position addresses, or null when it is not in one.
    const BlockLayout* CellLayoutFor(const RichDocPosition& pos) const;

    // ===== HIT TESTING =====
    // Element-local point -> document position. Snaps to the nearest block.
    RichDocPosition PositionFromPoint(const Point2Df& localPoint) const;
    const RichTextHitRect* LinkAtPoint(const Point2Df& localPoint) const;
    // Caret rectangle in element-local coordinates; invalid when off-screen.
    Rect2Df CaretRect() const;
    // Vertical motion: the position one visual line above/below `pos`.
    RichDocPosition VerticalStep(const RichDocPosition& pos, int direction) const;
    // The caret's x inside its block's layout — the column Up/Down aims for.
    float CaretLayoutX() const;

    // ===== INPUT =====
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    void AfterEdit();                 // invalidate, notify, keep the caret visible
    void AfterSelectionChange();

    UCRichDocumentEditor editor;
    RichTextEditStyle style;
    std::vector<BlockLayout> blockLayouts;

    Rect2Df visibleArea{0, 0, 0, 0};  // text area inside padding and scrollbar
    float scrollOffset = 0.0f;
    float contentHeight = 0.0f;
    float lastWrapWidth = -1.0f;
    // Selection the cached layouts were built under, so a moved selection
    // invalidates exactly the blocks it entered and left.
    RichDocRange appliedSelection;
    bool layoutsDirty = true;
    bool visibleAreaDirty = true;
    bool caretMoved = false;

    bool readOnly = false;
    bool selecting = false;           // mouse drag in progress
    bool draggingThumb = false;
    float thumbGrabOffset = 0.0f;
    Rect2Df thumbRect{0, 0, 0, 0};
    // Preferred x for Up/Down motion, so walking through short lines does not
    // drag the caret leftwards. -1 = recompute from the caret.
    float goalColumnX = -1.0f;

    RichFindOptions findOptions;

    // Spell checking. spellContextId identifies this element to the shared
    // service; spellBlockStarts maps a result's byte offsets back onto blocks.
    bool spellCheckEnabled = false;
    std::vector<SpellError> spellErrors;
    std::vector<int> spellBlockStarts;
    std::string spellText;            // the text the errors on hand describe
    uint64_t spellContextId = 0;
    SpellCheckOptions spellOptions;
    std::shared_ptr<UltraCanvasMenu> spellSuggestionMenu;
    // Cleared by the destructor: the worker-thread notifier can outlive the
    // element, and both of its hops test this before touching it.
    std::shared_ptr<std::atomic<bool>> spellAlive =
        std::make_shared<std::atomic<bool>>(true);

    // Rich clipboard within the process: the system clipboard carries the
    // plain text, and a paste whose text matches what was copied restores the
    // formatting too. Cross-application rich paste needs the clipboard MIME
    // flavours that UltraCanvasClipboardBackend does not carry yet.
    static std::vector<RichDocBlock> internalClipboard;
    static std::string internalClipboardText;
};

// ===== FACTORY =====

inline std::shared_ptr<UltraCanvasRichTextEdit> CreateRichTextEdit(
        const std::string& name, float x, float y, float width, float height) {
    return std::make_shared<UltraCanvasRichTextEdit>(name, x, y, width, height);
}

} // namespace UltraCanvas
