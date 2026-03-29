// include/UltraCanvasListView.h
// Model-View-Delegate ListView widget
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListSelection.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

    // ===== VIEW STYLE =====

    struct ListViewStyle {
        Color backgroundColor = Colors::White;
        Color headerBackgroundColor = Color(240, 240, 240);
        Color headerTextColor = Colors::Black;
        Color gridLineColor = Color(220, 220, 220);

        int rowHeight = 24;
        int headerHeight = 26;
        bool showHeader = false;
        bool showGridLines = false;
        bool alternateRowColors = false;
        Color alternateRowColor = Color(248, 248, 248);

        Color selectionBackgroundColor = Colors::Selection;
        Color hoverBackgroundColor = Colors::SelectionHover;

        ScrollbarStyle scrollbarStyle = ScrollbarStyle::Modern();
    };

    // ===== LIST VIEW WIDGET =====

    class UltraCanvasListView : public UltraCanvasUIElement {
    public:
        // Callbacks
        std::function<void(int row)> onItemClicked;
        std::function<void(int row)> onItemDoubleClicked;
        std::function<void(int row)> onItemActivated;
        std::function<void(const std::vector<int>&)> onSelectionChanged;
        std::function<void(int row)> onItemHovered;

        // Constructor
        UltraCanvasListView(const std::string& identifier, long id,
                            int x, int y, int w, int h);
        virtual ~UltraCanvasListView() = default;

        // === Model / Delegate / Selection wiring ===
        void SetModel(IListModel* model);
        IListModel* GetModel() const;

        void SetDelegate(std::shared_ptr<IItemDelegate> delegate);
        IItemDelegate* GetDelegate() const;

        void SetSelection(std::shared_ptr<IListSelection> selection);
        IListSelection* GetSelection() const;

        void SetStyle(const ListViewStyle& style);
        const ListViewStyle& GetStyle() const;

        void SetRowHeight(int height);
        int GetRowHeight() const;

        void SetShowHeader(bool show);
        bool GetShowHeader() const;

        // === Scrolling ===
        void ScrollToRow(int row);
        void EnsureRowVisible(int row);

        // === Core overrides ===
        void UpdateGeometry(IRenderContext* ctx) override;
        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;
        void SetBounds(const Rect2Di& bounds) override;
        void SetWindow(UltraCanvasWindowBase* win) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // Model / Delegate / Selection
        IListModel* model = nullptr;
        std::shared_ptr<IItemDelegate> delegate;
        std::shared_ptr<IListSelection> selection;

        // View state
        ListViewStyle viewStyle;

        // Scrollbar
        std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
        int scrollOffsetY = 0;
        int maxScrollY = 0;

        // Interaction state
        int hoveredRow = -1;
        int focusedRow = -1;

        // Internal methods
        void CreateScrollbar();
        void UpdateScrollbar();
        void ClampScrollOffset();

        // Geometry
        int GetTotalContentHeight() const;
        int GetHeaderOffset() const;
        Rect2Di GetViewportRect() const;
        int GetRowAtY(int y) const;
        Rect2Di GetRowRect(int row) const;

        // Rendering
        void RenderHeader(IRenderContext* ctx, const Rect2Di& contentRect);
        void RenderRows(IRenderContext* ctx, const Rect2Di& contentRect);

        // Event handlers
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseDoubleClick(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        // Keyboard navigation
        void NavigateUp();
        void NavigateDown();
        void NavigatePageUp();
        void NavigatePageDown();
        void NavigateHome();
        void NavigateEnd();

        // Model connection
        void ConnectModelSignals();
        void DisconnectModelSignals();
    };

} // namespace UltraCanvas
