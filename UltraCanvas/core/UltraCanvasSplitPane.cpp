// core/UltraCanvasSplitPane.cpp
// Split pane container that divides content into N draggable panes along one axis
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "UltraCanvasSplitPane.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =====================================================================
// UltraCanvasSplitter
// =====================================================================

    UltraCanvasSplitter::UltraCanvasSplitter(const std::string& id, SplitOrientation orient,
                                             UltraCanvasSplitPane* ownerPane)
            : UltraCanvasUIElement(id, 0, 0, 0, 0),
              orientation(orient),
              owner(ownerPane) {
        SetMouseCursor(orientation == SplitOrientation::Horizontal
                       ? UCMouseCursor::SizeWE
                       : UCMouseCursor::SizeNS);
    }

    void UltraCanvasSplitter::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!style.showSplitterBackground) return;

        Color fill = style.splitterColor;
        if (dragging)         fill = style.splitterActiveColor;
        else if (IsHovered()) fill = style.splitterHoverColor;

        ctx->SetFillPaint(fill);
        ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));
    }

    bool UltraCanvasSplitter::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseEnter:
                SetHovered(true);
                RequestRedraw();
                return true;

            case UCEventType::MouseLeave:
                SetHovered(false);
                RequestRedraw();
                return true;

            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && owner) {
                    dragging = true;
                    dragStartGlobalAxis = (orientation == SplitOrientation::Horizontal)
                                          ? event.pointerGlobal.x
                                          : event.pointerGlobal.y;
                    if (auto* app = UltraCanvasApplication::GetInstance()) {
                        app->CaptureMouse(this);
                    }
                    owner->BeginSplitterDrag(index);
                    RequestRedraw();
                    return true;
                }
                return false;

            case UCEventType::MouseMove:
                if (dragging && owner) {
                    int currentAxis = (orientation == SplitOrientation::Horizontal)
                                      ? event.pointerGlobal.x
                                      : event.pointerGlobal.y;
                    int delta = currentAxis - dragStartGlobalAxis;
                    owner->UpdateSplitterDrag(index, delta);
                    // Force a serviced relayout+paint this frame, independent of any
                    // stale arrangeValid left by concurrent (e.g. debug-session)
                    // invalidations that break the upward InvalidateLayout bubble.
                    // Invalidating the WINDOW (the root) cannot be defeated by the
                    // Element::InvalidateLayout early-return since it has no parent to
                    // bubble to; UpdateAndRender then re-Arranges down to PerformLayout,
                    // applying the new pane weights — the same geometry pass a resize gets.
                    if (auto* w = owner->GetWindow()) w->InvalidateLayout();
                    RequestRedraw();
                    return true;
                }
                return false;

            case UCEventType::MouseUp:
                if (dragging) {
                    dragging = false;
                    if (owner) {
                        owner->EndSplitterDrag(index);
                        // Guarantee the final committed geometry is laid out and painted.
                        if (auto* w = owner->GetWindow()) w->InvalidateLayout();
                    }
                    RequestRedraw();
                    return true;
                }
                return false;

            default:
                break;
        }
        return UltraCanvasUIElement::OnEvent(event);
    }

