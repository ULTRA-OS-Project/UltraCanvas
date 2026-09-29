// core/UltraCanvasRichDocumentEditor.cpp
// The editing core over UCRichDocument. See UltraCanvasRichDocumentEditor.h
// for the position model and the undo strategy.
//
// A block's text is the concatenation of its runs, with one '\n' byte
// contributed by every run carrying lineBreakBefore (a hard break inside the
// paragraph). That byte belongs to the run it precedes, so a run's byte span
// is [start, start + (lineBreakBefore ? 1 : 0) + text.size()).
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasRichDocumentEditor.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

// A list item that moved to another level (or changed between bullets and
// numbers) takes that level's label format from the same list - the nearest
// item of that level and kind above it, else below it - so "a)" items stay
// "a)" when one more joins them. With no such item the view's default applies.
void AdoptListLevelFormat(std::vector<RichDocBlock>& blocks, size_t index) {
    RichDocBlock& item = blocks[index];
    auto matches = [&](const RichDocBlock& other) {
        return other.listLevel == item.listLevel && other.orderedList == item.orderedList;
    };
    const RichDocBlock* model = nullptr;
    for (size_t i = index; i-- > 0 && blocks[i].type == RichBlockType::ListItem;) {
        if (matches(blocks[i])) { model = &blocks[i]; break; }
    }
    for (size_t i = index + 1; !model && i < blocks.size() && blocks[i].type == RichBlockType::ListItem; ++i) {
        if (matches(blocks[i])) model = &blocks[i];
    }
    item.numberFormat = model ? model->numberFormat : RichNumberFormat::Decimal;
    item.numberTemplate = model ? model->numberTemplate : std::string();
    item.bulletText = model ? model->bulletText : std::string();
}

bool IsContinuationByte(unsigned char c) { return (c & 0xC0) == 0x80; }

// Word characters for double-click and Ctrl+Arrow. Every byte above ASCII
// counts as a word byte, so accented and CJK text behaves like a word rather
// than like punctuation.
bool IsWordByte(unsigned char c) {
    if (c >= 0x80) return true;
    return std::isalnum(c) != 0 || c == '_';
}

bool IsTextBlockType(RichBlockType type) {
    switch (type) {
        case RichBlockType::Paragraph:
        case RichBlockType::Heading:
        case RichBlockType::ListItem:
        case RichBlockType::CodeBlock:
        case RichBlockType::BlockQuote:
        case RichBlockType::MathBlock:
            return true;
        default:
            return false;   // Table, Image, HorizontalRule, PageBreak
    }
}

// A block's style id as the style system reads it: none is Normal, and a
// heading that names none is its level's heading style.
std::string EffectiveStyleId(const RichDocBlock& block) {
    if (!block.styleId.empty()) return block.styleId;
    if (block.type == RichBlockType::Heading) return "Heading" + std::to_string(std::clamp(block.headingLevel, 1, 6));
    return "Normal";
}

// Byte length a run contributes to its block's text.
int RunSpan(const RichTextRun& run) {
    return (run.lineBreakBefore ? 1 : 0) + static_cast<int>(run.text.size());
}

// Clears what makes a run an object rather than formatted text: a picture's
// media, a field's kind.
void StripObjectIdentity(RichTextRun& run) {
    run.mediaIndex = -1;
    run.imageWidthPt = run.imageHeightPt = 0.0f;
    run.imageAltText.clear();
    run.field = RichTextRun::Field::Plain;
    run.fieldArgument.clear();
    run.change = RichTextRun::Change::Unchanged;
    run.revision = -1;
    // A note mark is raised because it is a mark; the text next to it is not.
    if (run.noteIndex >= 0) run.superscript = false;
    run.noteIndex = -1;
}

} // namespace

// ===== RichCharFormatDelta =====

void RichCharFormatDelta::ApplyTo(RichTextRun& run) const {
    if (setBold)          run.bold = bold;
    if (setItalic)        run.italic = italic;
    if (setUnderline)     run.underline = underline;
    if (setStrikethrough) run.strikethrough = strikethrough;
    if (setCode)          run.code = code;
    if (setSubscript) {
        run.subscript = subscript;
        if (subscript) run.superscript = false;
    }
    if (setSuperscript) {
        run.superscript = superscript;
        if (superscript) run.subscript = false;
    }
    if (setFontFamily) run.fontFamily = fontFamily;
    if (setFontSize)   run.fontSizePt = fontSizePt;
    if (setColor)      run.color = color;
    if (setLink)       run.linkTarget = linkTarget;
    if (addComment >= 0 && std::find(run.commentIds.begin(), run.commentIds.end(), addComment) == run.commentIds.end()) {
        run.commentIds.push_back(addComment);
    }
    if (removeComment >= 0) {
        run.commentIds.erase(std::remove(run.commentIds.begin(), run.commentIds.end(), removeComment),
                             run.commentIds.end());
    }
}

// ===== UTF-8 HELPERS =====

int UCRichDocumentEditor::NextCharOffset(const std::string& text, int byteOffset) {
    int n = static_cast<int>(text.size());
    if (byteOffset >= n) return n;
    int i = byteOffset + 1;
    while (i < n && IsContinuationByte(static_cast<unsigned char>(text[i]))) i++;
    return i;
}

int UCRichDocumentEditor::PreviousCharOffset(const std::string& text, int byteOffset) {
    if (byteOffset <= 0) return 0;
    int i = std::min(byteOffset, static_cast<int>(text.size())) - 1;
    while (i > 0 && IsContinuationByte(static_cast<unsigned char>(text[i]))) i--;
    return i;
}

int UCRichDocumentEditor::SnapToCharStart(const std::string& text, int byteOffset) {
    int n = static_cast<int>(text.size());
    if (byteOffset <= 0) return 0;
    if (byteOffset >= n) return n;
    int i = byteOffset;
    while (i > 0 && IsContinuationByte(static_cast<unsigned char>(text[i]))) i--;
    return i;
}

// ===== RUN PLUMBING =====

std::string UCRichDocumentEditor::RunsText(const std::vector<RichTextRun>& runs) {
    std::string out;
    for (const auto& run : runs) {
        if (run.lineBreakBefore) out += '\n';
        out += run.text;
    }
    return out;
}

int UCRichDocumentEditor::SplitRunAt(std::vector<RichTextRun>& runs, int byteOffset) {
    if (byteOffset <= 0) return 0;
    int pos = 0;
    for (size_t i = 0; i < runs.size(); i++) {
        int span = RunSpan(runs[i]);
        if (byteOffset == pos) return static_cast<int>(i);
        if (byteOffset < pos + span) {
            // Inside this run. Offset 0 of a break-carrying run sits before the
            // '\n', which is the boundary handled above, so `local` >= 1 there.
            int local = byteOffset - pos;
            int textLocal = runs[i].lineBreakBefore ? local - 1 : local;
            textLocal = std::max(0, std::min(textLocal, static_cast<int>(runs[i].text.size())));

            RichTextRun tail = runs[i];
            tail.text = runs[i].text.substr(static_cast<size_t>(textLocal));
            tail.lineBreakBefore = false;
            runs[i].text = runs[i].text.substr(0, static_cast<size_t>(textLocal));
            runs.insert(runs.begin() + static_cast<long>(i) + 1, tail);
            return static_cast<int>(i) + 1;
        }
        pos += span;
    }
    return static_cast<int>(runs.size());
}

void UCRichDocumentEditor::CoalesceRuns(std::vector<RichTextRun>& runs) {
    // Drop empty runs that carry nothing at all (an empty run with a break
    // still contributes its '\n' and must survive).
    for (size_t i = runs.size(); i-- > 0;) {
        if (runs[i].text.empty() && !runs[i].lineBreakBefore && runs.size() > 1) {
            runs.erase(runs.begin() + static_cast<long>(i));
        }
    }
    for (size_t i = 0; i + 1 < runs.size();) {
        // A break on the following run is a real boundary; merging would move
        // the '\n', so only break-free neighbours merge.
        if (!runs[i + 1].lineBreakBefore && runs[i].HasSameFormatting(runs[i + 1])) {
            runs[i].text += runs[i + 1].text;
            runs.erase(runs.begin() + static_cast<long>(i) + 1);
        } else {
            i++;
        }
    }
}

void UCRichDocumentEditor::EraseRunRange(std::vector<RichTextRun>& runs, int startByte, int endByte) {
    if (endByte <= startByte) return;
    // Split at the START first: splitting at the end first would leave that
    // index stale the moment the start split inserts a run before it. The end
    // split lands at or after the start boundary, so it cannot move startIdx.
    int startIdx = SplitRunAt(runs, startByte);
    int endIdx = SplitRunAt(runs, endByte);
    startIdx = std::min(startIdx, static_cast<int>(runs.size()));
    endIdx = std::min(std::max(endIdx, startIdx), static_cast<int>(runs.size()));
    runs.erase(runs.begin() + startIdx, runs.begin() + endIdx);
    CoalesceRuns(runs);
}

const RichTextRun* UCRichDocumentEditor::RunAtOffset(const std::vector<RichTextRun>& runs,
                                                     int byteOffset) {
    if (runs.empty()) return nullptr;
    int pos = 0;
    const RichTextRun* previous = nullptr;
    for (const auto& run : runs) {
        int span = RunSpan(run);
        if (byteOffset < pos + span) {
            // At a run boundary the caret inherits the run to its left, which
            // is what continuing to type after a bold word should do.
            if (byteOffset == pos && previous) return previous;
            return &run;
        }
        if (span > 0) previous = &run;
        pos += span;
    }
    return previous ? previous : &runs.back();
}

void UCRichDocumentEditor::InsertIntoRuns(std::vector<RichTextRun>& runs, int byteOffset,
                                          const std::string& text, const RichTextRun* format,
                                          bool lineBreakBefore) {
    RichTextRun inserted;
    if (format) {
        inserted = *format;
    } else if (const RichTextRun* source = RunAtOffset(runs, byteOffset)) {
        inserted = *source;
        // Typed text takes its neighbour's formatting, not what it IS: text
        // typed after a picture is not a picture, and after a page number
        // field it is not part of the number (which the next layout would
        // overwrite).
        StripObjectIdentity(inserted);
    }
    inserted.text = text;
    inserted.lineBreakBefore = lineBreakBefore;
    // Typed text is a change of its own, or none - never its neighbour's.
    if (trackChanges) {
        inserted.change = RichTextRun::Change::Inserted;
        inserted.revision = CurrentRevision();
    } else {
        inserted.change = RichTextRun::Change::Unchanged;
        inserted.revision = -1;
    }

    int idx = SplitRunAt(runs, byteOffset);
    idx = std::min(idx, static_cast<int>(runs.size()));
    runs.insert(runs.begin() + idx, inserted);
    CoalesceRuns(runs);
}

std::vector<RichTextRun> UCRichDocumentEditor::SliceRuns(const std::vector<RichTextRun>& runs,
                                                         int startByte, int endByte) {
    std::vector<RichTextRun> copy = runs;
    int total = static_cast<int>(RunsText(copy).size());
    startByte = std::max(0, std::min(startByte, total));
    endByte = std::max(startByte, std::min(endByte, total));
    EraseRunRange(copy, endByte, total);
    EraseRunRange(copy, 0, startByte);
    if (!copy.empty()) copy.front().lineBreakBefore = false;
    return copy;
}

// ===== EDIT SCOPE =====

UCRichDocumentEditor::EditScope::EditScope(UCRichDocumentEditor& e, int first, int count,
                                           bool isTyping)
    : ed(e), typing(isTyping) {
    int total = static_cast<int>(ed.doc->blocks.size());
    firstBlock = std::max(0, std::min(first, total));
    int span = std::max(0, std::min(count, total - firstBlock));
    tailCount = total - firstBlock - span;
    before.assign(ed.doc->blocks.begin() + firstBlock,
                  ed.doc->blocks.begin() + firstBlock + span);
    caretBefore = ed.caret;
    anchorBefore = ed.anchor;
}

UCRichDocumentEditor::EditScope::~EditScope() {
    int total = static_cast<int>(ed.doc->blocks.size());
    int newCount = std::max(0, std::min(total - firstBlock - tailCount, total - firstBlock));

    UndoStep step;
    step.firstBlock = firstBlock;
    step.before = std::move(before);
    step.after.assign(ed.doc->blocks.begin() + firstBlock,
                      ed.doc->blocks.begin() + firstBlock + newCount);
    step.caretBefore = caretBefore;
    step.anchorBefore = anchorBefore;
    step.caretAfter = ed.caret;
    step.anchorAfter = ed.anchor;
    step.typing = typing;
    if (captureStyles) {
        step.stylesChanged = true;
        step.stylesBefore = std::move(stylesBefore);
        step.stylesAfter = ed.doc->styles;
    }
    ed.CommitStep(std::move(step));
}

// ===== CONSTRUCTION =====

UCRichDocumentEditor::UCRichDocumentEditor() {
    SetDocument(nullptr);
}

UCRichDocumentEditor::UCRichDocumentEditor(std::shared_ptr<UCRichDocument> document) {
    SetDocument(std::move(document));
}

void UCRichDocumentEditor::SetDocument(std::shared_ptr<UCRichDocument> document) {
    doc = document ? std::move(document) : std::make_shared<UCRichDocument>();
    EnsureNotEmpty();
    caret = RichDocPosition(0, 0);
    anchor = caret;
    undoStack.clear();
    redoStack.clear();
    coalescing = false;
    pendingFormatValid = false;
    modified = false;
    NotifyChanged();
    NotifySelectionChanged();
}

void UCRichDocumentEditor::EnsureNotEmpty() {
    if (doc->blocks.empty()) {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        doc->blocks.push_back(block);
    }
}

// ===== BLOCK TEXT =====

// ===== TEXT CONTAINERS =====

const std::vector<RichTextRun>* UCRichDocumentEditor::RunsAt(const RichDocPosition& pos) const {
    if (pos.blockIndex < 0 || pos.blockIndex >= GetBlockCount()) return nullptr;
    const RichDocBlock& block = doc->blocks[static_cast<size_t>(pos.blockIndex)];

    if (!pos.InCell()) {
        return IsTextBlockType(block.type) ? &block.runs : nullptr;
    }
    if (block.type != RichBlockType::Table) return nullptr;
    if (pos.cellRow >= static_cast<int>(block.tableRows.size())) return nullptr;
    const RichTableRow& row = block.tableRows[static_cast<size_t>(pos.cellRow)];
    if (pos.cellColumn >= static_cast<int>(row.cells.size())) return nullptr;
    return &row.cells[static_cast<size_t>(pos.cellColumn)].runs;
}

std::vector<RichTextRun>* UCRichDocumentEditor::MutableRunsAt(const RichDocPosition& pos) {
    // Same lookup; const_cast keeps the two in step rather than duplicating the
    // bounds checks, which is where a divergence would hurt.
    return const_cast<std::vector<RichTextRun>*>(
        static_cast<const UCRichDocumentEditor*>(this)->RunsAt(pos));
}

std::string UCRichDocumentEditor::TextAt(const RichDocPosition& pos) const {
    const std::vector<RichTextRun>* runs = RunsAt(pos);
    return runs ? RunsText(*runs) : std::string();
}

int UCRichDocumentEditor::TextLengthAt(const RichDocPosition& pos) const {
    return static_cast<int>(TextAt(pos).size());
}

bool UCRichDocumentEditor::IsTextContainer(const RichDocPosition& pos) const {
    return RunsAt(pos) != nullptr;
}

RichDocPosition UCRichDocumentEditor::ContainerStart(const RichDocPosition& pos) const {
    RichDocPosition out = pos;
    out.byteOffset = 0;
    return out;
}

RichDocPosition UCRichDocumentEditor::ContainerEnd(const RichDocPosition& pos) const {
    RichDocPosition out = pos;
    out.byteOffset = TextLengthAt(pos);
    return out;
}

int UCRichDocumentEditor::TableRowCount(int blockIndex) const {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return 0;
    const RichDocBlock& block = doc->blocks[static_cast<size_t>(blockIndex)];
    if (block.type != RichBlockType::Table) return 0;
    return static_cast<int>(block.tableRows.size());
}

int UCRichDocumentEditor::TableColumnCount(int blockIndex, int row) const {
    if (row < 0 || row >= TableRowCount(blockIndex)) return 0;
    const RichDocBlock& block = doc->blocks[static_cast<size_t>(blockIndex)];
    return static_cast<int>(block.tableRows[static_cast<size_t>(row)].cells.size());
}

// The first container of a block: a table opens at its top-left cell, anything
// else is its own runs. Returns false for a block with no editable text at all
// (an image, a rule, a page break, or a table with no cells).
static bool FirstContainerOfBlock(const UCRichDocumentEditor& editor, int blockIndex,
                                  RichDocPosition& out) {
    if (blockIndex < 0 || blockIndex >= editor.GetBlockCount()) return false;
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    if (block.type == RichBlockType::Table) {
        for (int r = 0; r < editor.TableRowCount(blockIndex); r++) {
            if (editor.TableColumnCount(blockIndex, r) > 0) {
                out = RichDocPosition(blockIndex, r, 0, 0);
                return true;
            }
        }
        return false;
    }
    RichDocPosition candidate(blockIndex, 0);
    if (!editor.IsTextContainer(candidate)) return false;
    out = candidate;
    return true;
}

static bool LastContainerOfBlock(const UCRichDocumentEditor& editor, int blockIndex,
                                 RichDocPosition& out) {
    if (blockIndex < 0 || blockIndex >= editor.GetBlockCount()) return false;
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    if (block.type == RichBlockType::Table) {
        for (int r = editor.TableRowCount(blockIndex) - 1; r >= 0; r--) {
            const int columns = editor.TableColumnCount(blockIndex, r);
            if (columns > 0) {
                out = RichDocPosition(blockIndex, r, columns - 1, 0);
                return true;
            }
        }
        return false;
    }
    RichDocPosition candidate(blockIndex, 0);
    if (!editor.IsTextContainer(candidate)) return false;
    out = candidate;
    return true;
}

std::vector<RichDocPosition> UCRichDocumentEditor::AllContainers() const {
    std::vector<RichDocPosition> out;
    RichDocPosition pos;
    if (!FirstContainerOfBlock(*this, 0, pos)) {
        // The first block holds no text; NextContainer finds the first that does.
        pos = RichDocPosition(0, 0);
        if (!IsTextContainer(pos) && !NextContainer(pos)) return out;
        if (!IsTextContainer(pos)) return out;
    }
    out.push_back(pos);
    while (NextContainer(pos)) out.push_back(pos);
    return out;
}

void UCRichDocumentEditor::ForEachContainer(
        const std::function<void(const RichDocPosition&)>& fn) const {
    for (const RichDocPosition& container : AllContainers()) fn(container);
}

bool UCRichDocumentEditor::NextContainer(RichDocPosition& pos) const {
    // Inside a table: along the row, then down to the next row's first cell.
    if (pos.InCell()) {
        const int columns = TableColumnCount(pos.blockIndex, pos.cellRow);
        if (pos.cellColumn + 1 < columns) {
            pos = RichDocPosition(pos.blockIndex, pos.cellRow, pos.cellColumn + 1, 0);
            return true;
        }
        for (int r = pos.cellRow + 1; r < TableRowCount(pos.blockIndex); r++) {
            if (TableColumnCount(pos.blockIndex, r) > 0) {
                pos = RichDocPosition(pos.blockIndex, r, 0, 0);
                return true;
            }
        }
    }
    // Otherwise (or falling out of the last cell): the next block that holds text.
    for (int b = pos.blockIndex + 1; b < GetBlockCount(); b++) {
        RichDocPosition first;
        if (FirstContainerOfBlock(*this, b, first)) {
            pos = first;
            return true;
        }
    }
    return false;
}

bool UCRichDocumentEditor::PreviousContainer(RichDocPosition& pos) const {
    if (pos.InCell()) {
        if (pos.cellColumn > 0) {
            pos = RichDocPosition(pos.blockIndex, pos.cellRow, pos.cellColumn - 1, 0);
            return true;
        }
        for (int r = pos.cellRow - 1; r >= 0; r--) {
            const int columns = TableColumnCount(pos.blockIndex, r);
            if (columns > 0) {
                pos = RichDocPosition(pos.blockIndex, r, columns - 1, 0);
                return true;
            }
        }
    }
    for (int b = pos.blockIndex - 1; b >= 0; b--) {
        RichDocPosition last;
        if (LastContainerOfBlock(*this, b, last)) {
            pos = last;
            return true;
        }
    }
    return false;
}

std::string UCRichDocumentEditor::BlockText(int blockIndex) const {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return {};
    const RichDocBlock& block = doc->blocks[blockIndex];
    if (!IsTextBlockType(block.type)) return {};
    return RunsText(block.runs);
}

