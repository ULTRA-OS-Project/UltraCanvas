// UltraCanvasContainer.h
// Container component with scrollbars and child element management
// Version: 1.2.7
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasCairoDebugExtension.h"
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== CONTAINER STYLES =====
    struct ContainerStyle {
        // Scrollbar settings
        float scrollbarWidth = 16.0f;
        Color scrollbarTrackColor = Color(240, 240, 240, 255);
        Color scrollbarThumbColor = Color(192, 192, 192, 255);
        Color scrollbarThumbHoverColor = Color(160, 160, 160, 255);
        Color scrollbarThumbPressedColor = Color(128, 128, 128, 255);

        // Border and background
        Color borderColor = Color(200, 200, 200, 255);
        Color backgroundColor = Colors::White;
        float borderWidth = 3.0f;

        // Padding for content area
        float paddingLeft = 5.0f;
        float paddingTop = 5.0f;
        float paddingRight = 5.0f;
        float paddingBottom = 5.0f;

        // Scrolling behavior
        bool autoHideScrollbars = false;
        bool smoothScrolling = true;
        float scrollSpeed = 20.0f;
    };

// ===== SCROLL STATE =====
    struct ScrollState {
        float verticalPosition = 0.0f;      // Current vertical scroll position
        float horizontalPosition = 0.0f;    // Current horizontal scroll position

        float maxVerticalScroll = 0.0f;     // Maximum vertical scroll
        float maxHorizontalScroll = 0.0f;   // Maximum horizontal scroll

        bool showVerticalScrollbar = false;
        bool showHorizontalScrollbar = false;

        // Drag state for scrollbar interaction
        bool draggingVertical = false;
        bool draggingHorizontal = false;
        float dragStartPosition = 0.0f;
        float dragStartScroll = 0.0f;

        // Hover state for visual feedback
        bool hoveringVerticalScrollbar = false;
        bool hoveringHorizontalScrollbar = false;
        bool hoveringVerticalThumb = false;
        bool hoveringHorizontalThumb = false;
    };

// ===== CONTAINER CLASS =====
    class UltraCanvasContainer : public UltraCanvasElement {
    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasElement(id, uid, x, y, w, h) {
            UpdateScrollability();
        }

        virtual ~UltraCanvasContainer() = default;

        // ===== CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasElement> child) {
            if (child) {
                children.push_back(child);
                child->SetParent(this);
                UpdateScrollability();
            }
        }

        void RemoveChild(std::shared_ptr<UltraCanvasElement> child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) {
                (*it)->SetParent(nullptr);
                children.erase(it);
                UpdateScrollability();
            }
        }

        void ClearChildren() {
            for (auto& child : children) {
                if (child) {
                    child->SetParent(nullptr);
                }
            }
            children.clear();
            scrollState.verticalPosition = 0.0f;
            scrollState.horizontalPosition = 0.0f;
            UpdateScrollability();
        }

        const std::vector<std::shared_ptr<UltraCanvasElement>>& GetChildren() const {
            return children;
        }

        // ===== NEW: RELATIVE COORDINATE CHILD MANAGEMENT =====
