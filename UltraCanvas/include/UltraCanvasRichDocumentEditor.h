// include/UltraCanvasRichDocumentEditor.h
// UCRichDocumentEditor — the editing core over UCRichDocument: caret and
// selection positions, text and structure editing, character and paragraph
// formatting, and undo/redo.
//
// Like the model it edits, this layer is deliberately UI-free (std types only,
// no framework headers): it knows nothing about layouts, pixels, fonts or
// events, so every editing rule is unit-testable without a display.
// UltraCanvasRichTextEdit is the element that renders it and feeds it input.
//
// Positions address a block and a BYTE OFFSET into that block's concatenated
// run text — never a {run, offset} pair. Applying a format splits and merges
// runs constantly, and a caret must not move when the run structure changes
// underneath it. The concatenated text is also exactly what the element hands
// to ITextLayout, so layout hit-testing maps to these offsets with no
// translation step.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== POSITIONS =====

// A caret position or selection endpoint.
// A position addresses one *text container*: either a block's own runs, or one
// cell of a table block. cellRow/cellColumn are -1 for the former, which is
// what every non-table position uses, so the two-argument constructor and every
// existing {block, offset} position keep their old meaning.
struct RichDocPosition {
    int blockIndex = 0;     // index into UCRichDocument::blocks
    int cellRow = -1;       // -1 = the block's own runs; >= 0 = a table cell
    int cellColumn = -1;
    int byteOffset = 0;     // into that container's concatenated run text

    RichDocPosition() = default;
    RichDocPosition(int block, int offset) : blockIndex(block), byteOffset(offset) {}
    RichDocPosition(int block, int row, int column, int offset)
        : blockIndex(block), cellRow(row), cellColumn(column), byteOffset(offset) {}

    bool InCell() const { return cellRow >= 0 && cellColumn >= 0; }
    // The container this position is in, ignoring the offset — two positions
    // are in the same container exactly when this compares equal.
    bool SameContainer(const RichDocPosition& o) const {
        return blockIndex == o.blockIndex && cellRow == o.cellRow && cellColumn == o.cellColumn;
    }

    bool operator==(const RichDocPosition& o) const {
        return SameContainer(o) && byteOffset == o.byteOffset;
    }
    bool operator!=(const RichDocPosition& o) const { return !(*this == o); }
    // Document order: block, then row-major through a table's cells, then offset.
    bool operator<(const RichDocPosition& o) const {
        if (blockIndex != o.blockIndex) return blockIndex < o.blockIndex;
        if (cellRow != o.cellRow) return cellRow < o.cellRow;
        if (cellColumn != o.cellColumn) return cellColumn < o.cellColumn;
        return byteOffset < o.byteOffset;
    }
    bool operator<=(const RichDocPosition& o) const { return *this < o || *this == o; }
    bool operator>(const RichDocPosition& o) const { return o < *this; }
    bool operator>=(const RichDocPosition& o) const { return o <= *this; }
};

// An ordered position pair. Construction normalizes, so `start <= end` always.
struct RichDocRange {
    RichDocPosition start;
    RichDocPosition end;

    RichDocRange() = default;
    RichDocRange(const RichDocPosition& a, const RichDocPosition& b) {
        if (a <= b) { start = a; end = b; } else { start = b; end = a; }
    }
    bool IsEmpty() const { return start == end; }
    bool SingleBlock() const { return start.blockIndex == end.blockIndex; }
    bool Contains(const RichDocPosition& pos) const { return start <= pos && pos <= end; }
};

// ===== CHARACTER FORMATTING =====

// Which character attributes a formatting command should change. A command
// carries only the fields whose `set*` flag is true, so "make this bold"
// never disturbs the font or the colour of the runs it covers.
struct RichCharFormatDelta {
    bool setBold = false,          bold = false;
    bool setItalic = false,        italic = false;
    bool setUnderline = false,     underline = false;
    bool setStrikethrough = false, strikethrough = false;
    bool setCode = false,          code = false;
    bool setSubscript = false,     subscript = false;
    bool setSuperscript = false,   superscript = false;
    bool setFontFamily = false;    std::string fontFamily;
    bool setFontSize = false;      float fontSizePt = 0.0f;
    bool setColor = false;         std::string color;        // "#RRGGBB", empty = inherit
    bool setLink = false;          std::string linkTarget;   // empty = remove the link
    int addComment = -1;           // put the text under this comment
    int removeComment = -1;        // take it out from under this one

