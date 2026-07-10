// include/UltraCanvasScrollbar.h
// Standalone scrollbar UI control with full interaction support
// Version: 2.1.0
// Last Modified: 2026-05-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== SCROLLBAR ORIENTATION =====
    enum class ScrollbarOrientation {
        Vertical,
        Horizontal
    };

// ===== SCROLLBAR STYLE CONFIGURATION =====
    struct ScrollbarStyle {
        // Dimensions
        int trackSize = 16;
        int thumbMinSize = 20;
        int arrowButtonSize = 0;  // 0 = no arrow buttons

        // Track colors
        Color trackColor = Color(240, 240, 240, 255);
        Color trackHoverColor = Color(235, 235, 235, 255);
        Color trackBorderColor = Color(220, 220, 220, 255);

        // Thumb colors
        Color thumbColor = Color(192, 192, 192, 255);
        Color thumbHoverColor = Color(160, 160, 160, 255);
        Color thumbPressedColor = Color(128, 128, 128, 255);
        Color thumbBorderColor = Color(170, 170, 170, 255);

        // Arrow button colors (if enabled)
        Color arrowColor = Color(96, 96, 96, 255);
        Color arrowHoverColor = Color(64, 64, 64, 255);
        Color arrowPressedColor = Color(32, 32, 32, 255);
        Color arrowBackgroundColor = Color(240, 240, 240, 255);
        Color arrowBackgroundHoverColor = Color(220, 220, 220, 255);

        // Appearance options
        int thumbCornerRadius = 0;
        int trackCornerRadius = 0;
        bool showTrackBorder = false;
        bool showThumbBorder = false;

        // Custom handle (thumb) image. When thumbImagePath is non-empty the thumb is
        // drawn from this image (PNG or SVG) instead of the solid rounded rectangle,
        // scaled into the thumb rect using thumbImageFit. Leave empty for the default
        // solid thumb. SVG handles scale crisply; use Fill to stretch to the thumb.
        std::string thumbImagePath;
        // Optional orientation-specific handle image used for horizontal scrollbars.
        // When set it overrides thumbImagePath for horizontal bars, so a single style
        // can carry a vertical grip (thumbImagePath) and a matching horizontal grip
        // here. If left empty, horizontal bars fall back to thumbImagePath.
        std::string thumbImagePathHorizontal;
        ImageFitMode thumbImageFit = ImageFitMode::Fill;

        // Behavior
        bool autoHide = false;
        int scrollSpeed = 20;       // Pixels per scroll step
        int pageScrollRatio = 90;   // Percentage of viewport for page scroll
        bool smoothScrolling = false;
        int smoothScrollDuration = 150;  // milliseconds

        // ===== PRESET STYLES =====
        static ScrollbarStyle Default() {
            return ScrollbarStyle();
        }

        static ScrollbarStyle Modern() {
            ScrollbarStyle style;
            style.trackSize = 12;
            style.thumbMinSize = 30;
            style.arrowButtonSize = 0;
            style.trackColor = Color(245, 245, 245, 255);
            style.thumbColor = Color(180, 180, 180, 255);
            style.thumbHoverColor = Color(150, 150, 150, 255);
            style.thumbPressedColor = Color(120, 120, 120, 255);
            style.thumbCornerRadius = 6;
            style.trackCornerRadius = 6;
            style.autoHide = true;
            return style;
        }

        static ScrollbarStyle Minimal() {
            ScrollbarStyle style;
            style.trackSize = 8;
            style.thumbMinSize = 20;
            style.arrowButtonSize = 0;
            style.trackColor = Color(250, 250, 250, 200);
            style.thumbColor = Color(160, 160, 160, 200);
            style.thumbHoverColor = Color(130, 130, 130, 220);
            style.thumbPressedColor = Color(100, 100, 100, 255);
            style.thumbCornerRadius = 4;
            style.trackCornerRadius = 4;
            style.autoHide = true;
            return style;
        }

        static ScrollbarStyle Classic() {
            ScrollbarStyle style;
            style.trackSize = 16;
            style.thumbMinSize = 20;
            style.arrowButtonSize = 17;
            style.showTrackBorder = true;
            style.showThumbBorder = true;
            return style;
        }

        static ScrollbarStyle DropDown() {
            ScrollbarStyle style;
            style.trackSize = 10;
            style.thumbMinSize = 20;
            style.arrowButtonSize = 0;
            style.trackColor = Color(250, 250, 250, 200);
            style.thumbColor = Color(160, 160, 160, 200);
            style.thumbHoverColor = Color(130, 130, 130, 220);
            style.thumbPressedColor = Color(100, 100, 100, 255);
            style.thumbCornerRadius = 5;
            style.trackCornerRadius = 4;
            style.autoHide = false;
            return style;
        }
    };

