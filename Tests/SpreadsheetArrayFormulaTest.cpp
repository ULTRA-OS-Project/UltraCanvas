// Tests/SpreadsheetArrayFormulaTest.cpp
// Array formulas and the newer function set of the spreadsheet engine:
//   * arrays as values: array constants {1,2;3,4}, a range read as a value,
//     element-wise arithmetic and functions over them, LET and LAMBDA;
//   * dynamic-array spills: a result fills the cells right and below, #SPILL!
//     when one is taken, A1# reads the whole spill, the spill follows its
//     source (grows, shrinks, is freed again), a spilled cell holds a value and
//     no formula; legacy (Ctrl+Shift+Enter) array formulas fill a fixed range;
//   * FILTER, SORT, SORTBY, UNIQUE, SEQUENCE, RANDARRAY, TRANSPOSE, XLOOKUP,
//     XMATCH and the lambda helpers;
//   * the financial functions, checked against the results Excel documents;
//   * .xlsx (t="array" with ref, cm and xl/metadata.xml) and .ods
//     (table:number-matrix-*-spanned) round trips, and - where soffice is
//     installed - LibreOffice opening our .xlsx with the same values.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetFormula.h"
#include "UltraCanvasZipPackage.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Close(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(b));
}

std::string Describe(const CellValueVariant& v) {
    if (auto* n = std::get_if<double>(&v)) return FormatFloatClassic(*n, 15);
    if (auto* s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
    if (auto* b = std::get_if<bool>(&v)) return *b ? "TRUE" : "FALSE";
    if (auto* e = std::get_if<CellErrorType>(&v)) return CellErrorToString(*e);
    return "(empty)";
}

const CellValueVariant& Raw(const SpreadsheetSheet* sheet, int row, int col) {
    static const CellValueVariant empty;
    const SpreadsheetCell* cell = sheet ? sheet->GetCellIfExists(row, col) : nullptr;
    return cell ? cell->GetRawValue() : empty;
}

bool IsNumber(const CellValueVariant& v, double expected, double tolerance = 1e-9) {
    const double* n = std::get_if<double>(&v);
    return n && Close(*n, expected, tolerance);
}

bool IsError(const CellValueVariant& v, CellErrorType expected) {
    const CellErrorType* e = std::get_if<CellErrorType>(&v);
    return e && *e == expected;
}

bool IsText(const CellValueVariant& v, const std::string& expected) {
    const std::string* s = std::get_if<std::string>(&v);
    return s && *s == expected;
}

// A formula in a scratch cell far from everything else: its (top-left) value.
CellValueVariant Eval(UltraCanvasSpreadsheet& book, const std::string& formula) {
    SpreadsheetCell* cell = book.GetSheet(0)->GetCell(299, 30);
    cell->SetFormula(formula);
    book.RecalculateAll();
    return cell->GetRawValue();
}

void ExpectNumber(UltraCanvasSpreadsheet& book, const std::string& formula, double expected,
                  double tolerance = 1e-9) {
    const CellValueVariant v = Eval(book, formula);
    const bool ok = IsNumber(v, expected, tolerance);
    Check(ok, formula + " = " + FormatFloatClassic(expected, 12) + (ok ? "" : "  (got " + Describe(v) + ")"));
}

void ExpectError(UltraCanvasSpreadsheet& book, const std::string& formula, CellErrorType expected) {
    const CellValueVariant v = Eval(book, formula);
    const bool ok = IsError(v, expected);
    Check(ok, formula + " = " + CellErrorToString(expected) + (ok ? "" : "  (got " + Describe(v) + ")"));
}

void ExpectText(UltraCanvasSpreadsheet& book, const std::string& formula, const std::string& expected) {
    const CellValueVariant v = Eval(book, formula);
    const bool ok = IsText(v, expected);
    Check(ok, formula + " = \"" + expected + "\"" + (ok ? "" : "  (got " + Describe(v) + ")"));
}

// The values a block of cells holds, row by row, as text ("1,2;3,4").
std::string Block(const SpreadsheetSheet* sheet, int row, int col, int rows, int cols) {
    std::string out;
    for (int r = 0; r < rows; ++r) {
        if (r) out += ";";
        for (int c = 0; c < cols; ++c) {
            if (c) out += ",";
            const CellValueVariant& v = Raw(sheet, row + r, col + c);
            if (auto* n = std::get_if<double>(&v)) out += FormatFloatClassic(*n, 10);
            else if (auto* s = std::get_if<std::string>(&v)) out += *s;
            else if (auto* b = std::get_if<bool>(&v)) out += *b ? "TRUE" : "FALSE";
            else if (auto* e = std::get_if<CellErrorType>(&v)) out += CellErrorToString(*e);
        }
    }
    return out;
}

void ExpectBlock(const SpreadsheetSheet* sheet, int row, int col, int rows, int cols, const std::string& expected,
                 const std::string& what) {
    const std::string got = Block(sheet, row, col, rows, cols);
    Check(got == expected, what + ": " + expected + (got == expected ? "" : "  (got " + got + ")"));
}

// ===== ARRAYS AS VALUES =====

void TestArrayValues() {
    std::cout << "=== Arrays as values ===\n";
    UltraCanvasSpreadsheet book("Arrays", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    for (int r = 0; r < 5; ++r) {
        s->GetCell(r, 0)->SetNumber(r + 1);          // A1:A5 = 1..5
        s->GetCell(r, 1)->SetNumber((r + 1) * 10);   // B1:B5 = 10..50
    }
    ExpectNumber(book, "=SUM({1,2,3}*2)", 12);
    ExpectNumber(book, "=SUM({1,2;3,4})", 10);
    ExpectNumber(book, "=ROWS({1,2;3,4;5,6})", 3);
    ExpectNumber(book, "=COLUMNS({1,2;3,4;5,6})", 2);
    ExpectNumber(book, "=INDEX({1,2;3,4},2,1)", 3);
    ExpectNumber(book, "=SUM(A1:A5*B1:B5)", 550);           // a SUMPRODUCT without SUMPRODUCT
    ExpectNumber(book, "=SUMPRODUCT((A1:A5>2)*B1:B5)", 120);
    ExpectNumber(book, "=SUM(ABS({-1,-2,3}))", 6);          // ABS element by element
    ExpectNumber(book, "=SUM(IF(A1:A5>3,B1:B5,0))", 90);
    ExpectNumber(book, "=SUM(LEN({\"ab\",\"cde\"}))", 5);
    ExpectNumber(book, "=MAX(A1:A5-{2})", 3);
    ExpectNumber(book, "=SUM({1;2;3}*{10,20})", 180);       // a column times a row: 3 x 2
    ExpectNumber(book, "=SUM({-1,+2})", 1);
    ExpectNumber(book, "=LET(x,5,y,x*2,x+y)", 15);
    ExpectNumber(book, "=LET(total,SUM(A1:A5),total/COUNT(A1:A5))", 3);
    ExpectNumber(book, "=LAMBDA(a,b,a*b)(6,7)", 42);
    ExpectNumber(book, "=LET(sq,LAMBDA(n,n*n),sq(4)+sq(3))", 25);
    ExpectNumber(book, "=SUM(MAP({1,2,3},LAMBDA(v,v*10)))", 60);
    ExpectNumber(book, "=REDUCE(0,A1:A5,LAMBDA(acc,v,acc+v*v))", 55);
    ExpectNumber(book, "=SUM(BYROW({1,2;3,4},LAMBDA(r,MAX(r))))", 6);
    ExpectNumber(book, "=SUM(MAKEARRAY(2,3,LAMBDA(r,c,r*c)))", 18);
    ExpectNumber(book, "=INDEX(SCAN(0,{1,2,3},LAMBDA(a,v,a+v)),1,3)", 6);
    ExpectNumber(book, "=SEQUENCE(3,,10)", 10);              // an argument left out
    // {2,4,#N/A}: the #N/A where the shorter operand ends.
    {
        SpreadsheetCell* cell = s->GetCell(40, 0);
        cell->SetFormula("={1,2}+{1,2,3}");
        book.RecalculateAll();
        ExpectBlock(s, 40, 0, 1, 3, "2,4,#N/A", "{1,2}+{1,2,3} spills 2,4,#N/A");
        cell->ClearContents();
        book.RecalculateAll();
    }
    ExpectError(book, "=LAMBDA(x,x)", CellErrorType::Calc);   // never called
    ExpectError(book, "=@A1:A5", CellErrorType::ValueError);   // row 300 crosses no row of A1:A5
    SpreadsheetCell* atRow3 = s->GetCell(2, 5);                 // F3: crosses A3
    atRow3->SetFormula("=@A1:A5*2");
    book.RecalculateAll();
    Check(IsNumber(atRow3->GetRawValue(), 6) && Raw(s, 3, 5).index() == 0,
          "=@A1:A5*2 in row 3 takes A3 only: 6, nothing spilled");
}

// ===== FUNCTIONS RETURNING ARRAYS =====

void TestDynamicArrayFunctions() {
    std::cout << "\n=== Dynamic-array functions ===\n";
    UltraCanvasSpreadsheet book("Functions", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    const char* names[] = {"pear", "apple", "fig", "apple", "kiwi"};
    const double prices[] = {3, 1, 5, 2, 4};
    for (int r = 0; r < 5; ++r) {
        s->GetCell(r, 0)->SetText(names[r]);
        s->GetCell(r, 1)->SetNumber(prices[r]);
    }
    auto spill = [&](const std::string& formula, int rows, int cols, const std::string& expected) {
        SpreadsheetCell* cell = s->GetCell(10, 4);   // E11
        cell->SetFormula(formula);
        book.RecalculateAll();
        ExpectBlock(s, 10, 4, rows, cols, expected, formula);
        Check(cell->GetArrayRows() == rows && cell->GetArrayCols() == cols,
              formula + " covers " + std::to_string(rows) + " x " + std::to_string(cols));
        cell->ClearContents();
        book.RecalculateAll();
    };
    spill("=SEQUENCE(2,3)", 2, 3, "1,2,3;4,5,6");
    spill("=SEQUENCE(3,1,10,-5)", 3, 1, "10;5;0");
    spill("=TRANSPOSE({1,2;3,4;5,6})", 2, 3, "1,3,5;2,4,6");
    spill("=FILTER(A1:A5,B1:B5>2)", 3, 1, "pear;fig;kiwi");
    spill("=FILTER(A1:B5,A1:A5=\"apple\")", 2, 2, "apple,1;apple,2");
    spill("=SORT(B1:B5)", 5, 1, "1;2;3;4;5");
    spill("=SORT(A1:B5,2,-1)", 5, 2, "fig,5;kiwi,4;pear,3;apple,2;apple,1");
    spill("=SORT(A1:A5)", 5, 1, "apple;apple;fig;kiwi;pear");
    spill("=SORTBY(A1:A5,B1:B5,1)", 5, 1, "apple;apple;pear;kiwi;fig");
    spill("=UNIQUE(A1:A5)", 4, 1, "pear;apple;fig;kiwi");
    spill("=UNIQUE(A1:A5,,TRUE)", 3, 1, "pear;fig;kiwi");
    spill("=XLOOKUP(\"fig\",A1:A5,A1:B5)", 1, 2, "fig,5");
    spill("=XMATCH({\"fig\",\"kiwi\"},A1:A5)", 1, 2, "3,5");
    spill("=MAP(B1:B5,LAMBDA(p,p*2))", 5, 1, "6;2;10;4;8");
    ExpectError(book, "=FILTER(A1:A5,B1:B5>9)", CellErrorType::Calc);
    ExpectText(book, "=FILTER(A1:A5,B1:B5>9,\"none\")", "none");
    ExpectNumber(book, "=XLOOKUP(\"apple\",A1:A5,B1:B5,,0,-1)", 2);   // from the last
    ExpectNumber(book, "=XLOOKUP(4.5,B1:B5,B1:B5,,-1)", 4);           // exact or next smaller
    ExpectNumber(book, "=XLOOKUP(4.5,B1:B5,B1:B5,,1)", 5);            // exact or next larger
    ExpectText(book, "=XLOOKUP(\"zzz\",A1:A5,A1:A5,\"missing\")", "missing");
    ExpectNumber(book, "=XLOOKUP(\"f*\",A1:A5,B1:B5,,2)", 5);         // wildcards
    ExpectNumber(book, "=XMATCH(\"kiwi\",A1:A5)", 5);
    ExpectError(book, "=XMATCH(\"plum\",A1:A5)", CellErrorType::NAError);
    ExpectError(book, "=SEQUENCE(0)", CellErrorType::Calc);
    // RANDARRAY: 3 x 2 whole numbers between 5 and 7.
    SpreadsheetCell* random = s->GetCell(20, 4);
    random->SetFormula("=RANDARRAY(3,2,5,7,TRUE)");
    book.RecalculateAll();
    bool inRange = random->GetArrayRows() == 3 && random->GetArrayCols() == 2;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 2; ++c) {
            const double* n = std::get_if<double>(&Raw(s, 20 + r, 4 + c));
            inRange = inRange && n && *n >= 5 && *n <= 7 && *n == std::floor(*n);
        }
    }
    Check(inRange, "=RANDARRAY(3,2,5,7,TRUE) fills 3 x 2 with whole numbers 5..7");
}

// ===== SPILLS =====

void TestSpills() {
    std::cout << "\n=== Spills ===\n";
    UltraCanvasSpreadsheet book("Spills", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    for (int r = 0; r < 3; ++r) s->GetCell(r, 0)->SetNumber(r + 1);   // A1:A3 = 1,2,3
    s->GetCell(0, 2)->SetFormula("=A1:A3*10");                        // C1 spills C1:C3
    s->GetCell(0, 4)->SetFormula("=SUM(C1#)");                        // E1 reads the spill
    s->GetCell(1, 4)->SetFormula("=C3+1");                            // E2 reads a spilled cell
    s->GetCell(2, 4)->SetFormula("=ROWS(C1#)");
    book.RecalculateAll();

    ExpectBlock(s, 0, 2, 3, 1, "10;20;30", "=A1:A3*10 spills down C1:C3");
    const SpreadsheetCell* spilled = s->GetCellIfExists(1, 2);
    Check(spilled && spilled->IsSpilledValue() && !spilled->HasFormula() &&
              spilled->GetSpillAnchor().row == 0 && spilled->GetSpillAnchor().col == 2,
          "C2 holds a spilled value - no formula of its own - from C1");
    Check(s->GetCellIfExists(0, 2)->CoversArray(), "C1 is the spill's anchor");
    Check(IsNumber(Raw(s, 0, 4), 60), "=SUM(C1#) reads the whole spill: 60");
    Check(IsNumber(Raw(s, 1, 4), 31), "=C3+1 reads a spilled cell: 31");
    Check(IsNumber(Raw(s, 2, 4), 3), "=ROWS(C1#) = 3");

    // The source changes: the spill and what reads it follow (incremental).
    s->SetCellValue(2, 0, 7.0);   // A3 = 7, notified as a typed edit is
    book.Recalculate();
    ExpectBlock(s, 0, 2, 3, 1, "10;20;70", "A3 changed: the spill follows");
    Check(IsNumber(Raw(s, 0, 4), 100) && IsNumber(Raw(s, 1, 4), 71), "and so do =SUM(C1#) and =C3+1");

    // A value typed in the way: #SPILL!, and the spill's cells are emptied.
    s->SetCellValue(1, 2, std::string("blocker"));
    book.Recalculate();
    Check(IsError(Raw(s, 0, 2), CellErrorType::Spill), "a value in C2: C1 shows #SPILL!");
    Check(IsText(Raw(s, 1, 2), "blocker") && Raw(s, 2, 2).index() == 0, "C2 keeps what was typed, C3 is emptied");
    Check(IsError(Raw(s, 0, 4), CellErrorType::ReferenceError), "=SUM(C1#) of a blocked spill is #REF!");
    Check(IsNumber(Raw(s, 1, 4), 1), "=C3+1 reads the emptied C3: 1");

    // Cleared again: it spills again.
    s->ClearCell(1, 2);
    book.Recalculate();
    ExpectBlock(s, 0, 2, 3, 1, "10;20;70", "C2 cleared: C1 spills again");
    Check(IsNumber(Raw(s, 0, 4), 100), "and =SUM(C1#) is 100 again");

    // A spill that grows into cells read by a formula, and shrinks again.
    s->GetCell(0, 7)->SetFormula("=SEQUENCE(A1)");                    // H1: as many rows as A1 says
    s->GetCell(0, 8)->SetFormula("=H4");                              // I1 reads H4
    book.RecalculateAll();
    Check(IsNumber(Raw(s, 0, 8), 0) || Raw(s, 0, 8).index() == 0, "H4 is not spilled to yet: I1 reads nothing");
    s->SetCellValue(0, 0, 5.0);
    book.Recalculate();
    ExpectBlock(s, 0, 7, 5, 1, "1;2;3;4;5", "A1 = 5: SEQUENCE(A1) grows to five rows");
    Check(IsNumber(Raw(s, 0, 8), 4), "and =H4, reading a cell the spill reached only now, is 4");
    s->SetCellValue(0, 0, 2.0);
    book.Recalculate();
    ExpectBlock(s, 0, 7, 5, 1, "1;2;;;", "A1 = 2: it shrinks, the cells it left are emptied");
    Check(s->GetCellIfExists(3, 7) && !s->GetCellIfExists(3, 7)->IsSpilledValue(), "H4 is an ordinary cell again");

    // The anchor's formula deleted: its spill goes too.
    s->ClearCell(0, 7);
    book.Recalculate();
    Check(Raw(s, 1, 7).index() == 0, "H1 cleared: the value it spilled to H2 goes too");

    // Two spills in each other's way: the second is #SPILL!.
    s->GetCell(10, 0)->SetFormula("=SEQUENCE(1,3)");   // A11:C11
    s->GetCell(9, 1)->SetFormula("=SEQUENCE(3)");      // B10:B12 crosses B11
    book.RecalculateAll();
    const bool first = IsError(Raw(s, 10, 0), CellErrorType::Spill);
    const bool second = IsError(Raw(s, 9, 1), CellErrorType::Spill);
    Check(first != second, "two spills crossing: one of them shows #SPILL!");

    // A spill into a merged cell is blocked.
    s->MergeCells(30, 1, 30, 2);
    s->GetCell(29, 1)->SetFormula("=SEQUENCE(2)");
    book.RecalculateAll();
    Check(IsError(Raw(s, 29, 1), CellErrorType::Spill), "a merged cell in the way: #SPILL!");
}

// ===== LEGACY ARRAY FORMULAS AND IMPLICIT INTERSECTION =====

void TestFixedArraysAndIntersection() {
    std::cout << "\n=== Ctrl+Shift+Enter arrays, implicit intersection ===\n";
    UltraCanvasSpreadsheet book("Fixed", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    for (int r = 0; r < 3; ++r) s->GetCell(r, 0)->SetNumber(r + 1);
    s->GetCell(1, 3)->SetText("in the way");   // D2: a fixed array overwrites it
    s->SetArrayFormula(CellRange(0, 3, 3, 3), "=A1:A3*2");   // D1:D4, one row more than the result
    s->SetArrayFormula(CellRange(0, 5, 1, 6), "=7");         // F1:G2, a single value
    book.RecalculateAll();
    ExpectBlock(s, 0, 3, 4, 1, "2;4;6;#N/A", "{=A1:A3*2} over D1:D4 (one row too many: #N/A)");
    ExpectBlock(s, 0, 5, 2, 2, "7,7;7,7", "{=7} over F1:G2 repeats the value");
    Check(s->GetCellIfExists(0, 3)->GetFormulaArrayMode() == FormulaArrayMode::FixedRange &&
              s->GetCellIfExists(2, 3)->IsSpilledValue(),
          "D1 holds the fixed array formula, D3 its value");
    s->SetCellValue(1, 0, 10.0);
    book.Recalculate();
    ExpectBlock(s, 0, 3, 3, 1, "2;20;6", "A2 changed: the fixed array follows");

    // A formula read from a file without dynamic arrays is worth one value:
    // a range where one is expected gives the cell on its own row.
    SpreadsheetCell* legacy = s->GetCell(1, 8);   // I2
    legacy->SetFormula("=A1:A3*100");
    legacy->SetFormulaArrayMode(FormulaArrayMode::SingleValue);
    SpreadsheetCell* outside = s->GetCell(5, 8);   // I6: no row of A1:A3
    outside->SetFormula("=A1:A3");
    outside->SetFormulaArrayMode(FormulaArrayMode::SingleValue);
    SpreadsheetCell* summed = s->GetCell(6, 8);   // I7: a function that takes ranges sums them all
    summed->SetFormula("=SUMPRODUCT(A1:A3*2)");
    summed->SetFormulaArrayMode(FormulaArrayMode::SingleValue);
    book.RecalculateAll();
    Check(IsNumber(Raw(s, 1, 8), 1000) && Raw(s, 2, 8).index() == 0,
          "=A1:A3*100 in I2 read from a file: A2*100, nothing spilled");
    Check(IsError(Raw(s, 5, 8), CellErrorType::ValueError), "=A1:A3 in I6 (no such row): #VALUE!");
    Check(IsNumber(Raw(s, 6, 8), 28), "=SUMPRODUCT(A1:A3*2) still reads the whole range: 28");
}

// ===== FINANCIAL FUNCTIONS =====
// The examples of Microsoft's function reference; the digits beyond those it
// shows are LibreOffice 24.2's results for the same formulas.

void TestFinancial() {
    std::cout << "\n=== Financial functions (Excel's results) ===\n";
    UltraCanvasSpreadsheet book("Finance", 0, 0, 400, 300);
    SpreadsheetSheet* s = book.GetSheet(0);
    const double flows[] = {-70000, 12000, 15000, 18000, 21000, 26000};
    for (int r = 0; r < 6; ++r) s->GetCell(r, 0)->SetNumber(flows[r]);                 // A1:A6
    const double xflows[] = {-10000, 2750, 4250, 3250, 2750};
    const int xdates[][3] = {{2008, 1, 1}, {2008, 3, 1}, {2008, 10, 30}, {2009, 2, 15}, {2009, 4, 1}};
    for (int r = 0; r < 5; ++r) {
        s->GetCell(r, 1)->SetNumber(xflows[r]);                                         // B1:B5
        s->GetCell(r, 2)->SetNumber(DateTimeValue::FromDate(xdates[r][0], xdates[r][1], xdates[r][2]).serialNumber);
    }
    const double mirr[] = {-120000, 39000, 30000, 21000, 37000, 46000};
    for (int r = 0; r < 6; ++r) s->GetCell(r, 3)->SetNumber(mirr[r]);                  // D1:D6

    const double t = 1e-7;
    ExpectNumber(book, "=PMT(0.08/12,10,10000)", -1037.03208935915, t);
    ExpectNumber(book, "=PV(0.08/12,12*20,500,,0)", -59777.1458511878, t);
    ExpectNumber(book, "=FV(0.06/12,10,-200,-500,1)", 2581.40337406012, t);
    ExpectNumber(book, "=RATE(4*12,-200,8000)", 0.00770147248820165, 1e-6);
    ExpectNumber(book, "=NPER(0.12/12,-100,-1000,10000,1)", 59.6738656742946, t);
    ExpectNumber(book, "=NPER(0.12/12,-100,-1000,10000)", 60.0821228537617, t);
    ExpectNumber(book, "=NPER(0.12/12,-100,-1000)", -9.57859403981306, t);
    ExpectNumber(book, "=IPMT(0.1/12,1,3*12,8000)", -66.6666666666667, t);
    ExpectNumber(book, "=IPMT(0.1,3,3,8000)", -292.447129909366, t);
    ExpectNumber(book, "=PPMT(0.1/12,1,2*12,2000)", -75.6231860083368, t);
    ExpectNumber(book, "=PPMT(0.08,10,10,200000)", -27598.0534624214, t);
    ExpectNumber(book, "=CUMIPMT(0.09/12,30*12,125000,13,24,0)", -11135.2321307508, t);
    ExpectNumber(book, "=CUMIPMT(0.09/12,30*12,125000,1,1,0)", -937.5, t);
    ExpectNumber(book, "=CUMPRINC(0.09/12,30*12,125000,13,24,0)", -934.107123420839, t);
    ExpectNumber(book, "=CUMPRINC(0.09/12,30*12,125000,1,1,0)", -68.2782711809784, t);
    ExpectNumber(book, "=IRR(A1:A5)", -0.0212448482734162, 1e-6);
    ExpectNumber(book, "=IRR(A1:A6)", 0.0866309480365316, 1e-6);
    ExpectNumber(book, "=IRR(A1:A3,-10%)", -0.443506941334579, 1e-6);
    ExpectNumber(book, "=XNPV(0.09,B1:B5,C1:C5)", 2086.64760203445, 1e-6);
    ExpectNumber(book, "=XIRR(B1:B5,C1:C5)", 0.373362533518832, 1e-7);   // Excel shows 37.34%
    ExpectNumber(book, "=MIRR(D1:D6,10%,12%)", 0.126094130365905, t);
    ExpectNumber(book, "=MIRR(D1:D4,10%,12%)", -0.0480446552499808, t);
    ExpectNumber(book, "=MIRR(D1:D6,10%,14%)", 0.134759110828315, t);
    ExpectNumber(book, "=SLN(30000,7500,10)", 2250);
    ExpectNumber(book, "=SYD(30000,7500,10,1)", 4090.90909090909, t);
    ExpectNumber(book, "=SYD(30000,7500,10,10)", 409.090909090909, t);
    ExpectNumber(book, "=DB(1000000,100000,6,1,7)", 186083.333333333, t);
    ExpectNumber(book, "=DB(1000000,100000,6,2,7)", 259639.416666667, t);
    ExpectNumber(book, "=DB(1000000,100000,6,3,7)", 176814.44275, t);
    ExpectNumber(book, "=DB(1000000,100000,6,7,7)", 15845.0984738481, t);
    ExpectNumber(book, "=DDB(2400,300,10*365,1)", 1.31506849315068, t);
    ExpectNumber(book, "=DDB(2400,300,10*12,1,2)", 40, t);
    ExpectNumber(book, "=DDB(2400,300,10,1,2)", 480, t);
    ExpectNumber(book, "=DDB(2400,300,10,2,1.5)", 306, t);
    ExpectNumber(book, "=DDB(2400,300,10,10)", 22.1225472000002, t);
    ExpectNumber(book, "=VDB(2400,300,10*365,0,1)", 1.31506849315068, t);
    ExpectNumber(book, "=VDB(2400,300,10*12,0,1)", 40, t);
    ExpectNumber(book, "=VDB(2400,300,10,0,1)", 480, t);
    ExpectNumber(book, "=VDB(2400,300,10*12,6,18)", 396.306053264752, t);
    ExpectNumber(book, "=VDB(2400,300,10*12,6,18,1.5)", 311.808936658233, t);
    ExpectNumber(book, "=VDB(2400,300,10,0,0.875,1.5)", 315, t);
    // Fractional periods, and no switch to straight line (LibreOffice's results).
    ExpectNumber(book, "=VDB(2400,300,10,1.5,3.25)", 560.64, t);
    ExpectNumber(book, "=VDB(2400,300,10,2,5,2,TRUE)", 749.568, t);
    ExpectNumber(book, "=VDB(2400,300,10,0.5,4.5,2,TRUE)", 1275.264, t);
    ExpectNumber(book, "=EFFECT(0.0525,4)", 0.0535426673707582, t);
    ExpectNumber(book, "=NOMINAL(0.053543,4)", 0.052500319868356, t);
    ExpectError(book, "=IRR({1,2,3})", CellErrorType::NumError);           // no sign change
    ExpectError(book, "=RATE(10,100,100)", CellErrorType::NumError);       // no solution
    ExpectError(book, "=IPMT(0.1,0,3,8000)", CellErrorType::NumError);
    ExpectError(book, "=CUMIPMT(0.09/12,360,125000,0,1,0)", CellErrorType::NumError);
    ExpectError(book, "=SLN(1,0,0)", CellErrorType::DivisionByZero);
}

// ===== FILES =====

std::shared_ptr<UltraCanvasSpreadsheet> Load(const std::string& path) {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("loaded", 0, 0, 800, 600);
    if (!book->LoadFromFile(path)) {
        std::cout << "      cannot load " << path << ": " << book->GetLastError() << "\n";
        return nullptr;
    }
    return book;
}

// A workbook with each kind of array formula, on its first sheet:
//   A1:A3  1, 2, 3
//   B1     =A1:A3*10           dynamic, spills B1:B3
//   C1:C3  {=A1:A3+B1:B3}      a fixed (Ctrl+Shift+Enter) array
//   D1     =TRANSPOSE(A1:A3)   dynamic, spills D1:F1
//   D3     =SUM(B1#)           reads the spill
//   E3     =SEQUENCE(2,2,5)    dynamic, spills E3:F4 (a function only Excel 365 has)
void FillArrayBook(UltraCanvasSpreadsheet& book) {
    SpreadsheetSheet* s = book.GetSheet(0);
    for (int r = 0; r < 3; ++r) s->GetCell(r, 0)->SetNumber(r + 1);
    s->GetCell(0, 1)->SetFormula("=A1:A3*10");
    s->SetArrayFormula(CellRange(0, 2, 2, 2), "=A1:A3+B1:B3");
    s->GetCell(0, 3)->SetFormula("=TRANSPOSE(A1:A3)");
    s->GetCell(2, 3)->SetFormula("=SUM(B1#)");
    s->GetCell(2, 4)->SetFormula("=SEQUENCE(2,2,5)");
    book.RecalculateAll();
}

void CheckArrayBook(UltraCanvasSpreadsheet& book, const std::string& label, bool dynamicKept) {
    const SpreadsheetSheet* s = book.GetSheet(0);
    ExpectBlock(s, 0, 0, 3, 6, "1,10,11,1,2,3;2,20,22,,,;3,30,33,60,5,6", label + ": the values");
    ExpectBlock(s, 3, 4, 1, 2, "7,8", label + ": E4:F4");
    const SpreadsheetCell* b1 = s->GetCellIfExists(0, 1);
    const SpreadsheetCell* c1 = s->GetCellIfExists(0, 2);
    const SpreadsheetCell* d1 = s->GetCellIfExists(0, 3);
    Check(b1 && b1->HasFormula() && c1 && c1->HasFormula() && d1 && d1->HasFormula(),
          label + ": B1, C1 and D1 hold their formulas");
    if (!b1 || !c1 || !d1) return;
    const FormulaArrayMode spillMode = dynamicKept ? FormulaArrayMode::Spilling : FormulaArrayMode::FixedRange;
    Check(b1->GetFormulaArrayMode() == spillMode && b1->GetArrayRows() == 3 && b1->GetArrayCols() == 1,
          label + std::string(": B1 is ") + (dynamicKept ? "a dynamic spill" : "a fixed array") + " over B1:B3");
    Check(c1->GetFormulaArrayMode() == FormulaArrayMode::FixedRange && c1->GetArrayRows() == 3,
          label + ": C1 is a fixed array over C1:C3");
    Check(d1->GetArrayCols() == 3 && d1->GetArrayRows() == 1, label + ": D1 covers D1:F1");
    const SpreadsheetCell* b2 = s->GetCellIfExists(1, 1);
    const SpreadsheetCell* c3 = s->GetCellIfExists(2, 2);
    Check(b2 && b2->IsSpilledValue() && !b2->HasFormula() && c3 && c3->IsSpilledValue(),
          label + ": B2 and C3 are the arrays' values, not formulas");
}

std::string ReadEntry(const fs::path& file, const std::string& entry) {
    UCZipPackageReader zip;
    std::string out;
    if (zip.Open(PathToUtf8(file))) zip.ReadEntry(entry, out);
    return out;
}

bool HaveSoffice() {
#ifdef _WIN32
    return false;
#else
    return std::system("command -v soffice > /dev/null 2>&1") == 0;
#endif
}

void TestFiles() {
    std::cout << "\n=== .xlsx and .ods ===\n";
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8("uc-array-formulas-" + std::to_string(stamp));
    fs::create_directories(dir, ec);
    const fs::path xlsx = dir / "arrays.xlsx";
    const fs::path ods = dir / "arrays.ods";

    UltraCanvasSpreadsheet book("Arrays", 0, 0, 800, 600);
    FillArrayBook(book);
    CheckArrayBook(book, "calculated", true);
    Check(book.SaveToFile(PathToUtf8(xlsx)), "saved as .xlsx " + book.GetLastError());
    Check(book.SaveToFile(PathToUtf8(ods)), "saved as .ods " + book.GetLastError());

    const std::string sheetXml = ReadEntry(xlsx, "xl/worksheets/sheet1.xml");
    Check(sheetXml.find("<c r=\"B1\" cm=\"1\"><f t=\"array\" ref=\"B1:B3\">A1:A3*10</f>") != std::string::npos,
          ".xlsx: B1 is <f t=\"array\" ref=\"B1:B3\"> with cm=\"1\"");
    Check(sheetXml.find("<c r=\"C1\"><f t=\"array\" ref=\"C1:C3\">A1:A3+B1:B3</f>") != std::string::npos,
          ".xlsx: C1 is <f t=\"array\" ref=\"C1:C3\"> without cm");
    Check(sheetXml.find("SUM(_xlfn.ANCHORARRAY(B1))") != std::string::npos, ".xlsx: B1# is _xlfn.ANCHORARRAY(B1)");
    Check(sheetXml.find("_xlfn.SEQUENCE(2,2,5)") != std::string::npos, ".xlsx: SEQUENCE has its _xlfn. prefix");
    const std::string metadata = ReadEntry(xlsx, "xl/metadata.xml");
    Check(metadata.find("XLDAPR") != std::string::npos && metadata.find("fDynamic=\"1\"") != std::string::npos,
          ".xlsx: xl/metadata.xml marks the dynamic arrays");
    Check(ReadEntry(xlsx, "[Content_Types].xml").find("sheetMetadata+xml") != std::string::npos &&
              ReadEntry(xlsx, "xl/_rels/workbook.xml.rels").find("relationships/sheetMetadata") != std::string::npos,
          ".xlsx: the metadata part has its content type and relationship");
    const std::string content = ReadEntry(ods, "content.xml");
    Check(content.find("table:number-matrix-columns-spanned=\"1\" table:number-matrix-rows-spanned=\"3\"") !=
              std::string::npos,
          ".ods: the arrays span their blocks (table:number-matrix-*-spanned)");
    Check(content.find("COM.MICROSOFT.SEQUENCE") != std::string::npos, ".ods: SEQUENCE in Excel's namespace");

    if (auto loaded = Load(PathToUtf8(xlsx))) {
        CheckArrayBook(*loaded, ".xlsx loaded", true);
        // Still a spill after the round trip: it grows with its source.
        SpreadsheetSheet* s = loaded->GetSheet(0);
        s->SetCellValue(2, 0, 4.0);
        loaded->Recalculate();
        ExpectBlock(s, 0, 1, 3, 1, "10;20;40", ".xlsx loaded: the spill follows A3");
        Check(IsNumber(Raw(s, 2, 3), 70), ".xlsx loaded: =SUM(B1#) follows too");
        loaded->RecalculateAll();
        CheckArrayBook(*Load(PathToUtf8(xlsx)), ".xlsx loaded again", true);
    }
    if (auto loaded = Load(PathToUtf8(ods))) {
        loaded->RecalculateAll();
        // ODF has no dynamic spill: a spill comes back as an array formula
        // over the block it covered.
        CheckArrayBook(*loaded, ".ods loaded", false);
    }

    if (!HaveSoffice()) {
        std::cout << "  skipped: soffice is not installed\n";
    } else {
        // LibreOffice recalculates on load (a profile that says so), and
        // writes the first sheet as CSV.
        const fs::path profile = dir / "profile";
        fs::create_directories(profile / "user", ec);
        std::ofstream(profile / "user" / "registrymodifications.xcu")
            << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<oor:items xmlns:oor=\"http://openoffice.org/2001/registry\" "
               "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
               "<item oor:path=\"/org.openoffice.Office.Calc/Formula/Load\"><prop oor:name=\"OOXMLRecalcMode\" "
               "oor:op=\"fuse\"><value>0</value></prop></item>\n"
               "<item oor:path=\"/org.openoffice.Office.Calc/Formula/Load\"><prop oor:name=\"ODFRecalcMode\" "
               "oor:op=\"fuse\"><value>0</value></prop></item>\n"
               "</oor:items>\n";
        // What LibreOffice 24.2 calculates: no SEQUENCE and no spill operator
        // there, so this book has only the arrays it knows.
        UltraCanvasSpreadsheet plain("Plain", 0, 0, 800, 600);
        SpreadsheetSheet* s = plain.GetSheet(0);
        for (int r = 0; r < 3; ++r) s->GetCell(r, 0)->SetNumber(r + 1);
        s->GetCell(0, 1)->SetFormula("=A1:A3*10");
        s->SetArrayFormula(CellRange(0, 2, 2, 2), "=A1:A3+B1:B3");
        s->GetCell(0, 3)->SetFormula("=TRANSPOSE(A1:A3)");
        s->GetCell(2, 3)->SetFormula("=SUM(B1:B3)");
        plain.RecalculateAll();
        const fs::path lo = dir / "lo.xlsx";
        Check(plain.SaveToFile(PathToUtf8(lo)), "saved the LibreOffice book as .xlsx");
        const fs::path loOds = dir / "loods.ods";
        Check(plain.SaveToFile(PathToUtf8(loOds)), "and as .ods");
        // The first sheet as LibreOffice calculates it, as CSV ("" when it
        // could not convert).
        auto asCsv = [&](const fs::path& source) -> std::string {
            const fs::path outDir = dir / "csv";
            fs::create_directories(outDir, ec);
            const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                        " --headless --convert-to csv --outdir '" + PathToUtf8(outDir) + "' '" +
                                        PathToUtf8(source) + "' > /dev/null 2>&1";
            if (std::system(command.c_str()) != 0) return std::string();
            std::ifstream csv(outDir / PathFromUtf8(PathToUtf8(source.stem()) + ".csv"));
            std::stringstream text;
            text << csv.rdbuf();
            std::string got = text.str();
            for (char& c : got) if (c == '\r') c = '\n';
            std::string normal;
            for (size_t i = 0; i < got.size(); ++i) {
                if (got[i] == '\n' && !normal.empty() && normal.back() == '\n') continue;
                normal += got[i];
            }
            return normal;
        };
        const std::string expected = "1,10,11,1,2,3\n2,20,22,,,\n3,30,33,60,,\n";
        const std::string fromOds = asCsv(loOds);
        if (fromOds.empty()) {
            std::cout << "  skipped: soffice did not convert the .ods\n";
        } else {
            Check(fromOds == expected, "LibreOffice opens the .ods with the same values" +
                                           std::string(fromOds == expected ? "" : "  (got " + fromOds + ")"));
        }
        const std::string normal = asCsv(lo);
        if (normal.empty()) {
            std::cout << "  skipped: soffice did not convert the .xlsx\n";
        } else {
            Check(normal == expected, "LibreOffice opens the .xlsx with the same values" +
                                          std::string(normal == expected ? "" : "  (got " + normal + ")"));
            // And what LibreOffice saves back, read here.
            const fs::path back = dir / "back";
            fs::create_directories(back, ec);
            const std::string toXlsx = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                       " --headless --convert-to xlsx --outdir '" + PathToUtf8(back) + "' '" +
                                       PathToUtf8(lo) + "' > /dev/null 2>&1";
            if (std::system(toXlsx.c_str()) == 0) {
                if (auto loaded = Load(PathToUtf8(back / "lo.xlsx"))) {
                    loaded->RecalculateAll();
                    ExpectBlock(loaded->GetSheet(0), 0, 0, 3, 6, "1,10,11,1,2,3;2,20,22,,,;3,30,33,60,,",
                                "LibreOffice's own .xlsx of it, read here");
                    const SpreadsheetCell* b1 = loaded->GetSheet(0)->GetCellIfExists(0, 1);
                    const SpreadsheetCell* d1 = loaded->GetSheet(0)->GetCellIfExists(0, 3);
                    Check(b1 && b1->HasFormula() && b1->GetArrayRows() == 3 && d1 && d1->HasFormula() &&
                              d1->GetArrayCols() == 3,
                          "LibreOffice kept B1 and D1 as array formulas over B1:B3 and D1:F1");
                }
            }
        }
    }
    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    TestArrayValues();
    TestDynamicArrayFunctions();
    TestSpills();
    TestFixedArraysAndIntersection();
    TestFinancial();
    TestFiles();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: " + std::to_string(g_failures))
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