    bool IsEmpty() const {
        return !setBold && !setItalic && !setUnderline && !setStrikethrough && !setCode
            && !setSubscript && !setSuperscript && !setFontFamily && !setFontSize
            && !setColor && !setLink && addComment < 0 && removeComment < 0;
    }
    // Applies this delta to one run's attributes.
    void ApplyTo(RichTextRun& run) const;
};

// The formatting of a selection, as a toolbar needs to show it: every
// attribute is on, off, or mixed across the covered runs.
struct RichCharFormatState {
    enum class Tri { Off, On, Mixed };

    Tri bold = Tri::Off;
    Tri italic = Tri::Off;
    Tri underline = Tri::Off;
    Tri strikethrough = Tri::Off;
    Tri code = Tri::Off;
    Tri subscript = Tri::Off;
    Tri superscript = Tri::Off;

    // Empty when the covered runs disagree (or inherit).
    std::string fontFamily;
    bool fontFamilyMixed = false;
    float fontSizePt = 0.0f;
    bool fontSizeMixed = false;
    std::string color;
    bool colorMixed = false;
    std::string linkTarget;
    bool linkMixed = false;

    static bool IsOn(Tri t) { return t == Tri::On; }
};

// ===== SEARCH OPTIONS =====
// Case folding is ASCII, matching UltraCanvasTextArea's search: a
// case-insensitive search finds "Report" for "report" but not "STRASSE" for
// "Straße". Full Unicode case folding would need a folding table the framework
// does not carry yet.
struct RichFindOptions {
    bool caseSensitive = false;
    bool wholeWord = false;
    bool wrapAround = true;
};

// ===== AUTOCORRECT / AUTOFORMAT AS YOU TYPE =====
// What a word processor does to typed text. Each is applied as its own undo
// step (except the quotes, which change the character as it is typed), so
// Ctrl+Z right after takes back only the correction and keeps what was typed.
struct RichAutoFormatOptions {
    bool smartQuotes = true;    // " and ' become “ ” ‘ ’ by context
    bool dashes = true;         // "a--b" becomes "a—b", "a -- b" becomes "a – b"
    bool ellipsis = true;       // "..." becomes "…"
    bool symbols = true;        // (c) (r) (tm) -> <- => become © ® ™ → ← ⇒
    bool lists = true;          // "1. " / "a) " / "- " / "* " / "[ ] " opening a paragraph start a list
    bool headings = true;       // "# " .. "###### " opening a paragraph make it a heading
    bool rules = true;          // a paragraph of "---", "***" or "___" becomes a rule on Enter

    bool AnyEnabled() const {
        return smartQuotes || dashes || ellipsis || symbols || lists || headings || rules;
    }
};

// ===== THE EDITOR =====

// Owns a document plus a caret and selection over it, and is the only thing
// that mutates either. Every mutation goes through one undo step, so a caller
// never has to maintain an undo stack of its own.
class UCRichDocumentEditor {
public:
    UCRichDocumentEditor();
    explicit UCRichDocumentEditor(std::shared_ptr<UCRichDocument> document);

    // ===== DOCUMENT =====
    // Replacing the document resets the caret, the selection and the undo
    // history. A null document is replaced by a fresh one holding one empty
    // paragraph, so the editor is never in a state with no block to type into.
    void SetDocument(std::shared_ptr<UCRichDocument> document);
    const std::shared_ptr<UCRichDocument>& GetDocument() const { return doc; }
    int GetBlockCount() const { return static_cast<int>(doc->blocks.size()); }
    const RichDocBlock& GetBlock(int index) const { return doc->blocks[index]; }

