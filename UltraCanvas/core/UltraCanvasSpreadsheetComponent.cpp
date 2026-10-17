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
#include <cctype>
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

    EnsureSheetTabVisible(index);
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
    //if (sheets_.size() <= 1) return;  // Always keep at least one sheet.
    if (!recordingUndo_) {
        RemoveSheetAt(index, nullptr);
        return;
    }
    SpreadsheetUndoAction action;
    action.type = UndoActionType::SheetDelete;
    action.description = "Delete sheet " + sheets_[index]->GetName();
    RemoveSheetAt(index, &action);
    PushUndo(action);
}

void UltraCanvasSpreadsheet::RemoveSheetAt(int index, SpreadsheetUndoAction* record) {
    if (index < 0 || index >= (int)sheets_.size()) return;
    const std::string removedName = sheets_[index]->GetName();
    if (record) {
        record->sheetName = removedName;
        record->sheetId = sheets_[index]->GetId();
        record->sheetIndex = index;
        record->removedSheet = std::shared_ptr<SpreadsheetSheet>(sheets_[index]->CloneKeepingId());
        record->rewrittenFormulas.clear();
    }
    sheets_.erase(sheets_.begin() + index);

    // References to it become #REF! in the formulas themselves, as in Excel;
    // what they said is kept for undo. The sheet's own undo steps stay: they
    // find their sheet by id, so they wait, under this step, for undo to put
    // it back, and a later sheet given the same name never receives them.
    for (auto& sheet : sheets_) {
        sheet->ForEachCell([&](int row, int col, SpreadsheetCell& cell) {
            if (!cell.HasFormula()) return;
            const std::string text = RemoveSheetFromFormula(cell.GetFormulaText(), removedName);
            if (text == cell.GetFormulaText()) return;
            if (record) record->rewrittenFormulas.push_back({ sheet->GetId(), row, col, cell.GetFormulaText() });
            cell.SetFormula(text);
        });
    }

    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    if (onSheetRemove) onSheetRemove(index);
    RecalculateAll();   // the #REF! formulas show it

    // Keep the active sheet index valid.
    if (activeSheetIndex_ >= (int)sheets_.size()) {
        activeSheetIndex_ = (int)sheets_.size() - 1;
    }
    SetActiveSheet(activeSheetIndex_);
}

void UltraCanvasSpreadsheet::RestoreDeletedSheet(const SpreadsheetUndoAction& action) {
    if (!action.removedSheet) return;
    const int index = std::clamp(action.sheetIndex, 0, (int)sheets_.size());
    auto sheet = action.removedSheet->CloneKeepingId();
    SpreadsheetSheet* ptr = sheet.get();
    // A sheet given its name since then keeps it; this one comes back as
    // "Gone (2)" rather than as a second "Gone".
    if (!ValidateSheetName(-1, ptr->GetName()).empty()) {
        const std::string base = ptr->GetName();
        for (int n = 2; n < 1000; ++n) {
            const std::string name = base + " (" + std::to_string(n) + ")";
            if (ValidateSheetName(-1, name).empty()) { ptr->SetName(name); break; }
        }
    }
    ptr->SetParent(this);
    SetupSheetCallbacks(ptr);
    sheets_.insert(sheets_.begin() + index, std::move(sheet));
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        sheets_[i]->SetIndex(i);
    }

    // The references the delete turned to #REF! name the sheet again.
    for (const auto& f : action.rewrittenFormulas) {
        for (auto& other : sheets_) {
            if (other->GetId() != f.sheetId) continue;
            if (SpreadsheetCell* cell = other->GetCell(f.row, f.col)) cell->SetFormula(f.formula);
            break;
        }
    }

    if (onSheetAdd) onSheetAdd(index);
    RecalculateAll();
    SetActiveSheet(index);
}

void UltraCanvasSpreadsheet::RemoveSheetByName(const std::string& name) {
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        if (sheets_[i]->GetName() == name) {
            RemoveSheet(i);
            return;
        }
    }
}

