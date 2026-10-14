// Tests/SpreadsheetXlsFileTest.cpp
// Legacy Excel workbooks (.xls), end to end:
//   * the OLE2 compound-file reader (UCCompoundFileReader) the .doc and .xls
//     readers share;
//   * the .xls reader's model (ReadXlsWorkbook) for an Excel 97-2003 (BIFF8)
//     file written by LibreOffice, an Excel 5.0/95 (BIFF5) file, an HTML table
//     and an Excel 2003 XML Spreadsheet saved under the .xls name - values,
//     number formats, the formulas translated into the engine's syntax (shared
//     formulas, cross-sheet references, a defined name), fonts, fills,
//     borders, merges, sizes and what is hidden;
//   * UltraCanvasSpreadsheet::LoadFromFile on each, and the engine
//     recalculating the translated formulas to the values Excel cached;
//   * what is refused: an encrypted workbook, a Word document, binary junk;
//     and what is read through another reader: an .xlsx and delimited text
//     under the .xls name;
//   * the file display's preview page (UltraCanvasFilerWidget::
//     TextPreviewLines) and the media viewer's detail pane accepting .xls and
//     .xlsx.
// The fixtures are in Tests/fixtures (make-xls-fixtures.py rebuilds the
// binary ones and says how each was made).
// Version: 1.0.1
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetXls.h"
#include "UltraCanvasSupportedFormats.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#ifndef XLSTEST_FIXTURE_DIR
#error "XLSTEST_FIXTURE_DIR must name Tests/fixtures"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-9 * std::max(1.0, std::fabs(b)); }

std::string Fixture(const char* name) { return std::string(XLSTEST_FIXTURE_DIR) + "/" + name; }

const XlsCell* FindCell(const XlsSheet& sheet, int row, int col) {
    for (const XlsCell& c : sheet.cells) {
        if (c.row == row && c.col == col) return &c;
    }
    return nullptr;
}

std::string FormulaAt(const XlsSheet& sheet, int row, int col) {
    const XlsCell* c = FindCell(sheet, row, col);
    return c ? c->formula : std::string("<no cell>");
}

NumberFormatCategory CategoryAt(const XlsWorkbook& wb, const XlsSheet& sheet, int row, int col) {
    const XlsCell* c = FindCell(sheet, row, col);
    if (!c || c->format < 0) return NumberFormatCategory::General;
    const XlsCellFormat& f = wb.formats[static_cast<size_t>(c->format)];
    return ExcelNumberFormatCategory(f.numberFormatId, f.numberFormatCode);
}

const XlsFont* FontAt(const XlsWorkbook& wb, const XlsSheet& sheet, int row, int col) {
    const XlsCell* c = FindCell(sheet, row, col);
    if (!c || c->format < 0) return nullptr;
    const int font = wb.formats[static_cast<size_t>(c->format)].font;
    return font >= 0 && font < static_cast<int>(wb.fonts.size()) ? &wb.fonts[static_cast<size_t>(font)]
                                                                 : nullptr;
}

const XlsCellFormat* FormatAt(const XlsWorkbook& wb, const XlsSheet& sheet, int row, int col) {
    const XlsCell* c = FindCell(sheet, row, col);
    return c && c->format >= 0 ? &wb.formats[static_cast<size_t>(c->format)] : nullptr;
}

const SpreadsheetCell* CellOf(const UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    const SpreadsheetSheet* s = book.GetSheet(sheet);
    return s ? s->GetCellIfExists(row, col) : nullptr;
}

double NumberOf(const UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    const SpreadsheetCell* c = CellOf(book, sheet, row, col);
    return c ? c->GetNumber() : std::nan("");
}

std::string FormulaOf(const UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    const SpreadsheetCell* c = CellOf(book, sheet, row, col);
    return c ? c->GetFormulaText() : std::string("<no cell>");
}

std::string TextOf(const UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    const SpreadsheetCell* c = CellOf(book, sheet, row, col);
    return c ? c->GetText() : std::string("<no cell>");
}

