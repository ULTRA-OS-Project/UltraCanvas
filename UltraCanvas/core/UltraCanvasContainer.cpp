// UltraCanvasContainer.cpp
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.1
// Last Modified: 2026-04-08
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasLayout.h"
#include "UltraCanvasElementDebug.h"
//#include "UltraCanvasZOrderManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {
    UltraCanvasContainer::~UltraCanvasContainer() {
        if (layout) {
            delete layout;
        }
    }

// ===== RENDERING IMPLEMENTATION =====
    void UltraCanvasContainer::SetWindow(UltraCanvasWindowBase *win) {
        UltraCanvasUIElement::SetWindow(win);
        // Propagate to children
        for (auto& child : children) {
            child->SetWindow(win);
        }
    }

    void UltraCanvasContainer::UpdateGeometry(IRenderContext* ctx) {
        // Perform layout BEFORE children's UpdateGeometry so that children
        // get their correct bounds from the parent layout before they
        // process their own nested layouts (fixes first-frame positioning)
        bool isContainerGeometryUpdated = false;
        if (needsUpdateGeometry || IsLayoutDirty()) {
            SortChildrenByZOrder();
            if (layout) {
                layout->PerformLayout();
            }
            UpdateScrollability();
            layoutDirty = false;
            isContainerGeometryUpdated = true;
        }

        for (const auto &child: children) {
            // fixme! check this in future when will need to implement feature like visibility:hidden
            if (!child || !child->IsVisible()) continue;

            auto containerChild = dynamic_cast<UltraCanvasContainer*>(child.get());
            if (isContainerGeometryUpdated || child->needsUpdateGeometry || containerChild) {
                child->UpdateGeometry(ctx);
                child->needsUpdateGeometry = false;
            }
        }
        
        if (verticalScrollbar->IsVisible()) {
            verticalScrollbar->UpdateGeometry(ctx);
        }

        if (horizontalScrollbar->IsVisible()) {
            horizontalScrollbar->UpdateGeometry(ctx);
        }

        needsUpdateGeometry = false;
    }

    void UltraCanvasContainer::Render(IRenderContext* ctx) {
        if (needsRedraw) {
            UltraCanvasUIElement::Render(ctx);
        }

        // Render visible children with scroll offset
        auto ca = GetContentArea();
        auto hsroll = GetHorizontalScrollPosition();
        auto vsroll = GetVerticalScrollPosition();
        
        for (const auto &child: children) {
            if (!child || !child->IsVisible()) continue;

            auto containerChild = dynamic_cast<UltraCanvasContainer*>(child.get());
            if (needsRedraw || child->needsRedraw || containerChild) {
                Rect2Di adjustedChildBounds = child->GetBounds();
                adjustedChildBounds.x = adjustedChildBounds.x - hsroll + ca.x;
                adjustedChildBounds.y = adjustedChildBounds.y - vsroll + ca.y;

                Rect2Di intersection;
                if (adjustedChildBounds.Intersects(ca, intersection)) {
                    ctx->PushState();
                    ctx->ClipRect(Rect2Di(intersection.x - 1, intersection.y - 1, intersection.width + 2,
                                          intersection.height + 2));
                    ctx->Translate(adjustedChildBounds.TopLeft());
                    child->needsRedraw = true;
                    child->Render(ctx);
                    ctx->PopState();
                    child->needsRedraw = false;
                }
            }
        }
        ctx->PushState();
        RenderScrollbars(ctx);
        ctx->PopState();
        needsRedraw = false;
    }

    void UltraCanvasContainer::RenderScrollbars(IRenderContext *ctx) {
        int localContentX = GetBorderLeftWidth() + padding.left;
        int localContentY = GetBorderTopWidth() + padding.top;

        auto renderScrollbar = [&](UltraCanvasScrollbar* sb) {
            ctx->PushState();
            auto sbBounds = sb->GetBounds();
            ctx->Translate(Point2Di(sbBounds.x + localContentX, sbBounds.y + localContentY));
            sb->Render(ctx);
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
        int maxRight = 0;
        int maxBottom = 0;

        for (const auto& child : children) {
            if (!child || !child->IsVisible()) continue;

            Rect2Di childBounds = child->GetBounds();
            maxRight = std::max(maxRight, childBounds.x + childBounds.width);
            maxBottom = std::max(maxBottom, childBounds.y + childBounds.height);
        }
        verticalScrollbar->SetVisible(style.forceShowVerticalScrollbar);
        horizontalScrollbar->SetVisible(style.forceShowHorizontalScrollbar);

        auto clientRect = GetContentArea();
        horizontalScrollbar->SetScrollDimensions(clientRect.width, maxRight);
        verticalScrollbar->SetScrollDimensions(clientRect.height, maxBottom);

        if (style.autoShowScrollbars) {
            verticalScrollbar->SetVisible(verticalScrollbar->IsScrollable());
            horizontalScrollbar->SetVisible(horizontalScrollbar->IsScrollable());

            auto newClientRect = GetContentArea();
            if (newClientRect != clientRect) {
                horizontalScrollbar->SetScrollDimensions(clientRect.width, maxRight);
                verticalScrollbar->SetScrollDimensions(clientRect.height, maxBottom);

                // Determine scrollbar visibility
                verticalScrollbar->SetVisible(verticalScrollbar->IsScrollable());
                horizontalScrollbar->SetVisible(horizontalScrollbar->IsScrollable());
            }
        }

        int trackSize = style.scrollbarStyle.trackSize;
        int contentWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
        int contentHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
        int padAreaWidth = GetWidth() - GetTotalBorderHorizontal();
        int padAreaHeight = GetHeight() - GetTotalBorderVertical();

        if (verticalScrollbar->IsVisible()) {
            int sbX = contentWidth + padding.right - trackSize;
            int sbY = -padding.top;
            int sbHeight = padAreaHeight;

            if (horizontalScrollbar->IsVisible()) {
                sbHeight -= trackSize;
            }

            verticalScrollbar->SetBounds(Rect2Di(sbX, sbY, trackSize, sbHeight));
        }
        if (horizontalScrollbar->IsVisible()) {
            int sbX = -padding.left;
            int sbY = contentHeight + padding.bottom - trackSize;
            int sbWidth = padAreaWidth;

            if (verticalScrollbar->IsVisible()) {
                sbWidth -= trackSize;
            }

            horizontalScrollbar->SetBounds(Rect2Di(sbX, sbY, sbWidth, trackSize));
        }
    }


    Rect2Di UltraCanvasContainer::GetContentArea() {
        auto rect = GetLocalContentRect();

//        rect.x = padding.left + GetBorderLeftWidth();
//        rect.y = padding.top + GetBorderTopWidth();

        if (verticalScrollbar->IsVisible()) {
            rect.width -= style.scrollbarStyle.trackSize;
        }

        if (horizontalScrollbar->IsVisible()) {
            rect.height -= style.scrollbarStyle.trackSize;
        }

        // Ensure minimum size
        rect.width = std::max(0, rect.width);
        rect.height = std::max(0, rect.height);
        return rect;
    }

    bool UltraCanvasContainer::HandleScrollWheel(const UCEvent& event) {
        if (event.type != UCEventType::MouseWheel) return false;
        int contentX = GetBorderLeftWidth() + padding.left;
        int contentY = GetBorderTopWidth() + padding.top;
        int contentW = GetWidth() - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal());
        int contentH = GetHeight() - (GetTotalBorderVertical() + GetTotalPaddingVertical());
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
        if (!Contains(event.pointer)) {
            return false;
        }
        bool handled = false;

        int localContentX = GetBorderLeftWidth() + padding.left;
        int localContentY = GetBorderTopWidth() + padding.top;

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
//        UpdateScrollbarPositions();
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
                // if element already here then move it to back (to maintain order)
                for(auto it =children.begin(); it != children.end(); ++it) {
                    if (*it == child) {
                        children.erase(it);
                        children.push_back(child);
                        return;
                    }
                }
            } else {
                prevContainer->RemoveChild(child);
            }
        }


        child->SetParentContainer(this);
        child->SetWindow(GetWindow());

        // Add to this container at end
        children.push_back(child);

        // Notify callbacks
        if (onChildAdded) {
            onChildAdded(child.get());
        }
        InvalidateLayout();
    }

    void UltraCanvasContainer::RemoveChild(std::shared_ptr<UltraCanvasUIElement> child) {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {
            (*it)->SetParentContainer(nullptr);
            (*it)->SetWindow(nullptr);
            if (layout) {
                layout->RemoveUIElement(*it);
            }
            children.erase(it);

            InvalidateLayout();

            // Notify callbacks
            if (onChildRemoved) {
                onChildRemoved(child.get());
            }
        }
    }

    void UltraCanvasContainer::ClearChildren() {
        for (auto& child : children) {
            child->SetParentContainer(nullptr);
            child->SetWindow(nullptr);
//            UltraCanvasPopupRegistry::UnregisterPopupElement(child.get());
            if (layout) {
                layout->RemoveUIElement(child);
            }
        }
        children.clear();
        verticalScrollbar->SetScrollPosition(0);
        horizontalScrollbar->SetScrollPosition(0);
        horizontalScrollbar->SetScrollDimensions(0, 0);
        InvalidateLayout();
    }

    UltraCanvasUIElement *UltraCanvasContainer::FindChildById(const std::string &id) {
        for (const auto& child : children) {
            if (child->GetIdentifier() == id) {
                return child.get();
            }

            // Recursively search in child containers
            if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(child.get())) {
                if (auto* found = childContainer->FindChildById(id)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    bool UltraCanvasContainer::HasChild(UltraCanvasUIElement* elem) {
        for (const auto& child : children) {
            if (child.get() == elem) {
                return true;
            }

            // Recursively search in child containers
            if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(child.get())) {
                if (childContainer->HasChild(elem)) {
                    return true;
                }
            }
        }
        return false;
    }


    UltraCanvasUIElement* UltraCanvasContainer::FindElementAtPoint(const Point2Di &pos) {
        if (!GetBounds().Contains(pos)) {
            return nullptr;
        }

        auto contentRect = GetContentRect();

        Point2Di contentPoint = Point2Di(
                pos.x - contentRect.x + horizontalScrollbar->GetScrollPosition(),
                pos.y - contentRect.y + verticalScrollbar->GetScrollPosition()
                );

        // Check children in reverse order (topmost first) with proper clipping
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (!(*it) || !(*it)->IsVisible()) {
                continue;
            }

            UltraCanvasUIElement* child = it->get();
            Rect2Di childBounds = child->GetBounds();

            // CRITICAL FIX: Check if content-relative coordinates are within child bounds
            if (childBounds.Contains(contentPoint)) {
                // Check if child intersects with visible content area for clipping
                Rect2Di visibleChildBounds = GetVisibleChildBounds(childBounds);

                // Only return child if it's actually visible (not clipped)
                if (visibleChildBounds.IsValid()) {
                    // Recursively check child containers with corrected coordinates
                    auto childContainer = dynamic_cast<UltraCanvasContainer*>(child);
                    if (childContainer) {
                        // Pass child-relative coordinates to child container
                        UltraCanvasUIElement* hitElement = childContainer->FindElementAtPoint(contentPoint);
                        if (hitElement) {
                            return hitElement;
                        }
                    }
                    return child;
                }
            }
        }

        return this; // Hit container but no children
    }

    // return visible bounds of child in the parent's content area (with scroll position subtracted, mean as visible on screen)
    Rect2Di UltraCanvasContainer::GetVisibleChildBounds(const Rect2Di& childBounds) {
        auto ca = GetContentArea();

        Rect2Di adjustedChildBounds(
                childBounds.x - GetHorizontalScrollPosition() + ca.x,
                childBounds.y - GetVerticalScrollPosition() + ca.y,
                childBounds.width,
                childBounds.height
        );

        Rect2Di intersection;
        if (adjustedChildBounds.Intersects(ca, intersection)) {
            return intersection;
        }

        return Rect2Di::INVALID;
    }

    /**
    * Check if a child element is visible (not completely clipped)
    */
    bool UltraCanvasContainer::IsChildVisible(UltraCanvasUIElement* child) {
        if (!child || !child->IsVisible()) {
            return false;
        }

        Rect2Di childBounds = child->GetBounds();
        return GetVisibleChildBounds(childBounds).IsValid();
    }

    void UltraCanvasContainer::SetContainerStyle(const ContainerStyle &newStyle) {
        style = newStyle;
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

    void UltraCanvasContainer::SetLayout(UltraCanvasLayout* newLayout) {
        if (layout) {
            delete layout;
        }
        if (newLayout) {
            layout = newLayout;
            layout->SetParentContainer(this);
        }
        InvalidateLayout();
    }

    void UltraCanvasContainer::SetBounds(const Rect2Di& bounds) {
        UltraCanvasUIElement::SetBounds(bounds);
        InvalidateLayout();
    }

    void UltraCanvasContainer::RenderCorner(IRenderContext *ctx) {
//        if (scrollbarsCornerRect.width > 0 && scrollbarsCornerRect.height > 0) {
//            ctx->DrawFilledRectangle(scrollbarsCornerRect, backgroundColor);
//        }
    }

    void UltraCanvasContainer::CreateScrollbars() {
        // Create vertical scrollbar
        verticalScrollbar = std::make_unique<UltraCanvasScrollbar>(
                GetIdentifier() + "_vscroll", 0, 0, 0, style.scrollbarStyle.trackSize, 100,
                ScrollbarOrientation::Vertical);

        verticalScrollbar->onScrollChange = [this](int pos) {
            OnScrollChanged();
        };
        verticalScrollbar->SetVisible(false);
        verticalScrollbar->SetParentContainer(this);

        // Create horizontal scrollbar
        horizontalScrollbar = std::make_unique<UltraCanvasScrollbar>(
                GetIdentifier() + "_hscroll", 0, 0, 0, 100, style.scrollbarStyle.trackSize,
                ScrollbarOrientation::Horizontal);

        horizontalScrollbar->onScrollChange = [this](int pos) {
            OnScrollChanged();
        };
        horizontalScrollbar->SetVisible(false);
        horizontalScrollbar->SetParentContainer(this);

        ApplyStyleToScrollbars();
    }

    void UltraCanvasContainer::ApplyStyleToScrollbars() {
        verticalScrollbar->SetStyle(style.scrollbarStyle);
        horizontalScrollbar->SetStyle(style.scrollbarStyle);
    }

    void UltraCanvasContainer::SortChildrenByZOrder() {
        std::stable_sort(children.begin(), children.end(),
                         [](const std::shared_ptr<UltraCanvasUIElement>& a, const std::shared_ptr<UltraCanvasUIElement>& b) {
                             if (!a) return true;  // null elements go to beginning
                             if (!b) return false;
                             return a->GetZOrder() < b->GetZOrder();
                         });
    }
} // namespace UltraCanvas