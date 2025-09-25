// UltraCanvasContainer.cpp
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasBaseWindow.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
//#include "UltraCanvasZOrderManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== RENDERING IMPLEMENTATION =====
    void UltraCanvasContainer::SetWindow(UltraCanvasWindow *win) {
        UltraCanvasUIElement::SetWindow(win);
        // Propagate to children
        for (auto& child : children) {
            child->SetWindow(win);
        }
    }

    void UltraCanvasContainer::Render() {
        IRenderContext *ctx = GetRenderContext();
        if (!IsVisible() || !ctx) return;

        // Set up rendering scope for this container
        ctx->PushState();

        // Update layout if needed
        if (layoutDirty) {
//            UpdateChildZOrder();
            UpdateLayout();
        }
        auto bounds = GetBounds();
        // Update scroll animation if active
        if (scrollState.animatingScroll && style.smoothScrolling) {
            UpdateScrollAnimation();
        }

        if (style.backgroundColor.a > 0) {
            ctx->SetFillColor(style.backgroundColor);
            ctx->FillRectangle(bounds);
        }

        // Render border
        if (style.borderWidth > 0) {
            ctx->SetStrokeColor(style.borderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRectangle(bounds);
        }

//        if (!window->IsSelectiveRenderingActive()) {
            // Render container background
            ctx->PushState();
            ctx->IntersectClipRect(contentArea);

            ctx->Translate(contentArea.x - scrollState.horizontalPosition,
                      contentArea.y - scrollState.verticalPosition);
            // Set up clipping for content area
            //        Rect2Di clipRect = contentArea;
            //        ctx->SetClipRect(scrollState.horizontalPosition, scrollState.verticalPosition,
            //                            contentArea.width,
            //                            contentArea.height);

            // Render children with scroll offset
            for (const auto &child: children) {
                if (!child || !child->IsVisible()) continue;

                // Apply scroll offset to child rendering
                child->Render();
            }
            // Remove content clipping
            ctx->PopState();
//        }

        if (scrollState.showVerticalScrollbar || scrollState.showHorizontalScrollbar) {
            ctx->PushState();
            ctx->ClearClipRect();

            // Render scrollbars
            if (scrollState.showVerticalScrollbar) {
                RenderVerticalScrollbar(ctx);
            }

            if (scrollState.showHorizontalScrollbar) {
                RenderHorizontalScrollbar(ctx);
            }
            ctx->PopState();
        }
        ctx->PopState();
    }

// ===== EVENT HANDLING IMPLEMENTATION =====
    bool UltraCanvasContainer::OnEvent(const UCEvent& event) {
        if (HandleScrollbarEvents(event)) {
            return true;
        }
        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseEnter || event.type == UCEventType::MouseLeave) {
            return true;
        }

        // Handle container-specific events first
        if (HandleScrollWheel(event)) {
            return true;
        }
        // Don't forward events to childs as event is forwared by UltraCanvasBaseApplication::DispatchEvent
        // to element under cursor or to focused element

        // Handle base element events
        return UltraCanvasUIElement::OnEvent(event);
    }

