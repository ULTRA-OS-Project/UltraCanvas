// core/UltraCanvasSpreadsheet.cpp
// Main spreadsheet UI component implementation
// Version: 1.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include <stdexcept>
#include "UltraCanvasApplication.h"
#include "UltraCanvasSpreadsheet.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

UltraCanvasSpreadsheet::UltraCanvasSpreadsheet()
    : UltraCanvasUIElement()
{
    formulaEngine_ = std::make_unique<SpreadsheetFormulaEngine>();
    formulaEngine_->SetSpreadsheet(this);
    InitializeDefaultSheet();
}

UltraCanvasSpreadsheet::UltraCanvasSpreadsheet(
    const std::string& id,
    float x, float y, float width, float height)
    : UltraCanvasUIElement(id, x, y, width, height)
{
    formulaEngine_ = std::make_unique<SpreadsheetFormulaEngine>();
    formulaEngine_->SetSpreadsheet(this);
    InitializeDefaultSheet();
    UpdateLayout();
}


// ============================================================================
// UIELEMENT INTERFACE - RENDER
// ============================================================================

void UltraCanvasSpreadsheet::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;

    ctx->PushState();

    // Clear background
    ctx->SetFillPaint(Colors::White);
    ctx->FillRectangle(Rect2Df(0, 0, GetWidth(), GetHeight()));

    // Render components in order
    if (showFormulaBar_) {
        RenderFormulaBar(ctx);
    }

    RenderCornerHeader(ctx);

    if (showColumnHeaders_) {
        RenderColumnHeaders(ctx);
    }

    if (showRowHeaders_) {
        RenderRowHeaders(ctx);
    }

    RenderCells(ctx);
    RenderSelection(ctx);
    RenderFormulaRangeHighlights(ctx);

    if (HasFrozenPanes()) {
        RenderFreezeLines(ctx);
    }

    if (HasClipboardData()) {
        RenderClipboardIndicator(ctx);
    }

    if (IsEditing()) {
        RenderEditCursor(ctx);
    }

    RenderAutoFillHandle(ctx);

    if (showSheetTabs_) {
        RenderSheetTabs(ctx);
    }

    if (showScrollbars_) {
        RenderScrollbars(ctx);
    }

    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderFormulaBar(IRenderContext* ctx) {
    // Formula bar background
    ctx->SetFillPaint(formulaBarBackgroundColor_);
    ctx->FillRectangle(Rect2Df(formulaBarBounds_.x, formulaBarBounds_.y,
                  formulaBarBounds_.width, formulaBarBounds_.height));

    // Border
    ctx->SetStrokePaint(gridlineColor_);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(Rect2Df(formulaBarBounds_.x, formulaBarBounds_.y,
                  formulaBarBounds_.width, formulaBarBounds_.height));

    // Cell address box (Name Box)
    int nameBoxWidth = 80;
    ctx->SetFillPaint(Colors::White);
    ctx->FillRectangle(Rect2Df(formulaBarBounds_.x + 2, formulaBarBounds_.y + 2,
                  nameBoxWidth, formulaBarBounds_.height - 4));
    ctx->DrawRectangle(Rect2Df(formulaBarBounds_.x + 2, formulaBarBounds_.y + 2,
                  nameBoxWidth, formulaBarBounds_.height - 4));

    // Active cell address
    CellAddress active = GetActiveCell();
    ctx->SetTextPaint(Colors::Black);
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(11);
    std::string addrText = active.ToString();
    ctx->DrawText(addrText,
                  Point2Df(formulaBarBounds_.x + 6,
                  formulaBarBounds_.y + (formulaBarBounds_.height - ctx->GetTextLineHeight(addrText)) / 2));

    // Formula/value text
    int textX = formulaBarBounds_.x + nameBoxWidth + 10;
    std::string displayText = editBuffer_;
    if (!IsEditing()) {
        if (auto* cell = GetCellIfExists(active.row, active.col)) {
            displayText = cell->HasFormula() ? cell->GetFormulaText() : cell->GetDisplayValue();
        } else {
            displayText = "";
        }
    }

    ctx->DrawText(displayText,
                  Point2Df(textX,
                  formulaBarBounds_.y + (formulaBarBounds_.height - ctx->GetTextLineHeight(displayText)) / 2));
}

void UltraCanvasSpreadsheet::RenderCornerHeader(IRenderContext* ctx) {
    if (!showRowHeaders_ || !showColumnHeaders_) return;
    
    int x = gridBounds_.x;
    int y = gridBounds_.y;
    int w = SpreadsheetLimits::RowHeaderWidth;
    int h = SpreadsheetLimits::HeaderRowHeight;
    
    ctx->SetFillPaint(headerBackgroundColor_);
    ctx->FillRectangle(Rect2Df(x, y, w, h));

    ctx->SetStrokePaint(gridlineColor_);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(Rect2Df(x, y, w, h));
}

void UltraCanvasSpreadsheet::RenderColumnHeaders(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    int headerY = gridBounds_.y;
    int headerHeight = SpreadsheetLimits::HeaderRowHeight;
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    
    CellRange visible = GetVisibleRange();
    const auto& selection = sheet->GetSelection();
    
    int x = startX;
    for (int col = visible.start.col; col <= visible.end.col && x < gridBounds_.x + gridBounds_.width; ++col) {
        if (sheet->IsColumnHidden(col)) continue;
        
        int colWidth = sheet->GetColumnWidth(col);
        
        // Highlight if column is in selection
        bool isSelected = false;
        for (const auto& sel : selection.selections) {
            if (col >= sel.start.col && col <= sel.end.col) {
                isSelected = true;
                break;
            }
        }
        
        // Background
        ctx->SetFillPaint(isSelected ? activeHeaderColor_ : headerBackgroundColor_);
        ctx->FillRectangle(Rect2Df(x, headerY, colWidth, headerHeight));

        // Border
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Df(x, headerY, colWidth, headerHeight));

        // Column letter
        ctx->SetTextPaint(isSelected ? Colors::White : headerTextColor_);
        ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(11);
        std::string colName = CellAddress::ColumnToLetter(col);
        int textWidth = ctx->GetTextLineWidth(colName);
        ctx->DrawText(colName, Point2Df(x + (colWidth - textWidth) / 2,
                      headerY + (headerHeight - ctx->GetTextLineHeight(colName)) / 2));

        x += colWidth;
    }
}

void UltraCanvasSpreadsheet::RenderRowHeaders(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    int headerX = gridBounds_.x;
    int headerWidth = SpreadsheetLimits::RowHeaderWidth;
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    CellRange visible = GetVisibleRange();
    const auto& selection = sheet->GetSelection();
    
    int y = startY;
    for (int row = visible.start.row; row <= visible.end.row && y < gridBounds_.y + gridBounds_.height; ++row) {
        if (sheet->IsRowHidden(row)) continue;
        
        int rowHeight = sheet->GetRowHeight(row);
        
        // Highlight if row is in selection
        bool isSelected = false;
        for (const auto& sel : selection.selections) {
            if (row >= sel.start.row && row <= sel.end.row) {
                isSelected = true;
                break;
            }
        }
        
        // Background
        ctx->SetFillPaint(isSelected ? activeHeaderColor_ : headerBackgroundColor_);
        ctx->FillRectangle(Rect2Df(headerX, y, headerWidth, rowHeight));

        // Border
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Df(headerX, y, headerWidth, rowHeight));

        // Row number
        ctx->SetTextPaint(isSelected ? Colors::White : headerTextColor_);
        ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(11);
        std::string rowNum = std::to_string(row + 1);
        int textWidth = ctx->GetTextLineWidth(rowNum);
        ctx->DrawText(rowNum, Point2Df(headerX + (headerWidth - textWidth) / 2,
                      y + (rowHeight - ctx->GetTextLineHeight(rowNum)) / 2));

        y += rowHeight;
    }
}