int UCRichDocumentEditor::BlockTextLength(int blockIndex) const {
    return static_cast<int>(BlockText(blockIndex).size());
}

bool UCRichDocumentEditor::IsTextBlock(int blockIndex) const {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return false;
    return IsTextBlockType(doc->blocks[blockIndex].type);
}

// ===== CARET AND SELECTION =====

RichDocPosition UCRichDocumentEditor::ClampPosition(const RichDocPosition& pos) const {
    RichDocPosition out = pos;
    int blockCount = GetBlockCount();
    out.blockIndex = std::max(0, std::min(out.blockIndex, blockCount - 1));

    // Cell coordinates that no longer address a cell (the block changed type,
    // or rows were removed) fall back to the block's own runs rather than
    // leaving the caret pointing at nothing.
    if (out.InCell() && !IsTextContainer(out)) {
        out.cellRow = -1;
        out.cellColumn = -1;
    }

    std::string text = TextAt(out);
    out.byteOffset = std::max(0, std::min(out.byteOffset, static_cast<int>(text.size())));
    out.byteOffset = SnapToCharStart(text, out.byteOffset);
    return out;
}

// Shapes the moving end of a selection so the range is one an edit can honour.
// Text between two cells cannot be deleted - cells are not joined by it - so:
// - both ends in cells of one table: kept, and the selection is a block of
//   cells (see HasCellSelection);
// - from a cell out of its table: held at the table's first or last cell, a
//   cell selection reaching the table's edge;
// - from outside a table into it: the table is taken whole, the end moving
//   past it (or, travelling back towards the anchor, before it).
RichDocPosition UCRichDocumentEditor::ClampToAnchorContainer(const RichDocPosition& pos) const {
    if (pos.SameContainer(anchor)) return pos;
    if (!pos.InCell() && !anchor.InCell()) return pos;    // ordinary block selection
    if (pos.InCell() && anchor.InCell() && pos.blockIndex == anchor.blockIndex) return pos;

    if (anchor.InCell()) {
        RichDocPosition edge;
        const bool forward = anchor < pos;
        if (forward ? LastContainerOfBlock(*this, anchor.blockIndex, edge)
                    : FirstContainerOfBlock(*this, anchor.blockIndex, edge)) {
            return forward ? ContainerEnd(edge) : ContainerStart(edge);
        }
        return anchor;
    }

    // The anchor is outside the table the moving end reached.
    const int table = pos.blockIndex;
    const bool anchorBefore = anchor.blockIndex < table;
    // Coming back towards the anchor from the far side of the table stops
    // short of it; otherwise the table is included.
    const bool retreating = anchorBefore ? caret.blockIndex > table : caret.blockIndex < table;
    auto afterTable = [&]() {
        // The first container past the table's last cell.
        RichDocPosition probe;
        if (LastContainerOfBlock(*this, table, probe) && NextContainer(probe) && probe.blockIndex != table) {
            return probe;
        }
        return RichDocPosition(table, 0);
    };
    auto beforeTable = [&]() {
        RichDocPosition first;
        if (FirstContainerOfBlock(*this, table, first)) {
            RichDocPosition previous = first;
            if (PreviousContainer(previous) && previous.blockIndex != table) return ContainerEnd(previous);
        }
        return RichDocPosition(table, 0);
    };
    if (anchorBefore) return retreating ? beforeTable() : afterTable();
    return retreating ? afterTable() : beforeTable();
}

// ===== CELL SELECTION =====

bool UCRichDocumentEditor::CellRectBetween(const RichDocPosition& a, const RichDocPosition& b,
                                           int& top, int& left, int& bottom, int& right) const {
    if (!a.InCell() || !b.InCell() || a.blockIndex != b.blockIndex || a.SameContainer(b)) return false;
    if (a.blockIndex < 0 || a.blockIndex >= GetBlockCount()) return false;
    const RichDocBlock& table = doc->blocks[static_cast<size_t>(a.blockIndex)];
    const RichTableGrid grid = BuildTableGrid(table);
    int aRow = 0, aColumn = 0, bRow = 0, bColumn = 0;
    if (!grid.OriginOf(a.cellRow, a.cellColumn, aRow, aColumn)) return false;
    if (!grid.OriginOf(b.cellRow, b.cellColumn, bRow, bColumn)) return false;
    top = std::min(aRow, bRow);
    bottom = std::max(aRow, bRow);
    left = std::min(aColumn, bColumn);
    right = std::max(aColumn, bColumn);
    // Grow until every cell the rectangle touches lies inside it: half of a
    // merged cell cannot be selected.
    for (bool grown = true; grown;) {
        grown = false;
        for (int r = top; r <= bottom; ++r) {
            for (int c = left; c <= right; ++c) {
                const RichTableGridSlot& slot = grid.At(r, c);
                if (!slot.Occupied()) continue;
                int originRow = 0, originColumn = 0;
                if (!grid.OriginOf(slot.row, slot.cellIndex, originRow, originColumn)) continue;
                const RichTableCell& cell = table.tableRows[static_cast<size_t>(slot.row)]
                                                .cells[static_cast<size_t>(slot.cellIndex)];
                const int lastRow = std::min(grid.rowCount - 1, originRow + std::max(1, cell.rowSpan) - 1);
                const int lastColumn = std::min(grid.columnCount - 1, originColumn + std::max(1, cell.columnSpan) - 1);
                if (originRow < top) { top = originRow; grown = true; }
                if (originColumn < left) { left = originColumn; grown = true; }
                if (lastRow > bottom) { bottom = lastRow; grown = true; }
                if (lastColumn > right) { right = lastColumn; grown = true; }
            }
        }
    }
    return true;
}

bool UCRichDocumentEditor::HasCellSelection() const {
    return anchor.InCell() && caret.InCell() && anchor.blockIndex == caret.blockIndex
        && !anchor.SameContainer(caret);
}

bool UCRichDocumentEditor::GetCellSelectionRect(int& top, int& left, int& bottom, int& right) const {
    return CellRectBetween(anchor, caret, top, left, bottom, right);
}

namespace {
// The model cells whose top-left slot lies in the rectangle, row by row.
std::vector<RichDocPosition> CellsInRect(const RichDocBlock& table, int blockIndex,
                                         int top, int left, int bottom, int right) {
    std::vector<RichDocPosition> cells;
    const RichTableGrid grid = BuildTableGrid(table);
    for (int r = top; r <= bottom; ++r) {
        for (int c = left; c <= right; ++c) {
            const RichTableGridSlot& slot = grid.At(r, c);
            if (slot.Occupied() && slot.origin) cells.emplace_back(blockIndex, slot.row, slot.cellIndex, 0);
        }
    }
    return cells;
}
} // namespace

std::vector<RichDocPosition> UCRichDocumentEditor::SelectedCells() const {
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!GetCellSelectionRect(top, left, bottom, right)) return {};
    return CellsInRect(doc->blocks[static_cast<size_t>(caret.blockIndex)], caret.blockIndex,
                       top, left, bottom, right);
}

bool UCRichDocumentEditor::SelectCellRange(int blockIndex, int top, int left, int bottom, int right) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.rowCount == 0) return false;
    top = std::clamp(top, 0, grid.rowCount - 1);
    bottom = std::clamp(bottom, 0, grid.rowCount - 1);
    left = std::clamp(left, 0, grid.columnCount - 1);
    right = std::clamp(right, 0, grid.columnCount - 1);
    int fromRow = 0, fromCell = 0, toRow = 0, toCell = 0;
    if (!grid.CellAt(std::min(top, bottom), std::min(left, right), fromRow, fromCell)) return false;
    if (!grid.CellAt(std::max(top, bottom), std::max(left, right), toRow, toCell)) return false;
    anchor = RichDocPosition(blockIndex, fromRow, fromCell, 0);
    caret = RichDocPosition(blockIndex, toRow, toCell, 0);
    if (anchor.SameContainer(caret)) caret = ContainerEnd(caret);
    coalescing = false;
    pendingFormatValid = false;
    NotifySelectionChanged();
    return true;
}

void UCRichDocumentEditor::ClearSelectedCellsInternal() {
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!GetCellSelectionRect(top, left, bottom, right)) return;
    const int blockIndex = caret.blockIndex;
    RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];
    const std::vector<RichDocPosition> cells = CellsInRect(table, blockIndex, top, left, bottom, right);
    for (const RichDocPosition& cell : cells) {
        std::vector<RichTextRun>& runs = table.tableRows[static_cast<size_t>(cell.cellRow)]
                                             .cells[static_cast<size_t>(cell.cellColumn)].runs;
        // The first run stays, empty, so what is typed next keeps its look.
        if (runs.empty()) continue;
        runs.resize(1);
        runs[0].text.clear();
        runs[0].lineBreakBefore = false;
        if (runs[0].IsInlineImage() || runs[0].field != RichTextRun::Field::Plain || runs[0].IsNoteReference()) {
            runs[0] = RichTextRun{};
        }
    }
    caret = cells.empty() ? ClampPosition(caret) : cells.front();
    anchor = caret;
}

bool UCRichDocumentEditor::MergeSelectedCells() {
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!GetCellSelectionRect(top, left, bottom, right)) return false;
    const int blockIndex = caret.blockIndex;
    const RichTableGrid grid = TableGrid(blockIndex);
    int row = 0, cellIndex = 0;
    if (!grid.CellAt(top, left, row, cellIndex)) return false;
    const RichTableCell& origin = doc->blocks[static_cast<size_t>(blockIndex)]
                                      .tableRows[static_cast<size_t>(row)].cells[static_cast<size_t>(cellIndex)];
    const int extraColumns = right - (left + std::max(1, origin.columnSpan) - 1);
    const int extraRows = bottom - (top + std::max(1, origin.rowSpan) - 1);
    if (extraColumns < 0 || extraRows < 0 || (extraColumns == 0 && extraRows == 0)) return false;
    return MergeTableCells(blockIndex, row, cellIndex, extraColumns, extraRows);
}

void UCRichDocumentEditor::SetCaret(const RichDocPosition& pos, bool extend) {
    RichDocPosition clamped = ClampPosition(pos);
    if (extend) clamped = ClampPosition(ClampToAnchorContainer(clamped));
    if (clamped == caret && (extend || anchor == caret)) return;
    caret = clamped;
    if (!extend) anchor = caret;
    coalescing = false;         // a moved caret ends the current typing run
    pendingFormatValid = false;
    NotifySelectionChanged();
}

void UCRichDocumentEditor::SetSelection(const RichDocPosition& from, const RichDocPosition& to) {
    anchor = ClampPosition(from);
    caret = ClampPosition(ClampToAnchorContainer(ClampPosition(to)));
    coalescing = false;
    pendingFormatValid = false;
    NotifySelectionChanged();
}

void UCRichDocumentEditor::SelectAll() {
    SetSelection(DocumentStart(), DocumentEnd());
}

void UCRichDocumentEditor::SelectBlock(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return;
    SetSelection({blockIndex, 0}, {blockIndex, BlockTextLength(blockIndex)});
}

void UCRichDocumentEditor::SelectWordAt(const RichDocPosition& pos) {
    RichDocRange word = WordAt(pos);
    SetSelection(word.start, word.end);
}

void UCRichDocumentEditor::ClearSelection() {
    if (anchor == caret) return;
    anchor = caret;
    NotifySelectionChanged();
}

// ===== NAVIGATION =====

RichDocPosition UCRichDocumentEditor::NextCharacter(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    std::string text = TextAt(p);
    if (p.byteOffset < static_cast<int>(text.size())) {
        RichDocPosition out = p;
        out.byteOffset = NextCharOffset(text, p.byteOffset);
        return out;
    }
    // At the end of this container, step into the next one — the following
    // cell of a table, or the next block.
    RichDocPosition next = p;
    if (NextContainer(next)) return next;
    return p;
}

RichDocPosition UCRichDocumentEditor::PreviousCharacter(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    if (p.byteOffset > 0) {
        std::string text = TextAt(p);
        RichDocPosition out = p;
        out.byteOffset = PreviousCharOffset(text, p.byteOffset);
        return out;
    }
    RichDocPosition previous = p;
    if (PreviousContainer(previous)) return ContainerEnd(previous);
    return p;
}

RichDocPosition UCRichDocumentEditor::NextWord(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    std::string text = TextAt(p);
    int n = static_cast<int>(text.size());
    if (p.byteOffset >= n) {
        RichDocPosition next = p;
        if (NextContainer(next)) return next;
        return p;
    }
    int i = p.byteOffset;
    // Out of the current word, then over the gap to the next word's first byte.
    while (i < n && IsWordByte(static_cast<unsigned char>(text[i]))) i = NextCharOffset(text, i);
    while (i < n && !IsWordByte(static_cast<unsigned char>(text[i]))) i = NextCharOffset(text, i);
    RichDocPosition out = p;
    out.byteOffset = i;
    return out;
}

RichDocPosition UCRichDocumentEditor::PreviousWord(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    if (p.byteOffset == 0) {
        RichDocPosition previous = p;
        if (PreviousContainer(previous)) return ContainerEnd(previous);
        return p;
    }
    std::string text = TextAt(p);
    int i = p.byteOffset;
    auto prevByteIsWord = [&](int at) {
        int prev = PreviousCharOffset(text, at);
        return IsWordByte(static_cast<unsigned char>(text[prev]));
    };
    while (i > 0 && !prevByteIsWord(i)) i = PreviousCharOffset(text, i);
    while (i > 0 && prevByteIsWord(i)) i = PreviousCharOffset(text, i);
    RichDocPosition out = p;
    out.byteOffset = i;
    return out;
}

RichDocPosition UCRichDocumentEditor::BlockEnd(const RichDocPosition& pos) const {
    return ContainerEnd(ClampPosition(pos));
}

RichDocPosition UCRichDocumentEditor::DocumentStart() const {
    return {0, 0};
}

RichDocPosition UCRichDocumentEditor::DocumentEnd() const {
    int last = std::max(0, GetBlockCount() - 1);
    RichDocPosition end;
    if (LastContainerOfBlock(*this, last, end)) return ContainerEnd(end);
    return {last, 0};
}

RichDocRange UCRichDocumentEditor::WordAt(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    std::string text = TextAt(p);
    int n = static_cast<int>(text.size());
    if (n == 0) return RichDocRange(p, p);

    int at = std::min(p.byteOffset, n - 1);
    at = SnapToCharStart(text, at);
    bool wordRun = IsWordByte(static_cast<unsigned char>(text[at]));

    int start = at;
    while (start > 0) {
        int prev = PreviousCharOffset(text, start);
        if (IsWordByte(static_cast<unsigned char>(text[prev])) != wordRun) break;
        start = prev;
    }
    int end = at;
    while (end < n && IsWordByte(static_cast<unsigned char>(text[end])) == wordRun) {
        end = NextCharOffset(text, end);
    }
    return RichDocRange({p.blockIndex, start}, {p.blockIndex, end});
}

// ===== UNDO =====

void UCRichDocumentEditor::NoteChangedBlocks(int first, int count) {
    const int blockCount = GetBlockCount();
    if (blockCount == 0) {
        lastChangedFirst = lastChangedLast = -1;
        return;
    }
    lastChangedFirst = std::max(0, std::min(first, blockCount - 1));
    // An edit that removed blocks reports the span it left behind; one that
    // added them reports all of them. Either way the range is clamped to what
    // the document now holds, so a caller can index with it directly.
    lastChangedLast = std::max(lastChangedFirst,
                               std::min(first + std::max(0, count) - 1, blockCount - 1));
}

void UCRichDocumentEditor::CommitStep(UndoStep step) {
    NoteChangedBlocks(step.firstBlock, static_cast<int>(step.after.size()));
    if (applyingUndo) return;
    modified = true;

    if (coalescing && step.typing && !undoStack.empty()) {
        UndoStep& last = undoStack.back();
        if (last.typing && !last.stylesChanged && !step.stylesChanged && last.firstBlock == step.firstBlock
            && last.after.size() == step.before.size()) {
            // Same span, still typing: fold this keystroke into the open step
            // so a typed word undoes in one go.
            last.after = std::move(step.after);
            last.caretAfter = step.caretAfter;
            last.anchorAfter = step.anchorAfter;
            redoStack.clear();
            return;
        }
    }

    undoStack.push_back(std::move(step));
    if (maxUndoSteps > 0 && undoStack.size() > maxUndoSteps) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
    coalescing = undoStack.back().typing;
}

bool UCRichDocumentEditor::Undo() {
    if (undoStack.empty()) return false;
    UndoStep step = std::move(undoStack.back());
    undoStack.pop_back();

    applyingUndo = true;
    int first = std::min(step.firstBlock, static_cast<int>(doc->blocks.size()));
    int count = std::min(static_cast<int>(step.after.size()),
                         static_cast<int>(doc->blocks.size()) - first);
    doc->blocks.erase(doc->blocks.begin() + first, doc->blocks.begin() + first + count);
    doc->blocks.insert(doc->blocks.begin() + first, step.before.begin(), step.before.end());
    if (step.stylesChanged) doc->styles = step.stylesBefore;
    EnsureNotEmpty();
    caret = ClampPosition(step.caretBefore);
    anchor = ClampPosition(step.anchorBefore);
    applyingUndo = false;
    NoteChangedBlocks(first, static_cast<int>(step.before.size()));

    redoStack.push_back(std::move(step));
    coalescing = false;
    modified = true;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::Redo() {
    if (redoStack.empty()) return false;
    UndoStep step = std::move(redoStack.back());
    redoStack.pop_back();

    applyingUndo = true;
    int first = std::min(step.firstBlock, static_cast<int>(doc->blocks.size()));
    int count = std::min(static_cast<int>(step.before.size()),
                         static_cast<int>(doc->blocks.size()) - first);
    doc->blocks.erase(doc->blocks.begin() + first, doc->blocks.begin() + first + count);
    doc->blocks.insert(doc->blocks.begin() + first, step.after.begin(), step.after.end());
    if (step.stylesChanged) doc->styles = step.stylesAfter;
    EnsureNotEmpty();
    caret = ClampPosition(step.caretAfter);
    anchor = ClampPosition(step.anchorAfter);
    applyingUndo = false;
    NoteChangedBlocks(first, static_cast<int>(step.after.size()));

    undoStack.push_back(std::move(step));
    coalescing = false;
    modified = true;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

void UCRichDocumentEditor::SetMaxUndoSteps(size_t steps) {
    maxUndoSteps = steps;
    if (maxUndoSteps > 0 && undoStack.size() > maxUndoSteps) {
        undoStack.erase(undoStack.begin(),
                        undoStack.begin() + static_cast<long>(undoStack.size() - maxUndoSteps));
    }
}

void UCRichDocumentEditor::ClearUndoHistory() {
    undoStack.clear();
    redoStack.clear();
    coalescing = false;
}

void UCRichDocumentEditor::NotifyChanged() {
    if (onChanged) onChanged();
}

void UCRichDocumentEditor::NotifySelectionChanged() {
    if (onSelectionChanged) onSelectionChanged();
}

void UCRichDocumentEditor::SelectedBlockRange(int& firstBlock, int& lastBlock) const {
    RichDocRange range = GetSelectionRange();
    firstBlock = range.start.blockIndex;
    lastBlock = range.end.blockIndex;
    // A selection ending exactly at the start of a block does not include it.
    if (lastBlock > firstBlock && range.end.byteOffset == 0) lastBlock--;
}

// ===== TEXT EDITING =====

void UCRichDocumentEditor::DeleteRange(const RichDocRange& range) {
    if (range.IsEmpty()) return;
    int first = range.start.blockIndex;
    int count = range.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        DeleteRangeInternal(range);
    }
    NotifyChanged();
    NotifySelectionChanged();
}

void UCRichDocumentEditor::DeleteRangeInternal(const RichDocRange& range) {
    if (range.IsEmpty()) return;
    if (trackChanges && MarkRangeDeleted(range)) return;
    // Two cells of one table: the cells between them are emptied, not joined.
    int top = 0, left = 0, bottom = 0, right = 0;
    if (CellRectBetween(range.start, range.end, top, left, bottom, right)) {
        caret = range.end;
        anchor = range.start;
        ClearSelectedCellsInternal();
        return;
    }
    int firstBlock = range.start.blockIndex;
    int lastBlock = std::min(range.end.blockIndex, GetBlockCount() - 1);

    if (range.start.SameContainer(range.end)) {
        // One container — a block's own runs, or a single table cell.
        if (std::vector<RichTextRun>* runs = MutableRunsAt(range.start)) {
            EraseRunRange(*runs, range.start.byteOffset, range.end.byteOffset);
        }
        EnsureNotEmpty();
        caret = ClampPosition(range.start);
        anchor = caret;
        return;
    }

    if (firstBlock == lastBlock) {
        RichDocBlock& block = doc->blocks[firstBlock];
        if (IsTextBlockType(block.type)) {
            EraseRunRange(block.runs, range.start.byteOffset, range.end.byteOffset);
        }
    } else {
        // Keep the head of the first block and the tail of the last, drop
        // everything between, then join the two survivors into one block.
        RichDocBlock& head = doc->blocks[firstBlock];
        RichDocBlock tail = doc->blocks[lastBlock];

        if (IsTextBlockType(head.type)) {
            int headLen = static_cast<int>(RunsText(head.runs).size());
            EraseRunRange(head.runs, range.start.byteOffset, headLen);
        } else {
            head.runs.clear();
        }
        std::vector<RichTextRun> tailRuns;
        if (IsTextBlockType(tail.type)) {
            tailRuns = SliceRuns(tail.runs, range.end.byteOffset,
                                 static_cast<int>(RunsText(tail.runs).size()));
        }
        doc->blocks.erase(doc->blocks.begin() + firstBlock + 1,
                          doc->blocks.begin() + lastBlock + 1);

        RichDocBlock& merged = doc->blocks[firstBlock];
        if (!IsTextBlockType(merged.type)) {
            // The surviving head was an image or a rule: it cannot hold text,
            // so the selection consumed it entirely and the tail takes over.
            merged = tail;
            merged.runs = tailRuns;
            if (!merged.runs.empty()) merged.runs.front().lineBreakBefore = false;
        } else {
            for (auto& run : tailRuns) merged.runs.push_back(run);
            CoalesceRuns(merged.runs);
        }
    }
    EnsureNotEmpty();
    caret = ClampPosition(range.start);
    anchor = caret;
}

bool UCRichDocumentEditor::DeleteSelection() {
    if (!HasSelection()) return false;
    DeleteRange(GetSelectionRange());
    return true;
}

namespace {
// Every attribute of `run` as a delta, so replaced text can be given the
// formatting of the text it replaced.
RichCharFormatDelta DeltaFromRun(const RichTextRun& run) {
    RichCharFormatDelta delta;
    delta.setBold = true;          delta.bold = run.bold;
    delta.setItalic = true;        delta.italic = run.italic;
    delta.setUnderline = true;     delta.underline = run.underline;
    delta.setStrikethrough = true; delta.strikethrough = run.strikethrough;
    delta.setCode = true;          delta.code = run.code;
    delta.setSubscript = true;     delta.subscript = run.subscript;
    delta.setSuperscript = true;   delta.superscript = run.superscript;
    delta.setFontFamily = true;    delta.fontFamily = run.fontFamily;
    delta.setFontSize = true;      delta.fontSizePt = run.fontSizePt;
    delta.setColor = true;         delta.color = run.color;
    delta.setLink = true;          delta.linkTarget = run.linkTarget;
    return delta;
}
} // namespace

void UCRichDocumentEditor::ReplaceRange(const RichDocRange& range, const std::string& utf8) {
    int first = range.start.blockIndex;
    int count = range.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        ReplaceRangeInternal(range, utf8);
    }
    NotifyChanged();
    NotifySelectionChanged();
}