bool WriteFile(const std::string& path, const std::string& content) {
    std::FILE* f = OpenFileUtf8(path, "wb");
    if (!f) return false;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

// ===== THE COMPOUND FILE =====

void TestCompoundFile() {
    std::cout << "=== OLE2 compound files ===\n";
    const std::string biff8 = Fixture("xls-biff8-sample.xls");
    Check(UCCompoundFileReader::HasSignature(biff8), "an .xls carries the compound-file signature");
    UCCompoundFileReader cfb;
    Check(cfb.Open(biff8), "the .xls opens as a compound file");
    bool listed = false;
    for (const std::string& name : cfb.StreamNames()) listed = listed || name == "Workbook";
    Check(listed, "its root lists the Workbook stream");
    std::vector<uint8_t> stream;
    Check(cfb.ReadStream("WORKBOOK", stream) && stream.size() > 1000,
          "streams are found without regard to case");
    Check(stream.size() >= 4 && stream[0] == 0x09 && stream[1] == 0x08,
          "the stream starts with a BOF record");
    Check(!cfb.HasStream("NoSuchStream"), "an absent stream is absent");

    UCCompoundFileReader doc;
    Check(doc.Open(Fixture("legacy-word97.doc")) && doc.HasStream("WordDocument"),
          "a Word 97 document opens too (the .doc importer reads through it)");

    UCCompoundFileReader text;
    Check(!text.Open(Fixture("xls-html-export.xls")) && !text.GetLastError().empty(),
          "a file that is no compound file is refused with a reason");
}

// ===== EXCEL 97-2003 (BIFF8) =====

void TestBiff8Model() {
    std::cout << "\n=== Excel 97-2003 (BIFF8), the reader's model ===\n";
    XlsWorkbook wb;
    std::string error;
    const bool ok = ReadXlsWorkbook(Fixture("xls-biff8-sample.xls"), wb, error);
    Check(ok, "the workbook reads");
    if (!ok) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(wb.biffVersion == 8 && !wb.date1904, "BIFF8, 1900 date system");
    Check(wb.sheets.size() == 4, "four worksheets");
    if (wb.sheets.size() != 4) return;
    Check(wb.sheets[0].name == "Data" && wb.sheets[1].name == "Second Sheet" &&
              wb.sheets[2].name == "Strings" && wb.sheets[3].name == "Hidden",
          "sheet names in tab order");
    Check(!wb.sheets[0].hidden && wb.sheets[3].hidden, "the hidden sheet is marked hidden");

    const XlsSheet& data = wb.sheets[0];
    const XlsCell* a2 = FindCell(data, 1, 0);
    Check(a2 && a2->type == XlsValueType::Text && a2->text == "Apple", "A2 is the text Apple");
    const XlsCell* a3 = FindCell(data, 2, 0);
    Check(a3 && a3->text == "\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2", "A3 is Thai text");
    const XlsCell* a4 = FindCell(data, 3, 0);
    Check(a4 && a4->text == "\xE6\x9D\xB1\xE4\xBA\xAC \xF0\x9F\x93\x8A",
          "A4 is CJK text with an emoji (a surrogate pair)");
    const XlsCell* b2 = FindCell(data, 1, 1);
    Check(b2 && b2->type == XlsValueType::Number && Near(b2->number, 12.5), "B2 is 12.5");
    const XlsCell* c6 = FindCell(data, 5, 2);
    Check(c6 && Near(c6->number, 123456789.0), "C6 is 123456789 (an RK integer)");
    const XlsCell* d6 = FindCell(data, 5, 3);
    Check(d6 && Near(d6->number, 1e-5), "D6 is 1e-05 (a full IEEE double)");

    Check(CategoryAt(wb, data, 1, 2) == NumberFormatCategory::Date, "C2's format makes it a date");
    Check(FindCell(data, 1, 2) && Near(FindCell(data, 1, 2)->number, 45366.0),
          "C2 holds the serial of 2024-03-15");
    Check(CategoryAt(wb, data, 2, 2) == NumberFormatCategory::Time, "C3's format makes it a time");
    Check(CategoryAt(wb, data, 1, 3) == NumberFormatCategory::Percentage, "D2 is a percentage");
    Check(CategoryAt(wb, data, 3, 1) == NumberFormatCategory::Currency,
          "B4 is currency (a format with an escaped \\$)");

    Check(FormulaAt(data, 4, 1) == "=SUM(B2:B4)", "B5: =SUM(B2:B4)");
    const XlsCell* b5 = FindCell(data, 4, 1);
    Check(b5 && b5->hasFormula && Near(b5->number, 1008.75), "B5 keeps Excel's result 1008.75");
    Check(FormulaAt(data, 4, 2) == "=IF(B5>100,\"big\",\"small\")", "C5: an IF with strings");
    const XlsCell* c5 = FindCell(data, 4, 2);
    Check(c5 && c5->type == XlsValueType::Text && c5->text == "big",
          "C5's text result comes from its STRING record");
    Check(FormulaAt(data, 4, 3) == "=A2&\" pie\"", "D5: a concatenation");
    Check(FormulaAt(data, 1, 4) == "=B2*2" && FormulaAt(data, 2, 4) == "=B3*2" &&
              FormulaAt(data, 9, 4) == "=B10*2",
          "the filled-down shared formula is expanded for every cell");
    const XlsCell* b6 = FindCell(data, 5, 1);
    Check(b6 && b6->type == XlsValueType::Error && b6->errorCode == 0x07 &&
              XlsCellText(*b6) == "#DIV/0!",
          "B6 is the error #DIV/0!");

    Check(data.merges.size() == 1 && data.merges[0].firstRow == 7 && data.merges[0].lastColumn == 2,
          "A8:C8 is merged");
    bool widthA = false, hiddenG = false;
    for (const XlsColumnInfo& c : data.columns) {
        if (c.firstColumn == 0 && std::fabs(c.widthChars - 20.0) < 0.1) widthA = true;
        if (c.firstColumn <= 6 && c.lastColumn >= 6 && c.hidden) hiddenG = true;
    }
    Check(widthA, "column A is 20 characters wide");
    Check(hiddenG, "column G is hidden");
    bool tall = false, hiddenRow = false;
    for (const XlsRowInfo& r : data.rows) {
        if (r.row == 7 && r.customHeight && std::fabs(r.heightPoints - 30.0) < 0.01) tall = true;
        if (r.row == 11 && r.hidden) hiddenRow = true;
    }
    Check(tall, "row 8 is 30 points high");
    Check(hiddenRow, "row 12 is hidden");

    const XlsFont* header = FontAt(wb, data, 0, 0);
    Check(header && header->bold && std::fabs(header->sizePoints - 12.0) < 0.01,
          "the header font is bold 12 pt");
    const XlsCellFormat* headerFormat = FormatAt(wb, data, 0, 0);
    Check(headerFormat && headerFormat->fillPattern == 1 &&
              headerFormat->patternColor == Color(255, 255, 0) && headerFormat->bottom.style == 1,
          "the header has a yellow fill and a thin bottom border");
    const XlsFont* italic = FontAt(wb, data, 8, 0);
    Check(italic && italic->italic && !italic->automaticColor && italic->color == Color(255, 0, 0),
          "A9 is italic red");
    const XlsFont* struck = FontAt(wb, data, 8, 1);
    Check(struck && struck->strikethrough, "B9 is struck through");
    const XlsFont* underlined = FontAt(wb, data, 8, 2);
    Check(underlined && underlined->underline == 1, "C9 is underlined");
    const XlsCellFormat* wrapped = FormatAt(wb, data, 9, 0);
    Check(wrapped && wrapped->wrapText && wrapped->horizontalAlign == 3,
          "A10 wraps and is right-aligned");

    const XlsSheet& second = wb.sheets[1];
    Check(FormulaAt(second, 0, 0) == "='Data'.B5", "a cross-sheet reference: ='Data'.B5");
    Check(FormulaAt(second, 1, 0) == "=SUM('Data'.B2:B4)", "a cross-sheet range in a function");
    Check(FormulaAt(second, 2, 0) == "=VLOOKUP(\"Apple\",'Data'.A2:B4,2,FALSE)",
          "VLOOKUP with a Boolean argument");
    Check(FormulaAt(second, 3, 0) == "=ROUND(PI(),2)", "a function of no arguments inside another");
    Check(FormulaAt(second, 5, 0) == "=Rate*2", "a defined name");
    Check(FormulaAt(second, 6, 0) == "=-B1+3^2", "unary minus and power");
    Check(FormulaAt(second, 7, 0) == "=AVERAGE('Data'.B2:B3)%", "the percent operator");
    Check(FormulaAt(second, 8, 0) == "=ISPMT(0.1,1,3,1000)", "decimal constants");

    Check(wb.names.size() == 1 && wb.names[0].name == "Rate" && wb.names[0].refSheet == 0 &&
              wb.names[0].firstRow == 1 && wb.names[0].firstColumn == 1 &&
              wb.names[0].formula == "'Data'.$B$2",
          "the defined name Rate refers to Data!$B$2");

    // Every string of the shared string table, which spills over CONTINUE
    // records - with two-byte Thai strings among one-byte Latin ones.
    const XlsSheet& strings = wb.sheets[2];
    bool allStrings = strings.cells.size() == 400;
    for (int i = 0; i < 400 && allStrings; ++i) {
        char latin[64];
        std::snprintf(latin, sizeof(latin), "String number %04d of the table", i);
        char thai[96];
        std::snprintf(thai, sizeof(thai),
                      "\xE0\xB8\xAA\xE0\xB8\x95\xE0\xB8\xA3\xE0\xB8\xB4\xE0\xB8\x87 %04d "
                      "\xE0\xB8\x82\xE0\xB8\xAD\xE0\xB8\x87\xE0\xB8\x95\xE0\xB8\xB2\xE0\xB8\xA3"
                      "\xE0\xB8\xB2\xE0\xB8\x87",
                      i);
        const XlsCell* c = FindCell(strings, i, 0);
        allStrings = c && c->text == (i % 7 == 0 ? std::string(thai) : std::string(latin));
        if (!allStrings) std::cout << "         row " << i << ": " << (c ? c->text : "<none>") << "\n";
    }
    Check(allStrings, "all 400 shared strings read back across the CONTINUE records");

    XlsReadOptions preview;
    preview.maxSheets = 1;
    preview.maxRows = 3;
    XlsWorkbook partial;
    Check(ReadXlsWorkbook(Fixture("xls-biff8-sample.xls"), partial, error, preview) &&
              partial.sheets.size() == 1 && FindCell(partial.sheets[0], 2, 0) &&
              !FindCell(partial.sheets[0], 3, 0),
          "a preview read stops at the first sheet's first rows");
}

void TestBiff8Spreadsheet() {
    std::cout << "\n=== Excel 97-2003 (BIFF8) in the spreadsheet ===\n";
    UltraCanvasSpreadsheet book("Xls", 0, 0, 400, 300);
    const bool ok = book.LoadFromFile(Fixture("xls-biff8-sample.xls"));
    Check(ok, "LoadFromFile reads the .xls");
    if (!ok) {
        std::cout << "         " << book.GetLastError() << "\n";
        return;
    }
    Check(book.GetSheetCount() == 4, "four sheets");
    Check(book.GetSheet(3) && !book.GetSheet(3)->IsVisible(), "the hidden sheet stays hidden");
    Check(book.GetActiveSheetIndex() == 0, "the first visible sheet is active");

    Check(TextOf(book, 0, 2, 0) == "\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2", "Thai text arrives");
    const SpreadsheetCell* c2 = CellOf(book, 0, 1, 2);
    Check(c2 && c2->GetValueType() == CellValueType::Date && Near(c2->GetNumber(), 45366.0),
          "C2 is the date 2024-03-15");
    const SpreadsheetCell* c3 = CellOf(book, 0, 2, 2);
    Check(c3 && c3->GetValueType() == CellValueType::Time, "C3 is a time");
    const SpreadsheetCell* d2 = CellOf(book, 0, 1, 3);
    Check(d2 && d2->GetValueType() == CellValueType::Percentage && Near(d2->GetNumber(), 0.25),
          "D2 is 25 %");
    const SpreadsheetCell* b4 = CellOf(book, 0, 3, 1);
    Check(b4 && b4->GetValueType() == CellValueType::Currency && Near(b4->GetNumber(), -3.75),
          "B4 is currency");
    const SpreadsheetCell* b6 = CellOf(book, 0, 5, 1);
    Check(b6 && b6->GetError() == CellErrorType::DivisionByZero, "B6 holds #DIV/0!");

    Check(FormulaOf(book, 0, 4, 1) == "=SUM(B2:B4)" && Near(NumberOf(book, 0, 4, 1), 1008.75),
          "B5 has its formula and Excel's result");
    Check(FormulaOf(book, 0, 2, 4) == "=B3*2", "a shared formula cell has its own formula");
    Check(FormulaOf(book, 1, 0, 0) == "='Data'.B5", "a cross-sheet formula arrives");
    Check(book.HasNamedRange("Rate"), "the defined name is a named range");
    Check(FormulaOf(book, 1, 5, 0) == "=Rate*2", "the formula using it keeps it");
    // The engine has no VLOOKUP or ISPMT: those cells keep the value Excel
    // computed instead of a formula that would recalculate to #NAME?.
    SpreadsheetFormulaEngine* engine = book.GetFormulaEngine();
    const bool hasVlookup = engine && engine->GetFunctionLibrary().HasFunction("VLOOKUP");
    Check(FormulaOf(book, 1, 2, 0).empty() == !hasVlookup && Near(NumberOf(book, 1, 2, 0), 12.5),
          "VLOOKUP: a formula only if the engine has the function; Excel's 12.5 either way");
    const bool hasIspmt = engine && engine->GetFunctionLibrary().HasFunction("ISPMT");
    Check(FormulaOf(book, 1, 8, 0).empty() == !hasIspmt &&
              Near(NumberOf(book, 1, 8, 0), -66.66666666666667),
          "ISPMT: the same");

    const SpreadsheetSheet* data = book.GetSheet(0);
    if (data) {
        Check(data->IsCellMerged(7, 1), "A8:C8 is merged");
        Check(data->GetColumnWidth(0) == 140, "column A is 140 px (20 characters)");
        Check(data->IsColumnHidden(6), "column G is hidden");
        Check(data->GetRowHeight(7) == 40, "row 8 is 40 px (30 points)");
        Check(data->IsRowHidden(11), "row 12 is hidden");
        const SpreadsheetCell* header = data->GetCellIfExists(0, 0);
        Check(header && header->GetStyle().font.bold && header->GetStyle().fill.HasFill() &&
                  header->GetStyle().fill.foregroundColor == Color(255, 255, 0) &&
                  header->GetStyle().borders.bottom.style == BorderStyle::Thin,
              "the header is bold on yellow with a thin bottom border");
        const SpreadsheetCell* italic = data->GetCellIfExists(8, 0);
        Check(italic && italic->GetStyle().font.italic &&
                  italic->GetStyle().font.color == Color(255, 0, 0),
              "A9 is italic red");
        const SpreadsheetCell* plain = data->GetCellIfExists(1, 0);
        Check(plain && !plain->HasCustomStyle(),
              "a cell in the workbook's default format carries no style of its own");
    }

    // The translated formulas recalculate to what Excel computed.
    book.RecalculateAll();
    Check(Near(NumberOf(book, 0, 4, 1), 1008.75), "recalculated: SUM(B2:B4) = 1008.75");
    Check(Near(NumberOf(book, 0, 1, 4), 25.0) && Near(NumberOf(book, 0, 9, 4), 200.0),
          "recalculated: the shared B*2 formulas");
    Check(TextOf(book, 0, 4, 2) == "big", "recalculated: the IF");
    Check(TextOf(book, 0, 4, 3) == "Apple pie", "recalculated: the concatenation");
    Check(Near(NumberOf(book, 1, 0, 0), 1008.75), "recalculated: the cross-sheet reference");
    Check(Near(NumberOf(book, 1, 1, 0), 1008.75), "recalculated: the cross-sheet SUM");
    Check(Near(NumberOf(book, 1, 3, 0), 3.14), "recalculated: ROUND(PI(),2)");
    Check(Near(NumberOf(book, 1, 4, 0), 5.0), "recalculated: LEN");
    Check(Near(NumberOf(book, 1, 5, 0), 25.0), "recalculated: through the named range");
    Check(Near(NumberOf(book, 1, 6, 0), 7.0), "recalculated: -B1+3^2");
    Check(Near(NumberOf(book, 1, 7, 0), 5.0625), "recalculated: AVERAGE(...)%");
}

// ===== EXCEL 5.0/95 (BIFF5) =====

void TestBiff5() {
    std::cout << "\n=== Excel 5.0/95 (BIFF5) ===\n";
    XlsWorkbook wb;
    std::string error;
    const bool ok = ReadXlsWorkbook(Fixture("xls-biff5-sample.xls"), wb, error);
    Check(ok, "the workbook reads");
    if (!ok) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(wb.biffVersion == 5 && wb.date1904 && wb.codePage == 1252,
          "BIFF5, 1904 date system, code page 1252");
    Check(wb.sheets.size() == 2 && wb.sheets[0].name == "Donn\xC3\xA9" "es",
          "a sheet name in the code page arrives as UTF-8");
    if (wb.sheets.size() != 2) return;
    const XlsSheet& s = wb.sheets[0];
    const XlsCell* a1 = FindCell(s, 0, 0);
    Check(a1 && a1->text == "Caf\xC3\xA9 cr\xC3\xA8me", "a LABEL in the code page");
    Check(FindCell(s, 2, 1) && Near(FindCell(s, 2, 1)->number, 12.34), "an RK number divided by 100");
    Check(FindCell(s, 3, 1) && Near(FindCell(s, 3, 1)->number, -7.0) && FindCell(s, 3, 2) &&
              Near(FindCell(s, 3, 2)->number, 0.25),
          "a MULRK run, a negative integer and a scaled value");
    Check(FindCell(s, 4, 0) && FindCell(s, 4, 0)->type == XlsValueType::Boolean &&
              FindCell(s, 4, 0)->boolean,
          "a Boolean");
    Check(FindCell(s, 4, 1) && FindCell(s, 4, 1)->type == XlsValueType::Error &&
              XlsCellText(*FindCell(s, 4, 1)) == "#N/A",
          "an error value");
    Check(CategoryAt(wb, s, 5, 0) == NumberFormatCategory::Date &&
              CategoryAt(wb, s, 5, 1) == NumberFormatCategory::Date,
          "a built-in and a custom date format");
    Check(FormulaAt(s, 7, 1) == "=SUM(B1:B3)", "a BIFF5 area reference");
    Check(FormulaAt(s, 8, 1) == "=A1&\"!\"" && FindCell(s, 8, 1)->text == "Caf\xC3\xA9 cr\xC3\xA8me!",
          "a BIFF5 string constant, and the text result");
    Check(FormulaAt(s, 9, 1) == "=IF(B2>40,\"big\",\"small\")", "an IF around its jump tokens");
    Check(FormulaAt(s, 10, 1) == "=ROUND($B$1*2,1)", "absolute references and a fixed-argument function");
    const XlsFont* styled = FontAt(wb, s, 6, 0);
    Check(styled && styled->name == "Times New Roman" && styled->bold && styled->underline == 1 &&
              styled->color == Color(255, 0, 0) && std::fabs(styled->sizePoints - 14.0) < 0.01,
          "a font past the never-stored index 4");
    const XlsCellFormat* f = FormatAt(wb, s, 6, 0);
    Check(f && f->fillPattern == 1 && f->patternColor == Color(255, 255, 0) && f->wrapText &&
              f->horizontalAlign == 2 && f->bottom.style == 1,
          "a BIFF5 XF: fill, wrap, centre, bottom border");
    bool hidden = false, tall = false, hiddenRow = false;
    for (const XlsColumnInfo& c : s.columns) hidden = hidden || (c.firstColumn == 3 && c.hidden);
    for (const XlsRowInfo& r : s.rows) {
        tall = tall || (r.row == 0 && r.customHeight && std::fabs(r.heightPoints - 20.0) < 0.01);
        hiddenRow = hiddenRow || (r.row == 12 && r.hidden);
    }
    Check(hidden && tall && hiddenRow, "a hidden column, a tall row and a hidden row");
    Check(wb.sheets[1].cells.size() == 200 && FindCell(wb.sheets[1], 199, 0) &&
              FindCell(wb.sheets[1], 199, 0)->text == "Latin-1 line 199 \xC3\xA0 la carte",
          "the second sheet's 200 labels");

    UltraCanvasSpreadsheet book("Xls5", 0, 0, 400, 300);
    Check(book.LoadFromFile(Fixture("xls-biff5-sample.xls")), "LoadFromFile reads it");
    const SpreadsheetCell* date = CellOf(book, 0, 5, 0);
    Check(date && date->GetValueType() == CellValueType::Date && Near(date->GetNumber(), 45366.0),
          "a 1904-system date arrives as 2024-03-15");
    book.RecalculateAll();
    Check(Near(NumberOf(book, 0, 7, 1), 3.14159 + 42 + 12.34), "recalculated: SUM(B1:B3)");
    Check(Near(NumberOf(book, 0, 10, 1), 6.3), "recalculated: ROUND($B$1*2,1)");
}

// ===== WHAT ELSE TRAVELS AS .XLS =====

void TestHtmlTable() {
    std::cout << "\n=== An HTML table saved as .xls ===\n";
    XlsWorkbook wb;
    std::string error;
    Check(DetectXlsFileKind(Fixture("xls-html-export.xls")) == XlsFileKind::Html, "recognised as HTML");
    const bool ok = ReadXlsWorkbook(Fixture("xls-html-export.xls"), wb, error);
    Check(ok && wb.sheets.size() == 1, "read as one sheet");
    if (!ok || wb.sheets.empty()) return;
    const XlsSheet& s = wb.sheets[0];
    const XlsFont* header = FontAt(wb, s, 0, 0);
    Check(FindCell(s, 0, 0) && FindCell(s, 0, 0)->text == "Account" && header && header->bold,
          "a header cell, bold");
    Check(FindCell(s, 1, 0) && FindCell(s, 1, 0)->text == "Rent & utilities", "an entity decoded");
    Check(FindCell(s, 1, 1) && FindCell(s, 1, 1)->type == XlsValueType::Number &&
              Near(FindCell(s, 1, 1)->number, 1234.5),
          "the number Excel wrote in x:num, not the formatted text");
    Check(FindCell(s, 1, 2) && FindCell(s, 1, 2)->type == XlsValueType::Text &&
              FindCell(s, 1, 2)->text == "007",
          "x:str keeps 007 as text");
    Check(FindCell(s, 2, 0) &&
              FindCell(s, 2, 0)->text == "\xE0\xB8\x84\xE0\xB9\x88\xE0\xB8\xB2\xE0\xB9\x84\xE0\xB8\x9F",
          "Thai from numeric character references");
    Check(FindCell(s, 4, 0) && Near(FindCell(s, 4, 0)->number, 350.0), "3.5e2 is a number");
    Check(FindCell(s, 4, 1) && FindCell(s, 4, 1)->type == XlsValueType::Text,
          "12,5 stays text (numbers are dot-decimal)");
    Check(s.merges.size() == 2, "colspan and rowspan become merged ranges");

    UltraCanvasSpreadsheet book("Html", 0, 0, 400, 300);
    Check(book.LoadFromFile(Fixture("xls-html-export.xls")) && Near(NumberOf(book, 0, 1, 1), 1234.5),
          "LoadFromFile reads it");
}

void TestXmlSpreadsheet() {
    std::cout << "\n=== An Excel 2003 XML Spreadsheet saved as .xls ===\n";
    Check(DetectXlsFileKind(Fixture("xls-xml2003-sample.xls")) == XlsFileKind::XmlSpreadsheet,
          "recognised as Excel 2003 XML");
    XlsWorkbook wb;
    std::string error;
    const bool ok = ReadXlsWorkbook(Fixture("xls-xml2003-sample.xls"), wb, error);
    Check(ok && wb.sheets.size() == 2, "two worksheets");
    if (!ok || wb.sheets.size() != 2) {
        std::cout << "         " << error << "\n";
        return;
    }
    const XlsSheet& s = wb.sheets[0];
    Check(!s.hidden && wb.sheets[1].hidden, "the second sheet is hidden");
    Check(FindCell(s, 2, 0) && FindCell(s, 2, 0)->text == "\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2",
          "Thai text");
    Check(FindCell(s, 1, 3) && Near(FindCell(s, 1, 3)->number, 45366.0) &&
              CategoryAt(wb, s, 1, 3) == NumberFormatCategory::Date,
          "a DateTime cell with a Short Date style is the date's serial");
    Check(FindCell(s, 2, 3) && Near(FindCell(s, 2, 3)->number, 45366.0 + 13.75 / 24.0) &&
              CategoryAt(wb, s, 2, 3) == NumberFormatCategory::DateTime,
          "a date and time");
    Check(FormulaAt(s, 3, 1) == "=SUM(B2:B3)", "R1C1 relative references become A1");
    Check(FormulaAt(s, 3, 3) == "=B2/B3" && CategoryAt(wb, s, 3, 3) == NumberFormatCategory::Percentage,
          "a relative formula in a percent cell");
    Check(FindCell(s, 5, 1) && FindCell(s, 5, 1)->type == XlsValueType::Error &&
              FindCell(s, 5, 1)->errorCode == 0x07,
          "an error value");
    Check(FindCell(s, 5, 2) && FindCell(s, 5, 2)->text == "Rich text", "rich text flattened");
    const XlsFont* header = FontAt(wb, s, 0, 0);
    const XlsCellFormat* headerFormat = FormatAt(wb, s, 0, 0);
    Check(header && header->bold && headerFormat && headerFormat->fillPattern == 1 &&
              headerFormat->bottom.style == 1,
          "a bold header on a fill with a bottom border");
    Check(s.merges.size() == 1 && s.merges[0].lastColumn == 2, "MergeAcross");
    const XlsSheet& second = wb.sheets[1];
    Check(FormulaAt(second, 0, 0) == "='Data'.$B$4", "an absolute cross-sheet reference");
    Check(FormulaAt(second, 0, 2) == "=IF(A1>100,\"big\",\"small\")", "RC[-2] and strings");
    Check(FormulaAt(second, 0, 3) == "=ROUND(SUM('Data'.$B$2:$B$3)/2,1)", "a quoted sheet name");
    Check(wb.names.size() == 1 && wb.names[0].refSheet == 0 && wb.names[0].firstRow == 1 &&
              wb.names[0].firstColumn == 1,
          "the named range");

    UltraCanvasSpreadsheet book("Xml", 0, 0, 400, 300);
    Check(book.LoadFromFile(Fixture("xls-xml2003-sample.xls")), "LoadFromFile reads it");
    book.RecalculateAll();
    Check(Near(NumberOf(book, 0, 3, 1), 1012.5), "recalculated: SUM");
    Check(Near(NumberOf(book, 1, 0, 0), 1012.5), "recalculated: the cross-sheet reference");
    Check(Near(NumberOf(book, 1, 0, 1), 25.0), "recalculated: through the named range");
    Check(Near(NumberOf(book, 1, 0, 3), 506.3), "recalculated: ROUND(SUM(...)/2,1)");
}

void TestOtherContent() {
    std::cout << "\n=== Other content under the .xls name, and what is refused ===\n";
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
                         PathFromUtf8("xls-test \xE0\xB8\x95\xE0\xB8\xB2\xE0\xB8\xA3\xE0\xB8\xB2\xE0\xB8\x87");
    fs::create_directories(dir, ec);

    const std::string csv = PathToUtf8(dir / "export.xls");
    WriteFile(csv, "Name\tAmount\nApple\t12.5\nPear\t3\n");
    Check(DetectXlsFileKind(csv) == XlsFileKind::Text, "tab-separated text is recognised as text");
    {
        UltraCanvasSpreadsheet book("Csv", 0, 0, 400, 300);
        Check(book.LoadFromFile(csv) && TextOf(book, 0, 1, 0) == "Apple" &&
                  Near(NumberOf(book, 0, 1, 1), 12.5),
              "and read as delimited text");
    }

    const std::string renamed = PathToUtf8(dir / "renamed.xls");
    {
        UltraCanvasSpreadsheet writer("Writer", 0, 0, 400, 300);
        writer.SetCellValue(0, 0, std::string("from xlsx"));
        Check(writer.SaveXLSX(renamed), "an .xlsx written under an .xls name");
    }
    Check(DetectXlsFileKind(renamed) == XlsFileKind::OpenXml, "is recognised as OOXML");
    {
        UltraCanvasSpreadsheet book("Renamed", 0, 0, 400, 300);
        Check(book.LoadFromFile(renamed) && TextOf(book, 0, 0, 0) == "from xlsx",
              "and read as .xlsx");
    }

    {
        UltraCanvasSpreadsheet book("Encrypted", 0, 0, 400, 300);
        Check(!book.LoadFromFile(Fixture("xls-biff5-encrypted.xls")) &&
                  book.GetLastError().find("password") != std::string::npos,
              "an encrypted workbook is refused, saying it is password-protected");
    }
    const std::string word = PathToUtf8(dir / "letter.xls");
    fs::copy_file(PathFromUtf8(Fixture("legacy-word97.doc")), PathFromUtf8(word),
                  fs::copy_options::overwrite_existing, ec);
    {
        UltraCanvasSpreadsheet book("Word", 0, 0, 400, 300);
        Check(!book.LoadFromFile(word) && book.GetLastError().find("Word") != std::string::npos,
              "a Word document named .xls is refused as one");
    }
    const std::string junk = PathToUtf8(dir / "junk.xls");
    WriteFile(junk, std::string("\x01\x02\x00\x03junk", 8));
    {
        UltraCanvasSpreadsheet book("Junk", 0, 0, 400, 300);
        Check(DetectXlsFileKind(junk) == XlsFileKind::Unknown && !book.LoadFromFile(junk) &&
                  !book.GetLastError().empty(),
              "binary junk is refused, not read as text");
    }
    {
        UltraCanvasSpreadsheet book("Missing", 0, 0, 400, 300);
        Check(!book.LoadFromFile(PathToUtf8(dir / "missing.xls")) && !book.GetLastError().empty(),
              "a missing file says so");
        Check(!book.SaveToFile(PathToUtf8(dir / "out.xls")) &&
                  book.GetLastError().find(".xlsx") != std::string::npos,
              "saving as .xls is refused, pointing at .xlsx");
    }
    fs::remove_all(dir, ec);
}

// ===== THE FILE DISPLAY AND THE DETAIL PANE =====

void TestFilerAndViewer() {
    std::cout << "\n=== UltraFiler: the preview page and the detail pane ===\n";
    std::vector<std::string> lines;
    bool tabular = false;
    Check(UltraCanvasFilerWidget::TextPreviewLines(Fixture("xls-biff8-sample.xls"), lines, tabular) &&
              tabular && !lines.empty() && lines[0].rfind("Name\tAmount\tDate\tShare\tDouble", 0) == 0,
          "an .xls previews as a cell grid of its first sheet");
    Check(lines.size() >= 3 && lines[2].rfind("\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2\t1000", 0) == 0,
          "with its Thai text and numbers");
    lines.clear();
    Check(UltraCanvasFilerWidget::TextPreviewLines(Fixture("xls-biff5-sample.xls"), lines, tabular) &&
              !lines.empty() && lines[0].rfind("Caf\xC3\xA9 cr\xC3\xA8me", 0) == 0,
          "an Excel 95 workbook previews too");
    lines.clear();
    Check(UltraCanvasFilerWidget::TextPreviewLines(Fixture("xls-html-export.xls"), lines, tabular) &&
              !lines.empty() && lines[0] == "Account\tAmount\tNote",
          "and an HTML table saved as .xls");
    lines.clear();
    Check(!UltraCanvasFilerWidget::TextPreviewLines(Fixture("xls-biff5-encrypted.xls"), lines, tabular),
          "an encrypted workbook keeps its type glyph");

    for (const char* name : {"book.xls", "book.xlsx", "BOOK.XLS"}) {
        Check(UltraCanvasMediaViewer::IsSupportedMedia(name) &&
                  UltraCanvasMediaViewer::ClassifyFile(name) == MediaKind::Sheet,
              std::string(name) + " opens in the detail pane as a spreadsheet");
    }
    const auto xls = UltraCanvasSupportedFormats::FindByExtension("xls");
    Check(xls && xls->canLoad && !xls->canSave &&
              xls->category == MediaFormatCategory::Spreadsheet,
          "the format inventory lists .xls as a spreadsheet it loads and does not save");
}

} // namespace

int main() {
    TestCompoundFile();
    TestBiff8Model();
    TestBiff8Spreadsheet();
    TestBiff5();
    TestHtmlTable();
    TestXmlSpreadsheet();
    TestOtherContent();
    TestFilerAndViewer();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