void UltraCanvasSpreadsheet::RenderCells(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange visible = GetVisibleRange();
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    // Clip to cell area
    ctx->PushState();
    ctx->ClipRect(Rect2Df(startX, startY,
                  gridBounds_.width - (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0),
                  gridBounds_.height - (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0)));

    int y = startY;
    for (int row = visible.start.row; row <= visible.end.row; ++row) {
        if (sheet->IsRowHidden(row)) continue;
        
        int rowHeight = sheet->GetRowHeight(row);
        int x = startX;
        
        for (int col = visible.start.col; col <= visible.end.col; ++col) {
            if (sheet->IsColumnHidden(col)) continue;
            
            int colWidth = sheet->GetColumnWidth(col);
            Rect2Di cellBounds(x, y, colWidth, rowHeight);
            
            // Check if cell is part of merge (skip if not origin)
            if (sheet->IsCellMerged(row, col)) {
                auto* merge = sheet->GetMergedCell(row, col);
                if (merge && !merge->IsTopLeft(row, col)) {
                    x += colWidth;
                    continue;
                }
                // Expand bounds for merged cell
                if (merge) {
                    cellBounds = sheet->GetCellBounds(row, col);
                }
            }
            
            RenderCell(ctx, row, col, cellBounds);
            x += colWidth;
        }
        y += rowHeight;
    }
    
    // Draw gridlines
    if (showGridlines_) {
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);

        // Vertical lines
        int x = startX;
        for (int col = visible.start.col; col <= visible.end.col + 1; ++col) {
            if (col < visible.end.col + 1 && sheet->IsColumnHidden(col)) continue;
            ctx->DrawLine(Point2Df(x, startY), Point2Df(x, startY + gridBounds_.height));
            if (col <= visible.end.col) {
                x += sheet->GetColumnWidth(col);
            }
        }

        // Horizontal lines
        y = startY;
        for (int row = visible.start.row; row <= visible.end.row + 1; ++row) {
            if (row < visible.end.row + 1 && sheet->IsRowHidden(row)) continue;
            ctx->DrawLine(Point2Df(startX, y), Point2Df(startX + gridBounds_.width, y));
            if (row <= visible.end.row) {
                y += sheet->GetRowHeight(row);
            }
        }
    }

    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderCell(IRenderContext* ctx, int row, int col, const Rect2Di& bounds) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    const SpreadsheetCell* cell = sheet->GetCellIfExists(row, col);

    // In-cell editing: draw the live edit buffer over a clean background instead
    // of the committed value. The caret is drawn separately by RenderEditCursor.
    if (editMode_ == SpreadsheetEditMode::InCellEditing &&
        row == editingCell_.row && col == editingCell_.col) {
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(Rect2Df(bounds.x, bounds.y, bounds.width, bounds.height));

        ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(11);
        ctx->SetTextPaint(Colors::Black);

        int padding = 3;
        int textY = bounds.y + (bounds.height - ctx->GetTextLineHeight(editBuffer_)) / 2;
        ctx->PushState();
        ctx->ClipRect(Rect2Df(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2));
        ctx->DrawText(editBuffer_, Point2Df(bounds.x + padding, textY));
        ctx->PopState();
        return;
    }

    // Cell background
    if (cell && cell->HasCustomStyle()) {
        const auto& style = cell->GetStyle();
        if (style.fill.pattern != FillPattern::None) {
            ctx->SetFillPaint(style.fill.foregroundColor);
            ctx->FillRectangle(Rect2Df(bounds.x, bounds.y, bounds.width, bounds.height));
        }
    }

    // Cell content
    if (cell && !cell->IsEmpty()) {
        const auto& style = cell->GetStyle();

        // Set font
        std::string fontFamily = style.font.family.empty() ? "Arial" : style.font.family;
        ctx->SetFontFace(fontFamily,
                         (style.font.bold ? FontWeight::Bold : FontWeight::Normal),
                         (style.font.italic ? FontSlant::Italic : FontSlant::Normal));
        ctx->SetFontSize(style.font.size);
        ctx->SetTextPaint(style.font.color);

        // Get display text
        std::string text = cell->GetDisplayValue();

        // Calculate text position based on alignment
        int padding = 3;
        int textX = bounds.x + padding;
        int textY = bounds.y + (bounds.height - ctx->GetTextLineHeight(text)) / 2;

        // Horizontal alignment
        if (style.hAlign == HorizontalAlignment::Center) {
            int textWidth = ctx->GetTextLineWidth(text);
            textX = bounds.x + (bounds.width - textWidth) / 2;
        } else if (style.hAlign == HorizontalAlignment::Right) {
            int textWidth = ctx->GetTextLineWidth(text);
            textX = bounds.x + bounds.width - textWidth - padding;
        } else if (style.hAlign == HorizontalAlignment::General) {
            // Numbers and dates align right, text aligns left
            if (cell->IsNumeric()) {
                int textWidth = ctx->GetTextLineWidth(text);
                textX = bounds.x + bounds.width - textWidth - padding;
            }
        }

        // Clip text to cell bounds
        ctx->PushState();
        ctx->ClipRect(Rect2Df(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2));
        ctx->DrawText(text, Point2Df(textX, textY));
        ctx->PopState();
    }

    // Cell borders (if custom)
    if (cell && cell->HasCustomStyle()) {
        const auto& borders = cell->GetStyle().borders;

        if (borders.left.style != BorderStyle::None) {
            ctx->SetStrokePaint(borders.left.color);
            ctx->SetStrokeWidth(borders.left.width);
            ctx->DrawLine(Point2Df(bounds.x, bounds.y), Point2Df(bounds.x, bounds.y + bounds.height));
        }
        if (borders.right.style != BorderStyle::None) {
            ctx->SetStrokePaint(borders.right.color);
            ctx->SetStrokeWidth(borders.right.width);
            ctx->DrawLine(Point2Df(bounds.x + bounds.width, bounds.y),
                          Point2Df(bounds.x + bounds.width, bounds.y + bounds.height));
        }
        if (borders.top.style != BorderStyle::None) {
            ctx->SetStrokePaint(borders.top.color);
            ctx->SetStrokeWidth(borders.top.width);
            ctx->DrawLine(Point2Df(bounds.x, bounds.y), Point2Df(bounds.x + bounds.width, bounds.y));
        }
        if (borders.bottom.style != BorderStyle::None) {
            ctx->SetStrokePaint(borders.bottom.color);
            ctx->SetStrokeWidth(borders.bottom.width);
            ctx->DrawLine(Point2Df(bounds.x, bounds.y + bounds.height),
                          Point2Df(bounds.x + bounds.width, bounds.y + bounds.height));
        }
    }
}

