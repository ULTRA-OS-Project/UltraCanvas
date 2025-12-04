// include/UltraCanvasScrollArea.h
// Base class for scrollable areas with integrated scrollbar support
// Version: 1.0.0
// Last Modified: 2025-11-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <memory>
#include <functional>
#include <algorithm>
#include <vector>

namespace UltraCanvas {

// Forward declaration
    class UltraCanvasWindowBase;

    enum ShowScrollBar {
        Auto,
        Show,
        Hide
    };

// ===== SCROLL AREA CONFIGURATION =====
    struct ScrollAreaConfig {
        // Scrollbar enablement
        bool enableVerticalScrollbar = true;
        bool enableHorizontalScrollbar = false;

        // Scrollbar style
        ScrollbarStyle scrollbarStyle = ScrollbarStyle::Default();

        // Scrollbar placement
        bool verticalScrollbarOnRight = true;   // false = left side
        bool horizontalScrollbarOnBottom = true; // false = top side

        // Scrolling behavior
        int wheelScrollSpeed = 40;
        bool invertWheelDirection = false;
        bool enableSmoothScrolling = false;

        // Auto-hide scrollbars when content fits
//        bool autoHideScrollbars = true;

        // Content padding (inside scroll area)
//        int contentPaddingLeft = 0;
//        int contentPaddingTop = 0;
//        int contentPaddingRight = 0;
//        int contentPaddingBottom = 0;

        // Corner rectangle (where both scrollbars meet)
        Color cornerColor = Color(240, 240, 240, 255);
        bool showCorner = true;
    };

// ===== SCROLL AREA STATE =====
    struct ScrollAreaState {
        // Current scroll positions
        int scrollX = 0;
        int scrollY = 0;

        // Maximum scroll values
        int maxScrollX = 0;
        int maxScrollY = 0;

        // Content dimensions
        int contentWidth = 0;
        int contentHeight = 0;

        // Viewport dimensions (visible area)
        int viewportWidth = 0;
        int viewportHeight = 0;

        // Scrollbar visibility
        bool showVerticalScrollbar = false;
        bool showHorizontalScrollbar = false;

        // Animation state for smooth scrolling
        bool isAnimating = false;
        int targetScrollX = 0;
        int targetScrollY = 0;

        void UpdateMaxScroll() {
            maxScrollX = std::max(0, contentWidth - viewportWidth);
            maxScrollY = std::max(0, contentHeight - viewportHeight);
            scrollX = std::clamp(scrollX, 0, maxScrollX);
            scrollY = std::clamp(scrollY, 0, maxScrollY);
        }

        bool CanScrollVertically() const { return maxScrollY > 0; }
        bool CanScrollHorizontally() const { return maxScrollX > 0; }
    };

// ===== SCROLL AREA BASE CLASS =====
    class UltraCanvasScrollArea : public UltraCanvasUIElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(int, int)> onScrollChange;  // (scrollX, scrollY)
        std::function<void()> onContentSizeChange;

    protected:
        // Configuration and state
        ScrollAreaConfig config;
        ScrollAreaState scrollState;

        // Scrollbar components
        std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
        std::shared_ptr<UltraCanvasScrollbar> horizontalScrollbar;

        // Cached rectangles
        Rect2Di viewportRect;      // Visible content area
        Rect2Di contentRect;       // Full content area (may be larger than viewport)
        Rect2Di cornerRect;        // Corner where scrollbars meet
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasScrollArea(const std::string& id, long uid, int x, int y, int w, int h)
                : UltraCanvasUIElement(id, uid, x, y, w, h) {
            CreateScrollbars();
            UpdateLayout();
        }

        virtual ~UltraCanvasScrollArea() = default;

        // ===== CONFIGURATION =====
        void SetConfig(const ScrollAreaConfig& newConfig) {
            config = newConfig;
            ApplyConfigToScrollbars();
            layoutDirty = true;
            RequestRedraw();
        }

        const ScrollAreaConfig& GetConfig() const { return config; }
        ScrollAreaConfig& GetConfigRef() { return config; }

