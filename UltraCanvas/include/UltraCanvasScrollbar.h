// UltraCanvasScrollbar.h
// Platform-independent scrollbar component for UltraCanvas windows
// Version: 1.0.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderInterface.h"
#include <functional>
#include <algorithm>

namespace UltraCanvas {

// ===== SCROLLBAR ORIENTATION =====
    enum class ScrollbarOrientation {
        Vertical,
        Horizontal
    };

// ===== SCROLLBAR APPEARANCE SETTINGS =====
    struct ScrollbarAppearance {
        // Dimensions
        float trackWidth = 16.0f;
        float thumbMinSize = 20.0f;
        float arrowButtonSize = 16.0f;

        // Colors
        Color trackColor = Color(240, 240, 240, 255);
        Color thumbColor = Color(192, 192, 192, 255);
        Color thumbHoverColor = Color(160, 160, 160, 255);
        Color thumbPressedColor = Color(128, 128, 128, 255);
        Color arrowColor = Color(96, 96, 96, 255);
        Color arrowHoverColor = Color(64, 64, 64, 255);
        Color arrowPressedColor = Color(32, 32, 32, 255);
        Color borderColor = Color(200, 200, 200, 255);

        // Behavior
        bool showArrowButtons = true;
        bool autoHide = false;
        float scrollSpeed = 20.0f;
        bool smoothScrolling = true;

        static ScrollbarAppearance Default();
        static ScrollbarAppearance Modern();
        static ScrollbarAppearance Minimal();
    };

// ===== SCROLLBAR STATE =====
    struct ScrollbarState {
        float position = 0.0f;           // Current scroll position (0.0 to maxPosition)
        float maxPosition = 0.0f;        // Maximum scroll position
        float viewportSize = 100.0f;     // Size of visible area
        float contentSize = 100.0f;      // Total size of scrollable content

        // Interactive state
        bool isDragging = false;
        bool isHovered = false;
        bool thumbHovered = false;
        bool thumbPressed = false;
        bool upArrowHovered = false;
        bool upArrowPressed = false;
        bool downArrowHovered = false;
        bool downArrowPressed = false;

        // Drag tracking
        float dragStartPosition = 0.0f;
        float dragStartScrollPosition = 0.0f;

        void UpdateMaxPosition() {
            maxPosition = std::max(0.0f, contentSize - viewportSize);
            position = std::min(position, maxPosition);
        }

        float GetThumbRatio() const {
            if (contentSize <= 0.0f) return 1.0f;
            return std::min(1.0f, viewportSize / contentSize);
        }

        float GetScrollRatio() const {
            if (maxPosition <= 0.0f) return 0.0f;
            return position / maxPosition;
        }
    };

// ===== MAIN SCROLLBAR CLASS =====
    class UltraCanvasScrollbar : public UltraCanvasElement {
    private:
        // ===== STANDARD PROPERTIES =====
        StandardProperties properties;

        // ===== SCROLLBAR CONFIGURATION =====
        ScrollbarOrientation orientation;
        ScrollbarAppearance appearance;
        ScrollbarState state;

        // ===== CACHED RECTANGLES =====
        Rect2D trackRect;
        Rect2D thumbRect;
        Rect2D upArrowRect;
        Rect2D downArrowRect;
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasScrollbar(const std::string& id, long uid, long x, long y, long w, long h,
                             ScrollbarOrientation orient = ScrollbarOrientation::Vertical)
                : UltraCanvasElement(id, uid, x, y, w, h),
                  properties(id, uid, x, y, w, h),
                  orientation(orient) {

            UpdateLayout();
        }

        // ===== STANDARD PROPERTIES ACCESSORS =====
        ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()

        // ===== SCROLLBAR CONFIGURATION =====
        void SetOrientation(ScrollbarOrientation orient) {
            orientation = orient;
            layoutDirty = true;
            UpdateLayout();
        }

        ScrollbarOrientation GetOrientation() const { return orientation; }

        void SetAppearance(const ScrollbarAppearance& newAppearance) {
            appearance = newAppearance;
            layoutDirty = true;
        }

        const ScrollbarAppearance& GetAppearance() const { return appearance; }

        // ===== SCROLL CONTROL =====
        void SetScrollParameters(float viewportSize, float contentSize) {
            state.viewportSize = viewportSize;
            state.contentSize = contentSize;
            state.UpdateMaxPosition();
            layoutDirty = true;
        }