void UltraCanvasSpreadsheet::RenderSelection(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    const auto& selection = sheet->GetSelection();
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    // Clip to cell area
    ctx->PushState();
    ctx->ClipRect(Rect2Df(startX, startY,
                  gridBounds_.width - (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0),
                  gridBounds_.height - (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0)));

    // Draw selection ranges (fill)
    ctx->SetFillPaint(selectionColor_);
    for (const auto& range : selection.selections) {
        Point2Di topLeft = CellToScreen(range.start.row, range.start.col);
        Point2Di bottomRight = CellToScreen(range.end.row + 1, range.end.col + 1);

        ctx->FillRectangle(Rect2Df(topLeft.x, topLeft.y,
                      bottomRight.x - topLeft.x, bottomRight.y - topLeft.y));
    }

    // Draw selection border
    ctx->SetStrokePaint(selectionBorderColor_);
    ctx->SetStrokeWidth(2.0f);
    for (const auto& range : selection.selections) {
        Point2Di topLeft = CellToScreen(range.start.row, range.start.col);
        Point2Di bottomRight = CellToScreen(range.end.row + 1, range.end.col + 1);

        ctx->DrawRectangle(Rect2Df(topLeft.x, topLeft.y,
                      bottomRight.x - topLeft.x, bottomRight.y - topLeft.y));
    }

    // Draw active cell (white background with border)
    Point2Di activeTopLeft = CellToScreen(selection.activeCell.row, selection.activeCell.col);
    Rect2Di activeBounds = GetCellBounds(selection.activeCell.row, selection.activeCell.col);

    ctx->SetFillPaint(Colors::White);
    ctx->FillRectangle(Rect2Df(activeTopLeft.x + 1, activeTopLeft.y + 1,
                  activeBounds.width - 2, activeBounds.height - 2));

    // Re-render active cell content
    RenderCell(ctx, selection.activeCell.row, selection.activeCell.col,
               Rect2Di(activeTopLeft.x, activeTopLeft.y, activeBounds.width, activeBounds.height));

    // Active cell border
    ctx->SetStrokePaint(selectionBorderColor_);
    ctx->SetStrokeWidth(2.0f);
    ctx->DrawRectangle(Rect2Df(activeTopLeft.x, activeTopLeft.y, activeBounds.width, activeBounds.height));

    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderFormulaRangeHighlights(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;

    // Collect the cells and ranges referenced by the formula we want to trace:
    // either the formula currently being typed in-cell, or, when not editing,
    // the committed formula sitting in the active cell. This lets the user see
    // exactly which cells a calculation such as =SUM(E2:E6) covers.
    std::vector<CellRange> ranges;
    std::vector<CellAddress> cells;

    auto collect = [&](const std::shared_ptr<SpreadsheetFormula>& formula) {
        if (!formula || !formula->IsValid()) return;
        for (const auto& r : formula->GetRangeDependencies()) ranges.push_back(r);
        for (const auto& c : formula->GetDependencies()) cells.push_back(c);
    };

    if (editMode_ == SpreadsheetEditMode::InCellEditing &&
        !editBuffer_.empty() && editBuffer_[0] == '=') {
        auto live = std::make_shared<SpreadsheetFormula>(editBuffer_, editingCell_);
        live->Parse();
        collect(live);
    } else {
        const auto& selection = sheet->GetSelection();
        const SpreadsheetCell* cell =
            sheet->GetCellIfExists(selection.activeCell.row, selection.activeCell.col);
        if (cell && cell->HasFormula()) {
            collect(cell->GetFormula());
        }
    }

    if (ranges.empty() && cells.empty()) return;

    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);

    // Clip to the cell area so highlights never spill over the headers.
    ctx->PushState();
    ctx->ClipRect(Rect2Df(startX, startY,
                  gridBounds_.width - (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0),
                  gridBounds_.height - (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0)));

    // A small palette so multiple referenced ranges read as distinct, mirroring
    // the coloured precedents desktop spreadsheets draw while editing a formula.
    static const Color palette[] = {
        Color(0, 120, 215), Color(216, 80, 0), Color(16, 124, 16),
        Color(136, 23, 152), Color(193, 0, 90)
    };
    const int paletteSize = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    int colorIndex = 0;

    auto drawRange = [&](const CellRange& range) {
        Color base = palette[colorIndex % paletteSize];
        ++colorIndex;

        Point2Di topLeft = CellToScreen(range.start.row, range.start.col);
        Point2Di bottomRight = CellToScreen(range.end.row + 1, range.end.col + 1);
        Rect2Df rect(topLeft.x, topLeft.y,
                     bottomRight.x - topLeft.x, bottomRight.y - topLeft.y);

        Color fill = base;
        fill.a = 36;  // translucent tint over the covered cells
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(rect);

        ctx->SetStrokePaint(base);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRectangle(rect);
    };

    for (const auto& r : ranges) drawRange(r);
    for (const auto& c : cells) drawRange(CellRange(c));

    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderFreezeLines(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet || !sheet->HasFrozenPanes()) return;
    
    const auto& freeze = sheet->GetFreezePanes();
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    ctx->SetStrokePaint(frozenPaneLineColor_);
    ctx->SetStrokeWidth(2.0f);

    // Horizontal freeze line
    if (freeze.frozenRows > 0) {
        int y = startY;
        for (int r = 0; r < freeze.frozenRows; ++r) {
            y += sheet->GetRowHeight(r);
        }
        ctx->DrawLine(Point2Df(startX, y), Point2Df(gridBounds_.x + gridBounds_.width, y));
    }

    // Vertical freeze line
    if (freeze.frozenColumns > 0) {
        int x = startX;
        for (int c = 0; c < freeze.frozenColumns; ++c) {
            x += sheet->GetColumnWidth(c);
        }
        ctx->DrawLine(Point2Df(x, startY), Point2Df(x, gridBounds_.y + gridBounds_.height));
    }
}

void UltraCanvasSpreadsheet::RenderSheetTabs(IRenderContext* ctx) {
    int tabX = sheetTabsBounds_.x + 4;
    int tabY = sheetTabsBounds_.y;
    int tabHeight = sheetTabsBounds_.height;
    
    // Background
    ctx->SetFillPaint(headerBackgroundColor_);
    ctx->FillRectangle(Rect2Df(sheetTabsBounds_.x, sheetTabsBounds_.y,
                  sheetTabsBounds_.width, sheetTabsBounds_.height));

    // Navigation buttons placeholder
    int navWidth = 60;
    ctx->SetStrokePaint(gridlineColor_);
    ctx->DrawRectangle(Rect2Df(sheetTabsBounds_.x, tabY, navWidth, tabHeight));

    tabX += navWidth;

    // Sheet tabs
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(11);
    for (int i = 0; i < static_cast<int>(sheets_.size()); ++i) {
        const auto& sheet = sheets_[i];
        std::string name = sheet->GetName();
        int tabWidth = static_cast<int>(name.length() * 8) + 20;

        // Tab background
        ctx->SetFillPaint(i == activeSheetIndex_ ? sheetTabActiveColor_ : sheetTabInactiveColor_);
        ctx->FillRectangle(Rect2Df(tabX, tabY, tabWidth, tabHeight));

        // Tab border
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Df(tabX, tabY, tabWidth, tabHeight));

        // Tab color indicator
        if (sheet->GetTabColor() != Colors::Transparent) {
            ctx->SetFillPaint(sheet->GetTabColor());
            ctx->FillRectangle(Rect2Df(tabX, tabY + tabHeight - 3, tabWidth, 3));
        }

        // Tab name
        ctx->SetTextPaint(Colors::Black);
        ctx->DrawText(name, Point2Df(tabX + 10,
                      tabY + (tabHeight - ctx->GetTextLineHeight(name)) / 2));

        tabX += tabWidth;

        if (tabX >= sheetTabsBounds_.x + sheetTabsBounds_.width - 100) break;
    }

    // Add sheet button
    ctx->SetFillPaint(headerBackgroundColor_);
    ctx->FillRectangle(Rect2Df(tabX, tabY, 24, tabHeight));
    ctx->SetStrokePaint(gridlineColor_);
    ctx->DrawRectangle(Rect2Df(tabX, tabY, 24, tabHeight));
    ctx->SetTextPaint(Colors::Black);
    ctx->DrawText("+", Point2Df(tabX + 8, tabY + (tabHeight - ctx->GetTextLineHeight("+")) / 2));
}

