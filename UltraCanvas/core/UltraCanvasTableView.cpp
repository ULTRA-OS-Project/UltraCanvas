// core/UltraCanvasTableView.cpp
// Styled table widget implementation.
// Version: 2.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#include "UltraCanvasTableView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {
    // Sort key of a cell: numeric when the cell carries or parses to a
    // number, otherwise lexicographic on the text.
    bool CellNumericKey(const TableViewCell& c, double& out) {
        if (c.type == TableCellType::Number) { out = c.number; return true; }
        if (c.type == TableCellType::Text && !c.text.empty()) {
            char* end = nullptr;
            double v = std::strtod(c.text.c_str(), &end);
            if (end && *end == '\0') { out = v; return true; }
        }
        return false;
    }

    Color Shade(const Color& c, float factor) {
        auto mul = [factor](uint8_t v) {
            int r = static_cast<int>(v * factor);
            return static_cast<uint8_t>(std::clamp(r, 0, 255));
        };
        return Color(mul(c.r), mul(c.g), mul(c.b), c.a);
    }
}

// ============================================================================
// CONSTRUCTION
// ============================================================================

UltraCanvasTableView::UltraCanvasTableView(const std::string& identifier,
                                           float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    style = TableStyles::Create(TableDesign::Professional);
    CreateScrollbar();
}

// ============================================================================
// COLUMNS
// ============================================================================

void UltraCanvasTableView::SetColumns(const std::vector<TableViewColumn>& cols) {
    columns = cols;
    for (auto& row : rows) row.cells.resize(columns.size());
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
}

void UltraCanvasTableView::AddColumn(const TableViewColumn& column) {
    columns.push_back(column);
    for (auto& row : rows) row.cells.resize(columns.size());
    InvalidateGeometry();
    RequestRedraw();
}

void UltraCanvasTableView::AddColumn(const std::string& title, float width) {
    AddColumn(TableViewColumn(title, width));
}

void UltraCanvasTableView::SetHighlightedColumn(int col, const std::string& badgeText) {
    highlightedColumn = (col >= 0 && col < GetColumnCount()) ? col : -1;
    highlightBadgeText = (highlightedColumn >= 0) ? badgeText : "";
    InvalidateGeometry();      // The badge band changes the chrome height
    UpdateScrollbar();
    RequestRedraw();
}

// ============================================================================
// ROWS
// ============================================================================

int UltraCanvasTableView::AddRow(const std::vector<TableViewCell>& cells, void* userData) {
    TableViewRow row;
    row.cells = cells;
    row.cells.resize(columns.size());
    row.userData = userData;
    rows.push_back(std::move(row));
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
    return GetRowCount() - 1;
}

int UltraCanvasTableView::AddRow(const std::vector<std::string>& texts, void* userData) {
    std::vector<TableViewCell> cells;
    cells.reserve(texts.size());
    for (const auto& t : texts) cells.emplace_back(t);
    return AddRow(cells, userData);
}

int UltraCanvasTableView::AddSectionRow(const std::string& caption) {
    TableViewRow row;
    row.kind = TableRowKind::Section;
    row.cells.resize(std::max<size_t>(1, columns.size()));
    row.cells[0].text = caption;
    rows.push_back(std::move(row));
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
    return GetRowCount() - 1;
}

int UltraCanvasTableView::AddTotalRow(const std::vector<TableViewCell>& cells) {
    TableViewRow row;
    row.kind = TableRowKind::Total;
    row.cells = cells;
    row.cells.resize(columns.size());
    rows.push_back(std::move(row));
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
    return GetRowCount() - 1;
}

void UltraCanvasTableView::ClearRows() {
    rows.clear();
    selectedRow = selectedCol = hoveredRow = -1;
    pressedButtonRow = pressedButtonCol = -1;
    scrollOffsetY = 0;
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
}

void UltraCanvasTableView::SetCell(int row, int col, const TableViewCell& cell) {
    if (row < 0 || row >= GetRowCount()) return;
    if (col < 0 || col >= static_cast<int>(rows[row].cells.size())) return;
    rows[row].cells[col] = cell;
    RequestRedraw();
}

const TableViewCell* UltraCanvasTableView::GetCell(int row, int col) const {
    if (row < 0 || row >= GetRowCount()) return nullptr;
    if (col < 0 || col >= static_cast<int>(rows[row].cells.size())) return nullptr;
    return &rows[row].cells[col];
}

// ============================================================================
// STYLE
// ============================================================================

void UltraCanvasTableView::SetDesign(TableDesign design) {
    SetStyle(TableStyles::Create(design));
}

void UltraCanvasTableView::SetDesign(TableDesign design, const TablePalette& palette) {
    SetStyle(TableStyles::Create(design, palette));
}

void UltraCanvasTableView::SetStyle(const TableStyleSheet& sheet) {
    style = sheet;
    StyleChanged();
}

void UltraCanvasTableView::StyleChanged() {
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
}

void UltraCanvasTableView::SetShowHeader(bool show) {
    showHeader = show;
    InvalidateGeometry();
    UpdateScrollbar();
    RequestRedraw();
}

// ============================================================================
// SELECTION
// ============================================================================

void UltraCanvasTableView::SetSelectionMode(TableSelectionMode mode) {
    selectionMode = mode;
    if (mode == TableSelectionMode::NoSelection) SelectRow(-1);
    if (mode == TableSelectionMode::RowSelection) selectedCol = -1;
    RequestRedraw();
}

void UltraCanvasTableView::SelectRow(int row) {
    int newRow = (row >= 0 && row < GetRowCount() &&
                  rows[row].kind == TableRowKind::Data) ? row : -1;
    if (newRow == selectedRow && selectedCol == -1) return;
    selectedRow = newRow;
    selectedCol = -1;
    if (onSelectionChange) onSelectionChange(selectedRow, selectedCol);
    RequestRedraw();
}

