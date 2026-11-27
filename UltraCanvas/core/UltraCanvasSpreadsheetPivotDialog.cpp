// core/UltraCanvasSpreadsheetPivotDialog.cpp
// Pivot tables on the grid and their field list: the grid's calls that
// write, refresh, change and remove a pivot (UltraCanvasSpreadsheet's PIVOT
// TABLES), keep its cells read-only, follow its source's changes and drill
// into a value; the field list panel (UltraCanvasSpreadsheetPivotPanel.h)
// and the dialog around it (ShowSpreadsheetPivotDialog), built from the
// framework's own elements.
// Version: 1.0.1 - the field list one above the other: the four add buttons two by two, and Remove,
//                  Move up and Move down each as wide as its name, so a narrow panel (UltraOffice's
//                  Pivot table tool panel) shows them whole
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasSpreadsheetPivotPanel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// THE GRID'S CALLS
// ============================================================================

namespace {

bool SameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// A sheet name of `workbook` nobody has yet: "Pivot", "Pivot 2" ...
std::string FreeSheetName(const UltraCanvasSpreadsheet& workbook, const std::string& base) {
    std::string name = base;
    for (int n = 2; workbook.GetSheetByName(name); ++n) name = base + " " + std::to_string(n);
    return name;
}

// "Sheet1!A1:D20", "'My data'.A1:D20" or "A1:D20" (on `fallbackSheet`).
bool ParseSourceText(const std::string& text, const std::string& fallbackSheet, std::string& sheet, CellRange& range) {
    SpreadsheetChartRange parsed;
    if (SpreadsheetChartRange::FromExcel(text, parsed) || SpreadsheetChartRange::FromOdf(text, parsed)) {
        sheet = parsed.sheet;
        range = CellRange(parsed.range.start.row, parsed.range.start.col, parsed.range.end.row, parsed.range.end.col);
        return true;
    }
    std::string plain = text;
    plain.erase(std::remove(plain.begin(), plain.end(), '$'), plain.end());
    const CellRange local = CellRange::FromString(plain);
    if (!local.IsValid() || plain.empty()) return false;
    sheet = fallbackSheet;
    range = CellRange(local.start.row, local.start.col, local.end.row, local.end.col);
    return true;
}

std::string SourceText(const SpreadsheetPivotTable& pivot) {
    SpreadsheetChartRange where{pivot.sourceSheet, pivot.sourceRange};
    return where.ToExcel();
}

} // namespace

void UltraCanvasSpreadsheet::NotePivotSourceChange(const std::string& sheetName, const CellRange& range) {
    for (const std::string& noted : pivotSourcesChanged_) {
        if (noted == sheetName) return;
    }
    for (const auto& sheet : sheets_) {
        if (!sheet) continue;
        for (const SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
            if (pivot.unsupportedReason.empty() && SameName(pivot.sourceSheet, sheetName) &&
                pivot.sourceRange.Intersects(range)) {
                pivotSourcesChanged_.push_back(sheetName);
                return;
            }
        }
    }
}

bool UltraCanvasSpreadsheet::WritePivotTable(SpreadsheetSheet& sheet, SpreadsheetPivotTable& pivot,
                                             std::string* problem) {
    if (formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    const SpreadsheetSheetLookup lookup = [this](const std::string& name) -> const SpreadsheetSheet* {
        for (const auto& one : sheets_) {
            if (one && SameName(one->GetName(), name)) return one.get();
        }
        return nullptr;
    };
    writingPivots_ = true;
    const bool ok = RefreshSpreadsheetPivot(pivot, sheet, lookup, problem);
    writingPivots_ = false;
    // What the pivot wrote may be read by formulas.
    if (ok && formulaEngine_ && formulaEngine_->HasPendingChanges()) formulaEngine_->Recalculate();
    Invalidate();
    return ok;
}

void UltraCanvasSpreadsheet::FitPivotColumns(const SpreadsheetPivotTable& pivot) {
    // Its names and totals readable: each column as wide as what it holds.
    if (!pivot.HasOutput()) return;
    for (int c = pivot.outputRange.start.col; c <= pivot.outputRange.end.col && c < pivot.outputRange.start.col + 50; ++c) {
        AutoFitColumnWidth(c);
    }
}

bool UltraCanvasSpreadsheet::RefusePivotEdit(const CellRange& range) {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || writingPivots_) return false;
    for (const SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
        if (pivot.HasOutput() && pivot.outputRange.Intersects(range)) {
            if (onStatusChange) {
                onStatusChange(Tr("These cells belong to a pivot table: change its fields, or refresh it."));
            }
            return true;
        }
    }
    return false;
}

int UltraCanvasSpreadsheet::InsertPivotTable(const SpreadsheetPivotTable& given, int sheetIndex, std::string* problem) {
    if (IsEditing()) StopEditing(true);
    SpreadsheetPivotTable pivot = given;
    // A name no other pivot of the workbook has.
    bool taken = pivot.name.empty();
    for (int s = 0; s < GetSheetCount() && !taken; ++s) {
        if (const SpreadsheetSheet* other = GetSheet(s)) {
            for (const SpreadsheetPivotTable& one : other->GetPivotTables()) taken = taken || one.name == pivot.name;
        }
    }
    if (taken) pivot.name = NextSpreadsheetPivotName(*this);
    pivot.outputRange = CellRange(-1, -1, -1, -1);
    bool madeSheet = false;
    if (sheetIndex < 0) {
        sheetIndex = std::min(GetSheetCount(), GetActiveSheetIndex() + 1);
        if (!InsertSheet(sheetIndex, FreeSheetName(*this, Tr("Pivot")))) return -1;
        madeSheet = true;
        pivot.target = CellAddress(0, 0);
    }
    SpreadsheetSheet* sheet = GetSheet(sheetIndex);
    if (!sheet) return -1;
    if (!WritePivotTable(*sheet, pivot, problem)) {
        if (madeSheet) RemoveSheet(sheetIndex);
        return -1;
    }
    sheet->GetPivotTables().push_back(pivot);
    SetActiveSheet(sheetIndex);
    FitPivotColumns(pivot);
    SelectCell(pivot.target.row, pivot.target.col);
    EnsureCellVisible(pivot.target.row, pivot.target.col);
    if (onPivotTablesChange) onPivotTablesChange();
    return static_cast<int>(sheet->GetPivotTables().size()) - 1;
}

