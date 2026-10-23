// Apps/UltraDBAdmin/engine/DbAdminExport.cpp
// Version: 0.2.0 - RowsText, QuotedName
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminExport.h"

#include "DbAdminImport.h"
#include "DbAdminSqliteTable.h"

#include "UltraCanvasPathUtf8.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraDBAdmin {
namespace {

constexpr size_t kPageRows = 1000;

std::string Hex(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const uint8_t b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 0x0F];
    }
    return out;
}

std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (u < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", u);
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

bool Finite(const UltraDbValue& value) { return std::isfinite(value.AsDouble()); }

} // namespace

std::string CsvField(const UltraDbValue& value) {
    switch (value.Type()) {
        case UltraDbType::Null: return std::string();
        case UltraDbType::Blob: return "\\x" + Hex(value.AsBlob());
        default:                return value.AsString();
    }
}

std::string JsonValue(const UltraDbValue& value) {
    switch (value.Type()) {
        case UltraDbType::Null:   return "null";
        case UltraDbType::Boolean:   return value.AsBool() ? "true" : "false";
        case UltraDbType::Int:    return value.AsString();
        case UltraDbType::Double: return Finite(value) ? value.AsString() : "null";
        case UltraDbType::Blob:   return JsonString("\\x" + Hex(value.AsBlob()));
        case UltraDbType::Text:   return JsonString(value.AsString());
    }
    return "null";
}

std::string SqlLiteral(const UltraDbValue& value, SqlEngine engine) {
    switch (value.Type()) {
        case UltraDbType::Null:   return "NULL";
        case UltraDbType::Boolean:   return value.AsBool() ? "TRUE" : "FALSE";
        case UltraDbType::Int:    return value.AsString();
        case UltraDbType::Double: return Finite(value) ? value.AsString() : "NULL";
        case UltraDbType::Blob:
            return engine == SqlEngine::Sqlite ? "X'" + Hex(value.AsBlob()) + "'"
                                               : "decode('" + Hex(value.AsBlob()) + "', 'hex')";
        case UltraDbType::Text: {
            std::string out = "'";
            for (const char c : value.AsString()) {
                if (c == '\'') out += '\'';
                out += c;
            }
            return out + "'";
        }
    }
    return "NULL";
}

WorkbookCell WorkbookValue(const UltraDbValue& value, bool numericColumn) {
    WorkbookCell cell;
    switch (value.Type()) {
        case UltraDbType::Null:
            break;
        case UltraDbType::Int:
        case UltraDbType::Double:
            cell.kind = WorkbookCell::Kind::Number;
            cell.number = value.AsDouble();
            break;
        case UltraDbType::Boolean:
            cell.kind = WorkbookCell::Kind::Boolean;
            cell.boolean = value.AsBool();
            break;
        case UltraDbType::Text:
            cell.text = value.AsString();
            cell.kind = WorkbookCell::Kind::Text;
            // "01067" is not 1067: only a plain number that reads back as written.
            if (numericColumn && NormalizeNumber(cell.text, '.', 0) == cell.text &&
                !(cell.text.size() > 1 && cell.text[0] == '0' && cell.text[1] != '.')) {
                cell.kind = WorkbookCell::Kind::Number;
                cell.number = value.AsDouble();
            }
            break;
        case UltraDbType::Blob:
            cell.kind = WorkbookCell::Kind::Text;
            cell.text = CsvField(value);
            break;
    }
    return cell;
}

// ===== ExportWriter =====

ExportWriter::~ExportWriter() {
    if (file_) {
        std::fclose(file_);
        std::error_code ec;
        std::filesystem::remove(UltraCanvas::PathFromUtf8(partial_), ec);
    }
}

bool ExportWriter::Put(const std::string& utf8, std::string& error) {
    std::string bytes = utf8;
    if (options_.format == ExportFormat::Csv) {
        bytes = UltraCanvas::CSVEncodeFromUtf8(utf8, options_.csv.encoding, options_.csv.writeBOM && firstBytes_);
    }
    firstBytes_ = false;
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size()) {
        error = "the file could not be written (is the disk full?)";
        return false;
    }
    return true;
}

bool ExportWriter::Open(const std::string& path, const ExportOptions& options, const std::vector<std::string>& columns,
                        std::string& error) {
    options_ = options;
    columns_ = columns;
    path_ = path;
    partial_ = path + ".part";
    file_ = UltraCanvas::OpenFileUtf8(partial_, "wb");
    if (!file_) {
        error = "it cannot be written there";
        return false;
    }
    switch (options_.format) {
        case ExportFormat::Csv:
            return !options_.header || Put(UltraCanvas::CSVFormatRecord(columns_, options_.csv), error);
        case ExportFormat::Json:
            return Put("[", error);
        case ExportFormat::Sql:
            return Put(options_.preamble + (options_.preamble.empty() ? "" : "\n") + "BEGIN;\n", error);
    }
    return true;
}