void UltraCanvasTableView::SelectCell(int row, int col) {
    int newRow = (row >= 0 && row < GetRowCount() &&
                  rows[row].kind == TableRowKind::Data) ? row : -1;
    int newCol = (newRow >= 0 && col >= 0 && col < GetColumnCount()) ? col : -1;
    if (newRow == selectedRow && newCol == selectedCol) return;
    selectedRow = newRow;
    selectedCol = newCol;
    if (onSelectionChange) onSelectionChange(selectedRow, selectedCol);
    RequestRedraw();
}

// ============================================================================
// SORTING
// ============================================================================

void UltraCanvasTableView::SortByColumn(int col, bool ascending) {
    if (col < 0 || col >= GetColumnCount()) return;
    sortColumn = col;
    sortAscending = ascending;

    // Swapping the operands for descending keeps the comparator a strict
    // weak ordering (a plain negation would make equal elements "less than"
    // each other, which std::stable_sort is not allowed to see).
    auto less = [col, ascending](const TableViewRow& ra, const TableViewRow& rb) {
        const TableViewRow& a = ascending ? ra : rb;
        const TableViewRow& b = ascending ? rb : ra;
        const TableViewCell& ca = a.cells[col];
        const TableViewCell& cb = b.cells[col];
        double na, nb;
        if (CellNumericKey(ca, na) && CellNumericKey(cb, nb)) {
            return na < nb;
        }
        return ca.text < cb.text;
    };

    // Sort each contiguous run of Data rows on its own so section blocks and
    // total rows keep their positions.
    size_t i = 0;
    while (i < rows.size()) {
        if (rows[i].kind != TableRowKind::Data) { ++i; continue; }
        size_t j = i;
        while (j < rows.size() && rows[j].kind == TableRowKind::Data) ++j;
        std::stable_sort(rows.begin() + i, rows.begin() + j, less);
        i = j;
    }

    selectedRow = -1;
    hoveredRow = -1;
    if (onColumnSort) onColumnSort(col, ascending);
    RequestRedraw();
}

// ============================================================================
// GEOMETRY
// ============================================================================

void UltraCanvasTableView::InvalidateGeometry() {
    rowGeometryValid = false;
    columnGeometryWidth = -1.0f;
}

float UltraCanvasTableView::GetBadgeBandHeight() const {
    return (highlightedColumn >= 0 && !highlightBadgeText.empty())
               ? style.badgeBandHeight : 0.0f;
}

float UltraCanvasTableView::GetChromeOffset() const {
    return GetBadgeBandHeight() + (showHeader ? style.headerHeight : 0.0f);
}

float UltraCanvasTableView::RowHeightFor(const TableViewRow& row) const {
    switch (row.kind) {
        case TableRowKind::Section: return style.sectionRowHeight;
        case TableRowKind::Total:   return style.totalRowHeight;
        default:                    return style.rowHeight;
    }
}

void UltraCanvasTableView::RebuildRowGeometryIfNeeded() const {
    if (rowGeometryValid) return;
    rowGeometryValid = true;
    size_t n = rows.size();
    rowTops.resize(n + 1);
    dataOrdinals.assign(n, 0);
    float y = 0;
    int ordinal = 0;
    for (size_t i = 0; i < n; ++i) {
        rowTops[i] = y;
        y += RowHeightFor(rows[i]);
        if (rows[i].kind == TableRowKind::Data) dataOrdinals[i] = ordinal++;
    }
    rowTops[n] = y;
}

float UltraCanvasTableView::RowTopOffset(int row) const {
    RebuildRowGeometryIfNeeded();
    if (row < 0) return 0;
    if (row > GetRowCount()) row = GetRowCount();
    return rowTops[row];
}

float UltraCanvasTableView::RowsContentHeight() const {
    RebuildRowGeometryIfNeeded();
    return rowTops.empty() ? 0.0f : rowTops[rows.size()];
}

float UltraCanvasTableView::GetContentHeight() const {
    return GetChromeOffset() + RowsContentHeight() +
           2.0f * style.outerBorder.width +
           GetTotalBorderVertical() + GetTotalPaddingVertical();
}

int UltraCanvasTableView::RowAtContentY(float contentY) const {
    RebuildRowGeometryIfNeeded();
    int n = GetRowCount();
    if (n == 0 || contentY < 0 || contentY >= rowTops[n]) return -1;
    auto it = std::upper_bound(rowTops.begin(), rowTops.begin() + n, contentY);
    int row = static_cast<int>(it - rowTops.begin()) - 1;
    return std::clamp(row, 0, n - 1);
}

Rect2Df UltraCanvasTableView::GetViewportRect() const {
    float localContentX = GetBorderLeftWidth() + GetPaddingLeft() + style.outerBorder.width;
    float localContentY = GetBorderTopWidth() + GetPaddingTop() + style.outerBorder.width;
    float crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal()
                    - 2.0f * style.outerBorder.width;
    float crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical()
                     - 2.0f * style.outerBorder.width;
    float sbWidth = (verticalScrollbar && verticalScrollbar->IsVisible())
                        ? verticalScrollbar->GetStyle().trackSize : 0.0f;
    float chrome = GetChromeOffset();
    return Rect2Df(localContentX, localContentY + chrome,
                   crWidth - sbWidth, crHeight - chrome);
}