void UltraCanvasSpreadsheet::RenderScrollbars(IRenderContext* ctx) {
    // Horizontal scrollbar
    ctx->SetFillPaint(Color(240, 240, 240));
    ctx->FillRectangle(Rect2Df(horizontalScrollBounds_.x, horizontalScrollBounds_.y,
                  horizontalScrollBounds_.width, horizontalScrollBounds_.height));
    ctx->SetStrokePaint(gridlineColor_);
    ctx->DrawRectangle(Rect2Df(horizontalScrollBounds_.x, horizontalScrollBounds_.y,
                  horizontalScrollBounds_.width, horizontalScrollBounds_.height));

    // Vertical scrollbar
    ctx->SetFillPaint(Color(240, 240, 240));
    ctx->FillRectangle(Rect2Df(verticalScrollBounds_.x, verticalScrollBounds_.y,
                  verticalScrollBounds_.width, verticalScrollBounds_.height));
    ctx->SetStrokePaint(gridlineColor_);
    ctx->DrawRectangle(Rect2Df(verticalScrollBounds_.x, verticalScrollBounds_.y,
                  verticalScrollBounds_.width, verticalScrollBounds_.height));

    // Thumb geometry: size proportional to the visible fraction of the used
    // range, position proportional to the scroll offset (consistent with the
    // ScrollRowFromTrackY/ScrollColumnFromTrackX drag mapping).
    auto* sheet = GetActiveSheet();
    if (sheet) {
        CellRange visible = GetVisibleRange();
        ctx->SetFillPaint(Color(180, 180, 180));

        // Horizontal thumb
        int maxCol = GetMaxScrollColumn();
        int visibleCols = std::max(1, visible.end.col - visible.start.col + 1);
        float hTrack = horizontalScrollBounds_.width;
        float hThumbW = (maxCol > 0)
            ? std::max(24.0f, hTrack * visibleCols / static_cast<float>(maxCol + visibleCols))
            : hTrack;
        float hFrac = (maxCol > 0) ? static_cast<float>(sheet->GetScrollColumn()) / maxCol : 0.0f;
        float hThumbX = horizontalScrollBounds_.x + hFrac * (hTrack - hThumbW);
        ctx->FillRectangle(Rect2Df(hThumbX, horizontalScrollBounds_.y + 2,
                                   hThumbW, horizontalScrollBounds_.height - 4));

        // Vertical thumb
        int maxRow = GetMaxScrollRow();
        int visibleRows = std::max(1, visible.end.row - visible.start.row + 1);
        float vTrack = verticalScrollBounds_.height;
        float vThumbH = (maxRow > 0)
            ? std::max(24.0f, vTrack * visibleRows / static_cast<float>(maxRow + visibleRows))
            : vTrack;
        float vFrac = (maxRow > 0) ? static_cast<float>(sheet->GetScrollRow()) / maxRow : 0.0f;
        float vThumbY = verticalScrollBounds_.y + vFrac * (vTrack - vThumbH);
        ctx->FillRectangle(Rect2Df(verticalScrollBounds_.x + 2, vThumbY,
                                   verticalScrollBounds_.width - 4, vThumbH));
    }
}

void UltraCanvasSpreadsheet::RenderEditCursor(IRenderContext* ctx) {
    if (!IsEditing()) return;

    // Match the font used to render the edit text so the caret X lines up.
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(11);

    int caretChars = std::max(0, std::min(editCursorPos_, static_cast<int>(editBuffer_.length())));
    int textWidth = ctx->GetTextLineWidth(editBuffer_.substr(0, caretChars));

    int caretX, caretTop, caretBottom;
    if (editMode_ == SpreadsheetEditMode::InCellEditing) {
        Rect2Di bounds = GetCellBounds(editingCell_.row, editingCell_.col);
        int padding = 3;
        caretX = bounds.x + padding + textWidth;
        caretTop = bounds.y + 3;
        caretBottom = bounds.y + bounds.height - 3;
    } else {
        // Formula bar editing: text origin matches RenderFormulaBar.
        int nameBoxWidth = 80;
        caretX = formulaBarBounds_.x + nameBoxWidth + 10 + textWidth;
        caretTop = formulaBarBounds_.y + 4;
        caretBottom = formulaBarBounds_.y + formulaBarBounds_.height - 4;
    }

    ctx->SetStrokePaint(Colors::Black);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Df(caretX, caretTop), Point2Df(caretX, caretBottom));
}

void UltraCanvasSpreadsheet::RenderClipboardIndicator(IRenderContext* ctx) {
    if (!HasClipboardData()) return;
    
    // Draw marching ants around clipboard range
    auto* sheet = GetSheetByName(clipboardSheetName_);
    if (!sheet || sheet != GetActiveSheet()) return;
    
    Point2Di topLeft = CellToScreen(clipboardRange_.start.row, clipboardRange_.start.col);
    Point2Di bottomRight = CellToScreen(clipboardRange_.end.row + 1, clipboardRange_.end.col + 1);
    
    ctx->SetStrokePaint(Colors::Black);
    ctx->SetStrokeWidth(1.0f);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
    ctx->DrawRectangle(Rect2Df(topLeft.x, topLeft.y,
                  bottomRight.x - topLeft.x, bottomRight.y - topLeft.y));
    ctx->SetLineDash(UCDashPattern{});
}

void UltraCanvasSpreadsheet::RenderAutoFillHandle(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet || IsEditing()) return;
    
    const auto& selection = sheet->GetSelection();
    if (selection.selections.empty()) return;
    
    // Get bottom-right of selection
    const auto& range = selection.selections[0];
    Point2Di corner = CellToScreen(range.end.row + 1, range.end.col + 1);
    
    // Draw small square handle
    int handleSize = 6;
    ctx->SetFillPaint(selectionBorderColor_);
    ctx->FillRectangle(Rect2Df(corner.x - handleSize, corner.y - handleSize, handleSize, handleSize));
}

// ============================================================================
// UIELEMENT INTERFACE - EVENTS
// ============================================================================

bool UltraCanvasSpreadsheet::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseDown:
            HandleMouseDown(event);
            return true;
        case UCEventType::MouseUp:
            HandleMouseUp(event);
            return true;
        case UCEventType::MouseMove:
            HandleMouseMove(event);
            return true;
        case UCEventType::MouseDoubleClick:
            HandleMouseDoubleClick(event);
            return true;
        case UCEventType::MouseWheel:
            HandleMouseWheel(event);
            return true;
        case UCEventType::KeyDown:
            HandleKeyDown(event);
            return true;
//        case UCEventType::KeyChar:
//            HandleKeyPress(event);
//            return true;
        default:
            return false;
    }
}

void UltraCanvasSpreadsheet::HandleMouseDown(const UCEvent& event) {
    // Become the focused element so keyboard navigation/editing events are
    // routed here (the window only delivers KeyDown to the focused element).
    SetFocus(true);

    mouseDown_ = true;
    mouseDownPos_ = Point2Di(event.pointer.x, event.pointer.y);

    HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);
    
    switch (hit.area) {
        case HitArea::Cell: {
            if (IsEditing()) {
                StopEditing(true);
            }
            
            CellAddress clickedCell(hit.row, hit.col);
            mouseDownCell_ = clickedCell;
            
            if (event.shift) {
                // Extend selection
                ExtendSelection(clickedCell);
            } else if (event.ctrl) {
                // Add to selection
                AddToSelection(CellRange(clickedCell));
            } else {
                // New selection
                SelectCell(hit.row, hit.col);
            }
            
            editMode_ = SpreadsheetEditMode::Selecting;
            if (onCellClick) onCellClick(hit.row, hit.col);
            Invalidate();
            break;
        }
        
        case HitArea::ColumnHeader: {
            if (IsEditing()) StopEditing(true);
            SelectColumn(hit.col);
            Invalidate();
            break;
        }
        
        case HitArea::RowHeader: {
            if (IsEditing()) StopEditing(true);
            SelectRow(hit.row);
            Invalidate();
            break;
        }
        
        case HitArea::CornerHeader: {
            if (IsEditing()) StopEditing(true);
            SelectAll();
            Invalidate();
            break;
        }
        
        case HitArea::ColumnResizer: {
            resizingColumn_ = hit.col;
            resizeStartPos_ = event.pointer.x;
            resizeStartSize_ = GetColumnWidth(hit.col);
            editMode_ = SpreadsheetEditMode::Resizing;
            break;
        }

        case HitArea::RowResizer: {
            resizingRow_ = hit.row;
            resizeStartPos_ = event.pointer.y;
            resizeStartSize_ = GetRowHeight(hit.row);
            editMode_ = SpreadsheetEditMode::Resizing;
            break;
        }
        
        case HitArea::SheetTab: {
            if (hit.tabIndex >= 0 && hit.tabIndex < static_cast<int>(sheets_.size())) {
                SetActiveSheet(hit.tabIndex);
            }
            break;
        }
        
        case HitArea::FormulaBar: {
            if (!IsEditing()) {
                StartEditing();
            }
            break;
        }
        
        case HitArea::AutoFillHandle: {
            editMode_ = SpreadsheetEditMode::AutoFilling;
            break;
        }

        case HitArea::HorizontalScrollbar: {
            if (auto* sheet = GetActiveSheet()) {
                draggingHScrollbar_ = true;
                UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                sheet->SetScrollPosition(sheet->GetScrollRow(),
                                         ScrollColumnFromTrackX(event.pointer.x));
                Invalidate();
            }
            break;
        }

        case HitArea::VerticalScrollbar: {
            if (auto* sheet = GetActiveSheet()) {
                draggingVScrollbar_ = true;
                UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                sheet->SetScrollPosition(ScrollRowFromTrackY(event.pointer.y),
                                         sheet->GetScrollColumn());
                Invalidate();
            }
            break;
        }

        default:
            break;
    }
}

