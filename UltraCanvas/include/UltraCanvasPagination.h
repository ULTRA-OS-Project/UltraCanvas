// include/UltraCanvasPagination.h
// Pagination control: page-navigation strip for lists, tables and search
// results. Shows first/prev, a windowed run of page numbers with ellipsis
// gaps, next/last, and an optional "page info" readout.
//
// The page range uses the well-known boundary/sibling windowing algorithm
// (as in Material UI's usePagination): `boundaryCount` pages are always shown
// at each end, `siblingCount` pages are shown either side of the current page,
// and ellipses (…) fill the gaps.
//
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// ===== PAGINATION MODE =====
    enum class PaginationMode {
        Numbered,   // «  ‹  1 … 4 [5] 6 … 20  ›  »   (full page-number strip)
        Compact,    // ‹  Page 5 of 20  ›              (prev / info / next)
        Simple      // ‹ Prev            Next ›        (prev / next only)
    };

// ===== PAGINATION STYLE =====
    struct PaginationStyle {
        // Cells
        Color cellColor          = Colors::White;
        Color cellHoverColor     = Color(235, 242, 255, 255);
        Color cellPressedColor   = Color(215, 230, 250, 255);
        Color cellBorderColor    = Color(200, 200, 205, 255);
        Color cellTextColor      = Color(40, 40, 40, 255);

        // Current (active) page
        Color currentColor       = Colors::Selection;       // 0,120,215
        Color currentTextColor   = Colors::White;

        // Disabled nav (at the ends)
        Color disabledCellColor  = Color(245, 245, 247, 255);
        Color disabledTextColor  = Color(190, 190, 195, 255);

        // Nav chevrons / ellipsis / info text
        Color navGlyphColor      = Color(70, 70, 75, 255);
        Color ellipsisColor      = Color(120, 120, 125, 255);
        Color infoTextColor      = Color(80, 80, 85, 255);

        // Metrics
        float cellSize      = 32.0f;   // square nav/number cell
        float cellSpacing   = 4.0f;
        float cornerRadius  = 4.0f;
        float borderWidth   = 1.0f;
        float glyphSize     = 8.0f;
        float textPaddingH  = 10.0f;   // horizontal padding inside info / text cells

        FontStyle fontStyle;
    };

