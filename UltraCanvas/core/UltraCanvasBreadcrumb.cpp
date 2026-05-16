// core/UltraCanvasBreadcrumb.cpp
// Hierarchical breadcrumb navigation control implementation
// Version: 1.0.0
// Last Modified: 2026-05-14
// Author: UltraCanvas Framework

#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace UltraCanvas {

// ===== STYLE PRESETS =====
    BreadcrumbStyle BreadcrumbStyle::Default() {
        return BreadcrumbStyle{};
    }

    BreadcrumbStyle BreadcrumbStyle::Compact() {
        BreadcrumbStyle s;
        s.itemPaddingHorizontal = 3;
        s.itemPaddingVertical = 1;
        s.iconSize = 12;
        s.separatorSpacing = 3;
        s.separatorSize = 4;
        s.fontStyle.fontSize = 11.0f;
        return s;
    }

    BreadcrumbStyle BreadcrumbStyle::Pills() {
        BreadcrumbStyle s;
        s.itemStyle = BreadcrumbItemStyle::Pill;
        s.itemBackgroundColor = Color(235, 240, 245, 255);
        s.itemHoverBackgroundColor = Color(215, 230, 250, 255);
        s.itemPressedBackgroundColor = Color(190, 215, 245, 255);
        s.currentItemBackgroundColor = Color(0, 120, 215, 255);
        s.currentItemTextColor = Colors::White;
        s.itemCornerRadius = 12;
        s.itemPaddingHorizontal = 10;
        s.itemPaddingVertical = 4;
        s.separatorStyle = BreadcrumbSeparatorStyle::NoSeparator;
        s.separatorSpacing = 4;
        return s;
    }

    BreadcrumbStyle BreadcrumbStyle::FileExplorer() {
        BreadcrumbStyle s;
        s.itemStyle = BreadcrumbItemStyle::Plain;
        s.separatorStyle = BreadcrumbSeparatorStyle::Chevron;
        s.itemHoverBackgroundColor = Color(220, 232, 248, 255);
        s.itemPressedBackgroundColor = Color(195, 215, 240, 255);
        s.itemPaddingHorizontal = 6;
        s.itemPaddingVertical = 3;
        s.itemCornerRadius = 2;
        s.currentItemBold = false;
        s.backgroundColor = Color(250, 250, 252, 255);
        s.borderColor = Color(210, 210, 215, 255);
        s.borderWidth = 1.0f;
        s.cornerRadius = 3.0f;
        return s;
    }

    BreadcrumbStyle BreadcrumbStyle::WebDocs() {
        BreadcrumbStyle s;
        s.itemStyle = BreadcrumbItemStyle::Underline;
        s.separatorStyle = BreadcrumbSeparatorStyle::Slash;
        s.itemTextColor = Color(60, 100, 180, 255);
        s.itemHoverTextColor = Color(20, 70, 160, 255);
        s.currentItemTextColor = Color(60, 60, 60, 255);
        s.currentItemBold = false;
        s.underlineOnHover = true;
        s.itemHoverBackgroundColor = Colors::Transparent;
        s.itemPressedBackgroundColor = Colors::Transparent;
        s.itemPaddingHorizontal = 2;
        s.itemPaddingVertical = 1;
        s.separatorSpacing = 4;
        return s;
    }

// ===== CONSTRUCTOR =====
    UltraCanvasBreadcrumb::UltraCanvasBreadcrumb(const std::string& identifier,
                                                 long x, long y, long w, long h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        style = BreadcrumbStyle::Default();
        SetMouseCursor(UCMouseCursor::Hand);
    }