void UltraCanvasSpreadsheet::HandleMouseUp(const UCEvent& event) {
    if (editMode_ == SpreadsheetEditMode::Resizing) {
        resizingColumn_ = -1;
        resizingRow_ = -1;
    }

    draggingHScrollbar_ = false;
    draggingVScrollbar_ = false;
    mouseDown_ = false;

    // Preserve an active editing session (e.g. started by double-click or by
    // typing); only reset transient modes like Selecting/Resizing.
    if (!IsEditing()) {
        editMode_ = SpreadsheetEditMode::Normal;
    }
}

void UltraCanvasSpreadsheet::HandleMouseMove(const UCEvent& event) {
    // Scrollbar dragging (button held down).
    if (draggingHScrollbar_) {
        if (auto* sheet = GetActiveSheet()) {
            sheet->SetScrollPosition(sheet->GetScrollRow(),
                                     ScrollColumnFromTrackX(event.pointer.x));
            Invalidate();
        }
        return;
    }
    if (draggingVScrollbar_) {
        if (auto* sheet = GetActiveSheet()) {
            sheet->SetScrollPosition(ScrollRowFromTrackY(event.pointer.y),
                                     sheet->GetScrollColumn());
            Invalidate();
        }
        return;
    }

    if (mouseDown_) {
        if (editMode_ == SpreadsheetEditMode::Selecting) {
            HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);
            if (hit.area == HitArea::Cell) {
                CellAddress current(hit.row, hit.col);
                if (auto* sheet = GetActiveSheet()) {
                    sheet->SelectRange(CellRange(mouseDownCell_, current));
                    Invalidate();
                }
            }
        }
        else if (editMode_ == SpreadsheetEditMode::Resizing) {
            if (resizingColumn_ >= 0) {
                int delta = event.pointer.x - resizeStartPos_;
                int newWidth = std::max(10, resizeStartSize_ + delta);
                SetColumnWidth(resizingColumn_, newWidth);
                Invalidate();
            }
            else if (resizingRow_ >= 0) {
                int delta = event.pointer.y - resizeStartPos_;
                int newHeight = std::max(5, resizeStartSize_ + delta);
                SetRowHeight(resizingRow_, newHeight);
                Invalidate();
            }
        }
        return;
    }

    // No button held: update the mouse cursor so the user can discover that
    // column/row boundaries are draggable (the application applies the hovered
    // element's cursor automatically).
    HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);
    switch (hit.area) {
        case HitArea::ColumnResizer: SetMouseCursor(UCMouseCursor::SizeWE); break;
        case HitArea::RowResizer:    SetMouseCursor(UCMouseCursor::SizeNS); break;
        case HitArea::FormulaBar:    SetMouseCursor(UCMouseCursor::Text);   break;
        default:                     SetMouseCursor(UCMouseCursor::Default); break;
    }
}

void UltraCanvasSpreadsheet::HandleMouseDoubleClick(const UCEvent& event) {
    HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);
    
    if (hit.area == HitArea::Cell) {
        StartInCellEditing();
        if (onCellDoubleClick) onCellDoubleClick(hit.row, hit.col);
    }
    else if (hit.area == HitArea::ColumnResizer) {
        AutoFitColumnWidth(hit.col);
        Invalidate();
    }
    else if (hit.area == HitArea::RowResizer) {
        AutoFitRowHeight(hit.row);
        Invalidate();
    }
}

void UltraCanvasSpreadsheet::HandleMouseWheel(const UCEvent& event) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    int scrollAmount = event.wheelDelta > 0 ? -3 : 3;

    if (event.shift) {
        // Horizontal scroll
        int newCol = sheet->GetScrollColumn() + scrollAmount;
        sheet->SetScrollPosition(sheet->GetScrollRow(), 
                                  std::max(0, std::min(newCol, SpreadsheetLimits::MaxColumns - 1)));
    } else {
        // Vertical scroll
        int newRow = sheet->GetScrollRow() + scrollAmount;
        sheet->SetScrollPosition(std::max(0, std::min(newRow, SpreadsheetLimits::MaxRows - 1)),
                                  sheet->GetScrollColumn());
    }
    
    Invalidate();
}

void UltraCanvasSpreadsheet::HandleKeyDown(const UCEvent& event) {
    if (IsEditing()) {
        // Handle editing keys, operating on editBuffer_ / editCursorPos_.
        int len = static_cast<int>(editBuffer_.length());
        switch (event.virtualKey) {
            case UCKeys::Enter:
                StopEditing(true);
                MoveActiveCell(event.shift ? -1 : 1, 0);
                return;
            case UCKeys::Tab:
                StopEditing(true);
                MoveActiveCell(0, event.shift ? -1 : 1);
                return;
            case UCKeys::Escape:
                CancelEditing();
                return;
            case UCKeys::Backspace:
                if (editCursorPos_ > 0) {
                    editBuffer_.erase(editCursorPos_ - 1, 1);
                    --editCursorPos_;
                    if (onFormulaBarChange) onFormulaBarChange();
                }
                Invalidate();
                return;
            case UCKeys::Delete:
                if (editCursorPos_ < len) {
                    editBuffer_.erase(editCursorPos_, 1);
                    if (onFormulaBarChange) onFormulaBarChange();
                }
                Invalidate();
                return;
            case UCKeys::Left:
                editCursorPos_ = std::max(0, editCursorPos_ - 1);
                Invalidate();
                return;
            case UCKeys::Right:
                editCursorPos_ = std::min(len, editCursorPos_ + 1);
                Invalidate();
                return;
            case UCKeys::Home:
                editCursorPos_ = 0;
                Invalidate();
                return;
            case UCKeys::End:
                editCursorPos_ = len;
                Invalidate();
                return;
            default:
                break;
        }
        // Printable character: insert at caret (Linux delivers the character
        // on the KeyDown event; there is no separate KeyChar event).
        if (!event.ctrl && !event.alt && !event.meta &&
            event.character >= 32 && event.character < 127) {
            InsertTextAtCursor(std::string(1, static_cast<char>(event.character)));
            Invalidate();
        }
        return;
    }

    // Navigation keys
    bool shift = event.shift;
    bool ctrl = event.ctrl;

    // A printable character starts editing the active cell directly (Excel-style),
    // replacing its previous content with the typed character.
    if (!ctrl && !event.alt && !event.meta &&
        event.character >= 32 && event.character < 127) {
        StartInCellEditing();
        editBuffer_ = std::string(1, static_cast<char>(event.character));
        editCursorPos_ = 1;
        if (onFormulaBarChange) onFormulaBarChange();
        Invalidate();
        return;
    }

    switch (event.virtualKey) {
        case UCKeys::Up:
            if (ctrl) MoveToDataRegionEdge(-1, 0);
            else MoveActiveCell(-1, 0);
            break;
        case UCKeys::Down:
            if (ctrl) MoveToDataRegionEdge(1, 0);
            else MoveActiveCell(1, 0);
            break;
        case UCKeys::Left:
            if (ctrl) MoveToDataRegionEdge(0, -1);
            else MoveActiveCell(0, -1);
            break;
        case UCKeys::Right:
            if (ctrl) MoveToDataRegionEdge(0, 1);
            else MoveActiveCell(0, 1);
            break;
        case UCKeys::Home:
            if (ctrl) MoveToCellBegin();
            else MoveToRowBegin();
            break;
        case UCKeys::End:
            if (ctrl) MoveToCellEnd();
            else MoveToRowEnd();
            break;
        case UCKeys::PageUp:
            PageUp();
            break;
        case UCKeys::PageDown:
            PageDown();
            break;
        case UCKeys::Enter:
            MoveActiveCell(shift ? -1 : 1, 0);
            break;
        case UCKeys::Tab:
            MoveActiveCell(0, shift ? -1 : 1);
            break;
        case UCKeys::Delete:
            ClearSelection();
            break;
        case UCKeys::F2:
            StartEditing();
            break;
        default:
            break;
    }

    // Shortcuts
    if (ctrl) {
        switch (event.virtualKey) {
            case UCKeys::C:
                Copy();
                break;
            case UCKeys::X:
                Cut();
                break;
            case UCKeys::V:
                Paste();
                break;
            case UCKeys::Z:
                if (shift) Redo();
                else Undo();
                break;
            case UCKeys::Y:
                Redo();
                break;
            case UCKeys::A:
                SelectAll();
                break;
            case UCKeys::B:
                SetSelectionBold(!GetActiveCellFont().bold);
                break;
            case UCKeys::I:
                SetSelectionItalic(!GetActiveCellFont().italic);
                break;
            case UCKeys::U:
                SetSelectionUnderline(GetActiveCellFont().underline == UnderlineStyle::None
                                       ? UnderlineStyle::Single : UnderlineStyle::None);
                break;
            default:
                break;
        }
    }

    Invalidate();
}