        void SetScrollbarStyle(const ScrollbarStyle& style) {
            config.scrollbarStyle = style;
            ApplyConfigToScrollbars();
            layoutDirty = true;
            RequestRedraw();
        }

        // ===== SCROLLBAR ENABLEMENT =====
        void EnableVerticalScrollbar(bool enable) {
            config.enableVerticalScrollbar = enable;
            layoutDirty = true;
            UpdateScrollbarVisibility();
            RequestRedraw();
        }

        void EnableHorizontalScrollbar(bool enable) {
            config.enableHorizontalScrollbar = enable;
            layoutDirty = true;
            UpdateScrollbarVisibility();
            RequestRedraw();
        }

        bool IsVerticalScrollbarEnabled() const { return config.enableVerticalScrollbar; }
        bool IsHorizontalScrollbarEnabled() const { return config.enableHorizontalScrollbar; }

        // ===== SCROLL POSITION =====
        void SetScrollPosition(int x, int y) {
            int newX = std::clamp(x, 0, scrollState.maxScrollX);
            int newY = std::clamp(y, 0, scrollState.maxScrollY);

            if (newX != scrollState.scrollX || newY != scrollState.scrollY) {
                scrollState.scrollX = newX;
                scrollState.scrollY = newY;
                SyncScrollbarsFromState();
                OnScrollChanged();
            }
        }

        void SetScrollX(int x) {
            SetScrollPosition(x, scrollState.scrollY);
        }

        void SetScrollY(int y) {
            SetScrollPosition(scrollState.scrollX, y);
        }

        int GetScrollX() const { return scrollState.scrollX; }
        int GetScrollY() const { return scrollState.scrollY; }
        int GetMaxScrollX() const { return scrollState.maxScrollX; }
        int GetMaxScrollY() const { return scrollState.maxScrollY; }

        // ===== SCROLL OPERATIONS =====
        void ScrollBy(int deltaX, int deltaY) {
            SetScrollPosition(scrollState.scrollX + deltaX, scrollState.scrollY + deltaY);
        }

        void ScrollToTop() {
            SetScrollY(0);
        }

        void ScrollToBottom() {
            SetScrollY(scrollState.maxScrollY);
        }

        void ScrollToLeft() {
            SetScrollX(0);
        }

        void ScrollToRight() {
            SetScrollX(scrollState.maxScrollX);
        }

        void ScrollToTopLeft() {
            SetScrollPosition(0, 0);
        }

        void ScrollToBottomRight() {
            SetScrollPosition(scrollState.maxScrollX, scrollState.maxScrollY);
        }

        // Scroll to make a rectangle visible
        void ScrollToVisible(const Rect2Di& rect) {
            int targetX = scrollState.scrollX;
            int targetY = scrollState.scrollY;

            // Horizontal adjustment
            if (rect.x < scrollState.scrollX) {
                targetX = rect.x;
            } else if (rect.x + rect.width > scrollState.scrollX + scrollState.viewportWidth) {
                targetX = rect.x + rect.width - scrollState.viewportWidth;
            }

            // Vertical adjustment
            if (rect.y < scrollState.scrollY) {
                targetY = rect.y;
            } else if (rect.y + rect.height > scrollState.scrollY + scrollState.viewportHeight) {
                targetY = rect.y + rect.height - scrollState.viewportHeight;
            }

            SetScrollPosition(targetX, targetY);
        }

        // Scroll to make a point visible (with optional margin)
        void ScrollToVisible(int x, int y, int margin = 0) {
            ScrollToVisible(Rect2Di(x - margin, y - margin, margin * 2, margin * 2));
        }

        // ===== CONTENT SIZE =====
        void SetContentSize(int width, int height) {
            if (width != scrollState.contentWidth || height != scrollState.contentHeight) {
                scrollState.contentWidth = std::max(0, width);
                scrollState.contentHeight = std::max(0, height);
                UpdateScrollState();

                if (onContentSizeChange) {
                    onContentSizeChange();
                }
            }
        }

        int GetContentWidth() const { return scrollState.contentWidth; }
        int GetContentHeight() const { return scrollState.contentHeight; }

