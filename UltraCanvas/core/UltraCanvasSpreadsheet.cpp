// core/UltraCanvasSpreadsheet.cpp
// Main spreadsheet UI component implementation
// Version: 1.3.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include <stdexcept>
#include "UltraCanvasApplication.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasModalDialog.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

UltraCanvasSpreadsheet::UltraCanvasSpreadsheet()
    : UltraCanvasContainer("spreadsheet")
{
    formulaEngine_ = std::make_unique<SpreadsheetFormulaEngine>();
    formulaEngine_->SetSpreadsheet(this);
    InitializeDefaultSheet();
}

UltraCanvasSpreadsheet::UltraCanvasSpreadsheet(
    const std::string& id,
    float x, float y, float width, float height)
    : UltraCanvasContainer(id, x, y, width, height)
{
    formulaEngine_ = std::make_unique<SpreadsheetFormulaEngine>();
    formulaEngine_->SetSpreadsheet(this);
    InitializeDefaultSheet();
    UpdateLayout();
}


// ============================================================================
// UIELEMENT INTERFACE - RENDER
// ============================================================================

// Declared in UltraCanvasSpreadsheet.h. Restores the context's font state
// after measuring so callers can use it mid-render.
CellTextMeasureFn MakeSpreadsheetTextMeasurer(IRenderContext* ctx) {
    if (!ctx) return nullptr;
    return [ctx](const std::string& text, const CellFont& font) {
        ctx->PushState();
        ctx->SetFontFace(font.family.empty() ? "Arial" : font.family,
                         font.bold ? FontWeight::Bold : FontWeight::Normal,
                         font.italic ? FontSlant::Italic : FontSlant::Normal);
        ctx->SetFontSize(font.size > 0.0f ? font.size : 11.0f);
        int width = ctx->GetTextLineWidth(text);
        ctx->PopState();
        return width;
    };
}

void UltraCanvasSpreadsheet::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;

    // A file loaded before the grid was on a window left its unsized columns
    // waiting for real font metrics; this is the first moment we have them.
    if (autoFitPending_) {
        autoFitPending_ = false;
        CellTextMeasureFn measure = MakeSpreadsheetTextMeasurer(ctx);
        for (auto& sheet : sheets_) {
            if (sheet) sheet->AutoFitUnsizedColumns(measure);
        }
    }

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
        RenderCellEditor(ctx);
    }

    RenderAutoFillHandle(ctx);

    if (showSheetTabs_) {
        RenderSheetTabs(ctx);
    }

    if (showScrollbars_) {
        RenderScrollbars(ctx);
    }

    // Over everything else: it may reach across the headers and the grid.
    if (IsEditing()) {
        RenderFunctionHint(ctx);
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

    // The "fx" button that opens the function menu
    const Rect2Di fx = FunctionMenuButtonRect();
    if (fx.width > 0) {
        ctx->SetFillPaint(headerBackgroundColor_);
        ctx->FillRectangle(Rect2Df(fx.x, fx.y, fx.width, fx.height));
        ctx->SetStrokePaint(gridlineColor_);
        ctx->DrawRectangle(Rect2Df(fx.x, fx.y, fx.width, fx.height));
        ctx->SetFontFace("Times New Roman", FontWeight::Bold, FontSlant::Italic);
        ctx->SetFontSize(13);
        const std::string fxLabel = "fx";
        const int arrowW = 7;
        const int labelW = ctx->GetTextLineWidth(fxLabel);
        const int startX = fx.x + (fx.width - labelW - arrowW - 3) / 2;
        ctx->SetTextPaint(headerTextColor_);
        ctx->DrawText(fxLabel, Point2Df(startX, fx.y + (fx.height - ctx->GetTextLineHeight(fxLabel)) / 2));
        const double ax = startX + labelW + 3, ay = fx.y + fx.height * 0.5 - 1.5;
        ctx->SetFillPaint(headerTextColor_);
        ctx->FillLinePath(std::vector<Point2Dd>{ Point2Dd(ax, ay), Point2Dd(ax + arrowW, ay),
                                                 Point2Dd(ax + arrowW / 2.0, ay + 4) });
        ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(11);
    }

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

    const int textRight = fx.width > 0 ? fx.x - 4 : formulaBarBounds_.x + formulaBarBounds_.width - 4;
    ctx->PushState();
    ctx->ClipRect(Rect2Df(textX, formulaBarBounds_.y, static_cast<float>(std::max(0, textRight - textX)), formulaBarBounds_.height));
    // Being edited here, the editor draws the text itself; drawn as well, the
    // two copies sat a few pixels apart and read as "==SUM(...)".
    if (!(editMode_ == SpreadsheetEditMode::Editing && cellEditor)) {
        ctx->DrawText(displayText,
                      Point2Df(textX,
                      formulaBarBounds_.y + (formulaBarBounds_.height - ctx->GetTextLineHeight(displayText)) / 2));
    }
    ctx->PopState();
}

Rect2Di UltraCanvasSpreadsheet::FunctionMenuButtonRect() const {
    if (!showFormulaBar_ || !functionMenuEnabled_) return Rect2Di(0, 0, 0, 0);
    const int width = 36;
    return Rect2Di(formulaBarBounds_.x + formulaBarBounds_.width - width - 2,
                   formulaBarBounds_.y + 2, width, std::max(0, static_cast<int>(formulaBarBounds_.height) - 4));
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

    // A selected block of rows gets a sort button in each of its column headers.
    CellRange sortRange;
    const bool sortable = GetHeaderSortableRange(sortRange);
    const int sortedColumn = GetHeaderSortColumn();
    
    // The last column is usually only partly in view: clip its header at the
    // grid's right edge, as the cells below it are, instead of letting it run
    // under the vertical scrollbar.
    ctx->PushState();
    ctx->ClipRect(Rect2Df(startX, headerY, gridBounds_.x + gridBounds_.width - startX, headerHeight));

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

        if (sortable && col >= sortRange.start.col && col <= sortRange.end.col) {
            Rect2Di button = GetHeaderSortButtonRect(x, colWidth);
            if (button.width > 0) {
                RenderHeaderSortButton(ctx, button, col == sortedColumn, headerSortAscending_,
                                       isSelected ? Colors::White : headerTextColor_);
            }
        }

        x += colWidth;
    }
    ctx->PopState();
}

