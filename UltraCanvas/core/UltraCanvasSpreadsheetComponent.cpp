// core/UltraCanvasSpreadsheetComponent.cpp
// Workbook-level method implementations for UltraCanvasSpreadsheet.
// These are thin delegations to the active SpreadsheetSheet plus the
// workbook-scoped features (sheet management, named ranges, undo/redo).
// Rendering, event handling, editing, navigation and clipboard live in
// core/UltraCanvasSpreadsheet.cpp - this file MUST NOT redefine those.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include <stdexcept>
#include "UltraCanvasSpreadsheet.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ============================================================================
// SHEET MANAGEMENT
// ============================================================================

SpreadsheetSheet* UltraCanvasSpreadsheet::GetActiveSheet() {
    return (activeSheetIndex_ >= 0 && activeSheetIndex_ < (int)sheets_.size())
        ? sheets_[activeSheetIndex_].get() : nullptr;
}

const SpreadsheetSheet* UltraCanvasSpreadsheet::GetActiveSheet() const {
    return (activeSheetIndex_ >= 0 && activeSheetIndex_ < (int)sheets_.size())
        ? sheets_[activeSheetIndex_].get() : nullptr;
}

void UltraCanvasSpreadsheet::SetActiveSheet(int index) {
    if (sheets_.empty()) return;
    if (index < 0) index = 0;
    if (index >= (int)sheets_.size()) index = (int)sheets_.size() - 1;

    activeSheetIndex_ = index;

    // Update selected flags so only the active sheet is marked selected.
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetSelected(i == index);
    }

    UpdateFormulaBar();
    RequestRedraw();

    if (onSheetChange) onSheetChange(index);
}

void UltraCanvasSpreadsheet::SetActiveSheetByName(const std::string& name) {
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        if (sheets_[i]->GetName() == name) {
            SetActiveSheet(i);
            return;
        }
    }
}

SpreadsheetSheet* UltraCanvasSpreadsheet::GetSheet(int index) {
    return (index >= 0 && index < (int)sheets_.size()) ? sheets_[index].get() : nullptr;
}

const SpreadsheetSheet* UltraCanvasSpreadsheet::GetSheet(int index) const {
    return (index >= 0 && index < (int)sheets_.size()) ? sheets_[index].get() : nullptr;
}

SpreadsheetSheet* UltraCanvasSpreadsheet::GetSheetByName(const std::string& name) {
    for (auto& sheet : sheets_) {
        if (sheet->GetName() == name) return sheet.get();
    }
    return nullptr;
}

const SpreadsheetSheet* UltraCanvasSpreadsheet::GetSheetByName(const std::string& name) const {
    for (const auto& sheet : sheets_) {
        if (sheet->GetName() == name) return sheet.get();
    }
    return nullptr;
}

SpreadsheetSheet* UltraCanvasSpreadsheet::AddSheet(const std::string& name) {
    return InsertSheet((int)sheets_.size(), name);
}

SpreadsheetSheet* UltraCanvasSpreadsheet::InsertSheet(int index, const std::string& name) {
    if (index < 0) index = 0;
    if (index > (int)sheets_.size()) index = (int)sheets_.size();

    std::string sheetName = name.empty() ? GenerateUniqueSheetName() : name;

    auto sheet = std::make_unique<SpreadsheetSheet>(sheetName, index);
    SpreadsheetSheet* ptr = sheet.get();
    ptr->SetParent(this);
    SetupSheetCallbacks(ptr);

    sheets_.insert(sheets_.begin() + index, std::move(sheet));

    // Re-number sheet indices after the insertion point.
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    if (onSheetAdd) onSheetAdd(index);
    RequestRedraw();
    return ptr;
}

void UltraCanvasSpreadsheet::RemoveSheet(int index) {
    if (index < 0 || index >= (int)sheets_.size()) return;
    if (sheets_.size() <= 1) return;  // Always keep at least one sheet.

    sheets_.erase(sheets_.begin() + index);

    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    if (onSheetRemove) onSheetRemove(index);

    // Keep the active sheet index valid.
    if (activeSheetIndex_ >= (int)sheets_.size()) {
        activeSheetIndex_ = (int)sheets_.size() - 1;
    }
    SetActiveSheet(activeSheetIndex_);
}

