// core/UltraCanvasSpreadsheetPivot.cpp
// The pivot model (UltraCanvasSpreadsheetPivot.h): a source block read into
// records, each field's values made items (grouped where the field says),
// the records summed up for every row and column of the table, the table
// laid out as cells and written onto a sheet.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasTextUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace UltraCanvas {

namespace {

bool IsFilled(const SpreadsheetSheet& sheet, int row, int col) {
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    return cell && !cell->IsEmpty();
}

// A block of one cell grown to the cells in use around it.
CellRange GrowToRegion(const SpreadsheetSheet& sheet, CellRange range) {
    for (int pass = 0; pass < 100000; ++pass) {
        bool grew = false;
        auto filledRow = [&](int row) {
            if (row < 0 || row >= SpreadsheetLimits::MaxRows) return false;
            for (int col = std::max(0, range.start.col - 1); col <= range.end.col + 1; ++col)
                if (IsFilled(sheet, row, col)) return true;
            return false;
        };
        auto filledCol = [&](int col) {
            if (col < 0 || col >= SpreadsheetLimits::MaxColumns) return false;
            for (int row = std::max(0, range.start.row - 1); row <= range.end.row + 1; ++row)
                if (IsFilled(sheet, row, col)) return true;
            return false;
        };
        if (filledRow(range.start.row - 1)) { --range.start.row; grew = true; }
        if (filledRow(range.end.row + 1)) { ++range.end.row; grew = true; }
        if (filledCol(range.start.col - 1)) { --range.start.col; grew = true; }
        if (filledCol(range.end.col + 1)) { ++range.end.col; grew = true; }
        if (!grew) break;
    }
    return range;
}

// The source block as it is used: whole columns or rows cut to the cells in use.
CellRange UsedPart(const SpreadsheetSheet& sheet, CellRange range) {
    range.Normalize();
    const CellRange used = sheet.GetUsedRange();
    if (used.IsValid() && used.end.row >= 0) {
        range.end.row = std::min(range.end.row, std::max(used.end.row, range.start.row));
        range.end.col = std::min(range.end.col, std::max(used.end.col, range.start.col));
    }
    return range;
}

bool IsDateCell(const SpreadsheetCell& cell) {
    const CellValueType type = cell.GetValueType();
    if (type == CellValueType::Date || type == CellValueType::DateTime) return true;
    const NumberFormatCategory category = cell.GetStyle().numberFormat.category;
    return category == NumberFormatCategory::Date || category == NumberFormatCategory::DateTime;
}

bool CellNumber(const SpreadsheetCell* cell, double& value) {
    if (!cell) return false;
    const CellValueVariant& raw = cell->GetRawValue();
    if (const double* number = std::get_if<double>(&raw)) {
        value = *number;
        return std::isfinite(value);
    }
    if (const CurrencyValue* money = std::get_if<CurrencyValue>(&raw)) {
        value = money->amount;
        return std::isfinite(value);
    }
    return false;
}

std::string MonthName(int month) {
    switch (month) {
        case 1: return TrC("month-short", "Jan");
        case 2: return TrC("month-short", "Feb");
        case 3: return TrC("month-short", "Mar");
        case 4: return TrC("month-short", "Apr");
        case 5: return TrC("month-short", "May");
        case 6: return TrC("month-short", "Jun");
        case 7: return TrC("month-short", "Jul");
        case 8: return TrC("month-short", "Aug");
        case 9: return TrC("month-short", "Sep");
        case 10: return TrC("month-short", "Oct");
        case 11: return TrC("month-short", "Nov");
        default: return TrC("month-short", "Dec");
    }
}

// A number as an item's label: as few digits as it needs. A label is for
// reading, so the reader's decimal point would be right - but the label is
// also what .ods names a member by, so it stays dot-decimal.
std::string NumberLabel(double value) {
    return FormatFloatClassic(value, 10);
}

std::string ItemKeyOfNumber(double value) {
    return FormatFloatRoundTrip(value);
}

// The ends of a number grouping that has automatic ones: the source's
// smallest and largest numbers in that column.
void NumberRangeEnds(const SpreadsheetSheet& source, const CellRange& range, int col, PivotGrouping& grouping) {
    if (!grouping.autoStart && !grouping.autoEnd) return;
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (int r = range.start.row + 1; r <= range.end.row; ++r) {
        double value = 0.0;
        if (CellNumber(source.GetCellIfExists(r, col), value)) {
            low = std::min(low, value);
            high = std::max(high, value);
        }
    }
    if (!std::isfinite(low)) return;
    if (grouping.autoStart) grouping.start = std::floor(low);
    if (grouping.autoEnd) grouping.end = high;
}

// What a summary has seen of its records.
struct Accumulator {
    long long count = 0;          // cells with anything in them
    long long numbers = 0;
    double sum = 0.0;
    double sumSquares = 0.0;
    double product = 1.0;
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();

    void Add(bool filled, bool isNumber, double value) {
        if (filled) ++count;
        if (!isNumber) return;
        ++numbers;
        sum += value;
        sumSquares += value * value;
        product *= value;
        low = std::min(low, value);
        high = std::max(high, value);
    }

