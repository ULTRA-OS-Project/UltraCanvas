// core/UltraCanvasTextSelection.cpp
// A text selection shared by several labels, in their reading order: see
// UltraCanvasTextSelection.h.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasTextSelection.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <tuple>

namespace UltraCanvas {

    namespace {
        // Where a position past every character of a label's text points:
        // the label clamps it to its text's end.
        constexpr int kTextEnd = INT_MAX;
        constexpr unsigned int kAutoScrollIntervalMs = 40;
    }

    UltraCanvasTextSelection::~UltraCanvasTextSelection() {
        StopAutoScroll();
    }

    // ===== MEMBERSHIP =====

    void UltraCanvasTextSelection::AddLabel(UltraCanvasLabel& label) {
        label.SetTextSelection(shared_from_this());
    }

    void UltraCanvasTextSelection::AddLabelsIn(UltraCanvasContainer& root) {
        for (const auto& child : root.GetChildren()) {
            if (!child) continue;
            if (auto* label = dynamic_cast<UltraCanvasLabel*>(child.get())) {
                AddLabel(*label);
            } else if (auto* container = dynamic_cast<UltraCanvasContainer*>(child.get())) {
                AddLabelsIn(*container);
            }
        }
    }

    size_t UltraCanvasTextSelection::GetLabelCount() const {
        return static_cast<size_t>(std::count_if(labels.begin(), labels.end(),
                                                 [](const UltraCanvasLabel* l) { return l != nullptr; }));
    }

    void UltraCanvasTextSelection::JoinLabel(UltraCanvasLabel* label) {
        if (!label || indexOf.count(label)) return;
        indexOf[label] = static_cast<int>(labels.size());
        labels.push_back(label);
    }

    void UltraCanvasTextSelection::ForgetLabel(UltraCanvasLabel* label) {
        auto found = indexOf.find(label);
        if (found == indexOf.end()) return;
        const int index = found->second;
        labels[static_cast<size_t>(index)] = nullptr;
        indexOf.erase(found);
        if (keyboardLabel == label) keyboardLabel = nullptr;
        // An end of the selection went with the label. The other labels are
        // left as they are: this runs from destructors, often while the whole
        // view is being torn down, when touching them is not safe.
        if (anchor.label == index || focus.label == index) {
            anchor = focus = Position{};
            dragging = false;
            StopAutoScroll();
        }
    }

    void UltraCanvasTextSelection::LabelTextChanged(UltraCanvasLabel* label) {
        const int index = IndexOf(label);
        if (index < 0) return;
        const bool involved = anchor.label == index || focus.label == index ||
                              (paintedFirst >= 0 && index >= paintedFirst && index <= paintedLast);
        if (involved) ClearSelection();
    }

    int UltraCanvasTextSelection::IndexOf(UltraCanvasLabel* label) const {
        auto found = indexOf.find(label);
        return found == indexOf.end() ? -1 : found->second;
    }

    // ===== THE SELECTION =====

    bool UltraCanvasTextSelection::HasSelection() const {
        if (paintedFirst < 0) return false;
        for (int i = paintedFirst; i <= paintedLast; ++i) {
            const UltraCanvasLabel* label = labels[static_cast<size_t>(i)];
            if (label && label->HasSelectedRange()) return true;
        }
        return false;
    }

    std::string UltraCanvasTextSelection::GetSelectedText() const {
        std::string out;
        if (paintedFirst < 0) return out;
        bool first = true;
        Rect2Df previous;
        for (int i = paintedFirst; i <= paintedLast; ++i) {
            UltraCanvasLabel* label = labels[static_cast<size_t>(i)];
            if (!label || !label->HasSelectedRange()) continue;
            const Rect2Df bounds = label->GetBoundsInWindow();
            if (!first) {
                // Side by side - their lines overlap in height and this one
                // starts right of the last (cells of a table row): a tab.
                // Otherwise the next block, on a line of its own.
                const bool sideBySide =
                    bounds.y < previous.y + previous.height - 1.f &&
                    bounds.y + bounds.height > previous.y + 1.f &&
                    bounds.x >= previous.x + previous.width - 1.f;
                out += sideBySide ? '\t' : '\n';
            }
            out += label->GetSelectedText();
            previous = bounds;
            first = false;
        }
        return out;
    }

