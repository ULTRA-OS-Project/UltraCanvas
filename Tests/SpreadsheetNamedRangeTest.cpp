// Tests/SpreadsheetNamedRangeTest.cpp
// The workbook's named ranges and their scope. Until 2026-11 a name was
// stored by its text alone and looked up the same way from every sheet:
//  - a name scoped to one sheet was used from any other sheet;
//  - a scoped name whose range named no sheet read the cells of whichever
//    sheet the formula was on;
//  - the same name scoped to two sheets ("Total" on each month) kept only the
//    last one;
//  - a name that did not exist read A1 instead of giving #NAME?;
//  - renaming a sheet left the names scoped to it or covering it behind;
//  - deleting a sheet kept its names, broken; Excel drops those scoped to it
//    and shows #REF! for those covering it.
// Driven on the grid with no window.
//
// Usage: SpreadsheetNamedRangeTest
// Exit code is the number of failed checks.

#include "UltraCanvasSpreadsheet.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

std::string Shown(UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    return book.GetSheet(sheet)->GetCellDisplayValue(row, col);
}

double Number(UltraCanvasSpreadsheet& book, int sheet, int row, int col) {
    return book.GetSheet(sheet)->GetCellNumber(row, col);
}

void Formula(UltraCanvasSpreadsheet& book, int sheet, int row, int col, const std::string& text) {
    book.SetActiveSheet(sheet);
    book.SetCellFormula(row, col, text);
}

