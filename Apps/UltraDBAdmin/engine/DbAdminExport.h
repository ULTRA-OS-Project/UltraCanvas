// Apps/UltraDBAdmin/engine/DbAdminExport.h
// Rows out to a file: a whole table, read through a cursor a page at a time so
// ten million rows are never held at once, or the rows a console statement
// returned. Three formats are written here, row by row:
//   CSV / TSV - through the framework's CSV writer (CSVFormatRecord,
//               CSVEncodeFromUtf8) with the options of its Text Export
//               dialog: separator, quote, quoting, line ending, charset, BOM;
//   JSON      - an array of objects, one per row, a line each: numbers as
//               numbers, NULL as null, a blob as "\x..." hex;
//   SQL       - INSERT statements in the source engine's dialect, between
//               BEGIN and COMMIT, after the table's own CREATE TABLE where
//               there is one (a SQLite table; PostgreSQL's definition is
//               pg_dump's to write).
// A workbook (ODS, XLSX) is the framework spreadsheet's to write; the window
// does that, with rows it fetched through ReadRows.
//
// The file is written beside its final name and moved there when complete,
// so an export that fails or is stopped never leaves half a file in place of
// one that was there.
//
// The same three formats copy rows to the clipboard (RowsText): the rows
// selected in Browse or in the console's result.
// Version: 0.3.0 - ExportOptions::postamble
// Version: 0.2.0 - RowsText, QuotedName: rows as text for the clipboard
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminStructureEdit.h"   // SqlEngine

#include <UltraDatabase/UltraDatabase.h>

#include "UltraCanvasCSVImport.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace UltraDBAdmin {

using UltraCanvas::CSVExportOptions;   // the CSV writer's options, as its Text Export dialog sets them

enum class ExportFormat { Csv, Json, Sql };

struct ExportOptions {
    ExportFormat format = ExportFormat::Csv;
    CSVExportOptions csv;          // Csv
    bool header = true;            // Csv: the column names first
    SqlEngine engine = SqlEngine::Sqlite;   // Sql: the literals' dialect
    std::string table;             // Sql: the INSERTs' target, quoted ("main"."orders")
    std::string preamble;          // Sql: written first - a CREATE TABLE and its indexes, or a comment
    std::string postamble;         // Sql: written after the rows, before COMMIT - a counter moved past them
};

// A value as each format writes it.
std::string CsvField(const UltraDbValue& value);                        // NULL: empty
std::string JsonValue(const UltraDbValue& value);
std::string SqlLiteral(const UltraDbValue& value, SqlEngine engine);

// A name as SQLite and PostgreSQL both read it: in double quotes, a quote
// inside doubled - "order ""items""".
std::string QuotedName(const std::string& name);

// Rows as text, the way the file formats write them: CSV (a header line,
// commas, quotes only where needed, LF), JSON (an array of objects) or SQL
// INSERT statements into `table`, written as given (quoted by the caller).
// `columns` names the rows' values in order.
std::string RowsText(const std::vector<std::string>& columns, const std::vector<UltraDbRow>& rows,
                     ExportFormat format, SqlEngine engine, const std::string& table);

// How a workbook cell holds a value: nothing, a number, TRUE / FALSE, or text
// - never a formula. An exact numeric that comes back as text is a number in
// a column of numbers (NumericType), when it reads back the same.
struct WorkbookCell {
    enum class Kind { Empty, Number, Boolean, Text };
    Kind kind = Kind::Empty;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};
WorkbookCell WorkbookValue(const UltraDbValue& value, bool numericColumn);

// Writes one file, row by row.
class ExportWriter {
public:
    ExportWriter() = default;
    ~ExportWriter();               // an unfinished file is removed
    ExportWriter(const ExportWriter&) = delete;
    ExportWriter& operator=(const ExportWriter&) = delete;

    bool Open(const std::string& path, const ExportOptions& options, const std::vector<std::string>& columns,
              std::string& error);
    bool Write(const UltraDbRow& row, std::string& error);
    // Completes the file and puts it in place.
    bool Finish(std::string& error);
    size_t Rows() const { return rows_; }

private:
    bool Put(const std::string& utf8, std::string& error);

    ExportOptions options_;
    std::vector<std::string> columns_;
    std::string path_, partial_;
    std::FILE* file_ = nullptr;
    bool firstBytes_ = true;       // the BOM goes before them
    size_t rows_ = 0;
};

// Rows already in memory: a console statement's. `connection` quotes the
// column names of a SQL export.
UltraDbResult ExportResult(const std::string& connection, const UltraDbResultSet& rows, const std::string& path,
                           const ExportOptions& options, size_t& written);

// A query's rows, a page at a time through a cursor. `cancel`, when set,
// stops it between pages (the file is not left behind). `progress`, when
// given, hears the count after each page. `params` are the query's bound
// values (Browse's filters). Call it on a worker.
UltraDbResult ExportQuery(const std::string& connection, const std::string& sql, const std::string& path,
                          const ExportOptions& options, size_t& written, const std::atomic<bool>* cancel = nullptr,
                          const std::function<void(size_t)>& progress = nullptr, const UltraDbParams& params = {});

// Every row of a query into memory, up to `limit` (`truncated` says whether
// there were more) - for a workbook, which the spreadsheet writes whole.
UltraDbResult ReadRows(const std::string& connection, const std::string& sql, size_t limit, UltraDbResultSet& out,
                       bool& truncated, const UltraDbParams& params = {});

// SELECT * FROM the table, in key order where it has a key - every name quoted.
UltraDbResult TableQuery(const std::string& connection, const UltraDbTableSchema& table, std::string& sql);

// What a SQL export of a SQLite table writes first: its CREATE TABLE and the
// CREATE INDEX statements, so the file rebuilds the table. "" elsewhere.
UltraDbResult SqlitePreamble(const std::string& connection, const UltraDbTableSchema& table, std::string& out);

} // namespace UltraDBAdmin