    // ===== TEXT CONTAINERS =====
    // A block's own runs, or one table cell's. Everything that edits or
    // measures text goes through these rather than reaching into blocks
    // directly, which is what lets a caret sit inside a table cell.
    const std::vector<RichTextRun>* RunsAt(const RichDocPosition& pos) const;
    std::vector<RichTextRun>* MutableRunsAt(const RichDocPosition& pos);
    // Concatenated run text of the container `pos` addresses.
    std::string TextAt(const RichDocPosition& pos) const;
    int TextLengthAt(const RichDocPosition& pos) const;
    // True when the position addresses something editable (a text block's runs,
    // or a cell of a table).
    bool IsTextContainer(const RichDocPosition& pos) const;
    // The first/last position of the container `pos` is in.
    RichDocPosition ContainerStart(const RichDocPosition& pos) const;
    RichDocPosition ContainerEnd(const RichDocPosition& pos) const;
    // Walks containers in document order: cell to cell inside a table, then on
    // to the next block. False when there is no further container that way.
    bool NextContainer(RichDocPosition& pos) const;
    bool PreviousContainer(RichDocPosition& pos) const;
    // Every text container in the document, in document order.
    std::vector<RichDocPosition> AllContainers() const;
    void ForEachContainer(const std::function<void(const RichDocPosition&)>& fn) const;
    // Table geometry, for callers that move between cells (Tab, arrows).
    int TableRowCount(int blockIndex) const;
    int TableColumnCount(int blockIndex, int row) const;

    // Concatenated run text of a block — the string positions index into, and
    // the string the element lays out. Empty for Image / HorizontalRule /
    // PageBreak / Table blocks, which hold no editable inline text.
    std::string BlockText(int blockIndex) const;
    int BlockTextLength(int blockIndex) const;
    // True for blocks whose text can be edited and navigated through.
    bool IsTextBlock(int blockIndex) const;

    std::string GetPlainText() const { return doc->ToPlainText(); }
    std::string GetMarkdown() const { return doc->ToMarkdown(); }

    // ===== CARET AND SELECTION =====
    RichDocPosition GetCaret() const { return caret; }
    RichDocPosition GetAnchor() const { return anchor; }
    // Moves the caret; `extend` keeps the anchor so the selection grows.
    void SetCaret(const RichDocPosition& pos, bool extend = false);
    void SetSelection(const RichDocPosition& from, const RichDocPosition& to);
    void SelectAll();
    void SelectBlock(int blockIndex);
    void SelectWordAt(const RichDocPosition& pos);
    void ClearSelection();
    bool HasSelection() const { return caret != anchor; }
    RichDocRange GetSelectionRange() const { return RichDocRange(anchor, caret); }

    // ===== CELL SELECTION =====
    // When the anchor and the caret are in two different cells of one table,
    // the selection is not a run of text but a block of whole cells: the
    // smallest grid rectangle holding both cells, grown until no merged cell
    // sticks out of it. Deleting clears those cells, formatting applies to all
    // of their text, typing replaces them, copying copies them as a table, and
    // MergeSelectedCells() merges them.
    bool HasCellSelection() const;
    // The rectangle in grid rows and columns, inclusive. False without a cell
    // selection.
    bool GetCellSelectionRect(int& top, int& left, int& bottom, int& right) const;
    // The model cells in it, as {block, row, cellIndex, 0} positions in
    // document order.
    std::vector<RichDocPosition> SelectedCells() const;
    // Selects grid rectangle [top..bottom] x [left..right] of a table (grown
    // over merged cells as above).
    bool SelectCellRange(int blockIndex, int top, int left, int bottom, int right);
    // Merges every selected cell into the top-left one. False without a cell
    // selection, or when the rectangle cannot be one cell.
    bool MergeSelectedCells();

    // ===== NAVIGATION (positions only; no mutation) =====
    RichDocPosition ClampPosition(const RichDocPosition& pos) const;
    RichDocPosition NextCharacter(const RichDocPosition& pos) const;   // crosses blocks
    RichDocPosition PreviousCharacter(const RichDocPosition& pos) const;
    RichDocPosition NextWord(const RichDocPosition& pos) const;
    RichDocPosition PreviousWord(const RichDocPosition& pos) const;
    // Start/end of the container the caret is in — inside a table that is the
    // cell, which is what Home and End should reach there.
    RichDocPosition BlockStart(const RichDocPosition& pos) const { return ContainerStart(pos); }
    RichDocPosition BlockEnd(const RichDocPosition& pos) const;
    RichDocPosition DocumentStart() const;
    RichDocPosition DocumentEnd() const;
    // Word boundaries around `pos` (used by double-click and by SelectWordAt).
    RichDocRange WordAt(const RichDocPosition& pos) const;

    // ===== TEXT EDITING =====
    // All of these replace the selection when there is one, and leave the
    // caret after the inserted text.
    void InsertText(const std::string& utf8);
    void InsertLineBreak();          // soft break inside the paragraph (Shift+Enter)
    void SplitBlock();               // Enter: new paragraph, list item continues the list
    bool DeleteBackward();           // Backspace
    bool DeleteForward();            // Delete
    bool DeleteSelection();
    void DeleteRange(const RichDocRange& range);
    void ReplaceRange(const RichDocRange& range, const std::string& utf8);