        void SetScrollPosition(float position) {
            state.position = std::clamp(position, 0.0f, state.maxPosition);
            if (onScrollChanged) {
                onScrollChanged(state.position);
            }
        }

        float GetScrollPosition() const { return state.position; }
        float GetMaxScrollPosition() const { return state.maxPosition; }
        bool IsScrollable() const { return state.maxPosition > 0.0f; }

        // ===== SCROLL OPERATIONS =====
        void ScrollBy(float delta) {
            SetScrollPosition(state.position + delta);
        }

        void ScrollToTop() {
            SetScrollPosition(0.0f);
        }

        void ScrollToBottom() {
            SetScrollPosition(state.maxPosition);
        }

        void ScrollPageUp() {
            ScrollBy(-state.viewportSize * 0.9f);
        }

        void ScrollPageDown() {
            ScrollBy(state.viewportSize * 0.9f);
        }

        void ScrollLineUp() {
            ScrollBy(-appearance.scrollSpeed);
        }

        void ScrollLineDown() {
            ScrollBy(appearance.scrollSpeed);
        }

        // ===== VISIBILITY =====
        bool ShouldBeVisible() const {
            if (appearance.autoHide) {
                return IsScrollable();
            }
            return IsVisible();
        }

        // ===== RENDERING =====
        void Render() override {
            ULTRACANVAS_RENDER_SCOPE();

            if (!ShouldBeVisible()) return;

            UpdateLayout();
            DrawTrack();
            DrawThumb();
            if (appearance.showArrowButtons) {
                DrawArrowButtons();
            }
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !ShouldBeVisible()) return false;