        // ===== VIEWPORT =====
        Rect2Di GetViewportRect() const { return viewportRect; }
        int GetViewportWidth() const { return scrollState.viewportWidth; }
        int GetViewportHeight() const { return scrollState.viewportHeight; }

        // ===== SCROLLBAR VISIBILITY =====
        bool IsVerticalScrollbarVisible() const { return scrollState.showVerticalScrollbar; }
        bool IsHorizontalScrollbarVisible() const { return scrollState.showHorizontalScrollbar; }

        // ===== SCROLLBAR ACCESS =====
        UltraCanvasScrollbar* GetVerticalScrollbar() const { return verticalScrollbar.get(); }
        UltraCanvasScrollbar* GetHorizontalScrollbar() const { return horizontalScrollbar.get(); }

        // ===== COORDINATE CONVERSION =====
        // Convert viewport coordinates to content coordinates
        Point2Di ViewportToContent(const Point2Di& viewportPos) const {
            return Point2Di(viewportPos.x + scrollState.scrollX - viewportRect.x,
                            viewportPos.y + scrollState.scrollY - viewportRect.y);
        }

        Point2Di ViewportToContent(int vx, int vy) const {
            return ViewportToContent(Point2Di(vx, vy));
        }

        // Convert content coordinates to viewport coordinates
        Point2Di ContentToViewport(const Point2Di& contentPos) const {
            return Point2Di(contentPos.x - scrollState.scrollX + viewportRect.x,
                            contentPos.y - scrollState.scrollY + viewportRect.y);
        }

        Point2Di ContentToViewport(int cx, int cy) const {
            return ContentToViewport(Point2Di(cx, cy));
        }

        // Check if a content rectangle is visible in viewport
        bool IsRectVisible(const Rect2Di& contentRect) const {
            Rect2Di visibleContent(scrollState.scrollX, scrollState.scrollY,
                                   scrollState.viewportWidth, scrollState.viewportHeight);
            return visibleContent.Intersects(contentRect);
        }

        // Get the visible portion of the content
        Rect2Di GetVisibleContentRect() const {
            return Rect2Di(scrollState.scrollX, scrollState.scrollY,
                           scrollState.viewportWidth, scrollState.viewportHeight);
        }

        // ===== STATE ACCESS =====
        const ScrollAreaState& GetScrollState() const { return scrollState; }
        bool CanScrollVertically() const { return scrollState.CanScrollVertically(); }
        bool CanScrollHorizontally() const { return scrollState.CanScrollHorizontally(); }

        // ===== LAYOUT =====
        void InvalidateLayout() {
            layoutDirty = true;
        }

        // ===== WINDOW PROPAGATION =====
        void SetWindow(UltraCanvasWindowBase* win) override {
            UltraCanvasUIElement::SetWindow(win);
            if (verticalScrollbar) {
                verticalScrollbar->SetWindow(win);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetWindow(win);
            }
        }

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override {
            if (!ctx || !IsVisible()) return;

            ctx->PushState();

            // Update layout if needed
            if (layoutDirty) {
                UpdateLayout();
            }

            // Render background (can be overridden)
            RenderBackground(ctx);

            // Set up clipping for content area
            ctx->PushState();
            ctx->ClipRect(viewportRect);

            // Apply scroll offset translation
            ctx->Translate(viewportRect.x - scrollState.scrollX,
                           viewportRect.y - scrollState.scrollY);

            // Render content (to be implemented by derived classes)
            RenderContent(ctx);

            ctx->PopState();  // Remove clipping

            // Render scrollbars on top
            RenderScrollbars(ctx);

            // Render corner if both scrollbars visible
            if (config.showCorner && scrollState.showVerticalScrollbar && scrollState.showHorizontalScrollbar) {
                RenderCorner(ctx);
            }

            ctx->PopState();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (IsDisabled() || !IsVisible()) return false;

            if (layoutDirty) {
                UpdateLayout();
            }

            // Handle scrollbar events first
            if (HandleScrollbarEvents(event)) {
                return true;
            }

            // Handle wheel scrolling in viewport
            if (event.type == UCEventType::MouseWheel) {
                if (viewportRect.Contains(Point2Di(event.x, event.y))) {
                    return HandleWheelScroll(event);
                }
            }

            // Forward other events to derived class
            return HandleContentEvent(event);
        }