void UltraCanvasSpreadsheet::RemoveSheetByName(const std::string& name) {
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        if (sheets_[i]->GetName() == name) {
            RemoveSheet(i);
            return;
        }
    }
}

void UltraCanvasSpreadsheet::RenameSheet(int index, const std::string& newName) {
    if (index < 0 || index >= (int)sheets_.size()) return;
    if (newName.empty()) return;
    sheets_[index]->SetName(newName);
    if (onSheetRename) onSheetRename(index, newName);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::MoveSheet(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= (int)sheets_.size()) return;
    if (toIndex < 0) toIndex = 0;
    if (toIndex >= (int)sheets_.size()) toIndex = (int)sheets_.size() - 1;
    if (fromIndex == toIndex) return;

    // Remember the currently active sheet so we can restore it by identity.
    SpreadsheetSheet* active = GetActiveSheet();

    auto moved = std::move(sheets_[fromIndex]);
    sheets_.erase(sheets_.begin() + fromIndex);
    sheets_.insert(sheets_.begin() + toIndex, std::move(moved));

    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    // Restore active index to follow the same sheet.
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        if (sheets_[i].get() == active) {
            activeSheetIndex_ = i;
            break;
        }
    }

    if (onStructureChange) onStructureChange();
    RequestRedraw();
}

SpreadsheetSheet* UltraCanvasSpreadsheet::CopySheet(int index, const std::string& newName) {
    if (index < 0 || index >= (int)sheets_.size()) return nullptr;

    std::string copyName = newName.empty() ? GenerateUniqueSheetName() : newName;

    auto clone = sheets_[index]->Clone(copyName);
    SpreadsheetSheet* ptr = clone.get();
    ptr->SetParent(this);
    SetupSheetCallbacks(ptr);

    int insertAt = index + 1;
    sheets_.insert(sheets_.begin() + insertAt, std::move(clone));

    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    if (onSheetAdd) onSheetAdd(insertAt);
    RequestRedraw();
    return ptr;
}

std::vector<std::string> UltraCanvasSpreadsheet::GetSheetNames() const {
    std::vector<std::string> names;
    names.reserve(sheets_.size());
    for (const auto& sheet : sheets_) {
        names.push_back(sheet->GetName());
    }
    return names;
}

// ============================================================================
// CELL ACCESS
// ============================================================================

SpreadsheetCell* UltraCanvasSpreadsheet::GetCell(int row, int col) {
    auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCell(row, col) : nullptr;
}

SpreadsheetCell* UltraCanvasSpreadsheet::GetCell(const CellAddress& addr) {
    return GetCell(addr.row, addr.col);
}

const SpreadsheetCell* UltraCanvasSpreadsheet::GetCellIfExists(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCellIfExists(row, col) : nullptr;
}

void UltraCanvasSpreadsheet::SetCellValue(int row, int col, const std::string& text) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(CellRange(row, col, row, col));
    sheet->SetCellValue(row, col, text);  // fires sheet onCellChange
    if (formulaEngine_) {
        formulaEngine_->MarkDirty(CellAddress(row, col), sheet->GetName());
    }
    Recalculate();
}

void UltraCanvasSpreadsheet::SetCellValue(int row, int col, double number) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(CellRange(row, col, row, col));
    sheet->SetCellValue(row, col, number);
    if (formulaEngine_) {
        formulaEngine_->MarkDirty(CellAddress(row, col), sheet->GetName());
    }
    Recalculate();
}

void UltraCanvasSpreadsheet::SetCellValue(int row, int col, bool boolean) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(CellRange(row, col, row, col));
    sheet->SetCellValue(row, col, boolean);
    if (formulaEngine_) {
        formulaEngine_->MarkDirty(CellAddress(row, col), sheet->GetName());
    }
    Recalculate();
}

void UltraCanvasSpreadsheet::SetCellFormula(int row, int col, const std::string& formula) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(CellRange(row, col, row, col));

    sheet->SetCellFormula(row, col, formula);  // fires sheet onCellChange

    // Register with the formula engine (best-effort).
    if (formulaEngine_) {
        SpreadsheetCell* cell = sheet->GetCell(row, col);
        if (cell) {
            auto parsed = formulaEngine_->ParseFormula(formula, CellAddress(row, col));
            cell->SetParsedFormula(parsed);
            formulaEngine_->UpdateDependencies(cell, sheet->GetName());
        }
        formulaEngine_->MarkDirty(CellAddress(row, col), sheet->GetName());
    }
    Recalculate();
}

