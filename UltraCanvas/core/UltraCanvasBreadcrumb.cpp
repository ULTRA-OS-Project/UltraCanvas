// core/UltraCanvasBreadcrumb.cpp
// Hierarchical breadcrumb navigation control implementation
// Version: 1.3.0
// Last Modified: 2026-06-20
// Author: UltraCanvas Framework

#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "CSSLayout/LayoutUtils.h"
#include <algorithm>
#include <optional>
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

    BreadcrumbStyle BreadcrumbStyle::Arrow() {
        BreadcrumbStyle s;
        s.itemStyle = BreadcrumbItemStyle::Arrow;
        s.separatorStyle = BreadcrumbSeparatorStyle::NoSeparator;
        s.overflowMode = BreadcrumbOverflowMode::Clip;
        s.itemBackgroundColor = Color(228, 230, 233, 255);
        s.itemHoverBackgroundColor = Color(210, 224, 245, 255);
        s.itemPressedBackgroundColor = Color(190, 212, 240, 255);
        s.currentItemBackgroundColor = Color(0, 120, 215, 255);
        s.currentItemTextColor = Colors::White;
        s.itemTextColor = Color(60, 60, 60, 255);
        s.currentItemBold = false;
        s.currentItemClickable = true;
        // Thin light gap drawn along each segment outline so neighbours stay distinct.
        s.separatorColor = Color(255, 255, 255, 255);
        s.separatorThickness = 1.0f;
        s.arrowSize = 10;
        s.itemPaddingHorizontal = 12;
        s.itemPaddingVertical = 5;
        s.separatorSpacing = 0;
        return s;
    }

    BreadcrumbStyle BreadcrumbStyle::Parallelogram() {
        BreadcrumbStyle s;
        s.itemStyle = BreadcrumbItemStyle::Parallelogram;
        s.separatorStyle = BreadcrumbSeparatorStyle::NoSeparator;
        s.overflowMode = BreadcrumbOverflowMode::Clip;
        s.itemBackgroundColor = Color(244, 122, 32, 255);
        s.itemHoverBackgroundColor = Color(255, 150, 70, 255);
        s.itemPressedBackgroundColor = Color(225, 105, 20, 255);
        s.currentItemBackgroundColor = Color(250, 170, 110, 255);
        s.itemTextColor = Colors::White;
        s.currentItemTextColor = Colors::White;
        s.currentItemBold = false;
        s.currentItemClickable = true;
        s.separatorColor = Color(255, 255, 255, 255);   // thin seam between segments
        s.separatorThickness = 2.0f;
        s.arrowSize = 14;
        s.itemPaddingHorizontal = 14;
        s.itemPaddingVertical = 6;
        s.separatorSpacing = 0;
        return s;
    }

    BreadcrumbStyle BreadcrumbStyle::Steps() {
        BreadcrumbStyle s = BreadcrumbStyle::Arrow();
        // Dark "wizard step" strip with round numbered badges.
        s.backgroundColor = Color(32, 33, 36, 255);
        s.cornerRadius = 6.0f;
        s.itemBackgroundColor = Color(60, 63, 68, 255);
        s.itemHoverBackgroundColor = Color(80, 84, 90, 255);
        s.itemPressedBackgroundColor = Color(50, 53, 58, 255);
        s.currentItemBackgroundColor = Color(40, 110, 235, 255);
        s.itemTextColor = Color(210, 212, 216, 255);
        s.currentItemTextColor = Colors::White;
        s.separatorColor = Color(32, 33, 36, 255);
        s.separatorThickness = 2.0f;
        s.showLevelIndicator = true;
        s.levelIndicatorBackground = BreadcrumbLevelIndicatorBackground::Round;
        s.levelIndicatorBorder = true;
        s.levelIndicatorSize = 22;
        s.levelIndicatorColor = Color(28, 30, 34, 255);
        s.levelIndicatorTextColor = Colors::White;
        s.levelIndicatorBorderColor = Color(120, 150, 245, 255);
        s.levelIndicatorBorderWidth = 2.0f;
        s.itemPaddingHorizontal = 12;
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
                                                 float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        style = BreadcrumbStyle::Default();
        SetMouseCursor(UCMouseCursor::Hand);
    }

