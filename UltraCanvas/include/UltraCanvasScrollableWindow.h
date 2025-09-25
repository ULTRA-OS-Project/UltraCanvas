// include/UltraCanvasScrollableWindow.h
// Scrollable window component that integrates scrollbars with content area
// Version: 1.0.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasScrollbar.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== SCROLLABLE WINDOW CONFIGURATION =====
    struct ScrollableWindowConfig {
        // Scrollbar settings
        bool enableVerticalScrollbar = true;
        bool enableHorizontalScrollbar = false;
        ScrollbarAppearance scrollbarAppearance = ScrollbarAppearance::Default();

        // Content area settings
        Color backgroundColor = Color(255, 255, 255, 255);
        Color borderColor = Color(200, 200, 200, 255);
        float borderWidth = 1.0f;

        // Padding around content
        float paddingLeft = 5.0f;
        float paddingTop = 5.0f;
        float paddingRight = 5.0f;
        float paddingBottom = 5.0f;

        // Auto-size content area based on children
        bool autoCalculateContentSize = true;

        // Manual content size (used when autoCalculateContentSize = false)
        float manualContentWidth = 0.0f;
        float manualContentHeight = 0.0f;
    };

// ===== SCROLLABLE WINDOW CLASS =====
    class UltraCanvasScrollableWindow : public UltraCanvasUIElement {
    private:
        // ===== STANDARD PROPERTIES =====
        StandardProperties properties;

        // ===== CONFIGURATION =====
        ScrollableWindowConfig config;

        // ===== SCROLLBARS =====
        std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
        std::shared_ptr<UltraCanvasScrollbar> horizontalScrollbar;

        // ===== CHILD ELEMENTS =====
        std::vector<std::shared_ptr<UltraCanvasUIElement>> children;

        // ===== LAYOUT STATE =====
        Rect2D contentArea;
        Point2D contentSize;
        Point2D scrollOffset;
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasScrollableWindow(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasUIElement(id, uid, x, y, w, h),
                  properties(id, uid, x, y, w, h),
                  scrollOffset(0, 0) {

            CreateScrollbars();
            UpdateLayout();
        }

        // ===== STANDARD PROPERTIES ACCESSORS =====
        ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()

        // ===== CONFIGURATION =====
        void SetScrollableWindowConfig(const ScrollableWindowConfig& newConfig) {
            config = newConfig;
            UpdateScrollbarAppearance();
            layoutDirty = true;
            UpdateLayout();
        }

        const ScrollableWindowConfig& GetScrollableWindowConfig() const { return config; }

        // ===== CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasUIElement> child) {
            if (child) {
                children.push_back(child);
                layoutDirty = true;
                UpdateContentSize();
                UpdateScrollbars();
            }
        }

        void RemoveChild(std::shared_ptr<UltraCanvasUIElement> child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) {
                children.erase(it);
                layoutDirty = true;
                UpdateContentSize();
                UpdateScrollbars();
            }
        }

        void ClearChildren() {
            children.clear();
            layoutDirty = true;
            UpdateContentSize();
            UpdateScrollbars();
        }

        const std::vector<std::shared_ptr<UltraCanvasUIElement>>& GetChildren() const {
            return children;
        }

        // ===== SCROLLING CONTROL =====
        void ScrollTo(float x, float y) {
            scrollOffset.x = std::clamp(x, 0.0f, std::max(0.0f, contentSize.x - contentArea.width));
            scrollOffset.y = std::clamp(y, 0.0f, std::max(0.0f, contentSize.y - contentArea.height));

            if (verticalScrollbar) {
                verticalScrollbar->SetScrollPosition(scrollOffset.y);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetScrollPosition(scrollOffset.x);
            }

            if (onScrollChanged) {
                onScrollChanged(scrollOffset.x, scrollOffset.y);
            }
        }

        void ScrollBy(float deltaX, float deltaY) {
            ScrollTo(scrollOffset.x + deltaX, scrollOffset.y + deltaY);
        }

        Point2D GetScrollOffset() const { return scrollOffset; }

        void ScrollToTop() { ScrollTo(scrollOffset.x, 0); }
        void ScrollToBottom() { ScrollTo(scrollOffset.x, contentSize.y - contentArea.height); }
        void ScrollToLeft() { ScrollTo(0, scrollOffset.y); }
        void ScrollToRight() { ScrollTo(contentSize.x - contentArea.width, scrollOffset.y); }

        // ===== CONTENT SIZE MANAGEMENT =====
        void SetManualContentSize(float width, float height) {
            config.autoCalculateContentSize = false;
            config.manualContentWidth = width;
            config.manualContentHeight = height;
            UpdateContentSize();
            UpdateScrollbars();
        }

        void EnableAutoContentSize() {
            config.autoCalculateContentSize = true;
            UpdateContentSize();
            UpdateScrollbars();
        }

        Point2D GetContentSize() const { return contentSize; }
        Rect2D GetContentArea() const { return contentArea; }

        // ===== RENDERING =====
        void Render() override {
            ctx->PushState();

            UpdateLayout();

            // Draw background and border
            DrawBackground();

            // Set clipping to content area
            ctx->SetClipRect(contentArea);

            // Render children with scroll offset
            RenderChildren();

            // Clear clipping
            ctx->ClearClipRect();

            // Render scrollbars
            RenderScrollbars();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            UpdateLayout();

            // Handle scrollbar events first
            if (HandleScrollbarEvents(event)) {
                return true;
            }

            // Handle wheel scrolling
            if (event.type == UCEventType::MouseWheel && IsPointInContentArea(Point2D(event.x, event.y))) {
                return HandleWheelScrolling(event);
            }

            // Forward events to children
            if (IsPointInContentArea(Point2D(event.x, event.y))) {
                return ForwardEventToChildren(event);
            }

            return false;
        }

        // ===== EVENT CALLBACKS =====
        std::function<void(float, float)> onScrollChanged;

    private:
        // ===== INITIALIZATION =====
        void CreateScrollbars() {
            if (config.enableVerticalScrollbar) {
                verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
                        GetId() + "_vscroll", GetUid() + 1000, 0, 0,
                        config.scrollbarAppearance.trackWidth, 100,
                        ScrollbarOrientation::Vertical
                );

                verticalScrollbar->SetAppearance(config.scrollbarAppearance);
                verticalScrollbar->onScrollChanged = [this](float position) {
                    scrollOffset.y = position;
                    if (onScrollChanged) {
                        onScrollChanged(scrollOffset.x, scrollOffset.y);
                    }
                };
            }

            if (config.enableHorizontalScrollbar) {
                horizontalScrollbar = std::make_shared<UltraCanvasScrollbar>(
                        GetId() + "_hscroll", GetUid() + 1001, 0, 0,
                        100, config.scrollbarAppearance.trackWidth,
                        ScrollbarOrientation::Horizontal
                );

                horizontalScrollbar->SetAppearance(config.scrollbarAppearance);
                horizontalScrollbar->onScrollChanged = [this](float position) {
                    scrollOffset.x = position;
                    if (onScrollChanged) {
                        onScrollChanged(scrollOffset.x, scrollOffset.y);
                    }
                };
            }
        }

        void UpdateScrollbarAppearance() {
            if (verticalScrollbar) {
                verticalScrollbar->SetAppearance(config.scrollbarAppearance);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetAppearance(config.scrollbarAppearance);
            }
        }

        // ===== LAYOUT MANAGEMENT =====
        void UpdateLayout() {
            if (!layoutDirty) return;

            Rect2D bounds = GetBounds();

            // Calculate content area
            float contentWidth = bounds.width - config.paddingLeft - config.paddingRight;
            float contentHeight = bounds.height - config.paddingTop - config.paddingBottom;

            // Reserve space for scrollbars
            if (config.enableVerticalScrollbar && verticalScrollbar && verticalScrollbar->ShouldBeVisible()) {
                contentWidth -= config.scrollbarAppearance.trackWidth;
            }
            if (config.enableHorizontalScrollbar && horizontalScrollbar && horizontalScrollbar->ShouldBeVisible()) {
                contentHeight -= config.scrollbarAppearance.trackWidth;
            }

            contentArea = Rect2D(
                    bounds.x + config.paddingLeft,
                    bounds.y + config.paddingTop,
                    std::max(0.0f, contentWidth),
                    std::max(0.0f, contentHeight)
            );

            // Position scrollbars
            if (verticalScrollbar) {
                float scrollbarX = bounds.x + bounds.width - config.scrollbarAppearance.trackWidth;
                verticalScrollbar->SetPosition(scrollbarX, bounds.y);
                verticalScrollbar->SetSize(config.scrollbarAppearance.trackWidth, bounds.height);
            }

            if (horizontalScrollbar) {
                float scrollbarY = bounds.y + bounds.height - config.scrollbarAppearance.trackWidth;
                horizontalScrollbar->SetPosition(bounds.x, scrollbarY);
                horizontalScrollbar->SetSize(bounds.width, config.scrollbarAppearance.trackWidth);
            }

            UpdateContentSize();
            UpdateScrollbars();

            layoutDirty = false;
        }

        void UpdateContentSize() {
            if (config.autoCalculateContentSize) {
                CalculateAutoContentSize();
            } else {
                contentSize.x = config.manualContentWidth;
                contentSize.y = config.manualContentHeight;
            }
        }

        void CalculateAutoContentSize() {
            float maxX = 0;
            float maxY = 0;

            for (const auto& child : children) {
                if (child && child->IsVisible()) {
                    float childRight = child->GetX() + child->GetWidth();
                    float childBottom = child->GetY() + child->GetHeight();
                    maxX = std::max(maxX, childRight);
                    maxY = std::max(maxY, childBottom);
                }
            }

            // Add some padding to the calculated content size
            contentSize.x = maxX + config.paddingRight;
            contentSize.y = maxY + config.paddingBottom;
        }

        void UpdateScrollbars() {
            if (verticalScrollbar) {
                verticalScrollbar->SetScrollParameters(contentArea.height, contentSize.y);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetScrollParameters(contentArea.width, contentSize.x);
            }
        }

        // ===== RENDERING HELPERS =====
        void DrawBackground() {
            Rect2D bounds = GetBounds();

            // Draw background
           ctx->SetFillColor(config.backgroundColor);
            DrawFilledRect(bounds);

            // Draw border
            if (config.borderWidth > 0) {
                ctx->SetStrokeColor(config.borderColor);
                ctx->SetStrokeWidth(config.borderWidth);
                ctx->DrawRectangle(bounds);
            }
        }

        void RenderChildren() {
            for (const auto& child : children) {
                if (child && child->IsVisible()) {
                    // Save current transform
                    PushMatrix();

                    // Apply scroll offset
                    Translate(-scrollOffset.x, -scrollOffset.y);

                    // Render child
                    child->Render();

                    // Restore transform
                    PopMatrix();
                }
            }
        }

        void RenderScrollbars() {
            if (verticalScrollbar && verticalScrollbar->ShouldBeVisible()) {
                verticalScrollbar->Render();
            }
            if (horizontalScrollbar && horizontalScrollbar->ShouldBeVisible()) {
                horizontalScrollbar->Render();
            }
        }

        // ===== EVENT HANDLING HELPERS =====
        bool HandleScrollbarEvents(const UCEvent& event) {
            bool handled = false;

            if (verticalScrollbar && verticalScrollbar->ShouldBeVisible()) {
                if (verticalScrollbar->OnEvent(event)) {
                    handled = true;
                }
            }

            if (horizontalScrollbar && horizontalScrollbar->ShouldBeVisible()) {
                if (horizontalScrollbar->OnEvent(event)) {
                    handled = true;
                }
            }

            return handled;
        }

        bool HandleWheelScrolling(const UCEvent& event) {
            float scrollAmount = event.wheelDelta * config.scrollbarAppearance.scrollSpeed;

            if (event.shift && config.enableHorizontalScrollbar) {
                // Horizontal scrolling with Shift+Wheel
                ScrollBy(scrollAmount, 0);
            } else if (config.enableVerticalScrollbar) {
                // Vertical scrolling
                ScrollBy(0, -scrollAmount); // Invert for natural scrolling
            }

            return true;
        }

        bool ForwardEventToChildren(const UCEvent& event) {
            // Create modified event with scroll offset applied
            UCEvent childEvent = event;
            childEvent.x += static_cast<int>(scrollOffset.x);
            childEvent.y += static_cast<int>(scrollOffset.y);

            // Forward to children in reverse order (top-most first)
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if ((*it) && (*it)->IsVisible() && (*it)->OnEvent(childEvent)) {
                    return true;
                }
            }

            return false;
        }

        bool IsPointInContentArea(const Point2D& point) const {
            return contentArea.Contains(point);
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasScrollableWindow> CreateScrollableWindow(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasScrollableWindow>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasScrollableWindow> CreateVerticalScrollableWindow(
            const std::string& id, long uid, long x, long y, long w, long h) {
        auto window = std::make_shared<UltraCanvasScrollableWindow>(id, uid, x, y, w, h);

        ScrollableWindowConfig config = window->GetScrollableWindowConfig();
        config.enableVerticalScrollbar = true;
        config.enableHorizontalScrollbar = false;
        window->SetScrollableWindowConfig(config);

        return window;
    }

    inline std::shared_ptr<UltraCanvasScrollableWindow> CreateBothScrollableWindow(
            const std::string& id, long uid, long x, long y, long w, long h) {
        auto window = std::make_shared<UltraCanvasScrollableWindow>(id, uid, x, y, w, h);

        ScrollableWindowConfig config = window->GetScrollableWindowConfig();
        config.enableVerticalScrollbar = true;
        config.enableHorizontalScrollbar = true;
        window->SetScrollableWindowConfig(config);

        return window;
    }

} // namespace UltraCanvas