std::string UltraCanvasSpreadsheet::ValidateSheetName(int index, const std::string& name) const {
    std::string problem = CheckSheetName(name);
    if (!problem.empty()) return problem;
    for (int i = 0; i < (int)sheets_.size(); ++i) {
        if (i == index) continue;
        const std::string& other = sheets_[i]->GetName();
        bool same = other.size() == name.size();
        for (size_t k = 0; same && k < name.size(); ++k) {
            same = std::tolower(static_cast<unsigned char>(other[k])) ==
                   std::tolower(static_cast<unsigned char>(name[k]));
        }
        if (same) return "There is already a sheet called \"" + other + "\".";
    }
    return "";
}

bool UltraCanvasSpreadsheet::RenameSheet(int index, const std::string& newName) {
    if (index < 0 || index >= (int)sheets_.size()) return false;
    if (!ValidateSheetName(index, newName).empty()) return false;
    const std::string oldName = sheets_[index]->GetName();
    if (oldName == newName) return true;
    sheets_[index]->SetName(newName);

    // Formulas that name the sheet follow it to its new name.
    bool rewritten = false;
    for (auto& sheet : sheets_) {
        sheet->ForEachCell([&](int, int, SpreadsheetCell& cell) {
            if (!cell.HasFormula()) return;
            const std::string text = RenameSheetInFormula(cell.GetFormulaText(), oldName, newName);
            if (text == cell.GetFormulaText()) return;
            cell.SetFormula(text);   // parsed again on the next calculation
            rewritten = true;
        });
    }
    if (rewritten) RecalculateAll();

    // The undo history follows too: the formulas its steps would put back
    // name the new name (the steps find the sheet itself by its id).
    const uint64_t renamedId = sheets_[index]->GetId();
    RewriteUndoHistory([&](SpreadsheetUndoAction& action) {
        if (action.sheetId == renamedId || (action.sheetId == 0 && action.sheetName == oldName)) {
            action.sheetName = newName;
        }
        RewriteSnapshotFormulas(action, [&](const std::string& f) { return RenameSheetInFormula(f, oldName, newName); });
        return true;
    });

    if (onSheetRename) onSheetRename(index, newName);
    UpdateFormulaBar();
    RequestRedraw();
    return true;
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

CellRange UltraCanvasSpreadsheet::GetFormattingRange() const {
    CellRange sel = GetSelection();
    const auto* sheet = GetActiveSheet();
    if (!sheet) return sel;

    const bool wholeColumns = sel.end.row >= SpreadsheetLimits::MaxRows - 1;
    const bool wholeRows    = sel.end.col >= SpreadsheetLimits::MaxColumns - 1;
    if (!wholeColumns && !wholeRows) return sel;

    const CellRange used = sheet->GetUsedRange();
    if (wholeColumns) {
        sel.end.row = std::max(sel.start.row, std::min(sel.end.row, used.end.row));
    }
    if (wholeRows) {
        sel.end.col = std::max(sel.start.col, std::min(sel.end.col, used.end.col));
    }
    return sel;
}

void UltraCanvasSpreadsheet::SetSelectionFont(const CellFont& font) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
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
    CellRange sel = GetFormattingRange();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            sheet->GetCell(row, col)->SetTextWrap(wrap);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

void UltraCanvasSpreadsheet::AdjustSelectionDecimalPlaces(int delta) {
    auto* sheet = GetActiveSheet();
    if (!sheet || delta == 0) return;
    CellRange sel = GetFormattingRange();
    if (recordingUndo_) RecordRangeChange(sel);
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        for (int col = sel.start.col; col <= sel.end.col; ++col) {
            SpreadsheetCell* cell = sheet->GetCell(row, col);
            if (!cell) continue;
            NumberFormat fmt = cell->GetStyle().numberFormat;
            // "General" has no decimal setting of its own; the first
            // increase/decrease turns the cell into a plain number, as Excel
            // and LibreOffice both do.
            if (fmt.category == NumberFormatCategory::General ||
                fmt.category == NumberFormatCategory::Text) {
                fmt = NumberFormat::Number(std::max(0, delta), false);
            } else {
                int decimals = std::clamp(fmt.decimalPlaces + delta, 0, 15);
                if (decimals == fmt.decimalPlaces) continue;
                switch (fmt.category) {
                    case NumberFormatCategory::Currency:
                    case NumberFormatCategory::Accounting:
                        fmt = NumberFormat::Currency(fmt.currencySymbol, decimals,
                                                     fmt.currencySymbolAfter);
                        break;
                    case NumberFormatCategory::Percentage:
                        fmt = NumberFormat::Percentage(decimals);
                        break;
                    case NumberFormatCategory::Scientific:
                        fmt = NumberFormat::Scientific(decimals);
                        break;
                    default:
                        fmt = NumberFormat::Number(decimals, fmt.useThousandsSeparator);
                        break;
                }
            }
            cell->SetNumberFormat(fmt);
        }
    }
    if (onCellFormatChange) onCellFormatChange(sel.start.row, sel.start.col);
    RequestRedraw();
}

