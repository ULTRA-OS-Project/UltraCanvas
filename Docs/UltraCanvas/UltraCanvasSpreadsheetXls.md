# UltraCanvasSpreadsheetXls — legacy Excel workbooks (.xls)

<!-- doc-check: std::string path; -->

`UltraCanvasSpreadsheetXls.h` reads the workbooks Excel wrote before 2007:
**Excel 97-2003 (BIFF8)** and **Excel 5.0/95 (BIFF5)**, the record streams
stored inside an OLE2 compound file
([`UltraCanvasCompoundFile`](UltraCanvasCompoundFile.md)). It also reads what
else arrives under the `.xls` name and Excel opens as a workbook: an **HTML
table** (what web applications and Excel's "Save as Web Page" write) and the
**Excel 2003 XML Spreadsheet**.

The reader produces a plain model — no UI — and has two users:

- `UltraCanvasSpreadsheet::LoadXLS`, which `LoadFromFile` calls for `.xls`, and
  builds an editable sheet from it;
- the file display's preview page (`UltraCanvasFilerWidget`), which only reads
  the first rows of the first sheet.

There is **no writer**: save an opened `.xls` as `.xlsx` or `.ods`.
`SaveToFile` refuses `.xls` with a message saying so.

## Opening one in a spreadsheet

```cpp
#include "UltraCanvasSpreadsheet.h"

auto sheet = std::make_shared<UltraCanvas::UltraCanvasSpreadsheet>("Book", 0, 0, 800, 600);
if (!sheet->LoadFromFile(path)) {          // or sheet->LoadXLS(path)
    std::string why = sheet->GetLastError();   // "password-protected", "not an Excel workbook", ...
}
```

What arrives: every worksheet (hidden ones stay hidden; chart sheets are left
out), typed values — dates, times, percentages and currency by their number
format, with the 1904 date system converted — formulas with the results Excel
cached, merged cells, column widths, row heights, hidden rows and columns,
defined names that name one range, and the cell style subset the spreadsheet
models (font, fill, borders, alignment, wrap, rotation, indent).

`LoadXLS` looks at the content first, because a file named `.xls` is not always
one:

| The file holds | It is read by |
|---|---|
| an OLE2 compound file | the BIFF8 / BIFF5 reader |
| a ZIP package (an `.xlsx` renamed) | `LoadXLSX` |
| an HTML page with a table | the HTML reader below |
| `<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet">` | the Excel 2003 XML reader |
| text (UTF-8, a code page, or UTF-16 with a BOM) | `LoadCSV`, which detects the separator |
| other binary data | nothing — refused with a message |

## Formulas

Excel stores a formula as a postfix token list. The reader turns it into the
engine's own syntax: A1 references, `'Sheet Name'.A1` across sheets (the engine
does not read Excel's `Sheet!A1`), shared formulas expanded for every cell of
their range, defined names by name. Precedence is rebuilt with parentheses
where a writer left them out.

Every formula cell keeps its **cached result**, so the values are always what
Excel computed. The formula itself is kept only when the engine can evaluate it
— it parses, and every function and name in it exists in this spreadsheet.
Otherwise the cell holds the value alone, because a formula that recalculates to
`#NAME?` is worse than none. Formulas that cannot be expressed at all (an
external workbook, an array constant, an error literal, a deleted reference, an
add-in function) are left untranslated the same way.

## Reading the model directly

```cpp
#include "UltraCanvasSpreadsheetXls.h"

UltraCanvas::XlsWorkbook book;
std::string error;
if (UltraCanvas::ReadXlsWorkbook(path, book, error)) {
    for (const UltraCanvas::XlsSheet& s : book.sheets) {
        for (const UltraCanvas::XlsCell& cell : s.cells) {
            std::string shown = UltraCanvas::XlsCellText(cell);   // "12.5", "TRUE", "#N/A"
            if (cell.hasFormula && !cell.formula.empty()) {
                std::string formula = cell.formula;              // "=SUM(B2:B4)"
            }
            if (cell.format >= 0) {
                const UltraCanvas::XlsCellFormat& f = book.formats[static_cast<size_t>(cell.format)];
                UltraCanvas::NumberFormatCategory kind =
                    UltraCanvas::ExcelNumberFormatCategory(f.numberFormatId, f.numberFormatCode);
                (void)kind;   // Date, Time, Percentage, Currency, ...
            }
        }
    }
}
```

`XlsReadOptions` limits a read — `maxSheets`, `maxRows`, `maxColumns`, and
`translateFormulas = false` when only values are wanted, as for a preview:

```cpp
UltraCanvas::XlsReadOptions preview;
preview.maxSheets = 1;
preview.maxRows = 20;
preview.translateFormulas = false;
UltraCanvas::XlsWorkbook firstRows;
std::string error;
UltraCanvas::ReadXlsWorkbook(path, firstRows, error, preview);
```

`DetectXlsFileKind(path)` answers what a file holds (the table above) from its
first bytes. `ReadXlsWorkbook` reads the `Biff`, `Html` and `XmlSpreadsheet`
kinds and returns false, with a reason, for the others.
`ReadXlsWorkbookFromMemory` reads a binary workbook already in memory.

`ExcelNumberFormatCategory` is shared with the `.xlsx` loader: Excel's numFmt
ids (0-49 built in, 164 and up custom) and codes are the same in both formats.
It ignores quoted literals, escaped characters, padding and the bracketed
sections of a code, so `[Red]` is not read as a day and `[$-409]` not as digits,
and it reads `[h]:mm:ss` as a time and `[$€-407]` as currency.

## Model reference

| Type | Holds |
|---|---|
| `XlsWorkbook` | `biffVersion` (8, 5, or 0 for HTML / XML), `date1904`, `codePage`, `fonts`, `formats`, `sheets`, `names` |
| `XlsSheet` | `name`, `hidden`, `cells` (file order), `columns`, `rows`, `merges`, `defaultColumnWidthChars`, `defaultRowHeightPoints` |
| `XlsCell` | `row`, `col`, `type` (`XlsValueType`), `number` / `text` / `boolean` / `errorCode`, `hasFormula`, `formula`, `format` |
| `XlsCellFormat` | one XF record: `font`, `numberFormatId`, `numberFormatCode`, alignment, `wrapText`, `rotation`, `indent`, `fillPattern` and colours, four `XlsBorderLine`s |
| `XlsFont` | `name`, `sizePoints`, bold / italic / strikethrough / underline, super- / subscript, `automaticColor`, `color` |
| `XlsColumnInfo` / `XlsRowInfo` | widths in characters / heights in points, `hidden`, whether the size was set by hand |
| `XlsDefinedName` | `name`, `sheetScope`, `builtIn`, `formula`, and the range when it names one (`refSheet` ≥ 0) |

Dates are serial numbers, as in the file: with `date1904` the serial counts from
1904-01-01, so add 1462 for the usual 1900 system (`LoadXLS` does).

## What is not read

Charts, drawings, comments, conditional formats, data validation, pivot
tables and macros. **Encrypted** workbooks (a `FILEPASS` record, or an
encrypted Excel 2007+ package) are refused with a message that says they are
password-protected. Excel 2.x–4.0 worksheets (BIFF2-4, no compound file) are
not read. BIFF5 strings are decoded from their code page: Windows-1250, 1251
and 1252 are known; any other is read as Latin-1. In a BIFF5 workbook,
references to other sheets keep their cached values without a formula.

## Implementation notes

- Records are split once, with each `CONTINUE` record appended to the record
  it continues and its boundary remembered: a BIFF8 string's characters may run
  on into the next `CONTINUE`, which then begins with its own "16-bit
  characters" flag. The shared string table depends on this.
- Fonts and formats keep palette indices until the end of the workbook globals,
  because the `PALETTE` record follows the records that use it.
- Font index 4 is never stored, so an XF's font index above 3 is one less in
  the list of `FONT` records.
- Worksheets are found by the stream offset their `BOUNDSHEET` gives, and by
  their order when a writer left the offsets wrong.
- Relative references in shared formulas are offsets from the cell using
  them. A 3-D reference in a shared formula is translated only when it is
  absolute.

## Test

`Tests/SpreadsheetXlsFileTest.cpp` reads an Excel 97-2003 file written by
LibreOffice, an Excel 5.0/95 file, an encrypted one, an HTML table and an Excel
2003 XML file (`Tests/fixtures/xls-*.xls`; `Tests/fixtures/make-xls-fixtures.py`
rebuilds the binary ones). It checks the model, the loaded spreadsheet, the
engine recalculating the translated formulas to Excel's results, the refusals,
and UltraFiler's preview page and detail pane.
