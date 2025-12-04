// UltraCanvasContainer.cpp
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
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

    void UltraCanvasContainer::Render(IRenderContext* ctx) {
        if (!IsVisible() || !ctx) return;

        // Set up rendering scope for this container
        ctx->PushState();

        // Update layout if needed
        if (IsLayoutDirty()) {
//            UpdateChildZOrder();
            if (layout) {
                layout->PerformLayout();
            }
            UpdateScrollability();
            //UpdateScrollbarPositions();
            layoutDirty = false;
        }

        // Update scroll animation if active
//        if (scrollState.animatingScroll && style.smoothScrolling) {
//            UpdateScrollAnimation();
//        }

        ctx->PushState();
        //DrawElementDebug(this);
        UltraCanvasUIElement::Render(ctx);
        auto contentRect = GetContentRect();
        ctx->ClipRect(contentRect);

        ctx->Translate(contentRect.x - GetHorizontalScrollPosition(),
                       contentRect.y - GetVerticalScrollPosition());

        // Render children with scroll offset
        for (const auto &child: children) {
            if (!child || !child->IsVisible()) continue;

            // Apply scroll offset to child rendering
            child->Render(ctx);
        }
        // Remove content clipping
        ctx->PopState();

        RenderScrollbars(ctx);

        ctx->PopState();
    }

    void UltraCanvasContainer::RenderScrollbars(IRenderContext *ctx) {
        if (verticalScrollbar->IsVisible()) {
            verticalScrollbar->Render(ctx);
        }

        if (horizontalScrollbar->IsVisible()) {
            horizontalScrollbar->Render(ctx);
        }

        // Render corner if both scrollbars visible
        if (verticalScrollbar->IsVisible() && horizontalScrollbar->IsVisible()) {
            RenderCorner(ctx);
        }
    }