    // ===== CHARACTER FORMATTING =====
    // With a selection, applies to it; with a collapsed caret, arms the format
    // for the next typed character (what a word processor does when you press
    // Bold and keep typing).
    void ApplyCharFormat(const RichCharFormatDelta& delta);
    void ApplyCharFormatToRange(const RichDocRange& range, const RichCharFormatDelta& delta);
    void ToggleBold();
    void ToggleItalic();
    void ToggleUnderline();
    void ToggleStrikethrough();
    void ToggleCode();
    void ToggleSubscript();
    void ToggleSuperscript();
    void SetFontFamily(const std::string& family);
    void SetFontSize(float pt);
    void SetTextColor(const std::string& hexColor);
    void SetLink(const std::string& target);
    void ClearFormatting();          // back to plain text, keeping the characters

    // Merged formatting of the selection (or of the caret's context when the
    // selection is empty), including anything armed but not yet typed.
    RichCharFormatState GetFormatState() const;
    // The run that owns `pos`, or a default-constructed run when there is none.
    RichTextRun FormatAt(const RichDocPosition& pos) const;
    // Format armed for the next insertion, if any.
    bool HasPendingFormat() const { return pendingFormatValid; }

    // ===== BLOCK (PARAGRAPH) FORMATTING =====
    // Each applies to every block the selection touches.
    void SetBlockType(RichBlockType type, int headingLevel = 0);
    void SetHeadingLevel(int level);          // 0 = plain paragraph
    void SetAlignment(RichTextAlign align);
    void SetListStyle(bool ordered);          // turns blocks into list items
    void ToggleList(bool ordered);            // ... or back into paragraphs
    void IndentList();                        // deeper nesting (list blocks only)
    void OutdentList();
    void ToggleBlockQuote();
    void ToggleCodeBlock(const std::string& language = "");
    // Check lists: turns the selected blocks into unticked check list items,
    // or, when they all are check list items already, back into paragraphs.
    void ToggleCheckList();
    // Ticks or unticks one check list item. False when it is not one.
    bool ToggleChecked(int blockIndex);

    // ===== NAMED STYLES =====
    // The document's styles; a document without any gets
    // UCRichDocument::DefaultStyles() the first time one is applied.
    std::vector<RichStyle> GetStyles() const;
    // Gives the selected paragraphs style `id`: its paragraph and character
    // properties are applied (a heading style makes them headings), and the
    // properties the old style set that the new one does not are taken back
    // where the text still has them. One undo step.
    bool ApplyParagraphStyle(const std::string& id);
    // Gives the selected text character style `id` ("" removes it).
    bool ApplyCharacterStyle(const std::string& id);
    // Adds a style, or changes one: every paragraph and run that has it (or
    // a style based on it) follows, except in a property formatted directly
    // (one whose value is not what the style used to give it). Styles and
    // text change as one undo step.
    bool UpdateStyle(const RichStyle& style);
    // Removes a style; what had it takes the style it was based on.
    bool DeleteStyle(const std::string& id);
    // A new style from the paragraph at the caret: its alignment, indents and
    // spacing, and its first run's character formatting.
    RichStyle StyleFromCaret(const std::string& id, const std::string& name) const;
    // The caret paragraph's style ("Normal" when it states none; a heading
    // without one reads as its heading style).
    std::string CurrentParagraphStyle() const;
    std::string CurrentCharacterStyle() const;

    // ===== STRUCTURE =====
    void InsertHorizontalRule();
    void InsertPageBreak();
    // Adds the bytes to the document's media store and inserts an image block
    // at the caret. Returns the block index, or -1 when the data is empty.
    int InsertImage(const std::string& name, const std::string& mimeType,
                    const std::vector<uint8_t>& data,
                    const std::string& altText = "");
    // Inserts the picture INTO the line at the caret, as a run, rather than as
    // a paragraph of its own. Returns the media index, or -1.
    int InsertInlineImage(const std::string& name, const std::string& mimeType,
                          const std::vector<uint8_t>& data,
                          const std::string& altText = "");
    void DeleteBlock(int blockIndex);

