// UltraCanvasContainer.h
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderInterface.h"
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

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
        bool autoHideScrollbars = true;
        bool smoothScrolling = true;
        float scrollSpeed = 20.0f;
        bool enableVerticalScrolling = true;
        bool enableHorizontalScrolling = true;
    };

// ===== ENHANCED SCROLL STATE =====
    struct ScrollState {
        float verticalPosition = 0.0f;      // Current vertical scroll position
        float horizontalPosition = 0.0f;    // Current horizontal scroll position

        float maxVerticalScroll = 0.0f;     // Maximum vertical scroll
        float maxHorizontalScroll = 0.0f;   // Maximum horizontal scroll

        bool showVerticalScrollbar = false;
        bool showHorizontalScrollbar = false;

        // Enhanced drag state for scrollbar interaction
        bool draggingVertical = false;
        bool draggingHorizontal = false;
        float dragStartPosition = 0.0f;
        float dragStartScroll = 0.0f;
        Point2D dragStartMouse = Point2D(0, 0);

        // Enhanced hover state for visual feedback
        bool hoveringVerticalScrollbar = false;
        bool hoveringHorizontalScrollbar = false;
        bool hoveringVerticalThumb = false;
        bool hoveringHorizontalThumb = false;

        // Smooth scrolling animation
        float targetVerticalPosition = 0.0f;
        float targetHorizontalPosition = 0.0f;
        bool animatingScroll = false;
        float scrollAnimationSpeed = 8.0f;

        // Content size tracking
        float contentWidth = 0.0f;
        float contentHeight = 0.0f;
    };