    // The summary's result; false where it has none (an average of no numbers).
    bool Result(PivotSummary summary, double& out) const {
        const double n = static_cast<double>(numbers);
        auto variance = [&](bool sample) {
            const double mean = sum / n;
            const double squares = std::max(0.0, sumSquares - n * mean * mean);
            return squares / (sample ? n - 1.0 : n);
        };
        switch (summary) {
            case PivotSummary::Sum: out = sum; return true;
            case PivotSummary::Count: out = static_cast<double>(count); return true;
            case PivotSummary::CountNumbers: out = n; return true;
            case PivotSummary::Average: if (numbers == 0) return false; out = sum / n; return true;
            case PivotSummary::Max: if (numbers == 0) return false; out = high; return true;
            case PivotSummary::Min: if (numbers == 0) return false; out = low; return true;
            case PivotSummary::Product: if (numbers == 0) return false; out = product; return true;
            case PivotSummary::StdDev: if (numbers < 2) return false; out = std::sqrt(variance(true)); return true;
            case PivotSummary::StdDevP: if (numbers < 1) return false; out = std::sqrt(variance(false)); return true;
            case PivotSummary::Var: if (numbers < 2) return false; out = variance(true); return true;
            case PivotSummary::VarP: if (numbers < 1) return false; out = variance(false); return true;
        }
        return false;
    }
};

// Orders two items as a pivot does: numbers (and dates, groups) by value,
// before texts, which go without regard to case; a blank last.
bool ItemBefore(const PivotItem& a, const PivotItem& b) {
    if (a.blank != b.blank) return b.blank;
    if (a.isNumber != b.isNumber) return a.isNumber;
    if (a.isNumber) return a.sortNumber < b.sortNumber;
    std::string la = a.label, lb = b.label;
    for (char& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (la != lb) return la < lb;
    return a.label < b.label;
}

// The source read: each field's items, and each record's item per field
// and value per value field.
struct PivotData {
    CellRange range;
    std::vector<std::string> names;
    struct FieldItems {
        std::vector<PivotItem> items;          // in the field's order
        std::map<std::string, int> byKey;      // key -> index in `items`
        std::set<int> hidden;
    };
    // All the axis fields in one list: rows, then columns, then filters.
    std::vector<const PivotField*> fields;
    std::vector<FieldItems> fieldItems;
    struct Record {
        int sourceRow = 0;
        std::vector<int> items;                // per field in `fields`
        std::vector<char> filled;              // per value field
        std::vector<char> isNumber;
        std::vector<double> values;
    };
    std::vector<Record> records;
    int sourceRecords = 0;
};

void ReadPivotData(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source, PivotData& data) {
    data.range = UsedPart(source, pivot.sourceRange);
    data.names = SpreadsheetPivotFieldNames(source, data.range);
    for (const PivotField& f : pivot.rowFields) data.fields.push_back(&f);
    for (const PivotField& f : pivot.columnFields) data.fields.push_back(&f);
    for (const PivotField& f : pivot.filterFields) data.fields.push_back(&f);
    data.fieldItems.resize(data.fields.size());
    std::vector<PivotGrouping> groupings;
    for (const PivotField* f : data.fields) {
        PivotGrouping grouping = f->grouping;
        if (grouping.by == PivotGroupBy::NumberRange) {
            NumberRangeEnds(source, data.range, data.range.start.col + f->column, grouping);
        }
        groupings.push_back(grouping);
    }
    // Items in the order first met, sorted afterwards.
    std::vector<std::vector<PivotItem>> met(data.fields.size());
    std::vector<std::map<std::string, int>> metIndex(data.fields.size());
    std::vector<PivotData::Record> raw;
    const int columns = data.range.ColCount();
    for (int r = data.range.start.row + 1; r <= data.range.end.row; ++r) {
        bool any = false;
        for (int c = 0; c < columns && !any; ++c) any = IsFilled(source, r, data.range.start.col + c);
        if (!any) continue;   // an empty row of a block taken whole
        ++data.sourceRecords;
        PivotData::Record record;
        record.sourceRow = r;
        for (size_t f = 0; f < data.fields.size(); ++f) {
            const PivotItem item =
                SpreadsheetPivotCellItem(source, r, data.range.start.col + data.fields[f]->column, groupings[f]);
            auto it = metIndex[f].find(item.key);
            int index = 0;
            if (it == metIndex[f].end()) {
                index = static_cast<int>(met[f].size());
                metIndex[f][item.key] = index;
                met[f].push_back(item);
            } else {
                index = it->second;
            }
            record.items.push_back(index);
        }
        for (const PivotValueField& value : pivot.valueFields) {
            const SpreadsheetCell* cell = source.GetCellIfExists(r, data.range.start.col + value.column);
            double number = 0.0;
            const bool isNumber = CellNumber(cell, number);
            record.filled.push_back(cell && !cell->IsEmpty() ? 1 : 0);
            record.isNumber.push_back(isNumber ? 1 : 0);
            record.values.push_back(number);
        }
        raw.push_back(std::move(record));
    }
    // Sort each field's items and renumber the records' items.
    std::vector<std::vector<int>> renumber(data.fields.size());
    for (size_t f = 0; f < data.fields.size(); ++f) {
        std::vector<int> order(met[f].size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return ItemBefore(met[f][a], met[f][b]); });
        if (data.fields[f]->descending) {
            // Descending: the same order reversed, a blank still last.
            auto firstBlank = std::find_if(order.begin(), order.end(), [&](int i) { return met[f][i].blank; });
            std::reverse(order.begin(), firstBlank);
        }
        renumber[f].assign(met[f].size(), 0);
        PivotData::FieldItems& items = data.fieldItems[f];
        for (size_t i = 0; i < order.size(); ++i) {
            renumber[f][static_cast<size_t>(order[i])] = static_cast<int>(i);
            items.items.push_back(met[f][static_cast<size_t>(order[i])]);
            items.byKey[items.items.back().key] = static_cast<int>(i);
        }
        for (const std::string& key : data.fields[f]->hiddenItems) {
            auto it = items.byKey.find(key);
            if (it != items.byKey.end()) items.hidden.insert(it->second);
        }
    }
    for (PivotData::Record& record : raw) {
        bool shown = true;
        for (size_t f = 0; f < record.items.size(); ++f) {
            record.items[f] = renumber[f][static_cast<size_t>(record.items[f])];
            if (data.fieldItems[f].hidden.count(record.items[f])) shown = false;
        }
        if (shown) data.records.push_back(std::move(record));
    }
}

// The key of a path of items, for the maps below.
std::string PathKey(const std::vector<int>& items, size_t length) {
    std::string key;
    for (size_t i = 0; i < length && i < items.size(); ++i) {
        key += std::to_string(items[i]);
        key += ',';
    }
    return key;
}

} // namespace