std::string UltraCanvasSpreadsheet::GetCellText(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCellText(row, col) : std::string();
}

double UltraCanvasSpreadsheet::GetCellNumber(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCellNumber(row, col) : 0.0;
}

std::string UltraCanvasSpreadsheet::GetCellDisplayValue(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCellDisplayValue(row, col) : std::string();
}

std::string UltraCanvasSpreadsheet::GetCellFormula(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetCellFormula(row, col) : std::string();
}

void UltraCanvasSpreadsheet::ClearCell(int row, int col) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(CellRange(row, col, row, col));
    sheet->ClearCell(row, col);
    if (formulaEngine_) {
        formulaEngine_->MarkDirty(CellAddress(row, col), sheet->GetName());
    }
    Recalculate();
}

void UltraCanvasSpreadsheet::ClearSelection() {
    ClearRange(GetSelection());
}

void UltraCanvasSpreadsheet::ClearRange(const CellRange& range) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(range);
    sheet->ClearRange(range);
    Recalculate();
}

// ============================================================================
// SELECTION
// ============================================================================

CellAddress UltraCanvasSpreadsheet::GetActiveCell() const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetActiveCell() : CellAddress(0, 0);
}

void UltraCanvasSpreadsheet::SetActiveCell(int row, int col) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetActiveCell(row, col);
    }
}

void UltraCanvasSpreadsheet::SetActiveCell(const CellAddress& addr) {
    SetActiveCell(addr.row, addr.col);
}

CellRange UltraCanvasSpreadsheet::GetSelection() const {
    const auto* sheet = GetActiveSheet();
    if (!sheet) return CellRange(CellAddress(0, 0));
    return sheet->GetSelection().GetPrimarySelection();
}

void UltraCanvasSpreadsheet::Select(const CellRange& range) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SelectRange(range);
    }
}

void UltraCanvasSpreadsheet::SelectCell(int row, int col) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SelectCell(row, col);
    }
}

void UltraCanvasSpreadsheet::SelectRow(int row) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SelectRow(row);
    }
}

void UltraCanvasSpreadsheet::SelectColumn(int col) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SelectColumn(col);
    }
}

void UltraCanvasSpreadsheet::SelectAll() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SelectAll();
    }
}

void UltraCanvasSpreadsheet::ExtendSelection(const CellAddress& toCell) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange range(sheet->GetActiveCell(), toCell);
    sheet->ExtendSelection(range);
}

void UltraCanvasSpreadsheet::AddToSelection(const CellRange& range) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->AddToSelection(range);
    }
}

bool UltraCanvasSpreadsheet::IsCellSelected(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->IsCellSelected(row, col) : false;
}

// ============================================================================
// FORMATTING
// ============================================================================

void UltraCanvasSpreadsheet::SetSelectionFont(const CellFont& font) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetFont(font);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionFontFamily(const std::string& family) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetFontFamily(family);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionFontSize(float size) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetFontSize(size);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionFontColor(const Color& color) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetFontColor(color);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionBackgroundColor(const Color& color) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetBackgroundColor(color);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionBorders(const CellBorders& borders) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetBorders(borders);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionAlignment(HorizontalAlignment h, VerticalAlignment v) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetAlignment(h, v);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionNumberFormat(const NumberFormat& format) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetNumberFormat(format);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetSelectionWrapText(bool wrap) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetTextWrap(wrap);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

Color UltraCanvasSpreadsheet::GetActiveCellBackgroundColor() const {
    CellAddress active = GetActiveCell();
    if (const auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().fill.foregroundColor;
    }
    return Colors::White;
}

HorizontalAlignment UltraCanvasSpreadsheet::GetActiveCellHAlign() const {
    CellAddress active = GetActiveCell();
    if (const auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().hAlign;
    }
    return HorizontalAlignment::General;
}

VerticalAlignment UltraCanvasSpreadsheet::GetActiveCellVAlign() const {
    CellAddress active = GetActiveCell();
    if (const auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().vAlign;
    }
    return VerticalAlignment::Bottom;
}

