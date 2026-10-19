// Tests/SpreadsheetFormulaEngineTest.cpp
// The spreadsheet formula engine, on the faults found while adding .xls:
//   * a reference to a formula cell was handed on as its display text, so
//     ='Data'.B5 of a formula cell held "1008.75" and SUM over formula cells
//     left them out; a reference to a sheet that does not exist read the
//     current sheet;
//   * cells were calculated in storage order, so a formula reading a formula
//     further on (a total above its rows, an earlier sheet reading a later
//     one) read nothing;
//   * a number turned into text as std::to_string writes it ("12.500000");
//   * the tokenizer never moved past a '#', so a formula with an error
//     literal (=IF(ISNA(A1),#N/A,1)) hung whoever parsed it;
//   * .xlsx formulas kept Excel's Sheet!A1, which the engine cannot read, and
//     were saved in the engine's 'Sheet'.A1, which Excel cannot read;
//   * the function library had 45 functions: no VLOOKUP, SUMIF, COUNTIF, ...
// and checks the Excel-compatible functions added for the last point.
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetExcelFormula.h"
#include "UltraCanvasSpreadsheetFormula.h"
#include "UltraCanvasZipPackage.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Near(double a, double b, double tolerance = 1e-9) {
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(b));
}

// What a formula in a scratch cell of the first sheet evaluates to.
CellValueVariant Eval(UltraCanvasSpreadsheet& book, const std::string& formula) {
    SpreadsheetCell* cell = book.GetSheet(0)->GetCell(199, 25);
    cell->SetFormula(formula);
    book.RecalculateAll();
    return cell->GetRawValue();
}