// ============================================================================
// FIELDS AND ITEMS
// ============================================================================

std::vector<std::string> SpreadsheetPivotFieldNames(const SpreadsheetSheet& source, const CellRange& given) {
    CellRange range = given;
    range.Normalize();
    std::vector<std::string> names;
    for (int c = range.start.col; c <= range.end.col; ++c) {
        std::string name;
        if (const SpreadsheetCell* cell = source.GetCellIfExists(range.start.row, c)) name = cell->GetDisplayValue();
        if (name.empty()) name = TrF("Column {0}", CellAddress::ColumnToLetter(c));
        names.push_back(name);
    }
    return names;
}

SpreadsheetPivotTable MakeSpreadsheetPivot(const SpreadsheetSheet& source, const CellRange& given) {
    CellRange range = given;
    range.Normalize();
    if (range.IsSingleCell()) range = GrowToRegion(source, range);
    range = UsedPart(source, range);
    SpreadsheetPivotTable pivot;
    pivot.name = "PivotTable1";
    pivot.sourceSheet = source.GetName();
    pivot.sourceRange = range;
    pivot.fieldNames = SpreadsheetPivotFieldNames(source, range);
    return pivot;
}

PivotValueField MakePivotValueField(const SpreadsheetSheet& source, const CellRange& given, int column) {
    CellRange range = UsedPart(source, given);
    PivotValueField field;
    field.column = column;
    int numbers = 0, texts = 0;
    for (int r = range.start.row + 1; r <= range.end.row && numbers + texts < 200; ++r) {
        const SpreadsheetCell* cell = source.GetCellIfExists(r, range.start.col + column);
        if (!cell || cell->IsEmpty()) continue;
        double value = 0.0;
        if (CellNumber(cell, value)) ++numbers; else ++texts;
    }
    field.summary = texts > numbers ? PivotSummary::Count : PivotSummary::Sum;
    return field;
}

PivotItem SpreadsheetPivotCellItem(const SpreadsheetSheet& source, int row, int col, const PivotGrouping& grouping) {
    PivotItem item;
    const SpreadsheetCell* cell = source.GetCellIfExists(row, col);
    if (!cell || cell->IsEmpty()) {
        item.blank = true;
        item.label = Tr("(blank)");
        return item;
    }
    double number = 0.0;
    const bool isNumber = CellNumber(cell, number);
    if (isNumber && grouping.by != PivotGroupBy::NoGrouping) {
        const bool isDate = IsDateCell(*cell);
        if (isDate && (grouping.by == PivotGroupBy::Years || grouping.by == PivotGroupBy::Quarters ||
                       grouping.by == PivotGroupBy::Months)) {
            int year = 0, month = 1, day = 1;
            DateTimeValue(number).ToDate(year, month, day);
            item.isNumber = true;
            if (grouping.by == PivotGroupBy::Years) {
                item.sortNumber = year;
                item.label = std::to_string(year);
            } else if (grouping.by == PivotGroupBy::Quarters) {
                item.sortNumber = (month - 1) / 3 + 1;
                item.label = TrF("Q{0}", static_cast<int>(item.sortNumber));
            } else {
                item.sortNumber = month;
                item.label = MonthName(month);
            }
            item.key = std::to_string(static_cast<int>(item.sortNumber));
            return item;
        }
        if (grouping.by == PivotGroupBy::NumberRange && grouping.step > 0.0) {
            item.isNumber = true;
            if (number < grouping.start) {
                item.sortNumber = -std::numeric_limits<double>::infinity();
                item.key = "<";
                item.label = "<" + NumberLabel(grouping.start);
                return item;
            }
            if (number > grouping.end) {
                item.sortNumber = std::numeric_limits<double>::infinity();
                item.key = ">";
                item.label = ">" + NumberLabel(grouping.end);
                return item;
            }
            const double low = grouping.start + std::floor((number - grouping.start) / grouping.step) * grouping.step;
            item.sortNumber = low;
            item.key = ItemKeyOfNumber(low);
            const bool whole = std::floor(grouping.start) == grouping.start && std::floor(grouping.step) == grouping.step;
            item.label = NumberLabel(low) + "-" + NumberLabel(whole ? low + grouping.step - 1 : low + grouping.step);
            return item;
        }
    }
    if (isNumber) {
        item.isNumber = true;
        item.sortNumber = number;
        item.key = ItemKeyOfNumber(number);
        item.label = cell->GetDisplayValue();
        if (item.label.empty()) item.label = NumberLabel(number);
        return item;
    }
    item.label = cell->GetDisplayValue();
    item.key = item.label;
    return item;
}

