// core/UltraCanvasPagination.cpp
// Platform-independent pagination control implementation.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasPagination.h"
#include "UltraCanvasApplication.h"
#include <cmath>
#include <string>

namespace UltraCanvas {

    UltraCanvasPagination::UltraCanvasPagination(const std::string& identifier,
                                                 float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

// ===================================================================
// MODEL
// ===================================================================

    void UltraCanvasPagination::ClampCurrent() {
        int maxPage = std::max(1, pageCount);
        currentPage = std::max(1, std::min(currentPage, maxPage));
    }

    void UltraCanvasPagination::RecomputePageCount() {
        if (pageSize > 0 && totalItems > 0) {
            pageCount = std::max(1, (totalItems + pageSize - 1) / pageSize);
        }
        ClampCurrent();
    }

    void UltraCanvasPagination::SetPageCount(int count) {
        pageCount = std::max(1, count);
        ClampCurrent();
        RequestRedraw();
    }

    void UltraCanvasPagination::SetTotalItems(int total) {
        totalItems = std::max(0, total);
        RecomputePageCount();
        RequestRedraw();
    }

    void UltraCanvasPagination::SetPageSize(int size) {
        pageSize = std::max(0, size);
        RecomputePageCount();
        RequestRedraw();
    }

    void UltraCanvasPagination::SetCurrentPage(int page, bool runCallback) {
        int maxPage = std::max(1, pageCount);
        int np = std::max(1, std::min(page, maxPage));
        if (np != currentPage) {
            currentPage = np;
            if (runCallback && onPageChanged) onPageChanged(currentPage);
            RequestRedraw();
        }
    }

    int UltraCanvasPagination::GetPageStartItem() const {
        if (pageSize <= 0) return 0;
        int s = (currentPage - 1) * pageSize + 1;
        if (totalItems > 0) s = std::min(s, totalItems);
        return s;
    }

    int UltraCanvasPagination::GetPageEndItem() const {
        if (pageSize <= 0) return 0;
        int e = currentPage * pageSize;
        if (totalItems > 0) e = std::min(e, totalItems);
        return e;
    }

// ===================================================================
// PAGE WINDOWING (boundary/sibling algorithm, MUI-style)
// ===================================================================

    std::vector<int> UltraCanvasPagination::BuildPageRange() const {
        int count = pageCount;
        if (count <= 0) return {};

        auto range = [](int start, int end) {
            std::vector<int> v;
            for (int i = start; i <= end; ++i) v.push_back(i);
            return v;
        };

        std::vector<int> startPages = range(1, std::min(boundaryCount, count));
        int endStart = std::max(count - boundaryCount + 1, boundaryCount + 1);
        std::vector<int> endPages = range(endStart, count);

        int siblingsStart = std::max(
            std::min(currentPage - siblingCount, count - boundaryCount - siblingCount * 2 - 1),
            boundaryCount + 2);
        int endFirst = endPages.empty() ? (count - 1) : (endPages.front() - 2);
        int siblingsEnd = std::min(
            std::max(currentPage + siblingCount, boundaryCount + siblingCount * 2 + 2),
            endFirst);

        std::vector<int> items;
        for (int p : startPages) items.push_back(p);

        // Gap (ellipsis) or the single page bridging start boundary and siblings.
        if (siblingsStart > boundaryCount + 2) {
            items.push_back(0);   // ellipsis
        } else if (boundaryCount + 1 < count - boundaryCount) {
            items.push_back(boundaryCount + 1);
        }

        for (int i = siblingsStart; i <= siblingsEnd; ++i) items.push_back(i);

        // Gap (ellipsis) or the single page bridging siblings and end boundary.
        if (siblingsEnd < count - boundaryCount - 1) {
            items.push_back(0);   // ellipsis
        } else if (count - boundaryCount > boundaryCount) {
            items.push_back(count - boundaryCount);
        }

        for (int p : endPages) items.push_back(p);
        return items;
    }

    std::string UltraCanvasPagination::BuildInfoText() const {
        if (infoFormatter) return infoFormatter(currentPage, pageCount, totalItems, pageSize);
        if (totalItems > 0 && pageSize > 0) {
            // "1–10 of 95" (en dash U+2013)
            return std::to_string(GetPageStartItem()) + "\xE2\x80\x93" +
                   std::to_string(GetPageEndItem()) + " of " + std::to_string(totalItems);
        }
        return "Page " + std::to_string(currentPage) + " of " + std::to_string(pageCount);
    }

// ===================================================================
// LAYOUT
// ===================================================================

    void UltraCanvasPagination::ComputeLayout(IRenderContext* ctx, const Rect2Di& bounds) const {
        std::vector<Item> items;

        bool hasPrev = currentPage > 1;
        bool hasNext = currentPage < pageCount;

        auto addNav = [&](ItemType t, int page, bool enabled) {
            Item it;
            it.type = t;
            it.page = page;
            it.enabled = enabled;
            it.actionable = enabled;
            items.push_back(it);
        };
        auto addInfo = [&]() {
            Item it;
            it.type = ItemType::Info;
            it.label = BuildInfoText();
            items.push_back(it);
        };

        if (mode == PaginationMode::Simple) {
            Item prev; prev.type = ItemType::Prev; prev.page = currentPage - 1;
            prev.label = "Prev"; prev.enabled = hasPrev; prev.actionable = hasPrev;
            items.push_back(prev);
            if (showPageInfo) addInfo();
            Item next; next.type = ItemType::Next; next.page = currentPage + 1;
            next.label = "Next"; next.enabled = hasNext; next.actionable = hasNext;
            items.push_back(next);
        } else if (mode == PaginationMode::Compact) {
            if (showFirstLast) addNav(ItemType::First, 1, hasPrev);
            if (showPrevNext)  addNav(ItemType::Prev, currentPage - 1, hasPrev);
            addInfo();
            if (showPrevNext)  addNav(ItemType::Next, currentPage + 1, hasNext);
            if (showFirstLast) addNav(ItemType::Last, pageCount, hasNext);
        } else { // Numbered
            if (showFirstLast) addNav(ItemType::First, 1, hasPrev);
            if (showPrevNext)  addNav(ItemType::Prev, currentPage - 1, hasPrev);
            for (int p : BuildPageRange()) {
                if (p == 0) {
                    Item e; e.type = ItemType::Ellipsis; e.actionable = false;
                    items.push_back(e);
                } else {
                    Item pg; pg.type = ItemType::PageNumber; pg.page = p;
                    pg.enabled = true; pg.actionable = (p != currentPage);
                    items.push_back(pg);
                }
            }
            if (showPrevNext)  addNav(ItemType::Next, currentPage + 1, hasNext);
            if (showFirstLast) addNav(ItemType::Last, pageCount, hasNext);
            if (showPageInfo)  addInfo();
        }

        // Assign rects left-to-right.
        float cellH = std::min(style.cellSize, static_cast<float>(bounds.height));
        if (cellH <= 0) cellH = style.cellSize;
        float y = bounds.y + (bounds.height - cellH) / 2.0f;
        float x = bounds.x;

        if (ctx) ctx->SetFontStyle(style.fontStyle);

        for (auto& it : items) {
            float w = style.cellSize;
            bool textCell = (it.type == ItemType::Info) ||
                            (mode == PaginationMode::Simple &&
                             (it.type == ItemType::Prev || it.type == ItemType::Next));
            if (textCell) {
                int tw = ctx ? ctx->GetTextDimension(it.label).x
                             : static_cast<int>(it.label.size() * 7);
                w = tw + 2 * style.textPaddingH;
                if (it.type == ItemType::Info) w = std::max(w, style.cellSize);
            }
            it.rect = Rect2Di(static_cast<int>(x), static_cast<int>(y),
                              static_cast<int>(w), static_cast<int>(cellH));
            x += w + style.cellSpacing;
        }

        cachedItems = items;
    }

    float UltraCanvasPagination::GetPreferredWidth() const {
        int cells = 0;
        float extra = 0.0f;
        int textCells = 0;

        if (mode == PaginationMode::Simple) {
            textCells = 2;
            extra += 2 * (2 * style.textPaddingH + 34.0f);   // "Prev"/"Next"
            if (showPageInfo) { ++textCells; extra += 2 * style.textPaddingH + 90.0f; }
        } else if (mode == PaginationMode::Compact) {
            cells = (showFirstLast ? 2 : 0) + (showPrevNext ? 2 : 0);
            textCells = 1;
            extra += 2 * style.textPaddingH + 90.0f;
        } else {
            cells = (showFirstLast ? 2 : 0) + (showPrevNext ? 2 : 0) +
                    static_cast<int>(BuildPageRange().size());
            if (showPageInfo) { textCells = 1; extra += 2 * style.textPaddingH + 90.0f; }
        }

        float w = cells * style.cellSize + extra;
        int n = cells + textCells;
        w += std::max(0, n - 1) * style.cellSpacing;
        return w;
    }

// ===================================================================
// RENDERING
// ===================================================================

    void UltraCanvasPagination::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        Rect2Di bounds = GetLocalBounds();
        ComputeLayout(ctx, bounds);
        for (int i = 0; i < static_cast<int>(cachedItems.size()); ++i) {
            RenderCell(ctx, cachedItems[i], i);
        }
    }