void UltraCanvasTableView::RebuildColumnGeometryIfNeeded() const {
    float viewportWidth = GetViewportRect().width;
    if (columnGeometryWidth == viewportWidth &&
        columnLefts.size() == columns.size() + 1) return;
    columnGeometryWidth = viewportWidth;

    size_t n = columns.size();
    columnLefts.resize(n + 1);

    float fixedTotal = 0.0f;
    float stretchTotal = 0.0f;
    for (const auto& c : columns) {
        if (c.width > 0) fixedTotal += std::max(c.width, c.minWidth);
        else stretchTotal += std::max(c.stretch, 0.01f);
    }
    float leftover = std::max(0.0f, viewportWidth - fixedTotal);

    float x = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        columnLefts[i] = x;
        float w;
        if (columns[i].width > 0) {
            w = std::max(columns[i].width, columns[i].minWidth);
        } else {
            w = std::max(columns[i].minWidth,
                         leftover * std::max(columns[i].stretch, 0.01f) / stretchTotal);
        }
        x += w;
    }
    columnLefts[n] = x;
}

int UltraCanvasTableView::GetColumnAtX(float x) const {
    RebuildColumnGeometryIfNeeded();
    auto viewport = GetViewportRect();
    float local = x - viewport.x;
    int n = GetColumnCount();
    for (int i = 0; i < n; ++i) {
        if (local >= columnLefts[i] && local < columnLefts[i + 1]) return i;
    }
    return -1;
}

int UltraCanvasTableView::GetRowAtY(float y) const {
    auto viewport = GetViewportRect();
    return RowAtContentY(y - viewport.y + scrollOffsetY);
}

Rect2Df UltraCanvasTableView::GetCellRect(int row, int col) const {
    RebuildColumnGeometryIfNeeded();
    auto viewport = GetViewportRect();
    float rowY = viewport.y + RowTopOffset(row) - scrollOffsetY;
    float rowH = (row >= 0 && row < GetRowCount()) ? RowHeightFor(rows[row]) : 0.0f;
    if (col < 0 || col >= GetColumnCount()) {
        return Rect2Df(viewport.x, rowY, viewport.width, rowH);
    }
    return Rect2Df(viewport.x + columnLefts[col], rowY,
                   columnLefts[col + 1] - columnLefts[col], rowH);
}

Rect2Df UltraCanvasTableView::ButtonRectInCell(const Rect2Df& cellRect,
                                               const std::string& label,
                                               IRenderContext* /*ctx*/) const {
    // Deterministic geometry (shared by rendering and hit testing, so it
    // must not depend on a render context being available): an estimated
    // label width plus padding, clamped into the cell.
    float estimated = 7.0f * static_cast<float>(label.size()) + 28.0f;
    float w = std::clamp(estimated, 72.0f, std::max(72.0f, cellRect.width - 16.0f));
    float h = std::min(26.0f, cellRect.height - 8.0f);
    return Rect2Df(cellRect.x + (cellRect.width - w) / 2.0f,
                   cellRect.y + (cellRect.height - h) / 2.0f, w, h);
}

// ============================================================================
// STYLE RESOLUTION
// ============================================================================

TableResolvedCellStyle UltraCanvasTableView::ResolveCellStyle(int row, int col) const {
    RebuildRowGeometryIfNeeded();
    const TableViewRow& r = rows[row];

    TableResolvedCellStyle st;
    st.borderBottom = style.horizontalGrid;
    st.borderRight = style.verticalGrid;
    if (const auto* reg = style.Region(TableRegion::WholeTable)) st.Apply(*reg);

    if (r.kind == TableRowKind::Data) {
        int band = (dataOrdinals[row] / std::max(1, style.rowBandSize)) % 2;
        if (band == 1) {
            if (const auto* reg = style.Region(TableRegion::BandedRows)) st.Apply(*reg);
        }
    }
    if (col == 0) {
        if (const auto* reg = style.Region(TableRegion::FirstColumn)) st.Apply(*reg);
    }
    if (col == GetColumnCount() - 1 && col > 0) {
        if (const auto* reg = style.Region(TableRegion::LastColumn)) st.Apply(*reg);
    }
    if (r.kind == TableRowKind::Section) {
        if (const auto* reg = style.Region(TableRegion::SectionRow)) st.Apply(*reg);
    }
    if (r.kind == TableRowKind::Total) {
        if (const auto* reg = style.Region(TableRegion::TotalRow)) st.Apply(*reg);
    }
    // The highlighted column's background is a translucent wash composited
    // over the cell in RenderRows; apply only the non-background fields here.
    if (r.kind != TableRowKind::Section && col == highlightedColumn) {
        if (const auto* reg = style.Region(TableRegion::HighlightColumn)) {
            TableRegionStyle noBg = *reg;
            noBg.background.reset();
            st.Apply(noBg);
        }
    }
    if (r.kind == TableRowKind::Data) {
        bool isSelected = (selectionMode == TableSelectionMode::RowSelection && row == selectedRow) ||
                          (selectionMode == TableSelectionMode::CellSelection &&
                           row == selectedRow && col == selectedCol);
        if (isSelected) {
            if (const auto* reg = style.Region(TableRegion::SelectedRow)) st.Apply(*reg);
        } else if (row == hoveredRow && selectionMode != TableSelectionMode::NoSelection) {
            if (const auto* reg = style.Region(TableRegion::HoverRow)) st.Apply(*reg);
        }
    }

    // Column / cell overrides
    const TableViewCell* cell = GetCell(row, col);
    if (col >= 0 && col < GetColumnCount() && columns[col].alignment)
        st.hAlign = *columns[col].alignment;
    if (cell) {
        if (cell->hAlign) st.hAlign = *cell->hAlign;
        if (cell->textColor) st.textColor = *cell->textColor;
        if (cell->background) st.background = *cell->background;
    }
    return st;
}