std::vector<PivotItem> SpreadsheetPivotFieldItems(const SpreadsheetSheet& source, const CellRange& range,
                                                  const PivotField& field) {
    SpreadsheetPivotTable pivot;
    pivot.sourceRange = range;
    PivotField all = field;
    all.hiddenItems.clear();
    pivot.rowFields.push_back(all);
    PivotData data;
    ReadPivotData(pivot, source, data);
    return data.fieldItems.empty() ? std::vector<PivotItem>() : data.fieldItems.front().items;
}

std::string PivotSummaryName(PivotSummary summary) {
    switch (summary) {
        case PivotSummary::Sum: return Tr("Sum");
        case PivotSummary::Count: return Tr("Count");
        case PivotSummary::Average: return Tr("Average");
        case PivotSummary::Max: return Tr("Max");
        case PivotSummary::Min: return Tr("Min");
        case PivotSummary::Product: return TrC("pivot summary", "Product");
        case PivotSummary::CountNumbers: return Tr("Count numbers");
        case PivotSummary::StdDev: return Tr("StdDev");
        case PivotSummary::StdDevP: return Tr("StdDevP");
        case PivotSummary::Var: return Tr("Var");
        case PivotSummary::VarP: return Tr("VarP");
    }
    return Tr("Sum");
}

std::string PivotShowAsName(PivotShowAs showAs) {
    switch (showAs) {
        case PivotShowAs::Value: return Tr("Value");
        case PivotShowAs::PercentOfTotal: return Tr("% of grand total");
        case PivotShowAs::PercentOfRow: return Tr("% of row total");
        case PivotShowAs::PercentOfColumn: return Tr("% of column total");
    }
    return Tr("Value");
}

std::string PivotGroupByName(PivotGroupBy by) {
    switch (by) {
        case PivotGroupBy::NoGrouping: return Tr("No grouping");
        case PivotGroupBy::Years: return Tr("Years");
        case PivotGroupBy::Quarters: return Tr("Quarters");
        case PivotGroupBy::Months: return Tr("Months");
        case PivotGroupBy::NumberRange: return Tr("Number ranges");
    }
    return Tr("No grouping");
}

std::string PivotValueFieldCaption(const PivotValueField& field, const std::vector<std::string>& fieldNames) {
    if (!field.name.empty()) return field.name;
    const std::string name = field.column >= 0 && field.column < static_cast<int>(fieldNames.size())
                                 ? fieldNames[static_cast<size_t>(field.column)]
                                 : std::string();
    // "Sum of Sales": the summary's name, then the field's.
    return LocalisationDetail::Substitute(TrC("pivot value field", "{0} of {1}"), {PivotSummaryName(field.summary), name});
}

bool SpreadsheetPivotFieldsFit(const SpreadsheetPivotTable& pivot) {
    const int columns = pivot.sourceRange.ColCount();
    auto fits = [columns](int column) { return column >= 0 && column < columns; };
    for (const auto* list : {&pivot.rowFields, &pivot.columnFields, &pivot.filterFields}) {
        for (const PivotField& f : *list) if (!fits(f.column)) return false;
    }
    for (const PivotValueField& f : pivot.valueFields) if (!fits(f.column)) return false;
    return true;
}

void RenameSpreadsheetPivotSheet(std::vector<SpreadsheetPivotTable>& pivots, const std::string& oldName,
                                 const std::string& newName) {
    for (SpreadsheetPivotTable& pivot : pivots) {
        if (pivot.sourceSheet == oldName) pivot.sourceSheet = newName;
    }
}

std::string NextSpreadsheetPivotName(const UltraCanvasSpreadsheet& workbook) {
    std::set<std::string> taken;
    for (int s = 0; s < workbook.GetSheetCount(); ++s) {
        if (const SpreadsheetSheet* sheet = workbook.GetSheet(s)) {
            for (const SpreadsheetPivotTable& pivot : sheet->GetPivotTables()) taken.insert(pivot.name);
        }
    }
    for (int n = 1;; ++n) {
        const std::string name = "PivotTable" + std::to_string(n);
        if (!taken.count(name)) return name;
    }
}

// ============================================================================
// COMPUTING
// ============================================================================

