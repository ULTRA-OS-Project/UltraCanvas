// Tests/SpreadsheetAutoFillTest.cpp
// Regression test for the spreadsheet fill handle.
//
// The handle used to be drawn with nothing behind it: dragging it did not
// fill anything. The drag now ends in SpreadsheetSheet::AutoFill, which
// continues number series, counts on a text ending in a number, shifts the
// relative references of copied formulas and repeats anything else. This
// test pins that down on the real SpreadsheetSheet with no UI stack, together
// with ShiftFormulaReferences, which does the formula part.
#include "UltraCanvasSpreadsheetSheet.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!(_a == _e)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  " #actual " == " #expected \
                  << "  [got " << _a << ", want " << _e << "]\n"; \
        ++failures; \
    } \
} while (0)

static void TestShiftFormulaReferences() {
    CHECK_EQ(ShiftFormulaReferences("=B2*2", 1, 0), std::string("=B3*2"));
    CHECK_EQ(ShiftFormulaReferences("=SUM(D2:D4)/C2", 3, 0), std::string("=SUM(D5:D7)/C5"));
    CHECK_EQ(ShiftFormulaReferences("=A1+B1", 0, 2), std::string("=C1+D1"));
    // $ anchors hold their part.
    CHECK_EQ(ShiftFormulaReferences("=B2*$C$1", 4, 1), std::string("=C6*$C$1"));
    CHECK_EQ(ShiftFormulaReferences("=$B2+B$2", 1, 1), std::string("=$B3+C$2"));
    // Function names, text in quotes and longer names are left alone.
    CHECK_EQ(ShiftFormulaReferences("=LOG10(A1)", 1, 0), std::string("=LOG10(A2)"));
    CHECK_EQ(ShiftFormulaReferences("=IF(A1>0;\"B2\";A1)", 1, 0), std::string("=IF(A2>0;\"B2\";A2)"));
    CHECK_EQ(ShiftFormulaReferences("=Sheet2!A1*RATE2020", 1, 0), std::string("=Sheet2!A2*RATE2020"));
    // A row number no sheet has is not a reference (and must not throw).
    CHECK_EQ(ShiftFormulaReferences("=A99999999999+1", 1, 0), std::string("=A99999999999+1"));
    // Pushed off the sheet.
    CHECK_EQ(ShiftFormulaReferences("=A1", -1, 0), std::string("=#REF!"));
}

static void TestNumberSeriesDown() {
    SpreadsheetSheet sheet("Fill");
    sheet.GetCell(0, 0)->SetNumber(1);
    sheet.GetCell(1, 0)->SetNumber(2);
    sheet.GetCell(0, 1)->SetNumber(10);
    sheet.GetCell(1, 1)->SetNumber(20);
    sheet.AutoFill(CellRange(0, 0, 1, 1), CellRange(0, 0, 4, 1));   // A1:B2 -> A1:B5
    CHECK_EQ(sheet.GetCell(2, 0)->GetNumber(), 3.0);
    CHECK_EQ(sheet.GetCell(4, 0)->GetNumber(), 5.0);
    CHECK_EQ(sheet.GetCell(2, 1)->GetNumber(), 30.0);
    CHECK_EQ(sheet.GetCell(4, 1)->GetNumber(), 50.0);
    // The source is untouched.
    CHECK_EQ(sheet.GetCell(0, 0)->GetNumber(), 1.0);
}

static void TestSeriesUpAndLeft() {
    SpreadsheetSheet sheet("Fill");
    sheet.GetCell(5, 0)->SetNumber(10);
    sheet.GetCell(6, 0)->SetNumber(20);
    sheet.AutoFill(CellRange(5, 0, 6, 0), CellRange(3, 0, 6, 0));   // A6:A7 -> A4:A7
    CHECK_EQ(sheet.GetCell(4, 0)->GetNumber(), 0.0);
    CHECK_EQ(sheet.GetCell(3, 0)->GetNumber(), -10.0);

    sheet.GetCell(0, 4)->SetNumber(5);
    sheet.GetCell(0, 5)->SetNumber(7);
    sheet.AutoFill(CellRange(0, 4, 0, 5), CellRange(0, 2, 0, 5));   // E1:F1 -> C1:F1
    CHECK_EQ(sheet.GetCell(0, 3)->GetNumber(), 3.0);
    CHECK_EQ(sheet.GetCell(0, 2)->GetNumber(), 1.0);
}

static void TestSingleValues() {
    SpreadsheetSheet sheet("Fill");
    sheet.GetCell(0, 0)->SetNumber(42);          // one number: copied
    sheet.GetCell(0, 1)->SetText("Item 1");      // text + number: counts on
    sheet.GetCell(0, 2)->SetText("Total");       // plain text: copied
    sheet.AutoFill(CellRange(0, 0, 0, 2), CellRange(0, 0, 3, 2));
    CHECK_EQ(sheet.GetCell(3, 0)->GetNumber(), 42.0);
    CHECK_EQ(sheet.GetCell(1, 1)->GetText(), std::string("Item 2"));
    CHECK_EQ(sheet.GetCell(3, 1)->GetText(), std::string("Item 4"));
    CHECK_EQ(sheet.GetCell(2, 2)->GetText(), std::string("Total"));
}

static void TestPatternAndFormulas() {
    SpreadsheetSheet sheet("Fill");
    sheet.GetCell(0, 0)->SetText("a");
    sheet.GetCell(1, 0)->SetText("b");
    sheet.GetCell(0, 1)->SetFormula("=C1*2");
    sheet.GetCell(1, 1)->SetFormula("=C2*$D$1");
    sheet.AutoFill(CellRange(0, 0, 1, 1), CellRange(0, 0, 5, 1));   // A1:B2 -> A1:B6
    CHECK_EQ(sheet.GetCell(2, 0)->GetText(), std::string("a"));
    CHECK_EQ(sheet.GetCell(5, 0)->GetText(), std::string("b"));
    CHECK_EQ(sheet.GetCell(2, 1)->GetFormulaText(), std::string("=C3*2"));
    CHECK_EQ(sheet.GetCell(3, 1)->GetFormulaText(), std::string("=C4*$D$1"));
    CHECK_EQ(sheet.GetCell(5, 1)->GetFormulaText(), std::string("=C6*$D$1"));
    CHECK_EQ(sheet.GetCell(5, 1)->GetRow(), 5);
}

static void TestFormattingComes() {
    SpreadsheetSheet sheet("Fill");
    sheet.GetCell(0, 0)->SetNumber(1);
    sheet.GetCell(0, 0)->SetBold(true);
    sheet.GetCell(1, 0)->SetNumber(2);
    sheet.GetCell(1, 0)->SetBold(true);
    sheet.AutoFill(CellRange(0, 0, 1, 0), CellRange(0, 0, 3, 0));
    CHECK_EQ(sheet.GetCell(3, 0)->GetNumber(), 4.0);
    CHECK_EQ(sheet.GetCell(3, 0)->GetStyle().font.bold, true);
}

int main() {
    TestShiftFormulaReferences();
    TestNumberSeriesDown();
    TestSeriesUpAndLeft();
    TestSingleValues();
    TestPatternAndFormulas();
    TestFormattingComes();

    if (failures == 0) {
        std::cout << "SpreadsheetAutoFillTest: all checks passed\n";
        return 0;
    }
    std::cerr << "SpreadsheetAutoFillTest: " << failures << " failure(s)\n";
    return 1;
}