TableResolvedCellStyle UltraCanvasTableView::ResolveHeaderStyle(int col) const {
    TableResolvedCellStyle st;
    st.borderRight = style.verticalGrid;
    if (const auto* reg = style.Region(TableRegion::WholeTable)) st.Apply(*reg);
    if (const auto* reg = style.Region(TableRegion::HeaderRow)) st.Apply(*reg);
    if (col == highlightedColumn) {
        if (const auto* reg = style.Region(TableRegion::HighlightColumn)) {
            TableRegionStyle noBg = *reg;
            noBg.background.reset();
            st.Apply(noBg);
        }
    }
    if (col >= 0 && col < GetColumnCount() && columns[col].alignment)
        st.hAlign = *columns[col].alignment;
    return st;
}

// ============================================================================
// NUMBER FORMATTING
// ============================================================================

std::string UltraCanvasTableView::FormatNumber(double value, int decimals,
                                               bool thousands) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 12),
                  std::fabs(value));
    std::string digits(buf);
    if (thousands) {
        size_t dot = digits.find('.');
        size_t intLen = (dot == std::string::npos) ? digits.size() : dot;
        for (size_t pos = intLen; pos > 3; ) {
            pos -= 3;
            digits.insert(pos, ",");
        }
    }
    return (value < 0) ? "-" + digits : digits;
}

// ============================================================================
// RENDERING
// ============================================================================

void UltraCanvasTableView::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    UltraCanvasUIElement::Render(ctx, dirtyRect);   // Element bg + borders

    float localContentX = GetBorderLeftWidth() + GetPaddingLeft();
    float localContentY = GetBorderTopWidth() + GetPaddingTop();
    float crWidth = GetWidth() - GetTotalBorderHorizontal() - GetTotalPaddingHorizontal();
    float crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical();
    Rect2Df contentRect(localContentX, localContentY, crWidth, crHeight);

    ctx->PushState();
    ctx->ClipRect(contentRect);
    ctx->SetTextWrap(TextWrap::WrapNone);

    // Table base fill (the frame area below the badge band)
    float badgeBand = GetBadgeBandHeight();
    Rect2Df tableRect(contentRect.x, contentRect.y + badgeBand,
                      contentRect.width, contentRect.height - badgeBand);
    TableResolvedCellStyle base;
    if (const auto* reg = style.Region(TableRegion::WholeTable)) base.Apply(*reg);
    if (base.background.a > 0) {
        ctx->SetFillPaint(base.background);
        ctx->FillRectangle(tableRect);
    }

    if (badgeBand > 0) RenderBadgeBand(ctx, contentRect);
    if (showHeader && !columns.empty()) RenderHeader(ctx, contentRect);
    RenderRows(ctx, contentRect);

    // Outer frame
    if (style.outerBorder.IsVisible()) {
        ctx->SetStrokePaint(style.outerBorder.color);
        ctx->SetStrokeWidth(style.outerBorder.width);
        if (style.cornerRadius > 0) {
            ctx->DrawRoundedRectangle(tableRect, style.cornerRadius);
        } else {
            ctx->DrawRectangle(tableRect);
        }
    }
    ctx->PopState();

    // Scrollbar on top (bounds stored element-local)
    if (verticalScrollbar && verticalScrollbar->IsVisible()) {
        ctx->PushState();
        auto sbB = verticalScrollbar->GetBounds();
        ctx->Translate(Point2Df(sbB.x, sbB.y));
        verticalScrollbar->Render(ctx, dirtyRect);
        ctx->PopState();
    }
}

void UltraCanvasTableView::RenderBadgeBand(IRenderContext* ctx, const Rect2Df& contentRect) {
    RebuildColumnGeometryIfNeeded();
    if (highlightedColumn < 0 || highlightBadgeText.empty()) return;
    auto viewport = GetViewportRect();
    float colX = viewport.x + columnLefts[highlightedColumn];
    float colW = columnLefts[highlightedColumn + 1] - columnLefts[highlightedColumn];

    float chipH = style.badgeBandHeight - 4.0f;
    Rect2Df chip(colX, contentRect.y, colW, chipH);
    ctx->SetFillPaint(style.badgeBackground);
    ctx->FillRoundedRectangle(chip, 3.0f);

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.badgeFontSize);
    ctx->SetTextPaint(style.badgeTextColor);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(highlightBadgeText, chip);
}

void UltraCanvasTableView::RenderHeader(IRenderContext* ctx, const Rect2Df& contentRect) {
    RebuildColumnGeometryIfNeeded();
    auto viewport = GetViewportRect();
    float headerY = contentRect.y + GetBadgeBandHeight() + style.outerBorder.width;
    Rect2Df headerRect(viewport.x, headerY, viewport.width, style.headerHeight);

    for (int col = 0; col < GetColumnCount(); ++col) {
        TableResolvedCellStyle st = ResolveHeaderStyle(col);
        Rect2Df cellRect(viewport.x + columnLefts[col], headerY,
                         columnLefts[col + 1] - columnLefts[col], style.headerHeight);

        if (st.background.a > 0) {
            ctx->SetFillPaint(st.background);
            ctx->FillRectangle(cellRect);
        }
        if (col == highlightedColumn) {
            if (const auto* reg = style.Region(TableRegion::HighlightColumn)) {
                if (reg->background && reg->background->a > 0) {
                    ctx->SetFillPaint(*reg->background);
                    ctx->FillRectangle(cellRect);
                }
            }
        }

        ctx->SetFontFace(style.fontFamily, st.fontWeight, st.fontSlant);
        ctx->SetFontSize(st.fontSize);
        ctx->SetTextPaint(st.textColor);
        ctx->SetTextAlignment(st.hAlign);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        Rect2Df textRect(cellRect.x + style.cellPaddingX, cellRect.y,
                         cellRect.width - 2 * style.cellPaddingX, cellRect.height);
        ctx->DrawTextInRect(columns[col].title, textRect);

        if (columns[col].sortable && col == sortColumn) {
            RenderSortIndicator(ctx, cellRect, sortAscending, st.textColor);
        }
        if (st.borderRight.IsVisible() && col < GetColumnCount() - 1) {
            ctx->SetStrokePaint(st.borderRight.color);
            ctx->SetStrokeWidth(st.borderRight.width);
            ctx->DrawLine(cellRect.TopRight(), cellRect.BottomRight());
        }
    }

    // Header top/bottom rules span the full header width
    TableResolvedCellStyle st = ResolveHeaderStyle(0);
    RenderRowBorders(ctx, st, headerRect);
}