bool UltraCanvasSpreadsheet::SetPivotTable(int sheetIndex, int index, const SpreadsheetPivotTable& given,
                                           std::string* problem) {
    SpreadsheetSheet* sheet = GetSheet(sheetIndex);
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return false;
    SpreadsheetPivotTable& pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    SpreadsheetPivotTable changed = given;
    changed.outputRange = pivot.outputRange;   // the cells it fills now, to clear
    if (!WritePivotTable(*sheet, changed, problem)) return false;
    pivot = changed;
    if (sheetIndex == GetActiveSheetIndex()) FitPivotColumns(pivot);
    if (onPivotTablesChange) onPivotTablesChange();
    return true;
}

bool UltraCanvasSpreadsheet::RemovePivotTable(int sheetIndex, int index) {
    SpreadsheetSheet* sheet = GetSheet(sheetIndex);
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return false;
    auto& pivots = sheet->GetPivotTables();
    writingPivots_ = true;
    ClearSpreadsheetPivotCells(pivots[static_cast<size_t>(index)], *sheet);
    writingPivots_ = false;
    pivots.erase(pivots.begin() + index);
    Invalidate();
    if (onPivotTablesChange) onPivotTablesChange();
    return true;
}

bool UltraCanvasSpreadsheet::RefreshPivotTable(int sheetIndex, int index, std::string* problem) {
    SpreadsheetSheet* sheet = GetSheet(sheetIndex);
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return false;
    if (!WritePivotTable(*sheet, sheet->GetPivotTables()[static_cast<size_t>(index)], problem)) return false;
    if (onPivotTablesChange) onPivotTablesChange();
    return true;
}

int UltraCanvasSpreadsheet::RefreshPivotTables() {
    pivotSourcesChanged_.clear();
    int written = 0;
    for (auto& sheet : sheets_) {
        if (!sheet) continue;
        for (SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
            if (pivot.unsupportedReason.empty() && WritePivotTable(*sheet, pivot, nullptr)) ++written;
        }
    }
    if (written > 0 && onPivotTablesChange) onPivotTablesChange();
    return written;
}

bool UltraCanvasSpreadsheet::RefreshChangedPivotTables() {
    if (pivotSourcesChanged_.empty() || writingPivots_) return false;
    const std::vector<std::string> changed = std::move(pivotSourcesChanged_);
    pivotSourcesChanged_.clear();
    bool any = false;
    for (auto& sheet : sheets_) {
        if (!sheet) continue;
        for (SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) {
            if (!pivot.unsupportedReason.empty()) continue;
            const bool hit = std::any_of(changed.begin(), changed.end(),
                                         [&pivot](const std::string& name) { return SameName(name, pivot.sourceSheet); });
            if (hit && WritePivotTable(*sheet, pivot, nullptr)) any = true;
        }
    }
    // A pivot reading another's cells is written in the same round.
    pivotSourcesChanged_.clear();
    return any;
}

int UltraCanvasSpreadsheet::GetActivePivotTable() const {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return -1;
    const CellAddress active = sheet->GetActiveCell();
    return sheet->PivotTableAt(active.row, active.col);
}

bool UltraCanvasSpreadsheet::SetPivotTableSource(int index, const std::string& sheetName, const CellRange& range,
                                                 std::string* problem) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    const SpreadsheetSheet* source = GetSheetByName(sheetName);
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return false;
    if (!source) {
        if (problem) *problem = TrF("There is no sheet \"{0}\".", sheetName);
        return false;
    }
    SpreadsheetPivotTable pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    const std::vector<std::string> oldNames = pivot.fieldNames;
    const std::vector<std::string> newNames = SpreadsheetPivotFieldNames(*source, range);
    auto remap = [&](int column) {
        if (column < 0 || column >= static_cast<int>(oldNames.size())) return -1;
        for (size_t i = 0; i < newNames.size(); ++i) if (newNames[i] == oldNames[static_cast<size_t>(column)]) return static_cast<int>(i);
        return -1;
    };
    for (auto* list : {&pivot.rowFields, &pivot.columnFields, &pivot.filterFields}) {
        std::vector<PivotField> kept;
        for (PivotField f : *list) {
            f.column = remap(f.column);
            if (f.column >= 0) kept.push_back(f);
        }
        *list = kept;
    }
    std::vector<PivotValueField> values;
    for (PivotValueField v : pivot.valueFields) {
        v.column = remap(v.column);
        if (v.column >= 0) values.push_back(v);
    }
    pivot.valueFields = values;
    pivot.sourceSheet = source->GetName();
    pivot.sourceRange = range;
    pivot.sourceName.clear();
    pivot.fieldNames = newNames;
    return SetPivotTable(GetActiveSheetIndex(), index, pivot, problem);
}

