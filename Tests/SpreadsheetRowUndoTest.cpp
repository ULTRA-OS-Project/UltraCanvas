// Tests/SpreadsheetRowUndoTest.cpp
// Inserting and deleting rows and columns are undo steps that put the
// workbook back as it was, and redo does them again. Until 2026-11 they
// recorded a placeholder step whose undo cleared the sheet's whole used
// range. Driven on the grid with no window:
//  - undo of an insert: cells, row heights, a merge and the formulas (on the
//    sheet and on another sheet naming it) as they were;
//  - undo of a delete: the deleted row's contents, and formulas that became
//    #REF! read their cells again;
//  - columns; redo; a run of steps undone and redone in order.
//
// Usage: SpreadsheetRowUndoTest
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

// Sheet1: A1..A5 = 1..5, B1 = SUM(A1:A5), B6 = A3*10, row 3 30 px high,
// C1:D1 merged. Data: A1 = Sheet1.A4+1.
std::shared_ptr<UltraCanvasSpreadsheet> MakeBook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
    for (int r = 0; r < 5; ++r) book->SetCellValue(r, 0, static_cast<double>(r + 1));
    book->SetCellFormula(0, 1, "=SUM(A1:A5)");
    book->SetCellFormula(5, 1, "=A3*10");
    book->SetRowHeight(2, 30);
    book->MergeCells(CellRange(0, 2, 0, 3));
    book->AddSheet("Data");
    book->SetActiveSheet(1);
    book->SetCellFormula(0, 0, "=Sheet1.A4+1");
    book->SetActiveSheet(0);
    return book;
}

std::string DataFormula(UltraCanvasSpreadsheet& book) {
    return book.GetSheet(1)->GetCellFormula(0, 0);
}

double DataValue(UltraCanvasSpreadsheet& book) {
    return book.GetSheet(1)->GetCellNumber(0, 0);
}

// Everything MakeBook set up, as it was.
void CheckOriginal(UltraCanvasSpreadsheet& book, const std::string& when) {
    Check(book.GetCellNumber(2, 0) == 3.0 && book.GetCellNumber(4, 0) == 5.0, when + ": A3 = 3, A5 = 5");
    Check(book.GetCellFormula(0, 1) == "=SUM(A1:A5)" && book.GetCellNumber(0, 1) == 15.0,
          when + ": B1 = SUM(A1:A5) = 15 [" + book.GetCellFormula(0, 1) + "]");
    Check(book.GetCellFormula(5, 1) == "=A3*10" && book.GetCellNumber(5, 1) == 30.0,
          when + ": B6 = A3*10 = 30 [" + book.GetCellFormula(5, 1) + "]");
    Check(book.GetRowHeight(2) == 30, when + ": row 3 is 30 px high [" + std::to_string(book.GetRowHeight(2)) + "]");
    Check(book.IsCellMerged(0, 2), when + ": C1:D1 merged");
    Check(DataFormula(book) == "=Sheet1.A4+1" && DataValue(book) == 5.0,
          when + ": Data.A1 = Sheet1.A4+1 = 5 [" + DataFormula(book) + "]");
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: undo and redo of inserted and deleted rows and columns ===\n");

    std::printf("\n-- inserting two rows above row 2 --\n");
    {
        auto book = MakeBook();
        book->InsertRowsAt(1, 2);
        Check(book->GetUndoDescription() == "Insert 2 rows", "an undo step [" + book->GetUndoDescription() + "]");
        Check(book->GetCellNumber(4, 0) == 3.0 && book->GetCellFormula(7, 1) == "=A5*10",
              "A3 moved to A5, and B6 (now B8) reads it [" + book->GetCellFormula(7, 1) + "]");
        Check(DataFormula(*book) == "=Sheet1.A6+1", "Data follows [" + DataFormula(*book) + "]");
        book->SetCellValue(1, 0, 100.0);   // typed into an inserted row, then undone first
        book->Undo();
        book->Undo();
        CheckOriginal(*book, "undone");
        Check(book->GetCellText(1, 0) == "2", "A2 holds 2 again, not what was typed into the new row");
        Check(book->GetRedoDescription() == "Insert 2 rows", "redo offers the insert");
        book->Redo();
        Check(book->GetCellNumber(4, 0) == 3.0 && book->GetCellFormula(7, 1) == "=A5*10" &&
              DataFormula(*book) == "=Sheet1.A6+1", "redo inserts them again");
        book->Undo();
        CheckOriginal(*book, "undone again");
    }

    std::printf("\n-- deleting row 3 --\n");
    {
        auto book = MakeBook();
        book->DeleteRowsAt(2, 1);
        Check(book->GetCellFormula(4, 1) == "=#REF!*10", "B6 (now B5) lost its cell [" + book->GetCellFormula(4, 1) + "]");
        Check(book->GetCellFormula(0, 1) == "=SUM(A1:A4)", "the SUM shrank");
        Check(DataFormula(*book) == "=Sheet1.A3+1", "Data follows A4 to A3");
        book->Undo();
        CheckOriginal(*book, "undone");
        book->Redo();
        Check(book->GetCellFormula(4, 1) == "=#REF!*10" && book->GetCellNumber(2, 0) == 4.0, "redo deletes it again");
        book->Undo();
        CheckOriginal(*book, "undone again");
    }

    std::printf("\n-- columns --\n");
    {
        auto book = MakeBook();
        book->InsertColumnsAt(0, 1);
        Check(book->GetCellFormula(0, 2) == "=SUM(B1:B5)", "B1 moved to C1 and reads B [" + book->GetCellFormula(0, 2) + "]");
        book->Undo();
        CheckOriginal(*book, "insert undone");
        book->DeleteColumnsAt(0, 1);
        Check(book->GetCellFormula(0, 0) == "=SUM(#REF!)", "deleting column A [" + book->GetCellFormula(0, 0) + "]");
        book->Undo();
        CheckOriginal(*book, "delete undone");
    }

    std::printf("\n-- a run of steps --\n");
    {
        auto book = MakeBook();
        book->InsertRowsAt(0, 1);
        book->SetCellValue(0, 0, 7.0);
        book->DeleteRowsAt(3, 2);
        book->InsertColumnsAt(1, 1);
        for (int i = 0; i < 4; ++i) book->Undo();
        CheckOriginal(*book, "four undone");
        for (int i = 0; i < 4; ++i) book->Redo();
        // B1's SUM(A1:A5): a row inserted above (SUM(A2:A6) in B2), A4:A5
        // deleted (SUM(A2:A4)), a column inserted before B (now in C2).
        // A2:A4 then hold 1, 2 and 5.
        Check(book->GetCellFormula(1, 2) == "=SUM(A2:A4)" && book->GetCellNumber(1, 2) == 8.0,
              "redone: the SUM is in C2 as =SUM(A2:A4) = 8 [" + book->GetCellFormula(1, 2) + " = " +
              book->GetCellDisplayValue(1, 2) + "]");
        for (int i = 0; i < 4; ++i) book->Undo();
        CheckOriginal(*book, "redone and undone again");
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