    // ===== PICTURES =====
    // A picture is addressed by where it sits: an Image block by {block, 0},
    // a picture in the line by the offset of its placeholder in its
    // container. These return false when nothing is there.
    bool IsImageAt(const RichDocPosition& image) const;
    // Its size in points (0 = its own pixel size), its alt text and media.
    bool GetImageInfo(const RichDocPosition& image, float& widthPt, float& heightPt,
                      std::string& altText, int& mediaIndex) const;
    // Resizes it, one undo step. Sizes <= 0 are refused.
    bool SetImageSize(const RichDocPosition& image, float widthPt, float heightPt);
    // Sets the description a screen reader or a text export gives for it.
    bool SetImageAltText(const RichDocPosition& image, const std::string& altText);
    // Inserts a footnote or endnote reference at the caret, with a new note
    // holding one empty paragraph. Returns the note's index, or -1.
    int InsertNote(RichNote::Kind kind);
    // The note a reference at `pos` (the caret, typically) points to, or -1:
    // the reference just before or after the position.
    int NoteAt(const RichDocPosition& pos) const;

    // ===== SECTIONS =====
    // A section break before the caret's paragraph (splitting it at the
    // caret first when the caret is inside it): what follows is a new
    // section, like the one it was in, starting on a new page or not.
    bool InsertSectionBreak(bool newPage);
    // The caret's section in `columns` columns, `gapPt` apart.
    bool SetSectionColumns(int columns, float gapPt = 36.0f);
    RichSectionSetup CurrentSection() const;

    // ===== TRACKED CHANGES =====
    // With tracking on, typed and pasted text is marked inserted, and deleted
    // text stays, marked deleted, until the change is accepted (deleted text
    // goes, inserted text stays) or rejected (the other way round). Deleting
    // text that is itself a tracked insertion removes it outright. Paragraph
    // breaks, tables and pictures are edited untracked.
    void SetTrackChanges(bool enabled);
    bool IsTrackingChanges() const { return trackChanges; }
    void SetRevisionAuthor(const std::string& author, const std::string& date = "");
    bool AcceptAllChanges();
    bool RejectAllChanges();
    // The change at a position (the run it is in), or every change in the
    // selection when there is one.
    bool AcceptChangeAt(const RichDocPosition& pos);
    bool RejectChangeAt(const RichDocPosition& pos);
    // The start of the next tracked change after `pos` (wrapping), or false.
    bool NextChange(const RichDocPosition& pos, RichDocRange& out) const;

    // ===== COMMENTS =====
    // A comment on the selection (the word at the caret when nothing is
    // selected). Returns its index in UCRichDocument::comments, or -1.
    int AddComment(const std::string& text, const std::string& author = "", const std::string& date = "");
    // Takes the comment off its text (one undo step); it is then not shown or
    // saved.
    bool RemoveComment(int index);
    bool SetCommentText(int index, const std::string& text);
    bool SetCommentResolved(int index, bool resolved);
    // The comments the text at a position is under.
    std::vector<int> CommentsAt(const RichDocPosition& pos) const;
    // The span a comment covers (first to last covered character).
    bool CommentRange(int index, RichDocRange& out) const;

    // ===== BOOKMARKS, CROSS-REFERENCES, CAPTIONS, CONTENTS =====
    // A bookmark on the caret's paragraph (the table's, in a cell). False
    // when the name is empty or taken.
    bool AddBookmark(const std::string& name);
    bool RemoveBookmark(const std::string& name);
    // A field at the caret showing the bookmark's text (Field::Reference:
    // "Figure 3" for a caption) or its page (Field::PageReference).
    bool InsertCrossReference(const std::string& bookmark, RichTextRun::Field kind);
    // A numbered caption - "Figure 2: text", the number a Sequence field - as
    // a paragraph after the caret's block (a picture's or a table's), in the
    // Caption style when the document has one. It is bookmarked, so it can be
    // referred to; the bookmark's name is returned ("" on failure).
    std::string InsertCaption(const std::string& label, const std::string& text);
    // A table of contents of the headings of level 1..maxLevel, before the
    // caret's paragraph (in its place when it is empty); and the same rebuilt
    // where it is. Page numbers are PageReference fields a paged view fills.
    bool InsertTableOfContents(int maxLevel = 3);
    bool UpdateTableOfContents(int maxLevel = 3);

    // Inserts a page number (Field::PageNumber) or page count field at the
    // caret. Its text is a placeholder until a paged view numbers it.
    bool InsertField(RichTextRun::Field field);