//        void AddChildAtRelativePosition(std::shared_ptr<UltraCanvasElement> child) {
//            if (child) {
//                child->SetRelativePosition(child->GetRelativeX(), child->GetRelativeY());
//                AddChild(child);
//            }
//        }

        void AddChildAtRelativePosition(std::shared_ptr<UltraCanvasElement> child, long relativeX, long relativeY) {
            if (child) {
                child->SetPosition(relativeX, relativeY);
                AddChild(child);
            }
        }

        void AddChildAtRelativePosition(std::shared_ptr<UltraCanvasElement> child, const Point2D& relativePos) {
            AddChildAtRelativePosition(child, static_cast<long>(relativePos.x), static_cast<long>(relativePos.y));
        }

        std::shared_ptr<UltraCanvasElement> GetChildAtRelativePosition(const Point2D& containerRelativePoint) const {
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if ((*it) && (*it)->IsVisible() && (*it)->Contains(containerRelativePoint)) {
                    return *it;
                }
            }
            return nullptr;
        }

        // ===== NEW: COORDINATE TRANSFORMATION METHODS =====
        Point2D ChildToContainer(const Point2D& childPoint) const {
            return Point2D(childPoint.x, childPoint.y);
        }

        Point2D ContainerToChild(const Point2D& containerPoint) const {
            return Point2D(containerPoint.x, containerPoint.y);
        }

        Point2D ChildToWindow(const Point2D& childPoint) const {
            Rect2D contentArea = GetContentArea();
            return Point2D(
                    contentArea.x + childPoint.x - scrollState.horizontalPosition,
                    contentArea.y + childPoint.y - scrollState.verticalPosition
            );
        }

        Point2D WindowToChild(const Point2D& windowPoint) const {
            Rect2D contentArea = GetContentArea();
            return Point2D(
                    windowPoint.x - contentArea.x + scrollState.horizontalPosition,
                    windowPoint.y - contentArea.y + scrollState.verticalPosition
            );
        }

        // ===== SCROLLING FUNCTIONS =====
        void ScrollVertical(float delta) {
            float newPosition = scrollState.verticalPosition + delta;
            SetVerticalScrollPosition(newPosition);
        }

        void ScrollHorizontal(float delta) {
            float newPosition = scrollState.horizontalPosition + delta;
            SetHorizontalScrollPosition(newPosition);
        }

        void SetVerticalScrollPosition(float position) {
            scrollState.verticalPosition = std::max(0.0f, std::min(position, scrollState.maxVerticalScroll));
            OnScrollChanged();
        }

        void SetHorizontalScrollPosition(float position) {
            scrollState.horizontalPosition = std::max(0.0f, std::min(position, scrollState.maxHorizontalScroll));
            OnScrollChanged();
        }

        float GetVerticalScrollPosition() const { return scrollState.verticalPosition; }
        float GetHorizontalScrollPosition() const { return scrollState.horizontalPosition; }

        // ===== NEW: SCROLL TO CHILD SUPPORT =====
        void ScrollToChild(std::shared_ptr<UltraCanvasElement> child) {
            if (!child) return;

            Rect2D childBounds = child->GetBounds();
            Rect2D visibleArea = GetVisibleContentArea();

            float newHorizontalScroll = scrollState.horizontalPosition;
            float newVerticalScroll = scrollState.verticalPosition;

            if (childBounds.x < visibleArea.x) {
                newHorizontalScroll = childBounds.x;
            } else if (childBounds.x + childBounds.width > visibleArea.x + visibleArea.width) {
                newHorizontalScroll = childBounds.x + childBounds.width - visibleArea.width;
            }

            if (childBounds.y < visibleArea.y) {
                newVerticalScroll = childBounds.y;
            } else if (childBounds.y + childBounds.height > visibleArea.y + visibleArea.height) {
                newVerticalScroll = childBounds.y + childBounds.height - visibleArea.height;
            }

            SetHorizontalScrollPosition(newHorizontalScroll);
            SetVerticalScrollPosition(newVerticalScroll);
        }

        void EnsureChildVisible(std::shared_ptr<UltraCanvasElement> child) {
            if (!IsChildVisible(child)) {
                ScrollToChild(child);
            }
        }

        bool IsChildVisible(std::shared_ptr<UltraCanvasElement> child) const {
            if (!child || !child->IsVisible()) return false;

            Rect2D childBounds = child->GetBounds();
            Rect2D visibleArea = GetVisibleContentArea();

            return childBounds.Intersects(visibleArea);
        }

        Rect2D GetVisibleContentArea() const {
            Rect2D contentArea = GetContentArea();
            return Rect2D(
                    scrollState.horizontalPosition,
                    scrollState.verticalPosition,
                    contentArea.width,
                    contentArea.height
            );
        }

        // ===== SCROLLBAR VISIBILITY =====
        void SetShowVerticalScrollbar(bool show) {
            style.autoHideScrollbars = false;
            scrollState.showVerticalScrollbar = show;
        }

        void SetShowHorizontalScrollbar(bool show) {
            style.autoHideScrollbars = false;
            scrollState.showHorizontalScrollbar = show;
        }

        bool HasVerticalScrollbar() const { return scrollState.showVerticalScrollbar; }
        bool HasHorizontalScrollbar() const { return scrollState.showHorizontalScrollbar; }

        // ===== STYLE CONFIGURATION =====
        void SetContainerStyle(const ContainerStyle& newStyle) {
            style = newStyle;
            UpdateScrollability();
        }

        const ContainerStyle& GetContainerStyle() const { return style; }

        void SetScrollbarWidth(float width) {
            style.scrollbarWidth = width;
            UpdateScrollability();
        }

        void SetScrollbarColors(const Color& track, const Color& thumb, const Color& thumbHover, const Color& thumbPressed) {
            style.scrollbarTrackColor = track;
            style.scrollbarThumbColor = thumb;
            style.scrollbarThumbHoverColor = thumbHover;
            style.scrollbarThumbPressedColor = thumbPressed;
        }

        // ===== CONTENT AREA =====
        Rect2D GetContentArea() const {
            float x = GetX() + style.paddingLeft;
            float y = GetY() + style.paddingTop;
            float w = GetWidth() - style.paddingLeft - style.paddingRight;
            float h = GetHeight() - style.paddingTop - style.paddingBottom;

            // Reduce size if scrollbars are visible
            if (scrollState.showVerticalScrollbar) {
                w -= style.scrollbarWidth;
            }
            if (scrollState.showHorizontalScrollbar) {
                h -= style.scrollbarWidth;
            }

            return Rect2D(x, y, std::max(0.0f, w), std::max(0.0f, h));
        }

        Point2D GetContentSize() const {
            float maxX = 0, maxY = 0;

            for (const auto& child : children) {
                if (child) {
                    // Use child's relative coordinates for content size calculation
                    float childRight = child->GetX() + child->GetWidth();
                    float childBottom = child->GetY() + child->GetHeight();
                    maxX = std::max(maxX, childRight);
                    maxY = std::max(maxY, childBottom);
                }
            }

            return Point2D(maxX, maxY);
        }

        // ===== NEW: OPTIMIZED RENDERING WITH RELATIVE COORDINATES =====
        std::vector<std::shared_ptr<UltraCanvasElement>> GetVisibleChildren() const {
            std::vector<std::shared_ptr<UltraCanvasElement>> visibleChildren;
            Rect2D visibleArea = GetVisibleContentArea();

            for (const auto& child : children) {
                if (child && child->IsVisible()) {
                    Rect2D childBounds = child->GetBounds();
                    if (childBounds.Intersects(visibleArea)) {
                        visibleChildren.push_back(child);
                    }
                }
            }

            return visibleChildren;
        }

        // ===== RENDERING =====
        void Render() override {
            ULTRACANVAS_RENDER_SCOPE();

            // Draw container background and border
            DrawContainerBackground();

            // Set up clipping for content area
            Rect2D contentArea = GetContentArea();
            SetClipRect(contentArea);

            // Render children with coordinate transformation
            RenderChildren();

            // Restore clipping
            ClearClipRect();

            // Draw scrollbars
            DrawScrollbars();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            // Handle scrollbar interactions first
            if (HandleScrollbarEvents(event)) {
                return true;
            }

            // Handle scroll wheel events
            if (event.type == UCEventType::MouseWheel) {
                if (HandleScrollWheel(event)) {
                    return true;
                }
            }

            // Handle mouse events for container interactions
            if (event.type == UCEventType::MouseDown ||
                event.type == UCEventType::MouseUp ||
                event.type == UCEventType::MouseMove) {
                if (HandleMouseClick(event)) {
                    return true;
                }
            }

            // Forward events to children within content area with coordinate transformation
            if (IsPointInContentArea(Point2D(event.x, event.y))) {
                ForwardEventToChildren(event);
            }
            return false;
        }

        // ===== EVENT CALLBACKS =====
        std::function<void(float, float)> onScrollChanged;
        std::function<void()> onContentChanged;

    private:
        // ===== MEMBER VARIABLES =====
        std::vector<std::shared_ptr<UltraCanvasElement>> children;
        ContainerStyle style;
        ScrollState scrollState;

        // ===== PRIVATE HELPER METHODS =====

        void UpdateScrollability() {
            Point2D contentSize = GetContentSize();
            Rect2D contentArea = GetContentArea();

            // Calculate maximum scroll distances
            scrollState.maxVerticalScroll = std::max(0.0f, contentSize.y - contentArea.height);
            scrollState.maxHorizontalScroll = std::max(0.0f, contentSize.x - contentArea.width);

            // Determine scrollbar visibility
            if (style.autoHideScrollbars) {
                scrollState.showVerticalScrollbar = (scrollState.maxVerticalScroll > 0);
                scrollState.showHorizontalScrollbar = (scrollState.maxHorizontalScroll > 0);
            }

            // Clamp current scroll positions
            scrollState.verticalPosition = std::min(scrollState.verticalPosition, scrollState.maxVerticalScroll);
            scrollState.horizontalPosition = std::min(scrollState.horizontalPosition, scrollState.maxHorizontalScroll);
        }

        bool HandleMouseClick(const UCEvent& event) {
            return true;
        }

        bool HandleScrollbarEvents(const UCEvent& event) {
            // Handle scrollbar drag operations and hover states
            // Implementation details...
            return false;
        }

        bool HandleScrollWheel(const UCEvent& event) {
            if (event.wheelDelta != 0) {
                float scrollAmount = event.wheelDelta * style.scrollSpeed;

                // Scroll vertically by default, horizontally with Shift
                if (event.shift) {
                    ScrollHorizontal(scrollAmount);
                } else {
                    ScrollVertical(scrollAmount);
                }

                return true;
            }
            return false;
        }

        bool IsPointInContentArea(const Point2D& point) const {
            Rect2D contentArea = GetContentArea();
            return contentArea.Contains(point);
        }

        void ForwardEventToChildren(const UCEvent& event) {
            // Transform window coordinates to child (container-relative) coordinates
            Point2D childCoordinates = WindowToChild(Point2D(event.x, event.y));

            // Create modified event with container-relative coordinates
            UCEvent childEvent = event;
            childEvent.x = static_cast<int>(childCoordinates.x);
            childEvent.y = static_cast<int>(childCoordinates.y);

            // Forward to children in reverse order (top-most first)
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if ((*it) && (*it)->IsVisible()) {
                    // Check if the event is within this child's bounds (using relative coordinates)
                    if ((*it)->Contains(childEvent.x, childEvent.y)) {
                        if ((*it)->OnEvent(childEvent)) {
                            break; // Event was handled, stop propagation
                        }
                    }
                }
            }
        }

        void DrawContainerBackground() {
            // Draw background
            Rect2D bounds = GetBounds();
            SetFillColor(style.backgroundColor);
            FillRect(bounds);

            // Draw border if specified
            if (style.borderWidth > 0) {
                SetStrokeColor(style.borderColor);
                SetStrokeWidth(style.borderWidth);
                DrawRect(bounds);
            }
        }

        void RenderChildren() {
            Rect2D contentArea = GetContentArea();

            // Save current render state
            PushRenderState();

            // Translate to content area and apply scroll offset
            Translate(contentArea.x - scrollState.horizontalPosition,
                      contentArea.y - scrollState.verticalPosition);

            // Render each child using their relative coordinates
            for (auto& child : children) {
                if (child && child->IsVisible()) {
                    // Check if child is within visible area (culling optimization)
                    Rect2D childBounds = child->GetBounds();
                    Rect2D visibleArea(scrollState.horizontalPosition, scrollState.verticalPosition,
                                       contentArea.width, contentArea.height);

                    if (childBounds.Intersects(visibleArea)) {
                        // Save child's current transform
                        PushRenderState();

                        // Translate to child's relative position
                        Translate(child->GetX(), child->GetY());

                        // Render child
                        child->Render();
                        //UltraCanvas::RenderElementDebugWithCairo(child.get());

                        // Restore child's transform
                        PopRenderState();
                    }
                }
            }

            // Restore render state
            PopRenderState();
        }

        void DrawScrollbars() {
            // Draw vertical scrollbar
            if (scrollState.showVerticalScrollbar) {
                DrawVerticalScrollbar();
            }

            // Draw horizontal scrollbar
            if (scrollState.showHorizontalScrollbar) {
                DrawHorizontalScrollbar();
            }
        }

        void DrawVerticalScrollbar() {
            float containerX = static_cast<float>(GetX());
            float containerY = static_cast<float>(GetY());
            float containerWidth = static_cast<float>(GetWidth());
            float containerHeight = static_cast<float>(GetHeight());

            Rect2D trackRect(
                    containerX + containerWidth - style.scrollbarWidth,
                    containerY,
                    style.scrollbarWidth,
                    containerHeight
            );

            // Draw track
            SetFillColor(style.scrollbarTrackColor);
            DrawRect(trackRect);

            // Calculate thumb position and size
            if (scrollState.maxVerticalScroll > 0) {
                float thumbHeight = std::max(20.0f,
                                             (containerHeight * containerHeight) / (containerHeight + scrollState.maxVerticalScroll));
                float thumbPosition = (scrollState.verticalPosition / scrollState.maxVerticalScroll) *
                                      (containerHeight - thumbHeight);

                Rect2D thumbRect(
                        trackRect.x + 2.0f,
                        containerY + thumbPosition,
                        style.scrollbarWidth - 4.0f,
                        thumbHeight
                );

                // Choose thumb color based on state
                Color thumbColor = style.scrollbarThumbColor;
                if (scrollState.draggingVertical) {
                    thumbColor = style.scrollbarThumbPressedColor;
                } else if (scrollState.hoveringVerticalThumb) {
                    thumbColor = style.scrollbarThumbHoverColor;
                }

                // Draw thumb
                SetFillColor(thumbColor);
                DrawRect(thumbRect);
            }
        }

        void DrawHorizontalScrollbar() {
            float containerX = static_cast<float>(GetX());
            float containerY = static_cast<float>(GetY());
            float containerWidth = static_cast<float>(GetWidth());
            float containerHeight = static_cast<float>(GetHeight());

            Rect2D trackRect(
                    containerX,
                    containerY + containerHeight - style.scrollbarWidth,
                    containerWidth,
                    style.scrollbarWidth
            );

            // Draw track
            SetFillColor(style.scrollbarTrackColor);
            DrawRect(trackRect);

            // Calculate thumb position and size
            if (scrollState.maxHorizontalScroll > 0) {
                float thumbWidth = std::max(20.0f,
                                            (containerWidth * containerWidth) / (containerWidth + scrollState.maxHorizontalScroll));
                float thumbPosition = (scrollState.horizontalPosition / scrollState.maxHorizontalScroll) *
                                      (containerWidth - thumbWidth);

                Rect2D thumbRect(
                        containerX + thumbPosition,
                        trackRect.y + 2.0f,
                        thumbWidth,
                        style.scrollbarWidth - 4.0f
                );

                // Choose thumb color based on state
                Color thumbColor = style.scrollbarThumbColor;
                if (scrollState.draggingHorizontal) {
                    thumbColor = style.scrollbarThumbPressedColor;
                } else if (scrollState.hoveringHorizontalThumb) {
                    thumbColor = style.scrollbarThumbHoverColor;
                }

                // Draw thumb
                SetFillColor(thumbColor);
                DrawRect(thumbRect);
            }
        }

        void OnScrollChanged() {
            if (onScrollChanged) {
                onScrollChanged(scrollState.horizontalPosition, scrollState.verticalPosition);
            }
        }
    };

// ===== CONVENIENCE FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasContainer> CreateContainer(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasContainer>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasContainer> CreateScrollableContainer(
            const std::string& id, long uid, long x, long y, long w, long h,
            bool autoHide = true) {
        auto container = std::make_shared<UltraCanvasContainer>(id, uid, x, y, w, h);

        ContainerStyle style = container->GetContainerStyle();
        style.autoHideScrollbars = autoHide;
        container->SetContainerStyle(style);

        return container;
    }

} // namespace UltraCanvas