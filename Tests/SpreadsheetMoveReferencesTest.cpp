// Tests/SpreadsheetMoveReferencesTest.cpp
// Inserting or deleting rows and columns moves the references in formulas,
// as a spreadsheet does (MoveFormulaReferencesForRowsColumns). Until 2026-11
// the cells moved and their formulas did not: =A1*3 in B1 became =A1*3 in B2
// after a row was inserted above, reading the wrong cell. No UI stack.
#include "UltraCanvasSpreadsheetSheet.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

static void Expect(const std::string& got, const std::string& want, const std::string& what) {
    if (got != want) {
        std::cerr << "FAIL " << what << ": got " << got << ", want " << want << "\n";
        ++failures;
    }
}

// Rows inserted (count > 0) or deleted (count < 0) at 0-based `at` on Sheet1,
// seen from a formula on Sheet1.
static std::string Rows(const std::string& f, int at, int count, bool own = true) {
    return MoveFormulaReferencesForRowsColumns(f, "Sheet1", own, false, at, count);
}
static std::string Cols(const std::string& f, int at, int count) {
    return MoveFormulaReferencesForRowsColumns(f, "Sheet1", true, true, at, count);
}

int main() {
    // Two rows inserted before row 2.
    Expect(Rows("=A1*3", 1, 2), "=A1*3", "a reference above the insertion stays");
    Expect(Rows("=A2+$B$5", 1, 2), "=A4+$B$7", "references at and below move, $ ones too");
    Expect(Rows("=SUM(A1:A5)", 1, 2), "=SUM(A1:A7)", "a range across the insertion grows");
    Expect(Rows("=SUM(A2:A5)", 1, 2), "=SUM(A4:A7)", "a range starting at it moves");
    Expect(Rows("=SUM(a2:a5) / 2", 1, 2), "=SUM(A4:A7) / 2", "the rest of the text is kept");
    Expect(Rows("=\"A3\"&A3", 1, 2), "=\"A3\"&A5", "text in quotes is left alone");
    Expect(Rows("=LOG10(A3)", 1, 2), "=LOG10(A5)", "a function name is no reference");
    Expect(Rows("=A3", 1, 2, false), "=A3", "an unqualified reference on another sheet stays");
    Expect(Rows("=Sheet1.A3+Other.A3", 1, 2, false), "=Sheet1.A5+Other.A3", "a qualified one moves, others stay");
    Expect(MoveFormulaReferencesForRowsColumns("='Q 1'!B3:C4", "q 1", false, false, 1, 2), "='Q 1'!B5:C6",
           "a quoted sheet, matched case-insensitively");
    Expect(Rows("=Sheet1.A1:Sheet1.A3", 1, 1, false), "=Sheet1.A1:Sheet1.A4", "a range with both ends qualified");
    Expect(Rows("=A1048576", 0, 1), "=#REF!", "pushed past the last row");

    // Rows 2 and 3 deleted.
    Expect(Rows("=A1", 1, -2), "=A1", "above the deletion stays");
    Expect(Rows("=A2*2", 1, -2), "=#REF!*2", "a deleted cell is #REF!");
    Expect(Rows("=A4*2", 1, -2), "=A2*2", "below the deletion moves up");
    Expect(Rows("=SUM(A1:A5)", 1, -2), "=SUM(A1:A3)", "a range across it shrinks");
    Expect(Rows("=SUM(A2:A3)", 1, -2), "=SUM(#REF!)", "a range wholly deleted is #REF!");
    Expect(Rows("=SUM(A3:A6)", 1, -2), "=SUM(A2:A4)", "a range starting inside it is clipped");
    Expect(Rows("=SUM(A1:A2)", 1, -2), "=SUM(A1:A1)", "a range ending inside it is clipped");

    // Columns.
    Expect(Cols("=A1+B1+C1", 1, 1), "=A1+C1+D1", "a column inserted before B");
    Expect(Cols("=$B$2", 1, 1), "=$C$2", "$ columns move too");
    Expect(Cols("=SUM(A1:C1)", 1, -1), "=SUM(A1:B1)", "column B deleted");
    Expect(Cols("=B1", 1, -1), "=#REF!", "the deleted column");

    // On a sheet: the formulas move with their cells and read the same cells.
    SpreadsheetSheet sheet("Sheet1");
    sheet.SetCellValue(0, 0, 2.0);                 // A1
    sheet.SetCellFormula(0, 1, "=A1*3");           // B1
    sheet.SetCellFormula(5, 1, "=SUM(A1:A5)");     // B6
    sheet.InsertRows(0, 1);
    Expect(sheet.GetCellFormula(1, 1), "=A2*3", "B1's formula, now in B2");
    Expect(sheet.GetCellFormula(6, 1), "=SUM(A2:A6)", "B6's, now in B7");
    sheet.DeleteRows(0, 1);
    Expect(sheet.GetCellFormula(0, 1), "=A1*3", "deleting the row puts it back");
    sheet.InsertColumns(0, 2);
    Expect(sheet.GetCellFormula(0, 3), "=C1*3", "two columns inserted before A");

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "SpreadsheetMoveReferencesTest: all checks passed\n";
    return 0;
}
