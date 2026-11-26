// core/UltraCanvasSpreadsheetSheet.cpp
// Spreadsheet worksheet implementation (non-inline members)
// Version: 1.6.0 - conditional formats evaluated for drawing (GetConditionalFormatting: formula rules,
//                  colour scales, data bars, top / average / duplicates); Custom validation rules and
//                  lists taken from cells
// Version: 1.5.0 - charts: cloned with the sheet, moved with inserted / deleted rows and columns
// Version: 1.4.0 - PasteCells pastes formulas with their references moved from where they were copied; a values paste of a formula cell is its result
// Version: 1.3.0 - inserting / deleting rows and columns rewrites the formulas' references
// Version: 1.2.0 - GetEffectiveValidation, DataValidationListItems
// Version: 1.1.0 - data validation: one checker (CheckDataValidation) for range and cell rules; text is
//                  not a number, unreadable bounds fail, Date/Time bounds as YYYY-MM-DD / HH:MM
// Version: 1.0.0
// Last Modified: 2026-05-30
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetFormula.h"      // conditions and Custom rules are formulas
#include "UltraCanvasSpreadsheetMetrics.h"      // SpreadsheetUtf8Length
#include "UltraCanvasTextUtils.h"               // ParseFloatClassic
#include "UltraLocalisation/UltraLocalisation.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdio>
#include <optional>
#include <set>
#include <variant>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// COLUMN / ROW AUTO-FIT
// ============================================================================

void SpreadsheetSheet::AutoFitColumnWidth(int col, const CellTextMeasureFn& measure) {
    int contentWidth = 0;
    int scanned = 0;

    for (const auto& [coord, cellPtr] : cells_) {
        if (coord.second != col || !cellPtr || cellPtr->IsEmpty()) continue;
        if (++scanned > SpreadsheetAutoFit::MaxScanRows) break;

        const std::string text = cellPtr->GetDisplayValue();
        if (text.empty()) continue;
        const CellFont& font = cellPtr->GetStyle().font;
        // Real glyph advances when the UI handed us a measurer, an em-based
        // estimate otherwise - never the byte count, which sizes every column
        // holding "€" or an accent far too wide.
        int width = measure ? measure(text, font)
                            : SpreadsheetEstimateTextWidth(text, font);
        // An indent moves the text right inside the cell, so it costs width too.
        width += cellPtr->GetStyle().indent * 8;
        contentWidth = std::max(contentWidth, width);
    }

    // A column with nothing in it keeps the sheet default rather than shrinking
    // to the auto-fit minimum, so blank columns stay uniform.
    SetColumnWidth(col,
                   contentWidth > 0 ? SpreadsheetClampAutoFitWidth(contentWidth) : defaultColumnWidth_,
                   /*explicitWidth*/ false);
}

void SpreadsheetSheet::AutoFitColumnWidthRange(int startCol, int endCol, const CellTextMeasureFn& measure) {
    for (int col = startCol; col <= endCol; ++col) {
        AutoFitColumnWidth(col, measure);
    }
}

void SpreadsheetSheet::AutoFitUnsizedColumns(const CellTextMeasureFn& measure) {
    if (cells_.empty()) return;

    // One pass over the sparse cell map rather than one pass per column: a
    // freshly imported sheet can be thousands of columns wide, and re-walking
    // every cell for each of them turns a paste-sized import into a stall.
    std::unordered_map<int, int> widestByColumn;
    std::unordered_map<int, int> scannedByColumn;
    for (const auto& [coord, cellPtr] : cells_) {
        const int col = coord.second;
        if (!cellPtr || cellPtr->IsEmpty()) continue;
        if (HasExplicitColumnWidth(col)) continue;   // the document or the user chose this one
        if (++scannedByColumn[col] > SpreadsheetAutoFit::MaxScanRows) continue;

        const std::string text = cellPtr->GetDisplayValue();
        if (text.empty()) continue;
        const CellStyle& style = cellPtr->GetStyle();
        int width = measure ? measure(text, style.font)
                            : SpreadsheetEstimateTextWidth(text, style.font);
        width += style.indent * 8;

        int& widest = widestByColumn[col];
        widest = std::max(widest, width);
    }

    for (const auto& [col, widest] : widestByColumn) {
        if (widest <= 0) continue;
        SetColumnWidth(col, SpreadsheetClampAutoFitWidth(widest), /*explicitWidth*/ false);
    }
}

void SpreadsheetSheet::AutoFitRowHeight(int row) {
    // Row height follows the tallest font in the row: a 24pt heading in a 20px
    // row is clipped top and bottom, which is what this fixes. Wrapped text
    // would need line-broken measurement, so a wrapped cell only guarantees its
    // first line here.
    float tallest = 0.0f;
    for (const auto& [coord, cellPtr] : cells_) {
        if (coord.first != row || !cellPtr || cellPtr->IsEmpty()) continue;
        tallest = std::max(tallest, cellPtr->GetStyle().font.size);
    }
    if (tallest <= 0.0f) {
        SetRowHeight(row, defaultRowHeight_, /*explicitHeight*/ false);
        return;
    }
    // ~1.25 em line box plus a little breathing room, never below the sheet
    // default so a sheet of ordinary text keeps uniform rows.
    const int fitted = static_cast<int>(tallest * 1.25f + 0.5f) + 4;
    SetRowHeight(row, std::max(fitted, defaultRowHeight_), /*explicitHeight*/ false);
}

void SpreadsheetSheet::AutoFitRowHeightRange(int startRow, int endRow) {
    for (int row = startRow; row <= endRow; ++row) {
        AutoFitRowHeight(row);
    }
}

// ============================================================================
// INSERT / DELETE COLUMNS AND ROWS
// ============================================================================

void SpreadsheetSheet::InsertColumns(int col, int count) {
    if (count <= 0) return;

    // Shift cells: process from highest column down so we never overwrite.
    std::unordered_map<std::pair<int, int>, std::unique_ptr<SpreadsheetCell>, CellCoordHash> newCells;
    for (auto& [coord, cellPtr] : cells_) {
        int newCol = coord.second;
        if (coord.second >= col) {
            newCol += count;
            if (cellPtr) cellPtr->SetPosition(coord.first, newCol);
        }
        newCells.emplace(std::make_pair(coord.first, newCol), std::move(cellPtr));
    }
    cells_ = std::move(newCells);

    // Shift column definitions
    std::map<int, ColumnDefinition> newDefs;
    for (auto& [c, def] : columns_) {
        int nc = (c >= col) ? c + count : c;
        def.index = nc;
        newDefs[nc] = std::move(def);
    }
    columns_ = std::move(newDefs);

    ShiftCellReferences(0, col, 0, count);
    MoveFormulaReferences(name_, true, col, count);
    InvalidateUsedRange();
    NotifyStructureChange();
}

void SpreadsheetSheet::DeleteColumns(int col, int count) {
    if (count <= 0) return;

    std::unordered_map<std::pair<int, int>, std::unique_ptr<SpreadsheetCell>, CellCoordHash> newCells;
    for (auto& [coord, cellPtr] : cells_) {
        if (coord.second >= col && coord.second < col + count) {
            continue;  // Cell removed
        }
        int newCol = coord.second;
        if (coord.second >= col + count) {
            newCol -= count;
            if (cellPtr) cellPtr->SetPosition(coord.first, newCol);
        }
        newCells.emplace(std::make_pair(coord.first, newCol), std::move(cellPtr));
    }
    cells_ = std::move(newCells);

    std::map<int, ColumnDefinition> newDefs;
    for (auto& [c, def] : columns_) {
        if (c >= col && c < col + count) continue;
        int nc = (c >= col + count) ? c - count : c;
        def.index = nc;
        newDefs[nc] = std::move(def);
    }
    columns_ = std::move(newDefs);

    ShiftCellReferences(0, col + count, 0, -count);
    MoveFormulaReferences(name_, true, col, -count);
    InvalidateUsedRange();
    NotifyStructureChange();
}

void SpreadsheetSheet::InsertRows(int row, int count) {
    if (count <= 0) return;

    std::unordered_map<std::pair<int, int>, std::unique_ptr<SpreadsheetCell>, CellCoordHash> newCells;
    for (auto& [coord, cellPtr] : cells_) {
        int newRow = coord.first;
        if (coord.first >= row) {
            newRow += count;
            if (cellPtr) cellPtr->SetPosition(newRow, coord.second);
        }
        newCells.emplace(std::make_pair(newRow, coord.second), std::move(cellPtr));
    }
    cells_ = std::move(newCells);

    std::map<int, RowDefinition> newDefs;
    for (auto& [r, def] : rows_) {
        int nr = (r >= row) ? r + count : r;
        def.index = nr;
        newDefs[nr] = std::move(def);
    }
    rows_ = std::move(newDefs);

    ShiftCellReferences(row, 0, count, 0);
    MoveFormulaReferences(name_, false, row, count);
    InvalidateUsedRange();
    NotifyStructureChange();
}

void SpreadsheetSheet::DeleteRows(int row, int count) {
    if (count <= 0) return;

    std::unordered_map<std::pair<int, int>, std::unique_ptr<SpreadsheetCell>, CellCoordHash> newCells;
    for (auto& [coord, cellPtr] : cells_) {
        if (coord.first >= row && coord.first < row + count) {
            continue;  // Cell removed
        }
        int newRow = coord.first;
        if (coord.first >= row + count) {
            newRow -= count;
            if (cellPtr) cellPtr->SetPosition(newRow, coord.second);
        }
        newCells.emplace(std::make_pair(newRow, coord.second), std::move(cellPtr));
    }
    cells_ = std::move(newCells);

    std::map<int, RowDefinition> newDefs;
    for (auto& [r, def] : rows_) {
        if (r >= row && r < row + count) continue;
        int nr = (r >= row + count) ? r - count : r;
        def.index = nr;
        newDefs[nr] = std::move(def);
    }
    rows_ = std::move(newDefs);

    ShiftCellReferences(row + count, 0, -count, 0);
    MoveFormulaReferences(name_, false, row, -count);
    InvalidateUsedRange();
    NotifyStructureChange();
}

// ============================================================================
// AUTO FILTER
// ============================================================================

void SpreadsheetSheet::SetAutoFilter(const CellRange& range) {
    if (!autoFilter_) {
        autoFilter_ = std::make_unique<AutoFilter>();
    }
    autoFilter_->range = range;
    autoFilter_->enabled = true;
    autoFilter_->columnFilters.clear();
    autoFilter_->hiddenRows.clear();
    NotifyStructureChange();
}