    protected:
        // ===== VIRTUAL METHODS FOR DERIVED CLASSES =====

        // Override to render content (called with scroll offset already applied)
        virtual void RenderContent(IRenderContext* ctx) {
            // Default: empty - derived classes should override
        }

        // Override to handle events in content area
        virtual bool HandleContentEvent(const UCEvent& event) {
            return UltraCanvasUIElement::OnEvent(event);
        }

        // Override to render custom background
        virtual void RenderBackground(IRenderContext* ctx) {
            // Default: transparent background
        }

        // Called when scroll position changes
        virtual void OnScrollChanged() {
            RequestRedraw();

            if (onScrollChange) {
                onScrollChange(scrollState.scrollX, scrollState.scrollY);
            }
        }

        // Called to calculate content size (override in derived classes)
        virtual void CalculateContentSize() {
            // Default: content size equals viewport size (no scrolling)
            scrollState.contentWidth = scrollState.viewportWidth;
            scrollState.contentHeight = scrollState.viewportHeight;
        }

        // ===== LAYOUT MANAGEMENT =====
        void UpdateLayout() {
            Rect2Di bounds = GetBounds();

            // Calculate viewport size (accounting for scrollbars)
            CalculateViewportRect(bounds);

            // Update viewport dimensions in state
            scrollState.viewportWidth = viewportRect.width;
            scrollState.viewportHeight = viewportRect.height;

            // Let derived class calculate content size
            CalculateContentSize();

            // Update scroll state and scrollbar visibility
            UpdateScrollState();

            // Position scrollbars
            PositionScrollbars(bounds);

            layoutDirty = false;
        }

        void CalculateViewportRect(const Rect2Di& bounds) {
            auto clientRect = UltraCanvasUIElement::GetClientRect();

            // Determine if scrollbars will be needed
            if (!config.enableVerticalScrollbar);
            auto clientRect = UltraCanvasUIElement::GetClientRect();
            if (config.autoHideScrollbars) {
                return scrollState.contentHeight > clientRect.height;
            }
            return true;

            bool needsVertical = config.enableVerticalScrollbar && WillNeedVerticalScrollbar();
            bool needsHorizontal = config.enableHorizontalScrollbar && WillNeedHorizontalScrollbar();

            // Iterative calculation (scrollbars may affect each other)
            for (int i = 0; i < 2; ++i) {
                int sbWidth = config.scrollbarStyle.trackWidth;

                int viewportWidth = clientRect.width;
                int viewportHeight = clientRect.height;

                if (needsVertical) {
                    viewportWidth -= sbWidth;
                }
                if (needsHorizontal) {
                    viewportHeight -= sbWidth;
                }

                // Re-check if scrollbars are needed with adjusted viewport
                if (config.enableVerticalScrollbar) {
                    needsVertical = scrollState.contentHeight > viewportHeight;
                }
                if (config.enableHorizontalScrollbar) {
                    needsHorizontal = scrollState.contentWidth > viewportWidth;
                }
            }

            // Calculate final viewport
            int viewportWidth = clientRect.width;
            int viewportHeight = clientRect.height;
            int sbWidth = config.scrollbarStyle.trackWidth;

            if (needsVertical) {
                if (config.verticalScrollbarOnRight) {
                    viewportWidth -= sbWidth;
                } else {
                    left += sbWidth;
                    viewportWidth -= sbWidth;
                }
            }

            if (needsHorizontal) {
                if (config.horizontalScrollbarOnBottom) {
                    viewportHeight -= sbWidth;
                } else {
                    top += sbWidth;
                    viewportHeight -= sbWidth;
                }
            }

            viewportRect = Rect2Di(left, top,
                                   std::max(0, viewportWidth),
                                   std::max(0, viewportHeight));

            // Update scrollbar visibility
            scrollState.showVerticalScrollbar = needsVertical;
            scrollState.showHorizontalScrollbar = needsHorizontal;
        }

