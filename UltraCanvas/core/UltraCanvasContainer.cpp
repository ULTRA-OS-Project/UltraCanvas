// UltraCanvasContainer.cpp
// Container with scrollbars and child management. Child storage lives on
// CSSLayout::Element (via UltraCanvasUIElement); we iterate it through
// Children() and static_pointer_cast each element to UltraCanvasUIElement.
// Version: 4.1.3
// Last Modified: 2026-07-13
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasElementDebug.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    using namespace CSSLayout;

    // Local helper: every child of a Container is actually a UI element.
    static inline UltraCanvasUIElement* asUI(const std::shared_ptr<CSSLayout::Element>& c) {
        return static_cast<UltraCanvasUIElement*>(c.get());
    }

//    UltraCanvasContainer::~UltraCanvasContainer() = default;

    void UltraCanvasContainer::SetWindow(UltraCanvasWindowBase *win) {
        UltraCanvasUIElement::SetWindow(win);
        for (auto& c : Children()) {
            asUI(c)->SetWindow(win);
        }
        horizontalScrollbar->SetWindow(win);
        verticalScrollbar->SetWindow(win);
    }

    // if container unfocused then child elements also unfocused
    bool UltraCanvasContainer::SetFocus(bool on) {
        if (UltraCanvasUIElement::SetFocus(on)) {
            return true;
        }
        if (!on) {
            if (window) {
                UltraCanvasUIElement* focusedElem = window->GetFocusedElement();
                if (focusedElem) {
                    if (focusedElem->IsDescendantOf(this)) {
                        window->ClearFocus();
                        return true;
                    }
                }
            }
        }
        return false;
    }


    void UltraCanvasContainer::Arrange(const Rect2Df& finalRect, const LayoutContext& ctx) {
        UltraCanvasUIElement::Arrange(finalRect, ctx);

        // Post-layout housekeeping (z-order sort, scrollbar dimensions).
        SortChildrenByZOrder();
        UpdateScrollability();

        internalLayoutValid = true;
    }

    // ===== RENDERING IMPLEMENTATION =====

    void UltraCanvasContainer::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        auto ca = GetContentArea();
        auto hsroll = GetHorizontalScrollPosition();
        auto vsroll = GetVerticalScrollPosition();

        for (auto& c : Children()) {
            UltraCanvasUIElement* child = asUI(c);
            if (!child || !child->IsVisible()) continue;
            if (child->isPopup) continue;

            // Child finalBounds are border-box-relative to this container's own
            // origin (the CSS engine places in-flow children at border+padding+pos),
            // so we do NOT re-add the content origin (ca.x/ca.y) here — that would
            // double-count this container's left/top border+padding. The render ctx
            // is already translated to this container's top-left; ca is still used
            // below to clip children to the content area.
            Rect2Di adjustedChildBounds = child->GetBounds();
            adjustedChildBounds.x = adjustedChildBounds.x - hsroll;
            adjustedChildBounds.y = adjustedChildBounds.y - vsroll;

            Rect2Di contentAreaIntersection;
            if (!adjustedChildBounds.Intersects(ca, contentAreaIntersection)) continue;
            // Skip children whose drawn area doesn't touch the dirty region.
            if (!contentAreaIntersection.Intersects(dirtyRect)) continue;

            ctx->PushState();
            ctx->ClipRect(Rect2Di(contentAreaIntersection.x - 1, contentAreaIntersection.y - 1, contentAreaIntersection.width + 2,
                                  contentAreaIntersection.height + 2));
            ctx->Translate(adjustedChildBounds.TopLeft());

            // Translate dirtyRect into the child's local space — must use the
            // same compound offset we just translated ctx by.
            Rect2Di childDirty(dirtyRect.x - adjustedChildBounds.x,
                               dirtyRect.y - adjustedChildBounds.y,
                               dirtyRect.width, dirtyRect.height);
            child->Render(ctx, childDirty);
            ctx->PopState();
        }

        ctx->PushState();
        RenderScrollbars(ctx, dirtyRect);
        ctx->PopState();
    }

    void UltraCanvasContainer::RenderScrollbars(IRenderContext *ctx, const Rect2Df& dirtyRect) {
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth()  + GetPaddingTop();

        auto renderScrollbar = [&](UltraCanvasScrollbar* sb) {
            auto sbBounds = sb->GetBounds();
            Point2Df compoundOffset(sbBounds.x + localContentX, sbBounds.y + localContentY);
            Rect2Df sbDirty(dirtyRect.x - compoundOffset.x,
                            dirtyRect.y - compoundOffset.y,
                            dirtyRect.width, dirtyRect.height);
            ctx->PushState();
            ctx->Translate(compoundOffset);
            sb->Render(ctx, sbDirty);
            ctx->PopState();
        };

        if (verticalScrollbar->IsVisible()) {
            renderScrollbar(verticalScrollbar.get());
        }

        if (horizontalScrollbar->IsVisible()) {
            renderScrollbar(horizontalScrollbar.get());
        }

        if (verticalScrollbar->IsVisible() && horizontalScrollbar->IsVisible()) {
            RenderCorner(ctx);
        }
    }

