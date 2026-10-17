// Tests/SpreadsheetPointModeTest.cpp
// The spreadsheet grid's editing gestures, driven through its own mouse and
// key handling on an element with no window:
//  - point mode: while a formula is typed, a click on a cell types its
//    address at the caret, a drag a range, Shift+click grows it, Ctrl+click
//    adds another, and a click where no reference fits ends the edit;
//  - Ctrl+D / Ctrl+R and the fill handle's Ctrl (copy) mode;
//  - renaming a sheet: refused names, and formulas following the new name;
//  - undo after a sheet is renamed (it follows) or deleted (its steps go,
//    and nothing lands on the sheet that is open instead);
//  - deleting a sheet: references to it become #REF! in the formula text.
//
// Usage: SpreadsheetPointModeTest
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

UCEvent Mouse(UCEventType type, const Rect2Di& cell, bool shift = false, bool ctrl = false) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.pointer = Point2Di(cell.x + cell.width / 2, cell.y + cell.height / 2);
    e.pointerWindow = e.pointer;
    e.shift = shift;
    e.ctrl = ctrl;
    return e;
}

void Click(UltraCanvasSpreadsheet& grid, int row, int col, bool shift = false, bool ctrl = false) {
    const Rect2Di cell = grid.GetCellBounds(row, col);
    grid.OnEvent(Mouse(UCEventType::MouseDown, cell, shift, ctrl));
    grid.OnEvent(Mouse(UCEventType::MouseUp, cell, shift, ctrl));
}

void Drag(UltraCanvasSpreadsheet& grid, int row, int col, int toRow, int toCol) {
    grid.OnEvent(Mouse(UCEventType::MouseDown, grid.GetCellBounds(row, col)));
    grid.OnEvent(Mouse(UCEventType::MouseMove, grid.GetCellBounds(toRow, toCol)));
    grid.OnEvent(Mouse(UCEventType::MouseUp, grid.GetCellBounds(toRow, toCol)));
}

void Key(UltraCanvasSpreadsheet& grid, UCKeys key, bool ctrl) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.ctrl = ctrl;
    grid.OnEvent(e);
}