// ===== PAGINATION COMPONENT =====
    class UltraCanvasPagination : public UltraCanvasUIElement {
    public:
        // Kind of cell in the rendered strip.
        enum class ItemType { First, Prev, PageNumber, Ellipsis, Next, Last, Info };

    private:
        struct Item {
            ItemType type;
            int      page = 0;        // target page for nav / value for PageNumber
            std::string label;        // text for Info / textual nav cells
            Rect2Di  rect;
            bool     enabled = true;
            bool     actionable = false;  // reacts to clicks/hover
        };

        // ===== MODEL =====
        int pageCount   = 1;
        int currentPage = 1;
        int totalItems  = 0;   // optional; 0 = unknown (drives info text + pageCount)
        int pageSize    = 0;   // optional; 0 = pageCount set directly

        // ===== WINDOWING =====
        int siblingCount  = 1;
        int boundaryCount = 1;

        // ===== FEATURE TOGGLES =====
        PaginationMode mode = PaginationMode::Numbered;
        bool showFirstLast = true;
        bool showPrevNext  = true;
        bool showPageInfo  = false;   // in Compact mode the info is always shown

        PaginationStyle style;

        // Layout cache (computed in Render, reused for hit-testing in OnEvent).
        mutable std::vector<Item> cachedItems;
        int hoveredIndex = -1;
        int pressedIndex = -1;

        // Optional custom info formatter: (currentPage, pageCount, totalItems, pageSize).
        std::function<std::string(int, int, int, int)> infoFormatter;

    public:
        // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
        UltraCanvasPagination(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasPagination(const std::string& identifier, float w, float h)
            : UltraCanvasPagination(identifier, -1, -1, w, h) {}
        explicit UltraCanvasPagination(const std::string& identifier)
            : UltraCanvasPagination(identifier, -1, -1, -1, -1) {}

        // ===== MODEL =====
        void SetPageCount(int count);
        int  GetPageCount() const { return pageCount; }

        // Derive the page count from a total item count and a page size.
        void SetTotalItems(int total);
        void SetPageSize(int size);
        int  GetTotalItems() const { return totalItems; }
        int  GetPageSize() const { return pageSize; }

        void SetCurrentPage(int page, bool runCallback = true);
        int  GetCurrentPage() const { return currentPage; }

        void FirstPage() { SetCurrentPage(1); }
        void LastPage()  { SetCurrentPage(pageCount); }
        void NextPage()  { SetCurrentPage(currentPage + 1); }
        void PrevPage()  { SetCurrentPage(currentPage - 1); }

        // Index (1-based) of the first/last item shown on the current page.
        // Meaningful only when a page size is set. Returns 0 otherwise.
        int GetPageStartItem() const;
        int GetPageEndItem() const;

        // ===== WINDOWING =====
        void SetSiblingCount(int n)  { siblingCount  = std::max(0, n); RequestRedraw(); }
        void SetBoundaryCount(int n) { boundaryCount = std::max(1, n); RequestRedraw(); }
        int  GetSiblingCount() const  { return siblingCount; }
        int  GetBoundaryCount() const { return boundaryCount; }

        // ===== APPEARANCE / FEATURES =====
        void SetMode(PaginationMode m) { mode = m; RequestRedraw(); }
        PaginationMode GetMode() const { return mode; }

        void SetShowFirstLast(bool s) { showFirstLast = s; RequestRedraw(); }
        void SetShowPrevNext(bool s)  { showPrevNext = s; RequestRedraw(); }
        void SetShowPageInfo(bool s)  { showPageInfo = s; RequestRedraw(); }

        void SetInfoFormatter(std::function<std::string(int, int, int, int)> fn) {
            infoFormatter = std::move(fn); RequestRedraw();
        }

        PaginationStyle& GetStyle() { return style; }
        const PaginationStyle& GetStyle() const { return style; }
        void SetStyle(const PaginationStyle& s) { style = s; RequestRedraw(); }

        // Rough preferred width for the current configuration (square cells only;
        // info/text cells are estimated). Useful for sizing in a layout.
        float GetPreferredWidth() const;

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(int)> onPageChanged;   // fired with the new (1-based) page

    private:
        void ClampCurrent();
        void RecomputePageCount();

        // Windowed page list: positive = page number, 0 = ellipsis.
        std::vector<int> BuildPageRange() const;
        std::string BuildInfoText() const;

        // Build the full ordered strip of items with rects (needs ctx to measure
        // info / text cells). Stored into `cachedItems`.
        void ComputeLayout(IRenderContext* ctx, const Rect2Di& bounds) const;

        void RenderCell(IRenderContext* ctx, const Item& item, int index);
        void DrawNavGlyph(IRenderContext* ctx, const Rect2Di& rect, ItemType type,
                          const Color& color);

        int HitTest(const Point2Di& localPos) const;
        void Activate(const Item& item);
        bool HandleKeyDown(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasPagination> CreatePagination(
            const std::string& identifier, float x, float y, float w, float h,
            int pageCount = 1, int currentPage = 1) {
        auto p = std::make_shared<UltraCanvasPagination>(identifier, x, y, w, h);
        p->SetPageCount(pageCount);
        p->SetCurrentPage(currentPage, false);
        return p;
    }

    inline std::shared_ptr<UltraCanvasPagination> CreatePaginationForItems(
            const std::string& identifier, float x, float y, float w, float h,
            int totalItems, int pageSize, int currentPage = 1) {
        auto p = std::make_shared<UltraCanvasPagination>(identifier, x, y, w, h);
        p->SetPageSize(pageSize);
        p->SetTotalItems(totalItems);
        p->SetShowPageInfo(true);
        p->SetCurrentPage(currentPage, false);
        return p;
    }

} // namespace UltraCanvas
