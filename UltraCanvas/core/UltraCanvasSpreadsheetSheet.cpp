// core/UltraCanvasSpreadsheetSheet.cpp
// Spreadsheet worksheet implementation (non-inline members)
// Version: 1.0.0
// Last Modified: 2026-05-30
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetSheet.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <set>

namespace UltraCanvas {

// ============================================================================
// COLUMN / ROW AUTO-FIT
// ============================================================================

void SpreadsheetSheet::AutoFitColumnWidth(int col) {
    int maxWidth = defaultColumnWidth_;

    for (const auto& [coord, cellPtr] : cells_) {
        if (coord.second == col && cellPtr && !cellPtr->IsEmpty()) {
            std::string text = cellPtr->GetDisplayValue();
            int textWidth = static_cast<int>(text.length() * 8) + 10;
            maxWidth = std::max(maxWidth, textWidth);
        }
    }

    SetColumnWidth(col, std::min(maxWidth, 500));
}

void SpreadsheetSheet::AutoFitColumnWidthRange(int startCol, int endCol) {
    for (int col = startCol; col <= endCol; ++col) {
        AutoFitColumnWidth(col);
    }
}

void SpreadsheetSheet::AutoFitRowHeight(int row) {
    // Without text metrics, fall back to the default height.
    SetRowHeight(row, defaultRowHeight_);
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
                        try { cmpVal = std::stod(filter.values.front()); } catch (...) { cmpVal = 0.0; }
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

std::optional<CellStyle> SpreadsheetSheet::GetConditionalStyle(int row, int col) const {
    const SpreadsheetCell* cell = GetCellIfExists(row, col);

    for (const auto& format : conditionalFormats_) {
        if (!format.range.Contains(row, col)) continue;

        for (const auto& rule : format.rules) {
            bool matched = false;

            std::string text = cell ? cell->GetDisplayValue() : "";
            double value = cell ? cell->GetNumber() : 0.0;

            auto parseNum = [](const std::string& s) -> double {
                try { return std::stod(s); } catch (...) { return 0.0; }
            };

            switch (rule.type) {
                case ConditionalFormatType::CellValue: {
                    double v1 = parseNum(rule.formula1);
                    double v2 = parseNum(rule.formula2);
                    switch (rule.op) {
                        case ValidationOperator::Equal:          matched = (value == v1); break;
                        case ValidationOperator::NotEqual:       matched = (value != v1); break;
                        case ValidationOperator::GreaterThan:    matched = (value > v1); break;
                        case ValidationOperator::LessThan:       matched = (value < v1); break;
                        case ValidationOperator::GreaterOrEqual: matched = (value >= v1); break;
                        case ValidationOperator::LessOrEqual:    matched = (value <= v1); break;
                        case ValidationOperator::Between:        matched = (value >= v1 && value <= v2); break;
                        case ValidationOperator::NotBetween:     matched = (value < v1 || value > v2); break;
                    }
                    break;
                }
                case ConditionalFormatType::ContainsText:
                    matched = !rule.formula1.empty() && text.find(rule.formula1) != std::string::npos;
                    break;
                case ConditionalFormatType::NotContainsText:
                    matched = rule.formula1.empty() || text.find(rule.formula1) == std::string::npos;
                    break;
                case ConditionalFormatType::BeginsWithText:
                    matched = !rule.formula1.empty() && text.rfind(rule.formula1, 0) == 0;
                    break;
                case ConditionalFormatType::EndsWithText:
                    matched = !rule.formula1.empty() && text.size() >= rule.formula1.size() &&
                              text.compare(text.size() - rule.formula1.size(), rule.formula1.size(), rule.formula1) == 0;
                    break;
                case ConditionalFormatType::ContainsBlanks:
                    matched = text.empty();
                    break;
                case ConditionalFormatType::NotContainsBlanks:
                    matched = !text.empty();
                    break;
                case ConditionalFormatType::ContainsErrors:
                    matched = cell && cell->HasError();
                    break;
                case ConditionalFormatType::NotContainsErrors:
                    matched = !cell || !cell->HasError();
                    break;
                default:
                    matched = false;
                    break;
            }

            if (matched) {
                return rule.style;
            }
        }
    }

    return std::nullopt;
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

bool SpreadsheetSheet::ValidateCell(int row, int col, std::string* errorMessage) const {
    const DataValidation* validation = GetValidation(row, col);
    if (!validation || validation->type == ValidationType::Any) {
        return true;
    }

    const SpreadsheetCell* cell = GetCellIfExists(row, col);
    bool isBlank = !cell || cell->IsEmpty();

    if (isBlank) {
        if (validation->allowBlank) return true;
        if (errorMessage) {
            *errorMessage = validation->errorMessage.empty() ? "Value required" : validation->errorMessage;
        }
        return false;
    }

    std::string text = cell->GetDisplayValue();
    double value = cell->GetNumber();

    auto parseNum = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };

    bool valid = true;

    switch (validation->type) {
        case ValidationType::List: {
            // formula1 holds a comma-separated list of allowed values.
            valid = false;
            std::string source = validation->formula1;
            size_t start = 0;
            while (start <= source.size()) {
                size_t comma = source.find(',', start);
                std::string item = source.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                // Trim surrounding whitespace.
                size_t a = item.find_first_not_of(" \t");
                size_t b = item.find_last_not_of(" \t");
                if (a != std::string::npos) item = item.substr(a, b - a + 1);
                else item.clear();
                if (item == text) { valid = true; break; }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            break;
        }
        case ValidationType::WholeNumber:
        case ValidationType::Decimal:
        case ValidationType::Date:
        case ValidationType::Time:
        case ValidationType::TextLength: {
            double subject = value;
            if (validation->type == ValidationType::TextLength) {
                subject = static_cast<double>(text.length());
            }
            double v1 = parseNum(validation->formula1);
            double v2 = parseNum(validation->formula2);
            switch (validation->op) {
                case ValidationOperator::Between:        valid = (subject >= v1 && subject <= v2); break;
                case ValidationOperator::NotBetween:     valid = (subject < v1 || subject > v2); break;
                case ValidationOperator::Equal:          valid = (subject == v1); break;
                case ValidationOperator::NotEqual:       valid = (subject != v1); break;
                case ValidationOperator::GreaterThan:    valid = (subject > v1); break;
                case ValidationOperator::LessThan:       valid = (subject < v1); break;
                case ValidationOperator::GreaterOrEqual: valid = (subject >= v1); break;
                case ValidationOperator::LessOrEqual:    valid = (subject <= v1); break;
            }
            if (validation->type == ValidationType::WholeNumber && value != std::floor(value)) {
                valid = false;
            }
            break;
        }
        case ValidationType::Custom:
            // Custom formula evaluation requires the formula engine; accept.
            valid = true;
            break;
        default:
            valid = true;
            break;
    }

    if (!valid && errorMessage) {
        *errorMessage = validation->errorMessage.empty() ? "Invalid value" : validation->errorMessage;
    }
    return valid;
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
                                  const PasteOptions& options) {
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
                } else if (pt == PasteType::Formulas ||
                           pt == PasteType::FormulasAndNumberFormats) {
                    // Paste the formula text when present, otherwise the value.
                    if (source.HasFormula()) {
                        target.SetFormula(source.GetFormulaText());
                    } else {
                        target.SetValue(source.GetRawValue(), source.GetValueType());
                    }
                } else {
                    // Values paste: paste the computed value, not the formula.
                    target.SetValue(source.GetRawValue(), source.GetValueType());
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

void SpreadsheetSheet::AutoFill(const CellRange& source, const CellRange& destination) {
    // Simple copy-fill: repeat the source pattern across the destination.
    int srcRows = source.RowCount();
    int srcCols = source.ColCount();
    if (srcRows <= 0 || srcCols <= 0) return;

    for (int row = destination.start.row; row <= destination.end.row; ++row) {
        for (int col = destination.start.col; col <= destination.end.col; ++col) {
            if (source.Contains(row, col)) continue;  // Don't overwrite source.

            int sr = source.start.row + ((row - destination.start.row) % srcRows);
            int sc = source.start.col + ((col - destination.start.col) % srcCols);

            if (auto* src = GetCellIfExists(sr, sc)) {
                SpreadsheetCell& target = GetOrCreateCell(row, col);
                SpreadsheetCell copy = src->Clone();
                copy.SetPosition(row, col);
                target = copy;
            } else {
                DeleteCell(row, col);
            }
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
