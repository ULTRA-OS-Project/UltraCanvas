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

// Byte length a run contributes to its block's text.
int RunSpan(const RichTextRun& run) {
    return (run.lineBreakBefore ? 1 : 0) + static_cast<int>(run.text.size());
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
    }
    inserted.text = text;
    inserted.lineBreakBefore = lineBreakBefore;

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
    std::string text = BlockText(out.blockIndex);
    out.byteOffset = std::max(0, std::min(out.byteOffset, static_cast<int>(text.size())));
    out.byteOffset = SnapToCharStart(text, out.byteOffset);
    return out;
}

void UCRichDocumentEditor::SetCaret(const RichDocPosition& pos, bool extend) {
    RichDocPosition clamped = ClampPosition(pos);
    if (clamped == caret && (extend || anchor == caret)) return;
    caret = clamped;
    if (!extend) anchor = caret;
    coalescing = false;         // a moved caret ends the current typing run
    pendingFormatValid = false;
    NotifySelectionChanged();
}

void UCRichDocumentEditor::SetSelection(const RichDocPosition& from, const RichDocPosition& to) {
    anchor = ClampPosition(from);
    caret = ClampPosition(to);
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
    std::string text = BlockText(p.blockIndex);
    if (p.byteOffset < static_cast<int>(text.size())) {
        return {p.blockIndex, NextCharOffset(text, p.byteOffset)};
    }
    if (p.blockIndex + 1 < GetBlockCount()) return {p.blockIndex + 1, 0};
    return p;
}

RichDocPosition UCRichDocumentEditor::PreviousCharacter(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    if (p.byteOffset > 0) {
        std::string text = BlockText(p.blockIndex);
        return {p.blockIndex, PreviousCharOffset(text, p.byteOffset)};
    }
    if (p.blockIndex > 0) return {p.blockIndex - 1, BlockTextLength(p.blockIndex - 1)};
    return p;
}

RichDocPosition UCRichDocumentEditor::NextWord(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    std::string text = BlockText(p.blockIndex);
    int n = static_cast<int>(text.size());
    if (p.byteOffset >= n) {
        if (p.blockIndex + 1 < GetBlockCount()) return {p.blockIndex + 1, 0};
        return p;
    }
    int i = p.byteOffset;
    // Out of the current word, then over the gap to the next word's first byte.
    while (i < n && IsWordByte(static_cast<unsigned char>(text[i]))) i = NextCharOffset(text, i);
    while (i < n && !IsWordByte(static_cast<unsigned char>(text[i]))) i = NextCharOffset(text, i);
    return {p.blockIndex, i};
}

RichDocPosition UCRichDocumentEditor::PreviousWord(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    if (p.byteOffset == 0) {
        if (p.blockIndex > 0) return {p.blockIndex - 1, BlockTextLength(p.blockIndex - 1)};
        return p;
    }
    std::string text = BlockText(p.blockIndex);
    int i = p.byteOffset;
    auto prevByteIsWord = [&](int at) {
        int prev = PreviousCharOffset(text, at);
        return IsWordByte(static_cast<unsigned char>(text[prev]));
    };
    while (i > 0 && !prevByteIsWord(i)) i = PreviousCharOffset(text, i);
    while (i > 0 && prevByteIsWord(i)) i = PreviousCharOffset(text, i);
    return {p.blockIndex, i};
}

RichDocPosition UCRichDocumentEditor::BlockEnd(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    return {p.blockIndex, BlockTextLength(p.blockIndex)};
}

RichDocPosition UCRichDocumentEditor::DocumentStart() const {
    return {0, 0};
}

RichDocPosition UCRichDocumentEditor::DocumentEnd() const {
    int last = std::max(0, GetBlockCount() - 1);
    return {last, BlockTextLength(last)};
}

