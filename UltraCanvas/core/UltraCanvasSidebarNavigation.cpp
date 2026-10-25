// core/UltraCanvasSidebarNavigation.cpp
// The navigation column: sections, items, selection, keyboard, scrolling,
// compact mode (UltraCanvasSidebarNavigation.h).
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasSidebarNavigation.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    SidebarStyle SidebarStyle::Default() {
        SidebarStyle s;
        s.itemFont.fontSize = 13.0;
        s.sectionFont.fontSize = 11.0;
        s.sectionFont.fontWeight = FontWeight::Bold;
        s.badgeFont.fontSize = 11.0;
        s.badgeFont.fontWeight = FontWeight::Bold;
        return s;
    }

    UltraCanvasSidebarNavigation::UltraCanvasSidebarNavigation(const std::string& identifier, float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h), style(SidebarStyle::Default()) {
        if (w <= 0) SetWidth(style.expandedWidth);
        // In a window's row the column keeps its width; the page beside it gives way.
        layoutItem.SetFlexShrink(0);
        mouseCursor = UCMouseCursor::Default;
    }

    // ----- sections and items -----
    int UltraCanvasSidebarNavigation::UntitledSection() {
        if (sections.empty() || !sections.front().title.empty()) {
            sections.insert(sections.begin(), SidebarSection{});
        }
        return 0;
    }

    int UltraCanvasSidebarNavigation::AddSection(const std::string& title) {
        sections.push_back(SidebarSection{title, {}});
        InvalidateLayout();
        RequestRedraw();
        return static_cast<int>(sections.size()) - 1;
    }

    std::string UltraCanvasSidebarNavigation::AddItem(int sectionIndex, const SidebarItem& item) {
        if (item.id.empty()) return {};
        if (SidebarItem* existing = Find(item.id)) {
            *existing = item;
            RequestRedraw();
            return item.id;
        }
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) sectionIndex = UntitledSection();
        sections[sectionIndex].items.push_back(item);
        InvalidateLayout();
        RequestRedraw();
        return item.id;
    }

    std::string UltraCanvasSidebarNavigation::AddItem(int sectionIndex, const std::string& id, const std::string& label,
                                                      const std::string& iconPath) {
        SidebarItem item;
        item.id = id;
        item.label = label;
        item.iconPath = iconPath;
        return AddItem(sectionIndex, item);
    }

    std::string UltraCanvasSidebarNavigation::AddItem(const std::string& id, const std::string& label, const std::string& iconPath) {
        return AddItem(UntitledSection(), id, label, iconPath);
    }

    bool UltraCanvasSidebarNavigation::RemoveItem(const std::string& id) {
        for (auto& section : sections) {
            auto it = std::find_if(section.items.begin(), section.items.end(), [&](const SidebarItem& i) { return i.id == id; });
            if (it != section.items.end()) {
                section.items.erase(it);
                if (selectedId == id) { selectedId.clear(); if (onSelectionChanged) onSelectionChanged(selectedId); }
                if (hoveredId == id) hoveredId.clear();
                InvalidateLayout();
                RequestRedraw();
                return true;
            }
        }
        return false;
    }

    void UltraCanvasSidebarNavigation::Clear() {
        sections.clear();
        hoveredId.clear();
        pressedId.clear();
        scrollY = 0.0f;
        const bool had = !selectedId.empty();
        selectedId.clear();
        InvalidateLayout();
        RequestRedraw();
        if (had && onSelectionChanged) onSelectionChanged(selectedId);
    }

    int UltraCanvasSidebarNavigation::GetItemCount() const {
        int n = 0;
        for (const auto& s : sections) n += static_cast<int>(s.items.size());
        return n;
    }

    SidebarItem* UltraCanvasSidebarNavigation::Find(const std::string& id, int* sectionOut, int* itemOut) {
        for (size_t s = 0; s < sections.size(); ++s) {
            for (size_t i = 0; i < sections[s].items.size(); ++i) {
                if (sections[s].items[i].id == id) {
                    if (sectionOut) *sectionOut = static_cast<int>(s);
                    if (itemOut) *itemOut = static_cast<int>(i);
                    return &sections[s].items[i];
                }
            }
        }
        return nullptr;
    }

    const SidebarItem* UltraCanvasSidebarNavigation::FindItem(const std::string& id) const {
        return const_cast<UltraCanvasSidebarNavigation*>(this)->Find(id);
    }

    std::vector<std::string> UltraCanvasSidebarNavigation::GetItemIds() const {
        std::vector<std::string> out;
        for (const auto& s : sections) for (const auto& i : s.items) out.push_back(i.id);
        return out;
    }

    void UltraCanvasSidebarNavigation::SetBadge(const std::string& id, int count) {
        SetBadgeText(id, count > 0 ? std::to_string(count) : std::string());
    }
    void UltraCanvasSidebarNavigation::SetBadgeText(const std::string& id, const std::string& text) {
        if (SidebarItem* item = Find(id)) { item->badgeText = text; RequestRedraw(); }
    }
    void UltraCanvasSidebarNavigation::SetItemEnabled(const std::string& id, bool enabled) {
        if (SidebarItem* item = Find(id)) { item->enabled = enabled; RequestRedraw(); }
    }
    void UltraCanvasSidebarNavigation::SetItemLabel(const std::string& id, const std::string& label) {
        if (SidebarItem* item = Find(id)) { item->label = label; RequestRedraw(); }
    }
    void UltraCanvasSidebarNavigation::SetItemIcon(const std::string& id, const std::string& iconPath) {
        if (SidebarItem* item = Find(id)) { item->iconPath = iconPath; RequestRedraw(); }
    }
    void UltraCanvasSidebarNavigation::SetItemTooltip(const std::string& id, const std::string& tooltip) {
        if (SidebarItem* item = Find(id)) item->tooltip = tooltip;
    }
    void UltraCanvasSidebarNavigation::SetSectionTitle(int sectionIndex, const std::string& title) {
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) return;
        sections[sectionIndex].title = title;
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- selection -----
    bool UltraCanvasSidebarNavigation::Select(const std::string& id, bool fireCallback) {
        if (!id.empty()) {
            const SidebarItem* item = FindItem(id);
            if (!item || !item->enabled) return false;
        }
        if (selectedId == id) return true;
        selectedId = id;
        EnsureVisible(id);
        RequestRedraw();
        if (fireCallback && onSelectionChanged) onSelectionChanged(selectedId);
        return true;
    }

    bool UltraCanvasSidebarNavigation::SelectNext() {
        const std::vector<std::string> ids = GetItemIds();
        auto start = std::find(ids.begin(), ids.end(), selectedId);
        size_t from = start == ids.end() ? 0 : static_cast<size_t>(start - ids.begin()) + 1;
        for (size_t i = from; i < ids.size(); ++i) if (FindItem(ids[i])->enabled) return Select(ids[i]);
        return false;
    }

    bool UltraCanvasSidebarNavigation::SelectPrevious() {
        const std::vector<std::string> ids = GetItemIds();
        auto start = std::find(ids.begin(), ids.end(), selectedId);
        if (start == ids.end() || start == ids.begin()) return false;
        for (auto it = start; it != ids.begin();) {
            --it;
            if (FindItem(*it)->enabled) return Select(*it);
        }
        return false;
    }

    // ----- compact mode -----
    void UltraCanvasSidebarNavigation::SetCompact(bool value) {
        if (compact == value) return;
        compact = value;
        SetWidth(PreferredWidth());
        InvalidateLayout();
        RequestRedraw();
        if (onCompactChanged) onCompactChanged(compact);
    }

    void UltraCanvasSidebarNavigation::SetStyle(const SidebarStyle& newStyle) {
        style = newStyle;
        SetWidth(PreferredWidth());
        InvalidateLayout();
        RequestRedraw();
    }

    // ----- geometry -----
    std::vector<UltraCanvasSidebarNavigation::Row> UltraCanvasSidebarNavigation::LayoutRows() const {
        std::vector<Row> rows;
        const float w = std::max(0.0f, GetWidth() - 2.0f * style.paddingH);
        float y = style.paddingV;
        for (size_t s = 0; s < sections.size(); ++s) {
            const SidebarSection& section = sections[s];
            if (!section.title.empty()) {
                if (s > 0) y += style.sectionGap;
                // Compact: the header shrinks to a thin separator row.
                const float h = compact ? style.sectionGap : style.sectionHeight;
                rows.push_back({static_cast<int>(s), -1, Rect2Df(style.paddingH, y, w, h)});
                y += h;
            }
            for (size_t i = 0; i < section.items.size(); ++i) {
                rows.push_back({static_cast<int>(s), static_cast<int>(i), Rect2Df(style.paddingH, y, w, style.itemHeight)});
                y += style.itemHeight;
            }
        }
        return rows;
    }

    float UltraCanvasSidebarNavigation::ContentHeight() const {
        const std::vector<Row> rows = LayoutRows();
        if (rows.empty()) return 2.0f * style.paddingV;
        const Rect2Df& last = rows.back().rect;
        return last.y + last.height + style.paddingV;
    }

    const UltraCanvasSidebarNavigation::Row* UltraCanvasSidebarNavigation::RowAt(const Point2Df& local,
                                                                                 const std::vector<Row>& rows) const {
        const Point2Df p(local.x, local.y + scrollY);
        for (const Row& r : rows) if (r.item >= 0 && r.rect.Contains(p)) return &r;
        return nullptr;
    }

    Rect2Df UltraCanvasSidebarNavigation::ItemRect(const std::string& id) const {
        for (const Row& r : LayoutRows()) {
            if (r.item >= 0 && sections[r.section].items[r.item].id == id) {
                return Rect2Df(r.rect.x, r.rect.y - scrollY, r.rect.width, r.rect.height);
            }
        }
        return Rect2Df(0, 0, 0, 0);
    }

    void UltraCanvasSidebarNavigation::ClampScroll() {
        const float maxScroll = std::max(0.0f, ContentHeight() - GetHeight());
        scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    }

    void UltraCanvasSidebarNavigation::EnsureVisible(const std::string& id) {
        if (GetHeight() <= 0.0f) return;
        for (const Row& r : LayoutRows()) {
            if (r.item < 0 || sections[r.section].items[r.item].id != id) continue;
            if (r.rect.y < scrollY) scrollY = r.rect.y - style.paddingV;
            else if (r.rect.y + r.rect.height > scrollY + GetHeight()) scrollY = r.rect.y + r.rect.height + style.paddingV - GetHeight();
            ClampScroll();
            return;
        }
    }

    void UltraCanvasSidebarNavigation::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        const float padH = static_cast<float>(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal());
        const float padV = static_cast<float>(GetTotalPaddingVertical() + GetTotalBorderVertical());
        intrinsic.valid = true;
        intrinsic.minContentWidth = intrinsic.maxContentWidth = PreferredWidth() + padH;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = ContentHeight() + padV;
    }

    Size2Df UltraCanvasSidebarNavigation::MeasureOwnContent(std::optional<float> definiteContentWidth,
                                                            const CSSLayout::LayoutContext& /*ctx*/) {
        const float w = definiteContentWidth.has_value() ? std::max(0.0f, *definiteContentWidth) : PreferredWidth();
        return Size2Df(w, ContentHeight());
    }

    std::shared_ptr<UCImage> UltraCanvasSidebarNavigation::IconFor(const SidebarItem& item) {
        if (item.iconPath.empty()) return nullptr;
        for (const auto& [path, image] : iconCache) if (path == item.iconPath) return image;
        auto image = UCImage::Get(item.iconPath);
        iconCache.emplace_back(item.iconPath, image);
        return image;
    }

    // ----- drawing -----
    void UltraCanvasSidebarNavigation::DrawBadge(IRenderContext* ctx, const std::string& text, float rightX, float centerY) {
        ctx->SetFontStyle(style.badgeFont);
        const Point2Di ts = ctx->GetTextDimension(text);
        const float w = std::max(style.badgeMinWidth, static_cast<float>(ts.x) + 2.0f * style.badgePaddingH);
        const float h = style.badgeHeight;
        const Rect2Dd r(rightX - w, centerY - h / 2.0f, w, h);
        ctx->DrawFilledRectangle(r, style.badgeBackgroundColor, 0.0f, Colors::Transparent, h / 2.0f);
        ctx->SetTextPaint(style.badgeTextColor);
        ctx->DrawText(text, Point2Dd(r.x + (w - ts.x) / 2.0f, centerY - ts.y / 2.0f));
    }

    void UltraCanvasSidebarNavigation::DrawSection(IRenderContext* ctx, const SidebarSection& section, const Rect2Df& r) {
        if (compact) {
            ctx->SetStrokePaint(style.sectionTextColor);
            ctx->SetStrokeWidth(1.0f);
            const float y = r.y + r.height / 2.0f;
            ctx->DrawLine(Point2Dd(r.x + 8, y), Point2Dd(r.x + r.width - 8, y));
            return;
        }
        ctx->SetFontStyle(style.sectionFont);
        ctx->SetTextPaint(style.sectionTextColor);
        const Point2Di ts = ctx->GetTextDimension(section.title);
        ctx->DrawText(section.title, Point2Dd(r.x + style.itemPaddingH, r.y + r.height - ts.y - 3.0f));
    }

    void UltraCanvasSidebarNavigation::DrawItem(IRenderContext* ctx, const SidebarItem& item, const Rect2Df& r,
                                                bool selected, bool hovered) {
        const bool pressed = pressedId == item.id;
        if (selected) {
            ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height), style.selectedBackgroundColor, 0.0f,
                                     Colors::Transparent, style.cornerRadius);
            ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y + r.height * 0.2f, style.indicatorWidth, r.height * 0.6f),
                                     style.indicatorColor, 0.0f, Colors::Transparent, style.indicatorWidth / 2.0f);
        } else if ((hovered || pressed) && item.enabled) {
            ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height), style.hoverBackgroundColor, 0.0f,
                                     Colors::Transparent, style.cornerRadius);
        }
        const Color text = !item.enabled ? style.disabledTextColor : (selected ? style.selectedTextColor : style.textColor);
        const float cy = r.y + r.height / 2.0f;
        float x = r.x + style.itemPaddingH;
        if (compact) x = r.x + (r.width - style.iconSize) / 2.0f;

        if (auto icon = IconFor(item)) {
            if (!item.enabled) ctx->SetAlpha(0.45);
            ctx->DrawImage(*icon, Rect2Dd(x, cy - style.iconSize / 2.0f, style.iconSize, style.iconSize), ImageFitMode::Contain);
            if (!item.enabled) ctx->SetAlpha(1.0);
        }
        if (compact) {
            if (!item.badgeText.empty()) {
                // A dot at the icon's top right.
                ctx->SetFillPaint(style.badgeBackgroundColor);
                ctx->FillCircle(Point2Dd(x + style.iconSize + 1.0, cy - style.iconSize / 2.0 + 1.0), 4.0);
            }
            return;
        }
        x += (item.iconPath.empty() ? 0.0f : style.iconSize + style.iconGap);

        float right = r.x + r.width - style.itemPaddingH;
        if (!item.badgeText.empty()) {
            DrawBadge(ctx, item.badgeText, right, cy);
            ctx->SetFontStyle(style.badgeFont);
            const Point2Di bs = ctx->GetTextDimension(item.badgeText);
            right -= std::max(style.badgeMinWidth, static_cast<float>(bs.x) + 2.0f * style.badgePaddingH) + style.iconGap;
        }

        ctx->SetFontStyle(style.itemFont);
        ctx->SetTextPaint(text);
        const Point2Di ts = ctx->GetTextDimension(item.label);
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(x, r.y, std::max(0.0f, right - x), r.height));
        ctx->DrawText(item.label, Point2Dd(x, cy - ts.y / 2.0f));
        ctx->PopState();
    }

    void UltraCanvasSidebarNavigation::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        const Rect2Df b = GetLocalBounds();
        ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), style.backgroundColor, 0.0f, Colors::Transparent, 0.0f);
        ClampScroll();

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
        ctx->Translate(0.0, -scrollY);
        for (const Row& row : LayoutRows()) {
            const SidebarSection& section = sections[row.section];
            if (row.item < 0) { DrawSection(ctx, section, row.rect); continue; }
            const SidebarItem& item = section.items[row.item];
            DrawItem(ctx, item, row.rect, item.id == selectedId, item.id == hoveredId);
        }
        ctx->PopState();

        if (IsFocused() && !selectedId.empty()) {
            const Rect2Df r = ItemRect(selectedId);
            if (r.width > 0) {
                ctx->DrawFilledRectangle(Rect2Dd(r.x + 0.5, r.y + 0.5, r.width - 1.0, r.height - 1.0), Colors::Transparent,
                                         1.0f, style.focusColor, style.cornerRadius);
            }
        }
    }

    void UltraCanvasSidebarNavigation::ShowItemTooltip(const SidebarItem& item, const Rect2Df& r) {
        if (!GetWindow()) return;
        const std::string text = item.tooltip.empty() ? item.label : item.tooltip;
        if (text.empty()) return;
        const Point2Df winPos = GetPositionInWindow();
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), text,
            Point2Di(static_cast<int>(winPos.x + r.x + r.width), static_cast<int>(winPos.y + r.y)));
        tooltipShownFor = item.id;
    }

    // ----- events -----
    bool UltraCanvasSidebarNavigation::OnEvent(const UCEvent& event) {
        if (!IsVisible()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;
        const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));

        switch (event.type) {
            case UCEventType::MouseMove: {
                const std::vector<Row> rows = LayoutRows();
                const Row* row = RowAt(p, rows);
                const std::string id = row ? sections[row->section].items[row->item].id : std::string();
                if (id != hoveredId) {
                    hoveredId = id;
                    const SidebarItem* item = row ? &sections[row->section].items[row->item] : nullptr;
                    SetMouseCursor(item && item->enabled ? UCMouseCursor::Hand : UCMouseCursor::Default);
                    if (compact && item) ShowItemTooltip(*item, Rect2Df(row->rect.x, row->rect.y - scrollY, row->rect.width, row->rect.height));
                    else if (!tooltipShownFor.empty()) { UltraCanvasTooltipManager::HideTooltip(); tooltipShownFor.clear(); }
                    RequestRedraw();
                }
                return true;
            }
            case UCEventType::MouseLeave:
                hoveredId.clear();
                pressedId.clear();
                if (!tooltipShownFor.empty()) { UltraCanvasTooltipManager::HideTooltip(); tooltipShownFor.clear(); }
                SetMouseCursor(UCMouseCursor::Default);
                RequestRedraw();
                return true;
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return false;
                const std::vector<Row> rows = LayoutRows();
                const Row* row = RowAt(p, rows);
                if (!row) return false;
                const SidebarItem& item = sections[row->section].items[row->item];
                if (!item.enabled) return true;
                pressedId = item.id;
                SetFocus(true);
                Select(item.id);
                return true;
            }
            case UCEventType::MouseUp:
                if (!pressedId.empty()) { pressedId.clear(); RequestRedraw(); return true; }
                return false;
            case UCEventType::MouseDoubleClick: {
                const std::vector<Row> rows = LayoutRows();
                const Row* row = RowAt(p, rows);
                if (!row) return false;
                const SidebarItem& item = sections[row->section].items[row->item];
                if (item.enabled && onItemActivated) onItemActivated(item.id);
                return true;
            }
            case UCEventType::MouseWheel: {
                if (ContentHeight() <= GetHeight()) return false;
                scrollY -= static_cast<float>(event.wheelDelta) * style.itemHeight;
                ClampScroll();
                RequestRedraw();
                return true;
            }
            case UCEventType::KeyDown:
                if (!IsFocused()) return false;
                switch (event.virtualKey) {
                    case UCKeys::Down:   return SelectNext() || true;
                    case UCKeys::Up:     return SelectPrevious() || true;
                    case UCKeys::Home: {
                        for (const std::string& id : GetItemIds()) if (FindItem(id)->enabled) { Select(id); break; }
                        return true;
                    }
                    case UCKeys::End: {
                        const std::vector<std::string> ids = GetItemIds();
                        for (auto it = ids.rbegin(); it != ids.rend(); ++it) if (FindItem(*it)->enabled) { Select(*it); break; }
                        return true;
                    }
                    case UCKeys::Return:
                    case UCKeys::Space:
                        if (!selectedId.empty() && onItemActivated) onItemActivated(selectedId);
                        return true;
                    default:
                        return false;
                }
            default:
                return false;
        }
    }

} // namespace UltraCanvas
