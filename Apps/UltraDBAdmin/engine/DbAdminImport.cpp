// Apps/UltraDBAdmin/engine/DbAdminImport.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminImport.h"

#include <algorithm>

namespace UltraDBAdmin {
namespace {

bool Digit(char c) { return c >= '0' && c <= '9'; }

std::string Trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// A column that takes numbers: by how its values come back, or - since an
// exact numeric comes back as text (PostgreSQL's numeric) - by its declared
// type.
bool Numeric(const UltraDbColumnSchema& column) {
    return column.valueType == UltraDbType::Int || column.valueType == UltraDbType::Double ||
           NumericType(column.declaredType);
}

// A column an INSERT may leave out: the database fills it.
bool Optional(const UltraDbColumnSchema& column) {
    return column.nullable || column.hasDefault || column.autoIncrement || column.generated;
}

size_t FirstDataRow(const ImportGrid& grid, const ImportMapping& mapping) {
    return mapping.header && !grid.rows.empty() ? 1 : 0;
}

const ImportCell& CellAt(const ImportGrid& grid, size_t row, int column) {
    static const ImportCell empty;
    const auto& cells = grid.rows[row];
    return column >= 0 && static_cast<size_t>(column) < cells.size() ? cells[static_cast<size_t>(column)] : empty;
}

} // namespace

bool NumericType(const std::string& declaredType) {
    const std::string type = Lower(declaredType);
    for (const char* part : {"int", "num", "dec", "real", "doub", "floa", "serial"})
        if (type.find(part) != std::string::npos) return true;
    return false;
}

ImportCell ImportCell::Text(std::string text) {
    ImportCell cell;
    cell.empty = text.empty();
    cell.text = std::move(text);
    return cell;
}

ImportCell ImportCell::Number(std::string number) {
    ImportCell cell;
    cell.empty = number.empty();
    cell.text = number;
    cell.number = std::move(number);
    return cell;
}

size_t ImportGrid::Width() const {
    size_t width = 0;
    for (const auto& row : rows) width = std::max(width, row.size());
    return width;
}

void ImportGrid::AddRow(std::vector<ImportCell> row, size_t number) {
    rows.push_back(std::move(row));
    rowNumbers.push_back(number);
}

std::string NormalizeNumber(const std::string& raw, char decimalMark, char groupMark) {
    const std::string text = Trimmed(raw);
    size_t i = 0;
    std::string out;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        if (text[i] == '-') out += '-';
        ++i;
    }
    // Digits, grouped in threes after the first group, then the fraction.
    size_t integerDigits = 0;
    size_t sinceGroup = 0;
    bool grouped = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (Digit(c)) {
            out += c;
            ++integerDigits;
            ++sinceGroup;
        } else if (groupMark != 0 && groupMark != decimalMark && c == groupMark) {
            if (integerDigits == 0 || (grouped && sinceGroup != 3) || (!grouped && sinceGroup > 3)) return std::string();
            grouped = true;
            sinceGroup = 0;
        } else {
            break;
        }
    }
    if (grouped && sinceGroup != 3) return std::string();
    size_t fractionDigits = 0;
    if (i < text.size() && text[i] == decimalMark) {
        out += '.';
        for (++i; i < text.size() && Digit(text[i]); ++i, ++fractionDigits) out += text[i];
    }
    if (integerDigits + fractionDigits == 0) return std::string();
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        std::string exponent = "e";
        ++i;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) exponent += text[i++];
        size_t digits = 0;
        for (; i < text.size() && Digit(text[i]); ++i, ++digits) exponent += text[i];
        if (digits == 0) return std::string();
        out += exponent;
    }
    return i == text.size() ? out : std::string();
}

ImportGrid GridFromCsv(const std::string& text, const CSVImportOptions& options, std::string name) {
    ImportGrid grid;
    grid.name = std::move(name);
    const UltraCanvas::CSVGrid parsed = UltraCanvas::CSVParse(text, options);
    // CSVParse has left the rows above the start row out already.
    const size_t first = static_cast<size_t>(std::max(1, options.startRow));
    for (size_t r = 0; r < parsed.size(); ++r) {
        std::vector<ImportCell> row;
        bool any = false;
        for (const auto& field : parsed[r]) {
            ImportCell cell = ImportCell::Text(field.text);
            if (options.detectNumbers && !(field.quoted && options.quotedAsText))
                cell.number = NormalizeNumber(field.text, options.decimalSeparator, options.thousandsSeparator);
            any = any || !cell.empty;
            row.push_back(std::move(cell));
        }
        if (any) grid.AddRow(std::move(row), first + r);
    }
    return grid;
}

std::vector<std::string> FileColumnNames(const ImportGrid& grid, bool header) {
    std::vector<std::string> names;
    const size_t width = grid.Width();
    for (size_t c = 0; c < width; ++c) {
        std::string name;
        if (header && !grid.rows.empty() && c < grid.rows[0].size()) name = Trimmed(grid.rows[0][c].text);
        names.push_back(name.empty() ? "Column " + std::to_string(c + 1) : name);
    }
    return names;
}

bool GuessHeader(const UltraDbTableSchema& table, const ImportGrid& grid) {
    if (grid.rows.empty()) return false;
    for (const auto& cell : grid.rows[0]) {
        const std::string name = Lower(Trimmed(cell.text));
        for (const auto& column : table.columns)
            if (!name.empty() && Lower(column.name) == name) return true;
    }
    return false;
}

