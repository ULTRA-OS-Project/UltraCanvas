// include/UltraCanvasSpreadsheetXls.h
// Reader for legacy Excel workbooks (.xls): Excel 97-2003 (BIFF8) and
// Excel 5.0/95 (BIFF5), the record streams stored in an OLE2 compound file
// (UltraCanvasCompoundFile.h). It also reads what else travels under the .xls
// name and Excel opens as a workbook: an HTML table (what web applications
// export) and the Excel 2003 XML Spreadsheet (SpreadsheetML 2003) - see
// XlsFileKind.
//
// The reader turns a workbook into a plain model - sheets, typed cell values,
// fonts, cell formats, column widths, row heights, merged ranges, defined
// names - without any UI. Two callers use it:
//   * UltraCanvasSpreadsheet::LoadXLS (and LoadFromFile for ".xls"), which
//     builds an editable sheet from the model;
//   * the file display's thumbnail (UltraCanvasFilerWidget), which only wants
//     the first rows of the first sheet (XlsReadOptions limits the read).
//
// Formulas: every formula cell keeps the result Excel cached, so values are
// right whatever the formula contains. Its tokens are also translated into
// the spreadsheet engine's own syntax (A1 references, 'Sheet Name'.A1 across
// sheets, shared formulas expanded per cell); a formula with a construct the
// engine's syntax cannot express (an external workbook, an array constant, an
// error literal, a reference to a deleted cell) keeps `hasFormula` with an
// empty `formula` and is shown by its cached value.
//
// Not read: charts, drawings, comments, conditional formats, data validation,
// macros, and encrypted (password-protected) workbooks, which are refused with
// a message saying so. Excel 2.x-4.0 worksheets (BIFF2-4, no compound file)
// are refused too.
// Specification: [MS-XLS] Excel Binary File Format (.xls) Structure.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSpreadsheetTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// What a cell holds (for a formula cell: the type of its cached result).
enum class XlsValueType {
    Empty,     // a formatted cell without a value (BLANK / MULBLANK)
    Number,    // dates, times, percentages and currency are numbers too -
               // the cell's number format says which (ExcelNumberFormatCategory)
    Text,
    Boolean,
    Error
};

struct XlsCell {
    int row = 0;                  // 0-based
    int col = 0;
    XlsValueType type = XlsValueType::Empty;
    double number = 0.0;          // Number
    std::string text;             // Text, UTF-8
    bool boolean = false;         // Boolean
    uint8_t errorCode = 0;        // Error: the BIFF code (0x07 = #DIV/0!, see XlsErrorText)
    bool hasFormula = false;      // the value above is a formula's cached result
    std::string formula;          // the formula in the engine's syntax ("=SUM(B2:B4)"),
                                  // empty when it could not be translated
    int format = -1;              // index into XlsWorkbook::formats, -1 = none
};

struct XlsFont {
    std::string name = "Arial";
    double sizePoints = 10.0;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    int underline = 0;            // 0 none, 1 single, 2 double (accounting forms map to these)
    bool superscript = false;
    bool subscript = false;
    bool automaticColor = true;   // the window text colour; `color` is black then
    Color color = Colors::Black;
};

struct XlsBorderLine {
    int style = 0;                // BIFF line style: 0 none, 1 thin, 2 medium, 3 dashed,
                                  // 4 dotted, 5 thick, 6 double, 7 hair, 8 medium dashed,
                                  // 9 dash-dot, 10 medium dash-dot, 11 dash-dot-dot,
                                  // 12 medium dash-dot-dot, 13 slanted dash-dot
    Color color = Colors::Black;
};

// One XF record - the formatting a cell refers to by index.
struct XlsCellFormat {
    int font = 0;                 // index into XlsWorkbook::fonts
    int numberFormatId = 0;       // Excel's numFmt id (0 = General, 14 = date, ...)
    std::string numberFormatCode; // its format code ("General", "0.00%", "yyyy-mm-dd", ...)
    int horizontalAlign = 0;      // 0 general, 1 left, 2 centre, 3 right, 4 fill, 5 justify,
                                  // 6 centre across selection, 7 distributed
    int verticalAlign = 2;        // 0 top, 1 centre, 2 bottom, 3 justify, 4 distributed
    bool wrapText = false;
    bool shrinkToFit = false;
    int rotation = 0;             // degrees, -90..90 (vertical stacked text reads as 0)
    int indent = 0;
    bool locked = true;
    bool formulaHidden = false;
    int fillPattern = 0;          // BIFF pattern: 0 none, 1 solid, 2-18 the shaded patterns
    Color patternColor = Colors::Black;      // the solid fill colour when fillPattern == 1
    Color backgroundColor = Colors::White;
    XlsBorderLine left, right, top, bottom;
};

struct XlsColumnInfo {
    int firstColumn = 0;
    int lastColumn = 0;
    double widthChars = 0.0;      // in characters of the default font, padding included
    bool hidden = false;
    bool customWidth = false;     // set by the user rather than fitted to the content
    int format = -1;
};