int UltraCanvasSpreadsheet::DrillDownPivotCell(int row, int col) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return -1;
    const int index = sheet->PivotTableAt(row, col);
    if (index < 0) return -1;
    const SpreadsheetPivotTable pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    const SpreadsheetSheet* source = GetSheetByName(pivot.sourceSheet);
    if (!source || !pivot.unsupportedReason.empty()) return -1;
    const std::vector<int> rows = SpreadsheetPivotDrillDownRows(pivot, *source, row, col);
    if (rows.empty()) return -1;
    const int at = std::min(GetSheetCount(), GetActiveSheetIndex() + 1);
    SpreadsheetSheet* details = InsertSheet(at, FreeSheetName(*this, Tr("Details")));
    if (!details) return -1;
    source = GetSheetByName(pivot.sourceSheet);   // the list of sheets moved
    if (!source) return -1;
    const CellRange range = pivot.sourceRange;
    auto copyRow = [&](int from, int to) {
        for (int c = range.start.col; c <= range.end.col; ++c) {
            const SpreadsheetCell* cell = source->GetCellIfExists(from, c);
            if (!cell || cell->IsEmpty()) continue;
            SpreadsheetCell& copy = details->GetOrCreateCell(to, c - range.start.col);
            copy = *cell;
            copy.SetPosition(to, c - range.start.col);
            if (cell->HasFormula()) {
                // The value, not the formula: its references were the source's.
                const CellValueVariant value = cell->GetRawValue();
                CellValueType type = CellValueType::Text;
                if (std::holds_alternative<double>(value)) type = CellValueType::Number;
                else if (std::holds_alternative<bool>(value)) type = CellValueType::Boolean;
                else if (std::holds_alternative<CellErrorType>(value)) type = CellValueType::Error;
                else if (std::holds_alternative<CurrencyValue>(value)) type = CellValueType::Currency;
                copy.SetValue(value, type);
            }
        }
    };
    copyRow(range.start.row, 0);
    for (size_t i = 0; i < rows.size(); ++i) copyRow(rows[i], static_cast<int>(i) + 1);
    for (int c = 0; c < range.ColCount(); ++c) details->GetOrCreateCell(0, c).SetBold(true);
    for (int c = range.start.col; c <= range.end.col; ++c) {
        details->SetColumnWidth(c - range.start.col, source->GetColumnWidth(c));
    }
    details->NotifyCellsChanged(CellRange(0, 0, static_cast<int>(rows.size()), range.ColCount() - 1));
    SetActiveSheet(at);
    SelectCell(0, 0);
    Invalidate();
    return at;
}

bool UltraCanvasSpreadsheet::HandlePivotDoubleClick(int row, int col) {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    const int index = sheet ? sheet->PivotTableAt(row, col) : -1;
    if (index < 0) return false;
    const SpreadsheetPivotTable& pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    const SpreadsheetSheet* source = GetSheetByName(pivot.sourceSheet);
    const bool valueCell = source && pivot.unsupportedReason.empty() &&
                           !SpreadsheetPivotDrillDownRows(pivot, *source, row, col).empty();
    if (valueCell) {
        if (onPivotDrillDown) onPivotDrillDown(row, col);
        else DrillDownPivotCell(row, col);
    } else {
        ShowPivotTableDialog(index);
    }
    return true;
}

void UltraCanvasSpreadsheet::ShowInsertPivotTableDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    SpreadsheetPivotTable pivot = MakeSpreadsheetPivot(*sheet, GetSelection());
    pivot.name = NextSpreadsheetPivotName(*this);
    if (pivot.sourceRange.RowCount() < 2) {
        UltraCanvasDialogManager::ShowInformation(
            Tr("Select the data for the pivot table: a block of cells whose first row names its columns."),
            Tr("Pivot table"), nullptr, GetWindow());
        return;
    }
    // Next to the data, a column apart, when it stays on this sheet.
    pivot.target = CellAddress(pivot.sourceRange.start.row, pivot.sourceRange.end.col + 2);
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    ShowSpreadsheetPivotDialog(*this, pivot, true, [weakSelf](const SpreadsheetPivotTable& chosen, bool newSheet) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        std::string problem;
        if (self->InsertPivotTable(chosen, newSheet ? -1 : self->GetActiveSheetIndex(), &problem) < 0) {
            UltraCanvasDialogManager::ShowError(problem.empty() ? Tr("The pivot table could not be written.") : problem,
                                                Tr("Pivot table"), nullptr, self->GetWindow());
        }
    }, GetWindow());
}

void UltraCanvasSpreadsheet::ShowPivotTableDialog(int index) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return;
    if (IsEditing()) StopEditing(true);
    const SpreadsheetPivotTable& pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    if (!pivot.unsupportedReason.empty()) {
        UltraCanvasDialogManager::ShowInformation(
            TrF("This pivot table was made with something this program cannot show, so its cells stay as the file has "
                "them: {0}", pivot.unsupportedReason),
            Tr("Pivot table"), nullptr, GetWindow());
        return;
    }
    const int sheetIndex = GetActiveSheetIndex();
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    ShowSpreadsheetPivotDialog(*this, pivot, false, [weakSelf, sheetIndex, index](const SpreadsheetPivotTable& chosen, bool) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        std::string problem;
        if (!self->SetPivotTable(sheetIndex, index, chosen, &problem)) {
            UltraCanvasDialogManager::ShowError(problem.empty() ? Tr("The pivot table could not be written.") : problem,
                                                Tr("Pivot table"), nullptr, self->GetWindow());
        }
    }, GetWindow());
}

void UltraCanvasSpreadsheet::ShowPivotSourceDialog(int index) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet || index < 0 || index >= static_cast<int>(sheet->GetPivotTables().size())) return;
    const SpreadsheetPivotTable& pivot = sheet->GetPivotTables()[static_cast<size_t>(index)];
    const std::string fallback = pivot.sourceSheet;
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowInputDialog(
        Tr("The cells the pivot table summarises, their first row naming the fields (such as Sheet1!A1:D40):"),
        Tr("Change source"), SourceText(pivot), InputType::Text,
        [weakSelf, index, fallback](DialogResult result, const std::string& text) {
            auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
            if (!self || result != DialogResult::OK) return;
            std::string sheetName;
            CellRange range;
            std::string problem;
            if (!ParseSourceText(text, fallback, sheetName, range)) {
                problem = Tr("That is not a block of cells.");
            } else if (range.RowCount() < 2) {
                problem = Tr("The source needs a row of field names and at least one row of data.");
            } else {
                self->SetPivotTableSource(index, sheetName, range, &problem);
            }
            if (!problem.empty()) UltraCanvasDialogManager::ShowError(problem, Tr("Change source"), nullptr, self->GetWindow());
        },
        GetWindow());
}

// ============================================================================
// THE FIELD LIST PANEL
// ============================================================================