ImportMapping AutoMapping(const UltraDbTableSchema& table, const ImportGrid& grid, bool header) {
    ImportMapping mapping;
    mapping.header = header;
    mapping.source.assign(table.columns.size(), -1);
    bool matched = false;
    if (header && !grid.rows.empty()) {
        for (size_t t = 0; t < table.columns.size(); ++t) {
            if (table.columns[t].generated) continue;
            for (size_t c = 0; c < grid.rows[0].size(); ++c) {
                if (Lower(Trimmed(grid.rows[0][c].text)) == Lower(table.columns[t].name)) {
                    mapping.source[t] = static_cast<int>(c);
                    matched = true;
                    break;
                }
            }
        }
    }
    if (matched) return mapping;
    // In order. A file one column short of the table usually lacks the
    // numbered key, which the database fills: then that is skipped.
    std::vector<size_t> targets;
    size_t numbered = 0;
    for (size_t t = 0; t < table.columns.size(); ++t) {
        if (table.columns[t].generated) continue;
        targets.push_back(t);
        numbered += table.columns[t].autoIncrement;
    }
    const size_t width = grid.Width();
    const bool skipNumbered = numbered > 0 && width == targets.size() - numbered;
    int next = 0;
    for (size_t t : targets) {
        if (skipNumbered && table.columns[t].autoIncrement) continue;
        if (static_cast<size_t>(next) >= width) break;
        mapping.source[t] = next++;
    }
    return mapping;
}

size_t DataRows(const ImportGrid& grid, const ImportMapping& mapping) {
    return grid.rows.size() - FirstDataRow(grid, mapping);
}

std::string CheckMapping(const UltraDbTableSchema& table, const ImportGrid& grid, const ImportMapping& mapping) {
    if (DataRows(grid, mapping) == 0) return "The file has no rows to import.";
    if (mapping.source.size() != table.columns.size()) return "The mapping does not fit the table.";
    bool any = false;
    for (size_t t = 0; t < table.columns.size(); ++t) {
        const UltraDbColumnSchema& column = table.columns[t];
        if (mapping.source[t] >= 0) {
            if (column.generated) return column.name + " is computed by the database: it takes no value.";
            any = true;
        } else if (!Optional(column)) {
            return column.name + " must have a value and has no default: give it a column of the file.";
        }
    }
    return any ? std::string() : "Give at least one column of the table a column of the file.";
}

UltraDbValue ImportValue(const ImportCell& cell, const UltraDbColumnSchema& column, bool emptyIsNull) {
    if (cell.empty) return emptyIsNull ? UltraDbValue::Null() : UltraDbValue(std::string());
    if (Numeric(column) && !cell.number.empty()) return UltraDbValue(cell.number);
    return UltraDbValue(cell.text);
}

UltraDbResultSet ImportPreview(const UltraDbTableSchema& table, const ImportGrid& grid,
                               const ImportMapping& mapping, size_t limit) {
    UltraDbResultSet preview;
    std::vector<std::string> names;
    for (size_t t = 0; t < table.columns.size() && t < mapping.source.size(); ++t)
        if (mapping.source[t] >= 0) names.push_back(table.columns[t].name);
    preview.SetColumns(names);
    const size_t first = FirstDataRow(grid, mapping);
    for (size_t r = first; r < grid.rows.size() && r - first < limit; ++r) {
        std::vector<UltraDbValue> values;
        for (size_t t = 0; t < table.columns.size() && t < mapping.source.size(); ++t) {
            if (mapping.source[t] < 0) continue;
            values.push_back(ImportValue(CellAt(grid, r, mapping.source[t]), table.columns[t], mapping.emptyIsNull));
        }
        preview.AddRow(std::move(values));
    }
    return preview;
}

UltraDbResult ImportRows(const std::string& connection, const UltraDbTableSchema& table, const ImportGrid& grid,
                         const ImportMapping& mapping, size_t& imported, size_t& failedRow) {
    imported = 0;
    failedRow = 0;
    const std::string problem = CheckMapping(table, grid, mapping);
    if (!problem.empty()) return UltraDbResult::Error(UltraDbResultCode::InvalidArgument, problem);
    std::string target;
    UltraDbResult result = UltraDb_QuoteQualifiedName(connection, table.schema, table.name, target);
    if (!result) return result;
    std::string names, marks;
    std::vector<size_t> columns;
    for (size_t t = 0; t < table.columns.size(); ++t) {
        if (mapping.source[t] < 0) continue;
        std::string quoted;
        result = UltraDb_QuoteIdentifier(connection, table.columns[t].name, quoted);
        if (!result) return result;
        names += (names.empty() ? "" : ", ") + quoted;
        marks += marks.empty() ? "?" : ", ?";
        columns.push_back(t);
    }
    const std::string sql = "INSERT INTO " + target + " (" + names + ") VALUES (" + marks + ")";

    UltraDbResult begun;
    const UltraDbHandle tx = UltraDb_Begin(connection, &begun);
    if (tx == UltraDbInvalidHandle) return begun;
    for (size_t r = FirstDataRow(grid, mapping); r < grid.rows.size(); ++r) {
        UltraDbParams params;
        for (size_t t : columns)
            params.push_back(ImportValue(CellAt(grid, r, mapping.source[t]), table.columns[t], mapping.emptyIsNull));
        result = UltraDb_ExecInTx(tx, sql, params);
        if (!result) {
            failedRow = r < grid.rowNumbers.size() ? grid.rowNumbers[r] : r + 1;
            UltraDb_Rollback(tx);
            imported = 0;
            return result;
        }
        ++imported;
    }
    result = UltraDb_Commit(tx);
    if (!result) imported = 0;
    return result;
}

} // namespace UltraDBAdmin