void SpreadsheetSheet::RemoveAutoFilter() {
    if (autoFilter_) {
        // Unhide any rows hidden by the filter.
        for (int r : autoFilter_->hiddenRows) {
            SetRowHidden(r, false);
        }
        autoFilter_.reset();
        NotifyStructureChange();
    }
}

void SpreadsheetSheet::ApplyFilter(int column, const ColumnFilter& filter) {
    if (!autoFilter_ || !autoFilter_->enabled) return;

    // Replace any existing filter for this column.
    auto& filters = autoFilter_->columnFilters;
    filters.erase(std::remove_if(filters.begin(), filters.end(),
        [column](const ColumnFilter& f) { return f.column == column; }),
        filters.end());

    ColumnFilter f = filter;
    f.column = column;
    filters.push_back(f);

    RefreshFilter();
}

void SpreadsheetSheet::ClearFilter(int column) {
    if (!autoFilter_) return;

    auto& filters = autoFilter_->columnFilters;
    filters.erase(std::remove_if(filters.begin(), filters.end(),
        [column](const ColumnFilter& f) { return f.column == column; }),
        filters.end());

    RefreshFilter();
}

void SpreadsheetSheet::ClearAllFilters() {
    if (!autoFilter_) return;
    autoFilter_->columnFilters.clear();
    RefreshFilter();
}

void SpreadsheetSheet::RefreshFilter() {
    if (!autoFilter_ || !autoFilter_->enabled) return;

    // Unhide rows previously hidden by the filter, then recompute.
    for (int r : autoFilter_->hiddenRows) {
        SetRowHidden(r, false);
    }
    autoFilter_->hiddenRows.clear();

    const CellRange& range = autoFilter_->range;
    // First row of the filter range is treated as the header row.
    int firstDataRow = range.start.row + 1;

    for (int row = firstDataRow; row <= range.end.row; ++row) {
        bool visible = true;

        for (const auto& filter : autoFilter_->columnFilters) {
            int col = filter.column;
            const SpreadsheetCell* cell = GetCellIfExists(row, col);
            std::string text = cell ? cell->GetDisplayValue() : "";

            bool match = true;
            switch (filter.op) {
                case FilterOperator::Equals:
                    match = !filter.values.empty() &&
                            std::find(filter.values.begin(), filter.values.end(), text) != filter.values.end();
                    break;
                case FilterOperator::NotEquals:
                    match = filter.values.empty() ||
                            std::find(filter.values.begin(), filter.values.end(), text) == filter.values.end();
                    break;
                case FilterOperator::Contains:
                    match = !filter.values.empty() &&
                            text.find(filter.values.front()) != std::string::npos;
                    break;
                case FilterOperator::NotContains:
                    match = filter.values.empty() ||
                            text.find(filter.values.front()) == std::string::npos;
                    break;
                case FilterOperator::BeginsWith:
                    match = !filter.values.empty() &&
                            text.rfind(filter.values.front(), 0) == 0;
                    break;
                case FilterOperator::EndsWith:
                    if (filter.values.empty()) {
                        match = false;
                    } else {
                        const std::string& suffix = filter.values.front();
                        match = text.size() >= suffix.size() &&
                                text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
                    }
                    break;
                case FilterOperator::GreaterThan:
                case FilterOperator::LessThan:
                case FilterOperator::GreaterOrEqual:
                case FilterOperator::LessOrEqual: {
                    double cellVal = cell ? cell->GetNumber() : 0.0;
                    double cmpVal = 0.0;
                    if (!filter.values.empty()) {
                        try { cmpVal = std::stod(filter.values.front()); } catch (...) { cmpVal = 0.0; }   // locale-ok: compares against what the user typed into the filter
                    }
                    if (filter.op == FilterOperator::GreaterThan) match = cellVal > cmpVal;
                    else if (filter.op == FilterOperator::LessThan) match = cellVal < cmpVal;
                    else if (filter.op == FilterOperator::GreaterOrEqual) match = cellVal >= cmpVal;
                    else match = cellVal <= cmpVal;
                    break;
                }
                default:
                    match = true;
                    break;
            }

            if (text.empty() && filter.showBlanks) {
                match = true;
            }

            if (!match) {
                visible = false;
                break;
            }
        }

        if (!visible) {
            autoFilter_->hiddenRows.push_back(row);
            SetRowHidden(row, true);
        }
    }

    NotifyStructureChange();
}

// ============================================================================
// SORTING
// ============================================================================

void SpreadsheetSheet::Sort(const CellRange& range, const std::vector<SortCriteria>& criteria) {
    if (criteria.empty()) return;

    // Collect source row indices.
    std::vector<int> rows;
    for (int row = range.start.row; row <= range.end.row; ++row) {
        rows.push_back(row);
    }

    std::stable_sort(rows.begin(), rows.end(), [this, &criteria](int a, int b) {
        for (const auto& crit : criteria) {
            int col = crit.column;
            const SpreadsheetCell* cellA = GetCellIfExists(a, col);
            const SpreadsheetCell* cellB = GetCellIfExists(b, col);

            int cmp = 0;
            if (!cellA && !cellB) {
                cmp = 0;
            } else if (!cellA) {
                cmp = 1;   // Empty sorts last
            } else if (!cellB) {
                cmp = -1;
            } else {
                cmp = cellA->CompareValue(*cellB, crit.order == SortOrder::Ascending, crit.caseSensitive);
            }

            if (cmp != 0) return cmp < 0;
        }
        return false;
    });

    // Build the reordered cells, detaching them from the map.
    std::map<int, std::map<int, std::unique_ptr<SpreadsheetCell>>> moved;
    int srcIndex = 0;
    for (int sourceRow : rows) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            auto it = cells_.find({sourceRow, col});
            if (it != cells_.end()) {
                moved[srcIndex][col] = std::move(it->second);
            }
        }
        ++srcIndex;
    }

    // Clear the range.
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            cells_.erase({row, col});
        }
    }

    // Place moved cells at their target rows.
    for (auto& [index, colMap] : moved) {
        int targetRow = range.start.row + index;
        for (auto& [col, cellPtr] : colMap) {
            if (cellPtr) {
                cellPtr->SetPosition(targetRow, col);
                cells_[{targetRow, col}] = std::move(cellPtr);
            }
        }
    }

    InvalidateUsedRange();
    NotifyRangeChange(range);
}

int SpreadsheetSheet::CountFormulaCells(const CellRange& range) const {
    int count = 0;
    for (const auto& [key, cell] : cells_) {
        if (cell && cell->HasFormula() && range.Contains(key.first, key.second)) ++count;
    }
    return count;
}

void SpreadsheetSheet::SortByColumn(const CellRange& range, int column, SortOrder order) {
    SortCriteria crit;
    crit.column = column;
    crit.order = order;
    Sort(range, {crit});
}

// ============================================================================
// CONDITIONAL FORMATTING
// ============================================================================

void SpreadsheetSheet::AddConditionalFormat(const ConditionalFormat& format) {
    conditionalFormats_.push_back(format);
    NotifyRangeChange(format.range);
}

void SpreadsheetSheet::RemoveConditionalFormat(int index) {
    if (index >= 0 && index < static_cast<int>(conditionalFormats_.size())) {
        CellRange range = conditionalFormats_[index].range;
        conditionalFormats_.erase(conditionalFormats_.begin() + index);
        NotifyRangeChange(range);
    }
}

void SpreadsheetSheet::ClearConditionalFormats() {
    conditionalFormats_.clear();
    NotifyStructureChange();
}

void SpreadsheetSheet::SetConditionalFormats(const std::vector<ConditionalFormat>& formats) {
    conditionalFormats_ = formats;
    NotifyStructureChange();
}

std::optional<CellStyle> SpreadsheetSheet::GetConditionalStyle(int row, int col) const {
    return GetConditionalFormatting(row, col).style;
}

namespace {

std::string AsciiLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string TrimSpaces(const std::string& text) {
    const size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos) return std::string();
    const size_t b = text.find_last_not_of(" \t");
    return text.substr(a, b - a + 1);
}

// A whole dot-decimal number, and nothing after it.
std::optional<double> ReadWholeNumber(const std::string& text) {
    const std::string t = TrimSpaces(text);
    if (t.empty()) return std::nullopt;
    double number = 0.0;
    const char* first = t.c_str();
    const char* last = first + t.size();
    if (*first == '+') ++first;
    if (first != last && ParseFloatClassic(first, last, number) == last) return number;
    return std::nullopt;
}

// The function library the conditions' formulas are evaluated with: one per
// thread, built on first use.
FormulaFunctionLibrary& ConditionFunctionLibrary() {
    thread_local FormulaFunctionLibrary library;
    return library;
}

// `formula` (with or without its '='), written for `anchor` and moved to
// (row, col) as a copied formula is, evaluated on `sheet`.
FormulaValue EvaluateSheetFormula(const SpreadsheetSheet& sheet, const std::string& formula,
                                  int row, int col, const CellAddress& anchor) {
    std::string text = TrimSpaces(formula);
    if (text.empty()) return FormulaValue::Empty();
    if (text[0] != '=') text = "=" + text;
    if (row != anchor.row || col != anchor.col) {
        text = ShiftFormulaReferences(text, row - anchor.row, col - anchor.col);
    }
    SpreadsheetFormula parsed(text, CellAddress(row, col));
    if (!parsed.Parse()) return FormulaValue::Error(CellErrorType::NameError);
    FormulaEvaluator evaluator(ConditionFunctionLibrary());
    // The evaluator reads cells through a non-const sheet; it changes none.
    auto& readable = const_cast<SpreadsheetSheet&>(sheet);
    evaluator.SetSpreadsheet(readable.GetParent());
    evaluator.SetCurrentSheet(&readable);
    return evaluator.Evaluate(parsed);
}

// A rule's operand: a number, "quoted text", or a formula (a reference, a
// calculation); text that is none of these stands for itself.
FormulaValue ConditionOperand(const SpreadsheetSheet& sheet, const std::string& operand,
                              int row, int col, const CellAddress& anchor) {
    const std::string t = TrimSpaces(operand);
    if (t.empty()) return FormulaValue::Empty();
    if (auto number = ReadWholeNumber(t)) return FormulaValue::Number(*number);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        const std::string inner = t.substr(1, t.size() - 2);
        std::string unquoted;
        for (size_t i = 0; i < inner.size(); ++i) {
            unquoted += inner[i];
            if (inner[i] == '"' && i + 1 < inner.size() && inner[i + 1] == '"') ++i;
        }
        return FormulaValue::Text(unquoted);
    }
    FormulaValue value = EvaluateSheetFormula(sheet, t, row, col, anchor);
    if (value.IsError() && t[0] != '=') return FormulaValue::Text(t);
    return value;
}

