// include/UltraCanvasContainer.h
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {

// ===== CONTAINER STYLES =====
    struct ContainerStyle {
        // Scrollbar settings
        int scrollbarWidth = 16;
        Color scrollbarTrackColor = Color(240, 240, 240, 255);
        Color scrollbarThumbColor = Color(192, 192, 192, 255);
        Color scrollbarThumbHoverColor = Color(160, 160, 160, 255);
        Color scrollbarThumbPressedColor = Color(128, 128, 128, 255);

        // Border and background
        Color borderColor = Color(200, 200, 200, 255);
        Color backgroundColor = Color(255, 255, 255, 255);
        int borderWidth = 0;

        // Padding for content area
        int paddingLeft = 0;
        int paddingTop = 0;
        int paddingRight = 0;
        int paddingBottom = 0;

        // Scrolling behavior
        bool autoHideScrollbars = true;
        bool smoothScrolling = false;
        int scrollSpeed = 2;
        bool enableVerticalScrolling = true;
        bool enableHorizontalScrolling = true;
    };

// ===== ENHANCED SCROLL STATE =====
    struct ScrollState {
        int verticalPosition = 0;      // Current vertical scroll position
        int horizontalPosition = 0;    // Current horizontal scroll position

        int maxVerticalScroll = 0;     // Maximum vertical scroll
        int maxHorizontalScroll = 0;   // Maximum horizontal scroll

        bool showVerticalScrollbar = false;
        bool showHorizontalScrollbar = false;

        // Enhanced drag state for scrollbar interaction
        bool draggingVertical = false;
        bool draggingHorizontal = false;
        int dragStartPosition = 0;
        int dragStartScroll = 0;
        Point2Di dragStartMouse = Point2Di(0, 0);

        // Enhanced hover state for visual feedback
        bool hoveringVerticalScrollbar = false;
        bool hoveringHorizontalScrollbar = false;
        bool hoveringVerticalThumb = false;
        bool hoveringHorizontalThumb = false;

        // Smooth scrolling animation
        int targetVerticalPosition = 0;
        int targetHorizontalPosition = 0;
        bool animatingScroll = false;
        int scrollAnimationSpeed = 8;

        // Content size tracking
        int contentWidth = 0;
        int contentHeight = 0;
    };

// ===== ENHANCED CONTAINER CLASS =====
    class UltraCanvasContainer : public UltraCanvasElement {
    private:
        std::vector<std::shared_ptr<UltraCanvasElement>> children;
        // Enhanced scrollbar rectangles
        Rect2Di verticalScrollbarRect;
        Rect2Di horizontalScrollbarRect;
        Rect2Di verticalThumbRect;
        Rect2Di horizontalThumbRect;

        // Content area management
        Rect2Di contentArea;
        bool layoutDirty = true;

        // Callbacks
        std::function<void(int, int)> onScrollChanged;
        std::function<void(UltraCanvasElement*)> onChildAdded;
        std::function<void(UltraCanvasElement*)> onChildRemoved;

    protected:
        ContainerStyle style;
        ScrollState scrollState;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasElement(id, uid, x, y, w, h) {
            UpdateLayout();
            UpdateScrollability();
        }

        virtual ~UltraCanvasContainer() = default;

        // ===== ENHANCED CHILD MANAGEMENT =====
        void AddOrMoveChild(std::shared_ptr<UltraCanvasElement> child);
        void RemoveChild(std::shared_ptr<UltraCanvasElement> child);
        void ClearChildren();
        const std::vector<std::shared_ptr<UltraCanvasElement>>& GetChildren() const { return children; }
        size_t GetChildCount() const { return children.size(); }
        UltraCanvasElement* FindChildById(const std::string& id);
        UltraCanvasElement* FindElementAtPoint(int x, int y);
        UltraCanvasElement* FindElementAtPoint(const Point2Di& pos) { return FindElementAtPoint(pos.x, pos.y); }
        void ConvertWindowToContainerCoordinates(int &x, int &y);

//        virtual int GetXInWindow() override;
//        virtual int GetYInWindow() override;

        // ===== ENHANCED SCROLLING FUNCTIONS =====
        bool ScrollVertical(int delta);
        bool ScrollHorizontal(int delta);
        bool SetVerticalScrollPosition(int position);
        bool SetHorizontalScrollPosition(int position);

        // Enhanced scroll position queries
        int GetVerticalScrollPosition() const { return scrollState.verticalPosition; }
        int GetHorizontalScrollPosition() const { return scrollState.horizontalPosition; }
        int GetMaxVerticalScroll() const { return scrollState.maxVerticalScroll; }
        int GetMaxHorizontalScroll() const { return scrollState.maxHorizontalScroll; }

        // Scroll percentage (0.0 to 1.0)
        float GetVerticalScrollPercent() const {
            return scrollState.maxVerticalScroll > 0 ?
                   (float)scrollState.verticalPosition / (float)scrollState.maxVerticalScroll : 0;
        }

        float GetHorizontalScrollPercent() const {
            return scrollState.maxHorizontalScroll > 0 ?
                   (float)scrollState.horizontalPosition / (float)scrollState.maxHorizontalScroll : 0;
        }

        // ===== ENHANCED SCROLLBAR VISIBILITY =====
        void SetShowVerticalScrollbar(bool show);
        void SetShowHorizontalScrollbar(bool show);

        bool HasVerticalScrollbar() const { return scrollState.showVerticalScrollbar; }
        bool HasHorizontalScrollbar() const { return scrollState.showHorizontalScrollbar; }

        Rect2Di GetVisibleChildBounds(const Rect2Di& childBounds) const;
        bool IsChildVisible(UltraCanvasElement* child) const;

        // ===== ENHANCED STYLE MANAGEMENT =====
        void SetContainerStyle(const ContainerStyle& newStyle);

        const ContainerStyle& GetContainerStyle() const { return style; }

        // ===== ENHANCED EVENT CALLBACKS =====
        void SetScrollChangedCallback(std::function<void(int, int)> callback) {
            onScrollChanged = callback;
        }

        void SetChildAddedCallback(std::function<void(UltraCanvasElement*)> callback) {
            onChildAdded = callback;
        }

        void SetChildRemovedCallback(std::function<void(UltraCanvasElement*)> callback) {
            onChildRemoved = callback;
        }

        // ===== LAYOUT MANAGEMENT =====
        void UpdateLayout();
        void MarkLayoutDirty() { layoutDirty = true; }
        bool IsLayoutDirty() const { return layoutDirty; }
        Rect2Di GetContentArea();

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
//        void UpdateHoverStates(const UCEvent& event);

        // Event handling helpers
        bool HandleScrollbarEvents(const UCEvent& event);
        bool HandleScrollWheel(const UCEvent& event);
        bool IsPointInScrollbar(const Point2Di& point, bool& isVertical, bool& isThumb) const;
        void ForwardEventToChildren(const UCEvent& event);

        // Scrolling helpers
        void OnScrollChanged();
        int CalculateScrollbarThumbSize(bool vertical) const;
        int CalculateScrollbarThumbPosition(bool vertical) const;
        void RenderVerticalScrollbar(IRenderContext *ctx);
        void RenderHorizontalScrollbar(IRenderContext *ctx);
        void UpdateScrollbarHoverStates(const Point2Di& mousePos);
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