namespace {

constexpr float kRow = 28.0f;
constexpr float kGap = 6.0f;

std::shared_ptr<UltraCanvasContainer> Box(const std::string& id, CSSLayout::FlexDirection direction, float gap) {
    auto box = std::make_shared<UltraCanvasContainer>(id);
    box->layout.SetFlex(direction);
    box->layout.SetFlexGap(gap);
    DisableScrollbars(box);
    return box;
}

// A child of a column: its own height, the column's width.
template <typename T>
std::shared_ptr<T> Fixed(std::shared_ptr<T> element, float height) {
    element->size.height = CSSLayout::Dimension::Px(height);
    element->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return element;
}

// A child of a row: an equal share of its width.
template <typename T>
std::shared_ptr<T> Share(std::shared_ptr<T> element) {
    element->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return element;
}

std::shared_ptr<UltraCanvasLabel> Heading(const std::string& id, const std::string& text) {
    auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20, text);
    label->SetFontWeight(FontWeight::Bold);
    return Fixed(label, 20.0f);
}

std::shared_ptr<UltraCanvasButton> SmallButton(const std::string& id, const std::string& text, const std::string& tip) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, kRow, text);
    button->SetTooltip(tip);
    return button;
}

const PivotSummary kSummaries[] = {PivotSummary::Sum,     PivotSummary::Count,   PivotSummary::Average,
                                   PivotSummary::Max,     PivotSummary::Min,     PivotSummary::Product,
                                   PivotSummary::CountNumbers, PivotSummary::StdDev, PivotSummary::StdDevP,
                                   PivotSummary::Var,     PivotSummary::VarP};
const PivotShowAs kShowAs[] = {PivotShowAs::Value, PivotShowAs::PercentOfTotal, PivotShowAs::PercentOfRow,
                               PivotShowAs::PercentOfColumn};
const PivotGroupBy kGroupings[] = {PivotGroupBy::NoGrouping, PivotGroupBy::Years, PivotGroupBy::Quarters,
                                   PivotGroupBy::Months, PivotGroupBy::NumberRange};

template <typename T, size_t N>
int IndexIn(const T (&list)[N], T value) {
    for (size_t i = 0; i < N; ++i) if (list[i] == value) return static_cast<int>(i);
    return 0;
}

} // namespace

UltraCanvasSpreadsheetPivotPanel::UltraCanvasSpreadsheetPivotPanel(const std::string& identifier, float x, float y,
                                                                   float w, float h, bool wide)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    Build(wide);
}