namespace {

// The entries along one side of the table: each item path in the fields'
// order, a subtotal after each group (but the innermost field's), a grand
// total at the end; times the value fields where they run along this side.
void BuildEntries(const std::vector<const PivotField*>& fields, size_t firstField,
                  const std::map<std::string, std::vector<int>>& children, int valueFields, bool valuesHere,
                  bool grandTotal, std::vector<PivotOutput::Entry>& entries) {
    const size_t levels = fields.size();
    auto push = [&](const std::vector<int>& items, bool subtotal, bool grand) {
        if (valuesHere) {
            for (int v = 0; v < valueFields; ++v) entries.push_back({items, v, subtotal, grand});
        } else {
            entries.push_back({items, -1, subtotal, grand});
        }
    };
    if (levels == 0) {
        push({}, false, false);
        return;
    }
    std::function<void(std::vector<int>&)> walk = [&](std::vector<int>& prefix) {
        if (prefix.size() == levels) {
            push(prefix, false, false);
            return;
        }
        auto it = children.find(PathKey(prefix, prefix.size()));
        if (it == children.end()) return;
        for (int item : it->second) {
            prefix.push_back(item);
            walk(prefix);
            if (prefix.size() < levels && fields[prefix.size() - 1]->subtotals) push(prefix, true, false);
            prefix.pop_back();
        }
    };
    (void)firstField;
    std::vector<int> prefix;
    walk(prefix);
    if (grandTotal) push({}, false, true);
}

} // namespace