// ===== APPLICATION-WIDE DEFAULT SCROLLBAR STYLE =====
// An application may set a single default scrollbar style that every element
// owning a scrollbar (containers, tree/list views, menus, dropdowns, ...) adopts
// for a consistent app-wide look. Element style structs initialise their
// scrollbarStyle from GetDefaultScrollbarStyleOr(<their own default>), so the
// default is only applied to elements and never to standalone UltraCanvasScrollbar
// instances created with an explicit style (e.g. the scrollbar showcase page).
// No default is set until an app calls SetDefaultScrollbarStyle, so framework
// behaviour is unchanged for callers that never opt in.
    void SetDefaultScrollbarStyle(const ScrollbarStyle& style);
    void ClearDefaultScrollbarStyle();
    bool HasDefaultScrollbarStyle();
    // Returns the app-wide default style if one has been set, otherwise fallback.
    ScrollbarStyle GetDefaultScrollbarStyleOr(const ScrollbarStyle& fallback);

// ===== SCROLLBAR INTERACTION STATE =====
    struct ScrollbarInteractionState {
        // Hover states
        bool trackHovered = false;
        bool thumbHovered = false;
        bool upArrowHovered = false;
        bool downArrowHovered = false;

        // Press states
        bool thumbPressed = false;
        bool upArrowPressed = false;
        bool downArrowPressed = false;
        bool trackPressed = false;

        // Drag tracking
        bool isDragging = false;
        int dragStartMousePos = 0;
        int dragStartScrollPos = 0;

        // Animation state (for smooth scrolling)
        bool isAnimating = false;
        int animationTargetPos = 0;
        int animationStartPos = 0;
        float animationProgress = 0.0f;

        void Reset() {
            trackHovered = thumbHovered = upArrowHovered = downArrowHovered = false;
            thumbPressed = upArrowPressed = downArrowPressed = trackPressed = false;
            isDragging = false;
        }
    };

// ===== SCROLLBAR SCROLL STATE =====
    struct ScrollbarScrollState {
        int position = 0;           // Current scroll position (0 to maxPosition)
        int maxPosition = 0;        // Maximum scroll position
        int viewportSize = 100;     // Size of visible area
        int contentSize = 100;      // Total size of scrollable content

        void UpdateMaxPosition();

        double GetThumbRatio() const {
            if (contentSize <= 0) return 1.0f;
            return std::min(1.0, static_cast<double>(viewportSize) / static_cast<double>(contentSize));
        }

        double GetScrollRatio() const {
            if (maxPosition <= 0) return 0.0f;
            return static_cast<double>(position) / static_cast<double>(maxPosition);
        }

        bool IsScrollable() const {
            return maxPosition > 0;
        }
    };

