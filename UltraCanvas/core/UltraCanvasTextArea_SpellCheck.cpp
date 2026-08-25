// core/UltraCanvasTextArea_SpellCheck.cpp
// Spell check integration and character-range geometry for UltraCanvasTextArea
// Version: 1.0.0
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// Split out of UltraCanvasTextArea.cpp for the same reason as the _Markdown and
// _Hex units: the component is large and these concerns are self-contained.
//
// COORDINATE SYSTEMS IN PLAY
//   document byte   - offset into textContent, which is the verbatim
//                     concatenation of every entry of `lines`
//   line / column   - LineColumnIndex: index into `lines`, plus a codepoint
//                     column within that entry (source coordinates)
//   visible cp      - codepoint index into a line layout's rendered text, with
//                     markdown block prefixes and inline markup stripped
//   screen pixels   - what GetCharacterRangeBounds returns
//
// `lines` is sharded: a very long logical line is split across several entries
// (see utf8_split_lines_sharded), so SpellError::lineIndex - which simply
// counts newlines in the checked text - is NOT an index into `lines`. Every
// mapping here starts from the byte offset, which is always exact.

#include "UltraCanvasTextArea.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasUtilsUtf8.h"

#include <algorithm>

namespace UltraCanvas {

// ===================================================================
// DOCUMENT BYTE OFFSET <-> LINE/COLUMN
// ===================================================================

bool UltraCanvasTextArea::ByteOffsetToLineColumn(size_t byteOffset, LineColumnIndex& out) const {
    size_t consumed = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const size_t lineBytes = lines[i].size();
        // The final line is inclusive of its end so an offset pointing just
        // past the last character still resolves.
        const bool isLastLine = (i + 1 == lines.size());
        if (byteOffset < consumed + lineBytes || (isLastLine && byteOffset <= consumed + lineBytes)) {
            out.lineIndex = static_cast<int>(i);
            out.columnIndex = utf8_byte_to_cp(lines[i], byteOffset - consumed);
            return true;
        }
        consumed += lineBytes;
    }
    return false;
}

bool UltraCanvasTextArea::LineColumnToByteOffset(const LineColumnIndex& idx, size_t& out) const {
    if (idx.lineIndex < 0 || idx.lineIndex >= static_cast<int>(lines.size())) return false;

    size_t consumed = 0;
    for (int i = 0; i < idx.lineIndex; ++i) {
        consumed += lines[i].size();
    }
    out = consumed + utf8_cp_to_byte(lines[idx.lineIndex], idx.columnIndex);
    return true;
}

// ===================================================================
// CHARACTER RANGE -> SCREEN RECTANGLES
// ===================================================================

std::vector<Rect2Df> UltraCanvasTextArea::GetCharacterRangeBounds(size_t startByte,
                                                                  size_t byteLength) {
    std::vector<Rect2Df> bounds;
    if (byteLength == 0 || lines.empty()) return bounds;
    if (editingMode == TextAreaEditingMode::Hex) return bounds;

    LineColumnIndex startPos;
    LineColumnIndex endPos;
    if (!ByteOffsetToLineColumn(startByte, startPos)) return bounds;
    if (!ByteOffsetToLineColumn(startByte + byteLength, endPos)) return bounds;

    const int viewportTop = static_cast<int>(verticalScrollOffset);
    const int viewportBottom = viewportTop + static_cast<int>(visibleTextArea.height);

    for (int lineIndex = startPos.lineIndex; lineIndex <= endPos.lineIndex; ++lineIndex) {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) break;

        LineLayoutBase* line = GetActualLineLayout(lineIndex);
        if (!line || !line->layout) continue;

        // Skip lines outside the viewport rather than measuring them.
        const int lineTop = static_cast<int>(line->bounds.y);
        const int lineBottom = lineTop + static_cast<int>(line->bounds.height);
        if (lineBottom < viewportTop || lineTop > viewportBottom) continue;

        const int lineCpCount = GetLineVisibleLength(lineIndex);
        const int sourceStartCp = (lineIndex == startPos.lineIndex) ? startPos.columnIndex : 0;
        const int sourceEndCp   = (lineIndex == endPos.lineIndex) ? endPos.columnIndex : lineCpCount;
        if (sourceEndCp <= sourceStartCp) continue;

        AppendLineRangeBounds(line, sourceStartCp, sourceEndCp, bounds);
    }

    return bounds;
}

