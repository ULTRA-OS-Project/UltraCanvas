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

    bool IsEmpty() const {
        return !setBold && !setItalic && !setUnderline && !setStrikethrough && !setCode
            && !setSubscript && !setSuperscript && !setFontFamily && !setFontSize
            && !setColor && !setLink;
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

    // ===== STRUCTURE =====
    void InsertHorizontalRule();
    void InsertPageBreak();
    // Adds the bytes to the document's media store and inserts an image block
    // at the caret. Returns the block index, or -1 when the data is empty.
    int InsertImage(const std::string& name, const std::string& mimeType,
                    const std::vector<uint8_t>& data,
                    const std::string& altText = "");
    void DeleteBlock(int blockIndex);

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

    // ===== UNDO / REDO =====
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    bool Undo();
    bool Redo();
    void ClearUndoHistory();
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
        EditScope(UCRichDocumentEditor& e, int first, int count, bool isTyping = false);
        ~EditScope();
    };
    friend struct EditScope;

    // Mutations without their own undo step, so one public operation that
    // deletes and then inserts still undoes as a single step.
    void DeleteRangeInternal(const RichDocRange& range);
    void InsertTextInternal(const std::string& utf8);
    RichDocPosition ClampToAnchorContainer(const RichDocPosition& pos) const;
    void InsertLineBreakInternal();
    void ReplaceRangeInternal(const RichDocRange& range, const std::string& utf8);
    void ApplyCharFormatToRangeInternal(const RichDocRange& range,
                                        const RichCharFormatDelta& delta);
    void SplitBlockInternal();
    void InsertStructuralBlock(RichBlockType type);

    void CommitStep(UndoStep step);
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
    static void InsertIntoRuns(std::vector<RichTextRun>& runs, int byteOffset,
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

    std::vector<UndoStep> undoStack;
    std::vector<UndoStep> redoStack;
    size_t maxUndoSteps = 200;
    bool coalescing = false;        // the last step was typing and can absorb more
    bool applyingUndo = false;      // suppresses step recording while reverting
    bool modified = false;

    // Format armed by a toolbar press at a collapsed caret.
    RichTextRun pendingFormat;
    bool pendingFormatValid = false;
};

} // namespace UltraCanvas