void UltraCanvasSpreadsheetPivotPanel::Build(bool wide) {
    layout.SetFlex(wide ? CSSLayout::FlexDirection::Row : CSSLayout::FlexDirection::Column);
    layout.SetFlexGap(12.0f);
    {
        // Laid out in a dialog it never scrolls; beside the grid it does.
        ContainerStyle style = GetContainerStyle();
        style.autoShowScrollbars = !wide;
        style.autoShowVerticalScrollbar = !wide;
        style.autoShowHorizontalScrollbar = false;
        style.forceShowHorizontalScrollbar = false;
        style.forceShowVerticalScrollbar = false;
        SetContainerStyle(style);
    }
    auto left = Box(GetIdentifier() + "-left", CSSLayout::FlexDirection::Column, kGap);
    auto right = Box(GetIdentifier() + "-right", CSSLayout::FlexDirection::Column, kGap);
    if (wide) {
        Share(left);
        Share(right);
    } else {
        left->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        right->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    // The source's fields, and the buttons that add the picked one.
    left->AddChild(Heading(GetIdentifier() + "-fields-title", Tr("Fields")));
    fieldsModel_ = std::make_shared<UltraCanvasSimpleListModel>();
    auto fields = Fixed(std::make_shared<UltraCanvasListView>(GetIdentifier() + "-fields", 0, 0, 0, 160), wide ? 190.0f : 150.0f);
    fields->SetModel(fieldsModel_);
    fields->SetRowHeight(22);
    fields_ = fields.get();
    fields->onItemDoubleClicked = [this](int row) {
        if (!source_) return;
        const PivotValueField made = MakePivotValueField(*source_, pivot_.sourceRange, row);
        // Numbers are summed; anything else goes down the side.
        AddField(row, made.summary == PivotSummary::Sum ? Area::Values : Area::Rows);
    };
    left->AddChild(fields);
    // In a row in the dialog; two by two in the panel, which may be narrow.
    auto addRow = Fixed(Box(GetIdentifier() + "-add", CSSLayout::FlexDirection::Row, 4.0f), kRow);
    auto addRowTwo = wide ? addRow : Fixed(Box(GetIdentifier() + "-add2", CSSLayout::FlexDirection::Row, 4.0f), kRow);
    const std::pair<Area, std::string> adds[] = {{Area::Rows, Tr("Rows")}, {Area::Columns, Tr("Columns")},
                                                 {Area::Values, Tr("Values")}, {Area::Filters, Tr("Filters")}};
    const char* addIds[] = {"rows", "columns", "values", "filters"};
    for (size_t i = 0; i < 4; ++i) {
        auto button = Share(SmallButton(GetIdentifier() + "-add-" + addIds[i], adds[i].second,
                                        TrF("Add the field picked above to: {0}", adds[i].second)));
        const Area area = adds[i].first;
        button->onClick = [this, area]() {
            const int field = SelectedSourceField();
            if (field >= 0) AddField(field, area);
        };
        (i < 2 ? addRow : addRowTwo)->AddChild(button);
    }
    left->AddChild(addRow);
    if (!wide) left->AddChild(addRowTwo);

    // The picked field's items: a tick for each one shown.
    left->AddChild(Heading(GetIdentifier() + "-items-title", Tr("Items shown")));
    auto items = Fixed(CreateScrollableContainer(GetIdentifier() + "-items", 0, 0, 0, 140), wide ? 150.0f : 120.0f);
    items->layout.SetFlex(CSSLayout::FlexDirection::Column);
    items->layout.SetFlexGap(2.0f);
    items_ = items.get();
    left->AddChild(items);

    // The four areas, two by two.
    const std::string areaNames[] = {Tr("Rows"), Tr("Columns"), Tr("Values"), Tr("Filters")};
    const Area layoutOrder[2][2] = {{Area::Filters, Area::Columns}, {Area::Rows, Area::Values}};
    for (int r = 0; r < 2; ++r) {
        auto pair = Fixed(Box(GetIdentifier() + "-areas" + std::to_string(r), CSSLayout::FlexDirection::Row, 8.0f), 106.0f);
        for (int c = 0; c < 2; ++c) {
            const Area area = layoutOrder[r][c];
            const int a = static_cast<int>(area);
            auto column = Share(Box(GetIdentifier() + "-area" + std::to_string(a), CSSLayout::FlexDirection::Column, 2.0f));
            column->AddChild(Heading(GetIdentifier() + "-area-title" + std::to_string(a), areaNames[a]));
            areaModels_[a] = std::make_shared<UltraCanvasSimpleListModel>();
            auto list = Fixed(std::make_shared<UltraCanvasListView>(GetIdentifier() + "-area-list" + std::to_string(a), 0, 0, 0, 84), 84.0f);
            list->SetModel(areaModels_[a]);
            list->SetRowHeight(20);
            list->onItemClicked = [this, area](int row) { PickField(area, row); };
            areas_[a] = list.get();
            column->AddChild(list);
            pair->AddChild(column);
        }
        right->AddChild(pair);
    }
    auto moveRow = Fixed(Box(GetIdentifier() + "-move", CSSLayout::FlexDirection::Row, 4.0f), kRow);
    auto remove = Share(SmallButton(GetIdentifier() + "-remove", Tr("Remove"), Tr("Take the picked field out of the pivot table")));
    remove->onClick = [this]() { RemovePicked(); };
    auto up = Share(SmallButton(GetIdentifier() + "-up", Tr("Move up"), Tr("Move the picked field before the one above it")));
    up->onClick = [this]() { MovePicked(-1); };
    auto down = Share(SmallButton(GetIdentifier() + "-down", Tr("Move down"), Tr("Move the picked field after the one below it")));
    down->onClick = [this]() { MovePicked(1); };
    // In a narrow panel each as wide as its name, the room left shared out.
    if (!wide)
        for (auto* button : {remove.get(), up.get(), down.get()}) {
            button->size.width = CSSLayout::Dimension::Auto();
            button->SetPadding(0, 4, 0, 4);
            button->layoutItem.SetFlexBasis(CSSLayout::Dimension::Auto());
        }
    moveRow->AddChild(remove);
    moveRow->AddChild(up);
    moveRow->AddChild(down);
    right->AddChild(moveRow);

    // The picked field's settings.
    auto picked = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "-picked", 0, 0, 0, 20, "");
    picked->SetFontWeight(FontWeight::Bold);
    pickedLabel_ = picked.get();
    right->AddChild(Fixed(picked, 20.0f));
    auto form = CreateFormGrid(GetIdentifier() + "-settings", kGap, 10.0f);
    form->size.height = CSSLayout::Dimension::Px(5 * kRow + 4 * kGap);
    auto makeChoice = [this](const std::string& id, const std::vector<std::string>& names) {
        auto list = std::make_shared<UltraCanvasDropdown>(GetIdentifier() + "-" + id, 0, 0, 0, kRow);
        for (const std::string& name : names) list->AddItem(name);
        list->SetSelectedIndex(0, false);
        return list;
    };
    std::vector<std::string> summaryNames, showAsNames, groupNames;
    for (PivotSummary s : kSummaries) summaryNames.push_back(PivotSummaryName(s));
    for (PivotShowAs s : kShowAs) showAsNames.push_back(PivotShowAsName(s));
    for (PivotGroupBy g : kGroupings) groupNames.push_back(PivotGroupByName(g));
    auto summary = makeChoice("summary", summaryNames);
    auto showAs = makeChoice("show-as", showAsNames);
    auto order = makeChoice("order", {Tr("A to Z, smallest first"), Tr("Z to A, largest first")});
    auto grouping = makeChoice("grouping", groupNames);
    auto groupRow = Box(GetIdentifier() + "-group-row", CSSLayout::FlexDirection::Row, 6.0f);
    groupRow->size.height = CSSLayout::Dimension::Px(kRow);
    Share(grouping);
    auto step = std::make_shared<UltraCanvasTextInput>(GetIdentifier() + "-step", 0, 0, 70, kRow);
    step->size.width = CSSLayout::Dimension::Px(70.0f);
    step->layoutItem.SetFlexShrink(0);
    step->SetTooltip(Tr("The size of each number range"));
    groupRow->AddChild(grouping);
    groupRow->AddChild(step);
    auto subtotals = UltraCanvasCheckbox::CreateCheckbox(GetIdentifier() + "-subtotals", 0, 0, -1, kRow,
                                                         Tr("A subtotal after each item"), true);
    subtotals->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    summary_ = summary.get();
    showAs_ = showAs.get();
    order_ = order.get();
    grouping_ = grouping.get();
    step_ = step.get();
    subtotals_ = subtotals.get();
    AddFormRow(form, GetIdentifier() + "-summary", Tr("Summarise by") + ":", summary);
    AddFormRow(form, GetIdentifier() + "-show-as", Tr("Show as") + ":", showAs);
    AddFormRow(form, GetIdentifier() + "-order", Tr("Sort order") + ":", order);
    AddFormRow(form, GetIdentifier() + "-grouping", Tr("Group") + ":", groupRow);
    AddFormWideRow(form, subtotals);
    right->AddChild(form);

    summary->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (updating_) return;
        if (PivotValueField* v = PickedValueField()) {
            v->summary = kSummaries[std::clamp(index, 0, 10)];
            v->name.clear();
            Changed();
        }
    };
    showAs->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (updating_) return;
        if (PivotValueField* v = PickedValueField()) {
            v->showAs = kShowAs[std::clamp(index, 0, 3)];
            Changed();
        }
    };
    order->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (updating_) return;
        if (PivotField* f = PickedAxisField()) {
            f->descending = index == 1;
            Changed();
        }
    };
    grouping->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (updating_) return;
        if (PivotField* f = PickedAxisField()) {
            f->grouping.by = kGroupings[std::clamp(index, 0, 4)];
            f->hiddenItems.clear();   // the items are other ones now
            Changed();
        }
    };
    step->onTextChanged = [this](const std::string& text) {
        if (updating_) return;
        PivotField* f = PickedAxisField();
        double value = 0.0;
        // locale-ok: the reader types the step in a field of their own; a dot or a comma both read
        std::string dotted = text;
        std::replace(dotted.begin(), dotted.end(), ',', '.');
        if (f && TryParseFloat(dotted, value) && value > 0.0) {
            f->grouping.step = value;
            if (f->grouping.by == PivotGroupBy::NumberRange) Changed();
        }
    };
    subtotals->onStateChanged = [this](CheckedState, CheckedState now) {
        if (updating_) return;
        if (PivotField* f = PickedAxisField()) {
            f->subtotals = now == CheckedState::Checked;
            Changed();
        }
    };

    // The pivot's own options.
    right->AddChild(Heading(GetIdentifier() + "-options-title", Tr("Totals")));
    auto option = [this, &right](const std::string& id, const std::string& text, bool on) {
        auto box = UltraCanvasCheckbox::CreateCheckbox(GetIdentifier() + "-" + id, 0, 0, -1, 24, text, on);
        right->AddChild(Fixed(box, 24.0f));
        return box.get();
    };
    grandRow_ = option("grand-row", Tr("A grand total row at the bottom"), true);
    grandColumn_ = option("grand-column", Tr("A grand total column at the right"), true);
    valuesInRows_ = option("values-in-rows", Tr("Several values one under the other"), false);
    grandRow_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (updating_) return;
        pivot_.grandTotalRow = now == CheckedState::Checked;
        Changed();
    };
    grandColumn_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (updating_) return;
        pivot_.grandTotalColumn = now == CheckedState::Checked;
        Changed();
    };
    valuesInRows_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (updating_) return;
        pivot_.valuesInRows = now == CheckedState::Checked;
        Changed();
    };

    AddChild(left);
    AddChild(right);
}