// Starts editing C10 (row 9, col 2) with `text`, the caret at its end.
void Type(UltraCanvasSpreadsheet& grid, const std::string& text) {
    grid.SelectCell(9, 2);
    grid.StartInCellEditing();
    grid.SetEditBuffer(text);
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: point mode, fill commands, sheet rename ===\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    for (int r = 1; r <= 7; ++r) grid->SetCellValue(r, 2, 1000.0 * r);   // C2:C8

    std::printf("\n-- a click types the cell, a drag the range --\n");
    Type(*grid, "=SUM(");
    Check(grid->CanPointReference(), "=SUM( takes a reference");
    Click(*grid, 1, 2);
    Check(grid->IsEditing() && grid->GetEditBuffer() == "=SUM(C2", "click on C2 gives =SUM(C2 [" + grid->GetEditBuffer() + "]");
    Click(*grid, 2, 2);
    Check(grid->GetEditBuffer() == "=SUM(C3", "a second click replaces it [" + grid->GetEditBuffer() + "]");
    Drag(*grid, 1, 2, 7, 2);
    Check(grid->GetEditBuffer() == "=SUM(C2:C8", "a drag gives =SUM(C2:C8 [" + grid->GetEditBuffer() + "]");
    Check(grid->GetSelection().start.row == 9 && grid->GetSelection().start.col == 2,
          "the selection stays on the edited cell");
    grid->SetEditBuffer(grid->GetEditBuffer() + ")");
    grid->StopEditing(true);
    Check(grid->GetCellFormula(9, 2) == "=SUM(C2:C8)", "committed as =SUM(C2:C8)");
    Check(grid->GetCellNumber(9, 2) == 28000.0, "and it calculates 28000");

    std::printf("\n-- Shift+click grows, Ctrl+click adds --\n");
    Type(*grid, "=COUNT(");
    Click(*grid, 1, 2);
    Click(*grid, 4, 3, true);
    Check(grid->GetEditBuffer() == "=COUNT(C2:D5", "Shift+click grows to C2:D5 [" + grid->GetEditBuffer() + "]");
    Click(*grid, 6, 2, false, true);
    Check(grid->GetEditBuffer() == "=COUNT(C2:D5,C7", "Ctrl+click adds ,C7 [" + grid->GetEditBuffer() + "]");
    grid->CancelEditing();

    std::printf("\n-- operators take a reference too --\n");
    Type(*grid, "=C2+");
    Click(*grid, 2, 2);
    Check(grid->GetEditBuffer() == "=C2+C3", "=C2+ then C3 [" + grid->GetEditBuffer() + "]");
    grid->CancelEditing();

    std::printf("\n-- where no reference fits, a click ends the edit --\n");
    Type(*grid, "=C2");
    Check(!grid->CanPointReference(), "=C2| takes no reference");
    Click(*grid, 3, 4);
    Check(!grid->IsEditing(), "the edit ended");
    Check(grid->GetCellFormula(9, 2) == "=C2", "and was committed");
    Check(grid->GetActiveCell().row == 3 && grid->GetActiveCell().col == 4, "the clicked cell is selected");
    Type(*grid, "plain text");
    Click(*grid, 3, 4);
    Check(!grid->IsEditing(), "a text cell ends its edit on a click");

    std::printf("\n-- Ctrl+D / Ctrl+R --\n");
    grid->SetCellFormula(0, 5, "=C1*2");            // F1
    grid->Select(CellRange(0, 5, 3, 5));            // F1:F4
    Key(*grid, UCKeys::D, true);
    Check(grid->GetCellFormula(3, 5) == "=C4*2", "Ctrl+D fills =C4*2 into F4 [" + grid->GetCellFormula(3, 5) + "]");
    grid->SetCellValue(0, 6, 5.0);                  // G1
    grid->Select(CellRange(0, 7, 0, 7));            // H1
    Key(*grid, UCKeys::R, true);
    Check(grid->GetCellNumber(0, 7) == 5.0, "Ctrl+R copies G1 into H1");
    grid->Undo();
    Check(grid->GetCellText(0, 7).empty(), "and is one undo step");

    std::printf("\n-- fill handle copy mode --\n");
    grid->SetCellValue(20, 0, 1.0);
    grid->SetCellValue(21, 0, 2.0);
    grid->Select(CellRange(20, 0, 21, 0));
    grid->AutoFillSelection(CellRange(20, 0, 23, 0), AutoFillMode::Copy);
    Check(grid->GetCellNumber(22, 0) == 1.0 && grid->GetCellNumber(23, 0) == 2.0, "copy mode repeats 1, 2");
    grid->SetCellValue(20, 1, 1.0);
    grid->Select(CellRange(20, 1, 20, 1));
    Check(grid->AutoFillSelectionToDataEnd(), "double-click fill finds the data beside it");
    Check(grid->GetSelection().end.row == 23, "and fills down to its last row");

    std::printf("\n-- renaming a sheet --\n");
    grid->AddSheet("Sales");
    const int sales = grid->GetSheetCount() - 1;
    grid->SetCellFormula(0, 0, "=Sales.A1*2");
    Check(!grid->RenameSheet(sales, "Bad/Name"), "a name with / is refused");
    Check(!grid->RenameSheet(sales, grid->GetSheet(0)->GetName()), "another sheet's name is refused");
    Check(grid->ValidateSheetName(sales, "sales").empty(), "its own name in another case is fine");
    Check(grid->RenameSheet(sales, "Sales 2016"), "Sales 2016 is accepted");
    Check(grid->GetCellFormula(0, 0) == "='Sales 2016'.A1*2",
          "formulas follow the new name [" + grid->GetCellFormula(0, 0) + "]");

    std::printf("\n-- undo follows a renamed sheet --\n");
    {
        auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
        book->GetSheet(0)->GetCell(0, 0)->SetNumber(42);      // Sheet1.A1, outside the history
        book->AddSheet("Sales");
        book->SetActiveSheet(1);
        book->SetCellValue(0, 0, 5.0);                        // Sales.A1 = 5 (undoable)
        book->SetCellFormula(1, 0, "=A1*2");
        book->SetActiveSheet(0);
        book->SetCellFormula(0, 1, "=Sales.A1+1");            // Sheet1.B1
        book->SetCellFormula(0, 1, "=Sales.A1+2");
        book->RenameSheet(1, "Revenue");
        book->SetCellValue(0, 0, 77.0);                       // Sheet1.A1, open sheet
        book->Undo();                                         // Sheet1.A1 back to empty
        book->Undo();                                         // Sheet1.B1 back to =...+1
        Check(book->GetCellFormula(0, 1) == "=Revenue.A1+1",
              "an undone formula names the sheet's new name [" + book->GetCellFormula(0, 1) + "]");
        book->Undo();                                         // B1 empty
        book->Undo();                                         // Revenue.A2 formula gone
        book->Undo();                                         // Revenue.A1 back to empty
        auto* revenue = book->GetSheetByName("Revenue");
        const SpreadsheetCell* a1 = revenue ? revenue->GetCellIfExists(0, 0) : nullptr;
        Check(revenue && (!a1 || a1->IsEmpty()), "undo emptied the renamed sheet's A1 again");
        Check(book->GetCellNumber(0, 0) == 42.0, "and left the open sheet's A1 alone");
    }

    std::printf("\n-- deleting a sheet: its undo steps go, references become #REF! --\n");
    {
        auto book = std::make_shared<UltraCanvasSpreadsheet>("book2", 0, 0, 900, 600);
        book->GetSheet(0)->GetCell(4, 4)->SetNumber(1);       // Sheet1.E5, outside the history
        book->AddSheet("Gone");
        book->SetActiveSheet(1);
        book->SetCellValue(4, 4, 99.0);                       // Gone.E5 = 99 (undoable)
        book->SetActiveSheet(0);
        book->SetCellFormula(0, 0, "=Gone.E5*2");
        book->SetCellFormula(1, 0, "=SUM(Gone.A1:B3)+E5");
        book->RemoveSheet(1);
        Check(book->GetCellFormula(0, 0) == "=#REF!*2", "=Gone.E5*2 became =#REF!*2 [" + book->GetCellFormula(0, 0) + "]");
        Check(book->GetCellFormula(1, 0) == "=SUM(#REF!)+E5", "a range became #REF! [" + book->GetCellFormula(1, 0) + "]");
        Check(book->GetCellDisplayValue(0, 0) == "#REF!", "and shows #REF! [" + book->GetCellDisplayValue(0, 0) + "]");
        // Only Sheet1's two formula steps are left to undo; Gone's went with it.
        book->Undo();
        book->Undo();
        Check(!book->CanUndo(), "Gone's undo step was dropped");
        Check(book->GetCellNumber(4, 4) == 1.0, "Sheet1.E5 is untouched");
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