std::string QuotedName(const std::string& name) {
    std::string out = "\"";
    for (const char c : name) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string RowsText(const std::vector<std::string>& columns, const std::vector<UltraDbRow>& rows,
                     ExportFormat format, SqlEngine engine, const std::string& table) {
    std::string text;
    switch (format) {
        case ExportFormat::Csv: {
            const CSVExportOptions options;   // comma, quotes where needed, LF
            text = UltraCanvas::CSVFormatRecord(columns, options);
            for (const auto& row : rows) {
                std::vector<std::string> fields;
                for (size_t i = 0; i < row.Size(); ++i) fields.push_back(CsvField(row[i]));
                text += UltraCanvas::CSVFormatRecord(fields, options);
            }
            break;
        }
        case ExportFormat::Json: {
            text = "[";
            for (size_t r = 0; r < rows.size(); ++r) {
                text += r == 0 ? "\n{" : ",\n{";
                for (size_t i = 0; i < rows[r].Size() && i < columns.size(); ++i)
                    text += (i == 0 ? "" : ", ") + JsonString(columns[i]) + ": " + JsonValue(rows[r][i]);
                text += "}";
            }
            text += rows.empty() ? "]\n" : "\n]\n";
            break;
        }
        case ExportFormat::Sql: {
            std::string names;
            for (size_t i = 0; i < columns.size(); ++i) names += (i == 0 ? "" : ", ") + QuotedName(columns[i]);
            for (const auto& row : rows) {
                text += "INSERT INTO " + table + " (" + names + ") VALUES (";
                for (size_t i = 0; i < row.Size(); ++i) text += (i == 0 ? "" : ", ") + SqlLiteral(row[i], engine);
                text += ");\n";
            }
            break;
        }
    }
    return text;
}

bool ExportWriter::Write(const UltraDbRow& row, std::string& error) {
    std::string text;
    switch (options_.format) {
        case ExportFormat::Csv: {
            std::vector<std::string> fields;
            fields.reserve(row.Size());
            for (size_t i = 0; i < row.Size(); ++i) fields.push_back(CsvField(row[i]));
            text = UltraCanvas::CSVFormatRecord(fields, options_.csv);
            break;
        }
        case ExportFormat::Json: {
            text = rows_ == 0 ? "\n{" : ",\n{";
            for (size_t i = 0; i < row.Size() && i < columns_.size(); ++i)
                text += (i == 0 ? "" : ", ") + JsonString(columns_[i]) + ": " + JsonValue(row[i]);
            text += "}";
            break;
        }
        case ExportFormat::Sql: {
            text = "INSERT INTO " + options_.table + " (";
            for (size_t i = 0; i < columns_.size(); ++i) text += (i == 0 ? "" : ", ") + columns_[i];
            text += ") VALUES (";
            for (size_t i = 0; i < row.Size(); ++i) text += (i == 0 ? "" : ", ") + SqlLiteral(row[i], options_.engine);
            text += ");\n";
            break;
        }
    }
    if (!Put(text, error)) return false;
    ++rows_;
    return true;
}

bool ExportWriter::Finish(std::string& error) {
    if (!file_) { error = "nothing was written"; return false; }
    bool ok = true;
    if (options_.format == ExportFormat::Json) ok = Put(rows_ == 0 ? "]\n" : "\n]\n", error);
    if (options_.format == ExportFormat::Sql) ok = Put("COMMIT;\n", error);
    if (std::fclose(file_) != 0 && ok) {
        error = "the file could not be completed";
        ok = false;
    }
    file_ = nullptr;
    std::error_code ec;
    if (ok) {
        std::filesystem::rename(UltraCanvas::PathFromUtf8(partial_), UltraCanvas::PathFromUtf8(path_), ec);
        if (ec) { error = "it could not be put in place: " + ec.message(); ok = false; }
    }
    if (!ok) std::filesystem::remove(UltraCanvas::PathFromUtf8(partial_), ec);
    return ok;
}

// ===== Exports =====

namespace {

// For SQL, the INSERTs' column names are quoted for the connection's engine.
UltraDbResult ColumnsFor(const std::string& connection, const ExportOptions& options,
                         const std::vector<std::string>& names, std::vector<std::string>& out) {
    out = names;
    if (options.format != ExportFormat::Sql) return UltraDbResult::Ok();
    for (auto& name : out) {
        std::string quoted;
        const UltraDbResult result = UltraDb_QuoteIdentifier(connection, name, quoted);
        if (!result) return result;
        name = quoted;
    }
    return UltraDbResult::Ok();
}

UltraDbResult Failed(const std::string& path, const std::string& error) {
    return UltraDbResult::Error(UltraDbResultCode::Internal,
                                UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(path).filename()) + ": " + error);
}

} // namespace