void UltraCanvasSpreadsheetPivotPanel::SetPivot(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet* source) {
    pivot_ = pivot;
    source_ = source;
    if (source_ && pivot_.fieldNames.empty()) pivot_.fieldNames = SpreadsheetPivotFieldNames(*source_, pivot_.sourceRange);
    if (pickedIndex_ >= AreaCount(pickedArea_)) pickedIndex_ = -1;
    Refresh();
}

void UltraCanvasSpreadsheetPivotPanel::SetSource(const SpreadsheetSheet* source, const CellRange& range,
                                                 const std::string& sheetName) {
    source_ = source;
    pivot_.sourceSheet = sheetName;
    pivot_.sourceRange = range;
    pivot_.fieldNames = source ? SpreadsheetPivotFieldNames(*source, range) : std::vector<std::string>();
    // Fields beyond the new source's columns go.
    const int columns = range.ColCount();
    for (auto* list : {&pivot_.rowFields, &pivot_.columnFields, &pivot_.filterFields}) {
        list->erase(std::remove_if(list->begin(), list->end(), [columns](const PivotField& f) { return f.column >= columns; }),
                    list->end());
    }
    pivot_.valueFields.erase(std::remove_if(pivot_.valueFields.begin(), pivot_.valueFields.end(),
                                            [columns](const PivotValueField& f) { return f.column >= columns; }),
                             pivot_.valueFields.end());
    pickedIndex_ = -1;
    Changed();
}

std::vector<PivotField>* UltraCanvasSpreadsheetPivotPanel::AxisList(Area area) {
    switch (area) {
        case Area::Rows: return &pivot_.rowFields;
        case Area::Columns: return &pivot_.columnFields;
        case Area::Filters: return &pivot_.filterFields;
        case Area::Values: return nullptr;
    }
    return nullptr;
}

int UltraCanvasSpreadsheetPivotPanel::AreaCount(Area area) const {
    switch (area) {
        case Area::Rows: return static_cast<int>(pivot_.rowFields.size());
        case Area::Columns: return static_cast<int>(pivot_.columnFields.size());
        case Area::Filters: return static_cast<int>(pivot_.filterFields.size());
        case Area::Values: return static_cast<int>(pivot_.valueFields.size());
    }
    return 0;
}

PivotField* UltraCanvasSpreadsheetPivotPanel::PickedAxisField() {
    std::vector<PivotField>* list = AxisList(pickedArea_);
    if (!list || pickedIndex_ < 0 || pickedIndex_ >= static_cast<int>(list->size())) return nullptr;
    return &(*list)[static_cast<size_t>(pickedIndex_)];
}

PivotValueField* UltraCanvasSpreadsheetPivotPanel::PickedValueField() {
    if (pickedArea_ != Area::Values || pickedIndex_ < 0 || pickedIndex_ >= static_cast<int>(pivot_.valueFields.size())) {
        return nullptr;
    }
    return &pivot_.valueFields[static_cast<size_t>(pickedIndex_)];
}

int UltraCanvasSpreadsheetPivotPanel::SelectedSourceField() const {
    if (!fields_ || !fields_->GetSelection()) return -1;
    const int row = fields_->GetSelection()->GetCurrentRow();
    return row >= 0 && row < static_cast<int>(pivot_.fieldNames.size()) ? row : -1;
}

void UltraCanvasSpreadsheetPivotPanel::AddField(int column, Area area) {
    if (column < 0 || column >= static_cast<int>(pivot_.fieldNames.size())) return;
    if (area == Area::Values) {
        pivot_.valueFields.push_back(source_ ? MakePivotValueField(*source_, pivot_.sourceRange, column)
                                             : PivotValueField{column});
    } else {
        // A field stands once on the side, the top or the filters: added
        // again, it moves.
        for (auto* list : {&pivot_.rowFields, &pivot_.columnFields, &pivot_.filterFields}) {
            list->erase(std::remove_if(list->begin(), list->end(), [column](const PivotField& f) { return f.column == column; }),
                        list->end());
        }
        PivotField field;
        field.column = column;
        if (area == Area::Filters) field.subtotals = false;
        AxisList(area)->push_back(field);
    }
    pickedArea_ = area;
    pickedIndex_ = AreaCount(area) - 1;
    Changed();
}