bool ComputeSpreadsheetPivot(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source, PivotOutput& out,
                             std::string* problem) {
    out = PivotOutput();
    if (!pivot.unsupportedReason.empty()) {
        if (problem) *problem = pivot.unsupportedReason;
        return false;
    }
    if (pivot.sourceRange.RowCount() < 2) {
        if (problem) *problem = Tr("The source needs a row of field names and at least one row of data.");
        return false;
    }
    if (!SpreadsheetPivotFieldsFit(pivot)) {
        if (problem) *problem = Tr("A field of the pivot table is not a column of its source.");
        return false;
    }
    PivotData data;
    ReadPivotData(pivot, source, data);
    out.fieldNames = data.names;
    out.sourceRecords = data.sourceRecords;
    out.shownRecords = static_cast<int>(data.records.size());

    const size_t R = pivot.rowFields.size();
    const size_t C = pivot.columnFields.size();
    const size_t F = pivot.filterFields.size();
    const int V = static_cast<int>(pivot.valueFields.size());
    const bool valuesOnRows = V > 1 && pivot.valuesInRows;
    const bool valuesOnCols = V > 1 && !pivot.valuesInRows;

    // The items under each path, and the summaries of every pair of row and
    // column paths (each a prefix of a record's items).
    std::map<std::string, std::set<int>> rowChildrenSet, colChildrenSet;
    std::map<std::string, std::vector<Accumulator>> sums;   // rowPath | colPath -> per value field
    for (const PivotData::Record& record : data.records) {
        std::vector<int> rowPath(record.items.begin(), record.items.begin() + static_cast<long>(R));
        std::vector<int> colPath(record.items.begin() + static_cast<long>(R),
                                 record.items.begin() + static_cast<long>(R + C));
        for (size_t d = 0; d < R; ++d) rowChildrenSet[PathKey(rowPath, d)].insert(rowPath[d]);
        for (size_t d = 0; d < C; ++d) colChildrenSet[PathKey(colPath, d)].insert(colPath[d]);
        for (size_t dr = 0; dr <= R; ++dr) {
            const std::string rowKey = PathKey(rowPath, dr) + "|";
            for (size_t dc = 0; dc <= C; ++dc) {
                std::vector<Accumulator>& acc = sums[rowKey + PathKey(colPath, dc)];
                if (acc.empty()) acc.resize(static_cast<size_t>(V));
                for (int v = 0; v < V; ++v) {
                    acc[static_cast<size_t>(v)].Add(record.filled[static_cast<size_t>(v)] != 0,
                                                   record.isNumber[static_cast<size_t>(v)] != 0,
                                                   record.values[static_cast<size_t>(v)]);
                }
            }
        }
    }
    auto toChildren = [](const std::map<std::string, std::set<int>>& sets) {
        std::map<std::string, std::vector<int>> lists;
        for (const auto& [key, set] : sets) lists[key] = std::vector<int>(set.begin(), set.end());
        return lists;
    };
    std::vector<const PivotField*> rowFieldList, colFieldList;
    for (size_t i = 0; i < R; ++i) rowFieldList.push_back(data.fields[i]);
    for (size_t i = 0; i < C; ++i) colFieldList.push_back(data.fields[R + i]);
    BuildEntries(rowFieldList, 0, toChildren(rowChildrenSet), V, valuesOnRows, pivot.grandTotalRow && R > 0,
                 out.rowEntries);
    BuildEntries(colFieldList, 0, toChildren(colChildrenSet), V, valuesOnCols, pivot.grandTotalColumn && C > 0,
                 out.colEntries);
    for (size_t i = 0; i < R; ++i) out.rowItems.push_back(data.fieldItems[i].items);
    for (size_t i = 0; i < C; ++i) out.columnItems.push_back(data.fieldItems[R + i].items);

    auto fieldName = [&](int column) {
        return column >= 0 && column < static_cast<int>(data.names.size()) ? data.names[static_cast<size_t>(column)]
                                                                            : std::string();
    };
    auto valueCaption = [&](int v) {
        return PivotValueFieldCaption(pivot.valueFields[static_cast<size_t>(v)], data.names);
    };
    auto put = [&](int row, int col, PivotCellKind kind, const std::string& text) {
        if (text.empty()) return;
        PivotOutputCell cell;
        cell.row = row;
        cell.col = col;
        cell.kind = kind;
        cell.text = text;
        out.cells.push_back(cell);
    };

    // The filters, one a row, then an empty row.
    int row = 0;
    for (size_t f = 0; f < F; ++f) {
        const PivotData::FieldItems& items = data.fieldItems[R + C + f];
        put(row, 0, PivotCellKind::FieldName, fieldName(pivot.filterFields[f].column));
        std::string shown = Tr("(All)");
        if (!items.hidden.empty()) {
            const size_t visible = items.items.size() - items.hidden.size();
            if (visible == 1) {
                for (size_t i = 0; i < items.items.size(); ++i) {
                    if (!items.hidden.count(static_cast<int>(i))) shown = items.items[i].label;
                }
            } else {
                shown = Tr("(Multiple items)");
            }
        }
        put(row, 1, PivotCellKind::Caption, shown);
        ++row;
    }
    if (F > 0) ++row;
    out.tableTop = row;

    const int rowLevels = static_cast<int>(R) + (valuesOnRows ? 1 : 0);
    const int colLevels = static_cast<int>(C) + (valuesOnCols ? 1 : 0);
    const int labelCols = std::max(rowLevels, 1);
    out.columnLevels = colLevels;
    out.firstDataCol = labelCols;
    const std::string caption = V == 1 ? valueCaption(0) : std::string();
    auto rowLevelName = [&](int level) {
        return level < static_cast<int>(R) ? fieldName(pivot.rowFields[static_cast<size_t>(level)].column)
                                           : Tr("Values");
    };
    auto colLevelName = [&](int level) {
        return level < static_cast<int>(C) ? fieldName(pivot.columnFields[static_cast<size_t>(level)].column)
                                           : Tr("Values");
    };
    if (colLevels == 0) {
        // One header row: the row fields' names, then the data column's caption.
        for (int k = 0; k < rowLevels; ++k) put(row, k, PivotCellKind::FieldName, rowLevelName(k));
        if (V == 1) put(row, labelCols, PivotCellKind::Caption, caption);
        out.firstDataRow = row + 1;
    } else {
        put(row, 0, PivotCellKind::Caption, caption);
        for (int k = 0; k < colLevels; ++k) put(row, labelCols + k, PivotCellKind::FieldName, colLevelName(k));
        const int itemRow0 = row + 1;
        for (size_t j = 0; j < out.colEntries.size(); ++j) {
            const PivotOutput::Entry& e = out.colEntries[j];
            const int col = labelCols + static_cast<int>(j);
            const PivotOutput::Entry* before = j > 0 ? &out.colEntries[j - 1] : nullptr;
            const bool repeat = before && before->subtotal == e.subtotal && before->grandTotal == e.grandTotal &&
                                (e.subtotal || e.grandTotal) && before->items == e.items;
            if (repeat) {
                // the same total for the next value field: its label is there already
            } else if (e.grandTotal) {
                put(itemRow0, col, PivotCellKind::GrandTotalLabel, Tr("Grand Total"));
            } else if (e.subtotal) {
                const int level = static_cast<int>(e.items.size()) - 1;
                const std::string label = out.columnItems[static_cast<size_t>(level)][static_cast<size_t>(e.items.back())].label;
                put(itemRow0 + level, col, PivotCellKind::SubtotalLabel, TrF("{0} Total", label));
            } else {
                for (size_t k = 0; k < e.items.size(); ++k) {
                    const bool first = !before || before->grandTotal || before->items.size() <= k ||
                                       !std::equal(e.items.begin(), e.items.begin() + static_cast<long>(k) + 1,
                                                   before->items.begin());
                    if (first) {
                        put(itemRow0 + static_cast<int>(k), col, PivotCellKind::ColumnItem,
                            out.columnItems[k][static_cast<size_t>(e.items[k])].label);
                    }
                }
            }
            if (e.valueField >= 0) {
                put(itemRow0 + colLevels - 1, col, e.grandTotal || e.subtotal ? PivotCellKind::SubtotalLabel
                                                                               : PivotCellKind::ColumnItem,
                    valueCaption(e.valueField));
            }
        }
        const int namesRow = itemRow0 + colLevels - 1;
        for (int k = 0; k < rowLevels; ++k) put(namesRow, k, PivotCellKind::FieldName, rowLevelName(k));
        out.firstDataRow = itemRow0 + colLevels;
    }

    // The row labels and the values.
    for (size_t i = 0; i < out.rowEntries.size(); ++i) {
        const PivotOutput::Entry& e = out.rowEntries[i];
        const int r = out.firstDataRow + static_cast<int>(i);
        const PivotOutput::Entry* before = i > 0 ? &out.rowEntries[i - 1] : nullptr;
        const bool totalRow = e.grandTotal || e.subtotal;
        const bool repeat = before && before->subtotal == e.subtotal && before->grandTotal == e.grandTotal &&
                            totalRow && before->items == e.items;
        if (repeat) {
            // the same total for the next value field
        } else if (e.grandTotal) {
            put(r, 0, PivotCellKind::GrandTotalLabel, Tr("Grand Total"));
        } else if (e.subtotal) {
            const int level = static_cast<int>(e.items.size()) - 1;
            const std::string label = out.rowItems[static_cast<size_t>(level)][static_cast<size_t>(e.items.back())].label;
            put(r, level, PivotCellKind::SubtotalLabel, TrF("{0} Total", label));
        } else if (R == 0) {
            if (!valuesOnRows) put(r, 0, PivotCellKind::RowItem, V == 1 ? caption : Tr("Total"));
        } else {
            for (size_t k = 0; k < e.items.size(); ++k) {
                const bool first = !before || before->grandTotal || before->items.size() <= k ||
                                   !std::equal(e.items.begin(), e.items.begin() + static_cast<long>(k) + 1,
                                               before->items.begin());
                if (first) {
                    put(r, static_cast<int>(k), PivotCellKind::RowItem,
                        out.rowItems[k][static_cast<size_t>(e.items[k])].label);
                }
            }
        }
        if (e.valueField >= 0) {
            put(r, rowLevels - 1, totalRow ? PivotCellKind::SubtotalLabel : PivotCellKind::RowItem,
                valueCaption(e.valueField));
        }
        if (V == 0) continue;
        for (size_t j = 0; j < out.colEntries.size(); ++j) {
            const PivotOutput::Entry& c = out.colEntries[j];
            const int v = e.valueField >= 0 ? e.valueField : (c.valueField >= 0 ? c.valueField : 0);
            const PivotValueField& field = pivot.valueFields[static_cast<size_t>(v)];
            auto it = sums.find(PathKey(e.items, e.items.size()) + "|" + PathKey(c.items, c.items.size()));
            if (it == sums.end()) continue;
            double value = 0.0;
            if (!it->second[static_cast<size_t>(v)].Result(field.summary, value)) continue;
            bool percent = false;
            if (field.showAs != PivotShowAs::Value) {
                std::string denominatorKey;
                if (field.showAs == PivotShowAs::PercentOfTotal) denominatorKey = "|";
                else if (field.showAs == PivotShowAs::PercentOfRow) denominatorKey = PathKey(e.items, e.items.size()) + "|";
                else denominatorKey = "|" + PathKey(c.items, c.items.size());
                auto whole = sums.find(denominatorKey);
                double total = 0.0;
                if (whole == sums.end() || !whole->second[static_cast<size_t>(v)].Result(field.summary, total) ||
                    total == 0.0) {
                    continue;
                }
                value /= total;
                percent = true;
            }
            PivotOutputCell cell;
            cell.row = r;
            cell.col = out.firstDataCol + static_cast<int>(j);
            const bool totalCol = c.grandTotal || c.subtotal;
            cell.kind = (e.grandTotal || c.grandTotal) ? PivotCellKind::GrandTotalValue
                        : (totalRow || totalCol)       ? PivotCellKind::SubtotalValue
                                                       : PivotCellKind::Value;
            cell.isNumber = true;
            cell.number = value;
            cell.percent = percent;
            cell.rowEntry = static_cast<int>(i);
            cell.colEntry = static_cast<int>(j);
            cell.valueField = v;
            out.cells.push_back(cell);
        }
    }
    out.rows = out.firstDataRow + static_cast<int>(out.rowEntries.size());
    out.cols = out.firstDataCol + (V > 0 || colLevels > 0 ? static_cast<int>(out.colEntries.size()) : 0);
    for (const PivotOutputCell& cell : out.cells) {
        out.rows = std::max(out.rows, cell.row + 1);
        out.cols = std::max(out.cols, cell.col + 1);
    }
    return true;
}

