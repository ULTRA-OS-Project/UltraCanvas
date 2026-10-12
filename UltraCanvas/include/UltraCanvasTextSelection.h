// include/UltraCanvasTextSelection.h
// One text selection shared by several labels, the way a browser selects
// across a page: a drag that starts in one paragraph runs on through the next
// ones, Ctrl+A selects them all and Ctrl+C copies what is selected, one label
// to a line. A view built of many labels - an HTML mail, an e-book page -
// gives them one of these; a single label that should be selectable calls
// UltraCanvasLabel::SetSelectable(true), which makes one just for itself.
//
// The labels drive it: a press on one starts the selection there, the drag
// extends it to the label under the pointer (or the nearest one, when the
// pointer is between them), a double-click takes a word and a triple-click
// the label's whole text. Dragging past the top or bottom of the scroll view
// the labels sit in scrolls it. The selection holds the labels weakly - a
// label leaves it as it is destroyed - while each label keeps the selection
// alive, so a view that rebuilds its labels simply makes a new one.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasTimer.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasLabel;
    class UltraCanvasContainer;

    // Create it with std::make_shared: the labels hold it by shared_ptr.
    class UltraCanvasTextSelection : public std::enable_shared_from_this<UltraCanvasTextSelection> {
    public:
        UltraCanvasTextSelection() = default;
        ~UltraCanvasTextSelection();
        UltraCanvasTextSelection(const UltraCanvasTextSelection&) = delete;
        UltraCanvasTextSelection& operator=(const UltraCanvasTextSelection&) = delete;

        // ===== MEMBERSHIP =====
        // Adds a label after the ones already added: the order they are added
        // in is the reading order a drag and a copy follow. Same as
        // label.SetTextSelection(selection); a label belongs to one selection.
        void AddLabel(UltraCanvasLabel& label);
        // Adds every label under `root`, depth first in child order - the
        // document order of a tree built from HTML.
        void AddLabelsIn(UltraCanvasContainer& root);
        // The labels it holds, destroyed ones not counted.
        size_t GetLabelCount() const;

        // ===== THE SELECTION =====
        bool HasSelection() const;
        // The selected text: each label's part, a line break between labels
        // above one another and a tab between labels side by side (the cells
        // of a table row). Pictures in the text are left out, a no-break
        // space is copied as a space and a soft hyphen not at all.
        std::string GetSelectedText() const;
        // Puts GetSelectedText() on the clipboard; false when nothing is
        // selected.
        bool CopyToClipboard() const;
        void SelectAll();
        void ClearSelection();

        // Raised whenever what is selected changes.
        std::function<void()> onSelectionChanged;
        // A right-click on one of the labels, after a click outside the
        // selection has cleared it: the host shows its menu, HasSelection()
        // telling it whether to offer Copy. The label takes the click either
        // way - a right-click never opens a link.
        std::function<void(const UCEvent&)> onContextMenu;

        // ===== DRIVEN BY THE LABELS =====
        // A press at `byteIndex` of `label`'s rendered text: the selection
        // collapses there, or with `extend` (Shift) runs from where it started
        // to there.
        void PressAt(UltraCanvasLabel* label, int byteIndex, bool extend);
        // The drag reached a window point: the selection runs from where it
        // started to the text nearest that point.
        void DragTo(const Point2Di& windowPoint);
        // The drag ended (the button came up).
        void EndDrag();
        // A double-click: the word at `byteIndex`.
        void SelectWordAt(UltraCanvasLabel* label, int byteIndex);
        // A triple-click: the whole of the label's text.
        void SelectLabelText(UltraCanvasLabel* label);
        // The label a press last landed on: the one that takes the keyboard
        // focus for Ctrl+C / Ctrl+A, so the selection is one stop in the
        // Tab order instead of one per paragraph.
        UltraCanvasLabel* GetKeyboardLabel() const { return keyboardLabel; }
        void TakeKeyboard(UltraCanvasLabel* label) { keyboardLabel = label; }

        // Kept up by UltraCanvasLabel::SetTextSelection and the label's
        // destructor: a label joins at the end, or leaves.
        void JoinLabel(UltraCanvasLabel* label);
        void ForgetLabel(UltraCanvasLabel* label);
        // A label's text changed: the selected bytes no longer mean anything.
        void LabelTextChanged(UltraCanvasLabel* label);

    private:
        struct Position {
            int label = -1;      // index into labels; -1: none
            int byte = 0;
        };

        std::vector<UltraCanvasLabel*> labels;   // in reading order; null once destroyed
        std::unordered_map<UltraCanvasLabel*, int> indexOf;
        Position anchor, focus;
        // The labels currently given a range, so a change clears exactly those.
        int paintedFirst = -1, paintedLast = -1;
        UltraCanvasLabel* keyboardLabel = nullptr;

        // Auto-scroll while the pointer is dragged past the scroll view.
        bool dragging = false;
        Point2Di lastDragPoint;
        TimerId autoScrollTimer = 0;

        int IndexOf(UltraCanvasLabel* label) const;
        // The text position nearest a window point.
        Position PositionAt(const Point2Di& windowPoint) const;
        // Moves the focus end and repaints what changed.
        void SetFocusPosition(const Position& position);
        // Pushes [anchor, focus] to the labels as byte ranges.
        void Apply();
        void ClearPainted();
        // The scroll view the anchor's label sits in (the nearest container
        // above it that scrolls vertically), and how far to scroll it for a
        // drag at a window point: negative above it, positive below, 0 inside.
        UltraCanvasContainer* ScrollViewOf(UltraCanvasLabel* label) const;
        int AutoScrollStep(const Point2Di& windowPoint) const;
        void StartAutoScroll();
        void StopAutoScroll();
    };

} // namespace UltraCanvas