NumberFormat UltraCanvasSpreadsheet::GetActiveCellNumberFormat() const {
    CellAddress active = GetActiveCell();
    if (const auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().numberFormat;
    }
    return NumberFormat::General();
}

bool UltraCanvasSpreadsheet::GetActiveCellWrapText() const {
    CellAddress active = GetActiveCell();
    if (const auto* cell = GetCellIfExists(active.row, active.col)) {
        return cell->GetStyle().wrapText;
    }
    return false;
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
    CellTextMeasureFn measure = MakeSpreadsheetTextMeasurer(GetRenderContext());
    CellRange sel = GetFormattingRange();
    for (int col = sel.start.col; col <= sel.end.col; ++col) {
        sheet->AutoFitColumnWidth(col, measure);
        // Asked for explicitly, so the post-import pass must not undo it.
        sheet->GetOrCreateColumnDefinition(col).explicitWidth = true;
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
    CellRange sel = GetFormattingRange();
    for (int row = sel.start.row; row <= sel.end.row; ++row) {
        sheet->AutoFitRowHeight(row);
        // Asked for explicitly, so nothing later overrides it.
        sheet->GetOrCreateRowDefinition(row).explicitHeight = true;
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

void UltraCanvasSpreadsheet::SortSelectionByColumn(int column, SortOrder order) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return;
    CellRange sel = GetSelection();
    if (column < sel.start.col || column > sel.end.col || sel.RowCount() < 2) return;
    if (recordingUndo_) RecordRangeChange(sel);
    // Sort moves every column of the range with the key column, so each
    // selected row keeps its cells together and nothing outside moves.
    sheet->SortByColumn(sel, column, order);
    // Formulas elsewhere may read the moved cells; they are not dirty, so
    // recalculate them all.
    RecalculateAll();

    headerSortSheet_ = sheet->GetName();
    headerSortRange_ = sel;
    headerSortColumn_ = column;
    headerSortAscending_ = (order == SortOrder::Ascending);

    if (onSelectionSorted) onSelectionSorted(sel, column, order);
    if (onStatusChange) {
        onStatusChange("Sorted " + sel.ToString() + " by column " + CellAddress::ColumnToLetter(column) +
                       (headerSortAscending_ ? ", ascending" : ", descending"));
    }
    Invalidate();
}

bool UltraCanvasSpreadsheet::AutoFillSelection(const CellRange& destination, AutoFillMode mode) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return false;
    const CellRange source = GetSelection();

    // The destination must be the source grown along one axis only.
    const bool sameCols = destination.start.col == source.start.col && destination.end.col == source.end.col;
    const bool sameRows = destination.start.row == source.start.row && destination.end.row == source.end.row;
    const bool grows = sameCols ? (destination.start.row <= source.start.row && destination.end.row >= source.end.row)
                     : sameRows ? (destination.start.col <= source.start.col && destination.end.col >= source.end.col)
                     : false;
    if (!grows || destination == source) return false;
    for (const auto& merge : sheet->GetMergedCells()) {
        if (merge.range.Intersects(destination)) return false;
    }

    if (recordingUndo_) RecordRangeChange(destination);
    sheet->AutoFill(source, destination, mode);
    sheet->SelectRange(destination);
    RecalculateAll();   // formulas elsewhere may read the filled cells
    if (onStatusChange) {
        onStatusChange((mode == AutoFillMode::Copy ? "Copied " : "Filled ") + destination.ToString() +
                       " from " + source.ToString());
    }
    Invalidate();
    return true;
}

bool UltraCanvasSpreadsheet::AutoFillSelectionToDataEnd() {
    auto* sheet = GetActiveSheet();
    if (!sheet) return false;
    const CellRange source = GetSelection();
    const int last = sheet->GetAutoFillDownEnd(source);
    if (last <= source.end.row) return false;
    CellRange destination = source;
    destination.end.row = last;
    return AutoFillSelection(destination);
}

namespace {

// Ctrl+D / Ctrl+R: the line copied from and the range it is copied over.
// `down` copies the top row (or the row above a one-row selection) down,
// otherwise the left column (or the column to the left) right.
bool FillCommandRanges(const CellRange& selection, bool down, CellRange& source, CellRange& destination) {
    source = selection;
    destination = selection;
    if (down) {
        if (selection.start.row == selection.end.row) {
            if (selection.start.row == 0) return false;
            source.start.row = source.end.row = selection.start.row - 1;
            destination.start.row = source.start.row;
        } else {
            source.end.row = selection.start.row;
        }
    } else {
        if (selection.start.col == selection.end.col) {
            if (selection.start.col == 0) return false;
            source.start.col = source.end.col = selection.start.col - 1;
            destination.start.col = source.start.col;
        } else {
            source.end.col = selection.start.col;
        }
    }
    return true;
}

} // namespace

bool UltraCanvasSpreadsheet::FillSelectionDown()  { return FillSelectionAlong(true); }
bool UltraCanvasSpreadsheet::FillSelectionRight() { return FillSelectionAlong(false); }

bool UltraCanvasSpreadsheet::FillSelectionAlong(bool down) {
    auto* sheet = GetActiveSheet();
    if (!sheet) return false;
    const CellRange selection = GetSelection();
    CellRange source, destination;
    if (!FillCommandRanges(selection, down, source, destination)) return false;
    for (const auto& merge : sheet->GetMergedCells()) {
        if (merge.range.Intersects(destination)) return false;
    }
    if (recordingUndo_) RecordRangeChange(selection);
    sheet->AutoFill(source, destination, AutoFillMode::Copy);
    sheet->SelectRange(selection);
    RecalculateAll();
    if (onStatusChange) onStatusChange("Filled " + selection.ToString() + (down ? " down" : " right"));
    Invalidate();
    return true;
}

void UltraCanvasSpreadsheet::SetHeaderSortEnabled(bool enabled) {
    if (headerSortEnabled_ == enabled) return;
    headerSortEnabled_ = enabled;
    Invalidate();
}

int UltraCanvasSpreadsheet::GetHeaderSortColumn() const {
    const auto* sheet = GetActiveSheet();
    if (!sheet || headerSortColumn_ < 0 || sheet->GetName() != headerSortSheet_) return -1;
    return GetSelection() == headerSortRange_ ? headerSortColumn_ : -1;
}

bool UltraCanvasSpreadsheet::GetHeaderSortableRange(CellRange& range) const {
    const auto* sheet = GetActiveSheet();
    if (!sheet || !headerSortEnabled_ || !showColumnHeaders_) return false;

    // One rectangular block of at least two rows. Whole rows and whole
    // columns are left out: sorting a full column would take its title row
    // along and run over the entire sheet, which is never what a click on a
    // small header button means.
    const auto& selection = sheet->GetSelection();
    if (selection.selections.size() != 1) return false;
    const CellRange sel = selection.selections[0];
    if (sel.RowCount() < 2) return false;
    if (sel.end.row >= SpreadsheetLimits::MaxRows - 1 ||
        sel.end.col >= SpreadsheetLimits::MaxColumns - 1) return false;

    // Moving rows through a merged area would tear it apart.
    for (const auto& merge : sheet->GetMergedCells()) {
        if (merge.range.Intersects(sel)) return false;
    }
    range = sel;
    return true;
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

void UltraCanvasSpreadsheet::RewriteUndoHistory(const std::function<bool(SpreadsheetUndoAction&)>& keep) {
    auto rewrite = [&keep](std::stack<SpreadsheetUndoAction>& stack) {
        std::vector<SpreadsheetUndoAction> actions;   // top first
        while (!stack.empty()) { actions.push_back(std::move(stack.top())); stack.pop(); }
        for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
            if (keep(*it)) stack.push(std::move(*it));
        }
    };
    const size_t before = undoStack_.size() + redoStack_.size();
    rewrite(undoStack_);
    rewrite(redoStack_);
    if (undoStack_.size() + redoStack_.size() != before && onUndoStackChange) onUndoStackChange();
}

void UltraCanvasSpreadsheet::RewriteSnapshotFormulas(SpreadsheetUndoAction& action,
                                                     const std::function<std::string(const std::string&)>& rewrite) {
    for (auto& row : action.cellData) {
        for (auto& cell : row) {
            if (!cell.HasFormula()) continue;
            const std::string text = rewrite(cell.GetFormulaText());
            if (text != cell.GetFormulaText()) cell.SetFormula(text);
        }
    }
    // A deleted sheet's step: the formulas it would put back, on the sheet
    // and on the others.
    for (auto& f : action.rewrittenFormulas) f.formula = rewrite(f.formula);
    if (action.removedSheet) {
        action.removedSheet->ForEachCell([&](int, int, SpreadsheetCell& cell) {
            if (!cell.HasFormula()) return;
            const std::string text = rewrite(cell.GetFormulaText());
            if (text != cell.GetFormulaText()) cell.SetFormula(text);
        });
    }
}

SpreadsheetSheet* UltraCanvasSpreadsheet::FindUndoSheet(const SpreadsheetUndoAction& action) {
    if (action.sheetId == 0) return GetSheetByName(action.sheetName);
    for (auto& sheet : sheets_) {
        if (sheet->GetId() == action.sheetId) return sheet.get();
    }
    return nullptr;
}

void UltraCanvasSpreadsheet::ApplyUndo(const SpreadsheetUndoAction& action) {
    // Only ever into the sheet it was recorded on - never another sheet that
    // happens to be open, or that has since taken its name.
    SpreadsheetSheet* sheet = FindUndoSheet(action);
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

    if (action.type == UndoActionType::SheetDelete) {
        // Put the sheet back; redoing deletes it again.
        undoStack_.pop();
        RestoreDeletedSheet(action);
        redoStack_.push(action);
        if (onUndoStackChange) onUndoStackChange();
        return;
    }

    // Snapshot the current state of the action's range as the redo entry.
    SpreadsheetSheet* sheet = FindUndoSheet(action);
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

    if (action.type == UndoActionType::SheetDelete) {
        redoStack_.pop();
        SpreadsheetUndoAction again = action;
        for (int i = 0; i < (int)sheets_.size(); ++i) {
            if (sheets_[i]->GetId() != action.sheetId) continue;
            RemoveSheetAt(i, &again);   // records what this delete rewrites
            undoStack_.push(again);
            break;
        }
        if (onUndoStackChange) onUndoStackChange();
        return;
    }

    SpreadsheetSheet* sheet = FindUndoSheet(action);
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
    action.sheetId = sheet->GetId();
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
    action.sheetId = sheet->GetId();
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