        bool WillNeedVerticalScrollbar() const {
            if (!config.enableVerticalScrollbar) return false;
            auto clientRect = UltraCanvasUIElement::GetClientRect();
            if (config.autoHideScrollbars) {
                return scrollState.contentHeight > clientRect.height;
            }
            return true;
        }

        bool WillNeedHorizontalScrollbar() const {
            if (!config.enableHorizontalScrollbar) return false;
            if (config.autoHideScrollbars) {
                return scrollState.contentWidth > (GetWidth() - config.contentPaddingLeft - config.contentPaddingRight);
            }
            return true;
        }

        void UpdateScrollState() {
            scrollState.UpdateMaxScroll();
            UpdateScrollbarVisibility();
            SyncScrollbarsFromState();
        }

        void UpdateScrollbarVisibility() {
            if (verticalScrollbar) {
                verticalScrollbar->SetVisible(scrollState.showVerticalScrollbar);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetVisible(scrollState.showHorizontalScrollbar);
            }
        }

        void PositionScrollbars(const Rect2Di& bounds) {
            int sbWidth = config.scrollbarStyle.trackWidth;

            // Position vertical scrollbar
            if (verticalScrollbar && scrollState.showVerticalScrollbar) {
                int sbX = config.verticalScrollbarOnRight ?
                          (bounds.x + bounds.width - sbWidth - config.contentPaddingRight) :
                          (bounds.x + config.contentPaddingLeft);
                int sbY = bounds.y + config.contentPaddingTop;
                int sbHeight = viewportRect.height;

                verticalScrollbar->SetPosition(sbX, sbY);
                verticalScrollbar->SetSize(sbWidth, sbHeight);
                verticalScrollbar->SetScrollParameters(scrollState.viewportHeight, scrollState.contentHeight);
            }

            // Position horizontal scrollbar
            if (horizontalScrollbar && scrollState.showHorizontalScrollbar) {
                int sbX = bounds.x + config.contentPaddingLeft;
                int sbY = config.horizontalScrollbarOnBottom ?
                          (bounds.y + bounds.height - sbWidth - config.contentPaddingBottom) :
                          (bounds.y + config.contentPaddingTop);
                int sbWidth_h = viewportRect.width;

                horizontalScrollbar->SetPosition(sbX, sbY);
                horizontalScrollbar->SetSize(sbWidth_h, sbWidth);
                horizontalScrollbar->SetScrollParameters(scrollState.viewportWidth, scrollState.contentWidth);
            }

            // Calculate corner rect
            if (scrollState.showVerticalScrollbar && scrollState.showHorizontalScrollbar) {
                int cornerX = config.verticalScrollbarOnRight ?
                              (bounds.x + bounds.width - sbWidth - config.contentPaddingRight) :
                              (bounds.x + config.contentPaddingLeft);
                int cornerY = config.horizontalScrollbarOnBottom ?
                              (bounds.y + bounds.height - sbWidth - config.contentPaddingBottom) :
                              (bounds.y + config.contentPaddingTop);

                cornerRect = Rect2Di(cornerX, cornerY, sbWidth, sbWidth);
            } else {
                cornerRect = Rect2Di(0, 0, 0, 0);
            }
        }

        void SyncScrollbarsFromState() {
            if (verticalScrollbar) {
                verticalScrollbar->SetScrollPosition(scrollState.scrollY);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetScrollPosition(scrollState.scrollX);
            }
        }

        // ===== SCROLLBAR CREATION =====
        void CreateScrollbars() {
            // Create vertical scrollbar
            verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
                    GetIdentifier() + "_vscroll", 0, 0, 0, 16, 100,
                    ScrollbarOrientation::Vertical);

            verticalScrollbar->onScrollChange = [this](int pos) {
                if (pos != scrollState.scrollY) {
                    scrollState.scrollY = pos;
                    OnScrollChanged();
                }
            };

            // Create horizontal scrollbar
            horizontalScrollbar = std::make_shared<UltraCanvasScrollbar>(
                    GetIdentifier() + "_hscroll", 0, 0, 0, 100, 16,
                    ScrollbarOrientation::Horizontal);