    // ===== TABLES =====
    // Cells are stored sparsely (see RichTableGrid), so a cell's index within
    // its row is not its column. Positions address cells by model index, which
    // is what a caret carries; these take GRID columns wherever the argument
    // names one, because a grid column is what the user is pointing at.
    RichTableGrid TableGrid(int blockIndex) const;
    // Where the caret's cell starts in the grid. False when the caret is not
    // inside a table cell.
    bool CaretGridPosition(int& outRow, int& outColumn) const;

    // Inserts a rows x columns table at the caret and leaves the caret in its
    // first cell. Returns the block index, or -1 for a degenerate size.
    int InsertTable(int rows, int columns, bool headerRow = false);

    // Each of these keeps the grid rectangular: a span reaching across the
    // insertion point grows instead of being cut in two, and a span reaching
    // into a deleted row or column shrinks (or, where the span started there,
    // moves to the row below) instead of leaving a hole.
    bool InsertTableRow(int blockIndex, int row, bool below);
    bool InsertTableColumn(int blockIndex, int gridColumn, bool right);
    // Deleting the last row or the last column deletes the table itself: a
    // table with no cells has nothing to type into and no way back.
    bool DeleteTableRow(int blockIndex, int row);
    bool DeleteTableColumn(int blockIndex, int gridColumn);

    // Grows the cell at (row, cellIndex) by `extraColumns` to the right and
    // `extraRows` downwards, absorbing the cells it covers. Their text is
    // appended to the surviving cell rather than dropped. Refuses a rectangle
    // that would cut an existing span in half, since that cannot be stored.
    bool MergeTableCells(int blockIndex, int row, int cellIndex,
                         int extraColumns, int extraRows);
    // Back to 1x1, with fresh empty cells filling the slots it gives up.
    bool SplitTableCell(int blockIndex, int row, int cellIndex);

    // ===== AUTOFORMAT =====
    // Off by default in the editing core (a programmatic InsertText must
    // insert exactly what it is given); the element turns it on for typing.
    void SetAutoFormatOptions(const RichAutoFormatOptions& options) { autoFormat = options; }
    const RichAutoFormatOptions& GetAutoFormatOptions() const { return autoFormat; }
    void SetAutoFormatEnabled(bool enabled) { autoFormatEnabled = enabled; }
    bool IsAutoFormatEnabled() const { return autoFormatEnabled; }
    // Typing, as a keyboard delivers it: the text is inserted (with smart
    // quotes applied), then any correction the text now ends in is made as a
    // separate undo step. Returns true when a correction was made.
    bool TypeText(const std::string& utf8);
    // Enter, as typed: a paragraph of "---" becomes a rule first. Then splits.
    void TypeEnter();
    // The corrections alone, for callers that insert text themselves.
    std::string ApplySmartQuotes(const std::string& typed) const;
    bool AutoFormatBeforeCaret();

    // ===== CLIPBOARD SUPPORT =====
    // Blocks covered by `range`, trimmed to the selected text — the payload of
    // a rich copy. Media referenced by an image block is NOT copied; callers
    // pasting into another document must carry it over themselves.
    std::vector<RichDocBlock> ExtractRange(const RichDocRange& range) const;
    std::string RangeToPlainText(const RichDocRange& range) const;
    // Inserts blocks at the caret (replacing the selection). The first pasted
    // block merges into the current paragraph and the last one keeps the text
    // that followed the caret, which is what makes pasting mid-sentence work.
    void InsertBlocks(const std::vector<RichDocBlock>& blocks);
    // Drag and drop: moves (or, with `copy`, duplicates) the text of `range`
    // to `target`, as one undo step, and leaves the moved text selected. False
    // when the target is inside the range, or the range is a block of cells.
    bool MoveRange(const RichDocRange& range, const RichDocPosition& target, bool copy = false);

    // ===== SEARCH =====
    // Matches are found in block text, so a match never spans a block boundary
    // — which is also what makes every match safe to replace independently.
    // Blocks holding no editable inline text (images, rules, page breaks and,
    // for now, tables) are skipped: they are exactly the blocks BlockText()
    // returns empty for.
    //
    // Searches from `from` and returns the first match at or after it
    // (at or before it, searching backwards). With wrapAround the search
    // continues from the other end of the document, so it always terminates.
    bool Find(const std::string& needle, const RichDocPosition& from,
              bool backwards, const RichFindOptions& options,
              RichDocRange& outMatch) const;
    // Every match in the document, in document order.
    std::vector<RichDocRange> FindAll(const std::string& needle,
                                      const RichFindOptions& options) const;
    // Replaces every match in ONE undo step, so Ctrl+Z takes back the whole
    // replace rather than one word per press. Returns how many were replaced.
    int ReplaceAll(const std::string& needle, const std::string& replacement,
                   const RichFindOptions& options);