bool FormulaValueIsTrue(const FormulaValue& value) {
    if (value.IsBoolean() || value.IsNumber()) return value.GetBoolean();
    if (value.IsText()) return AsciiLower(value.GetText()) == "true";
    return false;
}

// The cell's value as a rule compares it: a number (a date or a currency
// amount too), else its text; nothing for an empty cell.
FormulaValue CellConditionValue(const SpreadsheetCell* cell) {
    if (!cell || cell->IsEmpty()) return FormulaValue::Empty();
    if (cell->HasError()) return FormulaValue::Error(cell->GetError());
    if (auto number = cell->TryGetNumber()) return FormulaValue::Number(*number);
    return FormulaValue::Text(cell->GetDisplayValue());
}

bool CompareCondition(const FormulaValue& subject, ValidationOperator op,
                      const FormulaValue& v1, const FormulaValue& v2) {
    auto order = [](const FormulaValue& a, const FormulaValue& b) -> std::optional<int> {
        const bool aNumber = a.IsNumber() || a.IsBoolean() || a.IsEmpty();
        const bool bNumber = b.IsNumber() || b.IsBoolean() || b.IsEmpty();
        if (aNumber && bNumber) {
            const double x = a.GetNumber(), y = b.GetNumber();
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        if (a.IsText() && b.IsText()) {
            const int c = AsciiLower(a.GetText()).compare(AsciiLower(b.GetText()));
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        return std::nullopt;   // a number against text: never equal, no order
    };
    if (subject.IsError()) return false;
    const auto c1 = order(subject, v1);
    switch (op) {
        case ValidationOperator::Equal:          return c1 && *c1 == 0;
        case ValidationOperator::NotEqual:       return !c1 || *c1 != 0;
        case ValidationOperator::GreaterThan:    return c1 && *c1 > 0;
        case ValidationOperator::LessThan:       return c1 && *c1 < 0;
        case ValidationOperator::GreaterOrEqual: return c1 && *c1 >= 0;
        case ValidationOperator::LessOrEqual:    return c1 && *c1 <= 0;
        case ValidationOperator::Between:
        case ValidationOperator::NotBetween: {
            const auto c2 = order(subject, v2);
            const bool inside = c1 && c2 && *c1 >= 0 && *c2 <= 0;
            return op == ValidationOperator::Between ? inside : !inside;
        }
    }
    return false;
}

// PERCENTILE.INC of sorted values; p from 0 to 1.
double SortedPercentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    p = std::clamp(p, 0.0, 1.0);
    const double position = p * static_cast<double>(sorted.size() - 1);
    const size_t below = static_cast<size_t>(std::floor(position));
    const size_t above = std::min(sorted.size() - 1, below + 1);
    return sorted[below] + (sorted[above] - sorted[below]) * (position - static_cast<double>(below));
}

Color MixColours(const Color& a, const Color& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto mix = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(static_cast<double>(x) + (static_cast<double>(y) - x) * t));
    };
    return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}

} // namespace

const SpreadsheetSheet::ConditionalRangeStats&
SpreadsheetSheet::GetConditionalRangeStats(size_t formatIndex, size_t ruleIndex) const {
    if (conditionalStatsRevision_ != contentRevision_) {
        conditionalStats_.clear();
        conditionalStatsRevision_ = contentRevision_;
    }
    const auto key = std::make_pair(formatIndex, ruleIndex);
    auto found = conditionalStats_.find(key);
    if (found != conditionalStats_.end()) return found->second;

    ConditionalRangeStats stats;
    const CellRange& range = conditionalFormats_[formatIndex].range;
    // Only the cells in use: a rule over whole columns reads what is there.
    const CellRange used = GetUsedRange();
    const int r0 = std::max(range.start.row, used.start.row), r1 = std::min(range.end.row, used.end.row);
    const int c0 = std::max(range.start.col, used.start.col), c1 = std::min(range.end.col, used.end.col);
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            const SpreadsheetCell* cell = GetCellIfExists(r, c);
            if (!cell || cell->IsEmpty() || cell->HasError()) continue;
            if (auto number = cell->TryGetNumber()) {
                stats.sortedNumbers.push_back(*number);
                stats.sum += *number;
            }
            const std::string text = cell->GetDisplayValue();
            if (!text.empty()) ++stats.textCounts[AsciiLower(text)];
        }
    }
    std::sort(stats.sortedNumbers.begin(), stats.sortedNumbers.end());
    return conditionalStats_.emplace(key, std::move(stats)).first->second;
}

ConditionalCellFormat SpreadsheetSheet::GetConditionalFormatting(int row, int col) const {
    ConditionalCellFormat result;
    if (conditionalFormats_.empty()) return result;

    // The rules over the cell, by priority (a rule without one keeps its
    // place after those with one).
    struct Candidate { size_t format; size_t rule; int priority; };
    std::vector<Candidate> candidates;
    for (size_t f = 0; f < conditionalFormats_.size(); ++f) {
        if (!conditionalFormats_[f].range.Contains(row, col)) continue;
        const auto& rules = conditionalFormats_[f].rules;
        for (size_t r = 0; r < rules.size(); ++r) {
            const int priority = rules[r].priority > 0 ? rules[r].priority : INT_MAX;
            candidates.push_back({f, r, priority});
        }
    }
    if (candidates.empty()) return result;
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });

    // Formulas read cells through the non-const accessors; nothing changes.
    const bool wasFrozen = contentRevisionFrozen_;
    contentRevisionFrozen_ = true;

    const SpreadsheetCell* cell = GetCellIfExists(row, col);
    const FormulaValue value = CellConditionValue(cell);
    const std::string text = cell ? cell->GetDisplayValue() : std::string();
    const std::string lowerText = AsciiLower(text);

    for (const Candidate& candidate : candidates) {
        const ConditionalFormat& format = conditionalFormats_[candidate.format];
        const ConditionalFormatRule& rule = format.rules[candidate.rule];
        const CellAddress anchor = format.range.start;

        // A threshold of a colour scale or a data bar, as a number.
        auto threshold = [&](const ConditionalThreshold& t, const ConditionalRangeStats& stats) -> double {
            const auto& sorted = stats.sortedNumbers;
            const double low = sorted.empty() ? 0.0 : sorted.front();
            const double high = sorted.empty() ? 0.0 : sorted.back();
            const std::optional<double> number = ReadWholeNumber(t.value);
            switch (t.kind) {
                case ConditionalThresholdKind::Lowest: return low;
                case ConditionalThresholdKind::Highest: return high;
                case ConditionalThresholdKind::Percent: return low + (high - low) * number.value_or(0.0) / 100.0;
                case ConditionalThresholdKind::Percentile: return SortedPercentile(sorted, number.value_or(50.0) / 100.0);
                case ConditionalThresholdKind::Number:
                    if (number) return *number;
                    [[fallthrough]];
                case ConditionalThresholdKind::Formula:
                    return ConditionOperand(*this, t.value, anchor.row, anchor.col, anchor).ToNumber();
            }
            return low;
        };
        auto thresholds = [&](size_t count) {
            if (rule.thresholds.size() == count) return rule.thresholds;
            std::vector<ConditionalThreshold> points;
            if (rule.colorScaleValues.size() == count) {
                for (double v : rule.colorScaleValues) {
                    points.emplace_back(ConditionalThresholdKind::Number, FormatFloatClassic(v, 15));
                }
                return points;
            }
            points.emplace_back(ConditionalThresholdKind::Lowest);
            if (count == 3) points.emplace_back(ConditionalThresholdKind::Percentile, "50");
            points.emplace_back(ConditionalThresholdKind::Highest);
            return points;
        };

        if (rule.type == ConditionalFormatType::ColorScale) {
            if (result.fill || !value.IsNumber() || rule.colorScale.size() < 2) continue;
            const size_t count = std::min<size_t>(rule.colorScale.size(), 3);
            const ConditionalRangeStats& stats = GetConditionalRangeStats(candidate.format, candidate.rule);
            std::vector<double> at;
            for (const auto& point : thresholds(count)) at.push_back(threshold(point, stats));
            const double v = value.GetNumber();
            Color colour = rule.colorScale[0];
            if (v >= at.back()) {
                colour = rule.colorScale[count - 1];
            } else if (v > at.front()) {
                for (size_t i = 0; i + 1 < count; ++i) {
                    if (v <= at[i + 1]) {
                        const double span = at[i + 1] - at[i];
                        colour = MixColours(rule.colorScale[i], rule.colorScale[i + 1],
                                            span > 0 ? (v - at[i]) / span : 1.0);
                        break;
                    }
                }
            }
            result.fill = colour;
            if (rule.stopIfTrue) break;
            continue;
        }
        if (rule.type == ConditionalFormatType::DataBar) {
            if (result.hasDataBar || !value.IsNumber()) continue;
            const ConditionalRangeStats& stats = GetConditionalRangeStats(candidate.format, candidate.rule);
            const auto points = thresholds(2);
            const double low = threshold(points[0], stats);
            const double high = threshold(points[1], stats);
            const double v = value.GetNumber();
            result.hasDataBar = true;
            result.dataBarLength = high > low ? std::clamp((v - low) / (high - low), 0.0, 1.0)
                                              : (v >= high ? 1.0 : 0.0);
            result.dataBarColor = rule.dataBarColor;
            result.showValue = rule.showValue;
            if (rule.stopIfTrue) break;
            continue;
        }
        if (result.style) continue;   // the first style that holds wins

        bool matched = false;
        switch (rule.type) {
            case ConditionalFormatType::CellValue: {
                const FormulaValue v1 = ConditionOperand(*this, rule.formula1, row, col, anchor);
                const FormulaValue v2 = ConditionOperand(*this, rule.formula2, row, col, anchor);
                // An empty cell is 0 against a number and "" against text.
                FormulaValue subject = value;
                if (subject.IsEmpty() && v1.IsText()) subject = FormulaValue::Text("");
                matched = CompareCondition(subject, rule.op, v1, v2);
                break;
            }
            case ConditionalFormatType::Expression:
                matched = FormulaValueIsTrue(EvaluateSheetFormula(*this, rule.formula1, row, col, anchor));
                break;
            case ConditionalFormatType::ContainsText:
                matched = !rule.formula1.empty() && lowerText.find(AsciiLower(rule.formula1)) != std::string::npos;
                break;
            case ConditionalFormatType::NotContainsText:
                matched = rule.formula1.empty() || lowerText.find(AsciiLower(rule.formula1)) == std::string::npos;
                break;
            case ConditionalFormatType::BeginsWithText:
                matched = !rule.formula1.empty() && lowerText.rfind(AsciiLower(rule.formula1), 0) == 0;
                break;
            case ConditionalFormatType::EndsWithText: {
                const std::string ending = AsciiLower(rule.formula1);
                matched = !ending.empty() && lowerText.size() >= ending.size() &&
                          lowerText.compare(lowerText.size() - ending.size(), ending.size(), ending) == 0;
                break;
            }
            case ConditionalFormatType::ContainsBlanks:
                matched = TrimSpaces(text).empty();
                break;
            case ConditionalFormatType::NotContainsBlanks:
                matched = !TrimSpaces(text).empty();
                break;
            case ConditionalFormatType::ContainsErrors:
                matched = cell && cell->HasError();
                break;
            case ConditionalFormatType::NotContainsErrors:
                matched = !cell || !cell->HasError();
                break;
            case ConditionalFormatType::Top10: {
                if (!value.IsNumber()) break;
                const auto& sorted = GetConditionalRangeStats(candidate.format, candidate.rule).sortedNumbers;
                if (sorted.empty()) break;
                const double wanted = ReadWholeNumber(rule.formula1).value_or(10.0);
                size_t n = rule.percent
                    ? static_cast<size_t>(std::floor(static_cast<double>(sorted.size()) * wanted / 100.0))
                    : static_cast<size_t>(std::max(0.0, wanted));
                n = std::clamp<size_t>(n, 1, sorted.size());
                matched = rule.bottom ? value.GetNumber() <= sorted[n - 1]
                                      : value.GetNumber() >= sorted[sorted.size() - n];
                break;
            }
            case ConditionalFormatType::AboveAverage: {
                if (!value.IsNumber()) break;
                const ConditionalRangeStats& stats = GetConditionalRangeStats(candidate.format, candidate.rule);
                if (stats.sortedNumbers.empty()) break;
                const double average = stats.sum / static_cast<double>(stats.sortedNumbers.size());
                const double v = value.GetNumber();
                matched = rule.aboveAverage ? (v > average || (rule.equalAverage && v == average))
                                            : (v < average || (rule.equalAverage && v == average));
                break;
            }
            case ConditionalFormatType::DuplicateValues:
            case ConditionalFormatType::UniqueValues: {
                if (text.empty()) break;
                const auto& counts = GetConditionalRangeStats(candidate.format, candidate.rule).textCounts;
                auto it = counts.find(lowerText);
                const int count = it == counts.end() ? 0 : it->second;
                matched = rule.type == ConditionalFormatType::DuplicateValues ? count > 1 : count == 1;
                break;
            }
            default:
                break;
        }
        if (matched) {
            result.style = rule.style;
            if (rule.stopIfTrue) break;
        }
    }

    contentRevisionFrozen_ = wasFrozen;
    return result;
}

