// include/UltraCanvasContainer.h
// Container component with scrollbars and child element management - ENHANCED
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasLayout.h"
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

// ===== CONTAINER STYLES =====
    struct ContainerStyle {
        int scrollbarWidth = 16;
        Color scrollbarTrackColor = Color(240, 240, 240, 255);
        Color scrollbarThumbColor = Color(192, 192, 192, 255);
        Color scrollbarThumbHoverColor = Color(160, 160, 160, 255);
        Color scrollbarThumbPressedColor = Color(128, 128, 128, 255);

        // Scrolling behavior
        bool autoHideScrollbars = true;
        bool smoothScrolling = false;
        int scrollSpeed = 5;
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
    class UltraCanvasContainer : public UltraCanvasUIElement {
    private:
        std::vector<std::shared_ptr<UltraCanvasUIElement>> children;
        // Enhanced scrollbar rectangles
        Rect2Di verticalScrollbarRect;
        Rect2Di horizontalScrollbarRect;
        Rect2Di verticalThumbRect;
        Rect2Di horizontalThumbRect;

        // Content area management
        bool layoutDirty = true;

        // Callbacks
        std::function<void(int, int)> onScrollChanged;
        std::function<void(UltraCanvasUIElement*)> onChildAdded;
        std::function<void(UltraCanvasUIElement*)> onChildRemoved;

        UltraCanvasLayout* layout = nullptr;
    protected:
        ContainerStyle style;
        ScrollState scrollState;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string& id, long uid, long x, long y, long w, long h)
                : UltraCanvasUIElement(id, uid, x, y, w, h) {
//            UpdateLayout();
        }

        virtual ~UltraCanvasContainer();

        // ===== ENHANCED CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasUIElement> child);
        void RemoveChild(std::shared_ptr<UltraCanvasUIElement> child);
        void ClearChildren();
        const std::vector<std::shared_ptr<UltraCanvasUIElement>>& GetChildren() const { return children; }
        size_t GetChildCount() const { return children.size(); }
        UltraCanvasUIElement* FindChildById(const std::string& id);
        UltraCanvasUIElement* FindElementAtPoint(int x, int y);
        UltraCanvasUIElement* FindElementAtPoint(const Point2Di& pos) { return FindElementAtPoint(pos.x, pos.y); }
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

        Rect2Di GetVisibleChildBounds(const Rect2Di& childBounds);
        bool IsChildVisible(UltraCanvasUIElement* child);

        void SetBounds(const Rect2Di& bounds) override;
        Rect2Di GetContentArea(); // zero based rectanble without container offset

        void SetContainerStyle(const ContainerStyle& newStyle);
        const ContainerStyle& GetContainerStyle() const { return style; }

        // ===== ENHANCED EVENT CALLBACKS =====
        void SetScrollChangedCallback(std::function<void(int, int)> callback) {
            onScrollChanged = callback;
        }

        void SetChildAddedCallback(std::function<void(UltraCanvasUIElement*)> callback) {
            onChildAdded = callback;
        }

        void SetChildRemovedCallback(std::function<void(UltraCanvasUIElement*)> callback) {
            onChildRemoved = callback;
        }

        // ===== LAYOUT MANAGEMENT =====
        void InvalidateLayout() { layoutDirty = true; RequestRedraw(); }
        bool IsLayoutDirty() const { return layoutDirty; }

        // ===== OVERRIDDEN ELEMENT METHODS =====
        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;

        virtual void SetWindow(UltraCanvasWindowBase* win) override;

        void SetLayout(UltraCanvasLayout* newLayout);

        // Get layout manager (non-owning pointer)
        UltraCanvasLayout* GetLayout() const { return layout; }

        // Check if container has a layout
        bool HasLayout() const { return layout != nullptr; }

    private:
        // ===== INTERNAL METHODS =====
        void UpdateScrollability();
        void UpdateContentSize();
        void UpdateScrollbarPositions();
        void UpdateScrollAnimation();
//        void UpdateHoverStates(const UCEvent& event);

        // Event handling helpers
        bool HandleScrollbarEvents(const UCEvent& event);
        bool HandleScrollWheel(const UCEvent& event);

        // Scrolling helpers
        void OnScrollChanged();
        int CalculateScrollbarThumbSize(bool vertical);
        int CalculateScrollbarThumbPosition(bool vertical);
        void RenderVerticalScrollbar(IRenderContext *ctx);
        void RenderHorizontalScrollbar(IRenderContext *ctx);
        void UpdateScrollbarHoverStates(const Point2Di& mousePos);
    };

// ===== ENHANCED FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasContainer> CreateContainer(
            const std::string& id, long uid, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasContainer>(id, uid, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasContainer> CreateContainer(
            const std::string& id, long x = 0, long y = 0, long w = 0, long h = 0) {
        return std::make_shared<UltraCanvasContainer>(id, 0, x, y, w, h);
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