void Value(UltraCanvasSpreadsheet& book, int sheet, int row, int col, double value) {
    book.SetActiveSheet(sheet);
    book.SetCellValue(row, col, value);
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: named ranges and their scope ===\n");
    auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
    book->AddSheet("Data");                            // sheet 1
    Value(*book, 0, 0, 0, 60.0);                       // Sheet1.A1
    Value(*book, 0, 1, 0, 0.0);                        // Sheet1.A2
    Value(*book, 1, 0, 0, 3.0);                        // Data.A1
    Value(*book, 1, 1, 0, 4.0);                        // Data.A2

    std::printf("\n-- a name scoped to a sheet --\n");
    book->AddNamedRange("Amounts", CellRange(0, 0, 1, 0), "Data");   // no sheet in the range
    Formula(*book, 1, 0, 1, "=SUM(Amounts)");          // Data.B1
    Formula(*book, 0, 0, 1, "=SUM(Amounts)");          // Sheet1.B1
    Formula(*book, 1, 1, 1, "=SUM(amounts)");          // Data.B2: other case
    Check(Number(*book, 1, 0, 1) == 7.0, "on Data it covers Data.A1:A2 = 7 [" + Shown(*book, 1, 0, 1) + "]");
    Check(Shown(*book, 0, 0, 1) == "#NAME?", "on Sheet1 it does not exist [" + Shown(*book, 0, 0, 1) + "]");
    Check(Number(*book, 1, 1, 1) == 7.0, "names match without regard to case");

    std::printf("\n-- a name that does not exist --\n");
    Formula(*book, 0, 2, 1, "=Nowhere*2");             // Sheet1.B3
    Check(Shown(*book, 0, 2, 1) == "#NAME?", "#NAME?, not A1*2 = 120 [" + Shown(*book, 0, 2, 1) + "]");

    std::printf("\n-- the same name on two sheets, and for the workbook --\n");
    CellRange onSheet1(0, 0, 0, 0);
    onSheet1.start.sheetName = onSheet1.end.sheetName = "Sheet1";
    book->AddNamedRange("Rate", onSheet1);             // workbook: Sheet1.A1 = 60
    book->AddNamedRange("Rate", CellRange(1, 0, 1, 0), "Data");   // on Data: Data.A2 = 4
    Formula(*book, 0, 3, 1, "=Rate");                  // Sheet1.B4
    Formula(*book, 1, 3, 1, "=Rate");                  // Data.B4
    Check(Number(*book, 0, 3, 1) == 60.0, "Sheet1 sees the workbook's Rate [" + Shown(*book, 0, 3, 1) + "]");
    Check(Number(*book, 1, 3, 1) == 4.0, "Data sees its own Rate [" + Shown(*book, 1, 3, 1) + "]");
    Check(book->HasNamedRange("Rate", "") && book->HasNamedRange("Rate", "Data") && book->HasNamedRange("rate"),
          "both are kept");
    book->AddSheet("Feb");                             // sheet 2
    Value(*book, 2, 0, 0, 100.0);
    book->AddNamedRange("Total", CellRange(0, 0, 0, 0), "Data");
    book->AddNamedRange("Total", CellRange(0, 0, 0, 0), "Feb");
    Formula(*book, 1, 4, 1, "=Total");                 // Data.B5
    Formula(*book, 2, 4, 1, "=Total");                 // Feb.B5
    Check(Number(*book, 1, 4, 1) == 3.0 && Number(*book, 2, 4, 1) == 100.0,
          "Total on Data and on Feb each read their own sheet [" + Shown(*book, 1, 4, 1) + ", " + Shown(*book, 2, 4, 1) + "]");

    std::printf("\n-- rows inserted on the sheet a name covers --\n");
    book->SetActiveSheet(1);
    book->InsertRowsAt(0, 1);                          // Data.A1:A2 -> A2:A3
    Check(book->GetNamedRange("Amounts", "Data").start.row == 1, "Amounts moved down with its cells");
    Check(Number(*book, 1, 1, 1) == 7.0, "and still sums them [" + Shown(*book, 1, 1, 1) + "]");
    book->Undo();
    Check(book->GetNamedRange("Amounts", "Data").start.row == 0, "undo puts it back");

    std::printf("\n-- renaming the sheet --\n");
    book->RenameSheet(1, "Daten");
    Check(book->HasNamedRange("Amounts", "Daten") && !book->HasNamedRange("Amounts", "Data"),
          "the name's scope follows the sheet");
    Check(book->GetNamedRange("Amounts", "Daten").start.sheetName == "Daten", "and so does what it covers");
    Check(Number(*book, 1, 0, 1) == 7.0, "Daten.B1 still sums 7 [" + Shown(*book, 1, 0, 1) + "]");
    book->Undo();
    Check(book->HasNamedRange("Amounts", "Data"), "undoing the rename brings the name back to Data");

    std::printf("\n-- removing one scope --\n");
    book->RemoveNamedRange("Rate", "Data");
    Check(book->HasNamedRange("Rate", "") && !book->HasNamedRange("Rate", "Data"), "only Data's Rate is gone");
    book->Recalculate();
    Check(Number(*book, 1, 3, 1) == 60.0, "Data now sees the workbook's Rate [" + Shown(*book, 1, 3, 1) + "]");

    std::printf("\n-- deleting the sheet a name belongs to --\n");
    {
        auto wb = std::make_shared<UltraCanvasSpreadsheet>("wb", 0, 0, 900, 600);
        wb->AddSheet("Gone");                          // sheet 1
        Value(*wb, 1, 0, 0, 5.0);                      // Gone.A1
        CellRange onGone(0, 0, 0, 0);
        onGone.start.sheetName = onGone.end.sheetName = "Gone";
        wb->AddNamedRange("Price", onGone);            // workbook name covering Gone
        wb->AddNamedRange("Local", CellRange(0, 0, 0, 0), "Gone");   // scoped to Gone
        Formula(*wb, 0, 0, 0, "=Price*2");             // Sheet1.A1
        Check(Number(*wb, 0, 0, 0) == 10.0, "Price*2 = 10 before");
        wb->RemoveSheet(1);
        Check(Shown(*wb, 0, 0, 0) == "#REF!", "a name covering the deleted sheet is #REF! [" + Shown(*wb, 0, 0, 0) + "]");
        Check(wb->HasNamedRange("Price", "") && !wb->GetNamedRange("Price").IsValid(), "and stays, broken");
        Check(!wb->HasNamedRange("Local"), "a name scoped to it is gone with it");
        Formula(*wb, 0, 1, 0, "=SUM(Price)");          // as a function's argument
        Check(Shown(*wb, 0, 1, 0) == "#REF!", "SUM(Price) is #REF! too [" + Shown(*wb, 0, 1, 0) + "]");
        wb->SetCellValue(0, 1, 0.0);                   // clear that formula again
        wb->ClearCell(0, 1);
        wb->Undo();                                    // the clear
        wb->Undo();                                    // the value
        wb->Undo();                                    // the SUM formula
        wb->Undo();                                    // the delete
        Check(wb->GetSheetCount() == 2, "undo brings the sheet back");
        Check(wb->GetNamedRange("Price").start.sheetName == "Gone" && wb->HasNamedRange("Local", "Gone"),
              "and both names");
        Check(Number(*wb, 0, 0, 0) == 10.0, "Price*2 = 10 again [" + Shown(*wb, 0, 0, 0) + "]");
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
