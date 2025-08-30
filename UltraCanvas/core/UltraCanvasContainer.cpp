// UltraCanvasContainer.cpp
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasBaseWindow.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasApplication.h"
//#include "UltraCanvasZOrderManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== RENDERING IMPLEMENTATION =====
    void UltraCanvasContainer::SetWindow(UltraCanvasWindow *win) {
        UltraCanvasElement::SetWindow(win);
        // Propagate to children
        for (auto& child : children) {
            child->SetWindow(win);
        }
    }

    void UltraCanvasContainer::Render() {
        if (!IsVisible()) return;

        // Set up rendering scope for this container
        ULTRACANVAS_RENDER_SCOPE();

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

        // Render container background
        if (style.backgroundColor.a > 0) {
            SetFillColor(style.backgroundColor);
            FillRectangle(bounds);
        }

        // Render border
        if (style.borderWidth > 0) {
            SetStrokeColor(style.borderColor);
            SetStrokeWidth(style.borderWidth);
            DrawRectangle(bounds);
        }

        PushRenderState();
        Translate(contentArea.x - scrollState.horizontalPosition,
                  contentArea.y - scrollState.verticalPosition);
        // Set up clipping for content area
//        Rect2Di clipRect = contentArea;
        SetClipRect(Rect2Di(0, 0, contentArea.width, contentArea.height));

        // Render children with scroll offset
        for (const auto& child : children) {
            if (!child || !child->IsVisible()) continue;

            // Apply scroll offset to child rendering
            child->Render();
        }

        // Remove content clipping
        PopRenderState();

        // Render scrollbars
        if (scrollState.showVerticalScrollbar) {
            RenderVerticalScrollbar();
        }

        if (scrollState.showHorizontalScrollbar) {
            RenderHorizontalScrollbar();
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
        // Forward events to children (in reverse order for proper hit testing)
        // Check if event is in content area
//        if (contentArea.Contains(event.x, event.y)) {
//            // Adjust event coordinates for scroll offset
//            UCEvent childEvent = event;
//            childEvent.x = event.x - contentArea.x + scrollState.horizontalPosition;
//            childEvent.y = event.y - contentArea.y + scrollState.verticalPosition;
//
//            // Forward to children in reverse order (topmost first)
//            for (auto it = children.rbegin(); it != children.rend(); ++it) {
//                if ((*it) && (*it)->IsVisible() && (*it)->OnEvent(childEvent)) {
//                    return true;
//                }
//            }
//        }

        // Handle container-specific events first
        if (HandleScrollWheel(event)) {
            return true;
        }

        // Handle base element events
        return UltraCanvasElement::OnEvent(event);
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
            ScrollHorizontal(scrollAmount);
        } else if (style.enableVerticalScrolling) {
            ScrollVertical(-scrollAmount); // Invert for natural scrolling
        }

        return true;
    }

    bool UltraCanvasContainer::HandleScrollbarEvents(const UCEvent& event) {
        Point2Di mousePos(event.x, event.y);

        // Handle mouse events on scrollbars
        if (event.type == UCEventType::MouseDown) {
            // Check vertical scrollbar
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

    void UltraCanvasContainer::RenderVerticalScrollbar() {
        // Render scrollbar track
        SetFillColor(style.scrollbarTrackColor);
        FillRectangle(verticalScrollbarRect);

        // Render scrollbar thumb
        Color thumbColor = style.scrollbarThumbColor;
        if (scrollState.hoveringVerticalThumb) {
            thumbColor = style.scrollbarThumbHoverColor;
        }
        if (scrollState.draggingVertical) {
            thumbColor = style.scrollbarThumbPressedColor;
        }

        SetFillColor(thumbColor);
        FillRectangle(verticalThumbRect);
    }

    void UltraCanvasContainer::RenderHorizontalScrollbar() {
        // Render scrollbar track
        SetFillColor(style.scrollbarTrackColor);
        FillRectangle(horizontalScrollbarRect);

        // Render scrollbar thumb
        Color thumbColor = style.scrollbarThumbColor;
        if (scrollState.hoveringHorizontalThumb) {
            thumbColor = style.scrollbarThumbHoverColor;
        }
        if (scrollState.draggingHorizontal) {
            thumbColor = style.scrollbarThumbPressedColor;
        }

        SetFillColor(thumbColor);
        FillRectangle(horizontalThumbRect);
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

    void UltraCanvasContainer::AddChild(std::shared_ptr<UltraCanvasElement> child) {
        if (!child) return;

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
        UpdateContentSize();
        UpdateScrollability();
        layoutDirty = true;

        // Notify callbacks
        if (onChildAdded) {
            onChildAdded(child.get());
        }
    }

    void UltraCanvasContainer::RemoveChild(std::shared_ptr<UltraCanvasElement> child) {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {
            // Unregister from popup system
//            UltraCanvasPopupRegistry::UnregisterPopupElement(child.get());

            (*it)->SetParentContainer(nullptr);
            (*it)->SetWindow(nullptr);
            children.erase(it);

            UpdateContentSize();
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

    UltraCanvasElement *UltraCanvasContainer::FindChildById(const std::string &id) {
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

    UltraCanvasElement* UltraCanvasContainer::FindElementAtPoint(int x, int y) {
        // Search in reverse order to get topmost element first (higher z-index)
        int localX = x - contentArea.x, localY = y - contentArea.y;
        auto childs = GetChildren();
        for (auto it = childs.rbegin(); it != childs.rend(); ++it) {
            auto element = *it;
            if (element && element->IsVisible() && element->IsEnabled() && element->Contains(localX, localY)) {
                auto elementAsChildContainer = dynamic_cast<UltraCanvasContainer*>(element.get());
                // Check children first (recursive hit testing)
                if (elementAsChildContainer) {
                    auto elem = elementAsChildContainer->FindElementAtPoint(localX - elementAsChildContainer->GetX(),
                                                                            localY + elementAsChildContainer->GetY());
                    if (elem) {
                        return elem;
                    } else if (parentContainer != nullptr) {
                        return element.get();
                    }
                } else {
                    return element.get();
                }
            }
        }
        return nullptr;
    }

    int UltraCanvasContainer::GetXInWindow()  {
        if (layoutDirty) {
            CalculateContentArea();
        }
        if (parentContainer) {
            return parentContainer->GetXInWindow() + parentContainer->GetContentArea().x + properties.x_pos;
        }
        return properties.x_pos;
    }

    int UltraCanvasContainer::GetYInWindow() {
        if (layoutDirty) {
            CalculateContentArea();
        }
        if (parentContainer) {
            return parentContainer->GetYInWindow() + parentContainer->GetContentArea().y + properties.y_pos;
        }
        return properties.x_pos;
    }

} // namespace UltraCanvas