RichDocRange UCRichDocumentEditor::WordAt(const RichDocPosition& pos) const {
    RichDocPosition p = ClampPosition(pos);
    std::string text = BlockText(p.blockIndex);
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

void UCRichDocumentEditor::CommitStep(UndoStep step) {
    if (applyingUndo) return;
    modified = true;

    if (coalescing && step.typing && !undoStack.empty()) {
        UndoStep& last = undoStack.back();
        if (last.typing && last.firstBlock == step.firstBlock
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
    if (undoStack.size() > maxUndoSteps) {
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
    EnsureNotEmpty();
    caret = ClampPosition(step.caretBefore);
    anchor = ClampPosition(step.anchorBefore);
    applyingUndo = false;

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
    EnsureNotEmpty();
    caret = ClampPosition(step.caretAfter);
    anchor = ClampPosition(step.anchorAfter);
    applyingUndo = false;

    undoStack.push_back(std::move(step));
    coalescing = false;
    modified = true;
    NotifyChanged();
    NotifySelectionChanged();
    return true;
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
    int firstBlock = range.start.blockIndex;
    int lastBlock = std::min(range.end.blockIndex, GetBlockCount() - 1);

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

    const int blockCount = static_cast<int>(doc->blocks.size());
    const RichDocPosition start = ClampPosition(from);

    // One sweep from `start` to the end of the document (or its beginning),
    // then — with wrapAround — a second sweep over the part not yet seen. The
    // block holding `start` is visited twice on a wrap, which is what lets a
    // single match be found again from the far side of it.
    auto sweep = [&](int firstBlock, int lastBlock, size_t firstOffset,
                     bool useFirstOffset) -> bool {
        const int step = backwards ? -1 : 1;
        for (int b = firstBlock; ; b += step) {
            if (backwards ? b < lastBlock : b > lastBlock) break;
            if (b < 0 || b >= blockCount) break;

            const std::string text = BlockText(b);
            if (!text.empty()) {
                size_t origin;
                if (useFirstOffset && b == firstBlock) {
                    origin = std::min(firstOffset, text.size());
                } else {
                    origin = backwards ? text.size() : 0;
                }
                const size_t at = FindInText(text, needle, origin, backwards,
                                             options.caseSensitive, options.wholeWord);
                if (at != std::string::npos) {
                    outMatch = RichDocRange(
                        RichDocPosition(b, static_cast<int>(at)),
                        RichDocPosition(b, static_cast<int>(at + needle.size())));
                    return true;
                }
            }
            if (b == lastBlock) break;
        }
        return false;
    };

    if (!backwards) {
        if (sweep(start.blockIndex, blockCount - 1,
                  static_cast<size_t>(start.byteOffset), true)) {
            return true;
        }
        if (!options.wrapAround) return false;
        return sweep(0, start.blockIndex, 0, false);
    }

    if (sweep(start.blockIndex, 0, static_cast<size_t>(start.byteOffset), true)) {
        return true;
    }
    if (!options.wrapAround) return false;
    return sweep(blockCount - 1, start.blockIndex, 0, false);
}

std::vector<RichDocRange> UCRichDocumentEditor::FindAll(
        const std::string& needle, const RichFindOptions& options) const {
    std::vector<RichDocRange> matches;
    if (needle.empty()) return matches;

    for (int b = 0; b < static_cast<int>(doc->blocks.size()); b++) {
        const std::string text = BlockText(b);
        if (text.empty()) continue;
        size_t at = 0;
        while ((at = FindInText(text, needle, at, /*backwards*/ false,
                                options.caseSensitive, options.wholeWord))
               != std::string::npos) {
            matches.emplace_back(RichDocPosition(b, static_cast<int>(at)),
                                 RichDocPosition(b, static_cast<int>(at + needle.size())));
            // Matches do not overlap: carry on past this one.
            at += needle.size();
            if (at > text.size()) break;
        }
    }
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
        if (i > 0) SplitBlockInternal();
        if (paragraphs[i].empty()) continue;

        RichDocBlock& block = doc->blocks[caret.blockIndex];
        if (!IsTextBlockType(block.type)) {
            // Typing at an image or a rule starts a paragraph after it.
            RichDocBlock paragraph;
            paragraph.type = RichBlockType::Paragraph;
            doc->blocks.insert(doc->blocks.begin() + caret.blockIndex + 1, paragraph);
            caret = RichDocPosition(caret.blockIndex + 1, 0);
        }
        RichDocBlock& target = doc->blocks[caret.blockIndex];
        InsertIntoRuns(target.runs, caret.byteOffset, paragraphs[i], format);
        caret.byteOffset += static_cast<int>(paragraphs[i].size());
        anchor = caret;
    }
    pendingFormatValid = false;
}

void UCRichDocumentEditor::InsertText(const std::string& utf8) {
    if (utf8.empty()) return;
    RichDocRange selection = GetSelectionRange();
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

void UCRichDocumentEditor::InsertLineBreak() {
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);
        RichDocBlock& block = doc->blocks[caret.blockIndex];
        if (IsTextBlockType(block.type)) {
            const RichTextRun* format = pendingFormatValid ? &pendingFormat : nullptr;
            InsertIntoRuns(block.runs, caret.byteOffset, "", format, /*lineBreakBefore*/ true);
            caret.byteOffset += 1;      // the '\n' the break contributes
            anchor = caret;
        }
    }
    NotifyChanged();
    NotifySelectionChanged();
}

void UCRichDocumentEditor::SplitBlockInternal() {
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
        } else {
            block.type = RichBlockType::Paragraph;
            block.orderedList = false;
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
        std::string text = BlockText(caret.blockIndex);
        int from = PreviousCharOffset(text, caret.byteOffset);
        DeleteRange(RichDocRange({caret.blockIndex, from}, caret));
        return true;
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

    std::string text = BlockText(caret.blockIndex);
    if (caret.byteOffset < static_cast<int>(text.size())) {
        int to = NextCharOffset(text, caret.byteOffset);
        DeleteRange(RichDocRange(caret, {caret.blockIndex, to}));
        return true;
    }
    if (caret.blockIndex + 1 >= GetBlockCount()) return false;

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
    if (!IsTextBlock(p.blockIndex)) return {};
    const RichDocBlock& block = doc->blocks[p.blockIndex];
    if (const RichTextRun* run = RunAtOffset(block.runs, p.byteOffset)) {
        RichTextRun copy = *run;
        copy.text.clear();
        copy.lineBreakBefore = false;
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
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::ToggleList(bool ordered) {
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    bool allSameList = true;
    for (int b = first; b <= last && b < GetBlockCount(); b++) {
        const RichDocBlock& block = doc->blocks[b];
        if (!IsTextBlockType(block.type)) continue;
        if (block.type != RichBlockType::ListItem || block.orderedList != ordered) {
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
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (block.type == RichBlockType::ListItem && block.listLevel < 8) {
                block.listLevel++;
            }
        }
    }
    NotifyChanged();
}

void UCRichDocumentEditor::OutdentList() {
    int first = 0, last = 0;
    SelectedBlockRange(first, last);
    {
        EditScope scope(*this, first, last - first + 1);
        for (int b = first; b <= last && b < GetBlockCount(); b++) {
            RichDocBlock& block = doc->blocks[b];
            if (block.type != RichBlockType::ListItem) continue;
            if (block.listLevel > 0) {
                block.listLevel--;
            } else {
                block.type = RichBlockType::Paragraph;
                block.orderedList = false;
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

void UCRichDocumentEditor::InsertHorizontalRule() {
    InsertStructuralBlock(RichBlockType::HorizontalRule);
}

void UCRichDocumentEditor::InsertPageBreak() {
    InsertStructuralBlock(RichBlockType::PageBreak);
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
            image.imageWidthPt = static_cast<float>(width);
            image.imageHeightPt = static_cast<float>(height);
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

// ===== CLIPBOARD SUPPORT =====

std::vector<RichDocBlock> UCRichDocumentEditor::ExtractRange(const RichDocRange& range) const {
    std::vector<RichDocBlock> out;
    if (range.IsEmpty()) return out;

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

void UCRichDocumentEditor::InsertBlocks(const std::vector<RichDocBlock>& blocks) {
    if (blocks.empty()) return;
    RichDocRange selection = GetSelectionRange();
    int first = selection.start.blockIndex;
    int count = selection.end.blockIndex - first + 1;
    {
        EditScope scope(*this, first, count);
        if (HasSelection()) DeleteRangeInternal(selection);

        if (blocks.size() == 1 && IsTextBlockType(blocks[0].type)
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
    NotifyChanged();
    NotifySelectionChanged();
}

} // namespace UltraCanvas