std::string Describe(const CellValueVariant& v) {
    if (auto* n = std::get_if<double>(&v)) return FormatFloatClassic(*n, 15);
    if (auto* s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
    if (auto* b = std::get_if<bool>(&v)) return *b ? "TRUE" : "FALSE";
    if (auto* e = std::get_if<CellErrorType>(&v)) return CellErrorToString(*e);
    return "(empty)";
}

void ExpectNumber(UltraCanvasSpreadsheet& book, const std::string& formula, double expected,
                  double tolerance = 1e-9) {
    const CellValueVariant v = Eval(book, formula);
    const double* n = std::get_if<double>(&v);
    const bool ok = n && Near(*n, expected, tolerance);
    Check(ok, formula + " = " + FormatFloatClassic(expected, 12) +
                  (ok ? "" : "  (got " + Describe(v) + ")"));
}

void ExpectText(UltraCanvasSpreadsheet& book, const std::string& formula, const std::string& expected) {
    const CellValueVariant v = Eval(book, formula);
    const std::string* s = std::get_if<std::string>(&v);
    const bool ok = s && *s == expected;
    Check(ok, formula + " = \"" + expected + "\"" + (ok ? "" : "  (got " + Describe(v) + ")"));
}

void ExpectBool(UltraCanvasSpreadsheet& book, const std::string& formula, bool expected) {
    const CellValueVariant v = Eval(book, formula);
    const bool* b = std::get_if<bool>(&v);
    const bool ok = b && *b == expected;
    Check(ok, formula + " = " + (expected ? "TRUE" : "FALSE") + (ok ? "" : "  (got " + Describe(v) + ")"));
}

void ExpectError(UltraCanvasSpreadsheet& book, const std::string& formula, CellErrorType expected) {
    const CellValueVariant v = Eval(book, formula);
    const CellErrorType* e = std::get_if<CellErrorType>(&v);
    const bool ok = e && *e == expected;
    Check(ok, formula + " = " + CellErrorToString(expected) + (ok ? "" : "  (got " + Describe(v) + ")"));
}

// ===== FORMULA-CELL REFERENCES =====

void TestFormulaCellReferences() {
    std::cout << "=== References to formula cells ===\n";
    UltraCanvasSpreadsheet book("Refs", 0, 0, 400, 300);
    book.AddSheet("Other");
    SpreadsheetSheet* first = book.GetSheet(0);
    SpreadsheetSheet* other = book.GetSheet(1);
    const std::string name = first->GetName();
    first->GetCell(0, 0)->SetNumber(5);
    first->GetCell(1, 0)->SetFormula("=A1*2");             // 10
    first->GetCell(2, 0)->SetFormula("=A1&\"\"");          // "5"
    first->GetCell(3, 0)->SetFormula("=A1>1");             // TRUE
    first->GetCell(0, 1)->SetFormula("=A2");               // same sheet
    first->GetCell(1, 1)->SetFormula("=SUM(A1:A2)");       // over a formula cell
    other->GetCell(0, 0)->SetFormula("='" + name + "'.A2");
    other->GetCell(1, 0)->SetFormula("='" + name + "'.A2*3");
    other->GetCell(2, 0)->SetFormula("=SUM('" + name + "'.A1:A2)");
    other->GetCell(3, 0)->SetFormula("='" + name + "'.A3");
    other->GetCell(4, 0)->SetFormula("='" + name + "'.A4");
    other->GetCell(5, 0)->SetFormula("='Missing'.A1");
    other->GetCell(6, 0)->SetFormula("=SUM('Missing'.A1:A3)");
    book.RecalculateAll();

    auto number = [](const SpreadsheetCell* c) {
        const double* n = c ? std::get_if<double>(&c->GetRawValue()) : nullptr;
        return n ? *n : std::nan("");
    };
    Check(Near(number(first->GetCellIfExists(0, 1)), 10.0),
          "=A2 of a formula cell is the number 10, not the text \"10\"");
    Check(Near(number(first->GetCellIfExists(1, 1)), 15.0), "SUM counts a formula cell");
    Check(Near(number(other->GetCellIfExists(0, 0)), 10.0), "a cross-sheet reference to it is 10");
    Check(Near(number(other->GetCellIfExists(1, 0)), 30.0), "and arithmetic on it works");
    Check(Near(number(other->GetCellIfExists(2, 0)), 15.0), "a cross-sheet SUM counts it too");
    const SpreadsheetCell* text = other->GetCellIfExists(3, 0);
    Check(text && std::get_if<std::string>(&text->GetRawValue()) &&
              *std::get_if<std::string>(&text->GetRawValue()) == "5",
          "a text result stays text");
    const SpreadsheetCell* boolean = other->GetCellIfExists(4, 0);
    Check(boolean && std::get_if<bool>(&boolean->GetRawValue()) &&
              *std::get_if<bool>(&boolean->GetRawValue()),
          "a Boolean result stays Boolean");
    const SpreadsheetCell* missing = other->GetCellIfExists(5, 0);
    Check(missing && std::get_if<CellErrorType>(&missing->GetRawValue()) &&
              *std::get_if<CellErrorType>(&missing->GetRawValue()) == CellErrorType::ReferenceError,
          "a reference to a sheet that does not exist is #REF!");
    const SpreadsheetCell* missingRange = other->GetCellIfExists(6, 0);
    Check(missingRange && std::get_if<CellErrorType>(&missingRange->GetRawValue()) &&
              *std::get_if<CellErrorType>(&missingRange->GetRawValue()) == CellErrorType::ReferenceError,
          "so is a range on one");
}

// ===== CALCULATION ORDER =====

void TestCalculationOrder() {
    std::cout << "\n=== Calculation order ===\n";
    UltraCanvasSpreadsheet book("Order", 0, 0, 400, 300);
    book.AddSheet("Later");
    SpreadsheetSheet* first = book.GetSheet(0);
    SpreadsheetSheet* later = book.GetSheet(1);
    // A total in row 1 over formula cells below it, and a first sheet that
    // reads a formula on the sheet after it: both were calculated before
    // what they read, and read nothing.
    first->GetCell(0, 0)->SetFormula("=SUM(A2:A4)");
    for (int r = 1; r <= 3; ++r) first->GetCell(r, 0)->SetFormula("=" + std::to_string(r) + "*10");
    first->GetCell(0, 1)->SetFormula("='Later'.A1+1");
    later->GetCell(0, 0)->SetFormula("=B1*2");
    later->GetCell(0, 1)->SetNumber(21);
    // A circular pair must end, not recurse forever.
    first->GetCell(5, 0)->SetFormula("=A7+1");
    first->GetCell(6, 0)->SetFormula("=A6+1");
    // A long chain written bottom-up: far deeper than one nested calculation
    // goes, so it takes more than one pass.
    for (int r = 10; r < 1010; ++r) first->GetCell(r, 3)->SetFormula("=D" + std::to_string(r + 2) + "+1");
    first->GetCell(1010, 3)->SetNumber(0);
    book.RecalculateAll();

    auto number = [](const SpreadsheetCell* c) {
        const double* n = c ? std::get_if<double>(&c->GetRawValue()) : nullptr;
        return n ? *n : std::nan("");
    };
    Check(Near(number(first->GetCellIfExists(0, 0)), 60.0), "a total above its formula rows is 60");
    Check(Near(number(first->GetCellIfExists(0, 1)), 43.0),
          "the first sheet reads the later sheet's formula: 43");
    Check(first->GetCellIfExists(5, 0) && first->GetCellIfExists(6, 0),
          "a circular reference ends (no endless recursion)");
    Check(Near(number(first->GetCellIfExists(10, 3)), 1000.0),
          "a 1000-cell chain written bottom-up calculates exactly, without a deep stack");
}

// ===== NUMBERS AS TEXT, ERROR LITERALS, REFERENCES =====

void TestTextAndTokens() {
    std::cout << "\n=== Numbers as text, error literals, sheet names ===\n";
    UltraCanvasSpreadsheet book("Tokens", 0, 0, 400, 300);
    book.GetSheet(0)->GetCell(0, 0)->SetNumber(12.5);
    ExpectText(book, "=A1&\" pie\"", "12.5 pie");
    ExpectText(book, "=CONCATENATE(1/4)", "0.25");
    ExpectText(book, "=1/3&\"\"", "0.333333333333333");

    // Each of these used to spin forever in the tokenizer.
    for (const std::string f : {"=#N/A", "=#DIV/0!+1", "=IF(ISNA(A1),#N/A,1)", "=#", "=#FOO!", "=1+#"}) {
        FormulaTokenizer tokenizer(f);
        const std::vector<FormulaToken> tokens = tokenizer.Tokenize();
        Check(!tokens.empty() && tokens.back().type == FormulaTokenType::EndOfFormula,
              "tokenizes and ends: " + f);
    }
    {
        FormulaTokenizer tokenizer("=#n/a");
        const std::vector<FormulaToken> tokens = tokenizer.Tokenize();
        Check(tokens.size() == 2 && tokens[0].type == FormulaTokenType::Error &&
                  tokens[0].errorValue == CellErrorType::NAError,
              "#n/a is the error literal #N/A, in any case");
    }
    ExpectError(book, "=#N/A", CellErrorType::NAError);
    ExpectBool(book, "=ISNA(#N/A)", true);
    ExpectNumber(book, "=IFERROR(#DIV/0!,7)", 7);
    ExpectError(book, "=#FOO!", CellErrorType::NameError);

    const CellAddress dotted = CellAddress::FromString("'My.Sheet'.B2");
    Check(dotted.sheetName == "My.Sheet" && dotted.row == 1 && dotted.col == 1,
          "a quoted sheet name may hold a '.'");
    const CellAddress quote = CellAddress::FromString("'It''s'.$A$1");
    Check(quote.sheetName == "It's" && quote.row == 0 && quote.col == 0,
          "and a doubled quote reads as one");
    Check(!CellAddress::FromString("Rate2024").IsValid(), "four letters are a name, not a column");
    Check(!CellAddress::FromString("A99999999999").IsValid(), "an absurd row is no reference (and no throw)");
}

// ===== EXCEL'S SYNTAX IN .XLSX =====

void TestExcelSyntax() {
    std::cout << "\n=== Excel formula syntax (.xlsx) ===\n";
    struct Case { const char* excel; const char* native; };
    const Case toNative[] = {
        {"=Data!B5*2", "='Data'.B5*2"},
        {"SUM('Sales 2024'!A1:B9)", "SUM('Sales 2024'.A1:B9)"},
        {"'It''s'!A1", "'It''s'.A1"},
        {"Donn\xC3\xA9" "es!$A$1+1.5", "'Donn\xC3\xA9" "es'.$A$1+1.5"},
        {"_xlfn.IFS(A1>1,\"x\",TRUE,\"y\")", "IFS(A1>1,\"x\",TRUE,\"y\")"},
        {"\"Data!B5\"&A1", "\"Data!B5\"&A1"},
        {"_xlfn.STDEV.S(A1:A3)", "STDEV.S(A1:A3)"},
    };
    for (const Case& c : toNative) {
        const std::string got = ExcelFormulaToNative(c.excel);
        Check(got == c.native, std::string("to native: ") + c.excel + " -> " + c.native +
                                   (got == c.native ? "" : "  (got " + got + ")"));
    }
    const Case toExcel[] = {
        {"='Data'!B5*2", "='Data'.B5*2"},
        {"Sheet1!A1+1.5", "Sheet1.A1+1.5"},
        {"_xlfn.STDEV.S(A1:A3)", "STDEV.S(A1:A3)"},
        {"ERROR.TYPE(A1)", "ERROR.TYPE(A1)"},
        {"\"x.A1\"&'My Sheet'!$B$2", "\"x.A1\"&'My Sheet'.$B$2"},
    };
    for (const Case& c : toExcel) {
        const std::string got = NativeFormulaToExcel(c.native);
        Check(got == c.excel, std::string("to Excel: ") + c.native + " -> " + c.excel +
                                  (got == c.excel ? "" : "  (got " + got + ")"));
    }

    // A round trip through a file: saved in Excel's syntax, read back in ours.
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "formula-engine-test";
    fs::create_directories(dir, ec);
    const std::string path = PathToUtf8(dir / "cross.xlsx");
    {
        UltraCanvasSpreadsheet book("Writer", 0, 0, 400, 300);
        book.GetSheet(0)->SetName("Data");
        book.AddSheet("Sales 2024");
        book.GetSheet(0)->GetCell(4, 1)->SetNumber(7);
        book.GetSheet(1)->GetCell(0, 0)->SetFormula("='Data'.B5*2");
        book.GetSheet(1)->GetCell(1, 0)->SetFormula("=STDEV.S(1,2,3)");
        book.RecalculateAll();
        Check(book.SaveXLSX(path), "a workbook with a cross-sheet formula saves as .xlsx");
    }
    UCZipPackageReader zip;
    std::string sheetXml;
    Check(zip.Open(path) && zip.ReadEntry("xl/worksheets/sheet2.xml", sheetXml) &&
              sheetXml.find("<f>'Data'!B5*2</f>") != std::string::npos &&
              sheetXml.find("<f>_xlfn.STDEV.S(1,2,3)</f>") != std::string::npos,
          "the file holds 'Data'!B5*2 and _xlfn.STDEV.S, as Excel writes them");
    zip.Close();
    {
        UltraCanvasSpreadsheet book("Reader", 0, 0, 400, 300);
        Check(book.LoadXLSX(path), "and loads back");
        const SpreadsheetCell* cell = book.GetSheet(1)->GetCellIfExists(0, 0);
        Check(cell && cell->GetFormulaText() == "='Data'.B5*2", "as ='Data'.B5*2");
        book.RecalculateAll();
        const double* n = cell ? std::get_if<double>(&cell->GetRawValue()) : nullptr;
        Check(n && Near(*n, 14.0), "which recalculates to 14 (it was #NAME?)");
    }
    fs::remove_all(dir, ec);
}

// ===== THE FUNCTIONS =====

void TestFunctions() {
    std::cout << "\n=== Functions ===\n";
    UltraCanvasSpreadsheet book("Functions", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    const char* fruit[] = {"apple", "banana", "cherry", "date", "elder"};
    const char* tags[] = {"x", "y", "x", "y", "x"};
    for (int r = 0; r < 5; ++r) {
        s->GetCell(r, 0)->SetText(fruit[r]);
        s->GetCell(r, 1)->SetNumber((r + 1) * 10.0);
        s->GetCell(r, 2)->SetText(tags[r]);
        s->GetCell(r, 3)->SetNumber(r + 1.0);
    }
    s->GetCell(0, 4)->SetNumber(45366);                      // E1: 2024-03-15, a Friday
    s->GetCell(1, 4)->SetNumber(13.75 / 24.0);               // E2: 13:45
    s->GetCell(0, 5)->SetText("\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2");   // F1: Thai, three letters
    const char* letters[] = {"a", "b", "c", "d", "e"};
    for (int c = 0; c < 5; ++c) {                            // G1:K2, a horizontal table
        s->GetCell(0, 6 + c)->SetText(letters[c]);
        s->GetCell(1, 6 + c)->SetNumber((c + 1) * 100.0);
    }

    std::cout << "  -- lookups\n";
    ExpectNumber(book, "=VLOOKUP(\"cherry\",A1:B5,2,FALSE)", 30);
    ExpectNumber(book, "=VLOOKUP(\"CHERRY\",A1:B5,2,FALSE)", 30);
    ExpectNumber(book, "=VLOOKUP(\"c*\",A1:B5,2,FALSE)", 30);
    ExpectNumber(book, "=VLOOKUP(25,B1:B5,1,TRUE)", 20);
    ExpectNumber(book, "=VLOOKUP(25,B1:B5,1)", 20);
    ExpectError(book, "=VLOOKUP(\"zzz\",A1:B5,2,FALSE)", CellErrorType::NAError);
    ExpectError(book, "=VLOOKUP(\"cherry\",A1:B5,3,FALSE)", CellErrorType::ReferenceError);
    ExpectNumber(book, "=HLOOKUP(\"c\",G1:K2,2,FALSE)", 300);
    ExpectNumber(book, "=INDEX(A1:B5,3,2)", 30);
    ExpectNumber(book, "=INDEX(B1:B5,4)", 40);
    ExpectText(book, "=INDEX(G1:K1,2)", "b");
    ExpectError(book, "=INDEX(A1:B5,9,1)", CellErrorType::ReferenceError);
    ExpectNumber(book, "=MATCH(\"date\",A1:A5,0)", 4);
    ExpectNumber(book, "=MATCH(35,B1:B5,1)", 3);
    ExpectNumber(book, "=MATCH(35,B1:B5)", 3);
    ExpectNumber(book, "=MATCH(\"y*\",C1:C5,0)", 2);
    ExpectText(book, "=INDEX(A1:A5,MATCH(40,B1:B5,0))", "date");
    ExpectText(book, "=LOOKUP(35,B1:B5,A1:A5)", "cherry");
    ExpectNumber(book, "=XLOOKUP(\"banana\",A1:A5,B1:B5)", 20);
    ExpectText(book, "=XLOOKUP(\"nope\",A1:A5,B1:B5,\"none\")", "none");
    ExpectText(book, "=CHOOSE(2,\"a\",\"b\",\"c\")", "b");
    ExpectNumber(book, "=ROWS(A1:B5)", 5);
    ExpectNumber(book, "=COLUMNS(A1:B5)", 2);

    std::cout << "  -- conditional aggregation\n";
    ExpectNumber(book, "=SUMIF(C1:C5,\"x\",B1:B5)", 90);
    ExpectNumber(book, "=SUMIF(B1:B5,\">25\")", 120);
    ExpectNumber(book, "=SUMIF(B1:B5,\"<>20\")", 130);
    ExpectNumber(book, "=SUMIF(B1:B5,30)", 30);
    ExpectNumber(book, "=COUNTIF(C1:C5,\"y\")", 2);
    ExpectNumber(book, "=COUNTIF(A1:A5,\"*e*\")", 4);
    ExpectNumber(book, "=COUNTIF(B1:B5,\">=30\")", 3);
    ExpectNumber(book, "=AVERAGEIF(C1:C5,\"x\",B1:B5)", 30);
    ExpectNumber(book, "=SUMIFS(B1:B5,C1:C5,\"x\",B1:B5,\">10\")", 80);
    ExpectNumber(book, "=COUNTIFS(C1:C5,\"x\",B1:B5,\">20\")", 2);
    ExpectNumber(book, "=AVERAGEIFS(B1:B5,C1:C5,\"y\")", 30);
    ExpectNumber(book, "=MAXIFS(B1:B5,C1:C5,\"y\")", 40);
    ExpectNumber(book, "=MINIFS(B1:B5,C1:C5,\"x\")", 10);
    ExpectNumber(book, "=COUNTBLANK(A1:A7)", 2);
    ExpectNumber(book, "=SUMPRODUCT(B1:B5,D1:D5)", 550);
    ExpectNumber(book, "=SUBTOTAL(9,B1:B5)", 150);
    ExpectNumber(book, "=SUBTOTAL(101,B1:B5)", 30);

    std::cout << "  -- math\n";
    ExpectNumber(book, "=ROUNDUP(3.2,0)", 4);
    ExpectNumber(book, "=ROUNDUP(-3.2,0)", -4);
    ExpectNumber(book, "=ROUNDDOWN(3.99,1)", 3.9);
    ExpectNumber(book, "=ROUNDUP(2.675,2)", 2.68);
    ExpectNumber(book, "=TRUNC(-2.7)", -2);
    ExpectNumber(book, "=CEILING(2.5,1)", 3);
    ExpectNumber(book, "=CEILING(0.3,0.1)", 0.3);
    ExpectNumber(book, "=FLOOR(2.5,1)", 2);
    ExpectNumber(book, "=MROUND(10,3)", 9);
    ExpectNumber(book, "=SIGN(-5)", -1);
    ExpectNumber(book, "=EVEN(3)", 4);
    ExpectNumber(book, "=ODD(2)", 3);
    ExpectNumber(book, "=PRODUCT(D1:D5)", 120);
    ExpectNumber(book, "=SUMSQ(1,2,3)", 14);
    ExpectNumber(book, "=QUOTIENT(7,2)", 3);
    ExpectNumber(book, "=FACT(5)", 120);
    ExpectNumber(book, "=COMBIN(5,2)", 10);
    ExpectNumber(book, "=GCD(12,18)", 6);
    ExpectNumber(book, "=LCM(4,6)", 12);
    ExpectNumber(book, "=DEGREES(PI())", 180);
    ExpectError(book, "=ASIN(2)", CellErrorType::NumError);
    ExpectError(book, "=REPT(\"\",1E300)", CellErrorType::ValueError);
    ExpectError(book, "=COMBIN(1E300,5E299)", CellErrorType::NumError);
    ExpectNumber(book, "=ROUNDUP(1,1E300)", 1);

    std::cout << "  -- statistics\n";
    ExpectNumber(book, "=STDEV(D1:D5)", std::sqrt(2.5));
    ExpectNumber(book, "=STDEV.S(D1:D5)", std::sqrt(2.5));
    ExpectNumber(book, "=STDEVP(D1:D5)", std::sqrt(2.0));
    ExpectNumber(book, "=VAR(D1:D5)", 2.5);
    ExpectNumber(book, "=VARP(D1:D5)", 2.0);
    ExpectNumber(book, "=LARGE(B1:B5,2)", 40);
    ExpectNumber(book, "=SMALL(B1:B5,2)", 20);
    ExpectNumber(book, "=RANK(30,B1:B5)", 3);
    ExpectNumber(book, "=RANK(40,B1:B5,1)", 4);
    ExpectNumber(book, "=MODE(1,2,2,3)", 2);
    ExpectError(book, "=MODE(1,2,3)", CellErrorType::NAError);
    ExpectNumber(book, "=PERCENTILE(D1:D5,0.25)", 2);
    ExpectNumber(book, "=QUARTILE(D1:D5,3)", 4);

    std::cout << "  -- text\n";
    ExpectNumber(book, "=LEN(F1)", 3);
    ExpectText(book, "=LEFT(F1,1)", "\xE0\xB8\x82");
    ExpectText(book, "=RIGHT(\"hello\",2)", "lo");
    ExpectText(book, "=MID(\"hello\",2,3)", "ell");
    ExpectText(book, "=MID(F1,2,5)", "\xE0\xB8\xB2\xE0\xB8\xA2");
    ExpectText(book, "=CONCAT(A1:A2)", "applebanana");
    ExpectText(book, "=TEXTJOIN(\", \",TRUE,A1:A3)", "apple, banana, cherry");
    ExpectText(book, "=SUBSTITUTE(\"a-b-c\",\"-\",\"+\")", "a+b+c");
    ExpectText(book, "=SUBSTITUTE(\"a-b-c\",\"-\",\"+\",2)", "a-b+c");
    ExpectText(book, "=REPLACE(\"abcdef\",2,3,\"X\")", "aXef");
    ExpectNumber(book, "=FIND(\"c\",\"abcabc\")", 3);
    ExpectNumber(book, "=FIND(\"c\",\"abcabc\",4)", 6);
    ExpectError(book, "=FIND(\"C\",\"abc\")", CellErrorType::ValueError);
    ExpectNumber(book, "=SEARCH(\"C\",\"abc\")", 3);
    ExpectNumber(book, "=SEARCH(\"b?d\",\"abcd\")", 2);
    ExpectBool(book, "=EXACT(\"a\",\"A\")", false);
    ExpectText(book, "=PROPER(\"hello world\")", "Hello World");
    ExpectText(book, "=REPT(\"ab\",3)", "ababab");
    ExpectText(book, "=CHAR(65)", "A");
    ExpectText(book, "=CHAR(128)", "\xE2\x82\xAC");
    ExpectNumber(book, "=CODE(\"A\")", 65);
    ExpectNumber(book, "=VALUE(\"12.5\")", 12.5);
    ExpectNumber(book, "=VALUE(\"50%\")", 0.5);
    ExpectError(book, "=VALUE(\"abc\")", CellErrorType::ValueError);
    ExpectText(book, "=T(5)", "");
    ExpectNumber(book, "=N(TRUE)", 1);

    std::cout << "  -- dates and times\n";
    ExpectNumber(book, "=TIME(13,45,0)", 13.75 / 24.0);
    ExpectNumber(book, "=HOUR(E2)", 13);
    ExpectNumber(book, "=MINUTE(E2)", 45);
    ExpectNumber(book, "=SECOND(TIME(1,2,3))", 3);
    ExpectNumber(book, "=WEEKDAY(E1)", 6);
    ExpectNumber(book, "=WEEKDAY(E1,2)", 5);
    ExpectNumber(book, "=EDATE(E1,1)", 45397);
    ExpectNumber(book, "=EDATE(DATE(2024,1,31),1)", 45351);   // 2024-02-29
    ExpectNumber(book, "=EOMONTH(E1,0)", 45382);
    ExpectNumber(book, "=MONTH(EOMONTH(E1,-2))", 1);
    ExpectNumber(book, "=DAYS(E1+10,E1)", 10);
    ExpectNumber(book, "=DATEDIF(E1,E1+400,\"Y\")", 1);
    ExpectNumber(book, "=DATEDIF(DATE(2024,1,31),DATE(2024,3,1),\"M\")", 1);
    ExpectNumber(book, "=DATEVALUE(\"2024-03-15\")", 45366);

    std::cout << "  -- logic and information\n";
    ExpectText(book, "=IFNA(NA(),\"n\")", "n");
    ExpectText(book, "=IFS(B1>15,\"a\",TRUE,\"b\")", "b");
    ExpectText(book, "=SWITCH(2,1,\"one\",2,\"two\",\"other\")", "two");
    ExpectText(book, "=SWITCH(9,1,\"one\",\"other\")", "other");
    ExpectBool(book, "=XOR(TRUE,FALSE)", true);
    ExpectBool(book, "=ISNA(NA())", true);
    ExpectBool(book, "=ISERR(1/0)", true);
    ExpectBool(book, "=ISERR(NA())", false);
    ExpectBool(book, "=ISLOGICAL(TRUE)", true);
    ExpectBool(book, "=ISNONTEXT(5)", true);
    ExpectBool(book, "=ISEVEN(4)", true);
    ExpectBool(book, "=ISODD(4)", false);

    std::cout << "  -- finance\n";
    ExpectNumber(book, "=FV(0.05/12,120,-100)", 15528.23, 1e-6);
    ExpectNumber(book, "=PV(0.08/12,240,500)", -59777.15, 1e-6);
    ExpectNumber(book, "=NPV(0.1,-10000,3000,4200,6800)", 1188.44, 1e-5);

    SpreadsheetFormulaEngine* engine = book.GetFormulaEngine();
    Check(engine && engine->GetFunctionLibrary().GetFunctionNames().size() >= 140,
          "the library holds 140 functions or more");
}

} // namespace

int main() {
    TestFormulaCellReferences();
    TestCalculationOrder();
    TestTextAndTokens();
    TestExcelSyntax();
    TestFunctions();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: " + std::to_string(g_failures))
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