void UltraCanvasTableView::RenderRowBorders(IRenderContext* ctx,
                                            const TableResolvedCellStyle& st,
                                            const Rect2Df& rect) {
    if (st.borderTop.IsVisible()) {
        ctx->SetStrokePaint(st.borderTop.color);
        ctx->SetStrokeWidth(st.borderTop.width);
        ctx->DrawLine(rect.TopLeft(), rect.TopRight());
        if (st.borderTop.doubleRule) {
            ctx->DrawLine(Point2Df(rect.x, rect.y + st.borderTop.width + 1.5f),
                          Point2Df(rect.Right(), rect.y + st.borderTop.width + 1.5f));
        }
    }
    if (st.borderBottom.IsVisible()) {
        ctx->SetStrokePaint(st.borderBottom.color);
        ctx->SetStrokeWidth(st.borderBottom.width);
        ctx->DrawLine(rect.BottomLeft(), rect.BottomRight());
        if (st.borderBottom.doubleRule) {
            ctx->DrawLine(Point2Df(rect.x, rect.Bottom() - st.borderBottom.width - 1.5f),
                          Point2Df(rect.Right(), rect.Bottom() - st.borderBottom.width - 1.5f));
        }
    }
}

void UltraCanvasTableView::RenderRows(IRenderContext* ctx, const Rect2Df& /*contentRect*/) {
    if (rows.empty() || columns.empty()) return;
    RebuildRowGeometryIfNeeded();
    RebuildColumnGeometryIfNeeded();

    auto viewport = GetViewportRect();
    ctx->PushState();
    ctx->ClipRect(viewport);

    int firstVisible = RowAtContentY(static_cast<float>(scrollOffsetY));
    if (firstVisible < 0) firstVisible = 0;

    const TableRegionStyle* highlightReg = style.Region(TableRegion::HighlightColumn);

    for (int row = firstVisible; row < GetRowCount(); ++row) {
        float rowH = RowHeightFor(rows[row]);
        float rowY = viewport.y + RowTopOffset(row) - scrollOffsetY;
        if (rowY > viewport.Bottom()) break;
        if (rowY + rowH < viewport.y) continue;

        Rect2Df rowRect(viewport.x, rowY, viewport.width, rowH);

        if (rows[row].kind == TableRowKind::Section) {
            // One full-width band: background, caption, borders
            TableResolvedCellStyle st = ResolveCellStyle(row, 0);
            if (st.background.a > 0) {
                ctx->SetFillPaint(st.background);
                ctx->FillRectangle(rowRect);
            }
            ctx->SetFontFace(style.fontFamily, st.fontWeight, st.fontSlant);
            ctx->SetFontSize(st.fontSize);
            ctx->SetTextPaint(st.textColor);
            ctx->SetTextAlignment(st.hAlign);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            Rect2Df textRect(rowRect.x + style.cellPaddingX, rowRect.y,
                             rowRect.width - 2 * style.cellPaddingX, rowRect.height);
            ctx->DrawTextInRect(rows[row].cells.empty() ? "" : rows[row].cells[0].text,
                                textRect);
            RenderRowBorders(ctx, st, rowRect);
            continue;
        }

        for (int col = 0; col < GetColumnCount(); ++col) {
            TableResolvedCellStyle st = ResolveCellStyle(row, col);
            Rect2Df cellRect(viewport.x + columnLefts[col], rowY,
                             columnLefts[col + 1] - columnLefts[col], rowH);

            if (st.background.a > 0) {
                ctx->SetFillPaint(st.background);
                ctx->FillRectangle(cellRect);
            }
            // Highlighted column: translucent wash over the base fill
            if (col == highlightedColumn && highlightReg &&
                highlightReg->background && highlightReg->background->a > 0) {
                ctx->SetFillPaint(*highlightReg->background);
                ctx->FillRectangle(cellRect);
            }

            const TableViewCell& cell = rows[row].cells[col];
            RenderCellContent(ctx, row, col, cell, st, cellRect);

            if (st.borderRight.IsVisible() && col < GetColumnCount() - 1) {
                ctx->SetStrokePaint(st.borderRight.color);
                ctx->SetStrokeWidth(st.borderRight.width);
                ctx->DrawLine(cellRect.TopRight(), cellRect.BottomRight());
            }
        }

        TableResolvedCellStyle rowSt = ResolveCellStyle(row, 0);
        RenderRowBorders(ctx, rowSt, rowRect);
    }

    ctx->PopState();
}