// ===== PRIVATE IMPLEMENTATION METHODS =====

    void UltraCanvasContainer::UpdateScrollability() {
        UpdateContentSize();

        // Calculate maximum scroll distances
        scrollState.maxVerticalScroll = std::max(0,
                                                 scrollState.contentHeight - contentArea.height);
        scrollState.maxHorizontalScroll = std::max(0,
                                                   scrollState.contentWidth - contentArea.width);

        // Determine scrollbar visibility
        bool needsVerticalScrollbar = style.enableVerticalScrolling &&
                                      (scrollState.maxVerticalScroll > 0);
        bool needsHorizontalScrollbar = style.enableHorizontalScrolling &&
                                        (scrollState.maxHorizontalScroll > 0);

        // Update scrollbar visibility
        if (style.autoHideScrollbars) {
            scrollState.showVerticalScrollbar = needsVerticalScrollbar;
            scrollState.showHorizontalScrollbar = needsHorizontalScrollbar;
        }

        // Clamp current scroll positions
        scrollState.verticalPosition = std::clamp(
                scrollState.verticalPosition, 0, scrollState.maxVerticalScroll);
        scrollState.horizontalPosition = std::clamp(
                scrollState.horizontalPosition, 0, scrollState.maxHorizontalScroll);

        scrollState.targetVerticalPosition = scrollState.verticalPosition;
        scrollState.targetHorizontalPosition = scrollState.horizontalPosition;

        UpdateLayout();
    }

    void UltraCanvasContainer::UpdateContentSize() {
        int maxRight = 0;
        int maxBottom = 0;

        for (const auto& child : children) {
            if (!child || !child->IsVisible()) continue;

            Rect2Di childBounds = child->GetBounds();
            maxRight = std::max(maxRight, childBounds.x + childBounds.width);
            maxBottom = std::max(maxBottom, childBounds.y + childBounds.height);
        }

        scrollState.contentWidth = maxRight + style.paddingRight;
        scrollState.contentHeight = maxBottom + style.paddingBottom;
    }

    void UltraCanvasContainer::CalculateContentArea() {
        Rect2Di bounds = GetBounds();

        contentArea.x = bounds.x + style.paddingLeft + style.borderWidth;
        contentArea.y = bounds.y + style.paddingTop + style.borderWidth;
        contentArea.width = bounds.width - style.paddingLeft - style.paddingRight -
                            (2 * style.borderWidth);
        contentArea.height = bounds.height - style.paddingTop - style.paddingBottom -
                             (2 * style.borderWidth);

        // Adjust for scrollbars
        if (scrollState.showVerticalScrollbar) {
            contentArea.width -= style.scrollbarWidth;
        }

        if (scrollState.showHorizontalScrollbar) {
            contentArea.height -= style.scrollbarWidth;
        }

        // Ensure minimum size
        contentArea.width = std::max(0, contentArea.width);
        contentArea.height = std::max(0, contentArea.height);
    }

    void UltraCanvasContainer::UpdateScrollbarPositions() {
        Rect2Di bounds = GetBounds();

        // Vertical scrollbar
        if (scrollState.showVerticalScrollbar) {
            verticalScrollbarRect.x = bounds.x + bounds.width - style.scrollbarWidth;
            verticalScrollbarRect.y = bounds.y;
            verticalScrollbarRect.width = style.scrollbarWidth;
            verticalScrollbarRect.height = bounds.height;

            if (scrollState.showHorizontalScrollbar) {
                verticalScrollbarRect.height -= style.scrollbarWidth;
            }

            // Calculate thumb rectangle
            int thumbHeight = CalculateScrollbarThumbSize(true);
            int thumbPosition = CalculateScrollbarThumbPosition(true);

            verticalThumbRect.x = verticalScrollbarRect.x;
            verticalThumbRect.y = verticalScrollbarRect.y + thumbPosition;
            verticalThumbRect.width = style.scrollbarWidth;
            verticalThumbRect.height = thumbHeight;
        }

        // Horizontal scrollbar
        if (scrollState.showHorizontalScrollbar) {
            horizontalScrollbarRect.x = bounds.x;
            horizontalScrollbarRect.y = bounds.y + bounds.height - style.scrollbarWidth;
            horizontalScrollbarRect.width = bounds.width;
            horizontalScrollbarRect.height = style.scrollbarWidth;

            if (scrollState.showVerticalScrollbar) {
                horizontalScrollbarRect.width -= style.scrollbarWidth;
            }

            // Calculate thumb rectangle
            int thumbWidth = CalculateScrollbarThumbSize(false);
            int thumbPosition = CalculateScrollbarThumbPosition(false);

            horizontalThumbRect.x = horizontalScrollbarRect.x + thumbPosition;
            horizontalThumbRect.y = horizontalScrollbarRect.y;
            horizontalThumbRect.width = thumbWidth;
            horizontalThumbRect.height = style.scrollbarWidth;
        }
    }

    void UltraCanvasContainer::UpdateScrollbarAppearance() {
        // This method can be extended for custom scrollbar styling
        // Currently uses the style settings directly
    }

    void UltraCanvasContainer::UpdateScrollAnimation() {
        if (!scrollState.animatingScroll) return;

        bool verticalComplete = true;
        bool horizontalComplete = true;

        // Animate vertical scrolling
        if (std::abs(scrollState.targetVerticalPosition - scrollState.verticalPosition) > 0.5f) {
            int delta = scrollState.targetVerticalPosition - scrollState.verticalPosition;
            scrollState.verticalPosition += delta * (scrollState.scrollAnimationSpeed * 0.016f); // Assume 60 FPS
            verticalComplete = false;
        } else {
            scrollState.verticalPosition = scrollState.targetVerticalPosition;
        }

        // Animate horizontal scrolling
        if (std::abs(scrollState.targetHorizontalPosition - scrollState.horizontalPosition) > 0.5f) {
            int delta = scrollState.targetHorizontalPosition - scrollState.horizontalPosition;
            scrollState.horizontalPosition += delta * (scrollState.scrollAnimationSpeed * 0.016f);
            horizontalComplete = false;
        } else {
            scrollState.horizontalPosition = scrollState.targetHorizontalPosition;
        }

        // Check if animation is complete
        if (verticalComplete && horizontalComplete) {
            scrollState.animatingScroll = false;
        }

        OnScrollChanged();
    }

    bool UltraCanvasContainer::HandleScrollWheel(const UCEvent& event) {
        if (event.type != UCEventType::MouseWheel) return false;
        if (!contentArea.Contains(Point2Di(event.x, event.y))) return false;

        int scrollAmount = event.wheelDelta * style.scrollSpeed;

        // Scroll vertically by default, horizontally with Shift
        if (event.shift && style.enableHorizontalScrolling) {
            return ScrollHorizontal(-scrollAmount);
        } else if (style.enableVerticalScrolling) {
            return ScrollVertical(-scrollAmount); // Invert for natural scrolling
        }
        return false;
    }

    bool UltraCanvasContainer::HandleScrollbarEvents(const UCEvent& event) {
        Point2Di mousePos(event.x, event.y);

        // Handle mouse events on scrollbars
        if (event.type == UCEventType::MouseDown) {
            // Check vertical scrollbar
            std::cout << " Vsb contains=" << verticalScrollbarRect.Contains(mousePos)
                << "rect (" << verticalScrollbarRect.x << ", " << verticalScrollbarRect.y << " " << verticalScrollbarRect.width << "x" << verticalScrollbarRect.height << ")"
                << " ev x=" << event.x << " y=" << event.y << std::endl;
            if (scrollState.showVerticalScrollbar && verticalScrollbarRect.Contains(mousePos)) {
                if (verticalThumbRect.Contains(mousePos)) {
                    UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                    scrollState.draggingVertical = true;
                    scrollState.dragStartPosition = event.y;
                    scrollState.dragStartScroll = scrollState.verticalPosition;
                    scrollState.dragStartMouse = mousePos;
                    return true;
                } else {
                    // Clicked on track - page scroll
                    int clickPosition = (mousePos.y - verticalScrollbarRect.y) / verticalScrollbarRect.height;
                    int targetPosition = clickPosition * scrollState.maxVerticalScroll;
                    SetVerticalScrollPosition(targetPosition);
                    return true;
                }
            }

            // Check horizontal scrollbar
            if (scrollState.showHorizontalScrollbar && horizontalScrollbarRect.Contains(mousePos)) {
                if (horizontalThumbRect.Contains(mousePos)) {
                    UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                    scrollState.draggingHorizontal = true;
                    scrollState.dragStartPosition = event.x;
                    scrollState.dragStartScroll = scrollState.horizontalPosition;
                    scrollState.dragStartMouse = mousePos;
                    return true;
                } else {
                    // Clicked on track - page scroll
                    int clickPosition = (mousePos.x - horizontalScrollbarRect.x) / horizontalScrollbarRect.width;
                    int targetPosition = clickPosition * scrollState.maxHorizontalScroll;
                    SetHorizontalScrollPosition(targetPosition);
                    return true;
                }
            }
        }

        if (event.type == UCEventType::MouseUp) {
            scrollState.draggingVertical = false;
            scrollState.draggingHorizontal = false;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
            return false;
        }

        if (event.type == UCEventType::MouseMove) {
            // Handle scrollbar dragging
            if (scrollState.draggingVertical) {
                int deltaY = event.y - scrollState.dragStartMouse.y;
                //int scrollDelta = deltaY * scrollState.maxVerticalScroll / verticalScrollbarRect.height;
                int scrollDelta = deltaY;
                SetVerticalScrollPosition(scrollState.dragStartScroll + scrollDelta);
                return true;
            }

            if (scrollState.draggingHorizontal) {
                int deltaX = event.x - scrollState.dragStartMouse.x;
                //int scrollDelta = (deltaX / horizontalScrollbarRect.width) * scrollState.maxHorizontalScroll;
                int scrollDelta = deltaX;
                SetHorizontalScrollPosition(scrollState.dragStartScroll + scrollDelta);
                return true;
            }

            // Update hover states
            UpdateScrollbarHoverStates(mousePos);
        }

        return false;
    }

    void UltraCanvasContainer::OnScrollChanged() {
        UpdateScrollbarPositions();
        RequestRedraw();

        if (onScrollChanged) {
            onScrollChanged(scrollState.horizontalPosition, scrollState.verticalPosition);
        }
    }

    int UltraCanvasContainer::CalculateScrollbarThumbSize(bool vertical) const {
        if (vertical) {
            if (scrollState.maxVerticalScroll <= 0) return verticalScrollbarRect.height;
            float ratio = (float)contentArea.height / (float)scrollState.contentHeight;
            return std::max(20, (int)((float)verticalScrollbarRect.height * ratio));
        } else {
            if (scrollState.maxHorizontalScroll <= 0) return horizontalScrollbarRect.width;
            float ratio = (float)contentArea.width / (float)scrollState.contentWidth;
            return std::max(20, (int)((float)horizontalScrollbarRect.width * ratio));
        }
    }

    int UltraCanvasContainer::CalculateScrollbarThumbPosition(bool vertical) const {
        if (vertical) {
            if (scrollState.maxVerticalScroll <= 0) return 0;
            float ratio = (float)scrollState.verticalPosition / (float)scrollState.maxVerticalScroll;
            int availableSpace = verticalScrollbarRect.height - CalculateScrollbarThumbSize(true);
            return (int)(ratio * (float)availableSpace);
        } else {
            if (scrollState.maxHorizontalScroll <= 0) return 0;
            float ratio = (float)scrollState.horizontalPosition / (float)scrollState.maxHorizontalScroll;
            int availableSpace = horizontalScrollbarRect.width - CalculateScrollbarThumbSize(false);
            return (int)(ratio * (float)availableSpace);
        }
    }

    void UltraCanvasContainer::RenderVerticalScrollbar(IRenderContext *ctx) {
        // Render scrollbar track
        ctx->SetFillColor(style.scrollbarTrackColor);
        ctx->FillRectangle(verticalScrollbarRect);

        // Render scrollbar thumb
        Color thumbColor = style.scrollbarThumbColor;
        if (scrollState.hoveringVerticalThumb) {
            thumbColor = style.scrollbarThumbHoverColor;
        }
        if (scrollState.draggingVertical) {
            thumbColor = style.scrollbarThumbPressedColor;
        }

        ctx->SetFillColor(thumbColor);
        ctx->FillRectangle(verticalThumbRect);
    }

    void UltraCanvasContainer::RenderHorizontalScrollbar(IRenderContext *ctx) {
        // Render scrollbar track
        ctx->SetFillColor(style.scrollbarTrackColor);
        ctx->FillRectangle(horizontalScrollbarRect);

        // Render scrollbar thumb
        Color thumbColor = style.scrollbarThumbColor;
        if (scrollState.hoveringHorizontalThumb) {
            thumbColor = style.scrollbarThumbHoverColor;
        }
        if (scrollState.draggingHorizontal) {
            thumbColor = style.scrollbarThumbPressedColor;
        }

        ctx->SetFillColor(thumbColor);
        ctx->FillRectangle(horizontalThumbRect);
    }

    void UltraCanvasContainer::UpdateScrollbarHoverStates(const Point2Di& mousePos) {
        // Update vertical scrollbar hover states
        scrollState.hoveringVerticalScrollbar = scrollState.showVerticalScrollbar &&
                                                verticalScrollbarRect.Contains(mousePos);
        scrollState.hoveringVerticalThumb = scrollState.hoveringVerticalScrollbar &&
                                            verticalThumbRect.Contains(mousePos);

        // Update horizontal scrollbar hover states
        scrollState.hoveringHorizontalScrollbar = scrollState.showHorizontalScrollbar &&
                                                  horizontalScrollbarRect.Contains(mousePos);
        scrollState.hoveringHorizontalThumb = scrollState.hoveringHorizontalScrollbar &&
                                              horizontalThumbRect.Contains(mousePos);
    }

    void UltraCanvasContainer::AddOrMoveChild(std::shared_ptr<UltraCanvasUIElement> child) {
        if (!child || child->GetParentContainer() == this) return;

        // Remove from previous parent if any
        if (auto* element = child.get()) {
            if (auto* prevContainer = element->GetParentContainer()) {
                prevContainer->RemoveChild(child);
            }
        }

        // Check if child implements popup interface
//        if (auto* popup = dynamic_cast<IPopupElement*>(child.get())) {
//            UltraCanvasPopupRegistry::RegisterPopupElement(child.get());
//        }

        // Auto-assign z-index if needed
//        if (child && child->GetZIndex() == 0) {
//            AutoAssignZIndex(child.get());
//        }
        child->SetParentContainer(this);
        child->SetWindow(GetWindow());

        // Add to this container
        children.push_back(child);

        // Update layout and scrolling
        UpdateScrollability();
        layoutDirty = true;

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
            children.erase(it);

            UpdateScrollability();
            layoutDirty = true;

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
        }
        children.clear();

        scrollState.verticalPosition = 0;
        scrollState.horizontalPosition = 0;
        scrollState.targetVerticalPosition = 0;
        scrollState.targetHorizontalPosition = 0;

        UpdateScrollability();
        layoutDirty = true;
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