UltraDbResult ExportResult(const std::string& connection, const UltraDbResultSet& rows, const std::string& path,
                           const ExportOptions& options, size_t& written) {
    written = 0;
    std::vector<std::string> columns;
    UltraDbResult result = ColumnsFor(connection, options, rows.Columns(), columns);
    if (!result) return result;
    ExportWriter writer;
    std::string error;
    if (!writer.Open(path, options, columns, error)) return Failed(path, error);
    for (const auto& row : rows)
        if (!writer.Write(row, error)) return Failed(path, error);
    if (!writer.Finish(error)) return Failed(path, error);
    written = writer.Rows();
    return UltraDbResult::Ok();
}

UltraDbResult ExportQuery(const std::string& connection, const std::string& sql, const std::string& path,
                          const ExportOptions& options, size_t& written, const std::atomic<bool>* cancel,
                          const std::function<void(size_t)>& progress, const UltraDbParams& params) {
    written = 0;
    UltraDbResult result;
    const UltraDbHandle cursor = UltraDb_OpenCursor(connection, sql, params, &result);
    if (cursor == UltraDbInvalidHandle) return result;
    ExportWriter writer;
    std::string error;
    bool opened = false, exhausted = false;
    UltraDbResultSet page;
    while (!exhausted) {
        result = UltraDb_FetchRows(cursor, kPageRows, page, exhausted);
        if (!result) break;
        if (!opened) {
            std::vector<std::string> columns;
            result = ColumnsFor(connection, options, page.Columns(), columns);
            if (!result) break;
            if (!writer.Open(path, options, columns, error)) { result = Failed(path, error); break; }
            opened = true;
        }
        for (const auto& row : page) {
            if (!writer.Write(row, error)) { result = Failed(path, error); break; }
        }
        if (!result) break;
        if (progress) progress(writer.Rows());
        if (cancel && *cancel) {
            result = UltraDbResult::Error(UltraDbResultCode::Cancelled, "stopped");
            break;
        }
    }
    UltraDb_CloseCursor(cursor);
    if (!result) return result;      // the writer removes what it began
    if (!writer.Finish(error)) return Failed(path, error);
    written = writer.Rows();
    return UltraDbResult::Ok();
}

UltraDbResult ReadRows(const std::string& connection, const std::string& sql, size_t limit, UltraDbResultSet& out,
                       bool& truncated, const UltraDbParams& params) {
    out = UltraDbResultSet();
    truncated = false;
    UltraDbResult result;
    const UltraDbHandle cursor = UltraDb_OpenCursor(connection, sql, params, &result);
    if (cursor == UltraDbInvalidHandle) return result;
    bool exhausted = false, first = true;
    UltraDbResultSet page;
    while (!exhausted) {
        result = UltraDb_FetchRows(cursor, kPageRows, page, exhausted);
        if (!result) break;
        if (first) { out.SetColumns(page.Columns()); first = false; }
        for (const auto& row : page) {
            if (out.Size() == limit) { truncated = true; break; }
            std::vector<UltraDbValue> values;
            for (size_t i = 0; i < row.Size(); ++i) values.push_back(row[i]);
            out.AddRow(std::move(values));
        }
        if (truncated) break;
    }
    UltraDb_CloseCursor(cursor);
    return result;
}

UltraDbResult TableQuery(const std::string& connection, const UltraDbTableSchema& table, std::string& sql) {
    std::string target;
    UltraDbResult result = UltraDb_QuoteQualifiedName(connection, table.schema, table.name, target);
    if (!result) return result;
    sql = "SELECT * FROM " + target;
    std::string order;
    for (const auto& column : table.primaryKey) {
        std::string quoted;
        result = UltraDb_QuoteIdentifier(connection, column, quoted);
        if (!result) return result;
        order += (order.empty() ? "" : ", ") + quoted;
    }
    if (!order.empty()) sql += " ORDER BY " + order;
    return UltraDbResult::Ok();
}

UltraDbResult SqlitePreamble(const std::string& connection, const UltraDbTableSchema& table, std::string& out) {
    out.clear();
    if (table.kind != UltraDbObjectKind::Table) return UltraDbResult::Ok();
    SqliteTableSource source;
    const UltraDbResult result = ReadSqliteTableSource(connection, table.schema, table.name, source);
    if (!result) return result;
    out = source.createSql + ";\n";
    for (const auto& [name, sql] : source.indexes) out += sql + ";\n";
    return UltraDbResult::Ok();
}

} // namespace UltraDBAdmin