// Emits one rectangle per visual line the [startCp, endCp) source range covers
// within a single line layout. Split on the layout's own line ranges so a soft
// wrap produces two rectangles rather than one spanning backwards.
void UltraCanvasTextArea::AppendLineRangeBounds(LineLayoutBase* line,
                                                int sourceStartCp,
                                                int sourceEndCp,
                                                std::vector<Rect2Df>& outBounds) {
    if (!line || !line->layout) return;

    const std::string layoutText = line->layout->GetText();
    const int layoutCpCount = utf8_length(layoutText);

    // Markdown lines render markup-stripped text, so the source columns must be
    // translated through the line's cpMap before they mean anything to the
    // layout. Snapping outward keeps a partially-hidden word fully covered.
    int visibleStartCp = SourceCpToVisibleCp(line->cpMap, sourceStartCp, /*snapForward=*/true);
    int visibleEndCp   = SourceCpToVisibleCp(line->cpMap, sourceEndCp,   /*snapForward=*/false);
    visibleStartCp = std::max(0, std::min(visibleStartCp, layoutCpCount));
    visibleEndCp   = std::max(0, std::min(visibleEndCp, layoutCpCount));
    if (visibleEndCp <= visibleStartCp) return;

    const int rangeStartByte = static_cast<int>(utf8_cp_to_byte(layoutText, visibleStartCp));
    const int rangeEndByte   = static_cast<int>(utf8_cp_to_byte(layoutText, visibleEndCp));

    // Element-space origin of this layout, scroll already applied - the same
    // expression LineColumnToCursorPos uses, so marks land on the glyphs.
    const float originX = visibleTextArea.x + line->bounds.x + line->layoutShift.x
                          - horizontalScrollOffset;
    const float originY = visibleTextArea.y + line->bounds.y + line->layoutShift.y
                          - verticalScrollOffset;

    auto emit = [&](int fromByte, int toByte) {
        if (toByte <= fromByte) return;
        const Rect2Di from = line->layout->GetCursorPos(fromByte).strongPos;
        const Rect2Di to   = line->layout->GetCursorPos(toByte).strongPos;

        Rect2Df rect;
        rect.x = originX + static_cast<float>(std::min(from.x, to.x));
        rect.y = originY + static_cast<float>(from.y);
        rect.width = static_cast<float>(std::abs(to.x - from.x));
        rect.height = static_cast<float>(from.height > 0 ? from.height : to.height);
        if (rect.width > 0.0f && rect.height > 0.0f) outBounds.push_back(rect);
    };

    const std::vector<LayoutLineRange> visualLines = line->layout->GetLineByteRanges();
    if (visualLines.size() <= 1) {
        emit(rangeStartByte, rangeEndByte);
        return;
    }

    for (const LayoutLineRange& visual : visualLines) {
        const int visualStart = visual.startByte;
        const int visualEnd = visual.startByte + visual.lengthBytes;
        const int clippedStart = std::max(rangeStartByte, visualStart);
        const int clippedEnd = std::min(rangeEndByte, visualEnd);
        if (clippedEnd > clippedStart) emit(clippedStart, clippedEnd);
    }
}

// ===================================================================
// RANGE REPLACEMENT
// ===================================================================