void UltraCanvasTableView::RenderCellContent(IRenderContext* ctx, int row, int col,
                                             const TableViewCell& cell,
                                             const TableResolvedCellStyle& st,
                                             const Rect2Df& cellRect) {
    switch (cell.type) {
        case TableCellType::Check:  RenderCheckGlyph(ctx, cellRect); return;
        case TableCellType::Cross:  RenderCrossGlyph(ctx, cellRect); return;
        case TableCellType::Minus:  RenderMinusGlyph(ctx, cellRect); return;
        case TableCellType::Badge:  RenderBadgeCell(ctx, cell, cellRect); return;
        case TableCellType::Button: RenderButtonCell(ctx, row, col, cell, cellRect); return;
        case TableCellType::Custom:
            if (cell.customRenderer) {
                ctx->PushState();
                ctx->ClipRect(cellRect);
                cell.customRenderer(ctx, cellRect, cell);
                ctx->PopState();
            }
            return;
        case TableCellType::Text:
        case TableCellType::Number:
            break;
    }

    std::string text = cell.text;
    Color textColor = st.textColor;
    if (cell.type == TableCellType::Number) {
        text = FormatNumber(cell.number, cell.decimals, cell.thousandsSeparator);
        if (style.negativeNumbersRed && cell.number < 0 && !cell.textColor) {
            textColor = style.negativeNumberColor;
        }
    }
    if (text.empty()) return;

    ctx->SetFontFace(style.fontFamily, st.fontWeight, st.fontSlant);
    ctx->SetFontSize(st.fontSize);
    ctx->SetTextPaint(textColor);
    ctx->SetTextAlignment(st.hAlign);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    Rect2Df textRect(cellRect.x + style.cellPaddingX, cellRect.y,
                     cellRect.width - 2 * style.cellPaddingX, cellRect.height);
    ctx->PushState();
    ctx->ClipRect(cellRect);
    ctx->DrawTextInRect(text, textRect);
    ctx->PopState();
}

void UltraCanvasTableView::RenderCheckGlyph(IRenderContext* ctx, const Rect2Df& cellRect) {
    auto c = cellRect.Center();
    ctx->PushState();
    ctx->SetStrokePaint(style.checkGlyphColor);
    ctx->SetStrokeWidth(2.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->SetLineJoin(LineJoin::Round);
    ctx->ClearPath();
    ctx->MoveTo(c.x - 5.5, c.y + 0.5);
    ctx->LineTo(c.x - 1.5, c.y + 4.5);
    ctx->LineTo(c.x + 5.5, c.y - 4.5);
    ctx->StrokePathPreserve();
    ctx->ClearPath();
    ctx->PopState();
}

void UltraCanvasTableView::RenderCrossGlyph(IRenderContext* ctx, const Rect2Df& cellRect) {
    // Circled slash ("not included"), the ⊘ of the comparison matrix
    auto c = cellRect.Center();
    const float r = 6.5f;
    ctx->PushState();
    ctx->SetStrokePaint(style.crossGlyphColor);
    ctx->SetStrokeWidth(1.5f);
    ctx->DrawCircle(Point2Dd(c.x, c.y), r);
    float d = r * 0.7071f;
    ctx->DrawLine(Point2Dd(c.x - d, c.y + d), Point2Dd(c.x + d, c.y - d));
    ctx->PopState();
}

void UltraCanvasTableView::RenderMinusGlyph(IRenderContext* ctx, const Rect2Df& cellRect) {
    auto c = cellRect.Center();
    ctx->PushState();
    ctx->SetStrokePaint(style.crossGlyphColor);
    ctx->SetStrokeWidth(2.0f);
    ctx->SetLineCap(LineCap::Round);
    ctx->DrawLine(Point2Dd(c.x - 5, c.y), Point2Dd(c.x + 5, c.y));
    ctx->PopState();
}

void UltraCanvasTableView::RenderBadgeCell(IRenderContext* ctx, const TableViewCell& cell,
                                           const Rect2Df& cellRect) {
    float estimated = 6.5f * static_cast<float>(cell.text.size()) + 18.0f;
    float w = std::clamp(estimated, 34.0f, std::max(34.0f, cellRect.width - 12.0f));
    float h = std::min(19.0f, cellRect.height - 6.0f);
    Rect2Df chip(cellRect.x + (cellRect.width - w) / 2.0f,
                 cellRect.y + (cellRect.height - h) / 2.0f, w, h);

    ctx->SetFillPaint(cell.background.value_or(style.badgeBackground));
    ctx->FillRoundedRectangle(chip, h / 2.0f);

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.badgeFontSize);
    ctx->SetTextPaint(cell.textColor.value_or(style.badgeTextColor));
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(cell.text, chip);
}

void UltraCanvasTableView::RenderButtonCell(IRenderContext* ctx, int row, int col,
                                            const TableViewCell& cell,
                                            const Rect2Df& cellRect) {
    const TableButtonStyle& bs = (col == highlightedColumn) ? style.buttonHighlight
                                                            : style.button;
    Rect2Df btn = ButtonRectInCell(cellRect, cell.text, ctx);
    bool pressed = (row == pressedButtonRow && col == pressedButtonCol);

    Color bg = cell.background.value_or(bs.background);
    if (pressed && bg.a > 0) bg = Shade(bg, 0.85f);
    if (bg.a > 0) {
        ctx->SetFillPaint(bg);
        ctx->FillRoundedRectangle(btn, bs.cornerRadius);
    }
    if (bs.borderWidth > 0 && bs.borderColor.a > 0) {
        ctx->SetStrokePaint(pressed ? Shade(bs.borderColor, 0.85f) : bs.borderColor);
        ctx->SetStrokeWidth(bs.borderWidth);
        ctx->DrawRoundedRectangle(btn, bs.cornerRadius);
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(11.0f);
    ctx->SetTextPaint(cell.textColor.value_or(bs.textColor));
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(cell.text, btn);
}

void UltraCanvasTableView::RenderSortIndicator(IRenderContext* ctx,
                                               const Rect2Df& headerCell,
                                               bool ascending, const Color& color) {
    float cx = headerCell.Right() - 12.0f;
    float cy = headerCell.y + headerCell.height / 2.0f;
    const float s = 3.5f;
    ctx->PushState();
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(1.5f);
    ctx->SetLineCap(LineCap::Round);
    if (ascending) {
        ctx->DrawLine(Point2Dd(cx - s, cy + s / 2), Point2Dd(cx, cy - s / 2));
        ctx->DrawLine(Point2Dd(cx, cy - s / 2), Point2Dd(cx + s, cy + s / 2));
    } else {
        ctx->DrawLine(Point2Dd(cx - s, cy - s / 2), Point2Dd(cx, cy + s / 2));
        ctx->DrawLine(Point2Dd(cx, cy + s / 2), Point2Dd(cx + s, cy - s / 2));
    }
    ctx->PopState();
}

// ============================================================================
// SCROLLBAR
// ============================================================================

void UltraCanvasTableView::CreateScrollbar() {
    verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
            GetIdentifier() + "_vscroll", 0, 0,
            GetDefaultScrollbarStyleOr(ScrollbarStyle::Modern()).trackSize, 100,
            ScrollbarOrientation::Vertical);
    verticalScrollbar->onScrollChange = [this](int pos) {
        scrollOffsetY = pos;
        RequestRedraw();
    };
    verticalScrollbar->SetStyle(GetDefaultScrollbarStyleOr(ScrollbarStyle::Modern()));
    verticalScrollbar->SetVisible(false);
}