// ===== MAIN SCROLLBAR CLASS =====
    class UltraCanvasScrollbar : public UltraCanvasUIElement {
    public:
        // ===== CALLBACK =====
        std::function<void(int)> onScrollChange;

    private:
        // Configuration
        ScrollbarOrientation orientation;
        ScrollbarStyle style;

        // State
        ScrollbarScrollState scrollState;
        ScrollbarInteractionState interactionState;

        // Cached rectangles
        Rect2Di trackRect;
        Rect2Di thumbRect;
        Rect2Di upArrowRect;
        Rect2Di downArrowRect;
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasScrollbar(const std::string& id, float x, float y, float w, float h,
                             ScrollbarOrientation orient = ScrollbarOrientation::Vertical);

        UltraCanvasScrollbar(const std::string& id, float w, float h,
                             ScrollbarOrientation orient = ScrollbarOrientation::Vertical)
            : UltraCanvasScrollbar(id, -1, -1, w, h, orient) {}

        UltraCanvasScrollbar(const std::string& id,
                             ScrollbarOrientation orient = ScrollbarOrientation::Vertical)
            : UltraCanvasScrollbar(id, -1, -1, -1, -1, orient) {}

        explicit UltraCanvasScrollbar(ScrollbarOrientation orient = ScrollbarOrientation::Vertical)
            : UltraCanvasScrollbar("", -1, -1, -1, -1, orient) {}

        virtual ~UltraCanvasScrollbar() = default;

        // ===== ORIENTATION =====
        void SetOrientation(ScrollbarOrientation orient);

        ScrollbarOrientation GetOrientation() const { return orientation; }

        bool IsVertical() const { return orientation == ScrollbarOrientation::Vertical; }
        bool IsHorizontal() const { return orientation == ScrollbarOrientation::Horizontal; }

        // ===== STYLE =====
        void SetStyle(const ScrollbarStyle& newStyle);

        const ScrollbarStyle& GetStyle() const { return style; }
        ScrollbarStyle& GetStyleRef() { return style; }

        // ===== SCROLL PARAMETERS =====
        void SetScrollDimensions(int viewportSize, int contentSize);
        void SetViewportSize(int size);
        void SetContentSize(int size);

        int GetViewportSize() const { return scrollState.viewportSize; }
        int GetContentSize() const { return scrollState.contentSize; }

        // ===== SCROLL POSITION =====
        bool SetScrollPosition(int position);

        int GetScrollPosition() const { return scrollState.position; }
        // Scroll percentage (0.0 to 1.0)
        int GetScrollPositionPercent() const { return scrollState.maxPosition > 0 ?
                                                                         (float)scrollState.position / (float)scrollState.maxPosition : 0; }
        int GetMaxScrollPosition() const { return scrollState.maxPosition; }

        // ===== SCROLL OPERATIONS =====
        bool ScrollBy(int delta) {
            return SetScrollPosition(scrollState.position + delta);
        }

        bool ScrollToTop() {
            return SetScrollPosition(0);
        }

        bool ScrollToBottom() {
            return SetScrollPosition(scrollState.maxPosition);
        }

        bool ScrollToStart() {
            return SetScrollPosition(0);
        }

        bool ScrollToEnd() {
            return SetScrollPosition(scrollState.maxPosition);
        }

        bool ScrollLineUp() {
            return ScrollBy(-style.scrollSpeed);
        }

        bool ScrollLineDown() {
            return ScrollBy(style.scrollSpeed);
        }

        bool ScrollPageUp() {
            int pageAmount = (scrollState.viewportSize * style.pageScrollRatio) / 100;
            return ScrollBy(-std::max(1, pageAmount));
        }

        bool ScrollPageDown() {
            int pageAmount = (scrollState.viewportSize * style.pageScrollRatio) / 100;
            return ScrollBy(std::max(1, pageAmount));
        }

        bool ScrollByWheel(int delta);

        // ===== SCROLLABILITY =====
        bool IsScrollable() const {
            return scrollState.IsScrollable();
        }

        bool ShouldBeVisible() const {
            if (style.autoHide) {
                return IsScrollable() && IsVisible();
            }
            return IsVisible();
        }

        // ===== STATE ACCESS =====
        const ScrollbarScrollState& GetScrollState() const { return scrollState; }
        const ScrollbarInteractionState& GetInteractionState() const { return interactionState; }
        bool IsDragging() const { return interactionState.isDragging; }

        // ===== RECT ACCESS (for external positioning) =====
        Rect2Di GetTrackRect() const { return trackRect; }
        Rect2Di GetThumbRect() const { return thumbRect; }

        void SetBounds(const Rect2Df& b) override;
        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;
        bool HandleMouseWheel(const UCEvent& event);

    private:
        // ===== LAYOUT =====
        void UpdateVerticalLayout(const Rect2Di& bounds);
        void UpdateHorizontalLayout(const Rect2Di& bounds);
        void UpdateThumbRect();

        // ===== RENDERING HELPERS =====
        void RenderTrack(IRenderContext* ctx);
        void RenderThumb(IRenderContext* ctx);
        void RenderArrowButton(IRenderContext* ctx, bool isUpOrLeft);

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseLeave(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasScrollbar> CreateScrollbar(
            const std::string& id, float x, float y, float w, float h,
            ScrollbarOrientation orientation = ScrollbarOrientation::Vertical) {
        return std::make_shared<UltraCanvasScrollbar>(id, x, y, w, h, orientation);
    }

    inline std::shared_ptr<UltraCanvasScrollbar> CreateVerticalScrollbar(
            const std::string& id, float x, float y, float width, float height) {
        return std::make_shared<UltraCanvasScrollbar>(id, x, y, width, height,
                                                      ScrollbarOrientation::Vertical);
    }

    inline std::shared_ptr<UltraCanvasScrollbar> CreateHorizontalScrollbar(
            const std::string& id, float x, float y, float width, float height) {
        return std::make_shared<UltraCanvasScrollbar>(id, x, y, width, height,
                                                      ScrollbarOrientation::Horizontal);
    }

} // namespace UltraCanvas