    // ===== WHAT THE LAST MUTATION TOUCHED =====
    // The blocks the most recent change replaced, as an inclusive range in the
    // document as it stands now; both -1 before anything has been changed. A
    // view that caches a layout per block needs this, because an edit that
    // leaves the caret where it was is otherwise indistinguishable from no
    // edit at all - and would go on showing the old text.
    void GetLastChangedBlocks(int& outFirst, int& outLast) const {
        outFirst = lastChangedFirst;
        outLast = lastChangedLast;
    }

    // ===== UNDO / REDO =====
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    bool Undo();
    bool Redo();
    void ClearUndoHistory();
    // How many steps Undo can go back (default 200). The oldest steps are
    // dropped past it; 0 = no limit. A step costs the blocks it touched, so a
    // long history of typing is cheap, one of whole-document replaces is not.
    void SetMaxUndoSteps(size_t steps);
    size_t GetMaxUndoSteps() const { return maxUndoSteps; }
    // Ends the current typing run, so the next keystroke starts a new undo
    // step. Call it when the caret moves by any means other than typing.
    void BreakUndoCoalescing() { coalescing = false; }

    // ===== STATE =====
    bool IsModified() const { return modified; }
    void SetModified(bool m) { modified = m; }
    // Fired after every mutation (including undo/redo).
    std::function<void()> onChanged;
    // Fired after every caret or selection change.
    std::function<void()> onSelectionChanged;

    // ===== TEXT OF A RUN LIST =====
    // The concatenation positions index into: each run's text, preceded by a
    // '\n' for every run carrying lineBreakBefore. Public because the element
    // lays out table cells, whose runs are not a block's own.
    static std::string RunsText(const std::vector<RichTextRun>& runs);

    // ===== UTF-8 HELPERS (byte offsets into a block's text) =====
    static int NextCharOffset(const std::string& text, int byteOffset);
    static int PreviousCharOffset(const std::string& text, int byteOffset);
    // Snaps an arbitrary byte offset to the start of the character holding it.
    static int SnapToCharStart(const std::string& text, int byteOffset);

private:
    // One undo step replaces the blocks [firstBlock, firstBlock + before.size())
    // with `after`. Every operation — typing, splitting, formatting, pasting —
    // is expressible this way, so there is a single apply/revert path, and the
    // cost of a step is the blocks it touched rather than the whole document
    // (in particular the media store is never copied).
    struct UndoStep {
        int firstBlock = 0;
        std::vector<RichDocBlock> before;
        std::vector<RichDocBlock> after;
        RichDocPosition caretBefore, anchorBefore;
        RichDocPosition caretAfter, anchorAfter;
        bool typing = false;        // eligible to absorb the next keystroke
        // Set when the step also changed the document's named styles.
        bool stylesChanged = false;
        std::vector<RichStyle> stylesBefore, stylesAfter;
    };

    // Records the blocks about to change, and on close records what they
    // became. Every mutating method opens an edit, mutates (leaving the caret
    // where it should end up), and closes it; nothing else touches
    // doc->blocks. Blocks after the edited span are untouched by construction,
    // so the span's new length follows from the preserved tail length — which
    // is how one scope covers edits that add or remove whole blocks.
    struct EditScope {
        UCRichDocumentEditor& ed;
        int firstBlock = 0;
        int tailCount = 0;
        std::vector<RichDocBlock> before;
        RichDocPosition caretBefore, anchorBefore;
        bool typing = false;
        bool captureStyles = false;
        std::vector<RichStyle> stylesBefore;
        EditScope(UCRichDocumentEditor& e, int first, int count, bool isTyping = false);
        ~EditScope();
        // The step also records the named styles (a style edit).
        void CaptureStyles() { captureStyles = true; stylesBefore = ed.doc->styles; }
    };
    friend struct EditScope;