// =====================================================================
// UltraCanvasSplitPane
// =====================================================================

    UltraCanvasSplitPane::UltraCanvasSplitPane(const std::string& id, float x, float y, float w, float h,
                                               SplitOrientation orient)
            : UltraCanvasContainer(id, x, y, w, h),
              orientation(orient) {
        ContainerStyle cs = GetContainerStyle();
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);
    }

    void UltraCanvasSplitPane::SetBounds(const Rect2Df& b) {
        UltraCanvasContainer::SetBounds(b);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetSplitPaneStyle(const SplitPaneStyle& s) {
        splitStyle = s;
        for (auto& sp : splitters) sp->SetStyle(splitStyle);
        InvalidateLayout();
    }

    int UltraCanvasSplitPane::AxisLength() const {
        auto area = const_cast<UltraCanvasSplitPane*>(this)->GetContentArea();
        return (orientation == SplitOrientation::Horizontal) ? area.width : area.height;
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::AddPane(double weight) {
        return InsertPane(panes.size(), weight);
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::InsertPane(size_t index, double weight) {
        if (index > panes.size()) index = panes.size();

        std::string paneId = GetIdentifier() + "_pane_" + std::to_string(panes.size());
        auto pane = std::make_shared<UltraCanvasContainer>(paneId);

        PaneSlot slot;
        slot.pane = pane;
        slot.weight = (weight > 0.0) ? weight : 1.0;
        panes.insert(panes.begin() + index, slot);

        AddChild(pane);
        EnsureSplitterCountMatches();
        RebindSplitterIndices();
        InvalidateLayout();
        return pane;
    }

    void UltraCanvasSplitPane::RemovePane(size_t index) {
        if (index >= panes.size()) return;
        auto paneShared = panes[index].pane;
        panes.erase(panes.begin() + index);
        if (paneShared) UltraCanvasContainer::RemoveChild(paneShared);
        EnsureSplitterCountMatches();
        RebindSplitterIndices();
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::RemovePane(UltraCanvasContainer* pane) {
        int idx = GetPaneIndex(pane);
        if (idx >= 0) RemovePane(static_cast<size_t>(idx));
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasSplitPane::GetPane(size_t index) const {
        if (index >= panes.size()) return nullptr;
        return panes[index].pane;
    }

    int UltraCanvasSplitPane::GetPaneIndex(UltraCanvasContainer* pane) const {
        for (size_t i = 0; i < panes.size(); ++i) {
            if (panes[i].pane.get() == pane) return static_cast<int>(i);
        }
        return -1;
    }

    void UltraCanvasSplitPane::SetPaneWeight(size_t index, double weight) {
        if (index >= panes.size()) return;
        panes[index].weight = (weight > 0.0) ? weight : 0.0001;
        InvalidateLayout();
    }

    double UltraCanvasSplitPane::GetPaneWeight(size_t index) const {
        if (index >= panes.size()) return 0.0;
        return panes[index].weight;
    }

    void UltraCanvasSplitPane::SetPaneMinSize(size_t index, int px) {
        if (index >= panes.size()) return;
        panes[index].minSize = std::max(0, px);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetPaneMaxSize(size_t index, int px) {
        if (index >= panes.size()) return;
        panes[index].maxSize = std::max(0, px);
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::SetPaneSizes(const std::vector<int>& pixelSizes) {
        if (pixelSizes.empty() || pixelSizes.size() != panes.size()) return;
        double total = 0.0;
        for (int s : pixelSizes) total += std::max(0, s);
        if (total <= 0.0) return;
        for (size_t i = 0; i < panes.size(); ++i) {
            double w = static_cast<double>(std::max(0, pixelSizes[i])) / total;
            panes[i].weight = (w > 0.0) ? w : 0.0001;
        }
        InvalidateLayout();
    }

    void UltraCanvasSplitPane::EnsureSplitterCountMatches() {
        size_t needed = (panes.size() > 0) ? panes.size() - 1 : 0;
        while (splitters.size() > needed) {
            auto sp = splitters.back();
            splitters.pop_back();
            UltraCanvasContainer::RemoveChild(sp);
        }
        while (splitters.size() < needed) {
            std::string sid = GetIdentifier() + "_splitter_" + std::to_string(splitters.size());
            auto sp = std::make_shared<UltraCanvasSplitter>(sid, orientation, this);
            sp->SetStyle(splitStyle);
            sp->SetZIndex(OverlayZOrder::Controls);
            splitters.push_back(sp);
            AddChild(sp);
        }
    }

    void UltraCanvasSplitPane::RebindSplitterIndices() {
        for (size_t i = 0; i < splitters.size(); ++i) {
            splitters[i]->SetIndex(i);
        }
    }

    std::vector<int> UltraCanvasSplitPane::ComputePaneSizes(int availableAxis) const {
        std::vector<int> sizes(panes.size(), 0);
        if (panes.empty() || availableAxis <= 0) return sizes;

        double totalWeight = 0.0;
        for (const auto& p : panes) totalWeight += p.weight;
        if (totalWeight <= 0.0) totalWeight = static_cast<double>(panes.size());

        // Initial proportional distribution
        int assigned = 0;
        for (size_t i = 0; i < panes.size(); ++i) {
            int s = static_cast<int>(std::round(availableAxis * panes[i].weight / totalWeight));
            sizes[i] = s;
            assigned += s;
        }
        // Push rounding drift onto the last pane
        if (!sizes.empty()) {
            sizes.back() += (availableAxis - assigned);
        }

        // Single-pass clamp; redistribute slack onto unclamped neighbours
        std::vector<bool> clamped(panes.size(), false);
        for (size_t i = 0; i < panes.size(); ++i) {
            int s = sizes[i];
            int minS = panes[i].minSize;
            int maxS = (panes[i].maxSize > 0) ? panes[i].maxSize : availableAxis;
            if (s < minS) { sizes[i] = minS; clamped[i] = true; }
            else if (s > maxS) { sizes[i] = maxS; clamped[i] = true; }
        }

        int sum = 0;
        for (int s : sizes) sum += s;
        int delta = availableAxis - sum;
        if (delta != 0) {
            int unclampedCount = 0;
            for (bool c : clamped) if (!c) ++unclampedCount;
            if (unclampedCount == 0) {
                // All clamped: dump residual on the last pane
                sizes.back() += delta;
                if (sizes.back() < 0) sizes.back() = 0;
            } else {
                int per = delta / unclampedCount;
                int rem = delta - per * unclampedCount;
                for (size_t i = 0; i < panes.size(); ++i) {
                    if (clamped[i]) continue;
                    sizes[i] += per;
                    if (rem != 0) {
                        int step = (rem > 0) ? 1 : -1;
                        sizes[i] += step;
                        rem -= step;
                    }
                    if (sizes[i] < 0) sizes[i] = 0;
                }
            }
        }

        return sizes;
    }

    void UltraCanvasSplitPane::PerformLayout(const CSSLayout::LayoutContext& ctx) {
        EnsureSplitterCountMatches();
        RebindSplitterIndices();

        Rect2Di area = GetContentArea();
        int axisLen = (orientation == SplitOrientation::Horizontal) ? area.width : area.height;
        int crossLen = (orientation == SplitOrientation::Horizontal) ? area.height : area.width;
        int splittersTotal = static_cast<int>(splitters.size()) * splitStyle.splitterThickness;
        int available = std::max(0, axisLen - splittersTotal);

        auto sizes = ComputePaneSizes(available);

        // Size each child through the CSS engine (Measure+Arrange) rather than SetBounds, so
        // each pane's Container::Arrange -> UpdateScrollability runs at its real split size and
        // shows scrollbars when its content overflows.
        auto arrangeChild = [&ctx](UltraCanvasUIElement* child, const Rect2Di& r) {
            if (!child) return;
            Rect2Df rf(r.x, r.y, r.width, r.height);
            CSSLayout::MeasureConstraints mc{
                    { CSSLayout::ConstraintMode::Exact, rf.width },
                    { CSSLayout::ConstraintMode::Exact, rf.height } };
            child->Measure(mc, ctx);
            child->Arrange(rf, ctx);
        };

        int axisOffset = 0;
        for (size_t i = 0; i < panes.size(); ++i) {
            int paneAxis = sizes[i];
            Rect2Di r;
            if (orientation == SplitOrientation::Horizontal) {
                r = Rect2Di(area.x + axisOffset, area.y, paneAxis, crossLen);
            } else {
                r = Rect2Di(area.x, area.y + axisOffset, crossLen, paneAxis);
            }
            arrangeChild(panes[i].pane.get(), r);
            axisOffset += paneAxis;

            if (i < splitters.size()) {
                Rect2Di sr;
                if (orientation == SplitOrientation::Horizontal) {
                    sr = Rect2Di(area.x + axisOffset, area.y, splitStyle.splitterThickness, crossLen);
                } else {
                    sr = Rect2Di(area.x, area.y + axisOffset, crossLen, splitStyle.splitterThickness);
                }
                arrangeChild(splitters[i].get(), sr);
                axisOffset += splitStyle.splitterThickness;
            }
        }
    }

    void UltraCanvasSplitPane::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasContainer::Arrange(finalRect, ctx);
        PerformLayout(ctx);
    }

    void UltraCanvasSplitPane::BeginSplitterDrag(size_t splitterIndex) {
        if (splitterIndex >= splitters.size()) return;
        dragStartPaneSizes.clear();
        dragStartPaneSizes.reserve(panes.size());
        for (auto& slot : panes) {
            if (!slot.pane) { dragStartPaneSizes.push_back(0); continue; }
            int axisSize = (orientation == SplitOrientation::Horizontal)
                           ? slot.pane->GetWidth()
                           : slot.pane->GetHeight();
            dragStartPaneSizes.push_back(axisSize);
        }
        if (onSplitterDragStart) onSplitterDragStart(splitterIndex);
    }

    void UltraCanvasSplitPane::UpdateSplitterDrag(size_t splitterIndex, int delta) {
        if (splitterIndex >= splitters.size()) return;
        size_t a = splitterIndex;
        size_t b = splitterIndex + 1;
        if (b >= panes.size() || dragStartPaneSizes.size() != panes.size()) return;

        int startA = dragStartPaneSizes[a];
        int startB = dragStartPaneSizes[b];
        int pairTotal = startA + startB;

        int newA = startA + delta;

        int minA = panes[a].minSize;
        int maxA = (panes[a].maxSize > 0) ? panes[a].maxSize : pairTotal;
        int minB = panes[b].minSize;
        int maxB = (panes[b].maxSize > 0) ? panes[b].maxSize : pairTotal;

        // Clamp newA against its own min/max, and against what's feasible for B
        int lower = std::max(minA, pairTotal - maxB);
        int upper = std::min(maxA, pairTotal - minB);
        if (lower > upper) {
            // Constraints contradictory: prefer satisfying B's min
            newA = std::max(0, pairTotal - minB);
        } else {
            newA = std::clamp(newA, lower, upper);
        }
        int newB = pairTotal - newA;
        if (newA < 0) newA = 0;
        if (newB < 0) newB = 0;

        // Convert all current pane sizes to weights (only a and b changed)
        std::vector<int> currentSizes = dragStartPaneSizes;
        currentSizes[a] = newA;
        currentSizes[b] = newB;

        double totalPx = 0.0;
        for (int s : currentSizes) totalPx += s;
        if (totalPx <= 0.0) return;

        double totalWeight = 0.0;
        for (const auto& p : panes) totalWeight += p.weight;
        if (totalWeight <= 0.0) totalWeight = static_cast<double>(panes.size());

        // Scale: keep total weight constant; redistribute proportionally to new px sizes
        for (size_t i = 0; i < panes.size(); ++i) {
            double w = (currentSizes[i] / totalPx) * totalWeight;
            panes[i].weight = (w > 0.0) ? w : 0.0001;
        }

        InvalidateLayout();

        if (onWeightsChanged) {
            std::vector<double> weights;
            weights.reserve(panes.size());
            for (const auto& p : panes) weights.push_back(p.weight);
            onWeightsChanged(weights);
        }
    }

    void UltraCanvasSplitPane::EndSplitterDrag(size_t splitterIndex) {
        dragStartPaneSizes.clear();
        if (onSplitterDragEnd) onSplitterDragEnd(splitterIndex);
    }

} // namespace UltraCanvas