void UltraCanvasSpreadsheet::RequestSortSelectionByColumn(int column, SortOrder order) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    const CellRange range = GetSelection();
    const int formulas = sortFormulaWarningEnabled_ ? sheet->CountFormulaCells(range) : 0;
    if (formulas == 0) {
        SortSelectionByColumn(column, order);
        return;
    }

    const std::string message =
        "The selected block " + range.ToString() + " contains " + std::to_string(formulas) +
        (formulas == 1 ? " formula cell." : " formula cells.") +
        "\n\nSorting moves formulas with their rows but does not rewrite their cell "
        "references: a formula such as =D3/C3 keeps pointing at row 3 after its row "
        "has moved, and may then calculate from the wrong row.\n\n"
        "Click OK to sort anyway, or Cancel to leave the block as it is.";

    // The dialog may outlive this element, so the callback holds it weakly,
    // and it sorts only if the same block is still selected on OK.
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowMessage(message, "Sort selection", DialogType::Warning,
        DialogButtons::OKCancel,
        [weakSelf, range, column, order](DialogResult result) {
            if (result != DialogResult::OK) return;
            auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
            if (!self || !(self->GetSelection() == range)) return;
            self->SortSelectionByColumn(column, order);
        },
        GetWindow());
}

Rect2Di UltraCanvasSpreadsheet::GetHeaderSortButtonRect(int colX, int colWidth) const {
    const int size = 14;
    // Leave the column letter room in the middle and keep clear of the
    // resize grip on the right-hand border.
    if (colWidth < 2 * size + 16) return Rect2Di(0, 0, 0, 0);
    const int headerHeight = SpreadsheetLimits::HeaderRowHeight;
    return Rect2Di(colX + colWidth - size - 6,
                   static_cast<int>(gridBounds_.y) + (headerHeight - size) / 2, size, size);
}