            UpdateLayout();

            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleMouseDown(event);
                case UCEventType::MouseUp:
                    return HandleMouseUp(event);
                case UCEventType::MouseMove:
                    return HandleMouseMove(event);
                case UCEventType::MouseWheel:
                    return HandleMouseWheel(event);
                default:
                    return false;
            }
        }

        // ===== EVENT CALLBACKS =====
        std::function<void(float)> onScrollChanged;

    private:
        // ===== LAYOUT MANAGEMENT =====
        void UpdateLayout() {
            if (!layoutDirty) return;

            Rect2D bounds = GetBounds();

            if (orientation == ScrollbarOrientation::Vertical) {
                if (appearance.showArrowButtons) {
                    upArrowRect = Rect2D(bounds.x, bounds.y, bounds.width, appearance.arrowButtonSize);
                    downArrowRect = Rect2D(bounds.x, bounds.y + bounds.height - appearance.arrowButtonSize,
                                           bounds.width, appearance.arrowButtonSize);
                    trackRect = Rect2D(bounds.x, bounds.y + appearance.arrowButtonSize,
                                       bounds.width, bounds.height - 2 * appearance.arrowButtonSize);
                } else {
                    trackRect = bounds;
                    upArrowRect = Rect2D(0, 0, 0, 0);
                    downArrowRect = Rect2D(0, 0, 0, 0);
                }
            } else {
                if (appearance.showArrowButtons) {
                    upArrowRect = Rect2D(bounds.x, bounds.y, appearance.arrowButtonSize, bounds.height);
                    downArrowRect = Rect2D(bounds.x + bounds.width - appearance.arrowButtonSize, bounds.y,
                                           appearance.arrowButtonSize, bounds.height);
                    trackRect = Rect2D(bounds.x + appearance.arrowButtonSize, bounds.y,
                                       bounds.width - 2 * appearance.arrowButtonSize, bounds.height);
                } else {
                    trackRect = bounds;
                    upArrowRect = Rect2D(0, 0, 0, 0);
                    downArrowRect = Rect2D(0, 0, 0, 0);
                }
            }

            UpdateThumbRect();
            layoutDirty = false;
        }

        void UpdateThumbRect() {
            if (state.maxPosition <= 0.0f) {
                thumbRect = Rect2D(0, 0, 0, 0);
                return;
            }

            float thumbRatio = state.GetThumbRatio();
            float scrollRatio = state.GetScrollRatio();

            if (orientation == ScrollbarOrientation::Vertical) {
                float thumbHeight = std::max(appearance.thumbMinSize, trackRect.height * thumbRatio);
                float thumbY = trackRect.y + (trackRect.height - thumbHeight) * scrollRatio;
                thumbRect = Rect2D(trackRect.x, thumbY, trackRect.width, thumbHeight);
            } else {
                float thumbWidth = std::max(appearance.thumbMinSize, trackRect.width * thumbRatio);
                float thumbX = trackRect.x + (trackRect.width - thumbWidth) * scrollRatio;
                thumbRect = Rect2D(thumbX, trackRect.y, thumbWidth, trackRect.height);
            }
        }

        // ===== RENDERING HELPERS =====
        void DrawTrack() {
            SetFillColor(appearance.trackColor);
            DrawFilledRect(trackRect);

            SetStrokeColor(appearance.borderColor);
            DrawRect(trackRect);
        }

        void DrawThumb() {
            if (thumbRect.width <= 0 || thumbRect.height <= 0) return;

            Color thumbColor = appearance.thumbColor;
            if (state.thumbPressed) {
                thumbColor = appearance.thumbPressedColor;
            } else if (state.thumbHovered) {
                thumbColor = appearance.thumbHoverColor;
            }

            SetFillColor(thumbColor);
            DrawFilledRect(thumbRect);

            SetStrokeColor(appearance.borderColor);
            DrawRect(thumbRect);
        }

        void DrawArrowButtons() {
            // Draw up/left arrow button
            if (upArrowRect.width > 0 && upArrowRect.height > 0) {
                Color arrowColor = appearance.arrowColor;
                if (state.upArrowPressed) {
                    arrowColor = appearance.arrowPressedColor;
                } else if (state.upArrowHovered) {
                    arrowColor = appearance.arrowHoverColor;
                }

                SetFillColor(appearance.trackColor);
                DrawFilledRect(upArrowRect);
                SetStrokeColor(appearance.borderColor);
                DrawRect(upArrowRect);

                DrawArrowSymbol(upArrowRect, orientation == ScrollbarOrientation::Vertical ? 0 : 3, arrowColor);
            }

            // Draw down/right arrow button
            if (downArrowRect.width > 0 && downArrowRect.height > 0) {
                Color arrowColor = appearance.arrowColor;
                if (state.downArrowPressed) {
                    arrowColor = appearance.arrowPressedColor;
                } else if (state.downArrowHovered) {
                    arrowColor = appearance.arrowHoverColor;
                }

                SetFillColor(appearance.trackColor);
                DrawFilledRect(downArrowRect);
                SetStrokeColor(appearance.borderColor);
                DrawRect(downArrowRect);

                DrawArrowSymbol(downArrowRect, orientation == ScrollbarOrientation::Vertical ? 2 : 1, arrowColor);
            }
        }

        void DrawArrowSymbol(const Rect2D& rect, int direction, const Color& color) {
            // direction: 0=up, 1=right, 2=down, 3=left
            SetStrokeColor(color);

            float centerX = rect.x + rect.width / 2.0f;
            float centerY = rect.y + rect.height / 2.0f;
            float size = std::min(rect.width, rect.height) * 0.3f;

            Point2D p1, p2, p3;

            switch (direction) {
                case 0: // Up arrow
                    p1 = Point2D(centerX, centerY - size/2);
                    p2 = Point2D(centerX - size/2, centerY + size/2);
                    p3 = Point2D(centerX + size/2, centerY + size/2);
                    break;
                case 1: // Right arrow
                    p1 = Point2D(centerX + size/2, centerY);
                    p2 = Point2D(centerX - size/2, centerY - size/2);
                    p3 = Point2D(centerX - size/2, centerY + size/2);
                    break;
                case 2: // Down arrow
                    p1 = Point2D(centerX, centerY + size/2);
                    p2 = Point2D(centerX - size/2, centerY - size/2);
                    p3 = Point2D(centerX + size/2, centerY - size/2);
                    break;
                case 3: // Left arrow
                    p1 = Point2D(centerX - size/2, centerY);
                    p2 = Point2D(centerX + size/2, centerY - size/2);
                    p3 = Point2D(centerX + size/2, centerY + size/2);
                    break;
            }

            DrawLine(p1, p2);
            DrawLine(p2, p3);
            DrawLine(p3, p1);
        }

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event) {
            Point2D mousePos(event.x, event.y);

            if (appearance.showArrowButtons && upArrowRect.Contains(mousePos)) {
                state.upArrowPressed = true;
                ScrollLineUp();
                return true;
            }

            if (appearance.showArrowButtons && downArrowRect.Contains(mousePos)) {
                state.downArrowPressed = true;
                ScrollLineDown();
                return true;
            }

            if (thumbRect.Contains(mousePos)) {
                state.isDragging = true;
                state.thumbPressed = true;
                state.dragStartPosition = (orientation == ScrollbarOrientation::Vertical) ? event.y : event.x;
                state.dragStartScrollPosition = state.position;
                return true;
            }

            if (trackRect.Contains(mousePos)) {
                // Click on track - jump to position
                float clickRatio;
                if (orientation == ScrollbarOrientation::Vertical) {
                    clickRatio = (event.y - trackRect.y) / trackRect.height;
                } else {
                    clickRatio = (event.x - trackRect.x) / trackRect.width;
                }

                SetScrollPosition(clickRatio * state.maxPosition);
                return true;
            }

            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            bool wasHandling = state.isDragging || state.upArrowPressed || state.downArrowPressed || state.thumbPressed;

            state.isDragging = false;
            state.thumbPressed = false;
            state.upArrowPressed = false;
            state.downArrowPressed = false;

            return wasHandling;
        }

        bool HandleMouseMove(const UCEvent& event) {
            Point2D mousePos(event.x, event.y);

            // Update hover states
            state.thumbHovered = thumbRect.Contains(mousePos);
            state.upArrowHovered = upArrowRect.Contains(mousePos);
            state.downArrowHovered = downArrowRect.Contains(mousePos);

            // Handle dragging
            if (state.isDragging) {
                float currentPos = (orientation == ScrollbarOrientation::Vertical) ? event.y : event.x;
                float delta = currentPos - state.dragStartPosition;

                float trackSize = (orientation == ScrollbarOrientation::Vertical) ? trackRect.height : trackRect.width;
                float thumbSize = (orientation == ScrollbarOrientation::Vertical) ? thumbRect.height : thumbRect.width;

                if (trackSize > thumbSize) {
                    float scrollDelta = (delta / (trackSize - thumbSize)) * state.maxPosition;
                    SetScrollPosition(state.dragStartScrollPosition + scrollDelta);
                }

                return true;
            }

            return GetBounds().Contains(mousePos);
        }

        bool HandleMouseWheel(const UCEvent& event) {
            if (GetBounds().Contains(Point2D(event.x, event.y))) {
                float scrollAmount = event.wheelDelta * appearance.scrollSpeed;

                if (orientation == ScrollbarOrientation::Vertical) {
                    ScrollBy(-scrollAmount); // Invert for natural scrolling
                } else {
                    ScrollBy(scrollAmount);
                }

                return true;
            }

            return false;
        }
    };