// ============================================================================
// MERGE CELLS
// ============================================================================

bool UltraCanvasSpreadsheet::MergeSelection() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return false;
    bool ok = sheet->MergeCells(GetSelection());
    RequestRedraw();
    return ok;
}

void UltraCanvasSpreadsheet::UnmergeSelection() {
    UnmergeCells(GetSelection());
}

void UltraCanvasSpreadsheet::MergeCells(const CellRange& range) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->MergeCells(range);
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::UnmergeCells(const CellRange& range) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->UnmergeCells(range);
        RequestRedraw();
    }
}

bool UltraCanvasSpreadsheet::IsCellMerged(int row, int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->IsCellMerged(row, col) : false;
}

// ============================================================================
// FREEZE PANES
// ============================================================================

void UltraCanvasSpreadsheet::FreezeRows(int count) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->FreezeRows(count);
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::FreezeColumns(int count) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->FreezeColumns(count);
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::FreezePanes(int rows, int cols) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetFreezePanes(rows, cols);
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::FreezePanesAtSelection() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->FreezePanesAt(sheet->GetActiveCell());
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::UnfreezePanes() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->UnfreezePanes();
        RequestRedraw();
    }
}

bool UltraCanvasSpreadsheet::HasFrozenPanes() const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->HasFrozenPanes() : false;
}

int UltraCanvasSpreadsheet::GetFrozenRowCount() const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetFrozenRowCount() : 0;
}

int UltraCanvasSpreadsheet::GetFrozenColumnCount() const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetFrozenColumnCount() : 0;
}

// ============================================================================
// ROWS AND COLUMNS
// ============================================================================

void UltraCanvasSpreadsheet::AutoFitSelectedColumns() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    for (int col = sel.start.col; col <= sel.end.col; ++col) {
        sheet->AutoFitColumnWidth(col);
    }
    UpdateLayout();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetColumnHidden(int col, bool hidden) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetColumnHidden(col, hidden);
        UpdateLayout();
        RequestRedraw();
    }
}

bool UltraCanvasSpreadsheet::IsColumnHidden(int col) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->IsColumnHidden(col) : false;
}

void UltraCanvasSpreadsheet::InsertColumns(int count) {
    InsertColumnsAt(GetSelection().start.col, count);
}

void UltraCanvasSpreadsheet::InsertColumnsAt(int col, int count) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordStructureChange();
    sheet->InsertColumns(col, count);
    Recalculate();
}

void UltraCanvasSpreadsheet::DeleteColumns(int count) {
    DeleteColumnsAt(GetSelection().start.col, count);
}

void UltraCanvasSpreadsheet::DeleteColumnsAt(int col, int count) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordStructureChange();
    sheet->DeleteColumns(col, count);
    Recalculate();
}

void UltraCanvasSpreadsheet::AutoFitSelectedRows() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        sheet->AutoFitRowHeight(row);
    }
    UpdateLayout();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetRowHidden(int row, bool hidden) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetRowHidden(row, hidden);
        UpdateLayout();
        RequestRedraw();
    }
}

bool UltraCanvasSpreadsheet::IsRowHidden(int row) const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->IsRowHidden(row) : false;
}

void UltraCanvasSpreadsheet::InsertRows(int count) {
    InsertRowsAt(GetSelection().start.row, count);
}

void UltraCanvasSpreadsheet::InsertRowsAt(int row, int count) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordStructureChange();
    sheet->InsertRows(row, count);
    Recalculate();
}

void UltraCanvasSpreadsheet::DeleteRows(int count) {
    DeleteRowsAt(GetSelection().start.row, count);
}

void UltraCanvasSpreadsheet::DeleteRowsAt(int row, int count) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordStructureChange();
    sheet->DeleteRows(row, count);
    Recalculate();
}

// ============================================================================
// FIND / REPLACE
// ============================================================================

CellAddress UltraCanvasSpreadsheet::FindNext(const std::string& searchText, bool matchCase,
                                             bool matchEntireCell, bool searchFormulas) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return CellAddress();
    SheetFindOptions opts;
    opts.matchCase = matchCase;
    opts.matchEntireCell = matchEntireCell;
    opts.searchFormulas = searchFormulas;
    return sheet->FindNext(searchText, sheet->GetActiveCell(), opts);
}