// ============================================================================
// WRITING ONTO A SHEET
// ============================================================================

namespace {

// The look of a pivot's cells: names and items on a light band, totals in bold.
CellStyle PivotCellStyle(PivotCellKind kind, const NumberFormat& format) {
    CellStyle style;
    style.numberFormat = format;
    const Color band(221, 235, 247);
    const Color rule(155, 194, 230);
    switch (kind) {
        case PivotCellKind::Caption:
        case PivotCellKind::FieldName:
            style.font.bold = true;
            style.fill = CellFill::Solid(band);
            style.borders.bottom = CellBorder{BorderStyle::Thin, rule, 1};
            break;
        case PivotCellKind::ColumnItem:
            style.font.bold = true;
            style.fill = CellFill::Solid(band);
            break;
        case PivotCellKind::RowItem:
            style.font.bold = true;
            break;
        case PivotCellKind::SubtotalLabel:
        case PivotCellKind::SubtotalValue:
            style.font.bold = true;
            style.borders.top = CellBorder{BorderStyle::Thin, rule, 1};
            break;
        case PivotCellKind::GrandTotalLabel:
        case PivotCellKind::GrandTotalValue:
            style.font.bold = true;
            style.fill = CellFill::Solid(band);
            style.borders.top = CellBorder{BorderStyle::Thin, rule, 1};
            break;
        case PivotCellKind::Value:
            break;
    }
    return style;
}

// The number format a value field shows its results in: its source
// column's (a currency stays a currency), a count's plain, a share's a
// percentage.
NumberFormat ValueFormat(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source, int v, bool percent) {
    if (percent) return NumberFormat::Percentage(2);
    const PivotValueField& field = pivot.valueFields[static_cast<size_t>(v)];
    if (field.summary == PivotSummary::Count || field.summary == PivotSummary::CountNumbers) {
        return NumberFormat::General();
    }
    const CellRange range = pivot.sourceRange;
    for (int r = range.start.row + 1; r <= range.end.row && r <= range.start.row + 200; ++r) {
        const SpreadsheetCell* cell = source.GetCellIfExists(r, range.start.col + field.column);
        double value = 0.0;
        if (cell && CellNumber(cell, value)) {
            const NumberFormat& format = cell->GetStyle().numberFormat;
            if (format.category == NumberFormatCategory::Date || format.category == NumberFormatCategory::DateTime ||
                format.category == NumberFormatCategory::Time) {
                return NumberFormat::General();
            }
            return format;
        }
    }
    return NumberFormat::General();
}

} // namespace

