// include/UltraCanvasBreadcrumb.h
// Hierarchical breadcrumb navigation control with overflow handling and per-item dropdowns
// Version: 1.1.0
// Last Modified: 2026-05-17
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BREADCRUMB_H
#define ULTRACANVAS_BREADCRUMB_H

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasImage.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== SEPARATOR STYLES =====
    enum class BreadcrumbSeparatorStyle {
        Chevron,        // Drawn ">" shape (vector path)
        ChevronDouble,  // Drawn ">>" shape (vector path)
        Slash,          // Text "/"
        BackSlash,      // Text "\\"
        GreaterThan,    // Text ">"
        Arrow,          // Text "->"
        Bullet,         // Text "*"
        Pipe,           // Text "|"
        Dot,            // Text "."
        NoSeparator,    // No separator drawn between items
        CustomText,     // Use customSeparatorText from style
        CustomIcon      // Use customSeparatorIcon from style
    };

// ===== OVERFLOW MODES =====
    enum class BreadcrumbOverflowMode {
        Clip,           // No overflow handling, clip whatever doesn't fit
        Collapse,       // Collapse middle items into a "..." menu
        Ellipsize,      // Ellipsize individual item texts
        ShrinkText      // Reduce per-item maxTextWidth until everything fits
    };

// ===== ITEM RENDER STYLE =====
    enum class BreadcrumbItemStyle {
        Plain,          // Text only (default), background only on hover/press
        Pill,           // Each item drawn with rounded background
        Underline,      // Text with hover underline
        Tab             // Tab-like with bottom border
    };

// ===== BREADCRUMB ITEM =====
    struct BreadcrumbItem {
        std::string id;                                 // Optional unique identifier
        std::string text;                               // Display text
        std::shared_ptr<UCImage> icon;                  // Optional leading icon
        std::string tooltip;                            // Tooltip text
        bool clickable = true;                          // Whether this item responds to clicks
        bool hasDropdown = false;                       // Show dropdown chevron next to item
        std::vector<MenuItemData> dropdownItems;        // Items shown when dropdown is opened
        std::function<std::vector<MenuItemData>()> dropdownItemsProvider; // Lazy provider
        std::function<void()> onClick;                  // Per-item click callback
        void* userData = nullptr;                       // Arbitrary user data

        BreadcrumbItem() = default;
        BreadcrumbItem(const std::string& itemText) : text(itemText) {}
        BreadcrumbItem(const std::string& itemText, std::function<void()> callback)
                : text(itemText), onClick(std::move(callback)) {}
        BreadcrumbItem(const std::string& itemId, const std::string& itemText)
                : id(itemId), text(itemText) {}

        static BreadcrumbItem WithIcon(const std::string& itemText, const std::string& iconPath) {
            BreadcrumbItem item(itemText);
            if (!iconPath.empty()) item.icon = UCImage::Get(iconPath);
            return item;
        }

        static BreadcrumbItem IconOnly(const std::string& iconPath, const std::string& itemTooltip = "") {
            BreadcrumbItem item;
            if (!iconPath.empty()) item.icon = UCImage::Get(iconPath);
            item.tooltip = itemTooltip;
            return item;
        }

        static BreadcrumbItem WithDropdown(const std::string& itemText,
                                           const std::vector<MenuItemData>& items) {
            BreadcrumbItem item(itemText);
            item.hasDropdown = true;
            item.dropdownItems = items;
            return item;
        }
    };