std::vector<CellAddress> UltraCanvasSpreadsheet::FindAll(const std::string& searchText, bool matchCase,
                                                         bool matchEntireCell, bool searchFormulas) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return {};
    SheetFindOptions opts;
    opts.matchCase = matchCase;
    opts.matchEntireCell = matchEntireCell;
    opts.searchFormulas = searchFormulas;
    return sheet->FindAll(searchText, opts);
}

int UltraCanvasSpreadsheet::ReplaceAll(const std::string& searchText, const std::string& replaceText,
                                       bool matchCase, bool matchEntireCell) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return 0;
    SheetFindOptions opts;
    opts.matchCase = matchCase;
    opts.matchEntireCell = matchEntireCell;
    int count = sheet->ReplaceAll(searchText, replaceText, opts);
    Recalculate();
    return count;
}

// ============================================================================
// SORTING & FILTERING
// ============================================================================

void UltraCanvasSpreadsheet::SortSelection(const std::vector<SortCriteria>& criteria) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    if (recordingUndo_) RecordRangeChange(GetSelection());
    sheet->Sort(GetSelection(), criteria);
    Recalculate();
}

void UltraCanvasSpreadsheet::SortSelectionAscending() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    sheet->SortByColumn(sel, sel.start.col, SortOrder::Ascending);
    Recalculate();
}

void UltraCanvasSpreadsheet::SortSelectionDescending() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (recordingUndo_) RecordRangeChange(sel);
    sheet->SortByColumn(sel, sel.start.col, SortOrder::Descending);
    Recalculate();
}

void UltraCanvasSpreadsheet::SetAutoFilter() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    sheet->SetAutoFilter(sheet->GetUsedRange());
    RequestRedraw();
}

void UltraCanvasSpreadsheet::RemoveAutoFilter() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->RemoveAutoFilter();
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::ApplyFilter(int column, const ColumnFilter& filter) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->ApplyFilter(column, filter);
        RequestRedraw();
    }
}

void UltraCanvasSpreadsheet::ClearAllFilters() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->ClearAllFilters();
        RequestRedraw();
    }
}

// ============================================================================
// NAMED RANGES
// ============================================================================

void UltraCanvasSpreadsheet::AddNamedRange(const std::string& name, const CellRange& range,
                                           const std::string& sheetScope) {
    if (name.empty()) return;
    namedRanges_[name] = NamedRange(name, range, sheetScope);
}

void UltraCanvasSpreadsheet::RemoveNamedRange(const std::string& name) {
    namedRanges_.erase(name);
}

bool UltraCanvasSpreadsheet::HasNamedRange(const std::string& name) const {
    return namedRanges_.find(name) != namedRanges_.end();
}

CellRange UltraCanvasSpreadsheet::GetNamedRange(const std::string& name) const {
    auto it = namedRanges_.find(name);
    if (it != namedRanges_.end()) return it->second.range;
    return CellRange();
}

std::vector<std::string> UltraCanvasSpreadsheet::GetNamedRangeNames() const {
    std::vector<std::string> names;
    names.reserve(namedRanges_.size());
    for (const auto& [name, range] : namedRanges_) {
        names.push_back(name);
    }
    return names;
}

// ============================================================================
// FORMULA ENGINE
// ============================================================================

void UltraCanvasSpreadsheet::Recalculate() {
    if (formulaEngine_) formulaEngine_->Recalculate();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::RecalculateAll() {
    if (formulaEngine_) formulaEngine_->RecalculateAll();
    RequestRedraw();
}

bool UltraCanvasSpreadsheet::IsAutoCalculateEnabled() const {
    return formulaEngine_ ? formulaEngine_->IsAutoCalculateEnabled() : false;
}

void UltraCanvasSpreadsheet::SetAutoCalculate(bool enabled) {
    if (formulaEngine_) formulaEngine_->SetAutoCalculate(enabled);
}

// ============================================================================
// PRINT
// ============================================================================

PrintSettings& UltraCanvasSpreadsheet::GetPrintSettings() {
    return GetActiveSheet()->GetPrintSettings();
}

const PrintSettings& UltraCanvasSpreadsheet::GetPrintSettings() const {
    return GetActiveSheet()->GetPrintSettings();
}

void UltraCanvasSpreadsheet::SetPrintArea(const CellRange& range) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetPrintArea(range);
    }
}