    void UltraCanvasPagination::RenderCell(IRenderContext* ctx, const Item& item, int index) {
        ctx->SetFontStyle(style.fontStyle);

        auto drawCentered = [&](const std::string& text, const Color& color) {
            Point2Di ts = ctx->GetTextDimension(text);
            int tx = item.rect.x + (item.rect.width - ts.x) / 2;
            int ty = item.rect.y + (item.rect.height - ts.y) / 2;
            ctx->SetTextPaint(color);
            ctx->DrawText(text, Point2Di(tx, ty));
        };

        if (item.type == ItemType::Ellipsis) {
            drawCentered("\xE2\x80\xA6", style.ellipsisColor);   // … (U+2026)
            return;
        }
        if (item.type == ItemType::Info) {
            drawCentered(item.label, style.infoTextColor);
            return;
        }

        bool isCurrent = (item.type == ItemType::PageNumber && item.page == currentPage);

        Color fill, textCol;
        if (isCurrent) {
            fill = style.currentColor;      textCol = style.currentTextColor;
        } else if (!item.enabled) {
            fill = style.disabledCellColor; textCol = style.disabledTextColor;
        } else if (pressedIndex == index) {
            fill = style.cellPressedColor;  textCol = style.cellTextColor;
        } else if (hoveredIndex == index) {
            fill = style.cellHoverColor;    textCol = style.cellTextColor;
        } else {
            fill = style.cellColor;         textCol = style.cellTextColor;
        }

        ctx->DrawFilledRectangle(item.rect, fill, style.borderWidth,
                                 style.cellBorderColor, style.cornerRadius);

        if (item.type == ItemType::PageNumber) {
            drawCentered(std::to_string(item.page), textCol);
        } else if (!item.label.empty()) {
            // Textual nav cell (Simple mode "Prev" / "Next").
            drawCentered(item.label, textCol);
        } else {
            // Chevron nav cell.
            Color g = item.enabled ? style.navGlyphColor : style.disabledTextColor;
            DrawNavGlyph(ctx, item.rect, item.type, g);
        }
    }

