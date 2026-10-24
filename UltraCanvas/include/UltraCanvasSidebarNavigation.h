// include/UltraCanvasSidebarNavigation.h
// The navigation column of an application window: sections of items, each
// an icon, a label and an optional badge, one of them selected. Up / Down,
// Home / End and Return on the keyboard; the wheel scrolls when the list is
// taller than the element; a compact mode shows the icons alone with the
// label as a tooltip. A store's Discover / Categories / Installed / Updates,
// a mail client's folders, a settings window's pages.
//
//   auto nav = CreateSidebarNavigation("nav");
//   nav->AddItem("discover", "Discover", "media/icons/compass.svg");
//   int lib = nav->AddSection("Library");
//   nav->AddItem(lib, "installed", "Installed", "media/icons/box.svg");
//   nav->AddItem(lib, "updates", "Updates", "media/icons/arrow-up.svg");
//   nav->SetBadge("updates", 3);
//   nav->Select("discover");
//   nav->onSelectionChanged = [](const std::string& id) { ... };
//
// Self-rendered (no child widgets), like UltraCanvasToolbar and the chips.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    struct SidebarItem {
        std::string id;              // what the callbacks name; unique in the sidebar
        std::string label;
        std::string iconPath;        // SVG or image, optional
        std::string badgeText;       // "3", "new"; empty for none
        std::string tooltip;         // shown in compact mode; the label when empty
        bool enabled = true;
    };

    struct SidebarSection {
        std::string title;           // empty: no header row
        std::vector<SidebarItem> items;
    };

    struct SidebarStyle {
        Color backgroundColor      = Color(247, 247, 249, 255);
        Color textColor            = Color(40, 40, 46, 255);
        Color disabledTextColor    = Color(170, 170, 176, 255);
        Color sectionTextColor     = Color(120, 120, 128, 255);
        Color hoverBackgroundColor = Color(232, 233, 237, 255);
        Color selectedBackgroundColor = Color(222, 232, 247, 255);
        Color selectedTextColor    = Color(20, 60, 120, 255);
        Color indicatorColor       = Colors::Selection;        // the bar at the selected item's left
        Color badgeBackgroundColor = Colors::Selection;
        Color badgeTextColor       = Colors::White;
        Color focusColor           = Colors::Selection;
        float itemHeight           = 34.0f;
        float sectionHeight        = 26.0f;
        float sectionGap           = 6.0f;      // above a section header
        float paddingH             = 10.0f;     // the list's horizontal inset
        float paddingV             = 8.0f;      // the list's vertical inset
        float itemPaddingH         = 10.0f;     // inside an item row
        float iconSize             = 18.0f;
        float iconGap              = 10.0f;
        float cornerRadius         = 6.0f;
        float indicatorWidth       = 3.0f;
        float badgeHeight          = 18.0f;
        float badgeMinWidth        = 18.0f;
        float badgePaddingH        = 6.0f;
        float expandedWidth        = 220.0f;
        float compactWidth         = 56.0f;
        FontStyle itemFont;                      // 13 by default
        FontStyle sectionFont;                   // bold 11 by default
        FontStyle badgeFont;                     // bold 11 by default

        static SidebarStyle Default();
    };

    class UltraCanvasSidebarNavigation : public UltraCanvasUIElement {
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::List; }

        UltraCanvasSidebarNavigation(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasSidebarNavigation(const std::string& identifier, float w, float h)
                : UltraCanvasSidebarNavigation(identifier, -1, -1, w, h) {}
        explicit UltraCanvasSidebarNavigation(const std::string& identifier)
                : UltraCanvasSidebarNavigation(identifier, -1, -1, -1, -1) {}

        // ----- sections and items -----
        // A titled section; returns its index. Items added without a section
        // go into an untitled first section, made on demand.
        int AddSection(const std::string& title);
        int GetSectionCount() const { return static_cast<int>(sections.size()); }
        // Adds an item to a section (or to the untitled one); returns the
        // item's id. An id already present replaces that item in place.
        std::string AddItem(int sectionIndex, const SidebarItem& item);
        std::string AddItem(int sectionIndex, const std::string& id, const std::string& label,
                            const std::string& iconPath = "");
        std::string AddItem(const std::string& id, const std::string& label, const std::string& iconPath = "");
        bool RemoveItem(const std::string& id);
        void Clear();
        int  GetItemCount() const;
        const SidebarItem* FindItem(const std::string& id) const;
        std::vector<std::string> GetItemIds() const;     // in display order

        void SetBadge(const std::string& id, int count);          // 0 clears
        void SetBadgeText(const std::string& id, const std::string& text);
        void SetItemEnabled(const std::string& id, bool enabled);
        void SetItemLabel(const std::string& id, const std::string& label);
        void SetItemIcon(const std::string& id, const std::string& iconPath);
        void SetItemTooltip(const std::string& id, const std::string& tooltip);
        void SetSectionTitle(int sectionIndex, const std::string& title);

        // ----- selection -----
        // Selects an item and fires onSelectionChanged when it changed;
        // false when there is no such enabled item. Empty clears.
        bool Select(const std::string& id, bool fireCallback = true);
        const std::string& GetSelectedId() const { return selectedId; }
        bool SelectNext();          // the next enabled item (keyboard Down)
        bool SelectPrevious();      // (Up)

        // ----- compact mode -----
        // Icons only, labels as tooltips, the element style.compactWidth wide.
        void SetCompact(bool compact);
        bool IsCompact() const { return compact; }
        // The width the sidebar wants in its current mode.
        float PreferredWidth() const { return compact ? style.compactWidth : style.expandedWidth; }
        // The height of everything listed, for a parent that sizes to it.
        float ContentHeight() const;

        // ----- look -----
        void SetStyle(const SidebarStyle& newStyle);
        const SidebarStyle& GetStyle() const { return style; }
        SidebarStyle& GetStyle() { return style; }

        // ----- callbacks -----
        std::function<void(const std::string& id)> onSelectionChanged;
        std::function<void(const std::string& id)> onItemActivated;   // double-click or Return
        std::function<void(bool compact)> onCompactChanged;

        // ----- framework -----
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;

        // The row an item occupies, local and already scrolled; empty when
        // the id is unknown. For tests and for a caller that anchors a popup.
        Rect2Df ItemRect(const std::string& id) const;

    private:
        struct Row {
            int section = -1;
            int item = -1;          // -1: a section header
            Rect2Df rect;           // local, before scrolling
        };
        std::vector<Row> LayoutRows() const;
        const Row* RowAt(const Point2Df& local, const std::vector<Row>& rows) const;
        SidebarItem* Find(const std::string& id, int* sectionOut = nullptr, int* itemOut = nullptr);
        std::shared_ptr<UCImage> IconFor(const SidebarItem& item);
        void DrawItem(IRenderContext* ctx, const SidebarItem& item, const Rect2Df& r, bool selected, bool hovered);
        void DrawSection(IRenderContext* ctx, const SidebarSection& section, const Rect2Df& r);
        void DrawBadge(IRenderContext* ctx, const std::string& text, float rightX, float centerY);
        void ClampScroll();
        void EnsureVisible(const std::string& id);
        void ShowItemTooltip(const SidebarItem& item, const Rect2Df& r);
        int  UntitledSection();

        std::vector<SidebarSection> sections;
        std::string selectedId;
        std::string hoveredId;
        std::string pressedId;
        bool compact = false;
        float scrollY = 0.0f;
        SidebarStyle style;
        std::vector<std::pair<std::string, std::shared_ptr<UCImage>>> iconCache;
        std::string tooltipShownFor;
    };

    inline std::shared_ptr<UltraCanvasSidebarNavigation> CreateSidebarNavigation(const std::string& identifier,
                                                                                 float x = -1, float y = -1,
                                                                                 float w = -1, float h = -1) {
        return std::make_shared<UltraCanvasSidebarNavigation>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