void ClearSpreadsheetPivotCells(SpreadsheetPivotTable& pivot, SpreadsheetSheet& target) {
    if (!pivot.HasOutput()) return;
    target.ClearRange(pivot.outputRange);
    pivot.outputRange = CellRange(-1, -1, -1, -1);
}

bool RefreshSpreadsheetPivot(SpreadsheetPivotTable& pivot, SpreadsheetSheet& target,
                             const SpreadsheetSheetLookup& sheets, std::string* problem) {
    if (!pivot.unsupportedReason.empty()) {
        if (problem) *problem = pivot.unsupportedReason;
        return false;
    }
    const SpreadsheetSheet* source = sheets ? sheets(pivot.sourceSheet) : nullptr;
    if (!source) {
        if (problem) *problem = TrF("The source sheet \"{0}\" of the pivot table is not there.", pivot.sourceSheet);
        return false;
    }
    PivotOutput out;
    if (!ComputeSpreadsheetPivot(pivot, *source, out, problem)) return false;
    ClearSpreadsheetPivotCells(pivot, target);
    pivot.fieldNames = out.fieldNames;
    const int top = pivot.target.row;
    const int left = pivot.target.col;
    std::map<std::pair<int, bool>, NumberFormat> formats;
    for (const PivotOutputCell& one : out.cells) {
        SpreadsheetCell& cell = target.GetOrCreateCell(top + one.row, left + one.col);
        cell.Clear();
        NumberFormat format = NumberFormat::General();
        if (one.isNumber) {
            cell.SetNumber(one.number);
            const auto key = std::make_pair(one.valueField, one.percent);
            auto it = formats.find(key);
            if (it == formats.end()) {
                it = formats.emplace(key, ValueFormat(pivot, *source, std::max(0, one.valueField), one.percent)).first;
            }
            format = it->second;
        } else {
            cell.SetText(one.text);
        }
        cell.SetStyle(PivotCellStyle(one.kind, format));
    }
    // Every cell of the table takes the band where it has one (an empty
    // corner of the header too), so the pivot reads as one block.
    const int rows = std::max(1, out.rows);
    const int cols = std::max(1, out.cols);
    for (int r = out.tableTop; r < out.firstDataRow; ++r) {
        for (int c = 0; c < cols; ++c) {
            SpreadsheetCell& cell = target.GetOrCreateCell(top + r, left + c);
            if (cell.IsEmpty()) cell.SetStyle(PivotCellStyle(PivotCellKind::FieldName, NumberFormat::General()));
        }
    }
    pivot.outputRange = CellRange(top, left, top + rows - 1, left + cols - 1);
    target.NotifyCellsChanged(pivot.outputRange);
    return true;
}

std::vector<int> SpreadsheetPivotDrillDownRows(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source,
                                               int row, int col) {
    std::vector<int> rows;
    PivotOutput out;
    if (!ComputeSpreadsheetPivot(pivot, source, out)) return rows;
    const PivotOutputCell* hit = nullptr;
    for (const PivotOutputCell& cell : out.cells) {
        if (cell.rowEntry >= 0 && pivot.target.row + cell.row == row && pivot.target.col + cell.col == col) {
            hit = &cell;
            break;
        }
    }
    if (!hit) return rows;
    const PivotOutput::Entry& re = out.rowEntries[static_cast<size_t>(hit->rowEntry)];
    const PivotOutput::Entry& ce = out.colEntries[static_cast<size_t>(hit->colEntry)];
    PivotData data;
    ReadPivotData(pivot, source, data);
    const size_t R = pivot.rowFields.size();
    for (const PivotData::Record& record : data.records) {
        bool match = true;
        for (size_t k = 0; k < re.items.size() && match; ++k) match = record.items[k] == re.items[k];
        for (size_t k = 0; k < ce.items.size() && match; ++k) match = record.items[R + k] == ce.items[k];
        if (match) rows.push_back(record.sourceRow);
    }
    return rows;
}

} // namespace UltraCanvas
