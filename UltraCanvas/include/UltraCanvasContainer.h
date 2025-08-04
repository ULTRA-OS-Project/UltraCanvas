// UltraCanvasContainer.h
// Container component with scrollbars and child element management
// Version: 1.2.6
// Last Modified: 2025-01-06
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderInterface.h"
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
        Color backgroundColor = Color(255, 255, 255, 255);
        float borderWidth = 1.0f;

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
                UpdateScrollability();
            }
        }

        void RemoveChild(std::shared_ptr<UltraCanvasElement> child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) {
                children.erase(it);
                UpdateScrollability();
            }
        }

        void ClearChildren() {
            children.clear();
            scrollState.verticalPosition = 0.0f;
            scrollState.horizontalPosition = 0.0f;
            UpdateScrollability();
        }

        const std::vector<std::shared_ptr<UltraCanvasElement>>& GetChildren() const {
            return children;
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
                    float childRight = child->GetX() + child->GetWidth();
                    float childBottom = child->GetY() + child->GetHeight();
                    maxX = std::max(maxX, childRight);
                    maxY = std::max(maxY, childBottom);
                }
            }

            return Point2D(maxX, maxY);
        }

        // ===== RENDERING =====
        void Render() override {
            ULTRACANVAS_RENDER_SCOPE();

            // Draw container background and border
            DrawContainerBackground();

            // Set up clipping for content area
            Rect2D contentArea = GetContentArea();
            SetClipRect(contentArea);

            // Render children with scroll offset
            RenderChildren();

            // Restore clipping
            ClearClipRect();

            // Draw scrollbars
            DrawScrollbars();
        }

        // ===== EVENT HANDLING =====
        void OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return;

            // Handle scrollbar interactions first
            if (HandleScrollbarEvents(event)) {
                return;
            }

            // Handle scroll wheel events
            if (event.type == UCEventType::MouseWheel) {
                if (HandleScrollWheel(event)) {
                    return;
                }
            }

            // Handle mouse events for container interactions
            if (event.type == UCEventType::MouseDown ||
                event.type == UCEventType::MouseUp ||
                event.type == UCEventType::MouseMove) {
                if (HandleMouseClick(event)) {
                    return;
                }
            }

            // Forward events to children within content area
            if (IsPointInContentArea(Point2D(event.x, event.y))) {
                ForwardEventToChildren(event);
            }
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

        void DrawContainerBackground() {
            // Draw background
            Rect2D bounds = GetBounds();
            DrawRect(bounds);
            SetFillColor(style.backgroundColor);

            // Draw border if specified
            if (style.borderWidth > 0) {
                SetStrokeColor(style.borderColor);
                SetStrokeWidth(style.borderWidth);
                DrawRect(bounds);
            }
        }

        void RenderChildren() {
            Rect2D contentArea = GetContentArea();

            // Apply scroll offset
            float scrollX = -scrollState.horizontalPosition;
            float scrollY = -scrollState.verticalPosition;

            PushRenderState();
            Translate(scrollX, scrollY);

            // Render each child
            for (auto& child : children) {
                if (child && child->IsVisible()) {
                    // Check if child is within visible area (culling optimization)
                    Rect2D childBounds = child->GetBounds();
                    if (childBounds.Intersects(contentArea)) {
                        child->Render();
                    }
                }
            }

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
            // *** FIXED: Use getter methods instead of raw variables ***
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
                float thumbHeight = std::max(20.0f, (containerHeight / (containerHeight + scrollState.maxVerticalScroll)) * containerHeight);
                float thumbY = containerY + (scrollState.verticalPosition / scrollState.maxVerticalScroll) * (containerHeight - thumbHeight);

                Rect2D thumbRect(trackRect.x, thumbY, style.scrollbarWidth, thumbHeight);

                // Choose thumb color based on state
                Color thumbColor = style.scrollbarThumbColor;
                if (scrollState.draggingVertical) {
                    thumbColor = style.scrollbarThumbPressedColor;
                } else if (scrollState.hoveringVerticalThumb) {
                    thumbColor = style.scrollbarThumbHoverColor;
                }

                SetFillColor(thumbColor);
                DrawRect(thumbRect);
            }
        }

        void DrawHorizontalScrollbar() {
            // *** FIXED: Use getter methods instead of raw variables ***
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
                float thumbWidth = std::max(20.0f, (containerWidth / (containerWidth + scrollState.maxHorizontalScroll)) * containerWidth);
                float thumbX = containerX + (scrollState.horizontalPosition / scrollState.maxHorizontalScroll) * (containerWidth - thumbWidth);

                Rect2D thumbRect(thumbX, trackRect.y, thumbWidth, style.scrollbarWidth);

                // Choose thumb color based on state
                Color thumbColor = style.scrollbarThumbColor;
                if (scrollState.draggingHorizontal) {
                    thumbColor = style.scrollbarThumbPressedColor;
                } else if (scrollState.hoveringHorizontalThumb) {
                    thumbColor = style.scrollbarThumbHoverColor;
                }

                SetFillColor(thumbColor);
                DrawRect(thumbRect);
            }
        }

        bool HandleMouseClick(const UCEvent& event) {
            if (event.type == UCEventType::MouseDown) {
                // *** FIXED: Use getter methods instead of raw variables ***
                float containerX = static_cast<float>(GetX());
                float containerY = static_cast<float>(GetY());
                float containerWidth = static_cast<float>(GetWidth());
                float containerHeight = static_cast<float>(GetHeight());

                // Check for vertical scrollbar interaction
                if (scrollState.showVerticalScrollbar) {
                    Rect2D vScrollArea(
                            containerX + containerWidth - style.scrollbarWidth,
                            containerY,
                            style.scrollbarWidth,
                            containerHeight
                    );

                    if (vScrollArea.Contains(Point2D(event.x, event.y))) {
                        HandleVerticalScrollbarClick(event);
                        return true;
                    }
                }

                // Check for horizontal scrollbar interaction
                if (scrollState.showHorizontalScrollbar) {
                    Rect2D hScrollArea(
                            containerX,
                            containerY + containerHeight - style.scrollbarWidth,
                            containerWidth,
                            style.scrollbarWidth
                    );

                    if (hScrollArea.Contains(Point2D(event.x, event.y))) {
                        HandleHorizontalScrollbarClick(event);
                        return true;
                    }
                }
            }

            return false;
        }

        bool HandleVerticalScrollbarClick(const UCEvent& event) {
            // Implementation for vertical scrollbar click handling
            scrollState.draggingVertical = true;
            scrollState.dragStartPosition = event.y;
            scrollState.dragStartScroll = scrollState.verticalPosition;
            return true;
        }

        bool HandleHorizontalScrollbarClick(const UCEvent& event) {
            // Implementation for horizontal scrollbar click handling
            scrollState.draggingHorizontal = true;
            scrollState.dragStartPosition = event.x;
            scrollState.dragStartScroll = scrollState.horizontalPosition;
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
            // Create modified event with scroll offset
            UCEvent childEvent = event;
            childEvent.x += static_cast<int>(scrollState.horizontalPosition);
            childEvent.y += static_cast<int>(scrollState.verticalPosition);

            // Forward to children in reverse order (top-most first)
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if ((*it) && (*it)->IsVisible()) {
                    (*it)->OnEvent(childEvent);
                }
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