bool UltraCanvasTextArea::ReplaceTextRange(size_t startByte, size_t byteLength,
                                           const std::string& replacement) {
    if (isReadOnly) return false;
    if (byteLength == 0 && replacement.empty()) return false;

    LineColumnIndex from;
    LineColumnIndex to;
    if (!ByteOffsetToLineColumn(startByte, from)) return false;
    if (!ByteOffsetToLineColumn(startByte + byteLength, to)) return false;

    // Reuse the selection path so the edit is undoable, reshards correctly and
    // raises onTextChanged exactly like a typed edit.
    //
    // A zero-length range is a pure insertion: HasSelection() is false for an
    // empty span, so DeleteSelection() would return without moving the caret
    // and InsertText would put the text wherever the caret happened to be.
    if (byteLength == 0) {
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd = LineColumnIndex::INVALID;
        SetCursorPosition(from);
    } else {
        selectionStart = from;
        selectionEnd = to;
        DeleteSelection();
    }

    if (!replacement.empty()) {
        InsertText(replacement);
    }
    return true;
}

// ===================================================================
// SPELL CHECK
// ===================================================================

void UltraCanvasTextArea::SetSpellCheckEnabled(bool enabled) {
    if (spellCheckEnabled == enabled) return;
    spellCheckEnabled = enabled;

    if (spellContextId == 0) {
        spellContextId = reinterpret_cast<uint64_t>(this);
    }

    if (enabled) {
        RegisterSpellResultNotifier();
        QueueSpellCheck();
    } else {
        UltraCanvasSpellChecker::Instance().CancelContext(spellContextId);
        spellErrors.clear();
        RequestRedraw();
    }
}

// Results are drained while rendering, so a check that finishes after the
// edit's repaint would sit undelivered until something else redrew the element.
// This asks for that frame.
//
// The notifier runs on the worker thread and can outlive this element, so it
// marshals to the UI thread and both hops are guarded by a liveness flag the
// destructor clears - the same pattern UltraCanvasAlbum uses for its poster
// worker.
void UltraCanvasTextArea::RegisterSpellResultNotifier() {
    if (spellContextId == 0) {
        spellContextId = reinterpret_cast<uint64_t>(this);
    }

    std::shared_ptr<std::atomic<bool>> alive = spellAlive;
    UltraCanvasTextArea* self = this;

    UltraCanvasSpellChecker::Instance().SetContextNotifier(spellContextId,
        [self, alive]() {
            if (!alive->load()) return;
            auto* app = UltraCanvasApplication::GetInstance();
            if (!app) return;
            app->PostToUIThread([self, alive]() {
                if (!alive->load()) return;   // element destroyed meanwhile
                self->RequestRedraw();
            });
        });
}

void UltraCanvasTextArea::SetSpellCheckOptions(const SpellCheckOptions& options) {
    spellOptions = options;
    QueueSpellCheck();
}

void UltraCanvasTextArea::RunSpellCheck() {
    QueueSpellCheck();
}

void UltraCanvasTextArea::QueueSpellCheck() {
    if (!spellCheckEnabled) return;

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();
    if (!service.IsEnabled()) return;

    if (spellContextId == 0) {
        spellContextId = reinterpret_cast<uint64_t>(this);
    }

    DropStaleSpellErrors();

    // Queuing replaces any job still pending for this element, so holding a key
    // down builds no backlog.
    service.QueueCheckText(spellContextId, textContent, spellOptions);
}

// The errors on hand describe the text as it was when the check ran. Until the
// next result arrives they would be painted against the edited text, so any
// whose span no longer holds the word they were raised for is dropped now.
//
// Keeping the ones that still match matters: typing at the end of a document
// leaves every earlier mark in place, so squiggles stay put instead of blinking
// off and back on at every keystroke.
void UltraCanvasTextArea::DropStaleSpellErrors() {
    if (spellErrors.empty()) return;

    const size_t sizeBefore = spellErrors.size();
    spellErrors.erase(
        std::remove_if(spellErrors.begin(), spellErrors.end(),
                       [this](const SpellError& error) {
                           if (error.startByte + error.byteLength > textContent.size()) {
                               return true;
                           }
                           return textContent.compare(error.startByte, error.byteLength,
                                                      error.word) != 0;
                       }),
        spellErrors.end());

    if (spellErrors.size() != sizeBefore) RequestRedraw();
}