// ============================================================================
// DATA VALIDATION
// ============================================================================

void SpreadsheetSheet::SetValidation(const CellRange& range, const DataValidation& validation) {
    // Remove any existing validation covering the same exact range first.
    validations_.erase(std::remove_if(validations_.begin(), validations_.end(),
        [&range](const std::pair<CellRange, DataValidation>& v) { return v.first == range; }),
        validations_.end());

    validations_.emplace_back(range, validation);
    NotifyRangeChange(range);
}

void SpreadsheetSheet::RemoveValidation(const CellRange& range) {
    validations_.erase(std::remove_if(validations_.begin(), validations_.end(),
        [&range](const std::pair<CellRange, DataValidation>& v) { return v.first.Intersects(range); }),
        validations_.end());
    NotifyRangeChange(range);
}

void SpreadsheetSheet::ClearAllValidations() {
    validations_.clear();
    NotifyStructureChange();
}

const DataValidation* SpreadsheetSheet::GetValidation(int row, int col) const {
    // Later validations take precedence over earlier overlapping ones.
    for (auto it = validations_.rbegin(); it != validations_.rend(); ++it) {
        if (it->first.Contains(row, col)) {
            return &it->second;
        }
    }
    return nullptr;
}

namespace {

// A validation bound: a dot-decimal number (what ODS and XLSX store, a date
// or time as its serial number), and for a Date or Time rule also
// "YYYY-MM-DD" or "HH:MM[:SS]", which a person setting a rule types.
std::optional<double> ReadValidationBound(const std::string& text, ValidationType type) {
    std::string t = text;
    const size_t a = t.find_first_not_of(" \t");
    const size_t b = t.find_last_not_of(" \t");
    t = (a == std::string::npos) ? std::string() : t.substr(a, b - a + 1);
    if (t.empty()) return std::nullopt;

    // The whole bound must be the number: TryParseFloat would read
    // "2026-01-01" as 2026 and ignore the rest.
    double number = 0.0;
    const char* first = t.c_str();
    const char* last = first + t.size();
    if (*first == '+') ++first;
    if (ParseFloatClassic(first, last, number) == last && first != last) return number;

    int p1 = 0, p2 = 0, p3 = 0;
    char c1 = 0, c2 = 0, rest = 0;
    if (type == ValidationType::Date &&
        std::sscanf(t.c_str(), "%d%c%d%c%d%c", &p1, &c1, &p2, &c2, &p3, &rest) == 5 &&
        c1 == '-' && c2 == '-' && p2 >= 1 && p2 <= 12 && p3 >= 1 && p3 <= 31) {
        return DateTimeValue::FromDate(p1, p2, p3).serialNumber;
    }
    if (type == ValidationType::Time) {
        p3 = 0;
        const int n = std::sscanf(t.c_str(), "%d%c%d%c%d%c", &p1, &c1, &p2, &c2, &p3, &rest);
        if ((n == 3 && c1 == ':') || (n == 5 && c1 == ':' && c2 == ':')) {
            if (p1 >= 0 && p1 <= 24 && p2 >= 0 && p2 < 60 && p3 >= 0 && p3 < 60) {
                return DateTimeValue::FromTime(p1, p2, p3).serialNumber;
            }
        }
    }
    return std::nullopt;
}

// The items of a list rule: "a,b,c" as typed, or ODF's "\"a\";\"b\"".
std::vector<std::string> ReadValidationList(const std::string& source) {
    const char separator = source.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= source.size()) {
        const size_t sep = source.find(separator, start);
        std::string item = source.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        const size_t a = item.find_first_not_of(" \t");
        const size_t b = item.find_last_not_of(" \t");
        item = (a == std::string::npos) ? std::string() : item.substr(a, b - a + 1);
        if (item.size() >= 2 && item.front() == '"' && item.back() == '"') {
            item = item.substr(1, item.size() - 2);
        }
        items.push_back(item);
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return items;
}

bool CompareByOperator(double subject, ValidationOperator op, double v1, double v2) {
    switch (op) {
        case ValidationOperator::Between:        return subject >= v1 && subject <= v2;
        case ValidationOperator::NotBetween:     return subject < v1 || subject > v2;
        case ValidationOperator::Equal:          return subject == v1;
        case ValidationOperator::NotEqual:       return subject != v1;
        case ValidationOperator::GreaterThan:    return subject > v1;
        case ValidationOperator::LessThan:       return subject < v1;
        case ValidationOperator::GreaterOrEqual: return subject >= v1;
        case ValidationOperator::LessOrEqual:    return subject <= v1;
    }
    return true;
}

bool OperatorNeedsSecondBound(ValidationOperator op) {
    return op == ValidationOperator::Between || op == ValidationOperator::NotBetween;
}

} // namespace

bool CheckDataValidation(const DataValidation& validation, const SpreadsheetCell* cell,
                         std::string* errorMessage) {
    if (validation.type == ValidationType::Any || validation.type == ValidationType::Custom) {
        return true;   // Custom: a formula, evaluated on the sheet (SpreadsheetSheet::ValidateCell)
    }

    auto fail = [&](const std::string& fallback) {
        if (errorMessage) {
            *errorMessage = validation.errorMessage.empty() ? fallback : validation.errorMessage;
        }
        return false;
    };

    // Empty, or a formula or text that shows nothing.
    const bool isBlank = !cell || cell->IsEmpty() || cell->GetDisplayValue().empty();
    if (isBlank) {
        return validation.allowBlank ? true : fail(Tr("A value is required in this cell."));
    }
    const std::string invalid = Tr("The value does not match the rule for this cell.");

    if (validation.type == ValidationType::List) {
        const std::string text = cell->GetDisplayValue();
        for (const auto& item : ReadValidationList(validation.formula1)) {
            if (item == text) return true;
        }
        return fail(invalid);
    }

    // The value the rule compares.
    double subject = 0.0;
    if (validation.type == ValidationType::TextLength) {
        subject = static_cast<double>(SpreadsheetUtf8Length(cell->GetDisplayValue()));
    } else {
        const CellValueVariant& raw = cell->GetRawValue();
        if (const auto* number = std::get_if<double>(&raw)) {
            subject = *number;
        } else if (const auto* currency = std::get_if<CurrencyValue>(&raw)) {
            subject = currency->amount;
        } else {
            return fail(invalid);   // text, a truth value or an error is not a number
        }
        if (validation.type == ValidationType::WholeNumber && subject != std::floor(subject)) {
            return fail(invalid);
        }
    }

    const auto v1 = ReadValidationBound(validation.formula1, validation.type);
    const auto v2 = OperatorNeedsSecondBound(validation.op)
                        ? ReadValidationBound(validation.formula2, validation.type)
                        : std::optional<double>(0.0);
    if (!v1 || !v2) return fail(invalid);   // a bound that cannot be read

    return CompareByOperator(subject, validation.op, *v1, *v2) ? true : fail(invalid);
}

std::optional<double> ReadDataValidationBound(const std::string& text, ValidationType type) {
    return ReadValidationBound(text, type);
}

std::vector<std::string> DataValidationListItems(const DataValidation& validation) {
    std::vector<std::string> items;
    if (validation.type != ValidationType::List) return items;
    for (auto& item : ReadValidationList(validation.formula1)) {
        if (!item.empty()) items.push_back(std::move(item));
    }
    return items;
}

