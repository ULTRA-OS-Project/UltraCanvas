// Tests/SpreadsheetRangeSortTest.cpp
// Regression test for sorting a selected block of a spreadsheet.
//
// The header sort buttons sort only the selected block, by the clicked
// column, and carry the block's other columns along so every row keeps its
// cells together. Anything outside the block - the title row above it, the
// totals row below it, the columns beside it - must stay exactly where it is.
// This test pins that down on the real SpreadsheetSheet, which is what the
// buttons call (UltraCanvasSpreadsheet::SortSelectionByColumn), with no UI stack.
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

// A small report: a title row, four data rows, a totals row, and a notes
// column (D) that is not part of the block being sorted.
//
//      A          B     C        D
//  0   Month      Qty   Price    Note
//  1   March      30    2.5      n1
//  2   January    10    9.0      n2
//  3   April      40    1.0      n3
//  4   February   20    4.0      n4
//  5   Total      100            total
static void Seed(SpreadsheetSheet& sheet) {
    sheet.GetCell(0, 0)->SetText("Month");
    sheet.GetCell(0, 1)->SetText("Qty");
    sheet.GetCell(0, 2)->SetText("Price");
    sheet.GetCell(0, 3)->SetText("Note");
    const char* months[] = { "March", "January", "April", "February" };
    const double qty[]   = { 30, 10, 40, 20 };
    const double price[] = { 2.5, 9.0, 1.0, 4.0 };
    for (int i = 0; i < 4; ++i) {
        sheet.GetCell(i + 1, 0)->SetText(months[i]);
        sheet.GetCell(i + 1, 1)->SetNumber(qty[i]);
        sheet.GetCell(i + 1, 2)->SetNumber(price[i]);
        sheet.GetCell(i + 1, 3)->SetText("n" + std::to_string(i + 1));
    }
    sheet.GetCell(5, 0)->SetText("Total");
    sheet.GetCell(5, 1)->SetNumber(100);
    sheet.GetCell(5, 3)->SetText("total");
}

static std::string Text(SpreadsheetSheet& sheet, int row, int col) {
    return sheet.GetCell(row, col)->GetText();
}

static double Number(SpreadsheetSheet& sheet, int row, int col) {
    return sheet.GetCell(row, col)->GetNumber();
}

// Everything outside the block A2:C5 is where the seed put it.
static void CheckOutsideUntouched(SpreadsheetSheet& sheet) {
    CHECK_EQ(Text(sheet, 0, 0), std::string("Month"));
    CHECK_EQ(Text(sheet, 0, 2), std::string("Price"));
    CHECK_EQ(Text(sheet, 5, 0), std::string("Total"));
    CHECK_EQ(Number(sheet, 5, 1), 100.0);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(Text(sheet, i + 1, 3), "n" + std::to_string(i + 1));
    }
    CHECK_EQ(Text(sheet, 5, 3), std::string("total"));
}

// Sorting by a middle column of the block moves the whole row of the block.
static void TestSortByClickedColumn() {
    SpreadsheetSheet sheet("Report");
    Seed(sheet);
    const CellRange block(1, 0, 4, 2);   // A2:C5

    sheet.SortByColumn(block, 1, SortOrder::Ascending);   // by Qty
    const char* months[] = { "January", "February", "March", "April" };
    const double qty[]   = { 10, 20, 30, 40 };
    const double price[] = { 9.0, 4.0, 2.5, 1.0 };
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(Text(sheet, i + 1, 0), std::string(months[i]));
        CHECK_EQ(Number(sheet, i + 1, 1), qty[i]);
        CHECK_EQ(Number(sheet, i + 1, 2), price[i]);
    }
    CheckOutsideUntouched(sheet);

    sheet.SortByColumn(block, 2, SortOrder::Descending);  // by Price, reversed
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(Number(sheet, i + 1, 2), price[i]);       // 9, 4, 2.5, 1
        CHECK_EQ(Text(sheet, i + 1, 0), std::string(months[i]));
    }
    CheckOutsideUntouched(sheet);
}

// Text keys sort case-insensitively, and a cell keeps its own position record.
static void TestSortByTextColumn() {
    SpreadsheetSheet sheet("Report");
    Seed(sheet);
    const CellRange block(1, 0, 4, 2);

    sheet.SortByColumn(block, 0, SortOrder::Ascending);
    const char* months[] = { "April", "February", "January", "March" };
    const double qty[]   = { 40, 20, 10, 30 };
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(Text(sheet, i + 1, 0), std::string(months[i]));
        CHECK_EQ(Number(sheet, i + 1, 1), qty[i]);
        CHECK_EQ(sheet.GetCell(i + 1, 1)->GetRow(), i + 1);
    }
    CheckOutsideUntouched(sheet);
}

// The header sort warns before moving formulas; CountFormulaCells is what it
// asks. Only cells inside the range count.
static void TestCountFormulaCells() {
    SpreadsheetSheet sheet("Report");
    Seed(sheet);
    const CellRange block(1, 0, 4, 2);                    // A2:C5
    CHECK_EQ(sheet.CountFormulaCells(block), 0);

    sheet.GetCell(2, 2)->SetFormula("=B3*2");             // C3, inside
    sheet.GetCell(5, 2)->SetFormula("=SUM(C2:C5)");       // C6, totals row, outside
    CHECK_EQ(sheet.CountFormulaCells(block), 1);
    CHECK_EQ(sheet.CountFormulaCells(CellRange(1, 0, 5, 2)), 2);
    CHECK_EQ(sheet.CountFormulaCells(CellRange(1, 0, 4, 1)), 0);   // A2:B5
}

// A single selected column sorts on its own; its neighbours do not move.
static void TestSortSingleColumn() {
    SpreadsheetSheet sheet("Report");
    Seed(sheet);
    sheet.SortByColumn(CellRange(1, 1, 4, 1), 1, SortOrder::Descending);   // B2:B5
    const double qty[] = { 40, 30, 20, 10 };
    const char* months[] = { "March", "January", "April", "February" };   // unchanged
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(Number(sheet, i + 1, 1), qty[i]);
        CHECK_EQ(Text(sheet, i + 1, 0), std::string(months[i]));
    }
    CheckOutsideUntouched(sheet);
}

int main() {
    TestSortByClickedColumn();
    TestSortByTextColumn();
    TestSortSingleColumn();
    TestCountFormulaCells();

    if (failures == 0) {
        std::cout << "SpreadsheetRangeSortTest: all checks passed\n";
        return 0;
    }
    std::cerr << "SpreadsheetRangeSortTest: " << failures << " failure(s)\n";
    return 1;
}