// ===== EVENT HANDLING IMPLEMENTATION =====
    bool UltraCanvasContainer::OnEvent(const UCEvent& event) {
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }
        if (HandleScrollbarEvents(event)) {
            return true;
        }
        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseEnter || event.type == UCEventType::MouseLeave) {
            return true;
        }
        return false;
    }

// ===== PRIVATE IMPLEMENTATION METHODS =====

    void UltraCanvasContainer::UpdateScrollability() {
        // Children's finalBounds are in this container's BORDER-BOX frame (the CSS
        // engine places them at border+padding+pos). The viewport compared against
        // below (clientRect = GetContentArea()) is the CONTENT box. Measure each
        // child's extent from the content-box origin so content-size and
        // viewport-size share a frame; otherwise the top/left border+padding
        // inflates content-size and a spurious scrollbar appears (the legend's 1px
        // top border made content 1px taller than the viewport, pushing the bottom
        // row out and triggering a vertical scrollbar).
        const float originX = GetBorderLeftWidth() + GetPaddingLeft();
        const float originY = GetBorderTopWidth()  + GetPaddingTop();

        float maxRight = 0;
        float maxBottom = 0;

        for (auto& c : Children()) {
            UltraCanvasUIElement* child = asUI(c);
            if (!child || !child->IsVisible()) continue;
            if (child->isPopup) continue;

            Rect2Df childBounds = child->GetBounds();
            maxRight  = std::max(maxRight,  (childBounds.x - originX) + childBounds.width);
            maxBottom = std::max(maxBottom, (childBounds.y - originY) + childBounds.height);
        }
        verticalScrollbar->SetVisible(style.forceShowVerticalScrollbar);
        horizontalScrollbar->SetVisible(style.forceShowHorizontalScrollbar);

        auto clientRect = GetContentArea();
        horizontalScrollbar->SetScrollDimensions(clientRect.width, maxRight);
        verticalScrollbar->SetScrollDimensions(clientRect.height, maxBottom);

        if (style.autoShowScrollbars) {
            verticalScrollbar->SetVisible(style.autoShowVerticalScrollbar &&
                                          verticalScrollbar->IsScrollable());
            horizontalScrollbar->SetVisible(style.autoShowHorizontalScrollbar &&
                                            horizontalScrollbar->IsScrollable());

            auto newClientRect = GetContentArea();
            if (newClientRect != clientRect) {
                horizontalScrollbar->SetScrollDimensions(newClientRect.width, maxRight);
                verticalScrollbar->SetScrollDimensions(newClientRect.height, maxBottom);

                verticalScrollbar->SetVisible(style.autoShowVerticalScrollbar &&
                                              verticalScrollbar->IsScrollable());
                horizontalScrollbar->SetVisible(style.autoShowHorizontalScrollbar &&
                                                horizontalScrollbar->IsScrollable());
            }
        }

        // Lock the scroll ranges to the FINAL content area (i.e. with the final
        // scrollbar visibility applied). When one scrollbar appears it shrinks the
        // cross-axis viewport by trackSize; if the other scrollbar then appears as a
        // consequence, the first one's viewport was measured before that happened and
        // is trackSize too large. Recomputing here keeps each scrollbar's range in sync
        // with what GetContentArea() returns at render time, so the last trackSize px of
        // content can scroll out from behind the opposite scrollbar instead of staying
        // hidden under it.
        auto finalRect = GetContentArea();
        horizontalScrollbar->SetScrollDimensions(finalRect.width, maxRight);
        verticalScrollbar->SetScrollDimensions(finalRect.height, maxBottom);

        int trackSize = style.scrollbarStyle.trackSize;
        int contentWidth  = GetWidth()  - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int contentHeight = GetHeight() - GetTotalBorderVertical()   - GetTotalPaddingVertical();
        int padAreaWidth  = GetWidth()  - GetTotalBorderHorizontal();
        int padAreaHeight = GetHeight() - GetTotalBorderVertical();

        if (verticalScrollbar->IsVisible()) {
            int sbX = contentWidth + GetPaddingRight() - trackSize;
            int sbY = -GetPaddingTop();
            int sbHeight = padAreaHeight;

            if (horizontalScrollbar->IsVisible()) {
                sbHeight -= trackSize;
            }

            verticalScrollbar->SetBounds(Rect2Di(sbX, sbY, trackSize, sbHeight));
        }
        if (horizontalScrollbar->IsVisible()) {
            int sbX = -GetPaddingLeft();
            int sbY = contentHeight + GetPaddingBottom() - trackSize;
            int sbWidth = padAreaWidth;

            if (verticalScrollbar->IsVisible()) {
                sbWidth -= trackSize;
            }

            horizontalScrollbar->SetBounds(Rect2Di(sbX, sbY, sbWidth, trackSize));
        }
    }


    Rect2Di UltraCanvasContainer::GetContentArea() {
        auto rect = GetLocalContentRect();

        if (verticalScrollbar->IsVisible()) {
            rect.width -= style.scrollbarStyle.trackSize;
        }

        if (horizontalScrollbar->IsVisible()) {
            rect.height -= style.scrollbarStyle.trackSize;
        }

        rect.width  = std::max(0.0f, rect.width);
        rect.height = std::max(0.0f, rect.height);
        return rect;
    }

    bool UltraCanvasContainer::HandleScrollWheel(const UCEvent& event) {
        if (event.type != UCEventType::MouseWheel) return false;
        int contentX = GetBorderLeftWidth() + GetPaddingLeft();
        int contentY = GetBorderTopWidth()  + GetPaddingTop();
        int contentW = GetWidth()  - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal());
        int contentH = GetHeight() - (GetTotalBorderVertical()   + GetTotalPaddingVertical());
        if (!Rect2Di(contentX, contentY, contentW, contentH).Contains(event.pointer)) return false;

        if (event.shift && style.forceShowHorizontalScrollbar) {
            horizontalScrollbar->ScrollByWheel(event.wheelDelta);
            return true;
        } else if (style.forceShowVerticalScrollbar) {
            verticalScrollbar->ScrollByWheel(event.wheelDelta);
            return true;
        }
        return false;
    }

    bool UltraCanvasContainer::HandleScrollbarEvents(const UCEvent& event) {
        if (!Contains(event.pointer) && !verticalScrollbar->IsDragging() && !horizontalScrollbar->IsDragging()) {
            return false;
        }
        bool handled = false;

        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth()  + GetPaddingTop();

        auto scrollbarLocalBounds = [&](UltraCanvasScrollbar* sb) {
            auto b = sb->GetBounds();
            return Rect2Di(b.x + localContentX, b.y + localContentY, b.width, b.height);
        };

        auto dispatchToScrollbar = [&](UltraCanvasScrollbar* sb) {
            UCEvent localEvent = event;
            auto sbLocal = scrollbarLocalBounds(sb);
            localEvent.pointer = event.pointer - sbLocal.TopLeft();
            return sb->OnEvent(localEvent);
        };

        if (verticalScrollbar->IsVisible()) {
            if (scrollbarLocalBounds(verticalScrollbar.get()).Contains(event.pointer) ||
                verticalScrollbar->IsDragging() ||
                event.type == UCEventType::MouseWheel || event.type == UCEventType::MouseLeave) {
                if (dispatchToScrollbar(verticalScrollbar.get())) {
                    handled = true;
                }
            }
        }

        if (!handled && horizontalScrollbar->IsVisible()) {
            if (scrollbarLocalBounds(horizontalScrollbar.get()).Contains(event.pointer) ||
                horizontalScrollbar->IsDragging() ||
                event.type == UCEventType::MouseWheel || event.type == UCEventType::MouseLeave) {
                if (dispatchToScrollbar(horizontalScrollbar.get())) {
                    handled = true;
                }
            }
        }

        return handled;
    }

    void UltraCanvasContainer::OnScrollChanged() {
        RequestRedraw();

        if (onScrollChanged) {
            onScrollChanged(horizontalScrollbar->GetScrollPosition(), verticalScrollbar->GetScrollPosition());
        }
    }

    void UltraCanvasContainer::AddChild(std::shared_ptr<UltraCanvasUIElement> child) {
        if (!child) return;

        auto* prevContainer = child->GetParentContainer();
        if (prevContainer) {
            if (prevContainer == this) {
                // Already a child — move to back to maintain "last added is on top".
                // CSSLayout::Element doesn't expose a reorder primitive, so the
                // cheap path is: remove + re-add.
                CSSLayout::Element::RemoveChild(child.get());
                CSSLayout::Element::AddChild(std::static_pointer_cast<CSSLayout::Element>(child));
                return;
            } else {
                prevContainer->RemoveChild(child);
            }
        }

        // Engine bookkeeping: sets parent, pushes into the inherited children vector,
        // bubbles InvalidateLayout up the engine tree.
        CSSLayout::Element::AddChild(std::static_pointer_cast<CSSLayout::Element>(child));
        child->SetWindow(GetWindow());

        if (onChildAdded) {
            onChildAdded(child.get());
        }
        InvalidateLayout();
    }

    void UltraCanvasContainer::RemoveChild(std::shared_ptr<UltraCanvasUIElement> child) {
        if (!child) return;
        // Check the child is actually ours before tearing it down.
        bool found = false;
        for (auto& c : Children()) {
            if (c.get() == child.get()) { found = true; break; }
        }
        if (!found) return;

        child->SetWindow(nullptr);
        CSSLayout::Element::RemoveChild(child.get());

        InvalidateLayout();

        if (onChildRemoved) {
            onChildRemoved(child.get());
        }
    }

    void UltraCanvasContainer::ClearChildren() {
        for (auto& c : Children()) {
            asUI(c)->SetWindow(nullptr);
        }
        CSSLayout::Element::ClearChildren();
        verticalScrollbar->SetScrollPosition(0);
        horizontalScrollbar->SetScrollPosition(0);
        horizontalScrollbar->SetScrollDimensions(0, 0);
        InvalidateLayout();
    }

    std::shared_ptr<UltraCanvasSpacer> UltraCanvasContainer::AddSpacer(float size) {
        // Fixed-size spacer. Works in both row and column flex containers:
        // for Row, the main-axis (width) is `size`; for Column, the main-axis
        // (height) is `size`. We set both axes so the spacer is `size` square
        // regardless of orientation, and the cross axis collapses to whatever
        // align-items dictates.
        auto sp = std::make_shared<UltraCanvasSpacer>(size, size, 0.0f);
        AddChild(sp);
        return sp;
    }

    std::shared_ptr<UltraCanvasSpacer> UltraCanvasContainer::AddStretchSpacer(float grow) {
        // Zero-size, flex-grow=grow. Absorbs main-axis slack.
        auto sp = std::make_shared<UltraCanvasSpacer>(0.0f, 0.0f, grow);
        AddChild(sp);
        return sp;
    }

    UltraCanvasUIElement *UltraCanvasContainer::FindChildById(const std::string &id) {
        for (auto& c : Children()) {
            UltraCanvasUIElement* child = asUI(c);
            if (child->GetIdentifier() == id) {
                return child;
            }
            if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(child)) {
                if (auto* found = childContainer->FindChildById(id)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    bool UltraCanvasContainer::HasChild(UltraCanvasUIElement* elem) {
        for (auto& c : Children()) {
            UltraCanvasUIElement* child = asUI(c);
            if (child == elem) {
                return true;
            }
            if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(child)) {
                if (childContainer->HasChild(elem)) {
                    return true;
                }
            }
        }
        return false;
    }


    UltraCanvasUIElement* UltraCanvasContainer::FindElementAtPoint(const Point2Df &pos) {
        if (!GetBounds().Contains(pos)) {
            return nullptr;
        }

        // pos arrives in parent coords; convert once for local-coord comparisons.
        Point2Di localPos(pos.x - (int)finalBounds.x, pos.y - (int)finalBounds.y);

        // If pos lies on this container's own visible scrollbar, claim it for the
        // container so the click reaches HandleScrollbarEvents instead of falling
        // through to a child rendered behind the scrollbar.
        int localContentX = GetBorderLeftWidth() + GetPaddingLeft();
        int localContentY = GetBorderTopWidth()  + GetPaddingTop();
        auto sbLocalBounds = [&](UltraCanvasScrollbar* sb) {
            auto b = sb->GetBounds();
            return Rect2Di(b.x + localContentX, b.y + localContentY, b.width, b.height);
        };
        if (verticalScrollbar->IsVisible() && sbLocalBounds(verticalScrollbar.get()).Contains(localPos)) {
            return this;
        }
        if (horizontalScrollbar->IsVisible() && sbLocalBounds(horizontalScrollbar.get()).Contains(localPos)) {
            return this;
        }

        // Children's finalBounds are border-box-relative to this container's origin
        // (i.e. in localPos's frame). Scroll shifts the children, so test the point
        // in that frame with the scroll offset added back. Do NOT subtract the content
        // origin (border+padding) — that lives in each child's finalBounds already.
        Point2Di contentPoint = Point2Di(
                localPos.x + horizontalScrollbar->GetScrollPosition(),
                localPos.y + verticalScrollbar->GetScrollPosition()
                );

        // Check children in reverse order (topmost first) with proper clipping.
        const auto& src = Children();
        for (auto it = src.rbegin(); it != src.rend(); ++it) {
            UltraCanvasUIElement* child = asUI(*it);
            if (!child || !child->IsVisible()) continue;

            Rect2Di childBounds = child->GetBounds();

            if (childBounds.Contains(contentPoint)) {
                // The *click point* must fall inside the visible (scrollbar-free)
                // portion of the child — not merely intersect it. Otherwise clicks
                // in the scrollbar-overlapped strip would be claimed by the child.
                Rect2Di visibleChildBounds = GetVisibleChildBounds(childBounds);
                if (!visibleChildBounds.IsValid() || !visibleChildBounds.Contains(localPos)) {
                    continue;
                }

                if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(child)) {
                    UltraCanvasUIElement* hitElement = childContainer->FindElementAtPoint(contentPoint);
                    if (hitElement) {
                        return hitElement;
                    }
                }
                return child;
            }
        }

        return this;
    }

    // return visible bounds of child in the parent's content area (with scroll position subtracted)
    Rect2Di UltraCanvasContainer::GetVisibleChildBounds(const Rect2Di& childBounds) {
        auto ca = GetContentArea();

        // childBounds is border-box-relative to this container's origin (same frame
        // as ca), so only scroll is applied here — the content origin is NOT re-added.
        Rect2Di adjustedChildBounds(
                childBounds.x - GetHorizontalScrollPosition(),
                childBounds.y - GetVerticalScrollPosition(),
                childBounds.width,
                childBounds.height
        );

        Rect2Di intersection;
        if (adjustedChildBounds.Intersects(ca, intersection)) {
            return intersection;
        }

        return Rect2Di::INVALID;
    }

    bool UltraCanvasContainer::IsChildVisible(UltraCanvasUIElement* child) {
        if (!child || !child->IsVisible()) {
            return false;
        }
        Rect2Di childBounds = child->GetBounds();
        return GetVisibleChildBounds(childBounds).IsValid();
    }

    void UltraCanvasContainer::SetContainerStyle(const ContainerStyle &newStyle) {
        style = newStyle;
        // Propagate the (possibly changed) scrollbarStyle to the live scrollbar
        // objects; otherwise styling set after construction would be ignored.
        ApplyStyleToScrollbars();
        InvalidateLayout();
    }

    void UltraCanvasContainer::SetShowHorizontalScrollbar(bool show) {
        style.autoShowScrollbars = false;
        horizontalScrollbar->SetVisible(show);
        InvalidateLayout();
    }

    void UltraCanvasContainer::SetShowVerticalScrollbar(bool show) {
        style.autoShowScrollbars = false;
        verticalScrollbar->SetVisible(show);
        InvalidateLayout();
    }

    bool UltraCanvasContainer::ScrollToHorizontal(int position) {
        return horizontalScrollbar->SetScrollPosition(position);
    }

    bool UltraCanvasContainer::ScrollToVertical(int position) {
        return verticalScrollbar->SetScrollPosition(position);
    }

    bool UltraCanvasContainer::ScrollByHorizontal(int delta) {
        return horizontalScrollbar->ScrollBy(delta);
    }

    bool UltraCanvasContainer::ScrollByVertical(int delta) {
        return verticalScrollbar->ScrollBy(delta);
    }

    void UltraCanvasContainer::SetBounds(const Rect2Df& bounds) {
        UltraCanvasUIElement::SetBounds(bounds);
        InvalidateLayout();
    }

    void UltraCanvasContainer::RenderCorner(IRenderContext *ctx) {
        // intentionally empty for now
    }

    void UltraCanvasContainer::CreateScrollbars() {
        verticalScrollbar = std::make_unique<UltraCanvasScrollbar>(
                GetIdentifier() + "_vscroll", 0, 0, style.scrollbarStyle.trackSize, 100,
                ScrollbarOrientation::Vertical);

        verticalScrollbar->onScrollChange = [this](int pos) {
            OnScrollChanged();
        };
        verticalScrollbar->SetVisible(false);
        // Scrollbars are owned via unique_ptr, NOT in the children vector,
        // but they still need the parent link so InvalidateRect /
        // GetPositionInWindow can walk up to compute window-relative coords.
        verticalScrollbar->SetParentNonOwning(this);
        // We render/hit-test them as fixed chrome in our content box (see
        // RenderScrollbars/HandleScrollbarEvents); tell them so their
        // self-invalidation uses the same transform instead of the generic walk.
        verticalScrollbar->SetFixedInParentContentBox(true);

        horizontalScrollbar = std::make_unique<UltraCanvasScrollbar>(
                GetIdentifier() + "_hscroll", 0, 0, 100, style.scrollbarStyle.trackSize,
                ScrollbarOrientation::Horizontal);

        horizontalScrollbar->onScrollChange = [this](int pos) {
            OnScrollChanged();
        };
        horizontalScrollbar->SetVisible(false);
        horizontalScrollbar->SetParentNonOwning(this);
        horizontalScrollbar->SetFixedInParentContentBox(true);

        ApplyStyleToScrollbars();
    }

    void UltraCanvasContainer::ApplyStyleToScrollbars() {
        verticalScrollbar->SetStyle(style.scrollbarStyle);
        horizontalScrollbar->SetStyle(style.scrollbarStyle);
    }

    void UltraCanvasContainer::SortChildrenByZOrder() {
        SortChildren([](const std::shared_ptr<CSSLayout::Element>& a,
                        const std::shared_ptr<CSSLayout::Element>& b) {
            if (!a) return true;
            if (!b) return false;
            return a->zIndex < b->zIndex;
        });
    }
} // namespace UltraCanvas