void UltraCanvasTableView::UpdateScrollbar() {
    if (!verticalScrollbar) return;
    float contentHeight = RowsContentHeight();
    float chrome = GetChromeOffset();
    float crHeight = GetHeight() - GetTotalBorderVertical() - GetTotalPaddingVertical()
                     - 2.0f * style.outerBorder.width;
    float viewportHeight = crHeight - chrome;

    maxScrollY = static_cast<int>(std::max(0.0f, contentHeight - viewportHeight));
    bool visible = maxScrollY > 0;
    verticalScrollbar->SetVisible(visible);

    if (visible) {
        float localX = GetBorderLeftWidth();
        float localY = GetBorderTopWidth();
        float paddingW = GetWidth() - GetTotalBorderHorizontal();
        float paddingH = GetHeight() - GetTotalBorderVertical();
        int sbWidth = verticalScrollbar->GetStyle().trackSize;
        float chromeTop = GetPaddingTop() + GetBadgeBandHeight() + style.outerBorder.width
                          + (showHeader ? style.headerHeight : 0.0f);
        verticalScrollbar->SetPosition(localX + paddingW - sbWidth, localY + chromeTop);
        verticalScrollbar->SetSize(sbWidth, paddingH - chromeTop);
        verticalScrollbar->SetViewportSize(static_cast<int>(viewportHeight));
        verticalScrollbar->SetContentSize(static_cast<int>(contentHeight));
    }
    ClampScrollOffset();
}

void UltraCanvasTableView::ClampScrollOffset() {
    scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
    if (verticalScrollbar && verticalScrollbar->IsVisible()) {
        verticalScrollbar->SetScrollPosition(scrollOffsetY);
    }
}

void UltraCanvasTableView::ScrollToRow(int row) {
    if (row < 0 || row >= GetRowCount()) return;
    scrollOffsetY = static_cast<int>(RowTopOffset(row));
    ClampScrollOffset();
    RequestRedraw();
}

void UltraCanvasTableView::EnsureRowVisible(int row) {
    if (row < 0 || row >= GetRowCount()) return;
    auto viewport = GetViewportRect();
    float rowTop = RowTopOffset(row);
    float rowBottom = rowTop + RowHeightFor(rows[row]);
    if (rowTop < scrollOffsetY) {
        scrollOffsetY = static_cast<int>(rowTop);
    } else if (rowBottom > scrollOffsetY + viewport.height) {
        scrollOffsetY = static_cast<int>(rowBottom - viewport.height);
    }
    ClampScrollOffset();
}

// ============================================================================
// EVENTS
// ============================================================================

bool UltraCanvasTableView::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;

    if (verticalScrollbar && verticalScrollbar->IsVisible()) {
        auto sbB = verticalScrollbar->GetBounds();
        if (sbB.Contains(event.pointer) || verticalScrollbar->IsDragging()) {
            UCEvent localEvent = event;
            localEvent.pointer = event.pointer - sbB.TopLeft();
            if (verticalScrollbar->OnEvent(localEvent)) return true;
        }
    }

    switch (event.type) {
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseLeave:
            if (hoveredRow != -1 || pressedButtonRow != -1) {
                hoveredRow = -1;
                pressedButtonRow = pressedButtonCol = -1;
                RequestRedraw();
            }
            return false;
        case UCEventType::MouseDoubleClick: return HandleMouseDoubleClick(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::KeyDown:          return HandleKeyDown(event);
        default:
            break;
    }
    return false;
}

bool UltraCanvasTableView::HandleMouseDown(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;
    SetFocus(true);

    // Header: sort toggle
    if (showHeader) {
        auto viewport = GetViewportRect();
        float headerTop = viewport.y - style.headerHeight;
        if (event.pointer.y >= headerTop && event.pointer.y < viewport.y) {
            int col = GetColumnAtX(static_cast<float>(event.pointer.x));
            if (col >= 0) HandleHeaderClick(col);
            return true;
        }
    }

    int row = GetRowAtY(static_cast<float>(event.pointer.y));
    if (row < 0) {
        if (selectionMode != TableSelectionMode::NoSelection) SelectRow(-1);
        return true;
    }
    int col = GetColumnAtX(static_cast<float>(event.pointer.x));

    // Button cells arm on press, fire on release inside
    if (rows[row].kind != TableRowKind::Section && col >= 0) {
        const TableViewCell& cell = rows[row].cells[col];
        if (cell.type == TableCellType::Button) {
            Rect2Df btn = ButtonRectInCell(GetCellRect(row, col), cell.text, nullptr);
            if (btn.Contains(event.pointer)) {
                pressedButtonRow = row;
                pressedButtonCol = col;
                RequestRedraw();
                return true;
            }
        }
    }

    if (rows[row].kind == TableRowKind::Data) {
        if (selectionMode == TableSelectionMode::CellSelection && col >= 0) {
            SelectCell(row, col);
        } else if (selectionMode == TableSelectionMode::RowSelection) {
            SelectRow(row);
        }
        if (onRowClick) onRowClick(row);
        if (onCellClick && col >= 0) onCellClick(row, col);
    }
    RequestRedraw();
    return true;
}