// Replaces `range` with `utf8`, which keeps the formatting of the text it
// replaced: deleting the range leaves the caret at the end of whatever preceded
// it, so a plain insert would silently adopt that run's formatting instead —
// replacing a bold word would leave plain text behind. No undo step of its own,
// so ReplaceAll can put a whole replace into one.
void UCRichDocumentEditor::ReplaceRangeInternal(const RichDocRange& range,
                                                const std::string& utf8) {
    // Sample from inside the match, before it is gone. A single-block range is
    // all Find produces, and a multi-block one takes its start block's format.
    RichCharFormatDelta format;
    bool haveFormat = false;
    if (range.start.blockIndex == range.end.blockIndex
        && range.end.byteOffset > range.start.byteOffset) {
        const int middle = range.start.byteOffset
                         + (range.end.byteOffset - range.start.byteOffset) / 2;
        format = DeltaFromRun(FormatAt(RichDocPosition(range.start.blockIndex, middle)));
        haveFormat = true;
    }

    caret = range.start;
    anchor = range.start;
    DeleteRangeInternal(range);
    caret = range.start;
    anchor = range.start;
    if (utf8.empty()) return;

    InsertTextInternal(utf8);
    if (haveFormat) {
        ApplyCharFormatToRangeInternal(
            RichDocRange(range.start,
                         RichDocPosition(range.start.blockIndex,
                                         range.start.byteOffset
                                             + static_cast<int>(utf8.size()))),
            format);
    }
}

// ===== SEARCH =====

namespace {

// ASCII case folding, as UltraCanvasTextArea's search uses. See the comment on
// RichFindOptions for what that does and does not cover.
inline char FoldAscii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// A byte that can be part of a word for the whole-word test. Every byte of a
// multi-byte UTF-8 sequence counts, so "Straße" is one word rather than two
// either side of the ß.
inline bool IsWordByte(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) != 0 || u == '_';
}

bool MatchesAt(const std::string& haystack, const std::string& needle,
               size_t at, bool caseSensitive) {
    if (at + needle.size() > haystack.size()) return false;
    if (caseSensitive) {
        return haystack.compare(at, needle.size(), needle) == 0;
    }
    for (size_t i = 0; i < needle.size(); i++) {
        if (FoldAscii(haystack[at + i]) != FoldAscii(needle[i])) return false;
    }
    return true;
}

bool IsWholeWordAt(const std::string& haystack, size_t at, size_t length) {
    if (at > 0 && IsWordByte(haystack[at - 1])) return false;
    const size_t after = at + length;
    if (after < haystack.size() && IsWordByte(haystack[after])) return false;
    return true;
}

// First match at or after `fromByte` (at or before it, searching backwards),
// or std::string::npos. Byte-wise scanning is safe for UTF-8: a match can only
// start on a lead byte, because a needle that starts mid-sequence cannot equal
// a well-formed needle's bytes.
size_t FindInText(const std::string& haystack, const std::string& needle,
                  size_t fromByte, bool backwards,
                  bool caseSensitive, bool wholeWord) {
    if (needle.empty() || needle.size() > haystack.size()) return std::string::npos;
    const size_t last = haystack.size() - needle.size();

    if (!backwards) {
        for (size_t at = std::min(fromByte, last + 1); at <= last; at++) {
            if (!MatchesAt(haystack, needle, at, caseSensitive)) continue;
            if (wholeWord && !IsWholeWordAt(haystack, at, needle.size())) continue;
            return at;
        }
        return std::string::npos;
    }

    // Backwards: the match must END at or before fromByte, so that repeatedly
    // searching back from a match's start walks through matches one at a time.
    if (fromByte < needle.size()) return std::string::npos;
    for (size_t at = std::min(fromByte - needle.size(), last) + 1; at-- > 0; ) {
        if (!MatchesAt(haystack, needle, at, caseSensitive)) continue;
        if (wholeWord && !IsWholeWordAt(haystack, at, needle.size())) continue;
        return at;
    }
    return std::string::npos;
}

} // namespace

bool UCRichDocumentEditor::Find(const std::string& needle,
                                const RichDocPosition& from,
                                bool backwards,
                                const RichFindOptions& options,
                                RichDocRange& outMatch) const {
    if (needle.empty() || doc->blocks.empty()) return false;

    const std::vector<RichDocPosition> containers = AllContainers();
    if (containers.empty()) return false;

    const RichDocPosition start = ClampPosition(from);
    // Where `start` sits in the container order. A position whose container
    // holds no text (an image block) falls to the nearest one in the search
    // direction, so a search from there still goes somewhere sensible.
    size_t startIndex = 0;
    bool startIsOwnContainer = false;
    for (size_t i = 0; i < containers.size(); i++) {
        if (containers[i].SameContainer(start)) {
            startIndex = i;
            startIsOwnContainer = true;
            break;
        }
        if (containers[i] < start) startIndex = i;
    }

    // Visit every container once from `start`, then — with wrapAround — the
    // ones before it. The starting container is searched from the offset;
    // every other one from its far end.
    const size_t count = containers.size();
    const size_t limit = options.wrapAround ? count : (backwards ? startIndex + 1 : count - startIndex);

    for (size_t step = 0; step < limit; step++) {
        size_t index;
        if (!backwards) {
            index = (startIndex + step) % count;
        } else {
            index = (startIndex + count - (step % count)) % count;
        }
        const RichDocPosition& container = containers[index];
        const std::string text = TextAt(container);
        if (text.empty()) continue;

        size_t origin;
        if (step == 0 && startIsOwnContainer) {
            origin = std::min(static_cast<size_t>(start.byteOffset), text.size());
        } else {
            origin = backwards ? text.size() : 0;
        }

        const size_t at = FindInText(text, needle, origin, backwards,
                                     options.caseSensitive, options.wholeWord);
        if (at != std::string::npos) {
            RichDocPosition matchStart = container;
            matchStart.byteOffset = static_cast<int>(at);
            RichDocPosition matchEnd = container;
            matchEnd.byteOffset = static_cast<int>(at + needle.size());
            outMatch = RichDocRange(matchStart, matchEnd);
            return true;
        }
    }
    return false;
}

std::vector<RichDocRange> UCRichDocumentEditor::FindAll(
        const std::string& needle, const RichFindOptions& options) const {
    std::vector<RichDocRange> matches;
    if (needle.empty()) return matches;

    // Every container in document order: each block's own runs, and every cell
    // of a table, so a search reaches inside tables as well.
    ForEachContainer([&](const RichDocPosition& container) {
        const std::string text = TextAt(container);
        if (text.empty()) return;
        size_t at = 0;
        while ((at = FindInText(text, needle, at, /*backwards*/ false,
                                options.caseSensitive, options.wholeWord))
               != std::string::npos) {
            RichDocPosition start = container;
            start.byteOffset = static_cast<int>(at);
            RichDocPosition end = container;
            end.byteOffset = static_cast<int>(at + needle.size());
            matches.emplace_back(start, end);
            // Matches do not overlap: carry on past this one.
            at += needle.size();
            if (at > text.size()) break;
        }
    });
    return matches;
}

int UCRichDocumentEditor::ReplaceAll(const std::string& needle,
                                     const std::string& replacement,
                                     const RichFindOptions& options) {
    const std::vector<RichDocRange> matches = FindAll(needle, options);
    if (matches.empty()) return 0;

    {
        // One scope over the whole document, so the whole replace is one undo
        // step. Applied back to front: every match lies inside a single block,
        // so replacing a later one never moves an earlier one's offsets.
        EditScope scope(*this, 0, static_cast<int>(doc->blocks.size()));
        // Back to front: every match lies inside a single block, so replacing a
        // later one never moves an earlier one's offsets.
        for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
            ReplaceRangeInternal(*it, replacement);
        }
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return static_cast<int>(matches.size());
}

void UCRichDocumentEditor::InsertTextInternal(const std::string& utf8) {
    if (utf8.empty()) return;

    // Line feeds in inserted text become paragraph breaks — what pasting
    // multi-line plain text into a word processor does.
    std::vector<std::string> paragraphs;
    std::string current;
    for (char c : utf8) {
        if (c == '\r') continue;
        if (c == '\n') {
            paragraphs.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    paragraphs.push_back(current);

    const RichTextRun* format = pendingFormatValid ? &pendingFormat : nullptr;
    for (size_t i = 0; i < paragraphs.size(); i++) {
        if (i > 0) {
            // A cell holds one paragraph: a newline inside it becomes a line
            // break rather than splitting the table's block in two.
            if (caret.InCell()) InsertLineBreakInternal();
            else SplitBlockInternal();
        }
        if (paragraphs[i].empty()) continue;

        if (!caret.InCell() && !IsTextBlockType(doc->blocks[caret.blockIndex].type)) {
            // Typing at an image or a rule starts a paragraph after it.
            RichDocBlock paragraph;
            paragraph.type = RichBlockType::Paragraph;
            doc->blocks.insert(doc->blocks.begin() + caret.blockIndex + 1, paragraph);
            caret = RichDocPosition(caret.blockIndex + 1, 0);
        }
        std::vector<RichTextRun>* target = MutableRunsAt(caret);
        if (!target) continue;
        InsertIntoRuns(*target, caret.byteOffset, paragraphs[i], format);
        caret.byteOffset += static_cast<int>(paragraphs[i].size());
        anchor = caret;
    }
    pendingFormatValid = false;
}

void UCRichDocumentEditor::InsertText(const std::string& utf8) {
    if (utf8.empty()) return;
    RichDocRange selection = GetSelectionRange();
    // An edit inside a cell still replaces the whole table block in the undo
    // step: a block span is the unit the step records, and a table is one block.
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count, /*typing*/ true);
        if (HasSelection()) DeleteRangeInternal(selection);
        InsertTextInternal(utf8);
    }
    NotifyChanged();
    NotifySelectionChanged();
}

// The break itself, with no undo step of its own, so InsertTextInternal can use
// it for a newline typed inside a table cell.
void UCRichDocumentEditor::InsertLineBreakInternal() {
    std::vector<RichTextRun>* runs = MutableRunsAt(caret);
    if (!runs) return;
    const RichTextRun* format = pendingFormatValid ? &pendingFormat : nullptr;
    InsertIntoRuns(*runs, caret.byteOffset, "", format, /*lineBreakBefore*/ true);
    caret.byteOffset += 1;      // the '\n' the break contributes
    anchor = caret;
}

void UCRichDocumentEditor::InsertLineBreak() {
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        InsertLineBreakInternal();
    }
    NotifyChanged();
    NotifySelectionChanged();
}

void UCRichDocumentEditor::SplitBlockInternal() {
    // Enter inside a table cell adds a line to the cell; splitting the table's
    // block there would tear the table in half.
    if (caret.InCell()) {
        InsertLineBreakInternal();
        return;
    }

    RichDocBlock& block = doc->blocks[caret.blockIndex];

    if (!IsTextBlockType(block.type)) {
        RichDocBlock paragraph;
        paragraph.type = RichBlockType::Paragraph;
        doc->blocks.insert(doc->blocks.begin() + caret.blockIndex + 1, paragraph);
        caret = RichDocPosition(caret.blockIndex + 1, 0);
        anchor = caret;
        return;
    }

    // Enter on an empty list item leaves the list instead of adding another
    // bullet — the behaviour every word processor and editor has.
    if (block.type == RichBlockType::ListItem && RunsText(block.runs).empty()) {
        if (block.listLevel > 0) {
            block.listLevel--;
            AdoptListLevelFormat(doc->blocks, static_cast<size_t>(caret.blockIndex));
        } else {
            block.type = RichBlockType::Paragraph;
            block.orderedList = false;
            block.checkbox = block.checked = false;
        }
        caret.byteOffset = 0;
        anchor = caret;
        return;
    }

    int textLength = static_cast<int>(RunsText(block.runs).size());
    std::vector<RichTextRun> tailRuns = SliceRuns(block.runs, caret.byteOffset, textLength);
    EraseRunRange(block.runs, caret.byteOffset, textLength);

    RichDocBlock next;
    next.align = block.align;
    switch (block.type) {
        case RichBlockType::ListItem:
            next.type = RichBlockType::ListItem;
            next.orderedList = block.orderedList;
            next.listLevel = block.listLevel;
            next.numberFormat = block.numberFormat;
            next.numberTemplate = block.numberTemplate;
            next.bulletText = block.bulletText;
            // The next item of a check list is another box, not yet ticked.
            next.checkbox = block.checkbox;
            break;
        case RichBlockType::BlockQuote:
            next.type = RichBlockType::BlockQuote;
            break;
        case RichBlockType::CodeBlock:
            next.type = RichBlockType::CodeBlock;
            next.codeLanguage = block.codeLanguage;
            break;
        default:
            // A heading's continuation is body text, not another heading.
            next.type = RichBlockType::Paragraph;
            break;
    }
    // The second half of a paragraph is laid out like the first: same
    // indents, spacing and tab stops. Not so after a heading, whose
    // continuation is body text.
    if (next.type == block.type) next.CopyParagraphGeometry(block);
    // A named style says what follows it (a heading: body text).
    if (!block.styleId.empty()) {
        const RichStyle* style = doc->FindStyle(block.styleId);
        next.styleId = style && !style->nextStyle.empty() ? style->nextStyle : block.styleId;
        if (next.styleId == "Normal") next.styleId.clear();
        if (next.styleId != block.styleId) {
            RestyleBlock(next, doc->ResolveStyle(block.styleId), doc->ResolveStyle(EffectiveStyleId(next)), true);
        }
    }
    next.runs = std::move(tailRuns);
    doc->blocks.insert(doc->blocks.begin() + caret.blockIndex + 1, next);
    caret = RichDocPosition(caret.blockIndex + 1, 0);
    anchor = caret;
}

void UCRichDocumentEditor::SplitBlock() {
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        // Inside a code block Enter adds a line, it does not end the block.
        if (doc->blocks[caret.blockIndex].type == RichBlockType::CodeBlock) {
            RichDocBlock& block = doc->blocks[caret.blockIndex];
            InsertIntoRuns(block.runs, caret.byteOffset, "", nullptr, true);
            caret.byteOffset += 1;
            anchor = caret;
        } else {
            SplitBlockInternal();
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
}

bool UCRichDocumentEditor::DeleteBackward() {
    if (HasSelection()) return DeleteSelection();

    if (caret.byteOffset > 0) {
        std::string text = TextAt(caret);
        RichDocPosition from = caret;
        from.byteOffset = PreviousCharOffset(text, caret.byteOffset);
        trackBackward = true;
        DeleteRange(RichDocRange(from, caret));
        trackBackward = false;
        return true;
    }
    // Tracking: a paragraph break is not a tracked change; Backspace at a
    // paragraph's start just moves back into the previous one.
    if (trackChanges && !caret.InCell() && caret.blockIndex > 0 && IsTextBlock(caret.blockIndex - 1)) {
        SetCaret(RichDocPosition(caret.blockIndex - 1, BlockTextLength(caret.blockIndex - 1)));
        return true;
    }
    // At the very start of a cell, Backspace moves to the previous cell rather
    // than deleting anything: joining cells is not an edit a table supports.
    if (caret.InCell()) {
        RichDocPosition previous = caret;
        if (PreviousContainer(previous) && previous.blockIndex == caret.blockIndex) {
            SetCaret(ContainerEnd(previous));
            return true;
        }
        return false;
    }
    if (caret.blockIndex == 0) return false;

    int previous = caret.blockIndex - 1;
    if (!IsTextBlock(previous)) {
        // An image or a rule above: Backspace removes it whole.
        {
            EditScope scope(*this, previous, 1);
            doc->blocks.erase(doc->blocks.begin() + previous);
            EnsureNotEmpty();
            caret = ClampPosition({previous, 0});
            anchor = caret;
        }
        NotifyChanged();
        NotifySelectionChanged();
        return true;
    }
    // Join this block onto the previous one.
    RichDocPosition join(previous, BlockTextLength(previous));
    DeleteRange(RichDocRange(join, {caret.blockIndex, 0}));
    return true;
}

bool UCRichDocumentEditor::DeleteForward() {
    if (HasSelection()) return DeleteSelection();

    std::string text = TextAt(caret);
    if (caret.byteOffset < static_cast<int>(text.size())) {
        RichDocPosition to = caret;
        to.byteOffset = NextCharOffset(text, caret.byteOffset);
        DeleteRange(RichDocRange(caret, to));
        return true;
    }
    if (caret.InCell()) {
        RichDocPosition next = caret;
        if (NextContainer(next) && next.blockIndex == caret.blockIndex) {
            SetCaret(ContainerStart(next));
            return true;
        }
        return false;
    }
    if (caret.blockIndex + 1 >= GetBlockCount()) return false;
    if (trackChanges && !caret.InCell() && IsTextBlock(caret.blockIndex + 1)) {
        SetCaret(RichDocPosition(caret.blockIndex + 1, 0));
        return true;
    }

    int next = caret.blockIndex + 1;
    if (!IsTextBlock(next)) {
        {
            EditScope scope(*this, next, 1);
            doc->blocks.erase(doc->blocks.begin() + next);
            EnsureNotEmpty();
            caret = ClampPosition(caret);
            anchor = caret;
        }
        NotifyChanged();
        NotifySelectionChanged();
        return true;
    }
    DeleteRange(RichDocRange(caret, {next, 0}));
    return true;
}

// ===== CHARACTER FORMATTING =====

// The formatting itself, with no undo step of its own, so a caller already
// inside an EditScope (ReplaceAll) does not commit a second one.
void UCRichDocumentEditor::ApplyCharFormatToRangeInternal(const RichDocRange& range,
                                                          const RichCharFormatDelta& delta) {
    if (delta.IsEmpty() || range.IsEmpty()) return;

    int top = 0, left = 0, bottom = 0, right = 0;
    if (CellRectBetween(range.start, range.end, top, left, bottom, right)) {
        RichDocBlock& table = doc->blocks[static_cast<size_t>(range.start.blockIndex)];
        for (const RichDocPosition& cell : CellsInRect(table, range.start.blockIndex, top, left, bottom, right)) {
            std::vector<RichTextRun>& runs = table.tableRows[static_cast<size_t>(cell.cellRow)]
                                                 .cells[static_cast<size_t>(cell.cellColumn)].runs;
            for (RichTextRun& run : runs) delta.ApplyTo(run);
            CoalesceRuns(runs);
        }
        return;
    }

    if (range.start.InCell()) {
        std::vector<RichTextRun>* runs = MutableRunsAt(range.start);
        if (!runs) return;
        const int textLength = static_cast<int>(RunsText(*runs).size());
        int from = std::max(0, std::min(range.start.byteOffset, textLength));
        int to = std::max(from, std::min(range.end.byteOffset, textLength));
        if (from == to) return;
        // Start boundary first — see EraseRunRange for why the order matters.
        int startIdx = SplitRunAt(*runs, from);
        int endIdx = SplitRunAt(*runs, to);
        for (int i = startIdx; i < endIdx && i < static_cast<int>(runs->size()); i++) {
            delta.ApplyTo((*runs)[static_cast<size_t>(i)]);
        }
        CoalesceRuns(*runs);
        return;
    }

    for (int b = range.start.blockIndex; b <= range.end.blockIndex && b < GetBlockCount(); b++) {
        RichDocBlock& block = doc->blocks[b];
        if (!IsTextBlockType(block.type)) continue;

        int textLength = static_cast<int>(RunsText(block.runs).size());
        int from = (b == range.start.blockIndex) ? range.start.byteOffset : 0;
        int to   = (b == range.end.blockIndex)   ? range.end.byteOffset   : textLength;
        from = std::max(0, std::min(from, textLength));
        to   = std::max(from, std::min(to, textLength));
        if (from == to) continue;

        // Start boundary first — see EraseRunRange for why the order matters.
        int startIdx = SplitRunAt(block.runs, from);
        int endIdx = SplitRunAt(block.runs, to);
        for (int i = startIdx; i < endIdx && i < static_cast<int>(block.runs.size()); i++) {
            delta.ApplyTo(block.runs[i]);
        }
        CoalesceRuns(block.runs);
    }
}

void UCRichDocumentEditor::ApplyCharFormatToRange(const RichDocRange& range,
                                                   const RichCharFormatDelta& delta) {
    if (delta.IsEmpty() || range.IsEmpty()) return;
    int first = range.start.blockIndex;
    int count = range.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        ApplyCharFormatToRangeInternal(range, delta);
    }
    NotifyChanged();
}