    // Mutations without their own undo step, so one public operation that
    // deletes and then inserts still undoes as a single step.
    void DeleteRangeInternal(const RichDocRange& range);
    void InsertTextInternal(const std::string& utf8);
    RichDocPosition ClampToAnchorContainer(const RichDocPosition& pos) const;
    // Clears the text of every selected cell; the caret goes to the start of
    // the top-left one. No undo step of its own.
    void ClearSelectedCellsInternal();
    void InsertBlocksIntoCellInternal(const std::vector<RichDocBlock>& blocks);
    void InsertBlocksInternal(const std::vector<RichDocBlock>& blocks);
    // Grid rectangle of a cell selection between two cell positions.
    bool CellRectBetween(const RichDocPosition& a, const RichDocPosition& b,
                         int& top, int& left, int& bottom, int& right) const;
    void InsertLineBreakInternal();
    void ReplaceRangeInternal(const RichDocRange& range, const std::string& utf8);
    void ApplyCharFormatToRangeInternal(const RichDocRange& range,
                                        const RichCharFormatDelta& delta);
    void SplitBlockInternal();
    void InsertStructuralBlock(RichBlockType type);

    void CommitStep(UndoStep step);
    // Puts `id`'s properties onto a block and its runs, taking back those of
    // `previousId` it still carries. force: the new style's properties are set
    // everywhere (applying a style); otherwise only where the text still had
    // the old value (a style being changed).
    void RestyleBlock(RichDocBlock& block, const RichStyle& before, const RichStyle& after, bool force) const;
    void EnsureStyles();
    void NotifyChanged();
    void NotifySelectionChanged();
    void EnsureNotEmpty();

    // Run-level plumbing. Offsets are bytes into the block's concatenated text.
    // Splits runs so a boundary exists exactly at `byteOffset`; returns the
    // index of the run starting there (runs.size() when it is at the end).
    static int SplitRunAt(std::vector<RichTextRun>& runs, int byteOffset);
    static void CoalesceRuns(std::vector<RichTextRun>& runs);
    // Removes [startByte, endByte) from the runs.
    static void EraseRunRange(std::vector<RichTextRun>& runs, int startByte, int endByte);
    // Inserts text at `byteOffset` carrying `format`; when `format` is null the
    // text inherits the formatting of the run it lands in.
    // With tracking on, the text is a tracked insertion.
    void InsertIntoRuns(std::vector<RichTextRun>& runs, int byteOffset,
                        const std::string& text, const RichTextRun* format,
                        bool lineBreakBefore = false);
    static std::vector<RichTextRun> SliceRuns(const std::vector<RichTextRun>& runs,
                                              int startByte, int endByte);
    // The run covering `byteOffset` (preferring the one to its left, which is
    // what a caret inherits when you keep typing).
    static const RichTextRun* RunAtOffset(const std::vector<RichTextRun>& runs, int byteOffset);

    // Blocks the selection touches, as an inclusive index range.
    void SelectedBlockRange(int& firstBlock, int& lastBlock) const;

    std::shared_ptr<UCRichDocument> doc;
    RichDocPosition caret;
    RichDocPosition anchor;

    // Set by every mutation, including undo and redo; read by the view.
    int lastChangedFirst = -1;
    int lastChangedLast = -1;
    void NoteChangedBlocks(int first, int count);

    std::vector<UndoStep> undoStack;
    std::vector<UndoStep> redoStack;
    size_t maxUndoSteps = 200;
    bool coalescing = false;        // the last step was typing and can absorb more
    bool applyingUndo = false;      // suppresses step recording while reverting
    bool modified = false;

    RichAutoFormatOptions autoFormat;
    bool autoFormatEnabled = false;

    // Format armed by a toolbar press at a collapsed caret.
    RichTextRun pendingFormat;
    bool pendingFormatValid = false;

    // Tracked changes.
    bool trackChanges = false;
    bool trackBackward = false;       // Backspace: the caret stays before what it marked
    std::string revisionAuthor;
    std::string revisionDate;
    int currentRevision = -1;         // this session's UCRichDocument::revisions entry
    int CurrentRevision();
    // Marks `range` deleted (tracked); true when it handled the range.
    bool MarkRangeDeleted(const RichDocRange& range);
    // Applies accept (true) or reject to the changes in [from, to) of every
    // run list in blocks first..last (all runs of middle blocks).
    void ResolveChanges(const RichDocRange& range, bool accept, bool wholeDocument);
};

} // namespace UltraCanvas