// ===== ITEM MANAGEMENT =====
    void UltraCanvasBreadcrumb::AddItem(const BreadcrumbItem& item) {
        items.push_back(item);
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

    void UltraCanvasBreadcrumb::AddItem(const std::string& text) {
        AddItem(BreadcrumbItem(text));
    }

    void UltraCanvasBreadcrumb::AddItem(const std::string& text, std::function<void()> onClick) {
        AddItem(BreadcrumbItem(text, std::move(onClick)));
    }

    void UltraCanvasBreadcrumb::InsertItem(int index, const BreadcrumbItem& item) {
        if (index < 0) index = 0;
        if (index > static_cast<int>(items.size())) index = static_cast<int>(items.size());
        items.insert(items.begin() + index, item);
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

    bool UltraCanvasBreadcrumb::RemoveItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return false;
        items.erase(items.begin() + index);
        if (explicitCurrentIndex >= static_cast<int>(items.size())) {
            explicitCurrentIndex = -1;
        }
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
        return true;
    }

    bool UltraCanvasBreadcrumb::RemoveItemById(const std::string& id) {
        int idx = GetItemIndexById(id);
        if (idx < 0) return false;
        return RemoveItem(idx);
    }

    void UltraCanvasBreadcrumb::RemoveItemsAfter(int index) {
        if (index < -1) index = -1;
        if (index >= static_cast<int>(items.size()) - 1) return;
        items.erase(items.begin() + (index + 1), items.end());
        if (explicitCurrentIndex >= static_cast<int>(items.size())) {
            explicitCurrentIndex = -1;
        }
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

    void UltraCanvasBreadcrumb::Clear() {
        if (items.empty()) return;
        items.clear();
        explicitCurrentIndex = -1;
        hoveredSlotIdx = -1;
        pressedSlotIdx = -1;
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

    BreadcrumbItem* UltraCanvasBreadcrumb::GetItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return nullptr;
        return &items[index];
    }

    const BreadcrumbItem* UltraCanvasBreadcrumb::GetItem(int index) const {
        if (index < 0 || index >= static_cast<int>(items.size())) return nullptr;
        return &items[index];
    }

    BreadcrumbItem* UltraCanvasBreadcrumb::GetItemById(const std::string& id) {
        int idx = GetItemIndexById(id);
        if (idx < 0) return nullptr;
        return &items[idx];
    }

    int UltraCanvasBreadcrumb::GetItemIndexById(const std::string& id) const {
        if (id.empty()) return -1;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

// ===== PATH HELPERS =====
    void UltraCanvasBreadcrumb::SetPath(const std::string& path, char separator) {
        items.clear();
        explicitCurrentIndex = -1;

        std::string segment;
        for (char c : path) {
            if (c == separator) {
                if (!segment.empty()) {
                    items.emplace_back(segment);
                    segment.clear();
                }
            } else {
                segment.push_back(c);
            }
        }
        if (!segment.empty()) {
            items.emplace_back(segment);
        }

        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

    std::string UltraCanvasBreadcrumb::GetPath(char separator) const {
        std::string path;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) path.push_back(separator);
            path += items[i].text;
        }
        return path;
    }

    void UltraCanvasBreadcrumb::SetItems(std::vector<BreadcrumbItem> newItems) {
        items = std::move(newItems);
        explicitCurrentIndex = -1;
        layoutDirty = true;
        RequestUpdateGeometry();
        NotifyPathChanged();
    }

// ===== CURRENT INDEX =====
    void UltraCanvasBreadcrumb::SetCurrentIndex(int index) {
        if (index < -1) index = -1;
        if (index >= static_cast<int>(items.size())) index = -1;
        if (explicitCurrentIndex == index) return;
        explicitCurrentIndex = index;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    int UltraCanvasBreadcrumb::GetCurrentIndex() const {
        return ResolvedCurrentIndex();
    }

    int UltraCanvasBreadcrumb::ResolvedCurrentIndex() const {
        if (items.empty()) return -1;
        if (explicitCurrentIndex >= 0 && explicitCurrentIndex < static_cast<int>(items.size())) {
            return explicitCurrentIndex;
        }
        return static_cast<int>(items.size()) - 1;
    }

// ===== STYLE =====
    void UltraCanvasBreadcrumb::SetStyle(const BreadcrumbStyle& newStyle) {
        style = newStyle;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    void UltraCanvasBreadcrumb::SetSeparatorStyle(BreadcrumbSeparatorStyle s) {
        if (style.separatorStyle == s) return;
        style.separatorStyle = s;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    void UltraCanvasBreadcrumb::SetCustomSeparatorText(const std::string& text) {
        style.customSeparatorText = text;
        if (style.separatorStyle == BreadcrumbSeparatorStyle::CustomText) {
            layoutDirty = true;
            RequestUpdateGeometry();
        }
    }

    void UltraCanvasBreadcrumb::SetCustomSeparatorIcon(const std::string& iconPath) {
        style.customSeparatorIcon = iconPath.empty() ? nullptr : UCImage::Get(iconPath);
        if (style.separatorStyle == BreadcrumbSeparatorStyle::CustomIcon) {
            layoutDirty = true;
            RequestUpdateGeometry();
        }
    }

    void UltraCanvasBreadcrumb::SetItemStyle(BreadcrumbItemStyle s) {
        if (style.itemStyle == s) return;
        style.itemStyle = s;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    void UltraCanvasBreadcrumb::SetOverflowMode(BreadcrumbOverflowMode mode) {
        if (style.overflowMode == mode) return;
        style.overflowMode = mode;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    void UltraCanvasBreadcrumb::SetFont(const std::string& family, float size, FontWeight weight) {
        style.fontStyle.fontFamily = family;
        style.fontStyle.fontSize = size;
        style.fontStyle.fontWeight = weight;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

    void UltraCanvasBreadcrumb::SetMaxItemTextWidth(int maxWidth) {
        if (style.maxItemTextWidth == maxWidth) return;
        style.maxItemTextWidth = maxWidth;
        layoutDirty = true;
        RequestUpdateGeometry();
    }

// ===== MEASUREMENT HELPERS =====
    static std::string SeparatorTextFor(BreadcrumbSeparatorStyle s, const std::string& custom) {
        switch (s) {
            case BreadcrumbSeparatorStyle::Slash:        return "/";
            case BreadcrumbSeparatorStyle::BackSlash:    return "\\";
            case BreadcrumbSeparatorStyle::GreaterThan:  return ">";
            case BreadcrumbSeparatorStyle::Arrow:        return "->";
            case BreadcrumbSeparatorStyle::Bullet:       return "*";
            case BreadcrumbSeparatorStyle::Pipe:         return "|";
            case BreadcrumbSeparatorStyle::Dot:          return ".";
            case BreadcrumbSeparatorStyle::CustomText:   return custom;
            default:                                     return std::string();
        }
    }

    int UltraCanvasBreadcrumb::MeasureSeparator(IRenderContext* ctx) {
        switch (style.separatorStyle) {
            case BreadcrumbSeparatorStyle::NoSeparator:
                return 0;
            case BreadcrumbSeparatorStyle::Chevron:
                return style.separatorSize;
            case BreadcrumbSeparatorStyle::ChevronDouble:
                return style.separatorSize * 2 - 2;
            case BreadcrumbSeparatorStyle::CustomIcon: {
                if (!style.customSeparatorIcon) return 0;
                return style.iconSize;
            }
            default: {
                std::string s = SeparatorTextFor(style.separatorStyle, style.customSeparatorText);
                if (s.empty()) return 0;
                return ctx->GetTextLineWidth(s);
            }
        }
    }

    int UltraCanvasBreadcrumb::MeasureItemWidth(IRenderContext* ctx, const BreadcrumbItem& item,
                                                bool includeDropdown,
                                                const std::string& displayText) {
        int width = style.itemPaddingHorizontal * 2;
        bool hasIcon = item.icon != nullptr;
        bool hasText = !displayText.empty();

        if (hasIcon) width += style.iconSize;
        if (hasIcon && hasText) width += style.iconTextSpacing;
        if (hasText) width += ctx->GetTextLineWidth(displayText);
        if (includeDropdown && item.hasDropdown) {
            width += style.dropdownChevronSpacing + style.dropdownChevronSize;
        }
        return width;
    }

    std::string UltraCanvasBreadcrumb::FitText(IRenderContext* ctx, const std::string& text,
                                               int maxPixelWidth) {
        if (text.empty() || maxPixelWidth <= 0) return text;
        int currentWidth = ctx->GetTextLineWidth(text);
        if (currentWidth <= maxPixelWidth) return text;

        const std::string ellipsis = "...";
        int ellipsisWidth = ctx->GetTextLineWidth(ellipsis);
        if (ellipsisWidth >= maxPixelWidth) return ellipsis;

        // Binary search for the longest prefix that fits with the ellipsis appended.
        int lo = 0, hi = static_cast<int>(text.size());
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            std::string candidate = text.substr(0, mid) + ellipsis;
            if (ctx->GetTextLineWidth(candidate) <= maxPixelWidth) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        if (lo <= 0) return ellipsis;
        return text.substr(0, lo) + ellipsis;
    }

// ===== LAYOUT =====
    void UltraCanvasBreadcrumb::RecalculateLayout(IRenderContext* ctx) {
        slots.clear();
        overflowItemIndices.clear();

        if (items.empty()) {
            layoutDirty = false;
            return;
        }

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);

        separatorWidth = MeasureSeparator(ctx);
        separatorHeight = ctx->GetTextLineHeight("Mg");
        rowHeight = std::max<int>(separatorHeight, style.iconSize) + style.itemPaddingVertical * 2;

        Rect2Di content = GetLocalContentRect();
        int availableWidth = content.width;
        int currentIdx = ResolvedCurrentIndex();

        // ===== STEP 1: Measure every item (with default text limits) =====
        std::vector<std::string> displayTexts(items.size());
        std::vector<int> itemWidths(items.size());
        int totalWidth = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            std::string text = items[i].text;
            if (style.maxItemTextWidth > 0
                && (style.overflowMode == BreadcrumbOverflowMode::Ellipsize
                    || style.overflowMode == BreadcrumbOverflowMode::ShrinkText
                    || style.overflowMode == BreadcrumbOverflowMode::Collapse)) {
                text = FitText(ctx, text, style.maxItemTextWidth);
            }
            displayTexts[i] = text;
            itemWidths[i] = MeasureItemWidth(ctx, items[i], true, text);
            totalWidth += itemWidths[i];
        }
        int separatorsTotal = items.size() > 1
                              ? static_cast<int>(items.size() - 1)
                                * (separatorWidth + style.separatorSpacing * 2)
                              : 0;
        totalWidth += separatorsTotal;

        // ===== STEP 2: Apply overflow strategy if necessary =====
        std::vector<bool> visible(items.size(), true);
        bool needsOverflow = false;

        if (totalWidth > availableWidth) {
            switch (style.overflowMode) {
                case BreadcrumbOverflowMode::Clip:
                    // Nothing — slots will be drawn under clipping by Render().
                    break;

                case BreadcrumbOverflowMode::Ellipsize: {
                    // Try shrinking each item's text further until things fit.
                    int totalSlack = totalWidth - availableWidth;
                    for (size_t i = 0; i < items.size() && totalSlack > 0; ++i) {
                        if (i == static_cast<size_t>(currentIdx)) continue; // never shrink current
                        int currentTextW = ctx->GetTextLineWidth(displayTexts[i]);
                        if (currentTextW <= 0) continue;
                        int newMax = std::max(8, currentTextW - totalSlack);
                        std::string fitted = FitText(ctx, items[i].text, newMax);
                        int newWidth = MeasureItemWidth(ctx, items[i], true, fitted);
                        totalSlack -= (itemWidths[i] - newWidth);
                        displayTexts[i] = fitted;
                        itemWidths[i] = newWidth;
                    }
                    break;
                }

                case BreadcrumbOverflowMode::ShrinkText: {
                    // Same as Ellipsize but applied uniformly.
                    int perItemBudget = std::max(20, availableWidth / std::max<int>(1, (int)items.size()));
                    for (size_t i = 0; i < items.size(); ++i) {
                        std::string fitted = FitText(ctx, items[i].text, perItemBudget);
                        displayTexts[i] = fitted;
                        itemWidths[i] = MeasureItemWidth(ctx, items[i], true, fitted);
                    }
                    break;
                }

                case BreadcrumbOverflowMode::Collapse: {
                    needsOverflow = true;
                    // Reserve width for the "..." overflow placeholder.
                    int overflowBaseWidth = style.itemPaddingHorizontal * 2
                                            + ctx->GetTextLineWidth(style.overflowEllipsisText)
                                            + style.dropdownChevronSpacing
                                            + style.dropdownChevronSize;
                    int sepBlock = separatorWidth + style.separatorSpacing * 2;

                    int firstIdx = style.keepFirstItemOnCollapse ? 0 : -1;
                    int lastKeep = std::max(currentIdx,
                                            static_cast<int>(items.size())
                                            - std::max(1, style.minVisibleAfterCollapse));

                    // Greedy collapse from the middle until fit or only required items remain.
                    while (true) {
                        // Compute width given current visibility.
                        int w = 0;
                        int visCount = 0;
                        for (size_t i = 0; i < items.size(); ++i) {
                            if (visible[i]) {
                                w += itemWidths[i];
                                ++visCount;
                            }
                        }
                        // overflow placeholder occupies one slot if anything is hidden.
                        bool anyHidden = false;
                        for (bool v : visible) if (!v) { anyHidden = true; break; }
                        if (anyHidden) {
                            w += overflowBaseWidth;
                            ++visCount;
                        }
                        if (visCount > 1) w += (visCount - 1) * sepBlock;
                        if (w <= availableWidth) break;

                        // Pick a candidate to hide: walk middle outwards, skipping required.
                        int n = static_cast<int>(items.size());
                        int candidate = -1;
                        for (int dist = 0; dist < n; ++dist) {
                            int center = n / 2;
                            int idxA = center - dist;
                            int idxB = center + dist;
                            for (int idx : {idxA, idxB}) {
                                if (idx < 0 || idx >= n) continue;
                                if (!visible[idx]) continue;
                                if (idx == firstIdx) continue;
                                if (idx >= lastKeep) continue;
                                candidate = idx;
                                break;
                            }
                            if (candidate >= 0) break;
                        }
                        if (candidate < 0) break;        // Nothing else to hide
                        visible[candidate] = false;
                    }

                    for (size_t i = 0; i < items.size(); ++i) {
                        if (!visible[i]) overflowItemIndices.push_back(static_cast<int>(i));
                    }
                    break;
                }
            }
        }

        // ===== STEP 3: Build slots in render order =====
        int x = content.x;
        int y = content.y;
        int slotHeight = std::max<int>(rowHeight, content.height);
        int contentTop = content.y;
        int contentBottom = content.y + content.height;
        int centerY = (contentTop + contentBottom) / 2;

        bool overflowEmitted = false;

        for (size_t i = 0; i < items.size(); ++i) {
            // Emit overflow placeholder right before the first hidden gap.
            if (needsOverflow && !visible[i] && !overflowEmitted) {
                if (!slots.empty()) {
                    x += style.separatorSpacing;
                    // (Separator drawing computed during Render based on slot layout.)
                    x += separatorWidth + style.separatorSpacing;
                }
                ItemSlot oslot;
                oslot.itemIndex = -1;
                oslot.isOverflow = true;
                int textW = ctx->GetTextLineWidth(style.overflowEllipsisText);
                int oWidth = style.itemPaddingHorizontal * 2 + textW
                             + style.dropdownChevronSpacing + style.dropdownChevronSize;
                oslot.rect = Rect2Di(x, centerY - slotHeight / 2, oWidth, slotHeight);
                int innerX = x + style.itemPaddingHorizontal;
                oslot.textRect = Rect2Di(innerX, oslot.rect.y, textW, slotHeight);
                innerX += textW + style.dropdownChevronSpacing;
                oslot.dropdownRect = Rect2Di(innerX,
                                             centerY - style.dropdownChevronSize / 2,
                                             style.dropdownChevronSize,
                                             style.dropdownChevronSize);
                oslot.displayText = style.overflowEllipsisText;
                slots.push_back(oslot);
                x += oWidth;
                overflowEmitted = true;
            }

            if (!visible[i]) continue;

            if (!slots.empty()) {
                x += style.separatorSpacing;
                x += separatorWidth + style.separatorSpacing;
            }

            ItemSlot slot;
            slot.itemIndex = static_cast<int>(i);
            slot.displayText = displayTexts[i];
            slot.isCurrent = (static_cast<int>(i) == currentIdx);
            int width = itemWidths[i];
            slot.rect = Rect2Di(x, centerY - slotHeight / 2, width, slotHeight);

            int innerX = x + style.itemPaddingHorizontal;
            if (items[i].icon) {
                slot.iconRect = Rect2Di(innerX, centerY - style.iconSize / 2,
                                        style.iconSize, style.iconSize);
                innerX += style.iconSize;
                if (!slot.displayText.empty()) innerX += style.iconTextSpacing;
            }
            if (!slot.displayText.empty()) {
                int textW = ctx->GetTextLineWidth(slot.displayText);
                int textH = ctx->GetTextLineHeight(slot.displayText);
                slot.textRect = Rect2Di(innerX, centerY - textH / 2, textW, textH);
                innerX += textW;
            }
            if (items[i].hasDropdown) {
                innerX += style.dropdownChevronSpacing;
                slot.dropdownRect = Rect2Di(innerX,
                                            centerY - style.dropdownChevronSize / 2,
                                            style.dropdownChevronSize,
                                            style.dropdownChevronSize);
            }
            slots.push_back(slot);
            x += width;
        }

        ctx->PopState();
        layoutDirty = false;
    }

// ===== UPDATE GEOMETRY =====
    void UltraCanvasBreadcrumb::UpdateGeometry(IRenderContext* ctx) {
        if (layoutDirty || needsUpdateGeometry) {
            RecalculateLayout(ctx);
            needsUpdateGeometry = false;
        }
    }

// ===== PREFERRED SIZE =====
    int UltraCanvasBreadcrumb::GetPreferredWidth() {
        auto ctx = GetRenderContext();
        if (!ctx || items.empty()) return GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        int sepW = MeasureSeparator(ctx);
        int total = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            std::string text = items[i].text;
            if (style.maxItemTextWidth > 0) {
                text = FitText(ctx, text, style.maxItemTextWidth);
            }
            total += MeasureItemWidth(ctx, items[i], true, text);
            if (i + 1 < items.size()) {
                total += sepW + style.separatorSpacing * 2;
            }
        }
        ctx->PopState();
        return total + GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
    }

    int UltraCanvasBreadcrumb::GetPreferredHeight() {
        auto ctx = GetRenderContext();
        if (!ctx) return style.fontStyle.fontSize + style.itemPaddingVertical * 2
                        + GetTotalPaddingVertical() + GetTotalBorderVertical();
        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        int textH = ctx->GetTextLineHeight("Mg");
        ctx->PopState();
        int rowH = std::max<int>(textH, style.iconSize) + style.itemPaddingVertical * 2;
        return rowH + GetTotalPaddingVertical() + GetTotalBorderVertical();
    }

// ===== HIT TESTING =====
    int UltraCanvasBreadcrumb::HitTest(const Point2Di& localPoint, bool& onDropdown) const {
        onDropdown = false;
        for (size_t i = 0; i < slots.size(); ++i) {
            const ItemSlot& slot = slots[i];
            if (slot.rect.Contains(localPoint)) {
                if (slot.dropdownRect.width > 0 && slot.dropdownRect.Contains(localPoint)) {
                    onDropdown = true;
                }
                return static_cast<int>(i);
            }
        }
        return -1;
    }

// ===== RENDERING =====
    void UltraCanvasBreadcrumb::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        UpdateGeometry(ctx);

        // Draw the strip background using the base implementation, then our background color.
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        RenderBackground(ctx);

        if (slots.empty()) return;

        ctx->PushState();
        // Clip to content area to avoid bleeding for Clip overflow mode.
        Rect2Di content = GetLocalContentRect();
        ctx->ClipRect(Rect2Df(content.x, content.y, content.width, content.height));

        ctx->SetFontStyle(style.fontStyle);

        // Draw separators between adjacent visible slots.
        for (size_t i = 1; i < slots.size(); ++i) {
            int prevRight = slots[i - 1].rect.x + slots[i - 1].rect.width;
            int nextLeft = slots[i].rect.x;
            int sepCenterX = (prevRight + nextLeft) / 2;
            int sepCenterY = slots[i].rect.y + slots[i].rect.height / 2;
            RenderSeparator(ctx, sepCenterX, sepCenterY);
        }

        for (size_t i = 0; i < slots.size(); ++i) {
            RenderSlot(ctx, slots[i], static_cast<int>(i));
        }

        ctx->PopState();
    }

    void UltraCanvasBreadcrumb::RenderBackground(IRenderContext* ctx) {
        if (style.backgroundColor.a == 0 && style.borderColor.a == 0) return;
        Rect2Di b = GetLocalBounds();
        ctx->DrawFilledRectangle(Rect2Df(b.x, b.y, b.width, b.height),
                                 style.backgroundColor,
                                 style.borderWidth,
                                 style.borderColor,
                                 style.cornerRadius);
    }

    void UltraCanvasBreadcrumb::RenderSlot(IRenderContext* ctx, const ItemSlot& slot, int slotIdx) {
        bool isHovered = (hoveredSlotIdx == slotIdx);
        bool isPressed = (pressedSlotIdx == slotIdx);
        bool isCurrent = slot.isCurrent;
        bool isOverflow = slot.isOverflow;
        bool clickable = isOverflow ||
                         (slot.itemIndex >= 0 && slot.itemIndex < (int)items.size()
                          && items[slot.itemIndex].clickable
                          && (!isCurrent || style.currentItemClickable));
        bool isDisabled = !clickable;

        // Pick text color
        Color textColor;
        if (isDisabled && !isCurrent) {
            textColor = style.disabledItemTextColor;
        } else if (isPressed) {
            textColor = style.itemPressedTextColor;
        } else if (isHovered && !isCurrent) {
            textColor = style.itemHoverTextColor;
        } else if (isCurrent) {
            textColor = style.currentItemTextColor;
        } else {
            textColor = style.itemTextColor;
        }

        // Pick background color
        Color bgColor = Colors::Transparent;
        bool drawBackground = false;
        if (style.itemStyle == BreadcrumbItemStyle::Pill
            || style.itemStyle == BreadcrumbItemStyle::Tab) {
            if (isCurrent && style.currentItemBackgroundColor.a > 0) {
                bgColor = style.currentItemBackgroundColor;
                drawBackground = true;
            } else if (style.itemBackgroundColor.a > 0) {
                bgColor = style.itemBackgroundColor;
                drawBackground = true;
            }
        }
        if (isPressed && clickable && style.itemPressedBackgroundColor.a > 0) {
            bgColor = style.itemPressedBackgroundColor;
            drawBackground = true;
        } else if (isHovered && clickable && style.itemHoverBackgroundColor.a > 0) {
            bgColor = style.itemHoverBackgroundColor;
            drawBackground = true;
        }

        if (drawBackground) {
            Rect2Di r = slot.rect;
            ctx->DrawFilledRectangle(Rect2Df(r.x, r.y, r.width, r.height),
                                     bgColor, 0.0f, Colors::Transparent,
                                     static_cast<float>(style.itemCornerRadius));
        }

        // Tab-style underline strip
        if (style.itemStyle == BreadcrumbItemStyle::Tab && isCurrent) {
            int y = slot.rect.y + slot.rect.height - 2;
            ctx->SetStrokePaint(style.currentItemTextColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLine(Point2Df(slot.rect.x + 2, y),
                          Point2Df(slot.rect.x + slot.rect.width - 2, y));
        }

        // Icon
        if (!isOverflow && slot.itemIndex >= 0 && slot.itemIndex < (int)items.size()
            && items[slot.itemIndex].icon && slot.iconRect.width > 0) {
            ctx->DrawImage(*items[slot.itemIndex].icon.get(),
                           Rect2Df(slot.iconRect.x, slot.iconRect.y,
                                   slot.iconRect.width, slot.iconRect.height),
                           ImageFitMode::Contain);
        }

        // Text — handle bolding for current item without mutating shared style.
        if (!slot.displayText.empty() && slot.textRect.width > 0) {
            FontWeight weight = style.fontStyle.fontWeight;
            if (isCurrent && style.currentItemBold) weight = FontWeight::Bold;
            ctx->SetFontFace(style.fontStyle.fontFamily, weight, style.fontStyle.fontSlant);
            ctx->SetFontSize(style.fontStyle.fontSize);
            ctx->SetTextPaint(textColor);
            ctx->DrawText(slot.displayText, Point2Df(slot.textRect.x, slot.textRect.y));

            // Underline
            bool drawUnderline = false;
            if (style.itemStyle == BreadcrumbItemStyle::Underline) {
                if (style.underlineCurrent && isCurrent) drawUnderline = true;
                if (style.underlineOnHover && isHovered && clickable) drawUnderline = true;
            } else {
                if (style.underlineOnHover && isHovered && clickable) drawUnderline = true;
            }
            if (drawUnderline) {
                int y = slot.textRect.y + slot.textRect.height + 1;
                ctx->SetStrokePaint(textColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(Point2Df(slot.textRect.x, y),
                              Point2Df(slot.textRect.x + slot.textRect.width, y));
            }
        }

        // Dropdown chevron
        if (slot.dropdownRect.width > 0) {
            RenderDropdownChevron(ctx, slot.dropdownRect, textColor);
        }
    }

    void UltraCanvasBreadcrumb::RenderSeparator(IRenderContext* ctx, int x, int centerY) {
        switch (style.separatorStyle) {
            case BreadcrumbSeparatorStyle::NoSeparator:
                return;
            case BreadcrumbSeparatorStyle::Chevron: {
                float half = style.separatorSize / 2.0f;
                ctx->SetStrokePaint(style.separatorColor);
                ctx->SetStrokeWidth(style.separatorThickness);
                ctx->SetLineCap(LineCap::Round);
                ctx->SetLineJoin(LineJoin::Round);
                ctx->ClearPath();
                ctx->MoveTo(x - half, centerY - half);
                ctx->LineTo(x + half, centerY);
                ctx->LineTo(x - half, centerY + half);
                ctx->StrokePathPreserve();
                ctx->ClearPath();
                return;
            }
            case BreadcrumbSeparatorStyle::ChevronDouble: {
                float half = style.separatorSize / 2.0f;
                ctx->SetStrokePaint(style.separatorColor);
                ctx->SetStrokeWidth(style.separatorThickness);
                ctx->SetLineCap(LineCap::Round);
                ctx->SetLineJoin(LineJoin::Round);
                ctx->ClearPath();
                ctx->MoveTo(x - style.separatorSize, centerY - half);
                ctx->LineTo(x, centerY);
                ctx->LineTo(x - style.separatorSize, centerY + half);
                ctx->MoveTo(x - 2, centerY - half);
                ctx->LineTo(x - 2 + style.separatorSize, centerY);
                ctx->LineTo(x - 2, centerY + half);
                ctx->StrokePathPreserve();
                ctx->ClearPath();
                return;
            }
            case BreadcrumbSeparatorStyle::CustomIcon: {
                if (!style.customSeparatorIcon) return;
                int s = style.iconSize;
                ctx->DrawImage(*style.customSeparatorIcon.get(),
                               Rect2Df(x - s / 2.0f, centerY - s / 2.0f, s, s),
                               ImageFitMode::Contain);
                return;
            }
            default: {
                std::string text = SeparatorTextFor(style.separatorStyle, style.customSeparatorText);
                if (text.empty()) return;
                ctx->SetTextPaint(style.separatorColor);
                int textW = ctx->GetTextLineWidth(text);
                int textH = ctx->GetTextLineHeight(text);
                ctx->DrawText(text, Point2Df(x - textW / 2.0f, centerY - textH / 2.0f));
                return;
            }
        }
    }

    void UltraCanvasBreadcrumb::RenderDropdownChevron(IRenderContext* ctx, const Rect2Di& rect,
                                                     const Color& color) {
        float cx = rect.x + rect.width / 2.0f;
        float cy = rect.y + rect.height / 2.0f;
        float half = rect.width / 2.0f;
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.4f);
        ctx->SetLineCap(LineCap::Round);
        ctx->SetLineJoin(LineJoin::Round);
        ctx->ClearPath();
        ctx->MoveTo(cx - half, cy - half / 2.0f);
        ctx->LineTo(cx, cy + half / 2.0f);
        ctx->LineTo(cx + half, cy - half / 2.0f);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasBreadcrumb::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseMove: {
                if (Contains(event.pointer)) {
                    bool onDropdown = false;
                    int idx = HitTest(event.pointer, onDropdown);
                    if (idx != hoveredSlotIdx) {
                        hoveredSlotIdx = idx;
                        RequestRedraw();
                    }
                    SetHovered(true);
                } else if (hoveredSlotIdx != -1) {
                    hoveredSlotIdx = -1;
                    SetHovered(false);
                    RequestRedraw();
                }
                return false;
            }

            case UCEventType::MouseLeave:
                if (hoveredSlotIdx != -1 || pressedSlotIdx != -1) {
                    hoveredSlotIdx = -1;
                    pressedSlotIdx = -1;
                    pressedOnDropdown = false;
                    SetHovered(false);
                    RequestRedraw();
                }
                return false;

            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return false;
                if (!Contains(event.pointer)) return false;
                bool onDropdown = false;
                int idx = HitTest(event.pointer, onDropdown);
                if (idx < 0) return false;
                pressedSlotIdx = idx;
                pressedOnDropdown = onDropdown;
                RequestRedraw();
                return true;
            }

            case UCEventType::MouseUp: {
                if (event.button != UCMouseButton::Left) return false;
                if (pressedSlotIdx < 0) return false;
                bool onDropdown = false;
                int idx = HitTest(event.pointer, onDropdown);
                bool wasPressed = (pressedSlotIdx == idx);
                int slotToActivate = pressedSlotIdx;
                bool dropdownClick = pressedOnDropdown && onDropdown;
                pressedSlotIdx = -1;
                pressedOnDropdown = false;
                RequestRedraw();
                if (wasPressed && idx >= 0 && idx < (int)slots.size()) {
                    HandleClickOnSlot(slotToActivate, dropdownClick);
                }
                return true;
            }

            default:
                break;
        }
        return false;
    }

    void UltraCanvasBreadcrumb::HandleClickOnSlot(int slotIdx, bool onDropdown) {
        if (slotIdx < 0 || slotIdx >= (int)slots.size()) return;
        const ItemSlot& slot = slots[slotIdx];

        if (slot.isOverflow) {
            OpenOverflowMenu(slotIdx);
            return;
        }

        int itemIdx = slot.itemIndex;
        if (itemIdx < 0 || itemIdx >= (int)items.size()) return;
        BreadcrumbItem& item = items[itemIdx];

        if (onDropdown && item.hasDropdown) {
            OpenDropdownForSlot(slotIdx);
            if (onItemDropdown) onItemDropdown(itemIdx, item);
            return;
        }

        bool isCurrent = (itemIdx == ResolvedCurrentIndex());
        if (isCurrent && !style.currentItemClickable) return;
        if (!item.clickable) return;

        if (item.onClick) item.onClick();
        if (onItemClicked) onItemClicked(itemIdx, item);
    }

    void UltraCanvasBreadcrumb::OpenDropdownForSlot(int slotIdx) {
        if (slotIdx < 0 || slotIdx >= (int)slots.size()) return;
        const ItemSlot& slot = slots[slotIdx];
        if (slot.itemIndex < 0) return;
        BreadcrumbItem& item = items[slot.itemIndex];

        std::vector<MenuItemData> menuItems = item.dropdownItems;
        if (item.dropdownItemsProvider) {
            auto provided = item.dropdownItemsProvider();
            menuItems.insert(menuItems.end(), provided.begin(), provided.end());
        }
        if (menuItems.empty()) return;

        auto win = GetWindow();
        if (!win) return;

        activePopupMenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_dropdown_" + std::to_string(slot.itemIndex),
                0, 0, 200, 0);
        activePopupMenu->SetMenuType(MenuType::PopupMenu);
        for (auto& mi : menuItems) activePopupMenu->AddItem(mi);

        Point2Di winPos(GetXInWindow() + slot.rect.x,
                        GetYInWindow() + slot.rect.y + slot.rect.height + 1);

        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        activePopupMenu->OpenMenu(winPos, *win, settings);
    }

    void UltraCanvasBreadcrumb::OpenOverflowMenu(int slotIdx) {
        if (overflowItemIndices.empty()) return;
        if (slotIdx < 0 || slotIdx >= (int)slots.size()) return;
        const ItemSlot& slot = slots[slotIdx];

        auto win = GetWindow();
        if (!win) return;

        activePopupMenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_overflow", 0, 0, 220, 0);
        activePopupMenu->SetMenuType(MenuType::PopupMenu);

        // Build a text representation of the current separator so menu rows can
        // end with it (e.g. "Engineering  >"). Drawn chevrons fall back to text.
        std::string sepSuffix;
        switch (style.separatorStyle) {
            case BreadcrumbSeparatorStyle::Chevron:       sepSuffix = ">";  break;
            case BreadcrumbSeparatorStyle::ChevronDouble: sepSuffix = ">>"; break;
            case BreadcrumbSeparatorStyle::CustomIcon:
            case BreadcrumbSeparatorStyle::NoSeparator:   sepSuffix.clear(); break;
            default:
                sepSuffix = SeparatorTextFor(style.separatorStyle, style.customSeparatorText);
                break;
        }

        for (int idx : overflowItemIndices) {
            if (idx < 0 || idx >= (int)items.size()) continue;
            int capturedIdx = idx;
            const BreadcrumbItem& item = items[idx];
            std::string label = item.text.empty() ? std::string("(item)") : item.text;
            if (!sepSuffix.empty()) label += "  " + sepSuffix;
            activePopupMenu->AddItem(MenuItemData::Action(label, [this, capturedIdx]() {
                if (capturedIdx < 0 || capturedIdx >= (int)items.size()) return;
                BreadcrumbItem& it = items[capturedIdx];
                if (it.onClick) it.onClick();
                if (onItemClicked) onItemClicked(capturedIdx, it);
            }));
        }

        Point2Di winPos(GetXInWindow() + slot.rect.x,
                        GetYInWindow() + slot.rect.y + slot.rect.height + 1);

        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        activePopupMenu->OpenMenu(winPos, *win, settings);

        if (onOverflowOpened) onOverflowOpened();
    }

    void UltraCanvasBreadcrumb::NotifyPathChanged() {
        if (onPathChanged) onPathChanged();
    }

} // namespace UltraCanvas
