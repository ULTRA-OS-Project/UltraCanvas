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
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasLayout.h"
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

// ===== CONTAINER STYLES =====
    struct ContainerStyle {
//        int scrollbarWidth = 16;
//        Color scrollbarTrackColor = Color(240, 240, 240, 255);
//        Color scrollbarThumbColor = Color(192, 192, 192, 255);
//        Color scrollbarThumbHoverColor = Color(160, 160, 160, 255);
//        Color scrollbarThumbPressedColor = Color(128, 128, 128, 255);

        // Scrolling behavior
        bool autoShowScrollbars = true;
//        bool smoothScrolling = false;
//        int scrollSpeed = 5;
        bool forceShowVerticalScrollbar = false;
        bool forceShowHorizontalScrollbar = false;

        ScrollbarStyle scrollbarStyle;
    };


    class UltraCanvasContainer : public UltraCanvasUIElement {
    private:
        std::vector<std::shared_ptr<UltraCanvasUIElement>> children;

        // Scrollbar components (using new unified scrollbar)
        std::unique_ptr<UltraCanvasScrollbar> verticalScrollbar;
        std::unique_ptr<UltraCanvasScrollbar> horizontalScrollbar;
        // Corner rectangle (where scrollbars meet)
        Rect2Di scrollbarsCornerRect;

        // Content area management
        bool layoutDirty = true;

        // Callbacks
        std::function<void(int, int)> onScrollChanged;
        std::function<void(UltraCanvasUIElement *)> onChildAdded;
        std::function<void(UltraCanvasUIElement *)> onChildRemoved;

        UltraCanvasLayout *layout = nullptr;
    protected:
        ContainerStyle style;
        //ScrollState scrollState;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string &id, long uid, long x, long y, long w, long h)
                : UltraCanvasUIElement(id, uid, x, y, w, h) {
//            UpdateLayout();
            CreateScrollbars();
        }

        virtual ~UltraCanvasContainer();

        // ===== ENHANCED CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasUIElement> child);
        void RemoveChild(std::shared_ptr<UltraCanvasUIElement> child);
        void ClearChildren();
        const std::vector<std::shared_ptr<UltraCanvasUIElement>> &GetChildren() const { return children; }

        size_t GetChildCount() const { return children.size(); }

        UltraCanvasUIElement *FindChildById(const std::string &id);
        UltraCanvasUIElement *FindElementAtPoint(int x, int y);
        UltraCanvasUIElement *FindElementAtPoint(const Point2Di &pos) { return FindElementAtPoint(pos.x, pos.y); }

        void ConvertWindowToContainerCoordinates(int &x, int &y);

//        virtual int GetXInWindow() override;
//        virtual int GetYInWindow() override;

        // ===== ENHANCED SCROLLING FUNCTIONS =====
        bool ScrollByVertical(int delta);
        bool ScrollByHorizontal(int delta);
        bool ScrollToVertical(int position);
        bool ScrollToHorizontal(int position);

        // Enhanced scroll position queries
        const UltraCanvasScrollbar &GetVerticalScrollBar() const { return *verticalScrollbar.get(); }
        const UltraCanvasScrollbar &GetHorizontalScrollBar() const { return *horizontalScrollbar.get(); }

        int GetHorizontalScrollPosition() { return horizontalScrollbar->GetScrollPosition(); }
        int GetVerticalScrollPosition() { return verticalScrollbar->GetScrollPosition(); }

        // ===== ENHANCED SCROLLBAR VISIBILITY =====
        void SetShowVerticalScrollbar(bool show);
        void SetShowHorizontalScrollbar(bool show);

        Rect2Di GetVisibleChildBounds(const Rect2Di &childBounds);
        bool IsChildVisible(UltraCanvasUIElement *child);

        void SetBounds(const Rect2Di &bounds) override;

        Rect2Di GetContentArea(); // zero based rectanble without container offset

        void SetContainerStyle(const ContainerStyle &newStyle);
        const ContainerStyle &GetContainerStyle() const { return style; }

        // ===== ENHANCED EVENT CALLBACKS =====
        void SetScrollChangedCallback(std::function<void(int, int)> callback) {
            onScrollChanged = callback;
        }

        void SetChildAddedCallback(std::function<void(UltraCanvasUIElement *)> callback) {
            onChildAdded = callback;
        }

        void SetChildRemovedCallback(std::function<void(UltraCanvasUIElement *)> callback) {
            onChildRemoved = callback;
        }

        // ===== LAYOUT MANAGEMENT =====
        void InvalidateLayout() {
            layoutDirty = true;
            RequestRedraw();
        }

        bool IsLayoutDirty() const { return layoutDirty; }

        // ===== OVERRIDDEN ELEMENT METHODS =====
        void Render(IRenderContext *ctx) override;

        bool OnEvent(const UCEvent &event) override;

        virtual void SetWindow(UltraCanvasWindowBase *win) override;

        void SetLayout(UltraCanvasLayout *newLayout);

        // Get layout manager (non-owning pointer)
        UltraCanvasLayout *GetLayout() const { return layout; }

        // Check if container has a layout
        bool HasLayout() const { return layout != nullptr; }

    private:
        // ===== INTERNAL METHODS =====
        void UpdateScrollability();

        void UpdateContentSize();

        void UpdateScrollbarPositions();
//        void UpdateScrollAnimation();
//        void UpdateHoverStates(const UCEvent& event);

        // Event handling helpers
        bool HandleScrollbarEvents(const UCEvent &event);

        bool HandleScrollWheel(const UCEvent &event);

        // Scrolling helpers
        void OnScrollChanged();
        // ===== SCROLLBAR CREATION =====
        void CreateScrollbars();
        void ApplyStyleToScrollbars();

        // ===== RENDERING HELPERS =====
        void RenderScrollbars(IRenderContext *ctx);
        void RenderCorner(IRenderContext *ctx);
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
        style.forceShowVerticalScrollbar = enableVertical;
        style.forceShowHorizontalScrollbar = enableHorizontal;
        style.autoShowScrollbars = true;
        container->SetContainerStyle(style);

        return container;
    }

} // namespace UltraCanvas