    void UltraCanvasPagination::DrawNavGlyph(IRenderContext* ctx, const Rect2Di& rect,
                                             ItemType type, const Color& color) {
        float h = style.glyphSize / 2.0f;
        int cx = rect.x + rect.width / 2;
        int cy = rect.y + rect.height / 2;

        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.6f);

        auto leftChevron = [&](float ox) {
            ctx->DrawLine(Point2Dd(cx + h + ox, cy - h), Point2Dd(cx - h + ox, cy));
            ctx->DrawLine(Point2Dd(cx - h + ox, cy),     Point2Dd(cx + h + ox, cy + h));
        };
        auto rightChevron = [&](float ox) {
            ctx->DrawLine(Point2Dd(cx - h + ox, cy - h), Point2Dd(cx + h + ox, cy));
            ctx->DrawLine(Point2Dd(cx + h + ox, cy),     Point2Dd(cx - h + ox, cy + h));
        };

        switch (type) {
            case ItemType::Prev:  leftChevron(0);  break;
            case ItemType::Next:  rightChevron(0); break;
            case ItemType::First: leftChevron(-h * 0.6f);  leftChevron(h * 0.6f);  break;
            case ItemType::Last:  rightChevron(-h * 0.6f); rightChevron(h * 0.6f); break;
            default: break;
        }
    }

// ===================================================================
// EVENTS
// ===================================================================

    int UltraCanvasPagination::HitTest(const Point2Di& p) const {
        for (int i = 0; i < static_cast<int>(cachedItems.size()); ++i) {
            if (cachedItems[i].rect.Contains(p)) return i;
        }
        return -1;
    }

    void UltraCanvasPagination::Activate(const Item& item) {
        switch (item.type) {
            case ItemType::First:
            case ItemType::Prev:
            case ItemType::Next:
            case ItemType::Last:
            case ItemType::PageNumber:
                SetCurrentPage(item.page);
                break;
            default:
                break;
        }
    }

    bool UltraCanvasPagination::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseDown: {
                int i = HitTest(event.pointer);
                if (i < 0) return false;
                SetFocus(true);
                if (cachedItems[i].actionable) {
                    pressedIndex = i;
                    Activate(cachedItems[i]);
                    pressedIndex = -1;
                    RequestRedraw();
                }
                return true;
            }
            case UCEventType::MouseUp:
                if (pressedIndex != -1) { pressedIndex = -1; RequestRedraw(); }
                return false;
            case UCEventType::MouseMove: {
                int i = HitTest(event.pointer);
                int nh = (i >= 0 && cachedItems[i].actionable) ? i : -1;
                mouseCursor = (nh >= 0) ? UCMouseCursor::Hand : UCMouseCursor::Default;
                if (nh != hoveredIndex) { hoveredIndex = nh; RequestRedraw(); }
                return i >= 0;
            }
            case UCEventType::MouseLeave:
                if (hoveredIndex != -1 || pressedIndex != -1) {
                    hoveredIndex = -1;
                    pressedIndex = -1;
                    RequestRedraw();
                }
                return true;
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasPagination::HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return false;
        switch (event.virtualKey) {
            case UCKeys::Left:  PrevPage();  return true;
            case UCKeys::Right: NextPage();  return true;
            case UCKeys::Home:  FirstPage(); return true;
            case UCKeys::End:   LastPage();  return true;
            default: break;
        }
        return false;
    }

} // namespace UltraCanvas
