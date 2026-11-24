// Tests/SpreadsheetRecalcTest.cpp
// A spreadsheet's formulas follow their inputs. Until 2026-11 Recalculate
// only calculated formulas that were already marked dirty, so changing a
// cell left every formula reading it at its old result; only RecalculateAll
// put them right. Driven on the grid with no window:
//  - a value changed after the formula, and a formula set before its input;
//  - chains (A1 -> B1 -> C1), ranges (SUM), other sheets, named ranges;
//  - typing, clearing, paste, undo, inserting and deleting rows (here and on
//    a sheet other formulas name), sorting, and a write into the sheet;
//  - formulas that read nothing that changed keep their result.
//
// Usage: SpreadsheetRecalcTest
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

std::string Shown(UltraCanvasSpreadsheet& grid, int row, int col) {
    return " [" + grid.GetCellDisplayValue(row, col) + "]";
}

void TypeInto(UltraCanvasSpreadsheet& grid, int row, int col, const std::string& text) {
    grid.SelectCell(row, col);
    grid.StartInCellEditing();
    grid.SetEditBuffer(text);
    grid.StopEditing(true);
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: formulas follow their inputs ===\n");

    std::printf("\n-- a changed input --\n");
    {
        auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
        grid->SetCellValue(1, 1, 4.0);                 // B2
        grid->SetCellFormula(1, 2, "=B2*2");           // C2
        Check(grid->GetCellNumber(1, 2) == 8.0, "C2 = B2*2 = 8" + Shown(*grid, 1, 2));
        grid->SetCellValue(1, 1, 5.0);
        Check(grid->GetCellNumber(1, 2) == 10.0, "B2 = 5 makes C2 10" + Shown(*grid, 1, 2));

        grid->SetCellFormula(0, 2, "=B1*2");           // C1 before its input
        grid->SetCellValue(0, 1, 4.0);                 // B1
        Check(grid->GetCellNumber(0, 2) == 8.0, "a formula set before its input follows it" + Shown(*grid, 0, 2));
    }

    std::printf("\n-- chains and ranges --\n");
    {
        auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
        grid->SetCellValue(0, 0, 1.0);                 // A1
        grid->SetCellFormula(0, 1, "=A1+1");           // B1
        grid->SetCellFormula(0, 2, "=B1*10");          // C1
        grid->SetCellFormula(0, 3, "=C1+B1");          // D1
        for (int r = 1; r <= 5; ++r) grid->SetCellValue(r, 0, static_cast<double>(r));   // A2:A6
        grid->SetCellFormula(6, 0, "=SUM(A2:A6)");     // A7
        grid->SetCellFormula(7, 0, "=A7/5");           // A8
        grid->SetCellFormula(8, 0, "=99");             // A9: reads nothing
        Check(grid->GetCellNumber(0, 3) == 22.0, "D1 = C1+B1 = 22" + Shown(*grid, 0, 3));

        grid->SetCellValue(0, 0, 4.0);
        Check(grid->GetCellNumber(0, 1) == 5.0 && grid->GetCellNumber(0, 2) == 50.0 &&
              grid->GetCellNumber(0, 3) == 55.0, "A1 = 4 runs down the chain to D1 = 55" + Shown(*grid, 0, 3));

        grid->SetCellValue(3, 0, 13.0);                // A4: 3 -> 13
        Check(grid->GetCellNumber(6, 0) == 25.0, "SUM(A2:A6) follows A4" + Shown(*grid, 6, 0));
        Check(grid->GetCellNumber(7, 0) == 5.0, "and A8 = A7/5 follows the SUM" + Shown(*grid, 7, 0));
        Check(grid->GetCellNumber(8, 0) == 99.0, "A9 keeps 99");

        // A long chain, deeper than one nested calculation follows.
        grid->SetCellValue(0, 5, 1.0);                 // F1
        for (int r = 1; r < 400; ++r) {
            grid->SetCellFormula(r, 5, "=F" + std::to_string(r) + "+1");
        }
        grid->SetCellValue(0, 5, 1000.0);
        Check(grid->GetCellNumber(399, 5) == 1399.0, "a 400-cell chain follows its start" + Shown(*grid, 399, 5));
    }

    std::printf("\n-- other sheets and names --\n");
    {
        auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
        book->AddSheet("Data");
        book->SetActiveSheet(1);
        book->SetCellValue(0, 0, 3.0);                 // Data.A1
        book->SetCellValue(1, 0, 4.0);                 // Data.A2
        book->SetActiveSheet(0);
        book->SetCellFormula(0, 0, "=Data.A1*10");     // Sheet1.A1
        book->SetCellFormula(0, 1, "=SUM(Data.A1:A2)"); // Sheet1.B1
        book->SetCellValue(5, 0, 1.0);                 // Sheet1.A6
        book->SetCellValue(6, 0, 2.0);                 // Sheet1.A7
        book->AddNamedRange("Amounts", CellRange(5, 0, 6, 0));
        book->SetCellFormula(0, 2, "=SUM(Amounts)");   // Sheet1.C1
        Check(book->GetCellNumber(0, 0) == 30.0, "Sheet1.A1 = Data.A1*10 = 30" + Shown(*book, 0, 0));

        book->SetActiveSheet(1);
        book->SetCellValue(0, 0, 6.0);                 // Data.A1 = 6
        book->SetActiveSheet(0);
        Check(book->GetCellNumber(0, 0) == 60.0, "a change on Data reaches Sheet1" + Shown(*book, 0, 0));
        Check(book->GetCellNumber(0, 1) == 10.0, "and a range on Data" + Shown(*book, 0, 1));
        Check(book->GetCellNumber(0, 2) == 3.0, "SUM(Amounts) = 3" + Shown(*book, 0, 2));
        book->SetCellValue(6, 0, 12.0);                // Sheet1.A7
        Check(book->GetCellNumber(0, 2) == 13.0, "a formula naming a range follows it" + Shown(*book, 0, 2));
    }

    std::printf("\n-- inserting rows on a sheet other formulas name --\n");
    {
        auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
        book->AddSheet("Data");
        book->SetActiveSheet(1);
        book->SetCellValue(0, 0, 3.0);                 // Data.A1
        book->SetActiveSheet(0);
        book->SetCellFormula(0, 0, "=Data.A1*10");     // Sheet1.A1
        book->SetCellValue(2, 0, 5.0);                 // Sheet1.A3: same address, other sheet
        book->SetCellFormula(0, 1, "=A3+1");           // Sheet1.B1
        book->SetActiveSheet(1);
        book->InsertRowsAt(0, 2);                      // Data.A1 -> Data.A3
        book->SetActiveSheet(0);
        Check(book->GetCellFormula(0, 0) == "=Data.A3*10", "Sheet1 follows Data's cell [" + book->GetCellFormula(0, 0) + "]");
        Check(book->GetCellFormula(0, 1) == "=A3+1", "and its own A3 stays");
        Check(book->GetCellNumber(0, 0) == 30.0 && book->GetCellNumber(0, 1) == 6.0, "with the same results");
        book->SetActiveSheet(1);
        book->DeleteRowsAt(2, 1);                      // Data.A3 deleted
        book->SetActiveSheet(0);
        Check(book->GetCellFormula(0, 0) == "=#REF!*10", "deleting it makes the reference #REF! [" + book->GetCellFormula(0, 0) + "]");
    }

    std::printf("\n-- every way a cell changes --\n");
    {
        auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
        grid->SetCellValue(0, 0, 2.0);                 // A1
        grid->SetCellFormula(0, 1, "=A1*3");           // B1

        TypeInto(*grid, 0, 0, "7");
        Check(grid->GetCellNumber(0, 1) == 21.0, "typing 7 into A1" + Shown(*grid, 0, 1));

        grid->Undo();
        Check(grid->GetCellNumber(0, 1) == 6.0, "undo puts B1 back to 6" + Shown(*grid, 0, 1));

        grid->ClearRange(CellRange(0, 0, 0, 0));
        Check(grid->GetCellNumber(0, 1) == 0.0, "clearing A1" + Shown(*grid, 0, 1));

        grid->SetCellValue(5, 0, 9.0);                 // A6
        grid->SelectCell(5, 0);
        grid->Copy();
        grid->SelectCell(0, 0);
        grid->Paste();
        Check(grid->GetCellNumber(0, 1) == 27.0, "pasting 9 into A1" + Shown(*grid, 0, 1));

        grid->GetActiveSheet()->SetCellValue(0, 0, 10.0);   // straight into the sheet
        grid->Recalculate();
        Check(grid->GetCellNumber(0, 1) == 30.0, "a write into the sheet itself" + Shown(*grid, 0, 1));

        // Inserting a row moves A1 and B1 down, and B1's =A1*3 with them.
        grid->InsertRowsAt(0, 1);
        Check(grid->GetCellFormula(1, 1) == "=A2*3", "B2 now reads A2 [" + grid->GetCellFormula(1, 1) + "]");
        grid->SetCellValue(1, 0, 11.0);
        Check(grid->GetCellNumber(1, 1) == 33.0, "and follows it after the insert" + Shown(*grid, 1, 1));
        grid->DeleteRowsAt(0, 1);
        Check(grid->GetCellFormula(0, 1) == "=A1*3" && grid->GetCellNumber(0, 1) == 33.0,
              "deleting the row moves both back" + Shown(*grid, 0, 1));

        // Sorting moves the inputs of a formula outside the block.
        grid->SetCellValue(10, 0, 3.0);                // A11
        grid->SetCellValue(11, 0, 1.0);                // A12
        grid->SetCellFormula(10, 3, "=A11*100");       // D11 reads A11
        grid->Select(CellRange(10, 0, 11, 0));
        grid->SortSelectionAscending();
        Check(grid->GetCellNumber(10, 0) == 1.0, "A11:A12 sorted");
        Check(grid->GetCellNumber(10, 3) == 100.0, "and D11 follows the sorted A11" + Shown(*grid, 10, 3));
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