void UCRichDocumentEditor::ApplyCharFormat(const RichCharFormatDelta& delta) {
    if (delta.IsEmpty()) return;
    if (HasSelection()) {
        ApplyCharFormatToRange(GetSelectionRange(), delta);
        return;
    }
    // Collapsed caret: arm the format for whatever is typed next.
    if (!pendingFormatValid) {
        pendingFormat = FormatAt(caret);
        pendingFormat.text.clear();
        pendingFormat.lineBreakBefore = false;
        pendingFormatValid = true;
    }
    delta.ApplyTo(pendingFormat);
    NotifySelectionChanged();
}

RichTextRun UCRichDocumentEditor::FormatAt(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    const std::vector<RichTextRun>* runs = RunsAt(p);
    if (!runs) return {};
    if (const RichTextRun* run = RunAtOffset(*runs, p.byteOffset)) {
        RichTextRun copy = *run;
        copy.text.clear();
        copy.lineBreakBefore = false;
        StripObjectIdentity(copy);      // a format, not the object it came from
        return copy;
    }
    return {};
}

RichCharFormatState UCRichDocumentEditor::GetFormatState() const {
    using Tri = RichCharFormatState::Tri;
    RichCharFormatState state;

    auto fold = [](Tri current, bool value, bool first) {
        if (first) return value ? Tri::On : Tri::Off;
        if (current == Tri::Mixed) return Tri::Mixed;
        if ((current == Tri::On) != value) return Tri::Mixed;
        return current;
    };
    auto foldString = [](std::string& current, bool& mixed, const std::string& value, bool first) {
        if (first) { current = value; return; }
        if (!mixed && current != value) { mixed = true; current.clear(); }
    };

    auto absorb = [&](const RichTextRun& run, bool first) {
        state.bold = fold(state.bold, run.bold, first);
        state.italic = fold(state.italic, run.italic, first);
        state.underline = fold(state.underline, run.underline, first);
        state.strikethrough = fold(state.strikethrough, run.strikethrough, first);
        state.code = fold(state.code, run.code, first);
        state.subscript = fold(state.subscript, run.subscript, first);
        state.superscript = fold(state.superscript, run.superscript, first);
        foldString(state.fontFamily, state.fontFamilyMixed, run.fontFamily, first);
        foldString(state.color, state.colorMixed, run.color, first);
        foldString(state.linkTarget, state.linkMixed, run.linkTarget, first);
        if (first) {
            state.fontSizePt = run.fontSizePt;
        } else if (!state.fontSizeMixed && state.fontSizePt != run.fontSizePt) {
            state.fontSizeMixed = true;
            state.fontSizePt = 0.0f;
        }
    };

    if (!HasSelection()) {
        absorb(pendingFormatValid ? pendingFormat : FormatAt(caret), true);
        return state;
    }

    RichDocRange range = GetSelectionRange();
    bool first = true;

    // A block of cells: every run of every cell in it.
    if (HasCellSelection()) {
        for (const RichDocPosition& cell : SelectedCells()) {
            if (const std::vector<RichTextRun>* cellRuns = RunsAt(cell)) {
                for (const auto& run : *cellRuns) {
                    if (run.text.empty() && !run.lineBreakBefore && cellRuns->size() > 1) continue;
                    absorb(run, first);
                    first = false;
                }
            }
        }
        if (first) absorb(FormatAt(range.start), true);
        return state;
    }

    // Otherwise a selection involving a cell stays inside that cell, so it
    // is read straight off that cell's runs.
    if (range.start.InCell()) {
        if (const std::vector<RichTextRun>* cellRuns = RunsAt(range.start)) {
            int pos = 0;
            for (const auto& run : *cellRuns) {
                int span = RunSpan(run);
                int runStart = pos;
                int runEnd = pos + span;
                pos = runEnd;
                if (runEnd <= range.start.byteOffset || runStart >= range.end.byteOffset) continue;
                absorb(run, first);
                first = false;
            }
        }
        if (first) absorb(FormatAt(range.start), true);
        return state;
    }

    for (int b = range.start.blockIndex; b <= range.end.blockIndex && b < GetBlockCount(); b++) {
        const RichDocBlock& block = doc->blocks[b];
        if (!IsTextBlockType(block.type)) continue;

        int textLength = static_cast<int>(RunsText(block.runs).size());
        int from = (b == range.start.blockIndex) ? range.start.byteOffset : 0;
        int to   = (b == range.end.blockIndex)   ? range.end.byteOffset   : textLength;
        if (from >= to) continue;

        int pos = 0;
        for (const auto& run : block.runs) {
            int span = RunSpan(run);
            int runStart = pos;
            int runEnd = pos + span;
            pos = runEnd;
            if (runEnd <= from || runStart >= to) continue;
            absorb(run, first);
            first = false;
        }
    }
    if (first) absorb(FormatAt(range.start), true);
    return state;
}