bool SpreadsheetCell::ValidateValue(std::string* errorMessage) const {
    if (!validation_) return true;
    return CheckDataValidation(*validation_, this, errorMessage);
}

const DataValidation* SpreadsheetSheet::GetEffectiveValidation(int row, int col) const {
    // A rule set on the range (SetValidation) comes first; a rule a cell
    // carries itself (SpreadsheetCell::SetValidation) applies otherwise.
    if (const DataValidation* validation = GetValidation(row, col)) return validation;
    const SpreadsheetCell* cell = GetCellIfExists(row, col);
    return cell ? cell->GetValidation() : nullptr;
}

namespace {

// The values of the cells a list rule names ("=$D$1:$D$5", "='Lists'.A1:A9",
// a named range), in order, empty ones left out.
std::vector<std::string> RangeListItems(const SpreadsheetSheet& sheet, const std::string& source) {
    constexpr size_t kMaxItems = 10000;
    std::vector<std::string> items;
    SpreadsheetFormula parsed(source, CellAddress(0, 0));
    if (!parsed.Parse() || !parsed.GetAST()) return items;
    FormulaEvaluator evaluator(ConditionFunctionLibrary());
    auto& readable = const_cast<SpreadsheetSheet&>(sheet);
    evaluator.SetSpreadsheet(readable.GetParent());
    evaluator.SetCurrentSheet(&readable);
    const FormulaNode* root = parsed.GetAST();
    CellRange range;
    switch (root->nodeType) {
        case FormulaNodeType::RangeRef: range = root->rangeRef; break;
        case FormulaNodeType::CellRef: range = CellRange(root->cellRef, root->cellRef); break;
        case FormulaNodeType::NamedRef: range = evaluator.GetNamedRange(root->namedRef); break;
        default: return items;
    }
    if (!range.IsValid()) return items;
    if (static_cast<long long>(range.RowCount()) * range.ColCount() > static_cast<long long>(kMaxItems)) {
        range.end.row = range.start.row + static_cast<int>(kMaxItems / std::max(1, range.ColCount())) - 1;
    }
    for (const FormulaValue& value : evaluator.FlattenRange(range)) {
        if (value.IsEmpty() || value.IsError()) continue;
        std::string text = value.GetText();
        if (!text.empty()) items.push_back(std::move(text));
    }
    return items;
}

} // namespace

std::vector<std::string> SpreadsheetSheet::GetValidationListItems(int row, int col) const {
    const DataValidation* rule = GetEffectiveValidation(row, col);
    if (!rule || rule->type != ValidationType::List) return {};
    if (!IsRangeListSource(*rule)) return DataValidationListItems(*rule);
    const bool wasFrozen = contentRevisionFrozen_;
    contentRevisionFrozen_ = true;
    std::vector<std::string> items = RangeListItems(*this, rule->formula1);
    contentRevisionFrozen_ = wasFrozen;
    return items;
}

bool SpreadsheetSheet::ValidateCell(int row, int col, std::string* errorMessage) const {
    const DataValidation* validation = GetEffectiveValidation(row, col);
    if (!validation) return true;
    const SpreadsheetCell* cell = GetCellIfExists(row, col);
    const bool custom = validation->type == ValidationType::Custom;
    // A formula in the rule is written for the rule's top-left cell (the
    // cell itself for a cell's own rule) and moved to this one, as a copied
    // formula is.
    CellAddress anchor(row, col);
    if (validation == GetValidation(row, col)) {
        for (auto it = validations_.rbegin(); it != validations_.rend(); ++it) {
            if (&it->second == validation) { anchor = it->first.start; break; }
        }
    }
    if (!custom && !IsRangeListSource(*validation)) {
        auto isFormula = [](const std::string& bound) {
            const std::string t = TrimSpaces(bound);
            return !t.empty() && t[0] == '=';
        };
        if (validation->type == ValidationType::List ||
            (!isFormula(validation->formula1) && !isFormula(validation->formula2))) {
            return CheckDataValidation(*validation, cell, errorMessage);
        }
        // A bound that is a formula ("=$B$1") is worked out first.
        DataValidation resolved = *validation;
        const bool wasFrozen = contentRevisionFrozen_;
        contentRevisionFrozen_ = true;
        for (std::string* bound : {&resolved.formula1, &resolved.formula2}) {
            if (!isFormula(*bound)) continue;
            const FormulaValue value = EvaluateSheetFormula(*this, *bound, row, col, anchor);
            *bound = (value.IsNumber() || value.IsBoolean()) ? FormatFloatRoundTrip(value.GetNumber())
                                                             : std::string();
        }
        contentRevisionFrozen_ = wasFrozen;
        return CheckDataValidation(resolved, cell, errorMessage);
    }

    auto fail = [&](const std::string& fallback) {
        if (errorMessage) {
            *errorMessage = validation->errorMessage.empty() ? fallback : validation->errorMessage;
        }
        return false;
    };
    const bool isBlank = !cell || cell->IsEmpty() || cell->GetDisplayValue().empty();
    if (isBlank) {
        return validation->allowBlank ? true : fail(Tr("A value is required in this cell."));
    }
    const std::string invalid = Tr("The value does not match the rule for this cell.");

    const bool wasFrozen = contentRevisionFrozen_;
    contentRevisionFrozen_ = true;
    bool valid = false;
    if (custom) {
        valid = FormulaValueIsTrue(EvaluateSheetFormula(*this, validation->formula1, row, col, anchor));
    } else {
        const std::string text = cell->GetDisplayValue();
        for (const std::string& item : RangeListItems(*this, validation->formula1)) {
            if (item == text) { valid = true; break; }
        }
    }
    contentRevisionFrozen_ = wasFrozen;
    return valid ? true : fail(invalid);
}

// ============================================================================
// NAMED RANGES
// ============================================================================

void SpreadsheetSheet::AddNamedRange(const std::string& name, const CellRange& range) {
    NamedRange nr(name, range, name_);
    namedRanges_[name] = nr;
}

void SpreadsheetSheet::RemoveNamedRange(const std::string& name) {
    namedRanges_.erase(name);
}

bool SpreadsheetSheet::HasNamedRange(const std::string& name) const {
    return namedRanges_.find(name) != namedRanges_.end();
}

const NamedRange* SpreadsheetSheet::GetNamedRange(const std::string& name) const {
    auto it = namedRanges_.find(name);
    return (it != namedRanges_.end()) ? &it->second : nullptr;
}

// ============================================================================
// PRINT SETTINGS
// ============================================================================

void SpreadsheetSheet::SetPrintArea(const CellRange& range) {
    printSettings_.printArea = range;
}

void SpreadsheetSheet::ClearPrintArea() {
    printSettings_.printArea = CellRange();
}

void SpreadsheetSheet::AddPageBreak(int row, bool horizontal) {
    auto& breaks = horizontal ? printSettings_.rowBreaks : printSettings_.colBreaks;
    if (std::find(breaks.begin(), breaks.end(), row) == breaks.end()) {
        breaks.push_back(row);
        std::sort(breaks.begin(), breaks.end());
    }
}

void SpreadsheetSheet::RemovePageBreak(int row, bool horizontal) {
    auto& breaks = horizontal ? printSettings_.rowBreaks : printSettings_.colBreaks;
    breaks.erase(std::remove(breaks.begin(), breaks.end(), row), breaks.end());
}

// ============================================================================
// PROTECTION
// ============================================================================

void SpreadsheetSheet::Protect(const std::string& password) {
    protection_.isProtected = true;
    // Store a trivial hash of the password (not cryptographically secure).
    if (!password.empty()) {
        std::hash<std::string> hasher;
        protection_.passwordHash = std::to_string(hasher(password));
    } else {
        protection_.passwordHash.clear();
    }
}

void SpreadsheetSheet::Unprotect(const std::string& password) {
    if (!protection_.passwordHash.empty()) {
        std::hash<std::string> hasher;
        if (std::to_string(hasher(password)) != protection_.passwordHash) {
            return;  // Wrong password; remain protected.
        }
    }
    protection_.isProtected = false;
    protection_.passwordHash.clear();
}

bool SpreadsheetSheet::CanEdit(int row, int col) const {
    if (!protection_.isProtected) return true;

    const SpreadsheetCell* cell = GetCellIfExists(row, col);
    if (cell && cell->HasCustomStyle()) {
        // Unlocked cells remain editable on a protected sheet.
        return !cell->GetStyle().locked;
    }
    // Default style is locked.
    return false;
}

// ============================================================================
// COPY / PASTE
// ============================================================================

std::vector<std::vector<SpreadsheetCell>> SpreadsheetSheet::CopyCells(const CellRange& range) const {
    std::vector<std::vector<SpreadsheetCell>> result;

    for (int row = range.start.row; row <= range.end.row; ++row) {
        std::vector<SpreadsheetCell> rowData;
        for (int col = range.start.col; col <= range.end.col; ++col) {
            if (auto* cell = GetCellIfExists(row, col)) {
                rowData.push_back(cell->Clone());
            } else {
                rowData.push_back(SpreadsheetCell(row, col));
            }
        }
        result.push_back(std::move(rowData));
    }

    return result;
}