void UltraCanvasSpreadsheet::HandleKeyPress(const UCEvent& event) {
    if (IsEditing()) {
        // Add character to edit buffer
        if (event.character >= 32) {
            InsertTextAtCursor(std::string(1, static_cast<char>(event.character)));
            Invalidate();
        }
    } else {
        // Start editing with the typed character
        if (event.character >= 32) {
            StartEditing();
            editBuffer_ = std::string(1, static_cast<char>(event.character));
            editCursorPos_ = 1;
            Invalidate();
        }
    }
}

// ============================================================================
// HIT TESTING
// ============================================================================

UltraCanvasSpreadsheet::HitTestResult UltraCanvasSpreadsheet::HitTest(int x, int y) const {
    HitTestResult result;
    
    // Formula bar
    if (showFormulaBar_ && formulaBarBounds_.Contains(x, y)) {
        result.area = HitArea::FormulaBar;
        return result;
    }
    
    // Sheet tabs
    if (showSheetTabs_ && sheetTabsBounds_.Contains(x, y)) {
        result.area = HitArea::SheetTab;
        // Calculate which tab was clicked
        int tabX = sheetTabsBounds_.x + 64;
        for (int i = 0; i < static_cast<int>(sheets_.size()); ++i) {
            int tabWidth = static_cast<int>(sheets_[i]->GetName().length() * 8) + 20;
            if (x >= tabX && x < tabX + tabWidth) {
                result.tabIndex = i;
                break;
            }
            tabX += tabWidth;
        }
        return result;
    }
    
    // Scrollbars
    if (showScrollbars_) {
        if (horizontalScrollBounds_.Contains(x, y)) {
            result.area = HitArea::HorizontalScrollbar;
            return result;
        }
        if (verticalScrollBounds_.Contains(x, y)) {
            result.area = HitArea::VerticalScrollbar;
            return result;
        }
    }
    
    int headerHeight = showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0;
    int headerWidth = showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0;
    
    // Corner header
    if (x < gridBounds_.x + headerWidth && y < gridBounds_.y + headerHeight) {
        result.area = HitArea::CornerHeader;
        return result;
    }
    
    // Column headers
    if (showColumnHeaders_ && y < gridBounds_.y + headerHeight) {
        result.area = HitArea::ColumnHeader;
        result.col = ScreenToCell(x, gridBounds_.y + headerHeight + 1).col;
        
        // Check for resizer
        if (auto* sheet = GetActiveSheet()) {
            int colX = gridBounds_.x + headerWidth;
            for (int c = sheet->GetScrollColumn(); c < SpreadsheetLimits::MaxColumns; ++c) {
                colX += sheet->GetColumnWidth(c);
                if (std::abs(x - colX) < 4) {
                    result.area = HitArea::ColumnResizer;
                    result.col = c;
                    break;
                }
                if (colX > x) break;
            }
        }
        return result;
    }
    
    // Row headers
    if (showRowHeaders_ && x < gridBounds_.x + headerWidth) {
        result.area = HitArea::RowHeader;
        result.row = ScreenToCell(gridBounds_.x + headerWidth + 1, y).row;
        
        // Check for resizer
        if (auto* sheet = GetActiveSheet()) {
            int rowY = gridBounds_.y + headerHeight;
            for (int r = sheet->GetScrollRow(); r < SpreadsheetLimits::MaxRows; ++r) {
                rowY += sheet->GetRowHeight(r);
                if (std::abs(y - rowY) < 4) {
                    result.area = HitArea::RowResizer;
                    result.row = r;
                    break;
                }
                if (rowY > y) break;
            }
        }
        return result;
    }
    
    // Cell area
    if (gridBounds_.Contains(x, y)) {
        CellAddress cell = ScreenToCell(x, y);
        result.area = HitArea::Cell;
        result.row = cell.row;
        result.col = cell.col;
        
        // Check for auto-fill handle
        if (auto* sheet = GetActiveSheet()) {
            const auto& selection = sheet->GetSelection();
            if (!selection.selections.empty()) {
                const auto& range = selection.selections[0];
                Point2Di corner = CellToScreen(range.end.row + 1, range.end.col + 1);
                if (std::abs(x - corner.x) < 6 && std::abs(y - corner.y) < 6) {
                    result.area = HitArea::AutoFillHandle;
                }
            }
        }
    }
    
    return result;
}

// ============================================================================
// COORDINATE CONVERSION
// ============================================================================

Point2Di UltraCanvasSpreadsheet::CellToScreen(int row, int col) const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return Point2Di(0, 0);
    
    int x = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int y = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    // Account for scroll position
    int scrollRow = sheet->GetScrollRow();
    int scrollCol = sheet->GetScrollColumn();
    
    for (int c = scrollCol; c < col; ++c) {
        if (!sheet->IsColumnHidden(c)) {
            x += sheet->GetColumnWidth(c);
        }
    }
    
    for (int r = scrollRow; r < row; ++r) {
        if (!sheet->IsRowHidden(r)) {
            y += sheet->GetRowHeight(r);
        }
    }
    
    return Point2Di(x, y);
}

CellAddress UltraCanvasSpreadsheet::ScreenToCell(int x, int y) const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return CellAddress(0, 0);
    
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    int scrollRow = sheet->GetScrollRow();
    int scrollCol = sheet->GetScrollColumn();
    
    // Find column
    int col = scrollCol;
    int accX = startX;
    while (col < SpreadsheetLimits::MaxColumns && accX < x) {
        if (!sheet->IsColumnHidden(col)) {
            int w = sheet->GetColumnWidth(col);
            if (accX + w > x) break;
            accX += w;
        }
        ++col;
    }
    
    // Find row
    int row = scrollRow;
    int accY = startY;
    while (row < SpreadsheetLimits::MaxRows && accY < y) {
        if (!sheet->IsRowHidden(row)) {
            int h = sheet->GetRowHeight(row);
            if (accY + h > y) break;
            accY += h;
        }
        ++row;
    }
    
    return CellAddress(row, col);
}

CellRange UltraCanvasSpreadsheet::GetVisibleRange() const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return CellRange(0, 0, 0, 0);
    
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    int viewWidth = gridBounds_.width - (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int viewHeight = gridBounds_.height - (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    return sheet->GetVisibleRange(viewWidth, viewHeight);
}

Rect2Di UltraCanvasSpreadsheet::GetCellBounds(int row, int col) const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return Rect2Di(0, 0, 0, 0);
    
    Point2Di topLeft = CellToScreen(row, col);
    int width = sheet->GetColumnWidth(col);
    int height = sheet->GetRowHeight(row);

    return Rect2Di(topLeft.x, topLeft.y, width, height);
}

