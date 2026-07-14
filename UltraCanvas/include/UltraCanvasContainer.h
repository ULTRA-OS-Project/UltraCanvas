// include/UltraCanvasContainer.h
// Container component with scrollbars and child element management.
// Children storage lives in CSSLayout::Element (inherited via UltraCanvasUIElement);
// this class provides typed UI accessors over that storage.
// Version: 4.1.0
// Last Modified: 2026-05-29
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasSpacer.h"
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

// ===== CONTAINER STYLES =====
    struct ContainerStyle {
        // Scrolling behavior
        bool autoShowScrollbars = true;
        bool forceShowVerticalScrollbar = false;
        bool forceShowHorizontalScrollbar = false;

        ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::Default());
    };


    class UltraCanvasContainer : public UltraCanvasUIElement {
    private:
        // Content area management
        bool internalLayoutValid = false;

        // Callbacks
        std::function<void(int, int)> onScrollChanged;
        std::function<void(UltraCanvasUIElement *)> onChildAdded;
        std::function<void(UltraCanvasUIElement *)> onChildRemoved;
    protected:
        // Scrollbar components (using new unified scrollbar)
        std::unique_ptr<UltraCanvasScrollbar> verticalScrollbar;
        std::unique_ptr<UltraCanvasScrollbar> horizontalScrollbar;

        ContainerStyle style;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasContainer(const std::string &id, float x, float y, float w, float h)
                : UltraCanvasUIElement(id, x, y, w, h) {
            CreateScrollbars();
        }

        UltraCanvasContainer(const std::string &id, float w, float h)
                : UltraCanvasContainer(id, -1, -1, w, h) {}

        explicit UltraCanvasContainer(const std::string &id)
                : UltraCanvasContainer(id, -1, -1, -1, -1) {}

        virtual ~UltraCanvasContainer();

        // ===== CHILD MANAGEMENT =====
        void AddChild(std::shared_ptr<UltraCanvasUIElement> child);
        void RemoveChild(std::shared_ptr<UltraCanvasUIElement> child);
        void ClearChildren();
        bool HasChild(UltraCanvasUIElement* elem);

        // Materializes a typed view over the engine's children vector.
        // One allocation per call; intended for occasional iteration and
        // legacy code. Performance-sensitive code should iterate
        // CSSLayout::Element::Children() directly with static_pointer_cast.
        std::vector<std::shared_ptr<UltraCanvasUIElement>> GetChildren() const {
            const auto& src = Children();
            std::vector<std::shared_ptr<UltraCanvasUIElement>> out;
            out.reserve(src.size());
            for (auto& c : src) {
                out.push_back(std::static_pointer_cast<UltraCanvasUIElement>(c));
            }
            return out;
        }

        size_t GetChildCount() const { return Children().size(); }

        UltraCanvasUIElement *FindChildById(const std::string &id);
        UltraCanvasUIElement *FindElementAtPoint(const Point2Df &pos);

        // ===== SPACERS (replace old AddSpacing / AddStretch) =====
        // Fixed-size spacer (use as inline gap between specific children).
        std::shared_ptr<UltraCanvasSpacer> AddSpacer(float size);
        // Zero-size spacer with flex-grow; absorbs slack on the main axis.
        std::shared_ptr<UltraCanvasSpacer> AddStretchSpacer(float grow = 1.0f);

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

        void SetBounds(const Rect2Df &bounds) override;

        Rect2Di GetContentArea(); // zero based rectangle without container offset

        static Rect2Di GetChildRect(UltraCanvasUIElement& elem); // get rect of child element in parent coordinates

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
        void InvalidateLayout() override {
            CSSLayout::Element::InvalidateLayout();
            internalLayoutValid = false;
            RequestRedraw();
        }

        // ===== OVERRIDDEN ELEMENT METHODS =====
        void Render(IRenderContext *ctx, const Rect2Df& dirtyRect) override;

        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

        bool OnEvent(const UCEvent &event) override;

        virtual void SetWindow(UltraCanvasWindowBase *win) override;

    private:
        // ===== INTERNAL METHODS =====
        void UpdateScrollability();
        void UpdateContentSize();

        // Event handling helpers
        bool HandleScrollbarEvents(const UCEvent &event);
        bool HandleScrollWheel(const UCEvent &event);

        // Scrolling helpers
        void OnScrollChanged();

        // ===== SCROLLBAR CREATION =====
        void CreateScrollbars();
        void ApplyStyleToScrollbars();

        // ===== RENDERING HELPERS =====
        void RenderScrollbars(IRenderContext *ctx, const Rect2Df& dirtyRect);
        void RenderCorner(IRenderContext *ctx);

        void SortChildrenByZOrder();
    };

// ===== ENHANCED FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasContainer> CreateContainer(
            const std::string& id, float x = 0, float y = 0, float w = 0, float h = 0) {
        return std::make_shared<UltraCanvasContainer>(id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasContainer> CreateScrollableContainer(
            const std::string& id, float x, float y, float w, float h,
            bool enableVertical = true, bool enableHorizontal = false) {
        auto container = std::make_shared<UltraCanvasContainer>(id, x, y, w, h);

        ContainerStyle style = container->GetContainerStyle();
        style.forceShowVerticalScrollbar = enableVertical;
        style.forceShowHorizontalScrollbar = enableHorizontal;
        style.autoShowScrollbars = true;
        container->SetContainerStyle(style);

        return container;
    }

} // namespace UltraCanvas