void SpreadsheetSheet::PasteCells(const CellAddress& destination,
                                  const std::vector<std::vector<SpreadsheetCell>>& cells,
                                  const PasteOptions& options,
                                  std::optional<CellAddress> from) {
    const int rowDelta = from ? destination.row - from->row : 0;
    const int colDelta = from ? destination.col - from->col : 0;
    auto movedFormula = [&](const SpreadsheetCell& source) {
        return (rowDelta == 0 && colDelta == 0) ? source.GetFormulaText()
                                                : ShiftFormulaReferences(source.GetFormulaText(), rowDelta, colDelta);
    };
    for (size_t r = 0; r < cells.size(); ++r) {
        for (size_t c = 0; c < cells[r].size(); ++c) {
            int targetRow = destination.row + static_cast<int>(r);
            int targetCol = destination.col + static_cast<int>(c);

            if (targetRow >= SpreadsheetLimits::MaxRows || targetCol >= SpreadsheetLimits::MaxColumns) {
                continue;
            }

            const SpreadsheetCell& source = cells[r][c];

            if (options.skipBlanks && source.IsEmpty()) {
                continue;
            }

            SpreadsheetCell& target = GetOrCreateCell(targetRow, targetCol);

            const PasteType pt = options.pasteType;
            bool pasteValues = (pt == PasteType::All || pt == PasteType::Values ||
                                pt == PasteType::AllExceptBorders ||
                                pt == PasteType::FormulasAndNumberFormats ||
                                pt == PasteType::ValuesAndNumberFormats ||
                                pt == PasteType::Formulas);
            bool pasteFormats = (pt == PasteType::All || pt == PasteType::Formats ||
                                 pt == PasteType::AllExceptBorders ||
                                 pt == PasteType::FormulasAndNumberFormats ||
                                 pt == PasteType::ValuesAndNumberFormats);

            if (pasteValues) {
                if (source.IsEmpty()) {
                    target.ClearContents();
                } else if (pt == PasteType::All || pt == PasteType::AllExceptBorders ||
                           pt == PasteType::Formulas || pt == PasteType::FormulasAndNumberFormats) {
                    // Paste the formula text when present, otherwise the value.
                    if (source.HasFormula()) {
                        target.SetFormula(movedFormula(source));
                    } else {
                        target.SetValue(source.GetRawValue(), source.GetValueType());
                    }
                } else {
                    // Values paste: paste the computed value, not the formula.
                    // A formula cell holds its result under the Formula type;
                    // the value pasted takes the type of what it is.
                    CellValueType type = source.GetValueType();
                    if (source.HasFormula() || type == CellValueType::Formula) {
                        const CellValueVariant& v = source.GetRawValue();
                        type = std::holds_alternative<std::string>(v)     ? CellValueType::Text
                             : std::holds_alternative<double>(v)          ? CellValueType::Number
                             : std::holds_alternative<bool>(v)            ? CellValueType::Boolean
                             : std::holds_alternative<CellErrorType>(v)   ? CellValueType::Error
                             : std::holds_alternative<CurrencyValue>(v)   ? CellValueType::Currency
                                                                          : CellValueType::Empty;
                    }
                    target.SetValue(source.GetRawValue(), type);
                }
            }

            if (pasteFormats && source.HasCustomStyle()) {
                target.SetStyle(source.GetStyle());
            }
        }
    }

    InvalidateUsedRange();
    NotifyStructureChange();
}

// ============================================================================
// FILL OPERATIONS
// ============================================================================

// Shifts every A1 reference outside string literals. A reference is up to
// three letters then digits, each optionally $-anchored, standing on its own:
// not glued to a longer name and not a function call (LOG10( ).
std::string ShiftFormulaReferences(const std::string& formula, int rowDelta, int colDelta) {
    auto isWordChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
    };
    std::string out;
    out.reserve(formula.size() + 8);
    const size_t n = formula.size();
    size_t i = 0;
    bool inString = false;
    while (i < n) {
        const char c = formula[i];
        if (c == '"') { inString = !inString; out += c; ++i; continue; }
        if (inString || (i > 0 && isWordChar(formula[i - 1]))) { out += c; ++i; continue; }

        // Try to read [$]LETTERS[$]DIGITS starting here.
        size_t p = i;
        const bool colAbs = (p < n && formula[p] == '$');
        if (colAbs) ++p;
        const size_t lettersStart = p;
        while (p < n && std::isalpha(static_cast<unsigned char>(formula[p])) && p - lettersStart < 4) ++p;
        const size_t letterCount = p - lettersStart;
        const bool rowAbs = (p < n && formula[p] == '$');
        if (rowAbs) ++p;
        const size_t digitsStart = p;
        while (p < n && std::isdigit(static_cast<unsigned char>(formula[p]))) ++p;
        const size_t digitCount = p - digitsStart;

        const bool isRef = letterCount >= 1 && letterCount <= 3 && digitCount >= 1 && digitCount <= 7 &&
                           !(p < n && (isWordChar(formula[p]) || formula[p] == '(' || formula[p] == '$'));
        if (!isRef) {
            if (!isWordChar(c) && c != '$') { out += c; ++i; continue; }
            // Copy the whole word so its tail is not mistaken for a reference.
            size_t end = i + 1;
            while (end < n && isWordChar(formula[end])) ++end;
            out.append(formula, i, end - i);
            i = end;
            continue;
        }

        std::string letters = formula.substr(lettersStart, letterCount);
        for (auto& ch : letters) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        int col = CellAddress::LetterToColumn(letters);
        int row = std::stoi(formula.substr(digitsStart, digitCount)) - 1;
        if (!colAbs) col += colDelta;
        if (!rowAbs) row += rowDelta;
        if (col < 0 || row < 0) {
            out += "#REF!";
        } else {
            if (colAbs) out += '$';
            out += CellAddress::ColumnToLetter(col);
            if (rowAbs) out += '$';
            out += std::to_string(row + 1);
        }
        i = p;
    }
    return out;
}

namespace {

// "Item 7" -> ("Item ", 7). False when the text does not end in digits.
bool SplitTrailingNumber(const std::string& text, std::string& prefix, long long& number) {
    size_t end = text.size();
    size_t start = end;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(text[start - 1]))) --start;
    if (start == end || end - start > 15) return false;
    prefix = text.substr(0, start);
    number = std::stoll(text.substr(start));
    return true;
}

} // namespace

bool FormulaAcceptsReferenceAt(const std::string& text, size_t start, size_t end) {
    if (text.empty() || text[0] != '=') return false;
    if (start < 1 || start > end || end > text.size()) return false;

    // Not inside a "text literal".
    size_t quotes = 0;
    for (size_t i = 0; i < start; ++i) {
        if (text[i] == '"') ++quotes;
    }
    if (quotes % 2 != 0) return false;

    // Before it: the '=', an opening parenthesis, a separator or an operator.
    size_t before = start;
    while (before > 0 && text[before - 1] == ' ') --before;
    if (before == 0) return false;
    static const std::string kOpeners = "=(,;+-*/^&<>:";
    if (kOpeners.find(text[before - 1]) == std::string::npos) return false;

    // After it: nothing that would run on into the address ("=(|B2)").
    if (end < text.size()) {
        const unsigned char next = static_cast<unsigned char>(text[end]);
        if (std::isalnum(next) || next == '$' || next == '_' || next == '.' ||
            next == '!' || next == '(' || next == '"') {
            return false;
        }
    }
    return true;
}

std::string CheckSheetName(const std::string& name) {
    if (name.empty()) return "A sheet name cannot be empty.";
    // Characters, not bytes: count UTF-8 lead bytes.
    size_t length = 0;
    for (unsigned char c : name) {
        if ((c & 0xC0) != 0x80) ++length;
    }
    if (length > 31) return "A sheet name can be at most 31 characters long.";
    if (name.find_first_of("[]:*?/\\") != std::string::npos) {
        return "A sheet name cannot contain any of [ ] : * ? / \\";
    }
    if (name.front() == '\'' || name.back() == '\'') {
        return "A sheet name cannot start or end with an apostrophe.";
    }
    return "";
}

namespace {

bool SameNameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool IsSheetNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

// How a formula writes the sheet `name`: bare when it is a plain word,
// otherwise in apostrophes with any apostrophe in it doubled.
std::string FormulaSheetName(const std::string& name) {
    bool plain = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0]));
    for (char c : name) plain = plain && IsSheetNameChar(c);
    if (plain) return name;
    std::string quoted = "'";
    for (char c : name) {
        quoted += c;
        if (c == '\'') quoted += '\'';
    }
    return quoted + "\'";
}

} // namespace