    bool UltraCanvasTextSelection::CopyToClipboard() const {
        const std::string text = GetSelectedText();
        if (text.empty()) return false;
        return SetClipboardText(text);
    }

    void UltraCanvasTextSelection::SelectAll() {
        int firstLabel = -1, lastLabel = -1;
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            if (!labels[static_cast<size_t>(i)]) continue;
            if (firstLabel < 0) firstLabel = i;
            lastLabel = i;
        }
        if (firstLabel < 0) return;
        anchor = Position{firstLabel, 0};
        focus = Position{lastLabel, kTextEnd};
        Apply();
    }

    void UltraCanvasTextSelection::ClearSelection() {
        const bool had = paintedFirst >= 0 || anchor.label >= 0;
        anchor = focus = Position{};
        ClearPainted();
        if (had && onSelectionChanged) onSelectionChanged();
    }

    // ===== DRIVEN BY THE LABELS =====

    void UltraCanvasTextSelection::PressAt(UltraCanvasLabel* label, int byteIndex, bool extend) {
        const int index = IndexOf(label);
        if (index < 0) return;
        keyboardLabel = label;
        dragging = true;
        const Position at{index, std::max(0, byteIndex)};
        if (extend && anchor.label >= 0 && labels[static_cast<size_t>(anchor.label)]) {
            focus = at;
        } else {
            anchor = focus = at;
        }
        Apply();
    }

    void UltraCanvasTextSelection::DragTo(const Point2Di& windowPoint) {
        if (!dragging || anchor.label < 0) return;
        lastDragPoint = windowPoint;
        SetFocusPosition(PositionAt(windowPoint));
        if (AutoScrollStep(windowPoint) != 0) StartAutoScroll();
        else StopAutoScroll();
    }

    void UltraCanvasTextSelection::EndDrag() {
        dragging = false;
        StopAutoScroll();
    }

    void UltraCanvasTextSelection::SelectWordAt(UltraCanvasLabel* label, int byteIndex) {
        const int index = IndexOf(label);
        if (index < 0) return;
        keyboardLabel = label;
        dragging = false;
        const std::pair<int, int> word = label->WordRangeAt(byteIndex);
        anchor = Position{index, word.first};
        focus = Position{index, word.second};
        Apply();
    }

    void UltraCanvasTextSelection::SelectLabelText(UltraCanvasLabel* label) {
        const int index = IndexOf(label);
        if (index < 0) return;
        keyboardLabel = label;
        dragging = false;
        anchor = Position{index, 0};
        focus = Position{index, kTextEnd};
        Apply();
    }

    // ===== INTERNALS =====

    UltraCanvasTextSelection::Position
    UltraCanvasTextSelection::PositionAt(const Point2Di& windowPoint) const {
        const float px = static_cast<float>(windowPoint.x);
        const float py = static_cast<float>(windowPoint.y);
        int beside = -1, lastAbove = -1, firstShown = -1;
        float besideDistance = FLT_MAX;
        Rect2Df besideBounds;
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            UltraCanvasLabel* label = labels[static_cast<size_t>(i)];
            if (!label || !label->IsVisible() || !label->GetWindow()) continue;
            const Rect2Df r = label->GetBoundsInWindow();
            if (r.width <= 0.f || r.height <= 0.f) continue;
            if (firstShown < 0) firstShown = i;
            if (py >= r.y && py < r.y + r.height) {
                // Over the label: the text there.
                if (px >= r.x && px < r.x + r.width) {
                    const int byte = label->TextIndexAtPoint(Point2Df(px - r.x, py - r.y));
                    return Position{i, std::max(0, byte)};
                }
                // Beside it, on its line: the nearest such label.
                const float distance = px < r.x ? r.x - px : px - (r.x + r.width);
                if (distance < besideDistance) {
                    besideDistance = distance;
                    beside = i;
                    besideBounds = r;
                }
            } else if (r.y + r.height <= py) {
                lastAbove = i;
            }
        }
        if (beside >= 0) {
            // The label answers a point left or right of a line with that
            // line's start or end.
            UltraCanvasLabel* label = labels[static_cast<size_t>(beside)];
            const int byte = label->TextIndexAtPoint(Point2Df(px - besideBounds.x, py - besideBounds.y));
            return Position{beside, std::max(0, byte)};
        }
        // Between labels or below them all: the end of the last one above the
        // point. Above them all: the start of the first.
        if (lastAbove >= 0) return Position{lastAbove, kTextEnd};
        if (firstShown >= 0) return Position{firstShown, 0};
        return focus;
    }

    void UltraCanvasTextSelection::SetFocusPosition(const Position& position) {
        if (position.label < 0) return;
        if (position.label == focus.label && position.byte == focus.byte) return;
        focus = position;
        Apply();
    }

    void UltraCanvasTextSelection::Apply() {
        if (anchor.label < 0 || focus.label < 0) {
            ClearPainted();
            if (onSelectionChanged) onSelectionChanged();
            return;
        }
        Position from = anchor, to = focus;
        if (std::tie(to.label, to.byte) < std::tie(from.label, from.byte)) std::swap(from, to);

        // Labels painted before but outside the new span lose their highlight.
        if (paintedFirst >= 0) {
            for (int i = paintedFirst; i <= paintedLast; ++i) {
                if (i >= from.label && i <= to.label) continue;
                if (UltraCanvasLabel* label = labels[static_cast<size_t>(i)]) label->ClearSelectedRange();
            }
        }
        for (int i = from.label; i <= to.label; ++i) {
            UltraCanvasLabel* label = labels[static_cast<size_t>(i)];
            if (!label) continue;
            const int start = i == from.label ? from.byte : 0;
            const int end = i == to.label ? to.byte : kTextEnd;
            label->SetSelectedRange(start, end);
        }
        paintedFirst = from.label;
        paintedLast = to.label;
        if (onSelectionChanged) onSelectionChanged();
    }

    void UltraCanvasTextSelection::ClearPainted() {
        if (paintedFirst >= 0) {
            for (int i = paintedFirst; i <= paintedLast; ++i) {
                if (UltraCanvasLabel* label = labels[static_cast<size_t>(i)]) label->ClearSelectedRange();
            }
        }
        paintedFirst = paintedLast = -1;
    }

    UltraCanvasContainer* UltraCanvasTextSelection::ScrollViewOf(UltraCanvasLabel* label) const {
        if (!label) return nullptr;
        for (UltraCanvasContainer* c = label->GetParentContainer(); c; c = c->GetParentContainer()) {
            if (c->GetVerticalScrollBar().IsVisible()) return c;
        }
        return nullptr;
    }

    int UltraCanvasTextSelection::AutoScrollStep(const Point2Di& windowPoint) const {
        if (anchor.label < 0) return 0;
        UltraCanvasContainer* view = ScrollViewOf(labels[static_cast<size_t>(anchor.label)]);
        if (!view) return 0;
        const Rect2Df r = view->GetBoundsInWindow();
        const float y = static_cast<float>(windowPoint.y);
        // Faster the further the pointer is past the edge.
        auto step = [](float distance) {
            return static_cast<int>(std::clamp(distance / 2.f + 4.f, 4.f, 60.f));
        };
        if (y < r.y) return -step(r.y - y);
        if (y > r.y + r.height) return step(y - (r.y + r.height));
        return 0;
    }

    void UltraCanvasTextSelection::StartAutoScroll() {
        if (autoScrollTimer) return;
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        // The timer outlives nothing: a weak reference, so a tick that comes
        // after the view (and with it this selection) was rebuilt does nothing.
        std::weak_ptr<UltraCanvasTextSelection> weak = weak_from_this();
        autoScrollTimer = app->StartTimer(kAutoScrollIntervalMs, true, [weak](TimerId) {
            auto self = weak.lock();
            if (!self) return;
            const int step = self->dragging ? self->AutoScrollStep(self->lastDragPoint) : 0;
            UltraCanvasContainer* view = self->anchor.label >= 0
                ? self->ScrollViewOf(self->labels[static_cast<size_t>(self->anchor.label)])
                : nullptr;
            if (step == 0 || !view || !view->ScrollByVertical(step)) {
                self->StopAutoScroll();
                return;
            }
            // The text moved under the pointer: the selection follows it.
            self->SetFocusPosition(self->PositionAt(self->lastDragPoint));
        });
    }

    void UltraCanvasTextSelection::StopAutoScroll() {
        if (!autoScrollTimer) return;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(autoScrollTimer);
        autoScrollTimer = 0;
    }

} // namespace UltraCanvas