bool UltraCanvasTableView::HandleMouseUp(const UCEvent& event) {
    if (pressedButtonRow < 0) return false;
    int row = pressedButtonRow, col = pressedButtonCol;
    pressedButtonRow = pressedButtonCol = -1;
    RequestRedraw();

    const TableViewCell* cell = GetCell(row, col);
    if (cell && cell->type == TableCellType::Button) {
        Rect2Df btn = ButtonRectInCell(GetCellRect(row, col), cell->text, nullptr);
        if (btn.Contains(event.pointer)) {
            if (onCellAction) onCellAction(row, col);
        }
    }
    return true;
}

bool UltraCanvasTableView::HandleMouseMove(const UCEvent& event) {
    int row = Contains(event.pointer)
                  ? GetRowAtY(static_cast<float>(event.pointer.y)) : -1;
    if (row >= 0 && rows[row].kind != TableRowKind::Data) row = -1;
    if (row != hoveredRow) {
        hoveredRow = row;
        RequestRedraw();
        return true;
    }
    return false;
}

bool UltraCanvasTableView::HandleMouseDoubleClick(const UCEvent& event) {
    int row = GetRowAtY(static_cast<float>(event.pointer.y));
    if (row >= 0 && rows[row].kind == TableRowKind::Data) {
        if (onRowDoubleClick) onRowDoubleClick(row);
        return true;
    }
    return false;
}

bool UltraCanvasTableView::HandleMouseWheel(const UCEvent& event) {
    if (verticalScrollbar && verticalScrollbar->IsVisible()) {
        verticalScrollbar->GetStyleRef().scrollSpeed =
                std::max(1, static_cast<int>(style.rowHeight));
        return verticalScrollbar->ScrollByWheel(event.wheelDelta);
    }
    return false;
}

void UltraCanvasTableView::HandleHeaderClick(int col) {
    if (col < 0 || col >= GetColumnCount() || !columns[col].sortable) return;
    bool ascending = (sortColumn != col) || !sortAscending;
    SortByColumn(col, ascending);
}

int UltraCanvasTableView::NextSelectableRow(int from, int direction) const {
    int row = from + direction;
    while (row >= 0 && row < GetRowCount() &&
           rows[row].kind != TableRowKind::Data) {
        row += direction;
    }
    return (row >= 0 && row < GetRowCount()) ? row : -1;
}

void UltraCanvasTableView::MoveSelection(int delta) {
    if (selectionMode == TableSelectionMode::NoSelection || GetRowCount() == 0) return;
    int direction = delta >= 0 ? 1 : -1;
    int row = selectedRow;
    if (row < 0) {
        row = NextSelectableRow(direction > 0 ? -1 : GetRowCount(), direction);
    } else {
        for (int step = 0; step < std::abs(delta); ++step) {
            int next = NextSelectableRow(row, direction);
            if (next < 0) break;
            row = next;
        }
    }
    if (row >= 0) {
        if (selectionMode == TableSelectionMode::CellSelection) {
            SelectCell(row, std::max(0, selectedCol));
        } else {
            SelectRow(row);
        }
        EnsureRowVisible(row);
        RequestRedraw();
    }
}

bool UltraCanvasTableView::HandleKeyDown(const UCEvent& event) {
    if (GetRowCount() == 0) return false;
    auto viewport = GetViewportRect();
    int pageRows = std::max(1, static_cast<int>(viewport.height / std::max(1.0f, style.rowHeight)));

    switch (event.virtualKey) {
        case UCKeys::Up:       MoveSelection(-1); return true;
        case UCKeys::Down:     MoveSelection(1); return true;
        case UCKeys::PageUp:   MoveSelection(-pageRows); return true;
        case UCKeys::PageDown: MoveSelection(pageRows); return true;
        case UCKeys::Home:
            MoveSelection(-GetRowCount());
            return true;
        case UCKeys::End:
            MoveSelection(GetRowCount());
            return true;
        case UCKeys::Left:
            if (selectionMode == TableSelectionMode::CellSelection && selectedRow >= 0) {
                SelectCell(selectedRow, std::max(0, selectedCol - 1));
                return true;
            }
            return false;
        case UCKeys::Right:
            if (selectionMode == TableSelectionMode::CellSelection && selectedRow >= 0) {
                SelectCell(selectedRow, std::min(GetColumnCount() - 1, selectedCol + 1));
                return true;
            }
            return false;
        case UCKeys::Return:
        case UCKeys::Space:
            if (selectedRow >= 0 && onRowActivate) {
                onRowActivate(selectedRow);
                return true;
            }
            return false;
        default:
            break;
    }
    return false;
}

// ============================================================================
// OVERRIDES
// ============================================================================

void UltraCanvasTableView::Arrange(const Rect2Df& finalRect,
                                   const CSSLayout::LayoutContext& ctx) {
    UltraCanvasUIElement::Arrange(finalRect, ctx);
    InvalidateGeometry();
    UpdateScrollbar();
}

void UltraCanvasTableView::SetBounds(const Rect2Df& bounds) {
    if (bounds != GetBounds()) {
        UltraCanvasUIElement::SetBounds(bounds);
        InvalidateGeometry();
        UpdateScrollbar();
        RequestRedraw();
    }
}

void UltraCanvasTableView::SetWindow(UltraCanvasWindowBase* win) {
    UltraCanvasUIElement::SetWindow(win);
    if (verticalScrollbar) verticalScrollbar->SetWindow(win);
}

} // namespace UltraCanvas