void UCRichDocumentEditor::ToggleBold() {
    RichCharFormatDelta delta;
    delta.setBold = true;
    delta.bold = !RichCharFormatState::IsOn(GetFormatState().bold);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleItalic() {
    RichCharFormatDelta delta;
    delta.setItalic = true;
    delta.italic = !RichCharFormatState::IsOn(GetFormatState().italic);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleUnderline() {
    RichCharFormatDelta delta;
    delta.setUnderline = true;
    delta.underline = !RichCharFormatState::IsOn(GetFormatState().underline);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleStrikethrough() {
    RichCharFormatDelta delta;
    delta.setStrikethrough = true;
    delta.strikethrough = !RichCharFormatState::IsOn(GetFormatState().strikethrough);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleCode() {
    RichCharFormatDelta delta;
    delta.setCode = true;
    delta.code = !RichCharFormatState::IsOn(GetFormatState().code);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleSubscript() {
    RichCharFormatDelta delta;
    delta.setSubscript = true;
    delta.subscript = !RichCharFormatState::IsOn(GetFormatState().subscript);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ToggleSuperscript() {
    RichCharFormatDelta delta;
    delta.setSuperscript = true;
    delta.superscript = !RichCharFormatState::IsOn(GetFormatState().superscript);
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::SetFontFamily(const std::string& family) {
    RichCharFormatDelta delta;
    delta.setFontFamily = true;
    delta.fontFamily = family;
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::SetFontSize(float pt) {
    RichCharFormatDelta delta;
    delta.setFontSize = true;
    delta.fontSizePt = pt;
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::SetTextColor(const std::string& hexColor) {
    RichCharFormatDelta delta;
    delta.setColor = true;
    delta.color = hexColor;
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::SetLink(const std::string& target) {
    RichCharFormatDelta delta;
    delta.setLink = true;
    delta.linkTarget = target;
    ApplyCharFormat(delta);
}

void UCRichDocumentEditor::ClearFormatting() {
    RichCharFormatDelta delta;
    delta.setBold = delta.setItalic = delta.setUnderline = true;
    delta.setStrikethrough = delta.setCode = true;
    delta.setSubscript = delta.setSuperscript = true;
    delta.setFontFamily = delta.setFontSize = delta.setColor = true;
    delta.bold = delta.italic = delta.underline = false;
    delta.strikethrough = delta.code = false;
    delta.subscript = delta.superscript = false;
    delta.fontSizePt = 0.0f;
    ApplyCharFormat(delta);
}

// ===== BLOCK FORMATTING =====

void UCRichDocumentEditor::SetBlockType(RichBlockType type, int headingLevel) {
    // A table cell holds runs and nothing else: RichTableCell carries no
    // paragraph properties, so there is nowhere to record a heading, a list, an
    // alignment or a quote for one. Applying the command to the enclosing table
    // block instead would silently re-align or restyle the whole table, which
    // is not what a caret sitting in one cell asks for.
    if (caret.InCell()) return;
    switch (type) {
        case RichBlockType::Paragraph:
        case RichBlockType::Heading:
        case RichBlockType::ListItem:
        case RichBlockType::CodeBlock:
        case RichBlockType::BlockQuote:
            break;
        default:
            return;     // structural blocks are inserted, never converted into
    }

    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (!IsTextBlockType(block.type)) continue;
            block.type = type;
            if (type == RichBlockType::Heading) {
                block.headingLevel = std::max(1, std::min(headingLevel == 0 ? 1 : headingLevel, 6));
            } else {
                block.headingLevel = 0;
            }
            if (type != RichBlockType::ListItem) {
                block.orderedList = false;
                block.listLevel = 0;
                block.checkbox = block.checked = false;
            }
            if (type != RichBlockType::CodeBlock) block.codeLanguage.clear();
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::SetHeadingLevel(int level) {
    if (level <= 0) {
        SetBlockType(RichBlockType::Paragraph);
    } else {
        SetBlockType(RichBlockType::Heading, level);
    }
}

void UCRichDocumentEditor::SetAlignment(RichTextAlign align) {
    // In a table the alignment is the cell's own (RichTableCell::align): the
    // caret's cell, or every cell of a cell selection.
    if (caret.InCell()) {
        std::vector<RichDocPosition> cells = HasCellSelection() ? SelectedCells()
                                                                 : std::vector<RichDocPosition>{caret};
        {
            EditScope scope(*this, caret.blockIndex, 1);
            RichDocBlock& table = doc->blocks[static_cast<size_t>(caret.blockIndex)];
            for (const RichDocPosition& cell : cells) {
                if (cell.cellRow < 0 || cell.cellRow >= static_cast<int>(table.tableRows.size())) continue;
                RichTableRow& row = table.tableRows[static_cast<size_t>(cell.cellRow)];
                if (cell.cellColumn < 0 || cell.cellColumn >= static_cast<int>(row.cells.size())) continue;
                row.cells[static_cast<size_t>(cell.cellColumn)].align = align;
            }
        }
        NotifyChanged();
        return;
    }
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            doc->blocks[b].align = align;
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::SetListStyle(bool ordered) {
    // A table cell holds runs and nothing else: RichTableCell carries no
    // paragraph properties, so there is nowhere to record a heading, a list, an
    // alignment or a quote for one. Applying the command to the enclosing table
    // block instead would silently re-align or restyle the whole table, which
    // is not what a caret sitting in one cell asks for.
    if (caret.InCell()) return;
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (!IsTextBlockType(block.type)) continue;
            if (block.type != RichBlockType::ListItem) {
                block.listLevel = 0;
                block.headingLevel = 0;
            }
            block.type = RichBlockType::ListItem;
            block.orderedList = ordered;
            block.checkbox = block.checked = false;
            AdoptListLevelFormat(doc->blocks, static_cast<size_t>(b));
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::ToggleList(bool ordered) {
    // A table cell holds runs and nothing else: RichTableCell carries no
    // paragraph properties, so there is nowhere to record a heading, a list, an
    // alignment or a quote for one. Applying the command to the enclosing table
    // block instead would silently re-align or restyle the whole table, which
    // is not what a caret sitting in one cell asks for.
    if (caret.InCell()) return;
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    bool allSameList = true;
    for (int b = first; b <= last && b < GetBlockCount(); b++) {
        const RichDocBlock& block = doc->blocks[b];
        if (!IsTextBlockType(block.type)) continue;
        if (block.type != RichBlockType::ListItem || block.orderedList != ordered || block.checkbox) {
            allSameList = false;
            break;
        }
    }
    if (allSameList) {
        SetBlockType(RichBlockType::Paragraph);
    } else {
        SetListStyle(ordered);
    }
}

void UCRichDocumentEditor::IndentList() {
    // A table cell holds runs and nothing else: RichTableCell carries no
    // paragraph properties, so there is nowhere to record a heading, a list, an
    // alignment or a quote for one. Applying the command to the enclosing table
    // block instead would silently re-align or restyle the whole table, which
    // is not what a caret sitting in one cell asks for.
    if (caret.InCell()) return;
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (block.type == RichBlockType::ListItem && block.listLevel < 8) {
                block.listLevel++;
                AdoptListLevelFormat(doc->blocks, static_cast<size_t>(b));
            }
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::OutdentList() {
    // A table cell holds runs and nothing else: RichTableCell carries no
    // paragraph properties, so there is nowhere to record a heading, a list, an
    // alignment or a quote for one. Applying the command to the enclosing table
    // block instead would silently re-align or restyle the whole table, which
    // is not what a caret sitting in one cell asks for.
    if (caret.InCell()) return;
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (block.type != RichBlockType::ListItem) continue;
            if (block.listLevel > 0) {
                block.listLevel--;
                AdoptListLevelFormat(doc->blocks, static_cast<size_t>(b));
            } else {
                block.type = RichBlockType::Paragraph;
                block.orderedList = false;
                block.checkbox = block.checked = false;
            }
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::ToggleBlockQuote() {
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    bool allQuoted = true;
    for (int b = first; b <= last && b < GetBlockCount(); b++) {
        if (doc->blocks[b].type != RichBlockType::BlockQuote) { allQuoted = false; break; }
    }
    SetBlockType(allQuoted ? RichBlockType::Paragraph : RichBlockType::BlockQuote);
}

void UCRichDocumentEditor::ToggleCodeBlock(const std::string& language) {
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    bool allCode = true;
    for (int b = first; b <= last && b < GetBlockCount(); b++) {
        if (doc->blocks[b].type != RichBlockType::CodeBlock) { allCode = false; break; }
    }
    if (allCode) {
        SetBlockType(RichBlockType::Paragraph);
        return;
    }
    SetBlockType(RichBlockType::CodeBlock);
    if (language.empty()) return;
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            if (doc->blocks[b].type == RichBlockType::CodeBlock) {
                doc->blocks[b].codeLanguage = language;
            }
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::ToggleCheckList() {
    if (caret.InCell()) return;
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    bool allChecklist = true;
    for (int b = first; b <= last && b < GetBlockCount(); b++) {
        const RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
        if (!IsTextBlockType(block.type)) continue;
        if (block.type != RichBlockType::ListItem || !block.checkbox) { allChecklist = false; break; }
    }
    if (allChecklist) {
        SetBlockType(RichBlockType::Paragraph);
        return;
    }
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
            if (!IsTextBlockType(block.type)) continue;
            if (block.type != RichBlockType::ListItem) {
                block.listLevel = 0;
                block.headingLevel = 0;
            }
            block.type = RichBlockType::ListItem;
            block.orderedList = false;
            if (!block.checkbox) block.checked = false;
            block.checkbox = true;
        }
    }
    NotifyChanged();
}

bool UCRichDocumentEditor::ToggleChecked(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return false;
    const RichDocBlock& current = doc->blocks[static_cast<size_t>(blockIndex)];
    if (current.type != RichBlockType::ListItem || !current.checkbox) return false;
    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& block = doc->blocks[static_cast<size_t>(blockIndex)];
        block.checked = !block.checked;
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

// ===== NAMED STYLES =====

namespace {

// Sets property `value` to what `after` states, or takes back what `before`
// stated: with force, `after`'s value is set regardless; otherwise only a
// value still equal to `before`'s (not formatted directly) changes.
template <typename T, typename D>
void Restyle(T& value, const std::optional<D>& before, const std::optional<D>& after, bool force, const T& none) {
    const bool following = before ? value == static_cast<T>(*before) : false;
    if (after) {
        if (force || following || !before) value = static_cast<T>(*after);
    } else if (following) {
        value = none;
    }
}

void RestyleRun(RichTextRun& run, const RichStyleCharacter& before, const RichStyleCharacter& after, bool force) {
    if (run.IsInlineImage()) return;
    Restyle(run.bold, before.bold, after.bold, force, false);
    Restyle(run.italic, before.italic, after.italic, force, false);
    Restyle(run.underline, before.underline, after.underline, force, false);
    Restyle(run.strikethrough, before.strikethrough, after.strikethrough, force, false);
    Restyle(run.code, before.code, after.code, force, false);
    Restyle(run.fontFamily, before.fontFamily, after.fontFamily, force, std::string());
    Restyle(run.fontSizePt, before.fontSizePt, after.fontSizePt, force, 0.0f);
    Restyle(run.color, before.color, after.color, force, std::string());
    Restyle(run.highlightColor, before.highlightColor, after.highlightColor, force, std::string());
}

} // namespace

void UCRichDocumentEditor::RestyleBlock(RichDocBlock& block, const RichStyle& before, const RichStyle& after,
                                        bool force) const {
    const RichStyleParagraph& a = after.paragraph;
    const RichStyleParagraph& b = before.paragraph;
    if (block.type == RichBlockType::Paragraph || block.type == RichBlockType::Heading) {
        // Headings are a style's outline level.
        int level = block.type == RichBlockType::Heading ? block.headingLevel : 0;
        Restyle(level, b.headingLevel, a.headingLevel, force, 0);
        if (level >= 1 && level <= 6) {
            block.type = RichBlockType::Heading;
            block.headingLevel = level;
        } else {
            block.type = RichBlockType::Paragraph;
            block.headingLevel = 0;
        }
    }
    Restyle(block.align, b.align, a.align, force, RichTextAlign::Default);
    Restyle(block.leftIndentPt, b.leftIndentPt, a.leftIndentPt, force, 0.0f);
    Restyle(block.rightIndentPt, b.rightIndentPt, a.rightIndentPt, force, 0.0f);
    Restyle(block.firstLineIndentPt, b.firstLineIndentPt, a.firstLineIndentPt, force, 0.0f);
    Restyle(block.spaceBeforePt, b.spaceBeforePt, a.spaceBeforePt, force, -1.0f);
    Restyle(block.spaceAfterPt, b.spaceAfterPt, a.spaceAfterPt, force, -1.0f);
    Restyle(block.lineSpacing, b.lineSpacing, a.lineSpacing, force, 0.0f);
    for (RichTextRun& run : block.runs) {
        // A run with a character style of its own keeps that style's look.
        if (!run.characterStyleId.empty()) continue;
        RestyleRun(run, before.character, after.character, force);
    }
}

void UCRichDocumentEditor::EnsureStyles() {
    if (doc->styles.empty()) doc->styles = UCRichDocument::DefaultStyles();
}

std::vector<RichStyle> UCRichDocumentEditor::GetStyles() const {
    return doc->styles.empty() ? UCRichDocument::DefaultStyles() : doc->styles;
}

bool UCRichDocumentEditor::ApplyParagraphStyle(const std::string& id) {
    if (caret.InCell()) return false;
    const bool hadStyles = !doc->styles.empty();
    if (!hadStyles && !UCRichDocument::DefaultStyles().empty()) {
        // Checked against the defaults before they are added, so an unknown
        // id changes nothing.
        bool known = false;
        for (const RichStyle& style : UCRichDocument::DefaultStyles()) known = known || style.id == id;
        if (!known) return false;
    } else if (!doc->FindStyle(id)) {
        return false;
    }
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        if (!hadStyles) {
            scope.CaptureStyles();
            EnsureStyles();
        }
        const RichStyle after = doc->ResolveStyle(id);
        if (after.kind != RichStyle::Kind::Paragraph) return false;
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
            if (!IsTextBlockType(block.type)) continue;
            const RichStyle before = doc->ResolveStyle(EffectiveStyleId(block));
            RestyleBlock(block, before, after, /*force*/ true);
            block.styleId = id == "Normal" ? std::string() : id;
        }
    }
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::ApplyCharacterStyle(const std::string& id) {
    if (!id.empty()) {
        const RichStyle* known = doc->FindStyle(id);
        if (!known && doc->styles.empty()) {
            for (const RichStyle& style : UCRichDocument::DefaultStyles()) {
                if (style.id == id) { known = &style; break; }
            }
            if (!known) return false;
        } else if (!known) {
            return false;
        }
    }
    if (!HasSelection()) return false;
    const RichDocRange range = GetSelectionRange();
    {
        EditScope scope(*this, range.start.blockIndex, range.end.blockIndex - range.start.blockIndex + 1);
        if (doc->styles.empty() && !id.empty()) {
            scope.CaptureStyles();
            EnsureStyles();
        }
        const RichStyle after = id.empty() ? RichStyle{} : doc->ResolveStyle(id);
        auto restyle = [&](std::vector<RichTextRun>& runs, int from, int to) {
            if (from >= to) return;
            const int startIdx = SplitRunAt(runs, from);
            const int endIdx = SplitRunAt(runs, to);
            for (int i = startIdx; i < endIdx && i < static_cast<int>(runs.size()); i++) {
                RichTextRun& run = runs[static_cast<size_t>(i)];
                const RichStyle before = run.characterStyleId.empty() ? RichStyle{}
                                                                      : doc->ResolveStyle(run.characterStyleId);
                RestyleRun(run, before.character, after.character, /*force*/ true);
                run.characterStyleId = id;
            }
            CoalesceRuns(runs);
        };
        int top = 0, left = 0, bottom = 0, right = 0;
        if (CellRectBetween(range.start, range.end, top, left, bottom, right)) {
            for (const RichDocPosition& cell : SelectedCells()) {
                std::vector<RichTextRun>* runs = MutableRunsAt(cell);
                if (runs) restyle(*runs, 0, static_cast<int>(RunsText(*runs).size()));
            }
        } else if (range.start.InCell()) {
            if (std::vector<RichTextRun>* runs = MutableRunsAt(range.start)) {
                restyle(*runs, range.start.byteOffset, range.end.byteOffset);
            }
        } else {
            for (int b = range.start.blockIndex; b <= range.end.blockIndex && b < GetBlockCount(); b++) {
                RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
                if (!IsTextBlockType(block.type)) continue;
                const int length = static_cast<int>(RunsText(block.runs).size());
                const int from = b == range.start.blockIndex ? range.start.byteOffset : 0;
                const int to = b == range.end.blockIndex ? range.end.byteOffset : length;
                restyle(block.runs, std::clamp(from, 0, length), std::clamp(to, 0, length));
            }
        }
    }
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::UpdateStyle(const RichStyle& style) {
    if (style.id.empty()) return false;
    {
        EditScope scope(*this, 0, GetBlockCount());
        scope.CaptureStyles();
        EnsureStyles();
        // What every style resolves to now, for each one the change reaches.
        std::vector<std::pair<std::string, RichStyle>> before;
        for (const RichStyle& existing : doc->styles) {
            if (doc->StyleDerivesFrom(existing.id, style.id)) before.emplace_back(existing.id, doc->ResolveStyle(existing.id));
        }
        bool replaced = false;
        for (RichStyle& existing : doc->styles) {
            if (existing.id == style.id) {
                existing = style;
                replaced = true;
            }
        }
        if (!replaced) doc->styles.push_back(style);
        auto resolvedBefore = [&](const std::string& id) -> const RichStyle* {
            for (const auto& [known, resolved] : before) if (known == id) return &resolved;
            return nullptr;
        };
        for (RichDocBlock& block : doc->blocks) {
            const std::string id = EffectiveStyleId(block);
            if (const RichStyle* old = resolvedBefore(id); old && style.kind == RichStyle::Kind::Paragraph) {
                RestyleBlock(block, *old, doc->ResolveStyle(id), /*force*/ false);
            }
            auto runsOf = [&](std::vector<RichTextRun>& runs) {
                for (RichTextRun& run : runs) {
                    if (run.characterStyleId.empty()) continue;
                    if (const RichStyle* old = resolvedBefore(run.characterStyleId)) {
                        RestyleRun(run, old->character, doc->ResolveStyle(run.characterStyleId).character, false);
                    }
                }
            };
            runsOf(block.runs);
            for (RichTableRow& row : block.tableRows) {
                for (RichTableCell& cell : row.cells) runsOf(cell.runs);
            }
        }
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::DeleteStyle(const std::string& id) {
    const RichStyle* existing = doc->FindStyle(id);
    if (!existing || id == "Normal") return false;
    const std::string parent = existing->basedOn;
    const RichStyle::Kind kind = existing->kind;
    {
        EditScope scope(*this, 0, GetBlockCount());
        scope.CaptureStyles();
        const RichStyle before = doc->ResolveStyle(id);
        doc->styles.erase(std::remove_if(doc->styles.begin(), doc->styles.end(),
                                         [&](const RichStyle& s) { return s.id == id; }),
                          doc->styles.end());
        // Styles based on it are based on its parent now.
        for (RichStyle& other : doc->styles) {
            if (other.basedOn == id) other.basedOn = parent;
        }
        const RichStyle after = parent.empty() ? RichStyle{} : doc->ResolveStyle(parent);
        for (RichDocBlock& block : doc->blocks) {
            if (kind == RichStyle::Kind::Paragraph && block.styleId == id) {
                RestyleBlock(block, before, after, false);
                block.styleId = parent == "Normal" ? std::string() : parent;
            }
            auto runsOf = [&](std::vector<RichTextRun>& runs) {
                for (RichTextRun& run : runs) {
                    if (run.characterStyleId != id) continue;
                    RestyleRun(run, before.character, after.character, false);
                    run.characterStyleId = kind == RichStyle::Kind::Character ? parent : std::string();
                }
            };
            runsOf(block.runs);
            for (RichTableRow& row : block.tableRows) {
                for (RichTableCell& cell : row.cells) runsOf(cell.runs);
            }
        }
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

RichStyle UCRichDocumentEditor::StyleFromCaret(const std::string& id, const std::string& name) const {
    RichStyle style;
    style.id = id;
    style.name = name.empty() ? id : name;
    style.basedOn = "Normal";
    if (caret.InCell() || caret.blockIndex < 0 || caret.blockIndex >= GetBlockCount()) return style;
    const RichDocBlock& block = doc->blocks[static_cast<size_t>(caret.blockIndex)];
    if (block.align != RichTextAlign::Default) style.paragraph.align = block.align;
    if (block.leftIndentPt != 0.0f) style.paragraph.leftIndentPt = block.leftIndentPt;
    if (block.rightIndentPt != 0.0f) style.paragraph.rightIndentPt = block.rightIndentPt;
    if (block.firstLineIndentPt != 0.0f) style.paragraph.firstLineIndentPt = block.firstLineIndentPt;
    if (block.spaceBeforePt >= 0.0f) style.paragraph.spaceBeforePt = block.spaceBeforePt;
    if (block.spaceAfterPt >= 0.0f) style.paragraph.spaceAfterPt = block.spaceAfterPt;
    if (block.lineSpacing > 0.0f) style.paragraph.lineSpacing = block.lineSpacing;
    if (block.type == RichBlockType::Heading) style.paragraph.headingLevel = block.headingLevel;
    const RichTextRun format = FormatAt(caret);
    if (format.bold) style.character.bold = true;
    if (format.italic) style.character.italic = true;
    if (format.underline) style.character.underline = true;
    if (format.strikethrough) style.character.strikethrough = true;
    if (!format.fontFamily.empty()) style.character.fontFamily = format.fontFamily;
    if (format.fontSizePt > 0.0f) style.character.fontSizePt = format.fontSizePt;
    if (!format.color.empty()) style.character.color = format.color;
    return style;
}

std::string UCRichDocumentEditor::CurrentParagraphStyle() const {
    if (caret.blockIndex < 0 || caret.blockIndex >= GetBlockCount()) return "Normal";
    return EffectiveStyleId(doc->blocks[static_cast<size_t>(caret.blockIndex)]);
}

std::string UCRichDocumentEditor::CurrentCharacterStyle() const {
    const std::vector<RichTextRun>* runs = RunsAt(caret);
    if (!runs) return {};
    const RichTextRun* run = RunAtOffset(*runs, caret.byteOffset);
    return run ? run->characterStyleId : std::string();
}

// ===== STRUCTURE =====

void UCRichDocumentEditor::InsertStructuralBlock(RichBlockType type) {
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);

        RichDocBlock block;
        block.type = type;

        RichDocBlock& current = doc->blocks[caret.blockIndex];
        bool currentIsEmpty = IsTextBlockType(current.type) && RunsText(current.runs).empty();
        if (currentIsEmpty) {
            // Replace the empty paragraph the caret sits in, then leave a fresh
            // one after the inserted block so there is somewhere to type.
            doc->blocks[caret.blockIndex] = block;
        } else {
            SplitBlockInternal();
            doc->blocks.insert(doc->blocks.begin() + caret.blockIndex, block);
        }
        // Either way the structural block now sits at caret.blockIndex: the
        // empty paragraph was replaced by it, or it was inserted ahead of the
        // tail the split produced.
        int afterIndex = caret.blockIndex + 1;
        if (afterIndex >= GetBlockCount() || !IsTextBlock(afterIndex)) {
            RichDocBlock paragraph;
            paragraph.type = RichBlockType::Paragraph;
            doc->blocks.insert(doc->blocks.begin() + afterIndex, paragraph);
        }
        caret = ClampPosition({afterIndex, 0});
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
}

// ===== TABLES =====

namespace {

RichTableCell FreshCell() {
    return RichTableCell{};
}

// An empty cell formatted like the cell covering (row, gridColumn), if any:
// a row or column added to a table with a document's borders gets the same
// frame and fill as its neighbours instead of a gap in the lines.
RichTableCell FreshCellLike(const RichDocBlock& table, int row, int gridColumn) {
    RichTableCell cell = FreshCell();
    const RichTableGrid grid = BuildTableGrid(table);
    if (grid.rowCount == 0 || grid.columnCount == 0) return cell;
    row = std::clamp(row, 0, grid.rowCount - 1);
    gridColumn = std::clamp(gridColumn, 0, grid.columnCount - 1);
    int ownerRow = 0, ownerCell = 0;
    if (grid.CellAt(row, gridColumn, ownerRow, ownerCell)) {
        cell.CopyCellFormat(table.tableRows[static_cast<size_t>(ownerRow)]
                                .cells[static_cast<size_t>(ownerCell)]);
    }
    return cell;
}

// Index in `row`'s cell vector at which a cell starting at `gridColumn`
// belongs: after every cell of that row whose own column is to its left.
int CellInsertIndexForColumn(const RichTableGrid& grid, int row, int gridColumn) {
    int index = 0;
    for (int c = 0; c < gridColumn && c < grid.columnCount; ++c) {
        const RichTableGridSlot& slot = grid.At(row, c);
        if (slot.origin && slot.row == row) index++;
    }
    return index;
}

// Puts an empty 1x1 cell into the slot (row, gridColumn), which must be free.
void InsertFreshCellAt(RichDocBlock& table, int row, int gridColumn) {
    const RichTableGrid grid = BuildTableGrid(table);
    if (row < 0 || row >= static_cast<int>(table.tableRows.size())) return;
    RichTableRow& modelRow = table.tableRows[static_cast<size_t>(row)];
    const int at = std::min(CellInsertIndexForColumn(grid, row, gridColumn),
                            static_cast<int>(modelRow.cells.size()));
    // Formatted like its left neighbour (or the right one at the left edge).
    RichTableCell cell = FreshCellLike(table, row, gridColumn > 0 ? gridColumn - 1 : gridColumn + 1);
    modelRow.cells.insert(modelRow.cells.begin() + at, std::move(cell));
}

} // namespace

RichTableGrid UCRichDocumentEditor::TableGrid(int blockIndex) const {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return RichTableGrid{};
    return BuildTableGrid(doc->blocks[static_cast<size_t>(blockIndex)]);
}

bool UCRichDocumentEditor::CaretGridPosition(int& outRow, int& outColumn) const {
    if (!caret.InCell()) return false;
    const RichTableGrid grid = TableGrid(caret.blockIndex);
    return grid.OriginOf(caret.cellRow, caret.cellColumn, outRow, outColumn);
}

int UCRichDocumentEditor::InsertTable(int rows, int columns, bool headerRow) {
    if (rows < 1 || columns < 1) return -1;

    RichDocBlock table;
    table.type = RichBlockType::Table;
    for (int r = 0; r < rows; ++r) {
        RichTableRow row;
        row.header = (headerRow && r == 0);
        for (int c = 0; c < columns; ++c) row.cells.push_back(FreshCell());
        table.tableRows.push_back(std::move(row));
    }

    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    int tableIndex = -1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);

        RichDocBlock& current = doc->blocks[static_cast<size_t>(caret.blockIndex)];
        const bool currentIsEmpty = IsTextBlockType(current.type) && RunsText(current.runs).empty();
        if (currentIsEmpty) {
            doc->blocks[static_cast<size_t>(caret.blockIndex)] = table;
        } else {
            SplitBlockInternal();
            doc->blocks.insert(doc->blocks.begin() + caret.blockIndex, table);
        }
        tableIndex = caret.blockIndex;

        // A table is not something you can type after unless a paragraph
        // follows it, and a document ending in one would trap the caret.
        const int afterIndex = tableIndex + 1;
        if (afterIndex >= GetBlockCount() || !IsTextBlock(afterIndex)) {
            RichDocBlock paragraph;
            paragraph.type = RichBlockType::Paragraph;
            doc->blocks.insert(doc->blocks.begin() + afterIndex, paragraph);
        }
        // Unlike a rule or a page break, a table is something you fill in, so
        // the caret goes into its first cell rather than past it.
        caret = RichDocPosition(tableIndex, 0, 0, 0);
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
    return tableIndex;
}

bool UCRichDocumentEditor::InsertTableRow(int blockIndex, int row, bool below) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.rowCount == 0) return false;
    if (row < 0 || row >= grid.rowCount) return false;

    const int newRow = below ? row + 1 : row;
    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];

        // A cell reaching across the new boundary grows by a row instead of
        // being split: its text lives in one place and cannot be in two rows.
        std::vector<bool> coveredColumn(static_cast<size_t>(grid.columnCount), false);
        std::vector<std::pair<int, int>> growing;       // (row, cellIndex), deduped
        for (int c = 0; c < grid.columnCount; ++c) {
            const RichTableGridSlot& slot = grid.At(newRow - 1, c);
            if (!slot.Occupied()) continue;
            const RichTableCell& cell = table.tableRows[static_cast<size_t>(slot.row)]
                                             .cells[static_cast<size_t>(slot.cellIndex)];
            const int lastRow = slot.row + std::max(1, cell.rowSpan) - 1;
            if (slot.row >= newRow || lastRow < newRow) continue;
            coveredColumn[static_cast<size_t>(c)] = true;
            const std::pair<int, int> id{slot.row, slot.cellIndex};
            if (std::find(growing.begin(), growing.end(), id) == growing.end()) {
                growing.push_back(id);
            }
        }
        for (const auto& [r, cellIndex] : growing) {
            RichTableCell& cell = table.tableRows[static_cast<size_t>(r)]
                                       .cells[static_cast<size_t>(cellIndex)];
            cell.rowSpan = std::max(1, cell.rowSpan) + 1;
        }

        // The new row supplies cells only for the columns no span covers.
        RichTableRow fresh;
        for (int c = 0; c < grid.columnCount; ++c) {
            // Formatted like the cell of the row it was inserted next to.
            if (!coveredColumn[static_cast<size_t>(c)]) fresh.cells.push_back(FreshCellLike(table, row, c));
        }
        table.tableRows.insert(table.tableRows.begin() + newRow, std::move(fresh));

        // A caret below the insertion point is now a row further down.
        if (caret.blockIndex == blockIndex && caret.InCell() && caret.cellRow >= newRow) {
            caret.cellRow++;
            anchor = caret;
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::DeleteTableRow(int blockIndex, int row) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.rowCount == 0) return false;
    if (row < 0 || row >= grid.rowCount) return false;

    // The last row taking the table with it is the only sane end state: a table
    // with no rows has no cell to put the caret in.
    if (grid.rowCount == 1) {
        DeleteBlock(blockIndex);
        return true;
    }

    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];

        // Cells starting in this row but reaching below it have to survive, so
        // they move into the next row one span shorter. Collected first, in
        // column order, because moving them invalidates cell indices.
        struct Moving { int column; RichTableCell cell; };
        std::vector<Moving> moving;
        std::vector<std::pair<int, int>> shrinking;     // spans crossing from above

        for (int c = 0; c < grid.columnCount; ++c) {
            const RichTableGridSlot& slot = grid.At(row, c);
            if (!slot.Occupied() || !slot.origin) {
                if (slot.Occupied() && slot.row < row) {
                    const std::pair<int, int> id{slot.row, slot.cellIndex};
                    if (std::find(shrinking.begin(), shrinking.end(), id) == shrinking.end()) {
                        shrinking.push_back(id);
                    }
                }
                continue;
            }
            const RichTableCell& cell = table.tableRows[static_cast<size_t>(row)]
                                             .cells[static_cast<size_t>(slot.cellIndex)];
            if (std::max(1, cell.rowSpan) > 1) {
                RichTableCell moved = cell;
                moved.rowSpan = std::max(1, cell.rowSpan) - 1;
                moving.push_back(Moving{c, std::move(moved)});
            }
        }
        for (const auto& [r, cellIndex] : shrinking) {
            RichTableCell& cell = table.tableRows[static_cast<size_t>(r)]
                                       .cells[static_cast<size_t>(cellIndex)];
            cell.rowSpan = std::max(1, std::max(1, cell.rowSpan) - 1);
        }

        table.tableRows.erase(table.tableRows.begin() + row);

        // Re-home the survivors into what is now `row`, each at the index its
        // column calls for. Rightmost first so earlier insertions do not shift
        // the indices computed for later ones.
        for (auto it = moving.rbegin(); it != moving.rend(); ++it) {
            const RichTableGrid after = BuildTableGrid(table);
            RichTableRow& target = table.tableRows[static_cast<size_t>(row)];
            const int at = std::min(CellInsertIndexForColumn(after, row, it->column),
                                    static_cast<int>(target.cells.size()));
            target.cells.insert(target.cells.begin() + at, it->cell);
        }

        if (caret.blockIndex == blockIndex && caret.InCell()) {
            if (caret.cellRow > row) caret.cellRow--;
            caret = ClampPosition(caret);
            anchor = caret;
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::InsertTableColumn(int blockIndex, int gridColumn, bool right) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.columnCount == 0) return false;
    if (gridColumn < 0 || gridColumn >= grid.columnCount) return false;

    const int newColumn = right ? gridColumn + 1 : gridColumn;
    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];

        // Walk bottom-up so the fresh cells inserted for one row never shift
        // the indices the grid reported for another.
        for (int r = grid.rowCount - 1; r >= 0; --r) {
            const RichTableGridSlot& slot = grid.At(r, newColumn - 1);
            bool grew = false;
            if (slot.Occupied()) {
                RichTableCell& cell = table.tableRows[static_cast<size_t>(slot.row)]
                                           .cells[static_cast<size_t>(slot.cellIndex)];
                int originRow = 0, originColumn = 0;
                if (grid.OriginOf(slot.row, slot.cellIndex, originRow, originColumn)) {
                    const int lastColumn = originColumn + std::max(1, cell.columnSpan) - 1;
                    if (originColumn < newColumn && lastColumn >= newColumn) {
                        // Only the row that owns the cell may grow it, or a cell
                        // spanning three rows would be widened three times.
                        if (slot.row == r) cell.columnSpan = std::max(1, cell.columnSpan) + 1;
                        grew = true;
                    }
                }
            }
            if (!grew) InsertFreshCellAt(table, r, newColumn);
        }
        // The new column takes the width of the one it was inserted beside,
        // so the document's own column proportions survive the edit.
        std::vector<float>& widths = table.tableColumnWidths;
        if (static_cast<int>(widths.size()) == grid.columnCount) {
            widths.insert(widths.begin() + newColumn, widths[static_cast<size_t>(gridColumn)]);
        }

        if (caret.blockIndex == blockIndex && caret.InCell()) {
            // The caret's cell may have gained an index if a fresh cell landed
            // to its left in the same row.
            const RichTableGrid after = BuildTableGrid(table);
            int caretColumn = 0, caretRow = 0;
            if (grid.OriginOf(caret.cellRow, caret.cellColumn, caretRow, caretColumn)) {
                const int shifted = caretColumn >= newColumn ? caretColumn + 1 : caretColumn;
                int ownerRow = 0, ownerCell = 0;
                if (after.CellAt(caret.cellRow, shifted, ownerRow, ownerCell)) {
                    caret.cellRow = ownerRow;
                    caret.cellColumn = ownerCell;
                }
            }
            caret = ClampPosition(caret);
            anchor = caret;
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::DeleteTableColumn(int blockIndex, int gridColumn) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.columnCount == 0) return false;
    if (gridColumn < 0 || gridColumn >= grid.columnCount) return false;

    if (grid.columnCount == 1) {
        DeleteBlock(blockIndex);
        return true;
    }

    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];

        std::vector<std::pair<int, int>> narrowing;  // spans that lose a column
        std::vector<std::pair<int, int>> removing;   // cells that go entirely
        for (int r = 0; r < grid.rowCount; ++r) {
            const RichTableGridSlot& slot = grid.At(r, gridColumn);
            if (!slot.Occupied()) continue;
            const RichTableCell& cell = table.tableRows[static_cast<size_t>(slot.row)]
                                             .cells[static_cast<size_t>(slot.cellIndex)];
            const std::pair<int, int> id{slot.row, slot.cellIndex};
            auto& target = std::max(1, cell.columnSpan) > 1 ? narrowing : removing;
            if (std::find(target.begin(), target.end(), id) == target.end()) {
                target.push_back(id);
            }
        }
        std::vector<float>& widths = table.tableColumnWidths;
        if (static_cast<int>(widths.size()) == grid.columnCount) {
            widths.erase(widths.begin() + gridColumn);
        }
        for (const auto& [r, cellIndex] : narrowing) {
            RichTableCell& cell = table.tableRows[static_cast<size_t>(r)]
                                       .cells[static_cast<size_t>(cellIndex)];
            cell.columnSpan = std::max(1, std::max(1, cell.columnSpan) - 1);
        }
        // Highest index first: erasing a cell shifts everything after it.
        std::sort(removing.begin(), removing.end(),
                  [](const auto& a, const auto& b) {
                      return a.first != b.first ? a.first < b.first : a.second > b.second;
                  });
        for (const auto& [r, cellIndex] : removing) {
            RichTableRow& modelRow = table.tableRows[static_cast<size_t>(r)];
            if (cellIndex < static_cast<int>(modelRow.cells.size())) {
                modelRow.cells.erase(modelRow.cells.begin() + cellIndex);
            }
        }
        // A row left with no cells at all is a row with nothing in it.
        table.tableRows.erase(
            std::remove_if(table.tableRows.begin(), table.tableRows.end(),
                           [](const RichTableRow& r) { return r.cells.empty(); }),
            table.tableRows.end());

        if (table.tableRows.empty()) {
            doc->blocks.erase(doc->blocks.begin() + blockIndex);
            EnsureNotEmpty();
            caret = ClampPosition(RichDocPosition(std::min(blockIndex, GetBlockCount() - 1), 0));
            anchor = caret;
        } else if (caret.blockIndex == blockIndex && caret.InCell()) {
            caret = ClampPosition(caret);
            anchor = caret;
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::MergeTableCells(int blockIndex, int row, int cellIndex,
                                           int extraColumns, int extraRows) {
    if (extraColumns < 0 || extraRows < 0) return false;
    if (extraColumns == 0 && extraRows == 0) return false;

    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.rowCount == 0) return false;
    int top = 0, left = 0;
    if (!grid.OriginOf(row, cellIndex, top, left)) return false;

    const RichDocBlock& readOnly = doc->blocks[static_cast<size_t>(blockIndex)];
    const RichTableCell& anchorCell = readOnly.tableRows[static_cast<size_t>(row)]
                                              .cells[static_cast<size_t>(cellIndex)];
    const int bottom = top + std::max(1, anchorCell.rowSpan) - 1 + extraRows;
    const int rightColumn = left + std::max(1, anchorCell.columnSpan) - 1 + extraColumns;
    if (bottom >= grid.rowCount || rightColumn >= grid.columnCount) return false;

    // Every cell the rectangle touches must sit entirely inside it. Otherwise
    // the merge would need half of somebody else's span, which the model cannot
    // express - so it is refused rather than approximated.
    std::vector<std::pair<int, int>> absorbed;
    for (int r = top; r <= bottom; ++r) {
        for (int c = left; c <= rightColumn; ++c) {
            const RichTableGridSlot& slot = grid.At(r, c);
            if (!slot.Occupied()) return false;         // ragged row: nothing to merge with
            const RichTableCell& cell = readOnly.tableRows[static_cast<size_t>(slot.row)]
                                                .cells[static_cast<size_t>(slot.cellIndex)];
            int cellTop = 0, cellLeft = 0;
            if (!grid.OriginOf(slot.row, slot.cellIndex, cellTop, cellLeft)) return false;
            const int cellBottom = cellTop + std::max(1, cell.rowSpan) - 1;
            const int cellRight = cellLeft + std::max(1, cell.columnSpan) - 1;
            if (cellTop < top || cellLeft < left || cellBottom > bottom || cellRight > rightColumn) {
                return false;
            }
            const std::pair<int, int> id{slot.row, slot.cellIndex};
            if (id != std::pair<int, int>{row, cellIndex} &&
                std::find(absorbed.begin(), absorbed.end(), id) == absorbed.end()) {
                absorbed.push_back(id);
            }
        }
    }

    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];
        RichTableCell& keep = table.tableRows[static_cast<size_t>(row)]
                                   .cells[static_cast<size_t>(cellIndex)];

        // The absorbed cells' text is appended rather than discarded: merging
        // cells is a layout decision, and losing what somebody wrote in them
        // would be a silent deletion.
        for (const auto& [r, ci] : absorbed) {
            const RichTableCell& source = table.tableRows[static_cast<size_t>(r)]
                                               .cells[static_cast<size_t>(ci)];
            if (RunsText(source.runs).empty()) continue;
            bool first = true;
            for (const RichTextRun& run : source.runs) {
                RichTextRun copy = run;
                if (first) {
                    copy.lineBreakBefore = !RunsText(keep.runs).empty();
                    first = false;
                }
                keep.runs.push_back(std::move(copy));
            }
        }
        keep.columnSpan = rightColumn - left + 1;
        keep.rowSpan = bottom - top + 1;

        std::vector<std::pair<int, int>> ordered = absorbed;
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) {
                      return a.first != b.first ? a.first < b.first : a.second > b.second;
                  });
        for (const auto& [r, ci] : ordered) {
            RichTableRow& modelRow = table.tableRows[static_cast<size_t>(r)];
            if (ci < static_cast<int>(modelRow.cells.size())) {
                modelRow.cells.erase(modelRow.cells.begin() + ci);
            }
        }

        // The caret may have been in a cell that no longer exists, and the
        // surviving cell's own index may have moved. Either way the caret
        // belongs in the merged cell; it keeps its offset only if it was
        // already there, since any other offset meant another cell's text.
        const bool caretWasInKeptCell =
            caret.blockIndex == blockIndex && caret.cellRow == row && caret.cellColumn == cellIndex;
        const RichTableGrid after = BuildTableGrid(table);
        int ownerRow = 0, ownerCell = 0;
        if (after.CellAt(top, left, ownerRow, ownerCell)) {
            caret = ClampPosition(RichDocPosition(blockIndex, ownerRow, ownerCell,
                                                  caretWasInKeptCell ? caret.byteOffset : 0));
        } else {
            caret = ClampPosition(caret);
        }
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::SplitTableCell(int blockIndex, int row, int cellIndex) {
    const RichTableGrid grid = TableGrid(blockIndex);
    if (grid.rowCount == 0) return false;
    int top = 0, left = 0;
    if (!grid.OriginOf(row, cellIndex, top, left)) return false;

    const RichTableCell& readOnly = doc->blocks[static_cast<size_t>(blockIndex)]
                                       .tableRows[static_cast<size_t>(row)]
                                       .cells[static_cast<size_t>(cellIndex)];
    const int rowSpan = std::max(1, readOnly.rowSpan);
    const int columnSpan = std::max(1, readOnly.columnSpan);
    if (rowSpan == 1 && columnSpan == 1) return false;      // nothing merged to undo

    {
        EditScope scope(*this, blockIndex, 1);
        RichDocBlock& table = doc->blocks[static_cast<size_t>(blockIndex)];
        table.tableRows[static_cast<size_t>(row)].cells[static_cast<size_t>(cellIndex)].rowSpan = 1;
        table.tableRows[static_cast<size_t>(row)].cells[static_cast<size_t>(cellIndex)].columnSpan = 1;

        // Every slot the cell gave up needs a cell of its own. The text stays
        // with the top-left one, which is where it was written.
        for (int r = top; r < top + rowSpan; ++r) {
            for (int c = left; c < left + columnSpan; ++c) {
                if (r == top && c == left) continue;
                const RichTableGrid current = BuildTableGrid(table);
                if (current.At(r, c).Occupied()) continue;
                InsertFreshCellAt(table, r, c);
            }
        }
        caret = ClampPosition(caret);
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

void UCRichDocumentEditor::InsertHorizontalRule() {
    InsertStructuralBlock(RichBlockType::HorizontalRule);
}

void UCRichDocumentEditor::InsertPageBreak() {
    InsertStructuralBlock(RichBlockType::PageBreak);
}

int UCRichDocumentEditor::InsertInlineImage(const std::string& name,
                                            const std::string& mimeType,
                                            const std::vector<uint8_t>& data,
                                            const std::string& altText) {
    if (data.empty()) return -1;
    // A picture in the line is a run, so it goes in exactly where typed text
    // would: same container, same offset, same undo step.
    if (!IsTextContainer(caret)) return -1;
    const int mediaIndex = doc->AddMedia(name, mimeType, data);

    RichTextRun picture;
    picture.text = RichTextRun::kObjectReplacement;
    picture.mediaIndex = mediaIndex;
    picture.imageAltText = altText;
    int width = 0, height = 0;
    if (UCRichDocument::SniffImagePixelSize(data, width, height)) {
        picture.imageWidthPt = static_cast<float>(width) * 72.0f / 96.0f;    // a pixel at 96 DPI
        picture.imageHeightPt = static_cast<float>(height) * 72.0f / 96.0f;
    }

    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        std::vector<RichTextRun>* runs = MutableRunsAt(caret);
        if (!runs) return -1;
        InsertIntoRuns(*runs, caret.byteOffset, picture.text, &picture);
        caret.byteOffset += static_cast<int>(picture.text.size());
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return mediaIndex;
}

namespace {
// The run holding the picture whose placeholder starts at `byteOffset`.
const RichTextRun* ImageRunAt(const std::vector<RichTextRun>& runs, int byteOffset) {
    int position = 0;
    for (const RichTextRun& run : runs) {
        const int start = position + (run.lineBreakBefore ? 1 : 0);
        position = start + static_cast<int>(run.text.size());
        if (run.IsInlineImage() && start == byteOffset) return &run;
        if (start > byteOffset) break;
    }
    return nullptr;
}
} // namespace

bool UCRichDocumentEditor::IsImageAt(const RichDocPosition& image) const {
    float w = 0, h = 0;
    std::string alt;
    int media = -1;
    return GetImageInfo(image, w, h, alt, media);
}

bool UCRichDocumentEditor::GetImageInfo(const RichDocPosition& image, float& widthPt, float& heightPt,
                                        std::string& altText, int& mediaIndex) const {
    if (image.blockIndex < 0 || image.blockIndex >= GetBlockCount()) return false;
    const RichDocBlock& block = doc->blocks[static_cast<size_t>(image.blockIndex)];
    if (!image.InCell() && block.type == RichBlockType::Image) {
        widthPt = block.imageWidthPt;
        heightPt = block.imageHeightPt;
        altText = block.imageAltText;
        mediaIndex = block.mediaIndex;
        return true;
    }
    const std::vector<RichTextRun>* runs = RunsAt(image);
    if (!runs) return false;
    const RichTextRun* run = ImageRunAt(*runs, image.byteOffset);
    if (!run) return false;
    widthPt = run->imageWidthPt;
    heightPt = run->imageHeightPt;
    altText = run->imageAltText;
    mediaIndex = run->mediaIndex;
    return true;
}

bool UCRichDocumentEditor::SetImageSize(const RichDocPosition& image, float widthPt, float heightPt) {
    if (!(widthPt > 0.0f) || !(heightPt > 0.0f) || !IsImageAt(image)) return false;
    {
        EditScope scope(*this, image.blockIndex, 1);
        RichDocBlock& block = doc->blocks[static_cast<size_t>(image.blockIndex)];
        if (!image.InCell() && block.type == RichBlockType::Image) {
            block.imageWidthPt = widthPt;
            block.imageHeightPt = heightPt;
        } else {
            RichTextRun* run = const_cast<RichTextRun*>(ImageRunAt(*MutableRunsAt(image), image.byteOffset));
            run->imageWidthPt = widthPt;
            run->imageHeightPt = heightPt;
        }
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::SetImageAltText(const RichDocPosition& image, const std::string& altText) {
    if (!IsImageAt(image)) return false;
    {
        EditScope scope(*this, image.blockIndex, 1);
        RichDocBlock& block = doc->blocks[static_cast<size_t>(image.blockIndex)];
        if (!image.InCell() && block.type == RichBlockType::Image) {
            block.imageAltText = altText;
        } else {
            RichTextRun* run = const_cast<RichTextRun*>(ImageRunAt(*MutableRunsAt(image), image.byteOffset));
            run->imageAltText = altText;
        }
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

int UCRichDocumentEditor::InsertNote(RichNote::Kind kind) {
    if (!IsTextContainer(caret)) return -1;
    RichNote note;
    note.kind = kind;
    note.blocks.emplace_back();
    const int noteIndex = static_cast<int>(doc->notes.size());
    doc->notes.push_back(std::move(note));
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        std::vector<RichTextRun>* runs = MutableRunsAt(caret);
        if (!runs) return -1;
        RichTextRun reference = FormatAt(caret);
        reference.noteIndex = noteIndex;
        reference.superscript = true;
        reference.text = "*";                 // numbered just below
        InsertIntoRuns(*runs, caret.byteOffset, reference.text, &reference);
        // Every mark after it moves on by one.
        doc->UpdateNoteMarks();
        caret = ClampPosition(caret);
        // After the mark, whatever its length turned out to be.
        const std::vector<RichTextRun>* after = RunsAt(caret);
        int position = 0;
        for (const RichTextRun& run : *after) {
            position += (run.lineBreakBefore ? 1 : 0) + static_cast<int>(run.text.size());
            if (run.noteIndex == noteIndex) break;
        }
        caret.byteOffset = position;
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return noteIndex;
}

int UCRichDocumentEditor::NoteAt(const RichDocPosition& pos) const {
    const std::vector<RichTextRun>* runs = RunsAt(pos);
    if (!runs) return -1;
    int position = 0;
    for (const RichTextRun& run : *runs) {
        const int start = position + (run.lineBreakBefore ? 1 : 0);
        const int end = start + static_cast<int>(run.text.size());
        if (run.noteIndex >= 0 && pos.byteOffset >= start && pos.byteOffset <= end) return run.noteIndex;
        position = end;
    }
    return -1;
}

// ===== SECTIONS =====

bool UCRichDocumentEditor::InsertSectionBreak(bool newPage) {
    if (caret.InCell()) return false;
    const RichSectionSetup current = CurrentSection();
    {
        EditScope scope(*this, caret.blockIndex, 1);
        if (HasSelection()) DeleteRangeInternal(GetSelectionRange());
        const bool atStart = caret.byteOffset == 0 && caret.blockIndex > 0;
        if (!atStart) SplitBlockInternal();
        RichDocBlock& start = doc->blocks[static_cast<size_t>(caret.blockIndex)];
        start.sectionStart = true;
        start.section = current;
        start.section.newPage = newPage;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::SetSectionColumns(int columns, float gapPt) {
    columns = std::clamp(columns, 1, 9);
    int start = -1;
    for (int i = std::min(caret.blockIndex, GetBlockCount() - 1); i >= 0; i--) {
        if (doc->blocks[static_cast<size_t>(i)].sectionStart) { start = i; break; }
    }
    if (start < 0) {
        // The first section's setup is the document's: not an undo step.
        doc->firstSection.columns = columns;
        doc->firstSection.columnGapPt = gapPt;
        modified = true;
    } else {
        EditScope scope(*this, start, 1);
        doc->blocks[static_cast<size_t>(start)].section.columns = columns;
        doc->blocks[static_cast<size_t>(start)].section.columnGapPt = gapPt;
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

RichSectionSetup UCRichDocumentEditor::CurrentSection() const {
    return doc->SectionFor(caret.blockIndex);
}

// ===== TRACKED CHANGES =====

void UCRichDocumentEditor::SetTrackChanges(bool enabled) {
    trackChanges = enabled;
    currentRevision = -1;             // a new session: a new revision entry
    pendingFormatValid = false;
}

void UCRichDocumentEditor::SetRevisionAuthor(const std::string& author, const std::string& date) {
    revisionAuthor = author;
    revisionDate = date;
    currentRevision = -1;
}

int UCRichDocumentEditor::CurrentRevision() {
    if (currentRevision < 0 || currentRevision >= static_cast<int>(doc->revisions.size())) {
        doc->revisions.push_back({revisionAuthor, revisionDate});
        currentRevision = static_cast<int>(doc->revisions.size()) - 1;
    }
    return currentRevision;
}

bool UCRichDocumentEditor::MarkRangeDeleted(const RichDocRange& range) {
    // Cells of a table selected as a block are emptied untracked.
    int top = 0, left = 0, bottom = 0, right = 0;
    if (CellRectBetween(range.start, range.end, top, left, bottom, right)) return false;
    const int revision = CurrentRevision();
    int caretBlock = range.end.blockIndex;
    int caretOffset = range.end.byteOffset;
    auto mark = [&](std::vector<RichTextRun>& runs, int from, int to, bool lastContainer) {
        const int length = static_cast<int>(RunsText(runs).size());
        from = std::clamp(from, 0, length);
        to = std::clamp(to, from, length);
        if (from == to) return;
        const int startIdx = SplitRunAt(runs, from);
        const int endIdx = SplitRunAt(runs, to);
        int removed = 0;
        for (int i = endIdx - 1; i >= startIdx && i < static_cast<int>(runs.size()); i--) {
            RichTextRun& run = runs[static_cast<size_t>(i)];
            if (run.change == RichTextRun::Change::Inserted) {
                // Deleting a tracked insertion takes it back.
                removed += static_cast<int>(run.text.size()) + (run.lineBreakBefore ? 1 : 0);
                runs.erase(runs.begin() + i);
            } else if (run.change == RichTextRun::Change::Unchanged) {
                run.change = RichTextRun::Change::Deleted;
                run.revision = revision;
            }
        }
        CoalesceRuns(runs);
        if (lastContainer) caretOffset = to - removed;
    };
    if (range.start.SameContainer(range.end)) {
        std::vector<RichTextRun>* runs = MutableRunsAt(range.start);
        if (!runs) return false;
        mark(*runs, range.start.byteOffset, range.end.byteOffset, true);
        caret = trackBackward ? ClampPosition(range.start)
                              : ClampPosition(range.start.InCell()
                                    ? RichDocPosition(range.start.blockIndex, range.start.cellRow, range.start.cellColumn, caretOffset)
                                    : RichDocPosition(range.start.blockIndex, caretOffset));
        anchor = caret;
        return true;
    }
    if (range.start.InCell() || range.end.InCell()) return false;
    const int lastBlock = std::min(range.end.blockIndex, GetBlockCount() - 1);
    for (int b = range.start.blockIndex; b <= lastBlock; b++) {
        RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
        if (!IsTextBlockType(block.type)) continue;
        const int length = static_cast<int>(RunsText(block.runs).size());
        const int from = b == range.start.blockIndex ? range.start.byteOffset : 0;
        const int to = b == range.end.blockIndex ? range.end.byteOffset : length;
        mark(block.runs, from, to, b == range.end.blockIndex);
    }
    caret = trackBackward ? ClampPosition(range.start) : ClampPosition(RichDocPosition(caretBlock, caretOffset));
    anchor = caret;
    return true;
}

void UCRichDocumentEditor::ResolveChanges(const RichDocRange& range, bool accept, bool wholeDocument) {
    auto resolve = [&](std::vector<RichTextRun>& runs, int from, int to) {
        if (!wholeDocument) {
            const int length = static_cast<int>(RunsText(runs).size());
            from = std::clamp(from, 0, length);
            to = std::clamp(to, from, length);
        }
        const int startIdx = wholeDocument ? 0 : SplitRunAt(runs, from);
        const int endIdx = wholeDocument ? static_cast<int>(runs.size()) : SplitRunAt(runs, to);
        for (int i = endIdx - 1; i >= startIdx && i < static_cast<int>(runs.size()); i--) {
            RichTextRun& run = runs[static_cast<size_t>(i)];
            if (run.change == RichTextRun::Change::Unchanged) continue;
            const bool drop = accept ? run.change == RichTextRun::Change::Deleted
                                     : run.change == RichTextRun::Change::Inserted;
            if (drop) {
                runs.erase(runs.begin() + i);
            } else {
                run.change = RichTextRun::Change::Unchanged;
                run.revision = -1;
            }
        }
        CoalesceRuns(runs);
    };
    const int first = wholeDocument ? 0 : range.start.blockIndex;
    const int last = wholeDocument ? GetBlockCount() - 1 : std::min(range.end.blockIndex, GetBlockCount() - 1);
    for (int b = first; b <= last; b++) {
        RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
        const bool edgeStart = !wholeDocument && b == range.start.blockIndex;
        const bool edgeEnd = !wholeDocument && b == range.end.blockIndex;
        if (!range.start.InCell() || wholeDocument) {
            const int length = static_cast<int>(RunsText(block.runs).size());
            resolve(block.runs, edgeStart ? range.start.byteOffset : 0, edgeEnd ? range.end.byteOffset : length);
        }
        for (size_t r = 0; r < block.tableRows.size(); r++) {
            for (size_t c = 0; c < block.tableRows[r].cells.size(); c++) {
                std::vector<RichTextRun>& runs = block.tableRows[r].cells[c].runs;
                const bool here = range.start.InCell() && static_cast<int>(r) == range.start.cellRow
                                  && static_cast<int>(c) == range.start.cellColumn;
                if (wholeDocument || (!range.start.InCell() && b > range.start.blockIndex && b < range.end.blockIndex)) {
                    resolve(runs, 0, static_cast<int>(RunsText(runs).size()));
                } else if (here) {
                    resolve(runs, range.start.byteOffset, range.end.byteOffset);
                }
            }
        }
    }
    EnsureNotEmpty();
    caret = ClampPosition(caret);
    anchor = ClampPosition(anchor);
}

bool UCRichDocumentEditor::AcceptAllChanges() {
    if (!doc->HasTrackedChanges()) return false;
    {
        EditScope scope(*this, 0, GetBlockCount());
        ResolveChanges(RichDocRange(), true, true);
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::RejectAllChanges() {
    if (!doc->HasTrackedChanges()) return false;
    {
        EditScope scope(*this, 0, GetBlockCount());
        ResolveChanges(RichDocRange(), false, true);
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

namespace {

// The extent of the tracked change a position is in: the run it touches
// with a change, widened over its neighbours with the same change.
bool ChangeExtent(const std::vector<RichTextRun>& runs, int offset, int& from, int& to) {
    int position = 0;
    int hit = -1;
    std::vector<std::pair<int, int>> spans;
    for (size_t i = 0; i < runs.size(); i++) {
        const int start = position + (runs[i].lineBreakBefore ? 1 : 0);
        const int end = start + static_cast<int>(runs[i].text.size());
        spans.emplace_back(position, end);
        if (runs[i].change != RichTextRun::Change::Unchanged && offset >= start && offset <= end && hit < 0) {
            hit = static_cast<int>(i);
        }
        position = end;
    }
    if (hit < 0) return false;
    int a = hit, b = hit;
    while (a > 0 && runs[static_cast<size_t>(a - 1)].change == runs[static_cast<size_t>(hit)].change
           && runs[static_cast<size_t>(a - 1)].revision == runs[static_cast<size_t>(hit)].revision) a--;
    while (b + 1 < static_cast<int>(runs.size()) && runs[static_cast<size_t>(b + 1)].change == runs[static_cast<size_t>(hit)].change
           && runs[static_cast<size_t>(b + 1)].revision == runs[static_cast<size_t>(hit)].revision) b++;
    from = spans[static_cast<size_t>(a)].first;
    to = spans[static_cast<size_t>(b)].second;
    return true;
}

} // namespace

bool UCRichDocumentEditor::AcceptChangeAt(const RichDocPosition& pos) {
    RichDocRange range = GetSelectionRange();
    if (range.IsEmpty()) {
        const std::vector<RichTextRun>* runs = RunsAt(ClampPosition(pos));
        int from = 0, to = 0;
        if (!runs || !ChangeExtent(*runs, pos.byteOffset, from, to)) return false;
        range = RichDocRange(pos, pos);
        range.start.byteOffset = from;
        range.end.byteOffset = to;
    }
    {
        EditScope scope(*this, range.start.blockIndex, range.end.blockIndex - range.start.blockIndex + 1);
        ResolveChanges(range, true, false);
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::RejectChangeAt(const RichDocPosition& pos) {
    RichDocRange range = GetSelectionRange();
    if (range.IsEmpty()) {
        const std::vector<RichTextRun>* runs = RunsAt(ClampPosition(pos));
        int from = 0, to = 0;
        if (!runs || !ChangeExtent(*runs, pos.byteOffset, from, to)) return false;
        range = RichDocRange(pos, pos);
        range.start.byteOffset = from;
        range.end.byteOffset = to;
    }
    {
        EditScope scope(*this, range.start.blockIndex, range.end.blockIndex - range.start.blockIndex + 1);
        ResolveChanges(range, false, false);
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::NextChange(const RichDocPosition& pos, RichDocRange& out) const {
    // Body runs only, in document order; wraps round to the first.
    struct Found { int block; int from; int to; };
    std::vector<Found> changes;
    for (int b = 0; b < GetBlockCount(); b++) {
        const std::vector<RichTextRun>& runs = doc->blocks[static_cast<size_t>(b)].runs;
        int position = 0;
        for (size_t i = 0; i < runs.size(); i++) {
            const int start = position + (runs[i].lineBreakBefore ? 1 : 0);
            const int end = start + static_cast<int>(runs[i].text.size());
            position = end;
            if (runs[i].change == RichTextRun::Change::Unchanged) continue;
            if (i > 0 && runs[i - 1].change == runs[i].change && runs[i - 1].revision == runs[i].revision) {
                changes.back().to = end;
                continue;
            }
            changes.push_back({b, start, end});
        }
    }
    if (changes.empty()) return false;
    for (const Found& found : changes) {
        if (found.block > pos.blockIndex || (found.block == pos.blockIndex && found.from > pos.byteOffset)) {
            out = RichDocRange(RichDocPosition(found.block, found.from), RichDocPosition(found.block, found.to));
            return true;
        }
    }
    out = RichDocRange(RichDocPosition(changes.front().block, changes.front().from),
                       RichDocPosition(changes.front().block, changes.front().to));
    return true;
}

int UCRichDocumentEditor::AddComment(const std::string& text, const std::string& author, const std::string& date) {
    RichDocRange range = GetSelectionRange();
    if (range.IsEmpty()) range = WordAt(caret);       // the word at the caret
    if (range.IsEmpty()) return -1;
    RichComment comment;
    comment.text = text;
    comment.author = author;
    comment.date = date;
    // Initials: the first letter of each word of the name.
    for (size_t i = 0; i < author.size();) {
        if (author[i] == ' ') { i++; continue; }
        const int next = NextCharOffset(author, static_cast<int>(i));
        comment.initials += author.substr(i, static_cast<size_t>(next) - i);       // a whole UTF-8 character
        while (i < author.size() && author[i] != ' ') i++;
    }
    doc->comments.push_back(std::move(comment));
    const int index = static_cast<int>(doc->comments.size()) - 1;
    RichCharFormatDelta delta;
    delta.addComment = index;
    coalescing = false;
    ApplyCharFormatToRange(range, delta);
    return index;
}

bool UCRichDocumentEditor::RemoveComment(int index) {
    if (index < 0 || index >= static_cast<int>(doc->comments.size())) return false;
    RichDocRange range;
    if (!CommentRange(index, range)) return false;
    RichCharFormatDelta delta;
    delta.removeComment = index;
    coalescing = false;
    // Whole blocks: a comment in a table is taken off every cell it covers.
    const int first = range.start.blockIndex, last = range.end.blockIndex;
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last; b++) {
            RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
            for (RichTextRun& run : block.runs) delta.ApplyTo(run);
            CoalesceRuns(block.runs);
            for (RichTableRow& row : block.tableRows) {
                for (RichTableCell& cell : row.cells) {
                    for (RichTextRun& run : cell.runs) delta.ApplyTo(run);
                    CoalesceRuns(cell.runs);
                }
            }
        }
    }
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::SetCommentText(int index, const std::string& text) {
    if (index < 0 || index >= static_cast<int>(doc->comments.size())) return false;
    doc->comments[static_cast<size_t>(index)].text = text;
    modified = true;
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::SetCommentResolved(int index, bool resolved) {
    if (index < 0 || index >= static_cast<int>(doc->comments.size())) return false;
    doc->comments[static_cast<size_t>(index)].resolved = resolved;
    modified = true;
    NotifyChanged();
    return true;
}

std::vector<int> UCRichDocumentEditor::CommentsAt(const RichDocPosition& pos) const {
    const std::vector<RichTextRun>* runs = RunsAt(ClampPosition(pos));
    if (!runs) return {};
    // The run the position is inside, or the one it ends.
    int offset = 0;
    const RichTextRun* found = nullptr;
    for (const RichTextRun& run : *runs) {
        const int start = offset + (run.lineBreakBefore ? 1 : 0);
        const int end = start + static_cast<int>(run.text.size());
        if (pos.byteOffset >= start && pos.byteOffset <= end && !run.commentIds.empty()) {
            found = &run;
            if (pos.byteOffset < end) break;
        }
        offset = end;
    }
    return found ? found->commentIds : std::vector<int>{};
}

bool UCRichDocumentEditor::CommentRange(int index, RichDocRange& out) const {
    bool any = false;
    auto consider = [&](const std::vector<RichTextRun>& runs, int block, int row, int column) {
        int offset = 0;
        for (const RichTextRun& run : runs) {
            const int start = offset + (run.lineBreakBefore ? 1 : 0);
            const int end = start + static_cast<int>(run.text.size());
            offset = end;
            if (std::find(run.commentIds.begin(), run.commentIds.end(), index) == run.commentIds.end()) continue;
            const RichDocPosition from = row >= 0 ? RichDocPosition(block, row, column, start) : RichDocPosition(block, start);
            const RichDocPosition to = row >= 0 ? RichDocPosition(block, row, column, end) : RichDocPosition(block, end);
            if (!any) out.start = from;
            out.end = to;
            any = true;
        }
    };
    for (int b = 0; b < GetBlockCount(); b++) {
        const RichDocBlock& block = doc->blocks[static_cast<size_t>(b)];
        consider(block.runs, b, -1, -1);
        for (size_t r = 0; r < block.tableRows.size(); r++) {
            for (size_t c = 0; c < block.tableRows[r].cells.size(); c++) {
                consider(block.tableRows[r].cells[c].runs, b, static_cast<int>(r), static_cast<int>(c));
            }
        }
    }
    return any;
}

bool UCRichDocumentEditor::AddBookmark(const std::string& name) {
    if (name.empty() || doc->FindBookmark(name) >= 0) return false;
    const int block = ClampPosition(caret).blockIndex;
    if (block < 0 || block >= static_cast<int>(doc->blocks.size())) return false;
    {
        EditScope scope(*this, block, 1);
        doc->blocks[static_cast<size_t>(block)].bookmarks.push_back(name);
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::RemoveBookmark(const std::string& name) {
    const int block = doc->FindBookmark(name);
    if (block < 0) return false;
    {
        EditScope scope(*this, block, 1);
        std::vector<std::string>& marks = doc->blocks[static_cast<size_t>(block)].bookmarks;
        marks.erase(std::remove(marks.begin(), marks.end(), name), marks.end());
    }
    coalescing = false;
    NotifyChanged();
    return true;
}

bool UCRichDocumentEditor::InsertCrossReference(const std::string& bookmark, RichTextRun::Field kind) {
    if (kind != RichTextRun::Field::Reference && kind != RichTextRun::Field::PageReference) return false;
    if (doc->FindBookmark(bookmark) < 0 || !IsTextContainer(caret)) return false;
    RichDocRange selection = GetSelectionRange();
    const int first = selection.start.blockIndex;
    // The whole document: filling the field in may touch any block.
    {
        EditScope scope(*this, 0, static_cast<int>(doc->blocks.size()));
        if (HasSelection()) DeleteRangeInternal(selection);
        std::vector<RichTextRun>* runs = MutableRunsAt(caret);
        if (!runs) return false;
        RichTextRun run = FormatAt(caret);
        run.field = kind;
        run.fieldArgument = bookmark;
        run.text = "1";
        InsertIntoRuns(*runs, caret.byteOffset, run.text, &run);
        doc->UpdateFields();
        // After the field, whatever it now says.
        int position = 0;
        for (const RichTextRun& r : *runs) {
            position += (r.lineBreakBefore ? 1 : 0) + static_cast<int>(r.text.size());
            if (r.field == kind && r.fieldArgument == bookmark && position > caret.byteOffset) break;
        }
        caret.byteOffset = position;
        anchor = caret;
    }
    (void)first;
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

std::string UCRichDocumentEditor::InsertCaption(const std::string& label, const std::string& text) {
    if (label.empty()) return "";
    const int block = ClampPosition(caret).blockIndex;
    if (block < 0 || block >= static_cast<int>(doc->blocks.size())) return "";
    const std::string name = doc->UniqueBookmarkName("_Ref" + label);
    {
        EditScope scope(*this, 0, static_cast<int>(doc->blocks.size()));
        RichDocBlock caption;
        caption.bookmarks.push_back(name);
        RichTextRun lead;
        lead.text = label + " ";
        RichTextRun number;
        number.field = RichTextRun::Field::Sequence;
        number.fieldArgument = label;
        number.text = "1";
        caption.runs = {lead, number};
        if (!text.empty()) {
            RichTextRun tail;
            tail.text = ": " + text;
            caption.runs.push_back(tail);
        }
        if (doc->FindStyle("Caption")) {
            caption.styleId = "Caption";
            const RichStyle style = doc->ResolveStyle("Caption");
            style.paragraph.ApplyTo(caption);
            for (RichTextRun& run : caption.runs) style.character.ApplyTo(run);
        } else {
            for (RichTextRun& run : caption.runs) run.italic = true;
        }
        doc->blocks.insert(doc->blocks.begin() + block + 1, caption);
        doc->UpdateFields();
        caret = ClampPosition({block + 1, BlockTextLength(block + 1)});
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return name;
}

bool UCRichDocumentEditor::InsertTableOfContents(int maxLevel) {
    int block = ClampPosition(caret).blockIndex;
    if (block < 0 || block >= static_cast<int>(doc->blocks.size())) return false;
    {
        EditScope scope(*this, 0, static_cast<int>(doc->blocks.size()));
        const std::vector<RichDocBlock> entries = doc->BuildTableOfContents(maxLevel);
        const RichDocBlock& here = doc->blocks[static_cast<size_t>(block)];
        const bool emptyParagraph = here.type == RichBlockType::Paragraph && RunsText(here.runs).empty()
                                    && here.bookmarks.empty();
        if (emptyParagraph) doc->blocks.erase(doc->blocks.begin() + block);
        doc->blocks.insert(doc->blocks.begin() + block, entries.begin(), entries.end());
        const int after = block + static_cast<int>(entries.size());
        if (after >= static_cast<int>(doc->blocks.size())) doc->blocks.emplace_back();
        caret = ClampPosition({after, 0});
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::UpdateTableOfContents(int maxLevel) {
    bool updated = false;
    {
        EditScope scope(*this, 0, static_cast<int>(doc->blocks.size()));
        updated = doc->UpdateTableOfContents(maxLevel);
        caret = ClampPosition(caret);
        anchor = ClampPosition(anchor);
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return updated;
}

bool UCRichDocumentEditor::InsertField(RichTextRun::Field field) {
    if (field == RichTextRun::Field::Plain) return false;
    if (!IsTextContainer(caret)) return false;
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        std::vector<RichTextRun>* runs = MutableRunsAt(caret);
        if (!runs) return false;
        RichTextRun run = FormatAt(caret);
        run.field = field;
        // The value until a paged view fills in the real one.
        run.text = "1";
        InsertIntoRuns(*runs, caret.byteOffset, run.text, &run);
        caret.byteOffset += static_cast<int>(run.text.size());
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

int UCRichDocumentEditor::InsertImage(const std::string& name, const std::string& mimeType,
                                      const std::vector<uint8_t>& data,
                                      const std::string& altText) {
    if (data.empty()) return -1;
    int mediaIndex = doc->AddMedia(name, mimeType, data);

    int blockIndex = -1;
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);

        RichDocBlock image;
        image.type = RichBlockType::Image;
        image.mediaIndex = mediaIndex;
        image.imageAltText = altText;
        int width = 0, height = 0;
        if (UCRichDocument::SniffImagePixelSize(data, width, height)) {
            image.imageWidthPt = static_cast<float>(width) * 72.0f / 96.0f;      // a pixel at 96 DPI
            image.imageHeightPt = static_cast<float>(height) * 72.0f / 96.0f;
        }

        RichDocBlock& current = doc->blocks[caret.blockIndex];
        if (IsTextBlockType(current.type) && RunsText(current.runs).empty()) {
            doc->blocks[caret.blockIndex] = image;
            blockIndex = caret.blockIndex;
        } else {
            SplitBlockInternal();
            doc->blocks.insert(doc->blocks.begin() + caret.blockIndex, image);
            blockIndex = caret.blockIndex;
        }
        int afterIndex = blockIndex + 1;
        if (afterIndex >= GetBlockCount() || !IsTextBlock(afterIndex)) {
            RichDocBlock paragraph;
            paragraph.type = RichBlockType::Paragraph;
            doc->blocks.insert(doc->blocks.begin() + afterIndex, paragraph);
        }
        caret = ClampPosition({afterIndex, 0});
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
    return blockIndex;
}

void UCRichDocumentEditor::DeleteBlock(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= GetBlockCount()) return;
    {
        EditScope scope(*this, blockIndex, 1);
        doc->blocks.erase(doc->blocks.begin() + blockIndex);
        EnsureNotEmpty();
        caret = ClampPosition({std::min(blockIndex, GetBlockCount() - 1), 0});
        anchor = caret;
    }
    NotifyChanged();
    NotifySelectionChanged();
}

// ===== AUTOFORMAT =====

namespace {

bool EndsWith(const std::string& text, size_t end, const std::string& tail) {
    return end >= tail.size() && text.compare(end - tail.size(), tail.size(), tail) == 0;
}

// The UTF-8 character that ends at `end` (empty at the start).
std::string CharBefore(const std::string& text, int end) {
    if (end <= 0) return {};
    const int start = UCRichDocumentEditor::PreviousCharOffset(text, end);
    return text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
}

bool IsSpaceChar(const std::string& c) {
    return c.empty() || c == " " || c == "\t" || c == "\n" || c == "\xC2\xA0";
}

} // namespace

std::string UCRichDocumentEditor::ApplySmartQuotes(const std::string& typed) const {
    if (!autoFormatEnabled || !autoFormat.smartQuotes) return typed;
    if (typed != "\"" && typed != "'") return typed;
    const std::vector<RichTextRun>* runs = RunsAt(caret);
    if (!runs) return typed;
    if (!caret.InCell() && (doc->blocks[static_cast<size_t>(caret.blockIndex)].type == RichBlockType::CodeBlock
                            || doc->blocks[static_cast<size_t>(caret.blockIndex)].type == RichBlockType::MathBlock)) {
        return typed;
    }
    const RichTextRun format = pendingFormatValid ? pendingFormat : FormatAt(caret);
    if (format.code || format.math) return typed;     // code and formulas mean the straight quote

    const std::string text = RunsText(*runs);
    const int at = HasSelection() ? GetSelectionRange().start.byteOffset : caret.byteOffset;
    const std::string before = CharBefore(text, std::min(at, static_cast<int>(text.size())));
    // Opening after nothing, a space, an opening bracket, a dash or another
    // opening quote; closing (which is also the apostrophe) after anything else.
    static const char* const openers[] = {"(", "[", "{", "<", "\xE2\x80\x94", "\xE2\x80\x93",
                                          "\xE2\x80\x9C", "\xE2\x80\x98", "-", "/"};
    bool opening = IsSpaceChar(before);
    for (const char* opener : openers) opening = opening || before == opener;
    if (typed == "\"") return opening ? "\xE2\x80\x9C" : "\xE2\x80\x9D";   // “ ”
    return opening ? "\xE2\x80\x98" : "\xE2\x80\x99";                       // ‘ ’
}

bool UCRichDocumentEditor::AutoFormatBeforeCaret() {
    if (!autoFormatEnabled || HasSelection()) return false;
    std::vector<RichTextRun>* runs = MutableRunsAt(caret);
    if (!runs) return false;
    const RichDocBlock& owner = doc->blocks[static_cast<size_t>(caret.blockIndex)];
    if (!caret.InCell() && (owner.type == RichBlockType::CodeBlock || owner.type == RichBlockType::MathBlock)) {
        return false;
    }
    const std::string text = RunsText(*runs);
    const int end = std::min(caret.byteOffset, static_cast<int>(text.size()));
    if (end <= 0) return false;
    if (const RichTextRun* run = RunAtOffset(*runs, end)) {
        if (run->code || run->math || run->field != RichTextRun::Field::Plain) return false;
    }

    // ---- replacements of what the caret just finished ----
    int replaceFrom = -1;
    std::string replacement;
    const std::string last = CharBefore(text, end);
    const size_t e = static_cast<size_t>(end);
    if (autoFormat.ellipsis && EndsWith(text, e, "...") && !EndsWith(text, e, "....")) {
        replaceFrom = end - 3;
        replacement = "\xE2\x80\xA6";                                 // …
    } else if (autoFormat.symbols) {
        static const std::pair<const char*, const char*> symbols[] = {
            {"(c)", "\xC2\xA9"}, {"(C)", "\xC2\xA9"}, {"(r)", "\xC2\xAE"}, {"(R)", "\xC2\xAE"},
            {"(tm)", "\xE2\x84\xA2"}, {"(TM)", "\xE2\x84\xA2"},
            {"->", "\xE2\x86\x92"}, {"<-", "\xE2\x86\x90"}, {"=>", "\xE2\x87\x92"}};
        for (const auto& [from, to] : symbols) {
            const std::string pattern = from;
            // "-->" is an arrow drawn with a longer shaft, not a dash and an arrow.
            if (EndsWith(text, e, pattern) && !(pattern == "->" && EndsWith(text, e, "-->"))) {
                replaceFrom = end - static_cast<int>(pattern.size());
                replacement = to;
                break;
            }
        }
    }
    if (replaceFrom < 0 && autoFormat.dashes && last != "-" && !last.empty()) {
        // "word--word" (an em dash) or "word -- word" (an en dash), decided
        // once the character after the hyphens is typed.
        const int hyphens = end - static_cast<int>(last.size());
        if (hyphens >= 2 && EndsWith(text, static_cast<size_t>(hyphens), "--")
            && !EndsWith(text, static_cast<size_t>(hyphens), "---")) {
            const std::string before = CharBefore(text, hyphens - 2);
            if (!before.empty()) {
                const bool spaced = IsSpaceChar(before) && IsSpaceChar(last);
                const bool joined = !IsSpaceChar(before) && !IsSpaceChar(last);
                if (spaced || joined) {
                    replaceFrom = hyphens - 2;
                    replacement = std::string(spaced ? "\xE2\x80\x93" : "\xE2\x80\x94") + last;
                }
            }
        }
    }
    if (replaceFrom >= 0) {
        {
            EditScope scope(*this, caret.blockIndex, 1);
            std::vector<RichTextRun>* target = MutableRunsAt(caret);
            EraseRunRange(*target, replaceFrom, end);
            InsertIntoRuns(*target, replaceFrom, replacement, nullptr);
            caret.byteOffset = replaceFrom + static_cast<int>(replacement.size());
            anchor = caret;
        }
        coalescing = false;
        NotifyChanged();
        NotifySelectionChanged();
        return true;
    }

    // ---- a paragraph's opening characters turning it into something ----
    if (caret.InCell() || last != " " || owner.type != RichBlockType::Paragraph) return false;
    const std::string head = text.substr(0, e - 1);        // what precedes the space
    RichDocBlock changed = owner;
    bool matched = false;
    if (autoFormat.headings && !head.empty() && head.size() <= 6
        && head.find_first_not_of('#') == std::string::npos) {
        changed.type = RichBlockType::Heading;
        changed.headingLevel = static_cast<int>(head.size());
        matched = true;
    } else if (autoFormat.lists && (head == "-" || head == "*" || head == "+")) {
        changed.type = RichBlockType::ListItem;
        changed.orderedList = false;
        matched = true;
    } else if (autoFormat.lists && (head == "[ ]" || head == "[x]" || head == "[X]")) {
        changed.type = RichBlockType::ListItem;
        changed.orderedList = false;
        changed.checkbox = true;
        changed.checked = head != "[ ]";
        matched = true;
    } else if (autoFormat.lists && head.size() >= 2 && (head.back() == '.' || head.back() == ')')) {
        const std::string label = head.substr(0, head.size() - 1);
        const bool digits = label.size() <= 4 && label.find_first_not_of("0123456789") == std::string::npos;
        const bool letter = label.size() == 1 && std::isalpha(static_cast<unsigned char>(label[0]));
        if (digits || letter) {
            changed.type = RichBlockType::ListItem;
            changed.orderedList = true;
            if (digits) {
                const int number = std::stoi(label);   // locale-ok: ASCII digits only
                changed.listStartNumber = number > 1 ? number : 0;
                changed.numberFormat = RichNumberFormat::Decimal;
            } else {
                const bool upper = std::isupper(static_cast<unsigned char>(label[0])) != 0;
                changed.numberFormat = upper ? RichNumberFormat::UpperLetter : RichNumberFormat::LowerLetter;
                const int number = std::tolower(static_cast<unsigned char>(label[0])) - 'a' + 1;
                changed.listStartNumber = number > 1 ? number : 0;
            }
            if (head.back() == ')') changed.numberTemplate = "%1)";
            matched = true;
        }
    } else if (autoFormat.lists && head == ">") {
        changed.type = RichBlockType::BlockQuote;
        matched = true;
    }
    if (!matched) return false;
    {
        EditScope scope(*this, caret.blockIndex, 1);
        RichDocBlock& block = doc->blocks[static_cast<size_t>(caret.blockIndex)];
        changed.runs = block.runs;
        EraseRunRange(changed.runs, 0, end);
        block = std::move(changed);
        caret.byteOffset = 0;
        anchor = caret;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

bool UCRichDocumentEditor::TypeText(const std::string& utf8) {
    if (!autoFormatEnabled) {
        InsertText(utf8);
        return false;
    }
    InsertText(ApplySmartQuotes(utf8));
    // Only a keystroke's worth of text triggers a correction; a paste does not.
    if (utf8.size() > 4) return false;
    return AutoFormatBeforeCaret();
}

void UCRichDocumentEditor::TypeEnter() {
    if (autoFormatEnabled && autoFormat.rules && !HasSelection() && !caret.InCell()
        && doc->blocks[static_cast<size_t>(caret.blockIndex)].type == RichBlockType::Paragraph) {
        const std::string text = BlockText(caret.blockIndex);
        const bool rule = text.size() >= 3 && caret.byteOffset == static_cast<int>(text.size())
                       && (text.find_first_not_of('-') == std::string::npos
                           || text.find_first_not_of('*') == std::string::npos
                           || text.find_first_not_of('_') == std::string::npos);
        if (rule) {
            {
                EditScope scope(*this, caret.blockIndex, 1);
                RichDocBlock& block = doc->blocks[static_cast<size_t>(caret.blockIndex)];
                block = RichDocBlock{};
                block.type = RichBlockType::HorizontalRule;
                RichDocBlock paragraph;
                doc->blocks.insert(doc->blocks.begin() + caret.blockIndex + 1, paragraph);
                caret = RichDocPosition(caret.blockIndex + 1, 0);
                anchor = caret;
            }
            coalescing = false;
            NotifyChanged();
            NotifySelectionChanged();
            return;
        }
    }
    SplitBlock();
}

// ===== CLIPBOARD SUPPORT =====

std::vector<RichDocBlock> UCRichDocumentEditor::ExtractRange(const RichDocRange& range) const {
    std::vector<RichDocBlock> out;
    if (range.IsEmpty()) return out;

    // A block of cells copies as a table of just those cells.
    int top = 0, left = 0, bottom = 0, right = 0;
    if (CellRectBetween(range.start, range.end, top, left, bottom, right)) {
        const RichDocBlock& source = doc->blocks[static_cast<size_t>(range.start.blockIndex)];
        const RichTableGrid grid = BuildTableGrid(source);
        RichDocBlock table = source;
        table.tableRows.clear();
        for (int r = top; r <= bottom; ++r) {
            RichTableRow row;
            row.header = source.tableRows[static_cast<size_t>(r)].header;
            for (int c = left; c <= right; ++c) {
                const RichTableGridSlot& slot = grid.At(r, c);
                if (slot.Occupied() && slot.origin) {
                    row.cells.push_back(source.tableRows[static_cast<size_t>(slot.row)]
                                            .cells[static_cast<size_t>(slot.cellIndex)]);
                }
            }
            table.tableRows.push_back(std::move(row));
        }
        if (static_cast<int>(source.tableColumnWidths.size()) == grid.columnCount) {
            table.tableColumnWidths.assign(source.tableColumnWidths.begin() + left,
                                           source.tableColumnWidths.begin() + right + 1);
        } else {
            table.tableColumnWidths.clear();
        }
        // A copied part of a table is as wide as its own columns, not the page.
        table.tableWidthPt = 0.0f;
        table.tableWidthPercent = 0.0f;
        out.push_back(std::move(table));
        return out;
    }

    // Copying inside a cell yields the selected run slice as a paragraph, so it
    // pastes as ordinary text wherever it lands.
    if (range.start.InCell()) {
        if (const std::vector<RichTextRun>* runs = RunsAt(range.start)) {
            RichDocBlock block;
            block.type = RichBlockType::Paragraph;
            block.runs = SliceRuns(*runs, range.start.byteOffset, range.end.byteOffset);
            out.push_back(std::move(block));
        }
        return out;
    }

    for (int b = range.start.blockIndex; b <= range.end.blockIndex && b < GetBlockCount(); b++) {
        RichDocBlock block = doc->blocks[b];
        if (IsTextBlockType(block.type)) {
            int textLength = static_cast<int>(RunsText(block.runs).size());
            int from = (b == range.start.blockIndex) ? range.start.byteOffset : 0;
            int to   = (b == range.end.blockIndex)   ? range.end.byteOffset   : textLength;
            if (b == range.end.blockIndex && from >= to && range.start.blockIndex != b) continue;
            block.runs = SliceRuns(block.runs, from, to);
        }
        out.push_back(block);
    }
    return out;
}

std::string UCRichDocumentEditor::RangeToPlainText(const RichDocRange& range) const {
    std::string out;
    for (const RichDocBlock& block : ExtractRange(range)) {
        if (!out.empty()) out += '\n';
        if (IsTextBlockType(block.type)) {
            out += RunsText(block.runs);
        } else if (block.type == RichBlockType::Table) {
            for (const auto& row : block.tableRows) {
                for (size_t c = 0; c < row.cells.size(); c++) {
                    if (c) out += '\t';
                    out += UCRichDocument::ConcatenateRunText(row.cells[c].runs);
                }
                out += '\n';
            }
        } else if (block.type == RichBlockType::Image) {
            out += block.imageAltText;
        }
    }
    return out;
}

namespace {
// Where `pos` ends up once `range` has been deleted (pos not inside it).
RichDocPosition PositionAfterDelete(const RichDocPosition& pos, const RichDocRange& range) {
    if (pos <= range.start) return pos;
    if (pos.SameContainer(range.end)) {
        RichDocPosition out = range.start;
        out.byteOffset = range.start.byteOffset + (pos.byteOffset - range.end.byteOffset);
        return out;
    }
    if (!range.start.InCell() && !range.end.InCell() && pos.blockIndex > range.end.blockIndex) {
        RichDocPosition out = pos;
        out.blockIndex -= range.end.blockIndex - range.start.blockIndex;
        return out;
    }
    return pos;
}
} // namespace

bool UCRichDocumentEditor::MoveRange(const RichDocRange& range, const RichDocPosition& target, bool copy) {
    if (range.IsEmpty()) return false;
    const RichDocPosition to = ClampPosition(target);
    // Onto itself: nothing to do. Its edges are fine for a copy.
    if (to > range.start && to < range.end) return false;
    if (!copy && (to == range.start || to == range.end)) return false;
    int top = 0, left = 0, bottom = 0, right = 0;
    if (CellRectBetween(range.start, range.end, top, left, bottom, right)) return false;
    const std::vector<RichDocBlock> moving = ExtractRange(range);
    if (moving.empty()) return false;

    const int first = std::min(range.start.blockIndex, to.blockIndex);
    const int last = std::max(range.end.blockIndex, to.blockIndex);
    {
        EditScope scope(*this, first, last - first + 1);
        RichDocPosition at = to;
        if (!copy) {
            DeleteRangeInternal(range);
            at = ClampPosition(PositionAfterDelete(to, range));
        }
        caret = anchor = at;
        pendingFormatValid = false;
        InsertBlocksInternal(moving);
        // The dropped text ends up selected, as it does in a word processor.
        anchor = at;
    }
    coalescing = false;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
}

// Pasting into a table cell. A cell holds runs, not blocks, so:
// - a table pastes cell by cell into the grid from the caret's cell on, as a
//   spreadsheet does (cells past the table's edge are dropped);
// - anything else flows into the cell, one line per pasted paragraph.
void UCRichDocumentEditor::InsertBlocksIntoCellInternal(const std::vector<RichDocBlock>& blocks) {
    RichDocBlock& table = doc->blocks[static_cast<size_t>(caret.blockIndex)];
    if (blocks.size() == 1 && blocks[0].type == RichBlockType::Table) {
        const RichTableGrid target = BuildTableGrid(table);
        const RichTableGrid source = BuildTableGrid(blocks[0]);
        int originRow = 0, originColumn = 0;
        if (!target.OriginOf(caret.cellRow, caret.cellColumn, originRow, originColumn)) return;
        for (int r = 0; r < source.rowCount; ++r) {
            for (int c = 0; c < source.columnCount; ++c) {
                const RichTableGridSlot& from = source.At(r, c);
                if (!from.Occupied() || !from.origin) continue;
                int row = 0, cellIndex = 0;
                if (!target.CellAt(originRow + r, originColumn + c, row, cellIndex)) continue;
                const RichTableGridSlot& to = target.At(originRow + r, originColumn + c);
                if (!to.origin) continue;       // inside a merged cell: its origin already took one
                table.tableRows[static_cast<size_t>(row)].cells[static_cast<size_t>(cellIndex)].runs =
                    blocks[0].tableRows[static_cast<size_t>(from.row)].cells[static_cast<size_t>(from.cellIndex)].runs;
            }
        }
        caret = ClampPosition(ContainerEnd(caret));
        anchor = caret;
        return;
    }

    std::vector<RichTextRun> flowing;
    for (const RichDocBlock& block : blocks) {
        std::vector<RichTextRun> runs;
        if (IsTextBlockType(block.type)) {
            runs = block.runs;
        } else if (block.type == RichBlockType::Table) {
            // A table's text, a cell per tab and a row per line.
            for (const RichTableRow& row : block.tableRows) {
                RichTextRun line;
                line.lineBreakBefore = !runs.empty();
                for (size_t c = 0; c < row.cells.size(); ++c) {
                    if (c) line.text += '\t';
                    line.text += RunsText(row.cells[c].runs);
                }
                runs.push_back(line);
            }
        } else {
            continue;
        }
        if (runs.empty()) runs.emplace_back();
        runs.front().lineBreakBefore = !flowing.empty();
        flowing.insert(flowing.end(), runs.begin(), runs.end());
    }
    if (flowing.empty()) return;
    std::vector<RichTextRun>* target = MutableRunsAt(caret);
    if (!target) return;
    const int at = SplitRunAt(*target, caret.byteOffset);
    target->insert(target->begin() + at, flowing.begin(), flowing.end());
    CoalesceRuns(*target);
    caret.byteOffset += static_cast<int>(RunsText(flowing).size());
    caret = ClampPosition(caret);
    anchor = caret;
}

void UCRichDocumentEditor::InsertBlocks(const std::vector<RichDocBlock>& blocks) {
    if (blocks.empty()) return;
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        InsertBlocksInternal(blocks);
    }
    NotifyChanged();
    NotifySelectionChanged();
}

void UCRichDocumentEditor::InsertBlocksInternal(const std::vector<RichDocBlock>& incoming) {
    if (incoming.empty()) return;
    // A pasted copy of a bookmarked paragraph does not take its bookmark:
    // names are unique, and references stay with the original.
    std::vector<RichDocBlock> blocks = incoming;
    // Pasted text is a tracked insertion when tracking is on.
    if (trackChanges) {
        const int revision = CurrentRevision();
        auto mark = [revision](std::vector<RichTextRun>& runs) {
            for (RichTextRun& run : runs) {
                if (run.change == RichTextRun::Change::Deleted) continue;
                run.change = RichTextRun::Change::Inserted;
                run.revision = revision;
            }
        };
        for (RichDocBlock& block : blocks) {
            mark(block.runs);
            for (RichTableRow& row : block.tableRows) {
                for (RichTableCell& cell : row.cells) mark(cell.runs);
            }
        }
    }
    for (RichDocBlock& block : blocks) {
        auto& marks = block.bookmarks;
        marks.erase(std::remove_if(marks.begin(), marks.end(),
                                   [this](const std::string& name) { return doc->FindBookmark(name) >= 0; }),
                    marks.end());
    }
    {
        if (caret.InCell()) {
            InsertBlocksIntoCellInternal(blocks);
        } else if (blocks.size() == 1 && IsTextBlockType(blocks[0].type)
            && IsTextBlock(caret.blockIndex)) {
            // A single-paragraph paste flows into the current paragraph,
            // keeping its own run formatting.
            RichDocBlock& target = doc->blocks[caret.blockIndex];
            int at = SplitRunAt(target.runs, caret.byteOffset);
            std::vector<RichTextRun> inserted = blocks[0].runs;
            if (!inserted.empty()) inserted.front().lineBreakBefore = false;
            target.runs.insert(target.runs.begin() + at, inserted.begin(), inserted.end());
            int insertedBytes = static_cast<int>(RunsText(inserted).size());
            CoalesceRuns(target.runs);
            caret.byteOffset += insertedBytes;
            anchor = caret;
        } else {
            // Multi-block paste: the first pasted block flows into the current
            // paragraph and the last one keeps the text that followed the
            // caret, so pasting mid-sentence reads as one continuous edit.
            SplitBlockInternal();                       // caret now starts the tail
            int headIndex = caret.blockIndex - 1;
            int tailIndex = caret.blockIndex;
            RichDocBlock tailBlock = doc->blocks[tailIndex];
            doc->blocks.erase(doc->blocks.begin() + tailIndex);

            std::vector<RichDocBlock> pasted = blocks;
            if (IsTextBlockType(pasted.front().type) && IsTextBlock(headIndex)) {
                RichDocBlock& head = doc->blocks[headIndex];
                size_t joinAt = head.runs.size();
                head.runs.insert(head.runs.end(),
                                 pasted.front().runs.begin(), pasted.front().runs.end());
                if (joinAt < head.runs.size()) head.runs[joinAt].lineBreakBefore = false;
                CoalesceRuns(head.runs);
                pasted.erase(pasted.begin());
            }
            int insertAt = headIndex + 1;
            doc->blocks.insert(doc->blocks.begin() + insertAt, pasted.begin(), pasted.end());

            int lastIndex = pasted.empty() ? headIndex
                                           : insertAt + static_cast<int>(pasted.size()) - 1;
            caret = ClampPosition({lastIndex, BlockTextLength(lastIndex)});
            anchor = caret;

            if (IsTextBlock(lastIndex) && IsTextBlockType(tailBlock.type)) {
                RichDocBlock& last = doc->blocks[lastIndex];
                size_t joinAt = last.runs.size();
                last.runs.insert(last.runs.end(), tailBlock.runs.begin(), tailBlock.runs.end());
                if (joinAt < last.runs.size()) last.runs[joinAt].lineBreakBefore = false;
                CoalesceRuns(last.runs);
            } else if (!IsTextBlockType(tailBlock.type)
                       || !RunsText(tailBlock.runs).empty()) {
                doc->blocks.insert(doc->blocks.begin() + lastIndex + 1, tailBlock);
            }
        }
    }
}

} // namespace UltraCanvas