struct XlsRowInfo {
    int row = 0;
    double heightPoints = 0.0;
    bool customHeight = false;    // the row does not follow its content's height
    bool hidden = false;
};

struct XlsMergedRange {
    int firstRow = 0;
    int firstColumn = 0;
    int lastRow = 0;
    int lastColumn = 0;
};

struct XlsSheet {
    std::string name;
    bool hidden = false;          // hidden or "very hidden" in Excel
    std::vector<XlsCell> cells;   // in file order: by row, then column
    std::vector<XlsColumnInfo> columns;
    std::vector<XlsRowInfo> rows;
    std::vector<XlsMergedRange> merges;
    int defaultColumnWidthChars = 8;      // DEFCOLWIDTH, padding excluded
    double defaultRowHeightPoints = 0.0;  // 0 when the sheet does not say
};

struct XlsDefinedName {
    std::string name;             // "Rate"; built-in names by their Excel name ("Print_Area")
    int sheetScope = -1;          // index into XlsWorkbook::sheets, -1 = the whole workbook
    bool builtIn = false;
    std::string formula;          // what it refers to, engine syntax without '=', or empty
    // When it names one rectangle on one worksheet: that rectangle.
    int refSheet = -1;            // index into XlsWorkbook::sheets, -1 = not a plain range
    int firstRow = 0;
    int firstColumn = 0;
    int lastRow = 0;
    int lastColumn = 0;
};

struct XlsWorkbook {
    int biffVersion = 8;          // 5 (Excel 5.0/95) or 8 (Excel 97-2003); 0 for an
                                  // HTML page or an Excel 2003 XML workbook
    bool date1904 = false;        // serial 0 is 1904-01-01 rather than 1899-12-30
    int codePage = 1252;          // of BIFF5's 8-bit strings; BIFF8 text is UTF-16
    std::vector<XlsFont> fonts;
    std::vector<XlsCellFormat> formats;
    std::vector<XlsSheet> sheets; // worksheets only, in tab order (chart sheets left out)
    std::vector<XlsDefinedName> names;
};

struct XlsReadOptions {
    int maxSheets = -1;           // read at most this many worksheets (-1 = all)
    int maxRows = -1;             // keep only cells in the first maxRows rows (-1 = all)
    int maxColumns = -1;          // keep only cells in the first maxColumns columns
    bool translateFormulas = true;
};

// What a file named .xls really holds. Web applications and older exporters
// write HTML tables, Excel 2003 XML or tab-separated text under the name, and
// some programs save an .xlsx with the old extension; Excel opens all of them.
enum class XlsFileKind {
    Missing,         // the file cannot be opened
    Biff,            // an OLE2 compound file - a real .xls (or another Office binary)
    OpenXml,         // a ZIP package: an .xlsx renamed
    Html,            // an HTML (or XHTML) page, typically one <table>
    XmlSpreadsheet,  // Excel 2003 XML Spreadsheet (<Workbook xmlns="urn:schemas-
                     // microsoft-com:office:spreadsheet">)
    Text,            // delimited text (UTF-8, a code page, or UTF-16 with its BOM -
                     // Excel's "Unicode Text")
    Unknown,         // binary data that is none of the above
};
XlsFileKind DetectXlsFileKind(const std::string& filePath);

// Reads a workbook of kind Biff, Html or XmlSpreadsheet (an HTML page becomes
// one sheet, its tables one under another; numbers are read dot-decimal, or
// from Excel's own x:num attribute). `filePath` is UTF-8 on every platform.
// Returns false with a reason in `error` for the other kinds - read an
// OpenXml file as .xlsx and Text as CSV - and when the workbook is encrypted
// or damaged beyond reading; `out` then holds what was read.
bool ReadXlsWorkbook(const std::string& filePath, XlsWorkbook& out, std::string& error,
                     const XlsReadOptions& options = XlsReadOptions());
// The same for a binary workbook already in memory.
bool ReadXlsWorkbookFromMemory(std::vector<uint8_t> fileBytes, XlsWorkbook& out,
                               std::string& error,
                               const XlsReadOptions& options = XlsReadOptions());

// The text Excel shows for a BIFF error code ("#DIV/0!", "#N/A", ...), and the
// engine's error type for it.
std::string XlsErrorText(uint8_t code);
CellErrorType XlsErrorType(uint8_t code);

// A cell's value as plain text, the way a preview lists it: numbers in the
// shortest round-tripping dot-decimal form, TRUE / FALSE, error texts.
std::string XlsCellText(const XlsCell& cell);

// What an Excel number format makes of a number: a date, a time, a
// percentage, currency, ... The numFmt ids (0-49 built in, 164+ custom) and
// codes are the same in .xls and .xlsx, so the .xlsx loader uses this too.
NumberFormatCategory ExcelNumberFormatCategory(int numFmtId, const std::string& formatCode);

} // namespace UltraCanvas