void UltraCanvasSpreadsheetPivotPanel::PickField(Area area, int index) {
    pickedArea_ = area;
    pickedIndex_ = index >= 0 && index < AreaCount(area) ? index : -1;
    RefreshPicked();
}

void UltraCanvasSpreadsheetPivotPanel::RemovePicked() {
    if (pickedIndex_ < 0 || pickedIndex_ >= AreaCount(pickedArea_)) return;
    if (pickedArea_ == Area::Values) pivot_.valueFields.erase(pivot_.valueFields.begin() + pickedIndex_);
    else AxisList(pickedArea_)->erase(AxisList(pickedArea_)->begin() + pickedIndex_);
    pickedIndex_ = std::min(pickedIndex_, AreaCount(pickedArea_) - 1);
    Changed();
}

void UltraCanvasSpreadsheetPivotPanel::MovePicked(int by) {
    const int to = pickedIndex_ + by;
    if (pickedIndex_ < 0 || to < 0 || to >= AreaCount(pickedArea_)) return;
    if (pickedArea_ == Area::Values) std::swap(pivot_.valueFields[static_cast<size_t>(pickedIndex_)], pivot_.valueFields[static_cast<size_t>(to)]);
    else std::swap((*AxisList(pickedArea_))[static_cast<size_t>(pickedIndex_)], (*AxisList(pickedArea_))[static_cast<size_t>(to)]);
    pickedIndex_ = to;
    Changed();
}

void UltraCanvasSpreadsheetPivotPanel::Changed() {
    Refresh();
    if (onPivotChanged) onPivotChanged(pivot_);
}

void UltraCanvasSpreadsheetPivotPanel::Refresh() {
    updating_ = true;
    std::vector<ListItem> names;
    for (const std::string& name : pivot_.fieldNames) names.emplace_back(name);
    const int keep = SelectedSourceField();
    fieldsModel_->SetItems(names);
    if (keep >= 0 && keep < static_cast<int>(names.size()) && fields_->GetSelection()) fields_->GetSelection()->Select(keep);
    auto nameOf = [this](int column) {
        return column >= 0 && column < static_cast<int>(pivot_.fieldNames.size()) ? pivot_.fieldNames[static_cast<size_t>(column)]
                                                                                   : std::string("?");
    };
    for (int a = 0; a < 4; ++a) {
        std::vector<ListItem> rows;
        const Area area = static_cast<Area>(a);
        if (area == Area::Values) {
            for (const PivotValueField& v : pivot_.valueFields) rows.emplace_back(PivotValueFieldCaption(v, pivot_.fieldNames));
        } else {
            for (const PivotField& f : *AxisList(area)) rows.emplace_back(nameOf(f.column));
        }
        areaModels_[a]->SetItems(rows);
        if (areas_[a]->GetSelection()) {
            areas_[a]->GetSelection()->Clear();
            if (area == pickedArea_ && pickedIndex_ >= 0 && pickedIndex_ < static_cast<int>(rows.size())) {
                areas_[a]->GetSelection()->Select(pickedIndex_);
            }
        }
    }
    grandRow_->SetChecked(pivot_.grandTotalRow);
    grandColumn_->SetChecked(pivot_.grandTotalColumn);
    valuesInRows_->SetChecked(pivot_.valuesInRows);
    updating_ = false;
    RefreshPicked();
}

void UltraCanvasSpreadsheetPivotPanel::RefreshPicked() {
    updating_ = true;
    PivotField* axis = PickedAxisField();
    PivotValueField* value = PickedValueField();
    for (int a = 0; a < 4; ++a) {
        if (static_cast<Area>(a) != pickedArea_ && areas_[a]->GetSelection()) areas_[a]->GetSelection()->Clear();
    }
    auto nameOf = [this](int column) {
        return column >= 0 && column < static_cast<int>(pivot_.fieldNames.size()) ? pivot_.fieldNames[static_cast<size_t>(column)]
                                                                                   : std::string();
    };
    if (value) pickedLabel_->SetText(PivotValueFieldCaption(*value, pivot_.fieldNames));
    else if (axis) pickedLabel_->SetText(nameOf(axis->column));
    else pickedLabel_->SetText(Tr("Pick a field in an area to set it"));
    summary_->SetDisabled(!value);
    showAs_->SetDisabled(!value);
    order_->SetDisabled(!axis);
    grouping_->SetDisabled(!axis);
    step_->SetDisabled(!axis);
    subtotals_->SetDisabled(!axis || pickedArea_ == Area::Filters);
    if (value) {
        summary_->SetSelectedIndex(IndexIn(kSummaries, value->summary), false);
        showAs_->SetSelectedIndex(IndexIn(kShowAs, value->showAs), false);
    }
    if (axis) {
        order_->SetSelectedIndex(axis->descending ? 1 : 0, false);
        grouping_->SetSelectedIndex(IndexIn(kGroupings, axis->grouping.by), false);
        step_->SetText(FormatFloatClassic(axis->grouping.step, 10));
        subtotals_->SetChecked(axis->subtotals);
    }
    // The items, ticked where shown.
    items_->ClearChildren();
    if (axis && source_) {
        const std::vector<PivotItem> list = SpreadsheetPivotFieldItems(*source_, pivot_.sourceRange, *axis);
        int n = 0;
        for (const PivotItem& item : list) {
            if (n >= 500) break;   // a field of thousands of items is filtered in its source
            const bool shown = std::find(axis->hiddenItems.begin(), axis->hiddenItems.end(), item.key) == axis->hiddenItems.end();
            auto box = UltraCanvasCheckbox::CreateCheckbox(GetIdentifier() + "-item" + std::to_string(n++), 0, 0, -1, 22,
                                                           item.label, shown);
            box->size.height = CSSLayout::Dimension::Px(22.0f);
            box->layoutItem.SetFlexShrink(0);
            const std::string key = item.key;
            box->onStateChanged = [this, key](CheckedState, CheckedState now) {
                if (updating_) return;
                PivotField* f = PickedAxisField();
                if (!f) return;
                auto& hidden = f->hiddenItems;
                hidden.erase(std::remove(hidden.begin(), hidden.end(), key), hidden.end());
                if (now != CheckedState::Checked) hidden.push_back(key);
                if (onPivotChanged) onPivotChanged(pivot_);
            };
            items_->AddChild(box);
        }
    }
    updating_ = false;
}

