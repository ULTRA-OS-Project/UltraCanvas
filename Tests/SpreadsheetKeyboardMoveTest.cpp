// Tests/SpreadsheetKeyboardMoveTest.cpp
// A keyboard move in the spreadsheet grid selects the cell it lands on:
// arrows, Enter, Tab, Home, End, Ctrl+Home, Ctrl+End and Ctrl+arrow. Until
// 2026-11 they moved only the active cell and left the old range selected,
// so Bold or a number format went to the cell the caret had left.
// Shift+arrow still extends the range from where it started.
//
// Usage: SpreadsheetKeyboardMoveTest
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

void Press(UltraCanvasSpreadsheet& grid, UCKeys key, bool ctrl = false, bool shift = false) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.ctrl = ctrl;
    event.shift = shift;
    grid.OnEvent(event);
}

// The selection is exactly the one cell, and it is the active cell.
void CheckOn(UltraCanvasSpreadsheet& grid, const std::string& cell, const std::string& what) {
    const std::string selection = grid.GetSelection().ToString();
    const std::string active = grid.GetActiveCell().ToString();
    Check(selection == cell && active == cell, what + " [selection " + selection + ", active " + active + "]");
}

} // namespace

int main() {
    std::printf("=== Spreadsheet grid: keyboard moves select the cell ===\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) grid->SetCellValue(r, c, std::to_string(r * 10 + c));   // A1:C4

    int notified = 0;
    grid->onSelectionChange = [&notified]() { ++notified; };

    grid->SelectCell(1, 1);                                  // B2
    notified = 0;
    Press(*grid, UCKeys::Down);
    CheckOn(*grid, "B3", "Down");
    Check(notified > 0, "and onSelectionChange fired");
    Press(*grid, UCKeys::Right);
    CheckOn(*grid, "C3", "Right");
    Press(*grid, UCKeys::Up);
    CheckOn(*grid, "C2", "Up");
    Press(*grid, UCKeys::Left);
    CheckOn(*grid, "B2", "Left");
    Press(*grid, UCKeys::Enter);
    CheckOn(*grid, "B3", "Enter");
    Press(*grid, UCKeys::Tab);
    CheckOn(*grid, "C3", "Tab");
    Press(*grid, UCKeys::Home);
    CheckOn(*grid, "A3", "Home");
    Press(*grid, UCKeys::End, true);
    CheckOn(*grid, "C4", "Ctrl+End");
    Press(*grid, UCKeys::Home, true);
    CheckOn(*grid, "A1", "Ctrl+Home");
    Press(*grid, UCKeys::Down, true);
    CheckOn(*grid, "A4", "Ctrl+Down");

    std::printf("\n-- a range collapses to the cell moved to --\n");
    grid->Select(CellRange(0, 0, 2, 2));                     // A1:C3
    Press(*grid, UCKeys::Down);
    Check(grid->GetSelection().RowCount() == 1 && grid->GetSelection().ColCount() == 1,
          "Down after A1:C3 leaves one cell selected [" + grid->GetSelection().ToString() + "]");

    std::printf("\n-- Shift+arrow still extends --\n");
    grid->SelectCell(0, 0);
    Press(*grid, UCKeys::Down, false, true);
    Press(*grid, UCKeys::Right, false, true);
    Check(grid->GetSelection().ToString() == "A1:B2", "Shift+Down, Shift+Right from A1 select A1:B2 [" +
                                                          grid->GetSelection().ToString() + "]");
    Check(grid->GetActiveCell().ToString() == "A1", "with A1 still the active cell");
    Press(*grid, UCKeys::Up, false, true);
    Check(grid->GetSelection().ToString() == "A1:B1", "Shift+Up shrinks it to A1:B1 [" + grid->GetSelection().ToString() + "]");
    Press(*grid, UCKeys::Up, false, true);
    Check(grid->GetSelection().ToString() == "A1:B1", "and at the top edge it stays [" + grid->GetSelection().ToString() + "]");
    grid->SelectCell(2, 2);                                  // C3
    Press(*grid, UCKeys::Left, false, true);
    Press(*grid, UCKeys::Up, false, true);
    Check(grid->GetSelection().ToString() == "B2:C3" && grid->GetActiveCell().ToString() == "C3",
          "Shift+Left, Shift+Up from C3 select B2:C3, C3 active [" + grid->GetSelection().ToString() + "]");

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