//    UltraCanvasUIElement* UltraCanvasContainer::FindElementAtPoint(int x, int y) {
//        // First check if point is within our bounds
//        if (!Contains(x, y)) {
//            return nullptr;
//        }
//
//        // Check scrollbar areas first - these have priority over content
//        if (scrollState.showVerticalScrollbar &&
//            verticalScrollbarRect.Contains(x, y)) {
//            return this; // Container handles scrollbar interactions
//        }
//
//        if (scrollState.showHorizontalScrollbar &&
//            horizontalScrollbarRect.Contains(x, y)) {
//            return this; // Container handles scrollbar interactions
//        }
//
//        // Check if point is within content area (not scrollbar area)
//        if (!contentArea.Contains(x, y)) {
//            return this; // Hit container but outside content area
//        }
//
//        // Calculate content-relative coordinates accounting for scroll offset
//        Point2Di mousePos(x, y);
//        int contentX = x - contentArea.x + scrollState.horizontalPosition;
//        int contentY = y - contentArea.y + scrollState.verticalPosition;
//
//        // Check children in reverse order (topmost first) with clipping awareness
//        for (auto it = children.rbegin(); it != children.rend(); ++it) {
//            if (!(*it) || !(*it)->IsVisible()) {
//                continue;
//            }
//
//            UltraCanvasUIElement* child = it->get();
//
//            // CRITICAL: Check if the child element intersects with visible content area
//            Rect2Di childBounds = child->GetBounds();
//            Rect2Di visibleChildBounds = GetVisibleChildBounds(childBounds);
//
//            // Skip child if it's completely clipped (not visible)
//            if (visibleChildBounds.width <= 0 || visibleChildBounds.height <= 0) {
//                continue;
//            }
//
//            // Check if mouse is within the visible portion of the child
//            if (visibleChildBounds.Contains(mousePos)) {
//                // Recursively check child elements
//                auto childContainer = dynamic_cast<UltraCanvasContainer*>(child);
//                if (childContainer) {
//                    UltraCanvasUIElement* hitElement = childContainer->FindElementAtPoint(contentX, contentY);
//                    if (hitElement) {
//                        return hitElement;
//                    }
//                }
//                return child;
//            }
//        }
//
//        return this; // Hit container but no children
//    }

    UltraCanvasUIElement* UltraCanvasContainer::FindElementAtPoint(int x, int y) {
        // First check if point is within our bounds
        if (!Contains(x, y)) {
            return nullptr;
        }

        // Check scrollbar areas first - these have priority over content
        if (scrollState.showVerticalScrollbar &&
            verticalScrollbarRect.Contains(x, y)) {
            return this; // Container handles scrollbar interactions
        }

        if (scrollState.showHorizontalScrollbar &&
            horizontalScrollbarRect.Contains(x, y)) {
            return this; // Container handles scrollbar interactions
        }

        // Check if point is within content area (not scrollbar area)
        if (!contentArea.Contains(x, y)) {
            return this; // Hit container but outside content area
        }

        // CRITICAL FIX: Convert mouse coordinates to content coordinates
        // accounting for scroll offset AND content area position
        int contentX = (x - contentArea.x) + scrollState.horizontalPosition;
        int contentY = (y - contentArea.y) + scrollState.verticalPosition;

        // Check children in reverse order (topmost first) with proper clipping
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (!(*it) || !(*it)->IsVisible() || !(*it)->IsEnabled()) {
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

// ===== CRITICAL FIX: Enhanced coordinate conversion for scrollable containers =====
    void UltraCanvasContainer::ConvertWindowToContainerCoordinates(int &x, int &y) {
        // Get our position in window coordinates
        Point2Di elementPos = GetPositionInWindow();

        // Convert from window coordinates to container coordinates
        x -= elementPos.x;
        y -= elementPos.y;

        // CRITICAL FIX: If coordinates are within content area, adjust for scrolling
        if (contentArea.Contains(x, y)) {
            // Convert to content-relative coordinates accounting for scroll
            x = (x - contentArea.x) + scrollState.horizontalPosition;
            y = (y - contentArea.y) + scrollState.verticalPosition;
        }
    }

    Rect2Di UltraCanvasContainer::GetVisibleChildBounds(const Rect2Di& childBounds) const {
        // Calculate child bounds in container coordinates (accounting for scroll)
        Rect2Di adjustedChildBounds(
                childBounds.x - scrollState.horizontalPosition + contentArea.x,
                childBounds.y - scrollState.verticalPosition + contentArea.y,
                childBounds.width,
                childBounds.height
        );

        // CRITICAL FIX: Intersect with content area to get visible portion
        Rect2Di intersection;
        if (adjustedChildBounds.Intersects(contentArea, intersection)) {
            return intersection;
        }

        // Return empty rect if no intersection (completely clipped)
        return Rect2Di(0, 0, 0, 0);
    }

    /**
    * Check if a child element is visible (not completely clipped)
    */
    bool UltraCanvasContainer::IsChildVisible(UltraCanvasUIElement* child) const {
        if (!child || !child->IsVisible()) {
            return false;
        }

        Rect2Di childBounds = child->GetBounds();
        Rect2Di visibleBounds = GetVisibleChildBounds(childBounds);

        return (visibleBounds.width > 0 && visibleBounds.height > 0);
    }

//    int UltraCanvasContainer::GetXInWindow() {
//        if (layoutDirty) {
//            CalculateContentArea();
//        }
//
//        if (parentContainer) {
//            // Get parent's window position
//            int parentWindowX = parentContainer->GetXInWindow();
//
//            // Add parent's content area offset and our position
//            Rect2Di parentContentArea = parentContainer->GetContentArea();
//            return parentWindowX + parentContentArea.x + properties.x_pos;
//        }
//
//        // If this is the root container (window), return our position
//        return properties.x_pos;
//    }
//
//    int UltraCanvasContainer::GetYInWindow() {
//        if (layoutDirty) {
//            CalculateContentArea();
//        }
//
//        if (parentContainer) {
//            // Get parent's window position
//            int parentWindowY = parentContainer->GetYInWindow();
//
//            // Add parent's content area offset and our position
//            Rect2Di parentContentArea = parentContainer->GetContentArea();
//            return parentWindowY + parentContentArea.y + properties.y_pos;
//        }
//
//        return properties.y_pos;
//    }

//    int UltraCanvasContainer::GetXInWindow() {
//        if (layoutDirty) {
//            CalculateContentArea();
//        }
//        if (parentContainer) {
//            return parentContainer->GetXInWindow() + parentContainer->GetContentArea().x + properties.x_pos;
//        }
//        return properties.x_pos;
//    }
//
//    int UltraCanvasContainer::GetYInWindow() {
//        if (layoutDirty) {
//            CalculateContentArea();
//        }
//        if (parentContainer) {
//            return parentContainer->GetYInWindow() + parentContainer->GetContentArea().y + properties.y_pos;
//        }
//        return properties.y_pos;
//    }

    void UltraCanvasContainer::UpdateLayout() {
        CalculateContentArea();
        UpdateScrollbarPositions();
        layoutDirty = false;
    }

    void UltraCanvasContainer::SetContainerStyle(const ContainerStyle &newStyle) {
        style = newStyle;
        UpdateScrollbarAppearance();
        UpdateLayout();
    }

    void UltraCanvasContainer::SetShowHorizontalScrollbar(bool show) {
        style.autoHideScrollbars = false;
        scrollState.showHorizontalScrollbar = show;
        UpdateLayout();
    }

    void UltraCanvasContainer::SetShowVerticalScrollbar(bool show) {
        style.autoHideScrollbars = false;
        scrollState.showVerticalScrollbar = show;
        UpdateLayout();
    }

    bool UltraCanvasContainer::SetHorizontalScrollPosition(int position) {
        int oldPosition = scrollState.horizontalPosition;
        scrollState.horizontalPosition = std::clamp(position, 0, scrollState.maxHorizontalScroll);
        scrollState.targetHorizontalPosition = scrollState.horizontalPosition;

        if (oldPosition != scrollState.horizontalPosition) {
            OnScrollChanged();
            return true;
        }
        return false;
    }

    bool UltraCanvasContainer::SetVerticalScrollPosition(int position) {
        int oldPosition = scrollState.verticalPosition;
        scrollState.verticalPosition = std::clamp(position, 0, scrollState.maxVerticalScroll);
        scrollState.targetVerticalPosition = scrollState.verticalPosition;

        if (oldPosition != scrollState.verticalPosition) {
            OnScrollChanged();
            return true;
        }
        return false;
    }

    bool UltraCanvasContainer::ScrollHorizontal(int delta) {
        if (!style.enableHorizontalScrolling) return false;

        if (style.smoothScrolling) {
            scrollState.targetHorizontalPosition += delta;
            scrollState.targetHorizontalPosition = std::clamp(
                    scrollState.targetHorizontalPosition,
                    0,
                    scrollState.maxHorizontalScroll
            );
            scrollState.animatingScroll = true;
            return true; // fix me! need to check is it will really scroll
        } else {
            int newPosition = scrollState.horizontalPosition + delta;
            return SetHorizontalScrollPosition(newPosition);
        }
    }

    bool UltraCanvasContainer::ScrollVertical(int delta) {
        if (!style.enableVerticalScrolling) return false;

        if (style.smoothScrolling) {
            scrollState.targetVerticalPosition += delta;
            scrollState.targetVerticalPosition = std::clamp(
                    scrollState.targetVerticalPosition,
                    0,
                    scrollState.maxVerticalScroll
            );
            scrollState.animatingScroll = true;
            return true; // fix me! need to check is it will really scroll
        } else {
            int newPosition = scrollState.verticalPosition + delta;
            return SetVerticalScrollPosition(newPosition);
        }
        return false;
    }

    Rect2Di UltraCanvasContainer::GetContentArea() {
        if (layoutDirty) {
            CalculateContentArea();
        }
        return contentArea;
    }

//    void UltraCanvasContainer::UpdateHoverStates(const UCEvent& event) {
//        Point2Di mousePos(event.x, event.y);
//
//        // Update scrollbar hover states if scrollbars are visible
//        if (scrollState.showVerticalScrollbar || scrollState.showHorizontalScrollbar) {
//            UpdateScrollbarHoverStates(mousePos);
//        }
//
//        // Update container hover state
//        bool wasHovered = isHovered;
//        isHovered = Contains(mousePos.x, mousePos.y);
//
//        if (isHovered != wasHovered) {
//            if (onHoverStateChanged) {
//                onHoverStateChanged(isHovered);
//            }
//        }
//    }

} // namespace UltraCanvas