namespace {

// Reads a sheet qualifier at formula[i] - 'Quoted name' or a bare name -
// followed by '.' or '!'. On success `name` is the sheet's name and `sep` the
// index of the separator. A bare name counts only before a cell address, so
// "2016.5" is a number, not sheet 2016.
bool ReadSheetQualifier(const std::string& formula, size_t i, std::string& name, size_t& sep) {
    name.clear();
    if (i >= formula.size()) return false;
    size_t j = i;
    if (formula[i] == '\'') {
        ++j;
        bool closed = false;
        while (j < formula.size()) {
            if (formula[j] == '\'') {
                if (j + 1 < formula.size() && formula[j + 1] == '\'') { name += '\''; j += 2; continue; }
                closed = true;
                ++j;
                break;
            }
            name += formula[j++];
        }
        if (!closed) return false;
    } else {
        if (!IsSheetNameChar(formula[i]) || (i > 0 && IsSheetNameChar(formula[i - 1]))) return false;
        while (j < formula.size() && IsSheetNameChar(formula[j])) ++j;
        name = formula.substr(i, j - i);
        const bool addressFollows = j + 1 < formula.size() &&
            (formula[j + 1] == '$' || std::isalpha(static_cast<unsigned char>(formula[j + 1])));
        if (!addressFollows) return false;
    }
    if (j >= formula.size() || (formula[j] != '.' && formula[j] != '!')) return false;
    sep = j;
    return true;
}

// The end of the cell address (or A1:B3 range) that starts at formula[i];
// the far end of a range may repeat the sheet (Sales.A1:Sales.B3).
size_t ReferenceEnd(const std::string& formula, size_t i) {
    auto addressEnd = [&](size_t k) {
        while (k < formula.size() && (formula[k] == '$' || std::isalnum(static_cast<unsigned char>(formula[k])))) ++k;
        return k;
    };
    size_t end = addressEnd(i);
    if (end < formula.size() && formula[end] == ':') {
        size_t k = end + 1;
        std::string name;
        size_t sep = 0;
        if (ReadSheetQualifier(formula, k, name, sep)) k = sep + 1;
        const size_t spillEnd = addressEnd(k);
        if (spillEnd > k) end = spillEnd;
    }
    return end;
}

// Shared by RenameSheetInFormula and RemoveSheetFromFormula: walks the
// formula outside "text", and for every reference qualified with sheet
// `sheetName` writes rewrite(qualifierStart, separator, out) and continues
// at the index it returns.
template <typename Rewrite>
std::string RewriteSheetReferences(const std::string& formula, const std::string& sheetName, Rewrite rewrite) {
    std::string out;
    out.reserve(formula.size());
    size_t i = 0;
    while (i < formula.size()) {
        const char c = formula[i];
        if (c == '"') {   // a text literal is copied as it is
            size_t close = formula.find('"', i + 1);
            close = close == std::string::npos ? formula.size() : close + 1;
            out.append(formula, i, close - i);
            i = close;
            continue;
        }
        std::string name;
        size_t sep = 0;
        if (ReadSheetQualifier(formula, i, name, sep)) {
            if (SameNameIgnoringCase(name, sheetName)) {
                i = rewrite(i, sep, out);
            } else {
                out.append(formula, i, sep - i);
                i = sep;
            }
            continue;
        }
        if (c == '\'' || IsSheetNameChar(c)) {   // a name or word that is no reference
            size_t j = i + 1;
            if (c == '\'') {
                while (j < formula.size() && formula[j] != '\'') ++j;
                if (j < formula.size()) ++j;
            } else {
                while (j < formula.size() && IsSheetNameChar(formula[j])) ++j;
            }
            out.append(formula, i, j - i);
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

} // namespace

std::string RenameSheetInFormula(const std::string& formula, const std::string& oldName,
                                 const std::string& newName) {
    if (oldName.empty() || newName.empty()) return formula;
    const std::string replacement = FormulaSheetName(newName);
    return RewriteSheetReferences(formula, oldName, [&](size_t, size_t sep, std::string& out) {
        out += replacement;
        return sep;   // the separator and the address follow as they were
    });
}

std::string RemoveSheetFromFormula(const std::string& formula, const std::string& sheetName) {
    if (sheetName.empty()) return formula;
    return RewriteSheetReferences(formula, sheetName, [&](size_t, size_t sep, std::string& out) {
        out += "#REF!";
        return ReferenceEnd(formula, sep + 1);
    });
}

namespace {

// One A1 address in a formula: [$]LETTERS[$]DIGITS.
struct FormulaAddress {
    bool colAbs = false;
    bool rowAbs = false;
    int col = 0;   // 0-based
    int row = 0;
    size_t end = 0;   // one past it in the formula
};

bool ReadFormulaAddress(const std::string& f, size_t i, FormulaAddress& a) {
    const size_t n = f.size();
    size_t p = i;
    a.colAbs = p < n && f[p] == '$';
    if (a.colAbs) ++p;
    const size_t letters = p;
    while (p < n && std::isalpha(static_cast<unsigned char>(f[p])) && p - letters < 4) ++p;
    const size_t letterCount = p - letters;
    a.rowAbs = p < n && f[p] == '$';
    if (a.rowAbs) ++p;
    const size_t digits = p;
    while (p < n && std::isdigit(static_cast<unsigned char>(f[p]))) ++p;
    const size_t digitCount = p - digits;
    if (letterCount < 1 || letterCount > 3 || digitCount < 1 || digitCount > 7) return false;
    // Not the start of a longer name or a call (LOG10( ).
    if (p < n && (IsSheetNameChar(f[p]) || f[p] == '(' || f[p] == '$' || f[p] == '.' || f[p] == '!')) return false;
    std::string upper = f.substr(letters, letterCount);
    for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    a.col = CellAddress::LetterToColumn(upper);
    a.row = std::stoi(f.substr(digits, digitCount)) - 1;
    a.end = p;
    return true;
}

std::string WriteFormulaAddress(const FormulaAddress& a) {
    std::string out;
    if (a.colAbs) out += '$';
    out += CellAddress::ColumnToLetter(a.col);
    if (a.rowAbs) out += '$';
    out += std::to_string(a.row + 1);
    return out;
}

} // namespace

std::string MoveFormulaReferencesForRowsColumns(const std::string& formula, const std::string& sheetName,
                                                bool onThatSheet, bool columns, int at, int count) {
    if (count == 0 || formula.empty()) return formula;
    const int limit = columns ? SpreadsheetLimits::MaxColumns : SpreadsheetLimits::MaxRows;
    const int deleted = count < 0 ? -count : 0;
    auto indexOf = [columns](FormulaAddress& a) -> int& { return columns ? a.col : a.row; };

    // One end of a reference: moved, or -1 when it falls in deleted rows.
    // `isEnd` says which end of a range it is, which decides where it goes
    // when its own row is deleted (a range shrinks to what is left).
    auto moveIndex = [&](int index, bool inRange, bool isEnd) -> int {
        if (count > 0) return index >= at ? index + count : index;
        if (index < at) return index;
        if (index >= at + deleted) return index - deleted;
        if (!inRange) return -1;
        return isEnd ? at - 1 : at;   // clipped to the rows that remain
    };

    // Rewrites one reference (cell or range) or answers false for #REF!.
    auto moveReference = [&](FormulaAddress& first, FormulaAddress* last) -> bool {
        if (!last) {
            const int moved = moveIndex(indexOf(first), false, false);
            if (moved < 0 || moved >= limit) return false;
            indexOf(first) = moved;
            return true;
        }
        int lo = indexOf(first), hi = indexOf(*last);
        const bool swapped = lo > hi;
        if (swapped) std::swap(lo, hi);
        const int newLo = moveIndex(lo, true, false);
        const int newHi = moveIndex(hi, true, true);
        if (newHi < newLo || newLo < 0 || newHi >= limit) return false;   // all of it deleted
        indexOf(swapped ? *last : first) = newLo;
        indexOf(swapped ? first : *last) = newHi;
        return true;
    };

    std::string out;
    out.reserve(formula.size() + 8);
    const size_t n = formula.size();
    size_t i = 0;
    while (i < n) {
        const char c = formula[i];
        if (c == '"') {   // a text literal is copied as it is
            size_t close = formula.find('"', i + 1);
            close = close == std::string::npos ? n : close + 1;
            out.append(formula, i, close - i);
            i = close;
            continue;
        }

        // A reference starts here: qualified (Sales.A1) or not (A1), and may
        // run on to a range (A1:B3, Sales.A1:Sales.B3).
        std::string name;
        size_t sep = 0;
        size_t addressAt = i;
        bool qualified = false;
        if (ReadSheetQualifier(formula, i, name, sep)) {
            qualified = true;
            addressAt = sep + 1;
        } else if (i > 0 && (IsSheetNameChar(formula[i - 1]) || formula[i - 1] == '.' || formula[i - 1] == '!')) {
            out += c;
            ++i;
            continue;
        }
        FormulaAddress first;
        if (!ReadFormulaAddress(formula, addressAt, first)) {
            if (qualified) {   // a qualifier before something that is no address
                out.append(formula, i, addressAt - i);
                i = addressAt;
                continue;
            }
            if (IsSheetNameChar(c) || c == '$') {   // a name or word: copied whole
                size_t j = i + 1;
                while (j < n && (IsSheetNameChar(formula[j]) || formula[j] == '.' || formula[j] == '$')) ++j;
                out.append(formula, i, j - i);
                i = j;
            } else {
                out += c;
                ++i;
            }
            continue;
        }

        size_t end = first.end;
        size_t lastAt = std::string::npos;   // where the far end's address starts
        FormulaAddress last;
        if (end < n && formula[end] == ':') {
            size_t k = end + 1;
            std::string farName;
            size_t farSep = 0;
            if (ReadSheetQualifier(formula, k, farName, farSep)) k = farSep + 1;
            if (ReadFormulaAddress(formula, k, last)) {
                lastAt = k;
                end = last.end;
            }
        }

        const bool applies = qualified ? SameNameIgnoringCase(name, sheetName) : onThatSheet;
        if (!applies) {
            out.append(formula, i, end - i);
            i = end;
            continue;
        }
        const bool isRange = lastAt != std::string::npos;
        if (!moveReference(first, isRange ? &last : nullptr)) {
            out += "#REF!";
            i = end;
            continue;
        }
        out.append(formula, i, addressAt - i);            // the qualifier, as written
        out += WriteFormulaAddress(first);
        if (isRange) {
            out.append(formula, first.end, lastAt - first.end);   // ':' and any far qualifier
            out += WriteFormulaAddress(last);
        }
        i = end;
    }
    return out;
}

void SpreadsheetSheet::MoveFormulaReferences(const std::string& changedSheet, bool columns, int at, int count) {
    const bool own = SameNameIgnoringCase(changedSheet, name_);
    for (auto& [coord, cell] : cells_) {
        if (!cell || !cell->HasFormula()) continue;
        const std::string& text = cell->GetFormulaText();
        std::string moved = MoveFormulaReferencesForRowsColumns(text, changedSheet, own, columns, at, count);
        if (moved != text) cell->SetFormula(moved);
    }
    // The charts' ranges on the changed sheet move with its cells.
    MoveSpreadsheetChartRanges(charts_, changedSheet, columns, at, count);
}

int SpreadsheetSheet::GetAutoFillDownEnd(const CellRange& source) const {
    auto filled = [&](int row, int col) {
        if (col < 0 || col >= SpreadsheetLimits::MaxColumns || row >= SpreadsheetLimits::MaxRows) return false;
        const SpreadsheetCell* cell = GetCellIfExists(row, col);
        return cell && !cell->IsEmpty();
    };
    const int below = source.end.row + 1;
    for (int col : { source.start.col - 1, source.end.col + 1 }) {
        if (!filled(below, col)) continue;
        int last = below;
        while (filled(last + 1, col)) ++last;
        return last;
    }
    return -1;
}

void SpreadsheetSheet::AutoFill(const CellRange& source, const CellRange& destination,
                                AutoFillMode mode) {
    const int srcRows = source.RowCount();
    const int srcCols = source.ColCount();
    if (srcRows <= 0 || srcCols <= 0) return;

    // Filling runs along columns when the destination keeps the source's
    // columns (dragged down or up), along rows otherwise.
    const bool vertical = destination.start.col == source.start.col &&
                          destination.end.col == source.end.col;
    const int lines = vertical ? srcCols : srcRows;      // columns (or rows) filled
    const int length = vertical ? srcRows : srcCols;     // source cells per line

    for (int line = 0; line < lines; ++line) {
        auto sourceAt = [&](int index) -> const SpreadsheetCell* {
            return vertical ? GetCellIfExists(source.start.row + index, source.start.col + line)
                            : GetCellIfExists(source.start.row + line, source.start.col + index);
        };

        // A line of two or more plain numbers continues as a linear series.
        bool series = length >= 2 && mode == AutoFillMode::Series;
        for (int k = 0; k < length && series; ++k) {
            const SpreadsheetCell* cell = sourceAt(k);
            series = cell && !cell->HasFormula() && cell->IsNumeric();
        }
        double first = 0, step = 0;
        if (series) {
            first = sourceAt(0)->GetNumber();
            step = (sourceAt(length - 1)->GetNumber() - first) / (length - 1);
        }

        // A single text ending in a number counts on from it.
        std::string textPrefix;
        long long textNumber = 0;
        bool counting = false;
        if (length == 1 && mode == AutoFillMode::Series) {
            const SpreadsheetCell* cell = sourceAt(0);
            counting = cell && !cell->HasFormula() && cell->GetValueType() == CellValueType::Text &&
                       SplitTrailingNumber(cell->GetText(), textPrefix, textNumber);
        }

        const int from = vertical ? destination.start.row : destination.start.col;
        const int to = vertical ? destination.end.row : destination.end.col;
        const int origin = vertical ? source.start.row : source.start.col;
        for (int pos = from; pos <= to; ++pos) {
            const int row = vertical ? pos : source.start.row + line;
            const int col = vertical ? source.start.col + line : pos;
            if (source.Contains(row, col)) continue;   // the source stays as it is

            const int offset = pos - origin;                         // may be negative
            const int index = ((offset % length) + length) % length;
            const SpreadsheetCell* src = sourceAt(index);
            if (!src) { DeleteCell(row, col); continue; }

            SpreadsheetCell copy = src->Clone();
            copy.SetPosition(row, col);
            if (src->HasFormula()) {
                const int moved = offset - index;
                copy.SetFormula(ShiftFormulaReferences(src->GetFormulaText(),
                                                       vertical ? moved : 0, vertical ? 0 : moved));
            } else if (series) {
                const double value = first + step * offset;
                if (src->GetValueType() == CellValueType::Currency) {
                    copy.SetValue(CellValueVariant(CurrencyValue(value, src->GetCurrency().currencyCode)),
                                  CellValueType::Currency);
                } else {
                    copy.SetValue(CellValueVariant(value), src->GetValueType());
                }
            } else if (counting) {
                copy.SetText(textPrefix + std::to_string(std::max(0LL, textNumber + offset)));
            }
            GetOrCreateCell(row, col) = copy;
        }
    }

    InvalidateUsedRange();
    NotifyRangeChange(destination);
}

void SpreadsheetSheet::FillRange(const CellRange& range, const SpreadsheetCell& value) {
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            SpreadsheetCell& target = GetOrCreateCell(row, col);
            SpreadsheetCell copy = value.Clone();
            copy.SetPosition(row, col);
            target = copy;
        }
    }

    InvalidateUsedRange();
    NotifyRangeChange(range);
}

// ============================================================================
// FIND / REPLACE
// ============================================================================

namespace {

bool CellMatches(const SpreadsheetCell* cell, const std::string& searchText,
                 const SheetFindOptions& options) {
    if (!cell) return false;

    std::string haystack = options.searchFormulas && cell->HasFormula()
                               ? cell->GetFormulaText()
                               : cell->GetDisplayValue();
    std::string needle = searchText;

    if (!options.matchCase) {
        for (auto& ch : haystack) ch = static_cast<char>(::tolower(ch));
        for (auto& ch : needle) ch = static_cast<char>(::tolower(ch));
    }

    if (options.matchEntireCell) {
        return haystack == needle;
    }
    return haystack.find(needle) != std::string::npos;
}

} // namespace

CellAddress SpreadsheetSheet::FindNext(const std::string& searchText,
                                       const CellAddress& startFrom,
                                       const SheetFindOptions& options) const {
    CellRange used = GetUsedRange();
    if (used.start.row > used.end.row || used.start.col > used.end.col) {
        return CellAddress(-1, -1);
    }

    auto check = [&](int row, int col) -> bool {
        return CellMatches(GetCellIfExists(row, col), searchText, options);
    };

    // Walk the used range starting just after startFrom, wrapping around.
    if (options.searchByRows) {
        int totalCols = used.end.col - used.start.col + 1;
        int startLinear = (startFrom.row - used.start.row) * totalCols +
                          (startFrom.col - used.start.col);
        int totalCells = (used.end.row - used.start.row + 1) * totalCols;

        for (int i = 1; i <= totalCells; ++i) {
            int linear = ((startLinear + i) % totalCells + totalCells) % totalCells;
            int row = used.start.row + linear / totalCols;
            int col = used.start.col + linear % totalCols;
            if (check(row, col)) return CellAddress(row, col);
        }
    } else {
        int totalRows = used.end.row - used.start.row + 1;
        int startLinear = (startFrom.col - used.start.col) * totalRows +
                          (startFrom.row - used.start.row);
        int totalCells = (used.end.col - used.start.col + 1) * totalRows;

        for (int i = 1; i <= totalCells; ++i) {
            int linear = ((startLinear + i) % totalCells + totalCells) % totalCells;
            int col = used.start.col + linear / totalRows;
            int row = used.start.row + linear % totalRows;
            if (check(row, col)) return CellAddress(row, col);
        }
    }

    return CellAddress(-1, -1);
}

std::vector<CellAddress> SpreadsheetSheet::FindAll(const std::string& searchText,
                                                   const SheetFindOptions& options) const {
    std::vector<CellAddress> results;
    CellRange used = GetUsedRange();
    if (used.start.row > used.end.row || used.start.col > used.end.col) {
        return results;
    }

    if (options.searchByRows) {
        for (int row = used.start.row; row <= used.end.row; ++row) {
            for (int col = used.start.col; col <= used.end.col; ++col) {
                if (CellMatches(GetCellIfExists(row, col), searchText, options)) {
                    results.emplace_back(row, col);
                }
            }
        }
    } else {
        for (int col = used.start.col; col <= used.end.col; ++col) {
            for (int row = used.start.row; row <= used.end.row; ++row) {
                if (CellMatches(GetCellIfExists(row, col), searchText, options)) {
                    results.emplace_back(row, col);
                }
            }
        }
    }

    return results;
}

int SpreadsheetSheet::ReplaceAll(const std::string& searchText,
                                 const std::string& replaceText,
                                 const SheetFindOptions& options) {
    if (searchText.empty()) return 0;

    std::vector<CellAddress> matches = FindAll(searchText, options);
    int replaced = 0;

    for (const auto& addr : matches) {
        SpreadsheetCell* cell = GetCellIfExists(addr.row, addr.col);
        if (!cell) continue;

        bool inFormula = options.searchFormulas && cell->HasFormula();
        std::string original = inFormula ? cell->GetFormulaText() : cell->GetDisplayValue();

        // Case-insensitive replacement requires manual scanning.
        std::string result;
        if (options.matchEntireCell) {
            result = replaceText;
        } else if (options.matchCase) {
            size_t pos = 0;
            while ((pos = original.find(searchText, pos)) != std::string::npos) {
                original.replace(pos, searchText.length(), replaceText);
                pos += replaceText.length();
            }
            result = original;
        } else {
            std::string lowerHay = original;
            std::string lowerNeedle = searchText;
            for (auto& ch : lowerHay) ch = static_cast<char>(::tolower(ch));
            for (auto& ch : lowerNeedle) ch = static_cast<char>(::tolower(ch));

            size_t pos = 0;
            size_t searchPos = 0;
            while ((searchPos = lowerHay.find(lowerNeedle, searchPos)) != std::string::npos) {
                result.append(original, pos, searchPos - pos);
                result.append(replaceText);
                searchPos += lowerNeedle.length();
                pos = searchPos;
            }
            result.append(original, pos, std::string::npos);
        }

        if (inFormula) {
            cell->SetFormula(result);
        } else {
            cell->SetValueFromString(result);
        }
        NotifyCellChange(addr.row, addr.col);
        ++replaced;
    }

    return replaced;
}

// ============================================================================
// CLONE
// ============================================================================

std::unique_ptr<SpreadsheetSheet> SpreadsheetSheet::CloneKeepingId() const {
    auto copy = Clone();
    copy->id_ = id_;
    return copy;
}

std::unique_ptr<SpreadsheetSheet> SpreadsheetSheet::Clone(const std::string& newName) const {
    auto clone = std::make_unique<SpreadsheetSheet>(
        newName.empty() ? name_ : newName, index_);

    // Deep-copy cells.
    for (const auto& [coord, cellPtr] : cells_) {
        if (cellPtr) {
            clone->cells_[coord] = std::make_unique<SpreadsheetCell>(*cellPtr);
        }
    }

    clone->tabColor_ = tabColor_;
    clone->visible_ = visible_;
    clone->selected_ = selected_;
    clone->columns_ = columns_;
    clone->defaultColumnWidth_ = defaultColumnWidth_;
    clone->rows_ = rows_;
    clone->defaultRowHeight_ = defaultRowHeight_;
    clone->mergedCells_ = mergedCells_;
    clone->freezePanes_ = freezePanes_;
    clone->selection_ = selection_;
    clone->conditionalFormats_ = conditionalFormats_;
    clone->validations_ = validations_;
    clone->namedRanges_ = namedRanges_;
    clone->printSettings_ = printSettings_;
    clone->protection_ = protection_;
    clone->defaultCellStyle_ = defaultCellStyle_;
    clone->scrollRow_ = scrollRow_;
    clone->scrollCol_ = scrollCol_;
    clone->zoomLevel_ = zoomLevel_;
    clone->charts_ = charts_;

    if (autoFilter_) {
        clone->autoFilter_ = std::make_unique<AutoFilter>(*autoFilter_);
    }

    clone->InvalidateUsedRange();
    return clone;
}

// ============================================================================
// PRIVATE: SHIFT CELL REFERENCES
// ============================================================================

void SpreadsheetSheet::ShiftCellReferences(int startRow, int startCol, int rowDelta, int colDelta) {
    // The charts standing on the sheet move with the cells under them.
    if (colDelta != 0) MoveSpreadsheetChartAnchors(charts_, true, colDelta < 0 ? startCol + colDelta : startCol, colDelta);
    if (rowDelta != 0) MoveSpreadsheetChartAnchors(charts_, false, rowDelta < 0 ? startRow + rowDelta : startRow, rowDelta);

    // Adjust references stored in named ranges, merges, validations,
    // conditional formats and the print area when rows/columns shift.

    auto shiftAddr = [&](CellAddress& addr) {
        if (rowDelta != 0 && addr.row >= startRow) {
            addr.row = std::max(0, addr.row + rowDelta);
        }
        if (colDelta != 0 && addr.col >= startCol) {
            addr.col = std::max(0, addr.col + colDelta);
        }
    };

    auto shiftRange = [&](CellRange& range) {
        shiftAddr(range.start);
        shiftAddr(range.end);
        range.Normalize();
    };

    for (auto& [name, nr] : namedRanges_) {
        shiftRange(nr.range);
    }

    for (auto& merge : mergedCells_) {
        shiftRange(merge.range);
    }

    for (auto& [range, validation] : validations_) {
        shiftRange(range);
    }

    for (auto& format : conditionalFormats_) {
        shiftRange(format.range);
    }

    if (autoFilter_) {
        shiftRange(autoFilter_->range);
    }

    shiftRange(printSettings_.printArea);
}

} // namespace UltraCanvas