// ============================================================================
// SCROLLBAR GEOMETRY / MAPPING
// ============================================================================

// Scrolling is bounded to the used data range so the thumb stays usable
// (mapping the full grid of MaxRows/MaxColumns would make it effectively
// immovable). At max scroll the last used row/column reaches the top/left.
int UltraCanvasSpreadsheet::GetMaxScrollRow() const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return 0;
    return std::max(0, sheet->GetUsedRange().end.row);
}

int UltraCanvasSpreadsheet::GetMaxScrollColumn() const {
    auto* sheet = GetActiveSheet();
    if (!sheet) return 0;
    return std::max(0, sheet->GetUsedRange().end.col);
}

int UltraCanvasSpreadsheet::ScrollRowFromTrackY(int y) const {
    int maxRow = GetMaxScrollRow();
    if (maxRow <= 0 || verticalScrollBounds_.height <= 0) return 0;
    float frac = (y - verticalScrollBounds_.y) / verticalScrollBounds_.height;
    frac = std::max(0.0f, std::min(1.0f, frac));
    return static_cast<int>(frac * maxRow + 0.5f);
}

int UltraCanvasSpreadsheet::ScrollColumnFromTrackX(int x) const {
    int maxCol = GetMaxScrollColumn();
    if (maxCol <= 0 || horizontalScrollBounds_.width <= 0) return 0;
    float frac = (x - horizontalScrollBounds_.x) / horizontalScrollBounds_.width;
    frac = std::max(0.0f, std::min(1.0f, frac));
    return static_cast<int>(frac * maxCol + 0.5f);
}

// ============================================================================
// RESIZE
// ============================================================================

void UltraCanvasSpreadsheet::Arrange(const UltraCanvas::Rect2Df &newFinalRect, const CSSLayout::LayoutContext &ctx) {
    UltraCanvasUIElement::Arrange(newFinalRect, ctx);
    UpdateLayout();
}

void UltraCanvasSpreadsheet::UpdateLayout() {
    CalculateBounds();
}

void UltraCanvasSpreadsheet::CalculateBounds() {
    float x = 0, y = 0;
    float w = GetWidth(), h = GetHeight();
    
    if (showFormulaBar_) {
        formulaBarBounds_ = Rect2Di(x, y, w, formulaBarHeight_);
        y += formulaBarHeight_;
        h -= formulaBarHeight_;
    }
    
    if (showSheetTabs_) {
        sheetTabsBounds_ = Rect2Di(x, y + h - sheetTabHeight_, 
                                    w - (showScrollbars_ ? scrollbarSize_ : 0), sheetTabHeight_);
        h -= sheetTabHeight_;
    }
    
    if (showScrollbars_) {
        horizontalScrollBounds_ = Rect2Di(
            x + SpreadsheetLimits::RowHeaderWidth, 
            y + h - scrollbarSize_,
            w - SpreadsheetLimits::RowHeaderWidth - scrollbarSize_,
            scrollbarSize_);
        verticalScrollBounds_ = Rect2Di(
            x + w - scrollbarSize_, 
            y, 
            scrollbarSize_, 
            h - scrollbarSize_);
        w -= scrollbarSize_;
        h -= scrollbarSize_;
    }
    
    gridBounds_ = Rect2Di(x, y, w, h);
}

// ============================================================================
// FOCUS
// ============================================================================

bool UltraCanvasSpreadsheet::SetFocus(bool focus) {
    bool r = UltraCanvasUIElement::SetFocus(focus);
    if (!focus && IsEditing()) StopEditing(true);
    RequestRedraw();
    return r;
}

// ============================================================================
// EDITING
// ============================================================================

void UltraCanvasSpreadsheet::StartEditing() {
    CellAddress active = GetActiveCell();
    StartEditingAt(active.row, active.col);
}

void UltraCanvasSpreadsheet::StartEditingAt(int row, int col) {
    if (IsEditing()) {
        StopEditing(true);
    }
    
    editingCell_ = CellAddress(row, col);
    editMode_ = SpreadsheetEditMode::Editing;
    
    if (auto* cell = GetCellIfExists(row, col)) {
        editBuffer_ = cell->HasFormula() ? cell->GetFormulaText() : cell->GetText();
    } else {
        editBuffer_.clear();
    }
    
    editCursorPos_ = static_cast<int>(editBuffer_.length());
    editSelectionStart_ = -1;
    
    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::StartInCellEditing() {
    CellAddress active = GetActiveCell();
    editingCell_ = active;
    editMode_ = SpreadsheetEditMode::InCellEditing;
    
    if (auto* cell = GetCellIfExists(active.row, active.col)) {
        editBuffer_ = cell->HasFormula() ? cell->GetFormulaText() : cell->GetText();
    } else {
        editBuffer_.clear();
    }
    
    editCursorPos_ = static_cast<int>(editBuffer_.length());
    editSelectionStart_ = -1;
    
    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::StopEditing(bool confirm) {
    if (!IsEditing()) return;
    
    if (confirm) {
        CommitEdit();
    }
    
    editMode_ = SpreadsheetEditMode::Normal;
    editBuffer_.clear();
    editCursorPos_ = 0;
    editSelectionStart_ = -1;
    
    UpdateFormulaBar();
    Invalidate();
}

void UltraCanvasSpreadsheet::CancelEditing() {
    StopEditing(false);
}

void UltraCanvasSpreadsheet::CommitEdit() {
    if (editBuffer_.empty()) {
        ClearCell(editingCell_.row, editingCell_.col);
    } else if (editBuffer_[0] == '=') {
        SetCellFormula(editingCell_.row, editingCell_.col, editBuffer_);
    } else {
        GetCell(editingCell_.row, editingCell_.col)->SetValueFromString(editBuffer_);
    }
}

void UltraCanvasSpreadsheet::SetEditBuffer(const std::string& text) {
    editBuffer_ = text;
    editCursorPos_ = static_cast<int>(text.length());
    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::InsertTextAtCursor(const std::string& text) {
    editBuffer_.insert(editCursorPos_, text);
    editCursorPos_ += static_cast<int>(text.length());
    if (onFormulaBarChange) onFormulaBarChange();
}

// ============================================================================
// NAVIGATION
// ============================================================================

void UltraCanvasSpreadsheet::MoveActiveCell(int rowDelta, int colDelta) {
    CellAddress active = GetActiveCell();
    int newRow = std::max(0, std::min(active.row + rowDelta, SpreadsheetLimits::MaxRows - 1));
    int newCol = std::max(0, std::min(active.col + colDelta, SpreadsheetLimits::MaxColumns - 1));
    
    SetActiveCell(newRow, newCol);
    EnsureCellVisible(newRow, newCol);
}

void UltraCanvasSpreadsheet::MoveToCellBegin() {
    SetActiveCell(0, 0);
    ScrollToCell(0, 0);
}

void UltraCanvasSpreadsheet::MoveToCellEnd() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange used = sheet->GetUsedRange();
    SetActiveCell(used.end.row, used.end.col);
    EnsureCellVisible(used.end.row, used.end.col);
}

void UltraCanvasSpreadsheet::MoveToRowBegin() {
    CellAddress active = GetActiveCell();
    SetActiveCell(active.row, 0);
    EnsureCellVisible(active.row, 0);
}

void UltraCanvasSpreadsheet::MoveToRowEnd() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellAddress active = GetActiveCell();
    CellRange used = sheet->GetUsedRange();
    int endCol = std::max(0, used.end.col);
    
    SetActiveCell(active.row, endCol);
    EnsureCellVisible(active.row, endCol);
}

void UltraCanvasSpreadsheet::MoveToDataRegionEdge(int rowDir, int colDir) {
    // Ctrl+Arrow behavior - move to edge of data region
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellAddress active = GetActiveCell();
    int row = active.row;
    int col = active.col;
    
    bool startedOnData = sheet->HasCell(row, col) && !sheet->GetCellIfExists(row, col)->IsEmpty();
    
    while (true) {
        int nextRow = row + rowDir;
        int nextCol = col + colDir;
        
        if (nextRow < 0 || nextRow >= SpreadsheetLimits::MaxRows ||
            nextCol < 0 || nextCol >= SpreadsheetLimits::MaxColumns) {
            break;
        }
        
        bool hasData = sheet->HasCell(nextRow, nextCol) && 
                       !sheet->GetCellIfExists(nextRow, nextCol)->IsEmpty();
        
        if (startedOnData && !hasData) {
            break;
        }
        if (!startedOnData && hasData) {
            row = nextRow;
            col = nextCol;
            break;
        }
        
        row = nextRow;
        col = nextCol;
    }
    
    SetActiveCell(row, col);
    EnsureCellVisible(row, col);
}

void UltraCanvasSpreadsheet::PageUp() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange visible = GetVisibleRange();
    int pageRows = visible.end.row - visible.start.row;
    MoveActiveCell(-pageRows, 0);
}