void UltraCanvasSpreadsheet::ClearPrintArea() {
    if (auto* sheet = GetActiveSheet()) {
        sheet->ClearPrintArea();
    }
}

// ============================================================================
// VIEW SETTINGS
// ============================================================================

void UltraCanvasSpreadsheet::SetShowRowHeaders(bool show) {
    showRowHeaders_ = show;
    UpdateLayout();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetShowColumnHeaders(bool show) {
    showColumnHeaders_ = show;
    UpdateLayout();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetShowFormulaBar(bool show) {
    showFormulaBar_ = show;
    UpdateLayout();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::SetShowSheetTabs(bool show) {
    showSheetTabs_ = show;
    UpdateLayout();
    RequestRedraw();
}

int UltraCanvasSpreadsheet::GetZoomLevel() const {
    const auto* sheet = GetActiveSheet();
    return sheet ? sheet->GetZoomLevel() : 100;
}

void UltraCanvasSpreadsheet::SetZoomLevel(int percent) {
    if (auto* sheet = GetActiveSheet()) {
        sheet->SetZoomLevel(percent);
        UpdateLayout();
        RequestRedraw();
    }
}

// ============================================================================
// UTILITY
// ============================================================================

CellAddress UltraCanvasSpreadsheet::GetCellAtPosition(int x, int y) const {
    return ScreenToCell(x, y);
}

// ============================================================================
// UNDO / REDO
// ============================================================================
//
// Snapshot-based undo/redo. Each recorded action captures the pre-change cell
// data of a CellRange on a named sheet. To undo, we first snapshot the current
// state of that range into an inverse action (pushed to the opposite stack),
// then restore the stored snapshot. This keeps value/format/clear/paste range
// edits correct. Structural actions (row/column insert/delete) are best-effort:
// they are recorded so they consume a stack slot, but restoring exact structure
// is not attempted here.

void UltraCanvasSpreadsheet::PushUndo(const SpreadsheetUndoAction& action) {
    undoStack_.push(action);
    // A fresh user action invalidates the redo history.
    while (!redoStack_.empty()) redoStack_.pop();

    // Cap the undo history depth.
    if ((int)undoStack_.size() > SpreadsheetLimits::MaxUndoLevels) {
        // std::stack has no bounded trim; rebuild keeping the newest entries.
        std::vector<SpreadsheetUndoAction> tmp;
        while (!undoStack_.empty()) { tmp.push_back(undoStack_.top()); undoStack_.pop(); }
        // tmp is newest..oldest; keep only the newest MaxUndoLevels entries.
        int keep = SpreadsheetLimits::MaxUndoLevels;
        for (int i = keep - 1; i >= 0; --i) {
            undoStack_.push(tmp[i]);
        }
    }

    if (onUndoStackChange) onUndoStackChange();
}

void UltraCanvasSpreadsheet::ApplyUndo(const SpreadsheetUndoAction& action) {
    SpreadsheetSheet* sheet = GetSheetByName(action.sheetName);
    if (!sheet) sheet = GetActiveSheet();
    if (!sheet) return;

    switch (action.type) {
        case UndoActionType::RowInsert:
        case UndoActionType::RowDelete:
        case UndoActionType::ColumnInsert:
        case UndoActionType::ColumnDelete:
            // Structural restore is best-effort and not reconstructed here.
            break;
        default: {
            // Restore the captured snapshot into the stored range.
            const CellRange& r = action.range;
            // Clear the target range first so removed cells disappear on undo.
            sheet->ClearRange(r);
            for (int i = 0; i < (int)action.cellData.size(); ++i) {
                int row = r.start.row + i;
                const auto& rowData = action.cellData[i];
                for (int j = 0; j < (int)rowData.size(); ++j) {
                    int col = r.start.col + j;
                    SpreadsheetCell* cell = sheet->GetCell(row, col);
                    if (cell) {
                        *cell = rowData[j];
                        cell->SetPosition(row, col);
                    }
                }
            }
            break;
        }
    }

    if (formulaEngine_) formulaEngine_->Recalculate();
    RequestRedraw();
}

void UltraCanvasSpreadsheet::Undo() {
    if (undoStack_.empty()) return;

    SpreadsheetUndoAction action = undoStack_.top();

    // Snapshot the current state of the action's range as the redo entry.
    SpreadsheetSheet* sheet = GetSheetByName(action.sheetName);
    if (!sheet) sheet = GetActiveSheet();
    if (sheet) {
        SpreadsheetUndoAction inverse = action;
        inverse.cellData = sheet->CopyCells(action.range);
        redoStack_.push(inverse);
    }

    // Restore without re-recording.
    bool wasRecording = recordingUndo_;
    recordingUndo_ = false;
    ApplyUndo(action);
    recordingUndo_ = wasRecording;

    undoStack_.pop();

    if (onUndoStackChange) onUndoStackChange();
}

void UltraCanvasSpreadsheet::Redo() {
    if (redoStack_.empty()) return;

    SpreadsheetUndoAction action = redoStack_.top();

    SpreadsheetSheet* sheet = GetSheetByName(action.sheetName);
    if (!sheet) sheet = GetActiveSheet();
    if (sheet) {
        SpreadsheetUndoAction inverse = action;
        inverse.cellData = sheet->CopyCells(action.range);
        undoStack_.push(inverse);
    }

    bool wasRecording = recordingUndo_;
    recordingUndo_ = false;
    ApplyUndo(action);
    recordingUndo_ = wasRecording;

    redoStack_.pop();

    if (onUndoStackChange) onUndoStackChange();
}

void UltraCanvasSpreadsheet::ClearUndoHistory() {
    while (!undoStack_.empty()) undoStack_.pop();
    while (!redoStack_.empty()) redoStack_.pop();
    if (onUndoStackChange) onUndoStackChange();
}

std::string UltraCanvasSpreadsheet::GetUndoDescription() const {
    return undoStack_.empty() ? std::string() : undoStack_.top().description;
}

std::string UltraCanvasSpreadsheet::GetRedoDescription() const {
    return redoStack_.empty() ? std::string() : redoStack_.top().description;
}

// ----- Undo recording helpers -----
//
// The header has no pending-action member, so BeginUndoGroup / CommitUndoGroup /
// CancelUndoGroup are lightweight: recording is captured eagerly by
// RecordRangeChange (which pushes a complete snapshot directly onto the undo
// stack). The group helpers exist to satisfy the interface and toggle the
// recording flag for nested operations.

void UltraCanvasSpreadsheet::BeginUndoGroup(UndoActionType /*type*/, const std::string& /*description*/) {
    // No explicit pending-action storage; snapshots are captured eagerly by
    // RecordRangeChange. Nothing to set up here beyond ensuring recording is on.
}

void UltraCanvasSpreadsheet::RecordCellChange(int row, int col) {
    RecordRangeChange(CellRange(row, col, row, col));
}

void UltraCanvasSpreadsheet::RecordRangeChange(const CellRange& range) {
    if (!recordingUndo_) return;
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;

    SpreadsheetUndoAction action;
    action.type = UndoActionType::CellValueChange;
    action.sheetName = sheet->GetName();
    action.range = range;
    action.cellData = sheet->CopyCells(range);  // pre-change snapshot
    action.description = "Edit " + range.ToString();

    PushUndo(action);
}

void UltraCanvasSpreadsheet::RecordStructureChange() {
    if (!recordingUndo_) return;
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;

    // Structural changes are recorded best-effort: capture the used range so a
    // stack slot is consumed and a description is available. Exact structural
    // reconstruction is not performed by ApplyUndo.
    SpreadsheetUndoAction action;
    action.type = UndoActionType::Composite;
    action.sheetName = sheet->GetName();
    action.range = sheet->GetUsedRange();
    action.description = "Structure change";

    PushUndo(action);
}

void UltraCanvasSpreadsheet::CommitUndoGroup() {
    // Snapshots are pushed eagerly; nothing to flush.
    if (onUndoStackChange) onUndoStackChange();
}

void UltraCanvasSpreadsheet::CancelUndoGroup() {
    // Best-effort: drop the most recent snapshot captured during this group.
    if (!undoStack_.empty()) {
        undoStack_.pop();
        if (onUndoStackChange) onUndoStackChange();
    }
}

} // namespace UltraCanvas