// ===== STYLE =====
    struct BreadcrumbStyle {
        // Background of the whole breadcrumb strip
        Color backgroundColor = Colors::Transparent;
        Color borderColor = Colors::Transparent;
        float borderWidth = 0.0f;
        float cornerRadius = 0.0f;

        // Item text colors per state
        Color itemTextColor = Color(80, 80, 80, 255);
        Color itemHoverTextColor = Color(0, 90, 180, 255);
        Color itemPressedTextColor = Color(0, 60, 130, 255);
        Color disabledItemTextColor = Color(160, 160, 160, 255);

        // Current (last) item — usually emphasized, often non-clickable
        Color currentItemTextColor = Color(20, 20, 20, 255);
        bool currentItemBold = true;
        bool currentItemClickable = false;

        // Item background per state (used by Pill / Tab item styles, and on hover)
        Color itemBackgroundColor = Colors::Transparent;
        Color itemHoverBackgroundColor = Color(225, 235, 245, 255);
        Color itemPressedBackgroundColor = Color(200, 220, 240, 255);
        Color currentItemBackgroundColor = Color(235, 240, 245, 255);

        // Separator
        BreadcrumbSeparatorStyle separatorStyle = BreadcrumbSeparatorStyle::Chevron;
        Color separatorColor = Color(140, 140, 140, 255);
        float separatorThickness = 1.5f;        // For drawn chevron
        int separatorSize = 6;                  // Width of the drawn chevron
        std::string customSeparatorText = ">";  // Used when separatorStyle == CustomText
        std::shared_ptr<UCImage> customSeparatorIcon;
        int separatorSpacing = 6;               // Horizontal padding around separator

        // Item layout
        BreadcrumbItemStyle itemStyle = BreadcrumbItemStyle::Plain;
        int itemPaddingHorizontal = 6;
        int itemPaddingVertical = 3;
        int itemCornerRadius = 3;
        int iconSize = 16;
        int iconTextSpacing = 4;
        int dropdownChevronSize = 6;
        int dropdownChevronSpacing = 4;
        bool underlineOnHover = false;          // For Plain style only
        bool underlineCurrent = false;

        // Font
        FontStyle fontStyle;

        // Overflow
        BreadcrumbOverflowMode overflowMode = BreadcrumbOverflowMode::Collapse;
        std::string overflowEllipsisText = "...";
        int minVisibleAfterCollapse = 1;        // Always keep at least N trailing items visible
        bool keepFirstItemOnCollapse = true;    // Always keep first item (root) visible
        int maxItemTextWidth = 200;             // 0 = no per-item limit

        // ===== PRESETS =====
        static BreadcrumbStyle Default();
        static BreadcrumbStyle Compact();
        static BreadcrumbStyle Pills();
        static BreadcrumbStyle FileExplorer();
        static BreadcrumbStyle WebDocs();
    };