            horizontalScrollbar->onScrollChange = [this](int pos) {
                if (pos != scrollState.scrollX) {
                    scrollState.scrollX = pos;
                    OnScrollChanged();
                }
            };

            ApplyConfigToScrollbars();
        }

        void ApplyConfigToScrollbars() {
            if (verticalScrollbar) {
                verticalScrollbar->SetStyle(config.scrollbarStyle);
            }
            if (horizontalScrollbar) {
                horizontalScrollbar->SetStyle(config.scrollbarStyle);
            }
        }

        // ===== RENDERING HELPERS =====
        void RenderScrollbars(IRenderContext* ctx) {
            if (verticalScrollbar && scrollState.showVerticalScrollbar) {
                verticalScrollbar->Render(ctx);
            }
            if (horizontalScrollbar && scrollState.showHorizontalScrollbar) {
                horizontalScrollbar->Render(ctx);
            }
        }

        void RenderCorner(IRenderContext* ctx) {
            if (cornerRect.width > 0 && cornerRect.height > 0) {
                ctx->DrawFilledRectangle(cornerRect, config.cornerColor);
            }
        }

        // ===== EVENT HANDLING =====
        bool HandleScrollbarEvents(const UCEvent& event) {
            bool handled = false;

            // Check vertical scrollbar
            if (verticalScrollbar && scrollState.showVerticalScrollbar) {
                if (verticalScrollbar->Contains(event.x, event.y) ||
                    verticalScrollbar->IsDragging()) {
                    if (verticalScrollbar->OnEvent(event)) {
                        handled = true;
                    }
                }
            }

            // Check horizontal scrollbar
            if (!handled && horizontalScrollbar && scrollState.showHorizontalScrollbar) {
                if (horizontalScrollbar->Contains(event.x, event.y) ||
                    horizontalScrollbar->IsDragging()) {
                    if (horizontalScrollbar->OnEvent(event)) {
                        handled = true;
                    }
                }
            }

            return handled;
        }

        bool HandleWheelScroll(const UCEvent& event) {
            int scrollAmount = event.wheelDelta * config.wheelScrollSpeed;
            if (config.invertWheelDirection) {
                scrollAmount = -scrollAmount;
            }

            // Shift+wheel for horizontal scrolling
            if (event.shift && config.enableHorizontalScrollbar && scrollState.showHorizontalScrollbar) {
                ScrollBy(-scrollAmount, 0);
                return true;
            }

            // Normal wheel for vertical scrolling
            if (config.enableVerticalScrollbar && scrollState.showVerticalScrollbar) {
                ScrollBy(0, -scrollAmount);
                return true;
            }

            // Fall back to horizontal if only horizontal is enabled
            if (config.enableHorizontalScrollbar && scrollState.showHorizontalScrollbar) {
                ScrollBy(-scrollAmount, 0);
                return true;
            }

            return false;
        }
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasScrollArea> CreateScrollArea(
            const std::string& id, long uid, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasScrollArea>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasScrollArea> CreateScrollArea(
            const std::string& id, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasScrollArea>(id, 0, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasScrollArea> CreateVerticalScrollArea(
            const std::string& id, long uid, int x, int y, int w, int h) {
        auto area = std::make_shared<UltraCanvasScrollArea>(id, uid, x, y, w, h);
        area->EnableVerticalScrollbar(true);
        area->EnableHorizontalScrollbar(false);
        return area;
    }

    inline std::shared_ptr<UltraCanvasScrollArea> CreateHorizontalScrollArea(
            const std::string& id, long uid, int x, int y, int w, int h) {
        auto area = std::make_shared<UltraCanvasScrollArea>(id, uid, x, y, w, h);
        area->EnableVerticalScrollbar(false);
        area->EnableHorizontalScrollbar(true);
        return area;
    }

    inline std::shared_ptr<UltraCanvasScrollArea> CreateBidirectionalScrollArea(
            const std::string& id, long uid, int x, int y, int w, int h) {
        auto area = std::make_shared<UltraCanvasScrollArea>(id, uid, x, y, w, h);
        area->EnableVerticalScrollbar(true);
        area->EnableHorizontalScrollbar(true);
        return area;
    }

} // namespace UltraCanvas