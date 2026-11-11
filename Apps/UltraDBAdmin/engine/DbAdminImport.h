// Apps/UltraDBAdmin/engine/DbAdminImport.h
// Rows from a file into a table: the file read into a grid of cells, each
// table column given a column of the file (or left to its default), and every
// row inserted in one transaction - all of them, or, when one fails, none.
//
// The grid is what the reader made of the file: a CSV or TSV file through the
// framework's CSV engine (UltraCanvasCSVImport.h, with the options of its
// Text Import dialog), a workbook through its spreadsheet reader (the window
// does that, as it needs the UI library). A cell keeps its text as read and,
// when it reads as a number, the number in dot-decimal form: "1.234,56" from a
// German export is 1234.56 for a numeric column, while a text column gets the
// text as written - so a postcode "01067" stays "01067".
//
// Values are bound as parameters, as Browse binds them, and the database
// converts them to the column's type; a value a column refuses is the
// database's error, naming the file row it came from.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include "UltraCanvasCSVImport.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UltraDBAdmin {

using UltraCanvas::CSVImportOptions;   // the CSV engine's options, as its Text Import dialog sets them

struct ImportCell {
    bool empty = true;
    std::string text;      // as read
    std::string number;    // the value as a dot-decimal number, when it reads as one; else ""

    static ImportCell Text(std::string text);
    static ImportCell Number(std::string number);   // a number cell: text and number alike
};

struct ImportGrid {
    std::string name;      // the sheet's name, or the file's
    std::vector<std::vector<ImportCell>> rows;   // ragged rows read as padded with empty cells
    std::vector<size_t> rowNumbers;              // each row's number in the file, from 1
    size_t Width() const;
    void AddRow(std::vector<ImportCell> row, size_t number);
};

// "1.234,56" with ',' as the decimal mark and '.' grouping -> "1234.56";
// "-3" -> "-3"; "" for anything that is not a plain number ("12 kg", "1e",
// "--1", a date).
std::string NormalizeNumber(const std::string& raw, char decimalMark, char groupMark);

// CSV text, already UTF-8 (CSVDecodeToUtf8), parsed with `options`: its
// separators, quote, start row, and its decimal and thousands separators for
// the numbers. Rows with no value at all are left out.
ImportGrid GridFromCsv(const std::string& text, const CSVImportOptions& options, std::string name);

struct ImportMapping {
    bool header = true;          // the first row names the columns, and is not imported
    bool emptyIsNull = true;     // an empty cell is NULL; else empty text
    std::vector<int> source;     // per table column, in table order: the file column, or -1 (its default)
};

// What the form calls the file's columns: the header's names, "Column 3"
// where a header cell is empty, or "Column 1", "Column 2"... without one.
std::vector<std::string> FileColumnNames(const ImportGrid& grid, bool header);

// The first row names columns when one of its cells is a column's name, in
// any case - so a file whose first row is data is not taken for a header.
bool GuessHeader(const UltraDbTableSchema& table, const ImportGrid& grid);

// Each table column the file column of its name; without a header, or with
// no name matching, the file's columns in order. A computed column gets none.
ImportMapping AutoMapping(const UltraDbTableSchema& table, const ImportGrid& grid, bool header);

// Why this mapping cannot be imported, or "": nothing mapped, no rows, a
// column the database computes given a value, a NOT NULL column without a
// default given none.
std::string CheckMapping(const UltraDbTableSchema& table, const ImportGrid& grid, const ImportMapping& mapping);

// The rows to insert: the grid's, less the header.
size_t DataRows(const ImportGrid& grid, const ImportMapping& mapping);

// A declared type that holds numbers, read as SQLite reads it for affinity:
// int, numeric, decimal, real, double, float, serial. An exact numeric comes
// back from PostgreSQL as text; its type says it is a number.
bool NumericType(const std::string& declaredType);

// What a cell is bound as for `column`: its number for a column of numbers
// (by value type, or by declared type - int, numeric, decimal, real,
// double, float, serial - as PostgreSQL's numeric comes back as text), else
// its text.
UltraDbValue ImportValue(const ImportCell& cell, const UltraDbColumnSchema& column, bool emptyIsNull);

// The first `limit` rows as they will be inserted - the mapped columns, the
// values they get - for the form's preview.
UltraDbResultSet ImportPreview(const UltraDbTableSchema& table, const ImportGrid& grid,
                               const ImportMapping& mapping, size_t limit);

// INSERT every data row, in one transaction. On failure nothing is
// inserted; `failedRow` is the row's number in the file, counting from 1 and
// counting the header, as a spreadsheet numbers it. Call it on a worker.
UltraDbResult ImportRows(const std::string& connection, const UltraDbTableSchema& table, const ImportGrid& grid,
                         const ImportMapping& mapping, size_t& imported, size_t& failedRow);

} // namespace UltraDBAdmin