// ===== PREDEFINED APPEARANCES =====
    inline ScrollbarAppearance ScrollbarAppearance::Default() {
        return ScrollbarAppearance{};
    }

    inline ScrollbarAppearance ScrollbarAppearance::Modern() {
        ScrollbarAppearance style;
        style.trackWidth = 12.0f;
        style.showArrowButtons = false;
        style.trackColor = Color(248, 248, 248, 255);
        style.thumbColor = Color(160, 160, 160, 255);
        style.thumbHoverColor = Color(128, 128, 128, 255);
        style.thumbPressedColor = Color(96, 96, 96, 255);
        style.autoHide = true;
        return style;
    }

    inline ScrollbarAppearance ScrollbarAppearance::Minimal() {
        ScrollbarAppearance style;
        style.trackWidth = 8.0f;
        style.showArrowButtons = false;
        style.trackColor = Color(250, 250, 250, 255);
        style.thumbColor = Color(180, 180, 180, 255);
        style.thumbHoverColor = Color(140, 140, 140, 255);
        style.thumbPressedColor = Color(100, 100, 100, 255);
        style.autoHide = true;
        style.smoothScrolling = true;
        return style;
    }

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasScrollbar> CreateVerticalScrollbar(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasScrollbar>(id, uid, x, y, w, h, ScrollbarOrientation::Vertical);
    }

    inline std::shared_ptr<UltraCanvasScrollbar> CreateHorizontalScrollbar(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasScrollbar>(id, uid, x, y, w, h, ScrollbarOrientation::Horizontal);
    }

} // namespace UltraCanvas