// ===== EVENT HANDLING IMPLEMENTATION =====
    bool UltraCanvasContainer::OnEvent(const UCEvent& event) {
        if (HandleScrollbarEvents(event)) {
            return true;
        }
        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseEnter || event.type == UCEventType::MouseLeave) {
            return true;
        }

        // Handle base element events
        return UltraCanvasUIElement::OnEvent(event);
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

        auto rect = GetPaddingRect();
        if (verticalScrollbar->IsVisible()) {
            int sbX = rect.x + rect.width - style.scrollbarStyle.trackSize;
            int sbY = rect.y;
            int sbHeight = rect.height;

            if (horizontalScrollbar->IsVisible()) {
                sbHeight -= style.scrollbarStyle.trackSize;
            }

            verticalScrollbar->SetBounds(Rect2Di(sbX, sbY, style.scrollbarStyle.trackSize, sbHeight));
        }
        if (horizontalScrollbar->IsVisible()) {
            int sbX = rect.x;
            int sbY = rect.y + rect.height - style.scrollbarStyle.trackSize;
            int sbWidth = rect.width;

            if (verticalScrollbar->IsVisible()) {
                sbWidth -= style.scrollbarStyle.trackSize;
            }

            horizontalScrollbar->SetBounds(Rect2Di(sbX, sbY, sbWidth, style.scrollbarStyle.trackSize));
        }
    }


    Rect2Di UltraCanvasContainer::GetContentArea() {
        auto rect = GetContentRect();

        rect.x = padding.left + GetBorderLeftWidth();
        rect.y = padding.top + GetBorderTopWidth();

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
        if (!GetContentRect().Contains(Point2Di(event.x, event.y))) return false;

        // Scroll vertically by default, horizontally with Shift
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
        if (!Contains(event.x, event.y)) {
            return false;
        }
        bool handled = false;

        // Check vertical scrollbar
        if (verticalScrollbar->IsVisible()) {
            if (verticalScrollbar->Contains(event.x, event.y) ||
                verticalScrollbar->IsDragging() ||
                event.type == UCEventType::MouseWheel) {
                if (verticalScrollbar->OnEvent(event)) {
                    handled = true;
                }
            }
        }

        // Check horizontal scrollbar
        if (!handled && horizontalScrollbar->IsVisible()) {
            if (horizontalScrollbar->Contains(event.x, event.y) ||
                horizontalScrollbar->IsDragging() ||
                event.type == UCEventType::MouseWheel) {
                if (horizontalScrollbar->OnEvent(event)) {
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
        if (!child || child->GetParentContainer() == this) return;

        // Remove from previous parent if any
        if (auto* element = child.get()) {
            if (auto* prevContainer = element->GetParentContainer()) {
                prevContainer->RemoveChild(child);
            }
        }

        child->SetParentContainer(this);
        child->SetWindow(GetWindow());

        // Add to this container
        children.push_back(child);

        // Update layout and scrolling

        // Notify callbacks
        if (onChildAdded) {
            onChildAdded(child.get());
        }
    }

    void UltraCanvasContainer::RemoveChild(std::shared_ptr<UltraCanvasUIElement> child) {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {
            // Unregister from popup system
//            UltraCanvasPopupRegistry::UnregisterPopupElement(child.get());

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


    UltraCanvasUIElement* UltraCanvasContainer::FindElementAtPoint(int x, int y) {
        // First check if point is within our bounds
        if (!Contains(x, y)) {
            return nullptr;
        }

        auto contentRect = GetContentRect();

        // Convert mouse coordinates to content coordinates
        // accounting for scroll offset AND content area position
        int contentX = (x - contentRect.x) + horizontalScrollbar->GetScrollPosition();
        int contentY = (y - contentRect.y) + verticalScrollbar->GetScrollPosition();

        // Check children in reverse order (topmost first) with proper clipping
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (!(*it) || !(*it)->IsVisible()) {
                continue;
            }

            UltraCanvasUIElement* child = it->get();
            Rect2Di childBounds = child->GetBounds();

            // CRITICAL FIX: Check if content-relative coordinates are within child bounds
            if (childBounds.Contains(contentX, contentY)) {
                // Check if child intersects with visible content area for clipping
                Rect2Di visibleChildBounds = GetVisibleChildBounds(childBounds);

                // Only return child if it's actually visible (not clipped)
                if (visibleChildBounds.width > 0 && visibleChildBounds.height > 0) {
                    // Recursively check child containers with corrected coordinates
                    auto childContainer = dynamic_cast<UltraCanvasContainer*>(child);
                    if (childContainer) {
                        // Pass child-relative coordinates to child container
                        UltraCanvasUIElement* hitElement = childContainer->FindElementAtPoint(contentX, contentY);
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

    void UltraCanvasContainer::ConvertWindowToContainerCoordinates(int &x, int &y) {
        // Get our position in window coordinates
        Point2Di elementPos = GetPositionInWindow();

        // Convert from window coordinates to container coordinates
        x -= elementPos.x;
        y -= elementPos.y;

        // CRITICAL FIX: If coordinates are within content area, adjust for scrolling
        auto contentRect = GetContentRect();
        if (contentRect.Contains(x, y)) {
            // Convert to content-relative coordinates accounting for scroll
            x = (x - contentRect.x) + GetHorizontalScrollPosition();
            y = (y - contentRect.y) + GetVerticalScrollPosition();
        }
    }

    Rect2Di UltraCanvasContainer::GetVisibleChildBounds(const Rect2Di& childBounds) {
        // Calculate child bounds in container coordinates (accounting for scroll)
        auto contentRect = GetContentRect();
        Rect2Di adjustedChildBounds(
                childBounds.x - GetHorizontalScrollPosition() + contentRect.x,
                childBounds.y - GetVerticalScrollPosition() + contentRect.y,
                childBounds.width,
                childBounds.height
        );

        // CRITICAL FIX: Intersect with content area to get visible portion
        Rect2Di intersection;
        if (adjustedChildBounds.Intersects(contentRect, intersection)) {
            return intersection;
        }

        // Return empty rect if no intersection (completely clipped)
        return Rect2Di(0, 0, 0, 0);
    }

    /**
    * Check if a child element is visible (not completely clipped)
    */
    bool UltraCanvasContainer::IsChildVisible(UltraCanvasUIElement* child) {
        if (!child || !child->IsVisible()) {
            return false;
        }

        Rect2Di childBounds = child->GetBounds();
        Rect2Di visibleBounds = GetVisibleChildBounds(childBounds);

        return (visibleBounds.width > 0 && visibleBounds.height > 0);
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

        // Create horizontal scrollbar
        horizontalScrollbar = std::make_unique<UltraCanvasScrollbar>(
                GetIdentifier() + "_hscroll", 0, 0, 0, 100, style.scrollbarStyle.trackSize,
                ScrollbarOrientation::Horizontal);

        horizontalScrollbar->onScrollChange = [this](int pos) {
            OnScrollChanged();
        };
        horizontalScrollbar->SetVisible(false);

        ApplyStyleToScrollbars();
    }

    void UltraCanvasContainer::ApplyStyleToScrollbars() {
        verticalScrollbar->SetStyle(style.scrollbarStyle);
        horizontalScrollbar->SetStyle(style.scrollbarStyle);
    }
} // namespace UltraCanvas