// ===== ITEM MANAGEMENT =====
    void UltraCanvasBreadcrumb::AddItem(const BreadcrumbItem& item) {
        items.push_back(item);
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
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
        InvalidateLayout(); RequestRedraw();
        NotifyPathChanged();
    }

    bool UltraCanvasBreadcrumb::RemoveItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return false;
        items.erase(items.begin() + index);
        if (explicitCurrentIndex >= static_cast<int>(items.size())) {
            explicitCurrentIndex = -1;
        }
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
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
        InvalidateLayout(); RequestRedraw();
        NotifyPathChanged();
    }

    void UltraCanvasBreadcrumb::Clear() {
        if (items.empty()) return;
        items.clear();
        explicitCurrentIndex = -1;
        hoveredSlotIdx = -1;
        pressedSlotIdx = -1;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
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
        InvalidateLayout(); RequestRedraw();
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
        InvalidateLayout(); RequestRedraw();
        NotifyPathChanged();
    }

// ===== CURRENT INDEX =====
    void UltraCanvasBreadcrumb::SetCurrentIndex(int index) {
        if (index < -1) index = -1;
        if (index >= static_cast<int>(items.size())) index = -1;
        if (explicitCurrentIndex == index) return;
        explicitCurrentIndex = index;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
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
        InvalidateLayout(); RequestRedraw();
    }

    void UltraCanvasBreadcrumb::SetSeparatorStyle(BreadcrumbSeparatorStyle s) {
        if (style.separatorStyle == s) return;
        style.separatorStyle = s;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
    }

    void UltraCanvasBreadcrumb::SetCustomSeparatorText(const std::string& text) {
        style.customSeparatorText = text;
        if (style.separatorStyle == BreadcrumbSeparatorStyle::CustomText) {
            layoutDirty = true;
            InvalidateLayout(); RequestRedraw();
        }
    }

    void UltraCanvasBreadcrumb::SetCustomSeparatorIcon(const std::string& iconPath) {
        style.customSeparatorIcon = iconPath.empty() ? nullptr : UCImage::Get(iconPath);
        if (style.separatorStyle == BreadcrumbSeparatorStyle::CustomIcon) {
            layoutDirty = true;
            InvalidateLayout(); RequestRedraw();
        }
    }

    void UltraCanvasBreadcrumb::SetItemStyle(BreadcrumbItemStyle s) {
        if (style.itemStyle == s) return;
        style.itemStyle = s;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
    }

    void UltraCanvasBreadcrumb::SetOverflowMode(BreadcrumbOverflowMode mode) {
        if (style.overflowMode == mode) return;
        style.overflowMode = mode;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
    }

    void UltraCanvasBreadcrumb::SetFont(const std::string& family, float size, FontWeight weight) {
        style.fontStyle.fontFamily = family;
        style.fontStyle.fontSize = size;
        style.fontStyle.fontWeight = weight;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
    }

    void UltraCanvasBreadcrumb::SetMaxItemTextWidth(int maxWidth) {
        if (style.maxItemTextWidth == maxWidth) return;
        style.maxItemTextWidth = maxWidth;
        layoutDirty = true;
        InvalidateLayout(); RequestRedraw();
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
                separatorLayout.reset();
                return 0;
            case BreadcrumbSeparatorStyle::Chevron:
                separatorLayout.reset();
                return style.separatorSize;
            case BreadcrumbSeparatorStyle::ChevronDouble:
                separatorLayout.reset();
                return style.separatorSize * 2 - 2;
            case BreadcrumbSeparatorStyle::CustomIcon: {
                separatorLayout.reset();
                if (!style.customSeparatorIcon) return 0;
                return style.iconSize;
            }
            default: {
                std::string s = SeparatorTextFor(style.separatorStyle, style.customSeparatorText);
                if (s.empty()) {
                    separatorLayout.reset();
                    return 0;
                }
                separatorLayout = ctx->CreateTextLayout(s, false);
                if (separatorLayout) {
                    separatorLayout->SetFontStyle(style.fontStyle);
                    separatorLayout->SetWrap(TextWrap::WrapNone);
                    return static_cast<int>(separatorLayout->GetLayoutWidth());
                }
                return 0;
            }
        }
    }

    std::unique_ptr<ITextLayout> UltraCanvasBreadcrumb::BuildItemLayout(IRenderContext* ctx,
                                                                       const std::string& text,
                                                                       bool bold,
                                                                       int maxWidth) const {
        auto layout = ctx->CreateTextLayout(text, false);
        if (!layout) return layout;
        FontStyle fs = style.fontStyle;
        if (bold) fs.fontWeight = FontWeight::Bold;
        layout->SetFontStyle(fs);
        layout->SetWrap(TextWrap::WrapNone);
        if (maxWidth > 0) {
            layout->SetEllipsize(EllipsizeMode::EllipsizeEnd);
            layout->SetExplicitWidth(static_cast<double>(maxWidth));
        }
        return layout;
    }

    int UltraCanvasBreadcrumb::ComputeItemSlotWidth(const BreadcrumbItem& item,
                                                   const Size2Dd& textSize,
                                                   bool includeDropdown) const {
        int width = style.itemPaddingHorizontal * 2;
        bool hasIcon = item.icon != nullptr;
        bool hasText = textSize.width > 0.0f;

        if (style.showLevelIndicator) {
            width += style.levelIndicatorSize;
            if (hasIcon || hasText) width += style.iconTextSpacing;
        }
        if (hasIcon) width += style.iconSize;
        if (hasIcon && hasText) width += style.iconTextSpacing;
        if (hasText) width += static_cast<int>(textSize.width);
        if (includeDropdown && item.hasDropdown) {
            width += style.dropdownChevronSpacing + style.dropdownChevronSize;
        }
        return width;
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

        // ===== STEP 1: Build per-item text layouts and measure =====
        // Each layout carries its own font weight (Bold for the current item when enabled),
        // so measurement and render use identical metrics.
        const bool allowInitialEllipsize =
            (style.maxItemTextWidth > 0
             && (style.overflowMode == BreadcrumbOverflowMode::Ellipsize
                 || style.overflowMode == BreadcrumbOverflowMode::ShrinkText
                 || style.overflowMode == BreadcrumbOverflowMode::Collapse));

        std::vector<std::unique_ptr<ITextLayout>> layouts(items.size());
        std::vector<Size2Dd> sizes(items.size());
        std::vector<int> itemWidths(items.size());
        int totalWidth = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            bool bold = (static_cast<int>(i) == currentIdx) && style.currentItemBold;
            int initMax = allowInitialEllipsize ? style.maxItemTextWidth : 0;
            layouts[i] = BuildItemLayout(ctx, items[i].text, bold, initMax);
            sizes[i] = layouts[i] ? layouts[i]->GetLayoutSize() : Size2Dd();
            itemWidths[i] = ComputeItemSlotWidth(items[i], sizes[i], true);
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
        std::unique_ptr<ITextLayout> overflowLayout;

        if (totalWidth > availableWidth) {
            switch (style.overflowMode) {
                case BreadcrumbOverflowMode::Clip:
                    // Nothing — slots will be drawn under clipping by Render().
                    break;

                case BreadcrumbOverflowMode::Ellipsize: {
                    // Tighten each non-current item until things fit.
                    int totalSlack = totalWidth - availableWidth;
                    for (size_t i = 0; i < items.size() && totalSlack > 0; ++i) {
                        if (i == static_cast<size_t>(currentIdx)) continue; // never shrink current
                        if (!layouts[i]) continue;
                        int currentTextW = static_cast<int>(sizes[i].width);
                        if (currentTextW <= 0) continue;
                        int newMax = std::max(8, currentTextW - totalSlack);
                        layouts[i]->SetEllipsize(EllipsizeMode::EllipsizeEnd);
                        layouts[i]->SetExplicitWidth(static_cast<double>(newMax));
                        sizes[i] = layouts[i]->GetLayoutSize();
                        int newWidth = ComputeItemSlotWidth(items[i], sizes[i], true);
                        totalSlack -= (itemWidths[i] - newWidth);
                        itemWidths[i] = newWidth;
                    }
                    break;
                }

                case BreadcrumbOverflowMode::ShrinkText: {
                    // Apply a uniform per-item text budget.
                    int perItemBudget = std::max(20, availableWidth / std::max<int>(1, (int)items.size()));
                    for (size_t i = 0; i < items.size(); ++i) {
                        if (!layouts[i]) continue;
                        layouts[i]->SetEllipsize(EllipsizeMode::EllipsizeEnd);
                        layouts[i]->SetExplicitWidth(static_cast<double>(perItemBudget));
                        sizes[i] = layouts[i]->GetLayoutSize();
                        itemWidths[i] = ComputeItemSlotWidth(items[i], sizes[i], true);
                    }
                    break;
                }

                case BreadcrumbOverflowMode::Collapse: {
                    needsOverflow = true;
                    // Build the "..." overflow placeholder layout once.
                    overflowLayout = BuildItemLayout(ctx, style.overflowEllipsisText, false, 0);
                    int overflowTextW = overflowLayout
                                        ? static_cast<int>(overflowLayout->GetLayoutWidth())
                                        : 0;
                    int overflowBaseWidth = style.itemPaddingHorizontal * 2
                                            + overflowTextW
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

        // Arrow/Parallelogram styles: segments butt against each other (no separator/gap)
        // and each non-first segment reserves a left inset so its content clears the
        // previous segment's tip/slant.
        const bool segmentStyle = (style.itemStyle == BreadcrumbItemStyle::Arrow
                                   || style.itemStyle == BreadcrumbItemStyle::Parallelogram);
        const int arrowDepth = segmentStyle ? std::max(0, style.arrowSize) : 0;

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
                Size2Dd oTextSize = overflowLayout ? overflowLayout->GetLayoutSize() : Size2Dd();
                int textW = static_cast<int>(oTextSize.width);
                int oWidth = style.itemPaddingHorizontal * 2 + textW
                             + style.dropdownChevronSpacing + style.dropdownChevronSize;
                oslot.rect = Rect2Di(x, centerY - slotHeight / 2, oWidth, slotHeight);
                int innerX = x + style.itemPaddingHorizontal;
                // Center the glyphs vertically via the text layout itself (full
                // precision) so they share the exact center line of separators/icons.
                if (overflowLayout) {
                    overflowLayout->SetVerticalAlignment(VerticalAlignment::Middle);
                    overflowLayout->SetExplicitHeight(static_cast<double>(slotHeight));
                }
                oslot.textRect = Rect2Di(innerX, oslot.rect.y, textW, slotHeight);
                innerX += textW + style.dropdownChevronSpacing;
                oslot.dropdownRect = Rect2Di(innerX,
                                             centerY - style.dropdownChevronSize / 2,
                                             style.dropdownChevronSize,
                                             style.dropdownChevronSize);
                oslot.displayText = style.overflowEllipsisText;
                oslot.textSize = oTextSize;
                oslot.textLayout = std::move(overflowLayout);
                slots.emplace_back(std::move(oslot));
                x += oWidth;
                overflowEmitted = true;
            }

            if (!visible[i]) continue;

            if (!slots.empty() && !segmentStyle) {
                x += style.separatorSpacing;
                x += separatorWidth + style.separatorSpacing;
            }

            ItemSlot slot;
            slot.itemIndex = static_cast<int>(i);
            slot.displayText = items[i].text;
            slot.isCurrent = (static_cast<int>(i) == currentIdx);
            // Non-first segments carve a left notch/slant; widen the body and push the
            // content right by that depth so text/icons never sit under the wedge.
            int leftNotch = (segmentStyle && !slots.empty()) ? arrowDepth : 0;
            int width = itemWidths[i] + leftNotch;
            slot.rect = Rect2Di(x, centerY - slotHeight / 2, width, slotHeight);

            int innerX = x + style.itemPaddingHorizontal + leftNotch;
            if (style.showLevelIndicator) {
                slot.indicatorRect = Rect2Di(innerX, centerY - style.levelIndicatorSize / 2,
                                             style.levelIndicatorSize, style.levelIndicatorSize);
                innerX += style.levelIndicatorSize;
                if (items[i].icon || sizes[i].width > 0.0f) innerX += style.iconTextSpacing;
            }
            if (items[i].icon) {
                slot.iconRect = Rect2Di(innerX, centerY - style.iconSize / 2,
                                        style.iconSize, style.iconSize);
                innerX += style.iconSize;
                if (sizes[i].width > 0.0f) innerX += style.iconTextSpacing;
            }
            if (sizes[i].width > 0.0f) {
                int textW = static_cast<int>(sizes[i].width);
                // Delegate vertical centering to the text layout (VerticalAlignment::Middle
                // over the full slot height). This keeps the glyphs on the exact same
                // center line as the separators and icons; the previous hand-rolled
                // "centerY - (int)textH / 2" truncated the half-height to an integer and
                // drifted the text ~1px off-center against the item/strip backgrounds.
                if (layouts[i]) {
                    layouts[i]->SetVerticalAlignment(VerticalAlignment::Middle);
                    layouts[i]->SetExplicitHeight(static_cast<double>(slotHeight));
                }
                slot.textRect = Rect2Di(innerX, slot.rect.y, textW, slotHeight);
                innerX += textW;
            }
            if (items[i].hasDropdown) {
                innerX += style.dropdownChevronSpacing;
                slot.dropdownRect = Rect2Di(innerX,
                                            centerY - style.dropdownChevronSize / 2,
                                            style.dropdownChevronSize,
                                            style.dropdownChevronSize);
            }
            slot.textSize = sizes[i];
            slot.textLayout = std::move(layouts[i]);
            slots.emplace_back(std::move(slot));
            x += width;
        }

        ctx->PopState();
        layoutDirty = false;
    }

// ===== CSS LAYOUT: MEASURE / ARRANGE =====

    // Natural content size (all items uncollapsed), excluding padding/border.
    Size2Df UltraCanvasBreadcrumb::MeasureContentSize(IRenderContext* ctx) const {
        // Height: one row from text/icon height + vertical item padding.
        int textH = ctx ? ctx->GetTextLineHeight("Mg") : (int)style.fontStyle.fontSize;
        float contentH = (float)(std::max<int>(textH, style.iconSize) + style.itemPaddingVertical * 2);

        if (!ctx || items.empty()) {
            return Size2Df(0.f, contentH);
        }

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        // MeasureSeparator mutates cached separator layout state; const-cast to reuse it.
        int sepW = const_cast<UltraCanvasBreadcrumb*>(this)->MeasureSeparator(ctx);
        int currentIdx = ResolvedCurrentIndex();
        int total = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            bool bold = (static_cast<int>(i) == currentIdx) && style.currentItemBold;
            auto layout = BuildItemLayout(ctx, items[i].text, bold, style.maxItemTextWidth);
            Size2Dd sz = layout ? layout->GetLayoutSize() : Size2Dd();
            total += ComputeItemSlotWidth(items[i], sz, true);
            if (i + 1 < items.size()) {
                total += sepW + style.separatorSpacing * 2;
            }
        }
        ctx->PopState();
        return Size2Df((float)total, contentH);
    }

    Size2Df UltraCanvasBreadcrumb::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                                     const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            // No surface yet — report no own content; the block path resolves
            // from size.width/height or the incoming constraints.
            return Size2Df(0.f, 0.f);
        }
        // Content box: all items uncollapsed (width) + one row height. The block
        // layout adds padding/border and applies size.*/constraints.
        return MeasureContentSize(rc);
    }

    void UltraCanvasBreadcrumb::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
            intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
            return;
        }
        const float padH = GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        const float padV = GetTotalPaddingVertical()   + GetTotalBorderVertical();
        Size2Df content = MeasureContentSize(rc);
        // Preferred == max-content (the breadcrumb collapses internally at arrange time).
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = content.width  + padH;
        intrinsic.minContentWidth  = content.width  + padH;
        intrinsic.maxContentHeight = content.height + padV;
        intrinsic.minContentHeight = content.height + padV;
    }

    void UltraCanvasBreadcrumb::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasUIElement::Arrange(finalRect, ctx);   // sets finalBounds + damage
        // Recompute slots/overflow against the resolved content rect.
        if (IRenderContext* rc = GetRenderContext()) {
            RecalculateLayout(rc);
            layoutDirty = false;
        }
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
    void UltraCanvasBreadcrumb::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Layout is normally computed in Arrange(); recompute here only as a safety
        // net if something dirtied it without an intervening relayout pass.
        if (layoutDirty) {
            RecalculateLayout(ctx);
        }

        // Draw the strip background using the base implementation, then our background color.
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        RenderBackground(ctx);

        if (slots.empty()) return;

        ctx->PushState();
        // Clip to content area to avoid bleeding for Clip overflow mode.
        Rect2Di content = GetLocalContentRect();
        ctx->ClipRect(Rect2Dd(content.x, content.y, content.width, content.height));

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
        ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height),
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
            || style.itemStyle == BreadcrumbItemStyle::Tab
            || style.itemStyle == BreadcrumbItemStyle::Arrow
            || style.itemStyle == BreadcrumbItemStyle::Parallelogram) {
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
            bool isLastSlot = (slotIdx == (int)slots.size() - 1);
            if (style.itemStyle == BreadcrumbItemStyle::Arrow) {
                // First segment has a flat left edge; every segment grows a right tip
                // that nests into the next segment's notch (the last one's tip trails off).
                RenderArrowBackground(ctx, slot.rect, /*leftNotch*/ slotIdx > 0,
                                      /*rightTip*/ true, bgColor);
            } else if (style.itemStyle == BreadcrumbItemStyle::Parallelogram) {
                // Outer edges of the first/last segment stay vertical; inner edges slant.
                RenderParallelogramBackground(ctx, slot.rect, /*leftSlant*/ slotIdx > 0,
                                              /*rightSlant*/ !isLastSlot, bgColor);
            } else {
                Rect2Di r = slot.rect;
                ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                                         bgColor, 0.0f, Colors::Transparent,
                                         static_cast<float>(style.itemCornerRadius));
            }
        }

        // Leading level indicator badge
        if (style.showLevelIndicator && !isOverflow && slot.indicatorRect.width > 0) {
            RenderLevelIndicator(ctx, slot);
        }

        // Tab-style underline strip
        if (style.itemStyle == BreadcrumbItemStyle::Tab && isCurrent) {
            int y = slot.rect.y + slot.rect.height - 2;
            ctx->SetStrokePaint(style.currentItemTextColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLine(Point2Dd(slot.rect.x + 2, y),
                          Point2Dd(slot.rect.x + slot.rect.width - 2, y));
        }

        // Icon
        if (!isOverflow && slot.itemIndex >= 0 && slot.itemIndex < (int)items.size()
            && items[slot.itemIndex].icon && slot.iconRect.width > 0) {
            ctx->DrawImage(*items[slot.itemIndex].icon.get(),
                           Rect2Dd(slot.iconRect.x, slot.iconRect.y,
                                   slot.iconRect.width, slot.iconRect.height),
                           ImageFitMode::Contain);
        }

        // Text — render the pre-built layout (font weight, ellipsization already baked in).
        if (slot.textLayout && slot.textRect.width > 0) {
            ctx->SetTextPaint(textColor);
            ctx->DrawTextLayout(*slot.textLayout,
                                Point2Dd(slot.textRect.x, slot.textRect.y));

            // Underline
            bool drawUnderline = false;
            if (style.itemStyle == BreadcrumbItemStyle::Underline) {
                if (style.underlineCurrent && isCurrent) drawUnderline = true;
                if (style.underlineOnHover && isHovered && clickable) drawUnderline = true;
            } else {
                if (style.underlineOnHover && isHovered && clickable) drawUnderline = true;
            }
            if (drawUnderline) {
                // textRect spans the full slot height (text is centered inside it by the
                // layout), so derive the glyph bottom from the layout's vertical offset
                // rather than the rect bottom to keep the underline hugging the text.
                int textBottom = slot.textRect.y
                                 + static_cast<int>(slot.textLayout->GetLayoutVerticalOffset())
                                 + static_cast<int>(slot.textSize.height);
                int y = textBottom + 1;
                ctx->SetStrokePaint(textColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(Point2Dd(slot.textRect.x, y),
                              Point2Dd(slot.textRect.x + slot.textRect.width, y));
            }
        }

        // Dropdown chevron
        if (slot.dropdownRect.width > 0) {
            RenderDropdownChevron(ctx, slot.dropdownRect, textColor);
        }
    }

    void UltraCanvasBreadcrumb::RenderArrowBackground(IRenderContext* ctx, const Rect2Di& rect,
                                                      bool leftNotch, bool rightTip,
                                                      const Color& fillColor) {
        const double a = std::max(0, style.arrowSize);
        const double left = rect.x;
        const double right = rect.x + rect.width;
        const double top = rect.y;
        const double bottom = rect.y + rect.height;
        const double midY = rect.y + rect.height / 2.0;

        // Outline clockwise from the top-left corner.
        ctx->ClearPath();
        ctx->MoveTo(left, top);
        ctx->LineTo(right, top);
        if (rightTip && a > 0.0) {
            ctx->LineTo(right + a, midY);   // pointed tip past the right edge
        }
        ctx->LineTo(right, bottom);
        ctx->LineTo(left, bottom);
        if (leftNotch && a > 0.0) {
            ctx->LineTo(left + a, midY);    // concave wedge matching the previous tip
        }
        ctx->ClosePath();

        ctx->SetFillPaint(fillColor);
        ctx->FillPathPreserve();
        if (style.separatorColor.a > 0 && style.separatorThickness > 0.0f) {
            ctx->SetStrokePaint(style.separatorColor);
            ctx->SetStrokeWidth(style.separatorThickness);
            ctx->SetLineJoin(LineJoin::Miter);
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void UltraCanvasBreadcrumb::RenderParallelogramBackground(IRenderContext* ctx, const Rect2Di& rect,
                                                              bool leftSlant, bool rightSlant,
                                                              const Color& fillColor) {
        const double s = std::max(0, style.arrowSize);
        const double left = rect.x;
        const double right = rect.x + rect.width;
        const double top = rect.y;
        const double bottom = rect.y + rect.height;

        // Slanted edges shift the top corners right by the skew; the previous segment's
        // right slant exactly fills this segment's left slant, so they tessellate.
        ctx->ClearPath();
        ctx->MoveTo(leftSlant ? left + s : left, top);
        ctx->LineTo(rightSlant ? right + s : right, top);
        ctx->LineTo(right, bottom);
        ctx->LineTo(left, bottom);
        ctx->ClosePath();

        ctx->SetFillPaint(fillColor);
        ctx->FillPathPreserve();
        if (style.separatorColor.a > 0 && style.separatorThickness > 0.0f) {
            ctx->SetStrokePaint(style.separatorColor);
            ctx->SetStrokeWidth(style.separatorThickness);
            ctx->SetLineJoin(LineJoin::Miter);
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void UltraCanvasBreadcrumb::RenderLevelIndicator(IRenderContext* ctx, const ItemSlot& slot) {
        const Rect2Di& r = slot.indicatorRect;
        Point2Dd center(r.x + r.width / 2.0, r.y + r.height / 2.0);

        const Color border = style.levelIndicatorBorder ? style.levelIndicatorBorderColor
                                                         : Colors::Transparent;
        const float borderW = style.levelIndicatorBorder ? style.levelIndicatorBorderWidth : 0.0f;

        switch (style.levelIndicatorBackground) {
            case BreadcrumbLevelIndicatorBackground::Round:
                ctx->DrawFilledCircle(center, r.width / 2.0f,
                                      style.levelIndicatorColor, border, borderW);
                break;
            case BreadcrumbLevelIndicatorBackground::Rectangle:
                ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                                         style.levelIndicatorColor, borderW, border,
                                         std::max(2.0f, r.width * 0.22f));
                break;
            case BreadcrumbLevelIndicatorBackground::NoBackground:
                break;  // number only
        }

        // Centered level number (1-based).
        ctx->PushState();
        FontStyle fs = style.fontStyle;
        fs.fontSize = std::max(8.0f, r.height * 0.58f);
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(style.levelIndicatorTextColor);
        ctx->DrawTextInRect(std::to_string(slot.itemIndex + 1),
                            Rect2Dd(r.x, r.y, r.width, r.height));
        ctx->PopState();
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
                               Rect2Dd(x - s / 2.0f, centerY - s / 2.0f, s, s),
                               ImageFitMode::Contain);
                return;
            }
            default: {
                if (!separatorLayout) return;
                ctx->SetTextPaint(style.separatorColor);
                Size2Dd sz = separatorLayout->GetLayoutSize();
                ctx->DrawTextLayout(*separatorLayout,
                                    Point2Dd(x - sz.width / 2.0f, centerY - sz.height / 2.0f));
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
