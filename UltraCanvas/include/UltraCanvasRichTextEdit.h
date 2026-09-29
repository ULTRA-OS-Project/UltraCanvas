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
#include "UltraCanvasInlineMath.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
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
    // Page view: the desk around the pages, the paper, and its shadow.
    Color deskColor = Color(212, 212, 212);
    Color pageColor = Color(255, 255, 255);
    Color pageShadowColor = Color(0, 0, 0, 40);
    Color pageMarginGuideColor = Color(210, 210, 210);  // text area corners, editable view only

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
    float pageGap = 16.0f;            // page view: desk between and around pages
    bool drawBorder = true;
    // Comments: the text they are on, and the pane beside the text.
    Color commentHighlightColor = Color(255, 236, 160);
    Color commentPaneColor = Color(238, 238, 238);
    Color commentBoxColor = Color(255, 255, 250);
    Color commentBorderColor = Color(222, 170, 40);
    Color commentAuthorColor = Color(150, 100, 0);
    float commentPaneWidth = 230.0f;
    // Tracked changes: inserted text underlined, deleted text struck through.
    Color insertionColor = Color(0, 105, 180);
    Color deletionColor = Color(185, 30, 30);
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
    // The document - also while a header or footer is being edited, when the
    // editing core (GetEditor) is working on that header's blocks instead.
    const std::shared_ptr<UCRichDocument>& GetDocument() const {
        return furnitureEdit ? furnitureEdit->bodyEditor.GetDocument() : editor.GetDocument();
    }
    // Convenience for simple cases and for tests.
    void SetMarkdown(const std::string& markdown, const std::string& baseDirectory = "");
    std::string GetMarkdown() const { return GetDocument()->ToMarkdown(); }
    std::string GetPlainText() const { return GetDocument()->ToPlainText(); }
    void Clear();

    // The editing core, for callers that need positions or block access
    // directly. Mutating it is fine; call InvalidateDocument() afterwards.
    UCRichDocumentEditor& GetEditor() { return editor; }
    const UCRichDocumentEditor& GetEditor() const { return editor; }
    // Drops every cached layout and re-measures on the next render.
    void InvalidateDocument();
    // Drops the cached layout of one block (after an edit inside it).
    void InvalidateBlock(int blockIndex);

    bool IsModified() const {
        return editor.IsModified() || (furnitureEdit && furnitureEdit->bodyEditor.IsModified());
    }
    void SetModified(bool modified) {
        editor.SetModified(modified);
        if (furnitureEdit) furnitureEdit->bodyEditor.SetModified(modified);
    }

    // ===== HEADERS AND FOOTERS =====
    // Double-click a page's header or footer (or its top or bottom margin, to
    // make one) to edit it; Escape, or a click in the body, goes back. While
    // it is edited the body is shown greyed, and everything - typing,
    // formatting, pictures, tables, page fields, undo - acts on the header or
    // footer. A document whose first page differs (firstPageDiffers) has two
    // of each; page 0 edits the first page's. Changes go into the document as
    // they are made. Outside page view, the one header and footer above and
    // below the body can be edited the same way.
    bool EditHeader(int pageIndex = 0);
    bool EditFooter(int pageIndex = 0);
    // Footnotes and endnotes. Footnotes are drawn at the foot of the page
    // their reference is on (below a short rule; the page's text makes room),
    // endnotes after the body - outside page view, both after the body.
    // Inserting one puts its reference at the caret and opens the note for
    // typing; double-clicking a note (or its reference) edits it, Escape goes
    // back. The marks number themselves: footnotes 1, 2, 3, endnotes i, ii.
    bool InsertFootnote();
    bool InsertEndnote();
    bool EditNote(int noteIndex);
    bool IsEditingNote() const { return furnitureEdit && furnitureEdit->noteIndex >= 0; }
    bool IsEditingHeaderOrFooter() const { return furnitureEdit != nullptr; }
    bool IsEditingFooter() const { return furnitureEdit && furnitureEdit->footer; }
    void FinishHeaderFooterEditing();
    std::function<void(bool editing)> onHeaderFooterEditingChanged;

    // ===== MODE =====
    void SetReadOnly(bool value);
    bool IsReadOnly() const { return readOnly; }

    // Page view, like a word processor's print layout: the document's pages
    // (UCRichDocument::page - A4 with 2 cm margins when it states none) drawn
    // on a desk, each with its header and footer, and the text column as wide
    // as the page's. Blocks move to the next page whole; a page break starts
    // one. Off (the default), the text fills the element and a document's
    // first-page header and footer sit above and below it.
    void SetPageView(bool enabled);
    bool IsPageView() const { return pageView; }
    // Pages laid out so far: 1 outside page view.
    int GetPageCount() const { return pageView ? std::max(1, static_cast<int>(pages.size())) : 1; }

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
    // Named styles (UCRichDocument::styles; a word processor's basic set
    // when the document has none). See UCRichDocumentEditor for the rules.
    std::vector<RichStyle> GetStyles() const { return editor.GetStyles(); }
    bool ApplyParagraphStyle(const std::string& id);
    bool ApplyCharacterStyle(const std::string& id);
    bool UpdateStyle(const RichStyle& style);
    bool DeleteStyle(const std::string& id);
    // Makes the caret paragraph's formatting into a new style and gives the
    // paragraph that style; or redefines its current style to match it.
    bool NewStyleFromCaret(const std::string& name);
    bool UpdateStyleFromCaret();
    std::string GetCurrentParagraphStyle() const { return editor.CurrentParagraphStyle(); }
    std::string GetCurrentCharacterStyle() const { return editor.CurrentCharacterStyle(); }
    // Check lists: a box in place of the bullet, ticked by clicking it (or by
    // ToggleCheckedAtCaret, for a keyboard shortcut).
    void ToggleCheckList();
    bool ToggleCheckedAtCaret();
    // The block type at the caret, for a style dropdown.
    RichBlockType GetCurrentBlockType() const;
    int GetCurrentHeadingLevel() const;

    // ===== BOOKMARKS, CROSS-REFERENCES, CAPTIONS, CONTENTS =====
    // See UCRichDocumentEditor. Cross-references, caption numbers and the
    // table of contents' page numbers keep themselves up to date as the
    // document changes (page numbers in page view).
    std::vector<UCRichDocument::BookmarkInfo> GetBookmarks() const { return GetDocument()->Bookmarks(); }
    bool AddBookmark(const std::string& name);
    bool RemoveBookmark(const std::string& name);
    bool InsertCrossReference(const std::string& bookmark, bool pageNumber = false);
    std::string InsertCaption(const std::string& label, const std::string& text = "");
    bool InsertTableOfContents(int maxLevel = 3);
    bool UpdateTableOfContents(int maxLevel = 3);
    // Puts the caret at a bookmark and scrolls it into view. Ctrl+click on a
    // cross-reference, a table of contents entry or a link to "#name" does it.
    bool GoToBookmark(const std::string& name);

    // Structure.
    void InsertHorizontalRule();
    void InsertPageBreak();
    // A page number or page count field at the caret: in page view it shows
    // the page it is on (or how many there are), and DOCX/ODT save it as a
    // field, so a word processor numbers it too.
    void InsertPageNumberField();
    void InsertPageCountField();
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

    // ===== PICTURES =====
    // Clicking a picture selects it: a frame with eight handles is drawn round
    // it, and dragging a handle resizes it (a corner keeps its proportions).
    // Delete removes a selected picture, like any selection.
    bool HasSelectedImage() const;
    // {block, 0} for an Image block, the placeholder's offset for a picture
    // in the line (see UCRichDocumentEditor::IsImageAt).
    RichDocPosition GetSelectedImage() const { return selectedImage; }
    // Selects the picture at `image`; false when there is none.
    bool SelectImage(const RichDocPosition& image);
    bool SetSelectedImageSize(float widthPt, float heightPt);
    std::string GetSelectedImageAltText() const;
    bool SetSelectedImageAltText(const std::string& altText);

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
    // A selection dragged (or Shift+arrowed) from one cell into another is a
    // block of whole cells: Delete empties them, formatting and alignment
    // apply to all of them, Copy copies them as a table, and this merges them
    // into one. False without such a selection.
    bool HasCellSelection() const { return editor.HasCellSelection(); }
    bool MergeSelectedCells();
        bool CanSplitCurrentCell() const;
    bool SplitCurrentCell();

    // ===== AUTOFORMAT AS YOU TYPE =====
    // On by default: typed quotes become “curly”, "--" a dash, "..." an
    // ellipsis, (c) ©, and "1. ", "- ", "[ ] ", "# " or "> " opening a
    // paragraph make it a list, check list, heading or quote; a paragraph of
    // "---" becomes a rule on Enter. Each correction is its own undo step,
    // so Ctrl+Z straight after takes back just the correction.
    void SetAutoFormatEnabled(bool enabled) { editor.SetAutoFormatEnabled(enabled); }
    bool IsAutoFormatEnabled() const { return editor.IsAutoFormatEnabled(); }
    void SetAutoFormatOptions(const RichAutoFormatOptions& options) { editor.SetAutoFormatOptions(options); }
    const RichAutoFormatOptions& GetAutoFormatOptions() const { return editor.GetAutoFormatOptions(); }

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

    // ===== PDF =====
    // The document as a PDF: its pages as page view lays them out (whether or
    // not the element is in page view), with headers, footers and page
    // numbers, as vectors with real text - and without anything that belongs
    // to editing (selection, caret, margin marks, table guides). Works on an
    // element that has never been shown.
    bool ExportToPdf(const std::string& utf8Path, std::string& error);
    // The same, into memory (for printing, attaching, uploading).
    bool ExportToPdf(std::vector<uint8_t>& pdfBytes, std::string& error);

    // ===== ZOOM =====
    // 1 = 100%. Everything is drawn scaled - text, pictures, pages - and
    // outside page view the text rewraps to the zoomed width, as a word
    // processor's web view does. Ctrl+wheel zooms too. 0.25 to 5.
    void SetZoom(float factor);
    float GetZoom() const { return zoom; }
    std::function<void(float zoom)> onZoomChanged;

    // ===== SCROLLING =====
    // A page wider than the view (a landscape page, or zoomed in) scrolls
    // sideways too: a horizontal scrollbar appears, Shift+wheel scrolls it.
    float GetHorizontalScrollOffset() const { return hScrollOffset; }
    void SetHorizontalScrollOffset(float offset);
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
    // A comment's box was double-clicked: a host opens its editor
    // (SetCommentText). Its text is selected already.
    std::function<void(int commentIndex)> onCommentActivated;

    // ===== COMMENTS =====
    // A comment on the selection (the word at the caret without one). The
    // commented text is shaded and the comments are shown in a pane at the
    // element's right, each level with its text; clicking one selects its
    // text. Resolved comments are shown pale.
    int AddComment(const std::string& text);
    bool RemoveComment(int index);
    bool SetCommentText(int index, const std::string& text);
    bool SetCommentResolved(int index, bool resolved);
    std::vector<int> GetCommentsAtCaret() const { return editor.CommentsAt(editor.GetCaret()); }
    // The name new comments and tracked changes are signed with.
    void SetCommentAuthor(const std::string& name) {
        commentAuthor = name;
        editor.SetRevisionAuthor(name, CurrentIsoTime());
    }

    // ===== SECTIONS =====
    // A section break at the caret, and the caret's section in columns. In
    // page view a section's text fills its columns one after the other, each
    // to the foot of the page (a paragraph moves whole to the next column);
    // outside page view the text is one column.
    bool InsertSectionBreak(bool newPage);
    bool SetSectionColumns(int columns, float gapPt = 36.0f);
    RichSectionSetup GetCurrentSection() const { return editor.CurrentSection(); }

    // ===== TRACKED CHANGES =====
    // See UCRichDocumentEditor::SetTrackChanges. Inserted text is shown
    // underlined (style.insertionColor), deleted text struck through
    // (style.deletionColor), until accepted or rejected.
    void SetTrackChanges(bool enabled);
    bool IsTrackingChanges() const { return editor.IsTrackingChanges(); }
    bool AcceptAllChanges();
    bool RejectAllChanges();
    // The change at the caret, or every change in the selection.
    bool AcceptChangeAtCaret();
    bool RejectChangeAtCaret();
    // Selects the next tracked change after the caret (wrapping round).
    bool GoToNextChange();
    // "2026-09-29T10:00:00Z": the time stamp comments and changes get.
    static std::string CurrentIsoTime();
    const std::string& GetCommentAuthor() const { return commentAuthor; }
    // Whether comments (their shading and pane) are shown; on by default.
    void SetShowComments(bool show);
    bool IsShowingComments() const { return showComments; }
    bool IsCommentPaneVisible() const { return commentPaneShown; }
    // The caret's rectangle, element coordinates (for tests and hosts that
    // place a popup at the caret); empty when it is not laid out.
    Rect2Df GetCaretRectForTest() const { return ToElement(CaretRect()); }
    // Drag and drop: on by default. A drag moves the selection; with Ctrl held
    // at the drop it copies it.
    bool enableDragAndDrop = true;
    // Files dropped from another application, with the document position
    // under the drop. Return true to consume them; otherwise image files are
    // inserted there as pictures in the line and anything else is ignored.
    std::function<bool(const std::vector<std::string>& paths, const RichDocPosition& at)> onFilesDropped;
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
        // In a section of several columns (page view): how far right of the
        // text column's left edge its column is, and the width it was laid
        // out at (0 = the whole column).
        float columnX = 0.0f;
        float builtWidth = -1.0f;
        float textLeft = 0.0f;                    // indent of the text inside bounds
        float markerLeft = 0.0f;                  // list bullet / number position
        std::string markerText;
        bool checkbox = false;                    // check list item: a box instead of markerText
        bool checked = false;
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
            // A typeset formula (a math run) instead of a picture: drawn with
            // its baseline on the line's, over the run's first character.
            std::shared_ptr<UltraCanvasInlineMath> math;
            // A floating picture: no room in the line; placed by the page
            // layout (see PlacedFloat) at its paragraph's top.
            bool floating = false;
            RichTextRun::ImageWrap wrap = RichTextRun::ImageWrap::Inline;
            RichTextAlign floatAlign = RichTextAlign::Left;
            float offsetX = 0.0f;                 // pixels
            float offsetY = 0.0f;
        };
        std::vector<InlineImage> inlineImages;
        // A display formula (MathBlock) typeset whole, centred in the column;
        // set only while the caret is elsewhere - editing shows the source.
        std::shared_ptr<UltraCanvasInlineMath> displayMath;
        // Room taken from the column's left and right by floating pictures
        // beside this block (pixels); set by the placement pass.
        float intrudeLeft = 0.0f;
        float intrudeRight = 0.0f;
        // Page view: the pieces of a block that runs over a page end, one per
        // page - layout y range [from, to) drawn at content y `top`, below a
        // repeat of the table's header rows `headerHeight` tall. Empty = the
        // block is whole, at bounds.y.
        struct PageSlice {
            float from = 0.0f;
            float to = 0.0f;
            float top = 0.0f;
            float headerHeight = 0.0f;
        };
        std::vector<PageSlice> slices;
        // Tables: layout y of each row boundary a page may break at (no cell
        // spans across it), and the height of the leading header rows.
        std::vector<float> rowBreaks;
        float headerRowsHeight = 0.0f;
        bool valid = false;
    };

    // ===== LAYOUT =====
    void EnsureLayouts(IRenderContext* ctx);
    void BuildBlockLayout(IRenderContext* ctx, int blockIndex);
    // Lays out blocks[index] into `bl`. `blockIndex` is the block's index in
    // the edited document, which selection and link hits refer to; -1 for a
    // header or footer block, which carries neither.
    void BuildBlockLayout(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int index,
                          BlockLayout& bl, int blockIndex);
    std::unique_ptr<ITextLayout> MakeRunsLayout(IRenderContext* ctx,
                                                const RichDocBlock& block,
                                                const std::vector<RichTextRun>& runs,
                                                float wrapWidth,
                                                std::vector<RichTextHitRect>* outHits,
                                                int blockIndex,
                                                std::vector<BlockLayout::InlineImage>* outInlineImages = nullptr,
                                                float paragraphOriginX = -1.0f,
                                                int cellRow = -1, int cellColumn = -1) const;
    void ApplyParagraphGeometry(ITextLayout* layout, const RichDocBlock& block,
                                const std::string& text, float originX, float wrapWidth) const;
    float GapAfterBlock(int index) const;
    float GapAfterBlock(const std::vector<RichDocBlock>& blocks, int index) const;
    FontStyle MarkerFontFor(const RichDocBlock& block) const;
    // A check list item's box, relative to the block's origin (content x of
    // the column, top of the block).
    Rect2Df CheckboxRect(const RichDocBlock& block, const BlockLayout& bl) const;
    // The check list item whose box is under an element-local point, or -1.
    int CheckboxAtPoint(const Point2Df& localPoint) const;
    float WidestSiblingLabel(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int index) const;
    void DrawDocumentCellFrame(IRenderContext* ctx, const RichTableCell& cell, const Rect2Dd& rect) const;
    void DrawParagraphFrame(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int index,
                            const BlockLayout& bl, float originX, float originY) const;
    void ApplyRunAttributes(ITextLayout* layout, const RichDocBlock& block,
                            const std::vector<RichTextRun>& runs,
                            std::vector<RichTextHitRect>* outHits, int blockIndex,
                            std::vector<BlockLayout::InlineImage>* outInlineImages = nullptr,
                            int cellRow = -1, int cellColumn = -1) const;
    // True when the caret (or a selection end) sits inside [start, end) of
    // the container {blockIndex, cellRow, cellColumn}: a formula being
    // edited shows its source.
    bool CaretWithin(int blockIndex, int cellRow, int cellColumn, int start, int end) const;
    // cellRow/cellColumn identify a table cell's layout; -1/-1 is a block's own.
    void ApplySelectionAttributes(ITextLayout* layout, int blockIndex,
                                  int cellRow = -1, int cellColumn = -1) const;
    float BlockIndentFor(const RichDocBlock& block) const;
    FontStyle FontForBlock(const RichDocBlock& block) const;
    void RecalculateVisibleArea();

    // ===== FLOATING PICTURES =====
    // A floating picture where the placement pass put it: x from the text
    // column's left edge, y in content coordinates.
    struct PlacedFloat {
        int blockIndex = 0;
        int byteOffset = 0;
        Rect2Df rect{0, 0, 0, 0};
        RichTextRun::ImageWrap wrap = RichTextRun::ImageWrap::Square;
        bool beside = true;                       // text flows beside it (else above and below)
        std::shared_ptr<UCImage> image;
    };
    std::vector<PlacedFloat> placedFloats;
    // Places block `index`'s floats at `top` and fits the block round the
    // floats placed so far (moving it below a top-and-bottom one, narrowing it
    // beside a square one - relaying it out when that changes). Returns the
    // block's top.
    float FlowAroundFloats(IRenderContext* ctx, int index, float top, std::vector<PlacedFloat>& pageFloats);
    void DrawFloats(IRenderContext* ctx, bool behindText);

    // ===== EDITING A HEADER OR FOOTER =====
    // The body's editing state, parked while `editor` edits the furniture.
    struct FurnitureEditState {
        bool footer = false;
        bool firstPage = false;               // the first page's own furniture
        int pageIndex = 0;
        int noteIndex = -1;                   // >= 0: a note is edited, not a header
        UCRichDocumentEditor bodyEditor;
        float bodyContentHeight = 0.0f;
    };
    std::unique_ptr<FurnitureEditState> furnitureEdit;
    bool BeginFurnitureEditing(int pageIndex, bool footer);
    // Parks the body and hands `editor` a document of `part`'s blocks.
    void StartEditingPart(std::shared_ptr<UCRichDocument> part, std::unique_ptr<FurnitureEditState> state);
    // Writes the furniture being edited into the document.
    void SyncFurnitureToDocument();
    // Places the furniture's blocks where it sits on its page. Returns the
    // content height (the body's).
    float PlaceFurnitureBeingEdited(IRenderContext* ctx);
    // The header (true) or footer region of the page under a content point,
    // for a double-click; false outside both.
    bool FurnitureRegionAt(float contentY, int& outPage, bool& outFooter) const;
    void RenderBodyBackdrop(IRenderContext* ctx);

    // ===== PAGES =====
    // A header or footer as laid out for one page (its page number filled in).
    struct FurnitureLayout {
        std::vector<RichDocBlock> blocks;
        std::vector<BlockLayout> layouts;         // bounds.y relative to the first block
        float height = 0.0f;
    };
    struct PageFrame {
        float top = 0.0f;                         // content coordinates
        float bodyTop = 0.0f;
        float bodyBottom = 0.0f;
        float headerTop = 0.0f;
        float footerTop = 0.0f;
        std::shared_ptr<FurnitureLayout> header;
        std::shared_ptr<FurnitureLayout> footer;
    };
    // ===== NOTES =====
    // A footnote or endnote laid out where it is drawn: `top` in content
    // coordinates; `ruleAbove` for the first footnote of a page and the first
    // endnote, which get the short separating rule.
    struct NoteArea {
        int noteIndex = -1;
        int page = 0;
        float top = 0.0f;
        bool ruleAbove = false;
        std::shared_ptr<FurnitureLayout> layout;
    };
    std::vector<NoteArea> noteAreas;
    std::vector<float> pageFootnoteRoom;      // per page: height the footnotes take
    bool BeginNoteEditing(int noteIndex);
    std::string BookmarkTargetAt(const RichDocPosition& position) const;
    bool InsertNoteOf(RichNote::Kind kind);
    // Room above the first note of a page (or of the endnotes) for its rule.
    float NoteRuleSpace() const { return static_cast<float>(style.baseFont.fontSize) * 1.2f; }
    // The note laid out at the column's width, its mark in front.
    std::shared_ptr<FurnitureLayout> LayoutNote(IRenderContext* ctx, int noteIndex, const std::string& mark);
    // Page view: sets each page's footnotes below its text, returning true
    // when the room they take changed (the pages must then be placed again).
    bool PlaceFootnotes(IRenderContext* ctx);
    // Endnotes (and, outside page view, footnotes too) from `y` on. Returns
    // the y below the last one.
    float PlaceEndnotes(IRenderContext* ctx, float y, bool footnotesToo);
    // Places the body (pages, footnotes, endnotes); the content height.
    float PlaceBody(IRenderContext* ctx);
    // The note under a content point, or -1.
    int NoteAreaAt(float contentY) const;
    void RenderNotes(IRenderContext* ctx);

    bool PageViewActive() const { return pageView; }
    RichPageSetup EffectivePageSetup() const;
    void UpdateColumnGeometry();
    // The text column: its left edge in element coordinates and its width.
    float ColumnLeft() const { return visibleArea.x + columnOffsetX - hScrollOffset; }
    float ColumnWidth() const { return columnWidthOverride > 0.0f ? columnWidthOverride : columnWidth; }
    // Page view with multi-column sections: each body block's column width
    // (0 = the whole text column), and whether any section has columns.
    std::vector<float> blockColumnWidths;
    bool hasColumns = false;
    mutable float columnWidthOverride = 0.0f;
    bool placingParkedBody = false;           // PlaceFurnitureBeingEdited placing the body
    // Where a body block's column starts, in element coordinates.
    float BlockLeft(const BlockLayout& bl) const { return ColumnLeft() + bl.columnX; }
    std::shared_ptr<FurnitureLayout> LayoutFurniture(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                                     int pageNumber, int pageCount);
    // Positions every block: on pages in page view, one column otherwise.
    float PlaceBlocksOnPages(IRenderContext* ctx);
    float PlaceBlocksInColumn(IRenderContext* ctx);
    void RenderPages(IRenderContext* ctx);
    // Page view slicing. Where a block's layout y is drawn, and back; the
    // bottom of its last piece.
    float BlockToContentY(const BlockLayout& bl, float layoutY) const;
    float ContentToBlockY(const BlockLayout& bl, float contentY) const;
    float BlockVisualBottom(const BlockLayout& bl) const;
    // Layout y's at which block `index` may continue on the next page, with
    // widow and orphan control (two lines at least either side), ascending.
    std::vector<float> PageBreakCandidates(int index) const;
    // Page view: which page a content y is on (0 outside page view).
    int PageIndexAt(float contentY) const;
    bool UpdateBodyPageFields();
    void RenderFurniture(IRenderContext* ctx, const FurnitureLayout& furniture, float top);

    // ===== RENDERING =====
    void RenderBlock(IRenderContext* ctx, int blockIndex, const BlockLayout& bl);
    void RenderBlockPieces(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int blockIndex,
                           const BlockLayout& bl, int selectionIndex);
    void RenderBlock(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int index,
                     const BlockLayout& bl, float originX, float originY, int blockIndex);
    void DrawInlineImages(IRenderContext* ctx, const BlockLayout& bl,
                          float originX, float originY) const;
    void DrawSelectionForNonTextBlock(IRenderContext* ctx, int blockIndex, const BlockLayout& bl, float originY);
    void DrawScrollbar(IRenderContext* ctx);
    void DrawHorizontalScrollbar(IRenderContext* ctx);
    // Element-local point -> the document space everything is laid out in
    // (the zoom undone), and a document rectangle back to element-local.
    Point2Df ToDocument(const Point2Di& elementPoint) const;
    Point2Df ToDocument(const Point2Df& elementPoint) const;
    Rect2Df ToElement(const Rect2Df& documentRect) const;
    // The width of what is laid out (a page and its desk in page view).
    float ContentWidth() const;
    float MaxHorizontalScroll() const;
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

    // ===== PICTURES (internal) =====
    // Every laid-out picture, with its element-local rectangle.
    void ForEachImage(const std::function<void(const RichDocPosition&, const Rect2Df&)>& visit) const;
    bool ImageAtPoint(const Point2Df& localPoint, RichDocPosition& outImage, Rect2Df& outRect) const;
    bool ImageRectFor(const RichDocPosition& image, Rect2Df& outRect) const;
    // Handle 0..7 (clockwise from the top-left corner) under the point, or -1.
    int ImageHandleAt(const Rect2Df& imageRect, const Point2Df& localPoint) const;
    static std::array<Point2Df, 8> ImageHandleCentres(const Rect2Df& rect);
    Rect2Df ResizedImageRect(const Point2Df& pointer) const;
    void DrawImageSelection(IRenderContext* ctx);

    // ===== HIT TESTING =====
    // Element-local point -> document position. Snaps to the nearest block.
    RichDocPosition PositionFromPoint(const Point2Df& localPoint) const;
    const RichTextHitRect* LinkAtPoint(const Point2Df& localPoint) const;
    // Caret rectangle in element-local coordinates; invalid when off-screen.
    Rect2Df CaretRect() const;
    // The same for any position (the drop point of a drag).
    Rect2Df PositionRect(const RichDocPosition& position) const;
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
    bool HandleFileDrop(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    void AfterEdit();                 // invalidate, notify, keep the caret visible
    void AfterSelectionChange();

    UCRichDocumentEditor editor;
    RichTextEditStyle style;
    std::vector<BlockLayout> blockLayouts;

    Rect2Df visibleArea{0, 0, 0, 0};  // text area inside padding and scrollbar
    // Page view state. The text column sits columnOffsetX right of
    // visibleArea.x and is columnWidth wide (the whole visible width outside
    // page view).
    bool pageView = false;
    float columnOffsetX = 0.0f;
    float columnWidth = 1.0f;
    float pageLeftX = 0.0f;           // page's left edge, from visibleArea.x
    float pageWidthPx = 0.0f;
    float pageHeightPx = 0.0f;
    std::vector<PageFrame> pages;     // page view: every page; else one frame for the furniture
    // The body's layout while a header or footer is edited (drawn greyed).
    std::vector<BlockLayout> parkedLayouts;
    std::vector<PageFrame> parkedPages;
    std::vector<PlacedFloat> parkedFloats;
    // Header and footer layouts by furniture, page number and page count, so
    // scrolling does not re-lay them. Cleared with the block layouts.
    std::map<std::string, std::shared_ptr<FurnitureLayout>> furnitureCache;
    float furnitureCacheWidth = -1.0f;
    float scrollOffset = 0.0f;
    float hScrollOffset = 0.0f;       // document pixels, page view only
    float zoom = 1.0f;
    bool needsHorizontalScrollbar = false;
    Rect2Df hThumbRect{0, 0, 0, 0};
    bool draggingHThumb = false;
    float hThumbGrabOffset = 0.0f;
    float contentHeight = 0.0f;
    float lastWrapWidth = -1.0f;
    // Selection the cached layouts were built under, so a moved selection
    // invalidates exactly the blocks it entered and left.
    RichDocRange appliedSelection;
    bool layoutsDirty = true;
    bool visibleAreaDirty = true;
    bool caretMoved = false;

    bool readOnly = false;
    // Drawing for output (PDF, print): no selection, caret, guides or marks.
    bool printing = false;
    // Comments' pane: shown while the document has comments and showComments.
    std::string commentAuthor;
    bool showComments = true;
    bool commentPaneShown = false;
    struct CommentBox {
        int index = -1;
        Rect2Df rect{0, 0, 0, 0};             // element coordinates
    };
    std::vector<CommentBox> commentBoxes;
    float CommentPaneLeft() const;
    void RenderCommentPane(IRenderContext* ctx);
    int CommentBoxAt(const Point2Di& elementPoint) const;
    bool ShowsEditingMarks() const { return !readOnly && !printing; }
    bool ExportPdfPages(class UltraCanvasPdfSurface& pdf, std::string& error);
    bool selecting = false;           // mouse drag in progress
    // Drag and drop of the selection: a press inside it arms a drag, which
    // starts once the pointer has moved a few pixels (a press and release
    // without moving just places the caret there).
    bool dragArmed = false;
    bool draggingText = false;
    Point2Df dragStartPoint{0, 0};
    RichDocPosition dropPosition;
    // The selected picture, and a resize in progress.
    bool imageSelected = false;
    RichDocPosition selectedImage;
    RichDocPosition imageSelectionAnchor, imageSelectionCaret;   // what SelectImage set
    int resizeHandle = -1;
    Rect2Df resizeStartRect{0, 0, 0, 0};
    Point2Df resizeStartPoint{0, 0};
    Rect2Df resizePreview{0, 0, 0, 0};
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