// ===== ENHANCED CONTAINER CLASS =====
    class UltraCanvasContainer : public UltraCanvasElement {
    private:
        std::vector<std::shared_ptr<UltraCanvasElement>> children;
        ContainerStyle style;
        ScrollState scrollState;

        // Enhanced scrollbar rectangles
        Rect2D verticalScrollbarRect;
        Rect2D horizontalScrollbarRect;
        Rect2D verticalThumbRect;
        Rect2D horizontalThumbRect;

        // Content area management
        Rect2D contentArea;
        bool layoutDirty = true;

        // Callbacks
        std::function<void(float, float)> onScrollChanged;
        std::function<void(UltraCanvasElement*)> onChildAdded;
        std::function<void(UltraCanvasElement*)> onChildRemoved;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasElement(id, uid, x, y, w, h) {
            UpdateLayout();
            UpdateScrollability();
        }

        virtual ~UltraCanvasContainer() = default;

        // ===== ENHANCED CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasElement> child);
        void RemoveChild(std::shared_ptr<UltraCanvasElement> child);
        void ClearChildren();
        const std::vector<std::shared_ptr<UltraCanvasElement>>& GetChildren() const { return children; }
        size_t GetChildCount() const { return children.size(); }
        UltraCanvasElement* FindChildById(const std::string& id);

        virtual long GetXInWindow() override;
        virtual long GetYInWindow() override;

        // ===== ENHANCED SCROLLING FUNCTIONS =====
        void ScrollVertical(float delta) {
            if (!style.enableVerticalScrolling) return;

            if (style.smoothScrolling) {
                scrollState.targetVerticalPosition += delta;
                scrollState.targetVerticalPosition = std::clamp(
                        scrollState.targetVerticalPosition,
                        0.0f,
                        scrollState.maxVerticalScroll
                );
                scrollState.animatingScroll = true;
            } else {
                float newPosition = scrollState.verticalPosition + delta;
                SetVerticalScrollPosition(newPosition);
            }
        }

        void ScrollHorizontal(float delta) {
            if (!style.enableHorizontalScrolling) return;

            if (style.smoothScrolling) {
                scrollState.targetHorizontalPosition += delta;
                scrollState.targetHorizontalPosition = std::clamp(
                        scrollState.targetHorizontalPosition,
                        0.0f,
                        scrollState.maxHorizontalScroll
                );
                scrollState.animatingScroll = true;
            } else {
                float newPosition = scrollState.horizontalPosition + delta;
                SetHorizontalScrollPosition(newPosition);
            }
        }

        void SetVerticalScrollPosition(float position) {
            float oldPosition = scrollState.verticalPosition;
            scrollState.verticalPosition = std::clamp(position, 0.0f, scrollState.maxVerticalScroll);
            scrollState.targetVerticalPosition = scrollState.verticalPosition;

            if (oldPosition != scrollState.verticalPosition) {
                OnScrollChanged();
            }
        }

        void SetHorizontalScrollPosition(float position) {
            float oldPosition = scrollState.horizontalPosition;
            scrollState.horizontalPosition = std::clamp(position, 0.0f, scrollState.maxHorizontalScroll);
            scrollState.targetHorizontalPosition = scrollState.horizontalPosition;

            if (oldPosition != scrollState.horizontalPosition) {
                OnScrollChanged();
            }
        }

        // Enhanced scroll position queries
        float GetVerticalScrollPosition() const { return scrollState.verticalPosition; }
        float GetHorizontalScrollPosition() const { return scrollState.horizontalPosition; }
        float GetMaxVerticalScroll() const { return scrollState.maxVerticalScroll; }
        float GetMaxHorizontalScroll() const { return scrollState.maxHorizontalScroll; }

        // Scroll percentage (0.0 to 1.0)
        float GetVerticalScrollPercent() const {
            return scrollState.maxVerticalScroll > 0 ?
                   scrollState.verticalPosition / scrollState.maxVerticalScroll : 0.0f;
        }

        float GetHorizontalScrollPercent() const {
            return scrollState.maxHorizontalScroll > 0 ?
                   scrollState.horizontalPosition / scrollState.maxHorizontalScroll : 0.0f;
        }

        // ===== ENHANCED SCROLLBAR VISIBILITY =====
        void SetShowVerticalScrollbar(bool show) {
            style.autoHideScrollbars = false;
            scrollState.showVerticalScrollbar = show;
            UpdateLayout();
        }

        void SetShowHorizontalScrollbar(bool show) {
            style.autoHideScrollbars = false;
            scrollState.showHorizontalScrollbar = show;
            UpdateLayout();
        }

        bool HasVerticalScrollbar() const { return scrollState.showVerticalScrollbar; }
        bool HasHorizontalScrollbar() const { return scrollState.showHorizontalScrollbar; }

        // ===== ENHANCED STYLE MANAGEMENT =====
        void SetContainerStyle(const ContainerStyle& newStyle) {
            style = newStyle;
            UpdateScrollbarAppearance();
            UpdateLayout();
        }

        const ContainerStyle& GetContainerStyle() const { return style; }

        // ===== ENHANCED EVENT CALLBACKS =====
        void SetScrollChangedCallback(std::function<void(float, float)> callback) {
            onScrollChanged = callback;
        }

        void SetChildAddedCallback(std::function<void(UltraCanvasElement*)> callback) {
            onChildAdded = callback;
        }

        void SetChildRemovedCallback(std::function<void(UltraCanvasElement*)> callback) {
            onChildRemoved = callback;
        }

        // ===== LAYOUT MANAGEMENT =====
        void UpdateLayout() {
            CalculateContentArea();
            UpdateScrollbarPositions();
            layoutDirty = false;
        }

        void MarkLayoutDirty() {
            layoutDirty = true;
        }

        bool IsLayoutDirty() const {
            return layoutDirty;
        }

        Rect2D GetContentArea() const {
            return contentArea;
        }

        // ===== OVERRIDDEN ELEMENT METHODS =====
        void Render() override;
        bool OnEvent(const UCEvent& event) override;

        virtual void SetWindow(UltraCanvasWindow* win) override;

    private:
        // ===== INTERNAL METHODS =====
        void UpdateScrollability();
        void UpdateContentSize();
        void UpdateScrollbarPositions();
        void UpdateScrollbarAppearance();
        void CalculateContentArea();
        void UpdateScrollAnimation();

        // Event handling helpers
        bool HandleScrollbarEvents(const UCEvent& event);
        bool HandleScrollWheel(const UCEvent& event);
        bool IsPointInScrollbar(const Point2D& point, bool& isVertical, bool& isThumb) const;
        void ForwardEventToChildren(const UCEvent& event);

        // Scrolling helpers
        void OnScrollChanged();
        float CalculateScrollbarThumbSize(bool vertical) const;
        float CalculateScrollbarThumbPosition(bool vertical) const;
        void RenderVerticalScrollbar();
        void RenderHorizontalScrollbar();
        void UpdateScrollbarHoverStates(const Point2D& mousePos);
    };

// ===== ENHANCED FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasContainer> CreateContainer(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasContainer>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasContainer> CreateScrollableContainer(
            const std::string& id, long uid, long x, long y, long w, long h,
            bool enableVertical = true, bool enableHorizontal = false) {
        auto container = std::make_shared<UltraCanvasContainer>(id, uid, x, y, w, h);

        ContainerStyle style = container->GetContainerStyle();
        style.enableVerticalScrolling = enableVertical;
        style.enableHorizontalScrolling = enableHorizontal;
        style.autoHideScrollbars = true;
        container->SetContainerStyle(style);

        return container;
    }

} // namespace UltraCanvas