void UltraCanvasSpreadsheet::PageDown() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange visible = GetVisibleRange();
    int pageRows = visible.end.row - visible.start.row;
    MoveActiveCell(pageRows, 0);
}

void UltraCanvasSpreadsheet::ScrollToCell(int row, int col) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    sheet->SetScrollPosition(row, col);
    Invalidate();
}

void UltraCanvasSpreadsheet::EnsureCellVisible(int row, int col) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange visible = GetVisibleRange();
    int scrollRow = sheet->GetScrollRow();
    int scrollCol = sheet->GetScrollColumn();
    
    if (row < visible.start.row) {
        scrollRow = row;
    } else if (row > visible.end.row - 1) {
        scrollRow = row - (visible.end.row - visible.start.row) + 1;
    }
    
    if (col < visible.start.col) {
        scrollCol = col;
    } else if (col > visible.end.col - 1) {
        scrollCol = col - (visible.end.col - visible.start.col) + 1;
    }
    
    sheet->SetScrollPosition(std::max(0, scrollRow), std::max(0, scrollCol));
    Invalidate();
}

// ============================================================================
// COLUMN/ROW OPERATIONS
// ============================================================================

int UltraCanvasSpreadsheet::GetColumnWidth(int col) const {
    auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetColumnWidth(col) : SpreadsheetLimits::DefaultColumnWidth;
}

void UltraCanvasSpreadsheet::SetColumnWidth(int col, int width) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetColumnWidth(col, width);
        Invalidate();
    }
}

void UltraCanvasSpreadsheet::AutoFitColumnWidth(int col) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    // Calculate max content width
    int maxWidth = SpreadsheetLimits::DefaultColumnWidth;
    CellRange used = sheet->GetUsedRange();
    
    for (int row = used.start.row; row <= used.end.row; ++row) {
        if (auto* cell = sheet->GetCellIfExists(row, col)) {
            std::string text = cell->GetDisplayValue();
            int textWidth = static_cast<int>(text.length() * 8) + 10;
            maxWidth = std::max(maxWidth, textWidth);
        }
    }
    
    sheet->SetColumnWidth(col, std::min(maxWidth, 500));
    Invalidate();
}

int UltraCanvasSpreadsheet::GetRowHeight(int row) const {
    auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetRowHeight(row) : SpreadsheetLimits::DefaultRowHeight;
}

void UltraCanvasSpreadsheet::SetRowHeight(int row, int height) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetRowHeight(row, height);
        Invalidate();
    }
}

void UltraCanvasSpreadsheet::AutoFitRowHeight(int row) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    // For now, use default height - proper implementation would calculate based on content
    sheet->SetRowHeight(row, SpreadsheetLimits::DefaultRowHeight);
    Invalidate();
}

// ============================================================================
// CLIPBOARD
// ============================================================================

void UltraCanvasSpreadsheet::Copy() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    clipboardRange_ = GetSelection();
    clipboardSheetName_ = sheet->GetName();
    clipboardOp_ = ClipboardOperation::Copy;
    clipboardData_ = sheet->CopyCells(clipboardRange_);
    
    Invalidate();
}

void UltraCanvasSpreadsheet::Cut() {
    Copy();
    clipboardOp_ = ClipboardOperation::Cut;
}

void UltraCanvasSpreadsheet::Paste() {
    PasteSpecial(PasteOptions());
}

void UltraCanvasSpreadsheet::PasteSpecial(const PasteOptions& options) {
    if (!HasClipboardData()) return;
    
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellAddress dest = GetActiveCell();
    sheet->PasteCells(dest, clipboardData_, options);
    
    if (clipboardOp_ == ClipboardOperation::Cut) {
        // Clear original cells
        if (auto* srcSheet = GetSheetByName(clipboardSheetName_)) {
            srcSheet->ClearRange(clipboardRange_);
        }
        ClearClipboard();
    }
    
    Invalidate();
}

void UltraCanvasSpreadsheet::ClearClipboard() {
    clipboardOp_ = ClipboardOperation::None;
    clipboardData_.clear();
    Invalidate();
}

// ============================================================================
// FORMATTING
// ============================================================================

void UltraCanvasSpreadsheet::SetSelectionBold(bool bold) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange sel = GetSelection();
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetBold(bold);
        }
    }
    Invalidate();
}

void UltraCanvasSpreadsheet::SetSelectionItalic(bool italic) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange sel = GetSelection();
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetItalic(italic);
        }
    }
    Invalidate();
}

void UltraCanvasSpreadsheet::SetSelectionUnderline(UnderlineStyle style) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange sel = GetSelection();
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetUnderline(style);
        }
    }
    Invalidate();
}

CellFont UltraCanvasSpreadsheet::GetActiveCellFont() const {
    CellAddress active = GetActiveCell();
    if (auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().font;
    }
    return CellFont();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void UltraCanvasSpreadsheet::InitializeDefaultSheet() {
    auto sheet = std::make_unique<SpreadsheetSheet>("Sheet1", 0);
    sheet->SetParent(this);
    sheet->SetSelected(true);
    SetupSheetCallbacks(sheet.get());
    sheets_.push_back(std::move(sheet));
    activeSheetIndex_ = 0;
}

void UltraCanvasSpreadsheet::SetupSheetCallbacks(SpreadsheetSheet* sheet) {
    sheet->onCellChange = [this](int row, int col) {
        Invalidate();
        if (onCellChange) onCellChange(row, col);
    };
    
    sheet->onSelectionChange = [this]() {
        UpdateFormulaBar();
        Invalidate();
        if (onSelectionChange) onSelectionChange();
    };
    
    sheet->onStructureChange = [this]() {
        Invalidate();
        if (onStructureChange) onStructureChange();
    };
}

std::string UltraCanvasSpreadsheet::GenerateUniqueSheetName() const {
    int num = static_cast<int>(sheets_.size()) + 1;
    std::string name;
    do {
        name = "Sheet" + std::to_string(num++);
    } while (GetSheetByName(name) != nullptr);
    return name;
}

void UltraCanvasSpreadsheet::UpdateFormulaBar() {
    if (!showFormulaBar_) return;
    
    CellAddress active = GetActiveCell();
    if (auto* cell = GetCellIfExists(active.row, active.col)) {
        if (cell->HasFormula()) {
            editBuffer_ = cell->GetFormulaText();
        } else {
            editBuffer_ = cell->GetDisplayValue();
        }
    } else {
        editBuffer_.clear();
    }
    
    if (onFormulaBarChange) onFormulaBarChange();
}

void UltraCanvasSpreadsheet::Invalidate() {
    // Request redraw - connects to UltraCanvasUIElement's invalidation
    RequestRedraw();
}

size_t UltraCanvasSpreadsheet::GetMemorySize() const {
    size_t size = sizeof(UltraCanvasSpreadsheet);
    
    for (const auto& sheet : sheets_) {
        size += sheet->GetMemorySize();
    }
    
    for (const auto& [name, range] : namedRanges_) {
        size += name.capacity() + sizeof(NamedRange);
    }
    
    return size;
}

} // namespace UltraCanvas