const SpellError* UltraCanvasTextArea::GetSpellErrorAtPosition(int x, int y) {
    if (!spellCheckEnabled || spellErrors.empty()) return nullptr;

    const LineColumnIndex hit = PosToLineColumn({x, y});
    if (!hit.IsValid()) return nullptr;

    size_t byteOffset = 0;
    if (!LineColumnToByteOffset(hit, byteOffset)) return nullptr;

    return SpellCheckText::FindErrorAtByteOffset(spellErrors, byteOffset);
}

bool UltraCanvasTextArea::ApplySpellSuggestion(const SpellError& error,
                                               const std::string& replacement) {
    // The error refers to the text as it was when the check ran. Copy the span
    // out before replacing: `error` may point into spellErrors, which
    // ReplaceTextRange invalidates by way of the text-changed path.
    const size_t startByte = error.startByte;
    const size_t byteLength = error.byteLength;

    if (!ReplaceTextRange(startByte, byteLength, replacement)) return false;
    QueueSpellCheck();
    return true;
}

bool UltraCanvasTextArea::ShowSpellSuggestionMenu(const UCEvent& event) {
    if (!spellCheckEnabled) return false;

    const SpellError* hit = GetSpellErrorAtPosition(event.pointer.x, event.pointer.y);
    if (!hit) return false;

    auto window = GetWindow();
    if (!window) return false;

    // The menu outlives this call, and applying a suggestion re-runs the check
    // and rebuilds spellErrors - which `hit` points into. Copy the error so the
    // callbacks below never dereference a freed element.
    const SpellError error = *hit;

    spellSuggestionMenu = std::make_shared<UltraCanvasMenu>(
        "TextAreaSpellSuggestions", 0, 0, 220, 0);
    spellSuggestionMenu->SetMenuType(MenuType::PopupMenu);

    // The element owns the menu, so a raw `this` would outlive a destroyed area
    // if the popup were still open. Weak capture keeps the callbacks safe.
    std::weak_ptr<UltraCanvasTextArea> weakSelf =
        std::static_pointer_cast<UltraCanvasTextArea>(shared_from_this());

    for (MenuItemData& item : UltraCanvasSpellChecker::BuildSuggestionMenuItems(
             error,
             [weakSelf, error](const std::string& replacement) {
                 if (auto self = weakSelf.lock()) {
                     self->ApplySpellSuggestion(error, replacement);
                 }
             },
             [weakSelf]() {
                 if (auto self = weakSelf.lock()) self->RunSpellCheck();
             })) {
        spellSuggestionMenu->AddItem(std::move(item));
    }

    spellSuggestionMenu->OpenMenu(event.pointerWindow, *window, PopupElementSettings());
    return true;
}

void UltraCanvasTextArea::DrawSpellErrorMarks(IRenderContext* ctx) {
    if (!ctx || !spellCheckEnabled) return;

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();

    // Drain anything the worker finished since the last frame. Stale results are
    // already dropped service-side, so whatever arrives matches current text.
    SpellCheckResult fresh;
    if (service.TryTakeResult(spellContextId, fresh)) {
        spellErrors = std::move(fresh.errors);
    }
    if (spellErrors.empty()) return;

    const SpellCheckStyle markStyle = service.GetStyle();

    ctx->PushState();
    for (const SpellError& error : spellErrors) {
        for (const Rect2Df& wordBounds :
             GetCharacterRangeBounds(error.startByte, error.byteLength)) {
            SpellCheckRendering::DrawSpellErrorMark(ctx, wordBounds, markStyle, error.kind);
        }
    }
    ctx->PopState();
}

} // namespace UltraCanvas