// ============================================================================
// THE DIALOG
// ============================================================================

void ShowSpreadsheetPivotDialog(const UltraCanvasSpreadsheet& workbook, const SpreadsheetPivotTable& pivot, bool isNew,
                                std::function<void(const SpreadsheetPivotTable&, bool newSheet)> onApply,
                                UltraCanvasWindowBase* parent) {
    const int width = 780;
    const int panelHeight = 600;
    const int topHeight = isNew ? static_cast<int>(2 * kRow + kGap) + 10 : 0;
    DialogConfig config;
    config.title = isNew ? Tr("Insert pivot table") : Tr("Pivot table fields");
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::OKCancel;
    config.defaultButton = DialogButton::OK;
    config.width = width;
    config.height = panelHeight + topHeight + 120;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetIconVisible(false);
    dialog->SetMessageVisible(false);

    auto content = std::make_shared<UltraCanvasContainer>("PivotDialogContent");
    content->layout.SetFlex(CSSLayout::FlexDirection::Column);
    content->layout.SetFlexGap(10.0f);
    content->size.width = CSSLayout::Dimension::Px(static_cast<float>(width - 48));
    content->size.height = CSSLayout::Dimension::Px(static_cast<float>(panelHeight + topHeight));
    DisableScrollbars(content);

    struct State {
        SpreadsheetPivotTable pivot;
        std::string sourceText;
        std::string cellText;
        bool newSheet = true;
    };
    auto state = std::make_shared<State>();
    state->pivot = pivot;
    state->sourceText = SourceText(pivot);
    state->cellText = pivot.target.ToString();

    auto panel = std::make_shared<UltraCanvasSpreadsheetPivotPanel>("PivotDialogPanel", 0, 0,
                                                                     static_cast<float>(width - 48),
                                                                     static_cast<float>(panelHeight), true);
    panel->size.height = CSSLayout::Dimension::Px(static_cast<float>(panelHeight));
    panel->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    panel->SetPivot(pivot, workbook.GetSheetByName(pivot.sourceSheet));
    panel->onPivotChanged = [state](const SpreadsheetPivotTable& changed) {
        // The dialog's own fields (source, place) stay the dialog's.
        const std::string sheet = state->pivot.sourceSheet;
        const CellRange range = state->pivot.sourceRange;
        state->pivot = changed;
        state->pivot.sourceSheet = sheet;
        state->pivot.sourceRange = range;
    };

    if (isNew) {
        auto form = CreateFormGrid("PivotDialogPlace", kGap, 10.0f);
        form->size.height = CSSLayout::Dimension::Px(2 * kRow + kGap);
        auto source = std::make_shared<UltraCanvasTextInput>("PivotDialogSource", 0, 0, 0, kRow);
        source->SetText(state->sourceText);
        const UltraCanvasSpreadsheet* book = &workbook;
        UltraCanvasSpreadsheetPivotPanel* panelRaw = panel.get();
        source->onTextChanged = [state, book, panelRaw](const std::string& text) {
            state->sourceText = text;
            std::string sheet;
            CellRange range;
            if (!ParseSourceText(text, state->pivot.sourceSheet, sheet, range) || range.RowCount() < 2) return;
            const SpreadsheetSheet* found = book->GetSheetByName(sheet);
            if (!found) return;
            state->pivot.sourceSheet = found->GetName();
            state->pivot.sourceRange = range;
            panelRaw->SetSource(found, range, found->GetName());
        };
        auto placeRow = Box("PivotDialogPlaceRow", CSSLayout::FlexDirection::Row, 8.0f);
        placeRow->size.height = CSSLayout::Dimension::Px(kRow);
        auto place = std::make_shared<UltraCanvasDropdown>("PivotDialogPlaceChoice", 0, 0, 0, kRow);
        place->AddItem(Tr("A new sheet"));
        place->AddItem(Tr("This sheet, from the cell"));
        place->SetSelectedIndex(0, false);
        Share(place);
        auto cell = std::make_shared<UltraCanvasTextInput>("PivotDialogCell", 0, 0, 90, kRow);
        cell->size.width = CSSLayout::Dimension::Px(90.0f);
        cell->layoutItem.SetFlexShrink(0);
        cell->SetText(state->cellText);
        cell->SetDisabled(true);
        UltraCanvasTextInput* cellRaw = cell.get();
        place->onSelectionChanged = [state, cellRaw](int index, const DropdownItem&) {
            state->newSheet = index == 0;
            cellRaw->SetDisabled(state->newSheet);
        };
        cell->onTextChanged = [state](const std::string& text) { state->cellText = text; };
        placeRow->AddChild(place);
        placeRow->AddChild(cell);
        AddFormRow(form, "PivotDialogSource", Tr("Data") + ":", source);
        AddFormRow(form, "PivotDialogPlace", Tr("Put it on") + ":", placeRow);
        content->AddChild(form);
    }
    content->AddChild(panel);
    dialog->AddDialogElement(content);
    if (isNew) {
        if (auto form = std::dynamic_pointer_cast<UltraCanvasContainer>(content->GetChildren().front())) HoldFormCaptionColumn(form);
    }

    UltraCanvasDialogManager::ShowDialog(dialog, [state, onApply, isNew](DialogResult result) {
        if (result != DialogResult::OK || !onApply) return;
        SpreadsheetPivotTable chosen = state->pivot;
        if (isNew && !state->newSheet) {
            std::string plain = state->cellText;
            plain.erase(std::remove(plain.begin(), plain.end(), '$'), plain.end());
            const CellRange at = CellRange::FromString(plain);
            if (at.IsValid()) chosen.target = CellAddress(at.start.row, at.start.col);
        }
        onApply(chosen, isNew && state->newSheet);
    }, parent);
}

} // namespace UltraCanvas