// ===== MAIN BREADCRUMB CLASS =====
    class UltraCanvasBreadcrumb : public UltraCanvasUIElement {
    public:
        // ===== EVENTS =====
        // Fired when a (non-current) item is clicked. index is the item index in the path.
        std::function<void(int index, const BreadcrumbItem& item)> onItemClicked;
        // Fired when the dropdown chevron of an item is clicked.
        std::function<void(int index, const BreadcrumbItem& item)> onItemDropdown;
        // Fired when the overflow ("...") menu is opened.
        std::function<void()> onOverflowOpened;
        // Fired when the path changes via SetPath / Add / Remove / Clear.
        std::function<void()> onPathChanged;

        // ===== CONSTRUCTORS =====
        UltraCanvasBreadcrumb(const std::string& identifier,
                              float x, float y, float w, float h);

        UltraCanvasBreadcrumb(const std::string& identifier,
                              float w, float h)
              : UltraCanvasBreadcrumb(identifier, -1, -1, w, h) {};

        explicit UltraCanvasBreadcrumb(const std::string& identifier = "")
                : UltraCanvasBreadcrumb(identifier, -1, -1, -1, -1) {};

        virtual ~UltraCanvasBreadcrumb() = default;

        // ===== ITEM MANAGEMENT =====
        void AddItem(const BreadcrumbItem& item);
        void AddItem(const std::string& text);
        void AddItem(const std::string& text, std::function<void()> onClick);
        void InsertItem(int index, const BreadcrumbItem& item);
        bool RemoveItem(int index);
        bool RemoveItemById(const std::string& id);
        // Removes items after `index` (exclusive). Useful when navigating up.
        void RemoveItemsAfter(int index);
        void Clear();

        size_t GetItemCount() const { return items.size(); }
        bool IsEmpty() const { return items.empty(); }
        BreadcrumbItem* GetItem(int index);
        const BreadcrumbItem* GetItem(int index) const;
        BreadcrumbItem* GetItemById(const std::string& id);
        int GetItemIndexById(const std::string& id) const;
        const std::vector<BreadcrumbItem>& GetItems() const { return items; }

        // ===== PATH HELPERS =====
        // Splits a path by separator and creates one item per segment.
        // Empty leading/trailing segments are skipped.
        void SetPath(const std::string& path, char separator = '/');
        // Returns reconstructed path joining item texts with `separator`.
        std::string GetPath(char separator = '/') const;
        // Replaces the entire item list.
        void SetItems(std::vector<BreadcrumbItem> newItems);

        // ===== CURRENT ITEM (the "leaf" / active position) =====
        // By default the current item is the last one. SetCurrentIndex lets a
        // caller mark a different item as current — items after it remain
        // visible but rendered as non-current style.
        void SetCurrentIndex(int index);
        int GetCurrentIndex() const;

        // ===== STYLE =====
        void SetStyle(const BreadcrumbStyle& newStyle);
        const BreadcrumbStyle& GetStyle() const { return style; }
        BreadcrumbStyle& GetStyle() { return style; }

        void SetSeparatorStyle(BreadcrumbSeparatorStyle s);
        void SetCustomSeparatorText(const std::string& text);
        void SetCustomSeparatorIcon(const std::string& iconPath);
        void SetItemStyle(BreadcrumbItemStyle s);
        void SetOverflowMode(BreadcrumbOverflowMode mode);
        void SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal);
        void SetMaxItemTextWidth(int maxWidth);

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return false; }

        // ===== LAYOUT (CSS Measure/Arrange) =====
        // Intrinsic size = size to render all items uncollapsed (Button pattern).
        // Arrange runs RecalculateLayout (incl. overflow handling) against finalBounds.
        void MeasureCore(const CSSLayout::MeasureConstraints& c,
                         const CSSLayout::LayoutContext& ctx) override;
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

    protected:
        // ===== INTERNAL TYPES =====
        struct ItemSlot {
            int itemIndex = -1;           // -1 means this is the overflow placeholder
            Rect2Di rect;                 // Slot rect (whole clickable item box)
            Rect2Di iconRect;
            Rect2Di textRect;
            Rect2Di dropdownRect;         // Empty if no dropdown
            std::string displayText;      // Original item text (kept for overflow menu / debugging)
            Size2Dd textSize;             // Cached logical size from textLayout
            std::unique_ptr<ITextLayout> textLayout;
            bool isOverflow = false;
            bool isCurrent = false;

            ItemSlot() = default;
            ItemSlot(ItemSlot&&) noexcept = default;
            ItemSlot& operator=(ItemSlot&&) noexcept = default;
            ItemSlot(const ItemSlot&) = delete;
            ItemSlot& operator=(const ItemSlot&) = delete;
        };

        // ===== STATE =====
        std::vector<BreadcrumbItem> items;
        BreadcrumbStyle style;

        // Overridden current-index. -1 means "use last".
        int explicitCurrentIndex = -1;

        // Hover/press tracking
        int hoveredSlotIdx = -1;
        int pressedSlotIdx = -1;
        bool pressedOnDropdown = false;

        // Layout cache — recomputed when geometry is dirty.
        std::vector<ItemSlot> slots;
        std::vector<int> overflowItemIndices; // Indices of items collapsed into "..."
        std::unique_ptr<ITextLayout> separatorLayout; // For text-based separators
        int separatorWidth = 0;               // Cached separator width
        int separatorHeight = 0;
        int rowHeight = 0;
        bool layoutDirty = true;

        // Active dropdown menu (kept alive while shown)
        std::shared_ptr<UltraCanvasMenu> activePopupMenu;

        // ===== HELPERS =====
        int ResolvedCurrentIndex() const;
        void RecalculateLayout(IRenderContext* ctx);
        // Natural content size (all items uncollapsed), excluding padding/border.
        Size2Df MeasureContentSize(IRenderContext* ctx) const;
        std::unique_ptr<ITextLayout> BuildItemLayout(IRenderContext* ctx, const std::string& text,
                                                    bool bold, int maxWidth) const;
        int ComputeItemSlotWidth(const BreadcrumbItem& item, const Size2Dd& textSize,
                                 bool includeDropdown) const;
        int MeasureSeparator(IRenderContext* ctx);

        // Hit testing (returns slot index or -1).
        int HitTest(const Point2Di& localPoint, bool& onDropdown) const;

        // Rendering.
        void RenderBackground(IRenderContext* ctx);
        void RenderSlot(IRenderContext* ctx, const ItemSlot& slot, int slotIdx);
        void RenderSeparator(IRenderContext* ctx, int x, int centerY);
        void RenderDropdownChevron(IRenderContext* ctx, const Rect2Di& rect, const Color& color);

        void OpenDropdownForSlot(int slotIdx);
        void OpenOverflowMenu(int slotIdx);

        void HandleClickOnSlot(int slotIdx, bool onDropdown);
        void NotifyPathChanged();
    };

// ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasBreadcrumb> CreateBreadcrumb(
            const std::string& identifier, float x, float y, float w = 400, float h = 28) {
        return std::make_shared<UltraCanvasBreadcrumb>(identifier, x, y, w, h);
    }

// ===== BUILDER =====
    class BreadcrumbBuilder {
    private:
        std::shared_ptr<UltraCanvasBreadcrumb> breadcrumb;

    public:
        explicit BreadcrumbBuilder(const std::string& identifier = "Breadcrumb",
                                   float x = 0, float y = 0, float w = 400, float h = 28) {
            breadcrumb = std::make_shared<UltraCanvasBreadcrumb>(identifier, x, y, w, h);
        }

        BreadcrumbBuilder& AddItem(const BreadcrumbItem& item) {
            breadcrumb->AddItem(item);
            return *this;
        }
        BreadcrumbBuilder& AddItem(const std::string& text) {
            breadcrumb->AddItem(text);
            return *this;
        }
        BreadcrumbBuilder& AddItem(const std::string& text, std::function<void()> cb) {
            breadcrumb->AddItem(text, std::move(cb));
            return *this;
        }
        BreadcrumbBuilder& SetPath(const std::string& path, char separator = '/') {
            breadcrumb->SetPath(path, separator);
            return *this;
        }
        BreadcrumbBuilder& SetStyle(const BreadcrumbStyle& s) {
            breadcrumb->SetStyle(s);
            return *this;
        }
        BreadcrumbBuilder& SetSeparator(BreadcrumbSeparatorStyle s) {
            breadcrumb->SetSeparatorStyle(s);
            return *this;
        }
        BreadcrumbBuilder& SetItemStyle(BreadcrumbItemStyle s) {
            breadcrumb->SetItemStyle(s);
            return *this;
        }
        BreadcrumbBuilder& SetOverflowMode(BreadcrumbOverflowMode m) {
            breadcrumb->SetOverflowMode(m);
            return *this;
        }
        BreadcrumbBuilder& SetFont(const std::string& family, float size,
                                   FontWeight weight = FontWeight::Normal) {
            breadcrumb->SetFont(family, size, weight);
            return *this;
        }
        BreadcrumbBuilder& OnItemClicked(
                std::function<void(int, const BreadcrumbItem&)> cb) {
            breadcrumb->onItemClicked = std::move(cb);
            return *this;
        }
        BreadcrumbBuilder& OnPathChanged(std::function<void()> cb) {
            breadcrumb->onPathChanged = std::move(cb);
            return *this;
        }

        std::shared_ptr<UltraCanvasBreadcrumb> Build() { return breadcrumb; }
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_BREADCRUMB_H