// The button is geometry, like the ListView's sort indicator: an outline with
// both directions offered while the block is unsorted, and a single filled
// triangle (apex up ascending, apex down descending) once it has been sorted
// by this column.
void UltraCanvasSpreadsheet::RenderHeaderSortButton(IRenderContext* ctx, const Rect2Di& button,
                                                    bool sorted, bool ascending, const Color& color) {
    ctx->SetStrokePaint(Color(color.r, color.g, color.b, 140));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRoundedRectangle(Rect2Dd(button.x + 0.5, button.y + 0.5, button.width - 1, button.height - 1), 2.0);

    const double cx = button.x + button.width * 0.5;
    const double cy = button.y + button.height * 0.5;
    auto up = [&](double top, double w, double h) {
        return std::vector<Point2Dd>{ Point2Dd(cx, top), Point2Dd(cx + w / 2, top + h), Point2Dd(cx - w / 2, top + h) };
    };
    auto down = [&](double top, double w, double h) {
        return std::vector<Point2Dd>{ Point2Dd(cx - w / 2, top), Point2Dd(cx + w / 2, top), Point2Dd(cx, top + h) };
    };

    if (sorted) {
        ctx->SetFillPaint(color);
        ctx->FillLinePath(ascending ? up(cy - 2.5, 8, 5) : down(cy - 2.5, 8, 5));
    } else {
        ctx->SetFillPaint(Color(color.r, color.g, color.b, 190));
        ctx->FillLinePath(up(cy - 5, 6, 3.5));
        ctx->FillLinePath(down(cy + 1.5, 6, 3.5));
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
    
    // The last row is usually only partly in view: clip its header at the
    // grid's bottom edge so it is cut off in step with its cells, instead of
    // being drawn whole across the horizontal scrollbar's row.
    ctx->PushState();
    ctx->ClipRect(Rect2Df(headerX, startY, headerWidth, gridBounds_.y + gridBounds_.height - startY));

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
    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderCells(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange visible = GetVisibleRange();
    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);
    
    // Clip to cell area
    ctx->PushState();
    ctx->ClipRect(CellAreaRect());

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
    // of the committed value; both come from the editor child.
    if (editMode_ == SpreadsheetEditMode::InCellEditing &&
        row == editingCell_.row && col == editingCell_.col) {
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(Rect2Df(bounds.x, bounds.y, bounds.width, bounds.height));

        // The live text and caret belong to the editor child, drawn by
        // RenderCellEditor once the grid is done (so it sits on top).
        return;
    }

    // Cell background
    if (cell && cell->HasCustomStyle()) {
        const auto& style = cell->GetStyle();
        if (style.fill.pattern != FillPattern::NoFill) {
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
        // Negative values in a "red negatives" number format paint red, as in
        // the authoring application; otherwise the style's own font colour wins.
        Color textColor = style.font.color;
        if (style.numberFormat.negativeRed && cell->IsNumeric() &&
            cell->GetNumber() < 0.0) {
            textColor = Color(255, 0, 0);
        }
        ctx->SetTextPaint(textColor);

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

        if (borders.left.style != BorderStyle::NoBorder) {
            ctx->SetStrokePaint(borders.left.color);
            ctx->SetStrokeWidth(borders.left.width);
            ctx->DrawLine(Point2Df(bounds.x, bounds.y), Point2Df(bounds.x, bounds.y + bounds.height));
        }
        if (borders.right.style != BorderStyle::NoBorder) {
            ctx->SetStrokePaint(borders.right.color);
            ctx->SetStrokeWidth(borders.right.width);
            ctx->DrawLine(Point2Df(bounds.x + bounds.width, bounds.y),
                          Point2Df(bounds.x + bounds.width, bounds.y + bounds.height));
        }
        if (borders.top.style != BorderStyle::NoBorder) {
            ctx->SetStrokePaint(borders.top.color);
            ctx->SetStrokeWidth(borders.top.width);
            ctx->DrawLine(Point2Df(bounds.x, bounds.y), Point2Df(bounds.x + bounds.width, bounds.y));
        }
        if (borders.bottom.style != BorderStyle::NoBorder) {
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
    ctx->ClipRect(CellAreaRect());

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
    // either the formula currently being typed (in the cell or the formula
    // bar, so each reference pointed with the mouse shows), or, when not editing,
    // the committed formula sitting in the active cell. This lets the user see
    // exactly which cells a calculation such as =SUM(E2:E6) covers.
    std::vector<CellRange> ranges;
    std::vector<CellAddress> cells;

    auto collect = [&](const std::shared_ptr<SpreadsheetFormula>& formula) {
        if (!formula || !formula->IsValid()) return;
        for (const auto& r : formula->GetRangeDependencies()) ranges.push_back(r);
        for (const auto& c : formula->GetDependencies()) cells.push_back(c);
    };

    if (IsEditing() && !editBuffer_.empty() && editBuffer_[0] == '=') {
        auto live = std::make_shared<SpreadsheetFormula>(editBuffer_, editingCell_);
        live->Parse();
        collect(live);
    } else {
        const auto& selection = sheet->GetSelection();
        SpreadsheetCell* cell =
            sheet->GetCellIfExists(selection.activeCell.row, selection.activeCell.col);
        if (cell && cell->HasFormula()) {
            // The engine parses a formula the first time it evaluates the
            // cell, so one loaded with its cached result (most of a file) has
            // only its text yet: parse it here, as the engine would.
            if (!cell->GetFormula()) {
                auto parsed = std::make_shared<SpreadsheetFormula>(cell->GetFormulaText(),
                                                                   cell->GetAddress());
                parsed->Parse();
                cell->SetParsedFormula(parsed);
            }
            collect(cell->GetFormula());
        }
    }

    if (ranges.empty() && cells.empty()) return;

    int startX = gridBounds_.x + (showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0);
    int startY = gridBounds_.y + (showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0);

    // Clip to the cell area so highlights never spill over the headers.
    ctx->PushState();
    ctx->ClipRect(CellAreaRect());

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

int UltraCanvasSpreadsheet::SheetTabWidth(const std::string& name) const {
    const int padding = 20;
    // Measured in the font the tab is drawn in, so long and non-Latin names
    // fit; the per-byte estimate is only for before the grid has a window.
    if (CellTextMeasureFn measure = MakeSpreadsheetTextMeasurer(GetRenderContext())) {
        return std::max(40, measure(name, CellFont()) + padding);
    }
    return std::max(40, static_cast<int>(name.length()) * 8 + padding);
}

UltraCanvasSpreadsheet::SheetTabLayout UltraCanvasSpreadsheet::LayoutSheetTabs(int firstTab) const {
    const int navButtonWidth = 30;
    const int addWidth = 24;
    const int tabY = sheetTabsBounds_.y;
    const int tabHeight = sheetTabsBounds_.height;
    const int right = sheetTabsBounds_.x + sheetTabsBounds_.width;

    SheetTabLayout layout;
    layout.prevButton = Rect2Di(sheetTabsBounds_.x, tabY, navButtonWidth, tabHeight);
    layout.nextButton = Rect2Di(sheetTabsBounds_.x + navButtonWidth, tabY, navButtonWidth, tabHeight);

    const int count = static_cast<int>(sheets_.size());
    firstTab = std::max(0, std::min(firstTab, count - 1));
    int tabX = sheetTabsBounds_.x + 2 * navButtonWidth + 4;
    int i = firstTab;
    for (; i < count; ++i) {
        int tabWidth = SheetTabWidth(sheets_[i]->GetName());
        // Always keep room for the "+" button after the last tab drawn; a
        // single tab wider than the strip is still shown (clipped).
        if (!layout.tabs.empty() && tabX + tabWidth + addWidth > right) break;
        tabWidth = std::min(tabWidth, std::max(40, right - addWidth - tabX));
        layout.tabs.emplace_back(i, Rect2Di(tabX, tabY, tabWidth, tabHeight));
        tabX += tabWidth;
    }
    layout.addButton = Rect2Di(tabX, tabY, addWidth, tabHeight);
    layout.canScrollPrev = firstTab > 0;
    layout.canScrollNext = i < count;
    return layout;
}

void UltraCanvasSpreadsheet::EnsureSheetTabVisible(int index) {
    const int count = static_cast<int>(sheets_.size());
    if (index < 0 || index >= count) return;
    firstVisibleTab_ = std::max(0, std::min(firstVisibleTab_, count - 1));
    if (index < firstVisibleTab_) {
        firstVisibleTab_ = index;
        return;
    }
    // Before the strip has been laid out there is nothing to fit into yet.
    if (sheetTabsBounds_.width <= 0) return;
    while (firstVisibleTab_ < index) {
        const SheetTabLayout layout = LayoutSheetTabs(firstVisibleTab_);
        if (!layout.tabs.empty() && layout.tabs.back().first >= index) break;
        ++firstVisibleTab_;
    }
}

void UltraCanvasSpreadsheet::RenderSheetTabs(IRenderContext* ctx) {
    // Background
    ctx->SetFillPaint(headerBackgroundColor_);
    ctx->FillRectangle(Rect2Df(sheetTabsBounds_.x, sheetTabsBounds_.y,
                  sheetTabsBounds_.width, sheetTabsBounds_.height));

    const SheetTabLayout layout = LayoutSheetTabs(firstVisibleTab_);
    firstVisibleTab_ = layout.tabs.empty() ? 0 : layout.tabs.front().first;

    // Scroll buttons: a triangle pointing the way the strip moves, greyed
    // when there is nothing more that way.
    auto drawNavButton = [&](const Rect2Di& r, bool enabled, bool pointLeft) {
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Df(r.x, r.y, r.width, r.height));
        const double cx = r.x + r.width * 0.5;
        const double cy = r.y + r.height * 0.5;
        const double w = 5.0, hh = 5.0;
        std::vector<Point2Dd> tri = pointLeft
            ? std::vector<Point2Dd>{ Point2Dd(cx - w / 2, cy), Point2Dd(cx + w / 2, cy - hh), Point2Dd(cx + w / 2, cy + hh) }
            : std::vector<Point2Dd>{ Point2Dd(cx + w / 2, cy), Point2Dd(cx - w / 2, cy - hh), Point2Dd(cx - w / 2, cy + hh) };
        ctx->SetFillPaint(enabled ? headerTextColor_ : Color(190, 190, 190));
        ctx->FillLinePath(tri);
    };
    drawNavButton(layout.prevButton, layout.canScrollPrev, true);
    drawNavButton(layout.nextButton, layout.canScrollNext, false);

    // Sheet tabs
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(11);
    for (const auto& [index, tab] : layout.tabs) {
        const auto& sheet = sheets_[index];
        std::string name = sheet->GetName();

        // Tab background
        ctx->SetFillPaint(index == activeSheetIndex_ ? sheetTabActiveColor_ : sheetTabInactiveColor_);
        ctx->FillRectangle(Rect2Df(tab.x, tab.y, tab.width, tab.height));

        // Tab border
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Df(tab.x, tab.y, tab.width, tab.height));

        // Tab color indicator
        if (sheet->GetTabColor() != Colors::Transparent) {
            ctx->SetFillPaint(sheet->GetTabColor());
            ctx->FillRectangle(Rect2Df(tab.x, tab.y + tab.height - 3, tab.width, 3));
        }

        // Tab name, kept inside the tab when the strip is too narrow for it
        ctx->PushState();
        ctx->ClipRect(Rect2Df(tab.x + 2, tab.y, std::max(0, tab.width - 4), tab.height));
        ctx->SetTextPaint(Colors::Black);
        ctx->DrawText(name, Point2Df(tab.x + 10,
                      tab.y + (tab.height - ctx->GetTextLineHeight(name)) / 2));
        ctx->PopState();
    }

    // Add sheet button
    const Rect2Di& addButton = layout.addButton;
    ctx->SetFillPaint(headerBackgroundColor_);
    ctx->FillRectangle(Rect2Df(addButton.x, addButton.y, addButton.width, addButton.height));
    ctx->SetStrokePaint(gridlineColor_);
    ctx->DrawRectangle(Rect2Df(addButton.x, addButton.y, addButton.width, addButton.height));
    ctx->SetTextPaint(Colors::Black);
    ctx->DrawText("+", Point2Df(addButton.x + (addButton.width - ctx->GetTextLineWidth("+")) / 2,
                                addButton.y + (addButton.height - ctx->GetTextLineHeight("+")) / 2));
}

void UltraCanvasSpreadsheet::RenderScrollbars(IRenderContext* ctx) {
    // The box under the row headers, left of the horizontal scrollbar, closes
    // off the header column instead of leaving a white gap below the last row.
    if (horizontalScrollBounds_.x > gridBounds_.x) {
        Rect2Df corner(gridBounds_.x, horizontalScrollBounds_.y,
                       horizontalScrollBounds_.x - gridBounds_.x, horizontalScrollBounds_.height);
        ctx->SetFillPaint(headerBackgroundColor_);
        ctx->FillRectangle(corner);
        ctx->SetStrokePaint(gridlineColor_);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(corner);
    }

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

void UltraCanvasSpreadsheet::RenderCellEditor(IRenderContext* ctx) {
    if (!IsEditing() || !cellEditor) return;
    PositionCellEditor();
    Rect2Df eb = cellEditor->GetBounds();
    ctx->PushState();
    // An in-cell editor scrolled partly out of view is cut at the cell area's
    // edges like the cell it covers.
    if (editMode_ == SpreadsheetEditMode::InCellEditing) ctx->ClipRect(CellAreaRect());
    ctx->ClipRect(Rect2Dd(eb.x, eb.y, eb.width, eb.height));
    ctx->Translate(Point2Df(eb.x, eb.y));
    cellEditor->Render(ctx, Rect2Df(0, 0, eb.width, eb.height));
    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderFunctionHint(IRenderContext* ctx) {
    if (!cellEditor || !formulaEngine_) return;
    // A caret moved inside the editor repainted only the editor; the hint
    // beside it needs the whole grid repainted once.
    const size_t caret = cellEditor->GetCaretPosition();
    if (caret != functionHintCaret_) {
        functionHintCaret_ = caret;
        Invalidate();
    }
    const std::string& text = cellEditor->GetText();
    if (text.empty() || text[0] != '=') return;
    std::string name;
    int argIndex = 0;
    if (!FindFormulaCallAtCaret(text, caret, name, argIndex)) return;
    const FormulaFunctionLibrary& library = formulaEngine_->GetFunctionLibrary();
    const FunctionDefinition* def = library.GetFunction(name);
    if (!def) return;
    const std::vector<std::string> args = library.GetSignatureArguments(name);
    // Past the named arguments of a function that takes more, "..." is the
    // argument being typed.
    const int current = args.empty() ? -1 : std::min(argIndex, static_cast<int>(args.size()) - 1);

    // The signature as pieces: plain, the current argument bold, plain.
    struct Piece { std::string text; bool bold; };
    std::vector<Piece> pieces;
    pieces.push_back({ def->name + "(", false });
    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        if (i) pieces.push_back({ ", ", false });
        pieces.push_back({ args[i], i == current });
    }
    pieces.push_back({ ")", false });

    ctx->PushState();
    ctx->SetFontSize(11);
    auto widthOf = [&](const Piece& p) {
        ctx->SetFontFace("Arial", p.bold ? FontWeight::Bold : FontWeight::Normal, FontSlant::Normal);
        return ctx->GetTextLineWidth(p.text);
    };
    int signatureW = 0;
    for (const auto& p : pieces) signatureW += widthOf(p);
    ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
    const int descW = def->description.empty() ? 0 : ctx->GetTextLineWidth(def->description);
    const int lineH = std::max(14, static_cast<int>(ctx->GetTextLineHeight("Ag")));
    const int pad = 5;
    const int lines = def->description.empty() ? 1 : 2;
    int boxW = std::max(signatureW, descW) + 2 * pad;
    const int boxH = lines * lineH + 2 * pad;

    // Under the editor, kept inside the grid's width.
    const Rect2Df editor = EditorRect();
    int x = static_cast<int>(editor.x) - 3;
    const int y = static_cast<int>(editor.y + editor.height) + 4;
    boxW = std::min(boxW, static_cast<int>(GetWidth()) - 4);
    x = std::max(2, std::min(x, static_cast<int>(GetWidth()) - boxW - 2));

    ctx->SetFillPaint(Color(255, 255, 225));
    ctx->FillRectangle(Rect2Df(x, y, boxW, boxH));
    ctx->SetStrokePaint(Color(160, 160, 160));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(Rect2Df(x, y, boxW, boxH));
    ctx->ClipRect(Rect2Df(x + 1, y + 1, boxW - 2, boxH - 2));

    int tx = x + pad;
    ctx->SetTextPaint(Colors::Black);
    for (const auto& p : pieces) {
        const int w = widthOf(p);
        ctx->DrawText(p.text, Point2Df(tx, y + pad));
        tx += w;
    }
    if (!def->description.empty()) {
        ctx->SetFontFace("Arial", FontWeight::Normal, FontSlant::Normal);
        ctx->SetTextPaint(Color(90, 90, 90));
        ctx->DrawText(def->description, Point2Df(x + pad, y + pad + lineH));
    }
    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderClipboardIndicator(IRenderContext* ctx) {
    if (!HasClipboardData()) return;
    
    // Draw marching ants around clipboard range
    auto* sheet = GetSheetByName(clipboardSheetName_);
    if (!sheet || sheet != GetActiveSheet()) return;
    
    Point2Di topLeft = CellToScreen(clipboardRange_.start.row, clipboardRange_.start.col);
    Point2Di bottomRight = CellToScreen(clipboardRange_.end.row + 1, clipboardRange_.end.col + 1);
    
    ctx->PushState();
    ctx->ClipRect(CellAreaRect());
    ctx->SetStrokePaint(Colors::Black);
    ctx->SetStrokeWidth(1.0f);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
    ctx->DrawRectangle(Rect2Df(topLeft.x, topLeft.y,
                  bottomRight.x - topLeft.x, bottomRight.y - topLeft.y));
    ctx->SetLineDash(UCDashPattern{});
    ctx->PopState();
}

void UltraCanvasSpreadsheet::RenderAutoFillHandle(IRenderContext* ctx) {
    auto* sheet = GetActiveSheet();
    if (!sheet || IsEditing()) return;
    
    const auto& selection = sheet->GetSelection();
    if (selection.selections.empty()) return;
    
    // Get bottom-right of selection
    const auto& range = selection.selections[0];
    Point2Di corner = CellToScreen(range.end.row + 1, range.end.col + 1);
    
    ctx->PushState();
    ctx->ClipRect(CellAreaRect());

    // Draw small square handle
    int handleSize = 6;
    ctx->SetFillPaint(selectionBorderColor_);
    ctx->FillRectangle(Rect2Df(corner.x - handleSize, corner.y - handleSize, handleSize, handleSize));

    // While the handle is dragged, a dashed outline shows the range the
    // release will fill.
    if (editMode_ == SpreadsheetEditMode::AutoFilling && !(autoFillTarget_ == autoFillSource_)) {
        Point2Di topLeft = CellToScreen(autoFillTarget_.start.row, autoFillTarget_.start.col);
        Point2Di bottomRight = CellToScreen(autoFillTarget_.end.row + 1, autoFillTarget_.end.col + 1);
        ctx->SetStrokePaint(selectionBorderColor_);
        ctx->SetStrokeWidth(1.5f);
        ctx->SetLineDash(UCDashPattern({ 4.0, 3.0 }));
        ctx->DrawRectangle(Rect2Df(topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y));
        ctx->SetLineDash(UCDashPattern());
    }
    ctx->PopState();
}

CellRange UltraCanvasSpreadsheet::AutoFillTargetFor(int row, int col) const {
    const CellRange& src = autoFillSource_;
    const int down  = row - src.end.row;
    const int up    = src.start.row - row;
    const int right = col - src.end.col;
    const int left  = src.start.col - col;
    const int vertical = std::max(down, up);
    const int horizontal = std::max(right, left);
    if (vertical <= 0 && horizontal <= 0) return src;   // still over the source

    CellRange target = src;
    if (vertical >= horizontal) {
        if (down > 0) target.end.row = std::min(row, SpreadsheetLimits::MaxRows - 1);
        else          target.start.row = std::max(row, 0);
    } else {
        if (right > 0) target.end.col = std::min(col, SpreadsheetLimits::MaxColumns - 1);
        else           target.start.col = std::max(col, 0);
    }
    return target;
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

    HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);

    // Right-click puts up the cell-formatting menu. It selects the cell under
    // the cursor first (unless it is already inside the selection, so a
    // right-click on a multi-cell range formats the whole range), and never
    // starts a drag-selection.
    if (event.button == UCMouseButton::Right) {
        if (hit.area == HitArea::SheetTab && !hit.addSheetButton && hit.tabScroll == 0) {
            // A tab opens (and its menu acts on it); beside the tabs, -1.
            if (IsEditing()) StopEditing(true);
            const bool onTab = hit.tabIndex >= 0 && hit.tabIndex < static_cast<int>(sheets_.size());
            if (onTab && hit.tabIndex != activeSheetIndex_) SetActiveSheet(hit.tabIndex);
            const int index = onTab ? hit.tabIndex : -1;
            if (onSheetTabContextMenu) {
                onSheetTabContextMenu(index, event.pointerWindow.x, event.pointerWindow.y);
            } else if (sheetTabMenuEnabled_) {
                ShowSheetTabMenuAt(index, event.pointerWindow.x, event.pointerWindow.y);
            }
            return;
        }
        if (hit.area == HitArea::ColumnSortButton) hit.area = HitArea::ColumnHeader;
        if (hit.area == HitArea::Cell || hit.area == HitArea::ColumnHeader ||
            hit.area == HitArea::RowHeader) {
            if (IsEditing()) StopEditing(true);
            if (hit.area == HitArea::ColumnHeader)      SelectColumn(hit.col);
            else if (hit.area == HitArea::RowHeader)    SelectRow(hit.row);
            else if (!IsCellSelected(hit.row, hit.col)) SelectCell(hit.row, hit.col);
            Invalidate();

            if (onCellContextMenu) {
                onCellContextMenu(hit.row, hit.col, event.pointerWindow.x, event.pointerWindow.y);
            } else if (formatMenuEnabled_) {
                ShowFormatMenuAt(event.pointerWindow.x, event.pointerWindow.y);
            }
        }
        return;
    }

    mouseDown_ = true;
    mouseDownPos_ = Point2Di(event.pointer.x, event.pointer.y);

    switch (hit.area) {
        case HitArea::Cell: {
            CellAddress clickedCell(hit.row, hit.col);
            if (IsEditing()) {
                // Point mode: a formula waiting for a reference gets the
                // clicked cell's address typed in at its caret, and the edit
                // goes on. Shift+click grows the reference the last click
                // typed into a range; Ctrl+click adds another one after it.
                size_t start = 0, end = 0;
                if (GetPointReferenceSpan(start, end)) {
                    const bool live = IsPointedReferenceLive();
                    if (event.shift && live) {
                        PointReference(pointAnchor_, clickedCell, start, end);
                    } else if (event.ctrl && live) {
                        PointReference(clickedCell, clickedCell, pointRefEnd_, pointRefEnd_, ",");
                    } else {
                        PointReference(clickedCell, clickedCell, start, end);
                    }
                    pointingReference_ = true;   // a drag now makes it a range
                    if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                    break;
                }
                StopEditing(true);
            }
            
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

        case HitArea::ColumnSortButton: {
            // Sorts the selection, which stays selected; the same button
            // again turns the order round.
            if (IsEditing()) StopEditing(true);
            const bool ascending = !(GetHeaderSortColumn() == hit.col && headerSortAscending_);
            mouseDown_ = false;   // the warning dialog may take the button release
            RequestSortSelectionByColumn(hit.col, ascending ? SortOrder::Ascending : SortOrder::Descending);
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
            if (hit.addSheetButton) {
                // "+" appends a sheet with the next free name and opens it.
                if (AddSheet("")) SetActiveSheet(static_cast<int>(sheets_.size()) - 1);
            } else if (hit.tabScroll != 0) {
                // The arrows scroll the strip one tab; the active sheet stays.
                firstVisibleTab_ += hit.tabScroll;
                RequestRedraw();
            } else if (hit.tabIndex >= 0 && hit.tabIndex < static_cast<int>(sheets_.size())) {
                SetActiveSheet(hit.tabIndex);
            }
            break;
        }
        
        case HitArea::FormulaBar: {
            if (hit.functionMenuButton) {
                // Opens under the button; an edit in progress stays open so
                // the chosen function lands at its caret.
                const Rect2Di fx = FunctionMenuButtonRect();
                const Point2Df origin = GetPositionInWindow();
                ShowFunctionMenuAt(static_cast<int>(origin.x + fx.x),
                                   static_cast<int>(origin.y + fx.y + fx.height));
            } else if (!IsEditing()) {
                StartEditing();
            }
            break;
        }
        
        case HitArea::AutoFillHandle: {
            if (IsEditing()) StopEditing(true);
            autoFillSource_ = GetSelection();
            autoFillTarget_ = autoFillSource_;
            editMode_ = SpreadsheetEditMode::AutoFilling;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
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

    pointingReference_ = false;

    if (editMode_ == SpreadsheetEditMode::AutoFilling) {
        editMode_ = SpreadsheetEditMode::Normal;
        // Ctrl held on release copies the selection as it is, no series.
        if (!(autoFillTarget_ == autoFillSource_)) {
            AutoFillSelection(autoFillTarget_, event.ctrl ? AutoFillMode::Copy : AutoFillMode::Series);
        }
        autoFillTarget_ = autoFillSource_;
        Invalidate();
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
        if (pointingReference_) {
            // A point-mode drag: the reference typed at the press becomes the
            // range from there to the cell under the pointer.
            if (!IsEditing() || !IsPointedReferenceLive()) {
                pointingReference_ = false;
                return;
            }
            AutoScrollForDrag(event.pointer.x, event.pointer.y);
            CellAddress over = ScreenToCell(event.pointer.x, event.pointer.y);
            const CellRange current = CellRange(pointAnchor_, over);
            const std::string wanted = current.IsSingleCell() ? current.start.ToString() : current.ToString();
            if (pointRefText_.compare(pointRefStart_, pointRefEnd_ - pointRefStart_, wanted) != 0) {
                PointReference(pointAnchor_, over, pointRefStart_, pointRefEnd_);
            }
        }
        else if (editMode_ == SpreadsheetEditMode::Selecting) {
            AutoScrollForDrag(event.pointer.x, event.pointer.y);
            HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);
            if (hit.area == HitArea::Cell) {
                CellAddress current(hit.row, hit.col);
                if (auto* sheet = GetActiveSheet()) {
                    sheet->SelectRange(CellRange(mouseDownCell_, current));
                    Invalidate();
                }
            }
        }
        else if (editMode_ == SpreadsheetEditMode::AutoFilling) {
            AutoScrollForDrag(event.pointer.x, event.pointer.y);
            // ScreenToCell also answers for a pointer beyond the last visible
            // cell, so the drag keeps tracking outside the grid.
            CellAddress over = ScreenToCell(event.pointer.x, event.pointer.y);
            CellRange target = AutoFillTargetFor(over.row, over.col);
            if (!(target == autoFillTarget_)) {
                autoFillTarget_ = target;
                Invalidate();
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
        case HitArea::FormulaBar:
            SetMouseCursor(hit.functionMenuButton ? UCMouseCursor::Hand : UCMouseCursor::Text);
            break;
        case HitArea::ColumnSortButton: SetMouseCursor(UCMouseCursor::Hand); break;
        case HitArea::AutoFillHandle: SetMouseCursor(UCMouseCursor::Cross);  break;
        case HitArea::Cell:
            // A click would type this cell into the formula being edited.
            SetMouseCursor(IsEditing() && CanPointReference() ? UCMouseCursor::Cross
                                                              : UCMouseCursor::Default);
            break;
        default:                     SetMouseCursor(UCMouseCursor::Default); break;
    }
}

void UltraCanvasSpreadsheet::HandleMouseDoubleClick(const UCEvent& event) {
    HitTestResult hit = HitTest(event.pointer.x, event.pointer.y);

    // Still editing after the first click means it pointed a reference into
    // the formula: the second one must not start editing the clicked cell.
    if (hit.area == HitArea::Cell && IsEditing()) return;
    
    if (hit.area == HitArea::AutoFillHandle) {
        // Fills down as far as the data next to the selection goes.
        AutoFillSelectionToDataEnd();
    }
    else if (hit.area == HitArea::SheetTab && !hit.addSheetButton && hit.tabScroll == 0 &&
             hit.tabIndex >= 0 && hit.tabIndex < static_cast<int>(sheets_.size())) {
        PromptRenameSheet(hit.tabIndex);
    }
    else if (hit.area == HitArea::Cell) {
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
        // Caret movement, selection, clipboard and undo belong to the editor
        // child and reach it directly while it holds the focus; Enter and
        // Escape arrive through its callbacks. Tab is the one editing key the
        // sheet still owns, because it commits and steps to the next cell.
        if (event.virtualKey == UCKeys::Tab) {
            StopEditing(true);
            MoveActiveCell(0, event.shift ? -1 : 1);
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
        SetEditBuffer(std::string(1, static_cast<char>(event.character)));
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
            case UCKeys::D:
                FillSelectionDown();
                break;
            case UCKeys::R:
                FillSelectionRight();
                break;
            case UCKeys::B:
                SetSelectionBold(!GetActiveCellFont().bold);
                break;
            case UCKeys::I:
                SetSelectionItalic(!GetActiveCellFont().italic);
                break;
            case UCKeys::U:
                SetSelectionUnderline(GetActiveCellFont().underline == UnderlineStyle::NoUnderline
                                       ? UnderlineStyle::Single : UnderlineStyle::NoUnderline);
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
            SetEditBuffer(std::string(1, static_cast<char>(event.character)));
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
        result.functionMenuButton = FunctionMenuButtonRect().Contains(x, y);
        return result;
    }
    
    // Sheet tabs
    if (showSheetTabs_ && sheetTabsBounds_.Contains(x, y)) {
        result.area = HitArea::SheetTab;
        const SheetTabLayout layout = LayoutSheetTabs(firstVisibleTab_);
        if (layout.addButton.Contains(x, y)) {
            result.addSheetButton = true;
        } else if (layout.prevButton.Contains(x, y)) {
            result.tabScroll = layout.canScrollPrev ? -1 : 0;
        } else if (layout.nextButton.Contains(x, y)) {
            result.tabScroll = layout.canScrollNext ? 1 : 0;
        } else {
            for (const auto& [index, tab] : layout.tabs) {
                if (tab.Contains(x, y)) {
                    result.tabIndex = index;
                    break;
                }
            }
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

            // Sort button of a column inside the selected block.
            CellRange sortRange;
            if (result.area == HitArea::ColumnHeader && GetHeaderSortableRange(sortRange) &&
                result.col >= sortRange.start.col && result.col <= sortRange.end.col) {
                int left = static_cast<int>(gridBounds_.x) + headerWidth;
                for (int c = sheet->GetScrollColumn(); c < result.col; ++c) {
                    if (!sheet->IsColumnHidden(c)) left += sheet->GetColumnWidth(c);
                }
                Rect2Di button = GetHeaderSortButtonRect(left, sheet->GetColumnWidth(result.col));
                if (button.width > 0 && button.Contains(x, y)) {
                    result.area = HitArea::ColumnSortButton;
                }
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
    // A cell scrolled out to the left/top sits before the cell area, so the
    // clip hides it (or the part of a range that is out of view) rather
    // than it being drawn over the first visible column/row.
    for (int c = col; c < scrollCol; ++c) {
        if (!sheet->IsColumnHidden(c)) {
            x -= sheet->GetColumnWidth(c);
        }
    }
    
    for (int r = scrollRow; r < row; ++r) {
        if (!sheet->IsRowHidden(r)) {
            y += sheet->GetRowHeight(r);
        }
    }
    for (int r = row; r < scrollRow; ++r) {
        if (!sheet->IsRowHidden(r)) {
            y -= sheet->GetRowHeight(r);
        }
    }
    
    return Point2Di(x, y);
}

Rect2Df UltraCanvasSpreadsheet::CellAreaRect() const {
    const int left = showRowHeaders_ ? SpreadsheetLimits::RowHeaderWidth : 0;
    const int top = showColumnHeaders_ ? SpreadsheetLimits::HeaderRowHeight : 0;
    return Rect2Df(gridBounds_.x + left, gridBounds_.y + top,
                   std::max(0.0f, static_cast<float>(gridBounds_.width - left)),
                   std::max(0.0f, static_cast<float>(gridBounds_.height - top)));
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

Rect2Df UltraCanvasSpreadsheet::EditorRect() const {
    if (editMode_ == SpreadsheetEditMode::InCellEditing) {
        Rect2Di b = GetCellBounds(editingCell_.row, editingCell_.col);
        const int padding = 3;
        return Rect2Df(static_cast<float>(b.x + padding),
                       static_cast<float>(b.y + 2),
                       static_cast<float>(std::max(8, b.width - 2 * padding)),
                       static_cast<float>(std::max(8, b.height - 4)));
    }
    // Formula-bar editing: the text area to the right of the name box, where
    // RenderFormulaBar draws the text when not editing (so it does not move).
    const int nameBoxWidth = 80;
    float x = formulaBarBounds_.x + nameBoxWidth + 10;
    const Rect2Di fx = FunctionMenuButtonRect();
    const float right = fx.width > 0 ? fx.x - 4.0f : formulaBarBounds_.x + formulaBarBounds_.width - 8.0f;
    return Rect2Df(x, formulaBarBounds_.y + 2,
                   std::max(8.0f, right - x),
                   std::max(8.0f, formulaBarBounds_.height - 4.0f));
}

void UltraCanvasSpreadsheet::PositionCellEditor() {
    if (!cellEditor) return;
    PlaceChildAt(cellEditor, EditorRect());
}

void UltraCanvasSpreadsheet::BuildCellEditor(const std::string& initialText,
                                             bool selectAll) {
    DestroyCellEditor();
    cellEditor = CreateTextInput(GetIdentifier() + "-cell-editor", 0, 0, 10, 10);
    TextInputStyle ts;
    // The sheet paints the cell (or the formula bar) behind it; the editor
    // contributes the text, caret and selection.
    ts.backgroundColor = Colors::White;
    ts.borderWidth = 0;
    ts.borderRadius = 0;
    ts.textColor = Colors::Black;
    ts.caretColor = Colors::Black;
    ts.fontStyle.fontFamily = "Arial";
    ts.fontStyle.fontSize = 11;
    ts.paddingLeft = 0;
    ts.paddingRight = 0;
    ts.paddingTop = 0;
    ts.paddingBottom = 0;
    cellEditor->SetStyle(ts);
    cellEditor->SetShowValidationState(false);
    // Attach and size before filling: SetText() scrolls the field to keep the
    // caret visible, measured against whatever width it has at the time.
    AddChild(cellEditor);
    PositionCellEditor();
    cellEditor->SetText(initialText);
    cellEditor->onTextChanged = [this](const std::string& s) {
        editBuffer_ = s;                 // the formula preview reads this
        if (onFormulaBarChange) onFormulaBarChange();
        Invalidate();
    };
    // A caret moved by the keys or the mouse moves the function hint along.
    cellEditor->onSelectionChanged = [this](size_t start, size_t end) {
        editorSelStart_ = start;
        editorSelEnd_ = end;
        Invalidate();
    };
    cellEditor->onEnterPressed = [this](const std::string&) {
        StopEditing(true);
        MoveActiveCell(1, 0);
        return true;
    };
    cellEditor->onEscapePressed = [this]() {
        CancelEditing();
        return true;
    };
    cellEditor->SetFocus(true);
    if (selectAll) cellEditor->SelectAll();
    else           cellEditor->SetSelection(initialText.size(), initialText.size());
    editBuffer_ = initialText;
}

void UltraCanvasSpreadsheet::DestroyCellEditor() {
    if (!cellEditor) return;
    auto editor = cellEditor;
    cellEditor.reset();
    // Enter / Escape reach us from inside the editor's own key handler, so
    // detaching must not drop the last reference while that frame runs.
    editor->onTextChanged = nullptr;
    editor->onSelectionChanged = nullptr;
    editor->onEnterPressed = nullptr;
    editor->onEscapePressed = nullptr;
    pointingReference_ = false;
    pointRefText_.clear();
    RemoveChild(editor);
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        app->PostToUIThread([editor]() {});
    }
}

void UltraCanvasSpreadsheet::StartEditingAt(int row, int col) {
    if (IsEditing()) {
        StopEditing(true);
    }
    
    editingCell_ = CellAddress(row, col);
    editMode_ = SpreadsheetEditMode::Editing;
    
    std::string initial;
    if (auto* cell = GetCellIfExists(row, col)) {
        initial = cell->HasFormula() ? cell->GetFormulaText() : cell->GetText();
    }
    BuildCellEditor(initial, false);

    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

bool UltraCanvasSpreadsheet::IsPointedReferenceLive() const {
    // Live while nothing but pointing has touched the text or the caret.
    return cellEditor && !pointRefText_.empty() && !cellEditor->HasSelection() &&
           cellEditor->GetCaretPosition() == pointRefEnd_ &&
           cellEditor->GetText() == pointRefText_;
}

bool UltraCanvasSpreadsheet::GetPointReferenceSpan(size_t& start, size_t& end) const {
    if (!cellEditor || !IsEditing()) return false;
    if (IsPointedReferenceLive()) {
        start = pointRefStart_;
        end = pointRefEnd_;
        return true;
    }
    const std::string text = cellEditor->GetText();
    start = end = std::min(cellEditor->GetCaretPosition(), text.size());
    // A selection is replaced, as typing would replace it - when the span the
    // editor last reported is still what it has selected.
    if (cellEditor->HasSelection() && editorSelStart_ < editorSelEnd_ && editorSelEnd_ <= text.size() &&
        text.compare(editorSelStart_, editorSelEnd_ - editorSelStart_, cellEditor->GetSelectedText()) == 0) {
        start = editorSelStart_;
        end = editorSelEnd_;
    }
    return FormulaAcceptsReferenceAt(text, start, end);
}

void UltraCanvasSpreadsheet::PointReference(const CellAddress& anchor, const CellAddress& to,
                                            size_t start, size_t end, const std::string& prefix) {
    if (!cellEditor) return;
    // Addresses on the edited sheet, so without a sheet name.
    const CellRange range(CellAddress(anchor.row, anchor.col), CellAddress(to.row, to.col));
    const std::string reference = range.IsSingleCell() ? range.start.ToString() : range.ToString();

    std::string text = cellEditor->GetText();
    start = std::min(start, text.size());
    end = std::min(std::max(end, start), text.size());
    text.replace(start, end - start, prefix + reference);
    pointAnchor_ = CellAddress(anchor.row, anchor.col);
    pointRefStart_ = start + prefix.size();
    pointRefEnd_ = pointRefStart_ + reference.size();
    pointRefText_ = text;

    cellEditor->SetText(text);
    cellEditor->SetSelection(pointRefEnd_, pointRefEnd_);
    cellEditor->SetFocus(true);   // the press focused the grid; typing goes on in the formula
    editBuffer_ = text;
    if (onFormulaBarChange) onFormulaBarChange();
    if (onStatusChange) onStatusChange("Point: " + reference);
    Invalidate();
}

void UltraCanvasSpreadsheet::AutoScrollForDrag(int x, int y) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    const Rect2Df area = CellAreaRect();
    int row = sheet->GetScrollRow();
    int col = sheet->GetScrollColumn();
    if (y >= area.y + area.height)  row = std::min(row + 1, SpreadsheetLimits::MaxRows - 1);
    else if (y < area.y && row > 0) --row;
    if (x >= area.x + area.width)   col = std::min(col + 1, SpreadsheetLimits::MaxColumns - 1);
    else if (x < area.x && col > 0) --col;
    if (row != sheet->GetScrollRow() || col != sheet->GetScrollColumn()) {
        sheet->SetScrollPosition(row, col);
        Invalidate();
    }
}

void UltraCanvasSpreadsheet::InsertFunction(const std::string& name) {
    if (name.empty()) return;
    if (!IsEditing()) {
        StartEditing();   // in the formula bar
        if (!cellEditor) return;
        if (editBuffer_.size() > 1 && editBuffer_[0] == '=') {
            // A formula is wrapped: =A1+B1 becomes =ROUND(A1+B1), the caret
            // before the closing parenthesis for the remaining arguments.
            const std::string text = "=" + name + "(" + editBuffer_.substr(1) + ")";
            cellEditor->SetText(text);
            cellEditor->SetSelection(text.size() - 1, text.size() - 1);
            editBuffer_ = text;
            cellEditor->SetFocus(true);
            if (onFormulaBarChange) onFormulaBarChange();
            Invalidate();
            return;
        }
        // Anything else: the cell becomes a call of the function.
        cellEditor->SetText("=");
        cellEditor->SetSelection(1, 1);
    }
    if (!cellEditor) return;
    std::string text = cellEditor->GetText();
    size_t caret = std::min(cellEditor->GetCaretPosition(), text.size());
    if (text.empty()) { text = "="; caret = 1; }
    text.insert(caret, name + "()");
    // The caret between the parentheses, ready for the arguments.
    caret += name.size() + 1;
    cellEditor->SetText(text);
    cellEditor->SetSelection(caret, caret);
    editBuffer_ = text;
    cellEditor->SetFocus(true);
    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::StartInCellEditing() {
    CellAddress active = GetActiveCell();
    editingCell_ = active;
    editMode_ = SpreadsheetEditMode::InCellEditing;
    
    std::string initial;
    if (auto* cell = GetCellIfExists(active.row, active.col)) {
        initial = cell->HasFormula() ? cell->GetFormulaText() : cell->GetText();
    }
    BuildCellEditor(initial, false);

    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::StopEditing(bool confirm) {
    if (!IsEditing()) return;
    
    if (confirm) {
        CommitEdit();
    }
    
    editMode_ = SpreadsheetEditMode::Normal;
    DestroyCellEditor();
    editBuffer_.clear();
    SetFocus(true);   // the grid takes the keyboard back for navigation

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
    if (cellEditor) {
        cellEditor->SetText(text);
        cellEditor->SetSelection(text.size(), text.size());
    }
    if (onFormulaBarChange) onFormulaBarChange();
    Invalidate();
}

void UltraCanvasSpreadsheet::InsertTextAtCursor(const std::string& text) {
    // The editor owns the caret, so an insert is placed through it.
    if (cellEditor) {
        std::string cur = cellEditor->GetText();
        size_t at = std::min(cellEditor->GetCaretPosition(), cur.size());
        cur.insert(at, text);
        cellEditor->SetText(cur);
        cellEditor->SetSelection(at + text.size(), at + text.size());
        editBuffer_ = cur;
    }
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

    sheet->AutoFitColumnWidth(col, MakeSpreadsheetTextMeasurer(GetRenderContext()));
    // Double-clicking a header divider is the user sizing the column, so the
    // result must survive the next auto-width pass.
    sheet->GetOrCreateColumnDefinition(col).explicitWidth = true;
    Invalidate();
}

void UltraCanvasSpreadsheet::AutoFitUnsizedColumns() {
    CellTextMeasureFn measure = MakeSpreadsheetTextMeasurer(GetRenderContext());
    if (!measure) {
        // No window yet: defer to the first render rather than fitting columns
        // against guessed metrics.
        autoFitPending_ = true;
        return;
    }
    for (auto& sheet : sheets_) {
        if (sheet) sheet->AutoFitUnsizedColumns(measure);
    }
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
    clipboardOp_ = ClipboardOperation::NoOperation;
    clipboardData_.clear();
    Invalidate();
}

// ============================================================================
// FORMATTING
// ============================================================================

void UltraCanvasSpreadsheet::SetSelectionBold(bool bold) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    
    CellRange sel = GetFormattingRange();
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
    
    CellRange sel = GetFormattingRange();
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
    
    CellRange sel = GetFormattingRange();
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
