// Tests/SpreadsheetValidationGridTest.cpp
// The spreadsheet grid enforcing data validation on what is typed, driven on
// an element with no window (so onValidationFailed answers, not a dialog):
//  - a Stop rule: Cancel puts the old value back and takes the entry's undo
//    step with it; Retry opens the entry again on its cell, also after Tab,
//    which commits and then moves on; Accept counts as Cancel;
//  - a Warning rule: Accept keeps the entry as one undo step;
//  - an answer given later (a dialog) acts when it comes;
//  - a rule with showErrorAlert off keeps whatever is typed;
//  - a list rule's items, and picking one;
//  - a typed plain value is an undo step and fires onCellChange (it was
//    neither until 2026-11).
//
// Usage: SpreadsheetValidationGridTest
// Exit code is the number of failed checks.

#include "UltraCanvasSpreadsheet.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

using Answer = UltraCanvasSpreadsheet::ValidationAnswer;

// Types `text` into (row, col) and commits it, as Enter does.
void TypeInto(UltraCanvasSpreadsheet& grid, int row, int col, const std::string& text) {
    grid.SelectCell(row, col);
    grid.StartInCellEditing();
    grid.SetEditBuffer(text);
    grid.StopEditing(true);
}

DataValidation WholeNumber(int low, int high, ValidationAlertStyle style) {
    DataValidation rule;
    rule.type = ValidationType::WholeNumber;
    rule.op = ValidationOperator::Between;
    rule.formula1 = std::to_string(low);
    rule.formula2 = std::to_string(high);
    rule.alertStyle = style;
    rule.errorMessage = "Enter a whole number from " + std::to_string(low) + " to " + std::to_string(high) + ".";
    return rule;
}

} // namespace

int main() {
    std::printf("=== Spreadsheet grid: data validation on typed entries ===\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    auto* sheet = grid->GetActiveSheet();

    int asked = 0;
    Answer reply = Answer::Cancel;
    UltraCanvasSpreadsheet::ValidationFailure last;
    grid->onValidationFailed = [&](const UltraCanvasSpreadsheet::ValidationFailure& failure,
                                   std::function<void(Answer)> answer) {
        ++asked;
        last = failure;
        answer(reply);
    };

    std::printf("\n-- a typed value is an undo step --\n");
    int changes = 0;
    grid->onCellChange = [&changes](int, int) { ++changes; };
    TypeInto(*grid, 0, 1, "4");                             // B1
    Check(grid->GetCellNumber(0, 1) == 4.0, "B1 holds 4");
    Check(changes > 0, "onCellChange fired for the typed value");
    Check(!grid->GetUndoDescription().empty(), "and it is an undo step");
    grid->Undo();
    Check(grid->GetCellText(0, 1).empty(), "Undo takes the typed value back");

    std::printf("\n-- Stop: Cancel puts the old value back --\n");
    sheet->SetValidation(CellRange(0, 0, 9, 0), WholeNumber(1, 10, ValidationAlertStyle::Stop));   // A1:A10
    grid->SetCellValue(0, 0, 5.0);
    const std::string undoBefore = grid->GetUndoDescription();
    reply = Answer::Cancel;
    TypeInto(*grid, 0, 0, "42");
    Check(asked == 1, "the refused entry was asked about");
    Check(last.enteredText == "42" && last.row == 0 && last.col == 0, "with the entry and its cell");
    Check(last.message == "Enter a whole number from 1 to 10.", "and the rule's message [" + last.message + "]");
    Check(grid->GetCellNumber(0, 0) == 5.0, "A1 is 5 again [" + grid->GetCellDisplayValue(0, 0) + "]");
    Check(grid->GetUndoDescription() == undoBefore, "and the entry left no undo step");
    Check(!grid->IsEditing(), "the edit is over");

    std::printf("\n-- a valid entry is not asked about --\n");
    TypeInto(*grid, 0, 0, "7");
    Check(asked == 1 && grid->GetCellNumber(0, 0) == 7.0, "7 is kept without a question");
    TypeInto(*grid, 1, 0, "");
    Check(asked == 1, "an empty entry passes (blanks allowed)");

    std::printf("\n-- Stop: Retry edits the entry again --\n");
    reply = Answer::Retry;
    TypeInto(*grid, 0, 0, "abc");
    Check(asked == 2, "text in a whole-number cell is refused");
    Check(grid->IsEditing(), "Retry opens the editor again");
    Check(grid->GetEditBuffer() == "abc", "with the entry in it [" + grid->GetEditBuffer() + "]");
    Check(grid->GetActiveCell().row == 0 && grid->GetActiveCell().col == 0, "on A1");
    Check(grid->GetCellNumber(0, 0) == 7.0, "while A1 keeps 7");
    grid->SetEditBuffer("3");
    grid->StopEditing(true);
    Check(asked == 2 && grid->GetCellNumber(0, 0) == 3.0, "a corrected entry is kept");

    std::printf("\n-- Retry after Tab stays on the cell --\n");
    grid->SelectCell(2, 0);                              // A3
    grid->StartInCellEditing();
    grid->SetEditBuffer("99");
    UCEvent tab;
    tab.type = UCEventType::KeyDown;
    tab.virtualKey = UCKeys::Tab;
    const int askedBeforeTab = asked;
    grid->OnEvent(tab);
    Check(asked == askedBeforeTab + 1, "Tab committed the entry and it was asked about");
    Check(grid->IsEditing() && grid->GetEditBuffer() == "99", "Tab with a refused entry: editing it again");
    Check(grid->GetActiveCell().row == 2 && grid->GetActiveCell().col == 0,
          "on A3, not the cell Tab moved to [" + grid->GetActiveCell().ToString() + "]");
    grid->CancelEditing();

    std::printf("\n-- Stop: Accept counts as Cancel --\n");
    reply = Answer::Accept;
    TypeInto(*grid, 0, 0, "11");
    Check(grid->GetCellNumber(0, 0) == 3.0, "a Stop rule never keeps 11");

    std::printf("\n-- Warning: Accept keeps the entry --\n");
    sheet->SetValidation(CellRange(0, 4, 0, 4), WholeNumber(1, 10, ValidationAlertStyle::Warning));   // E1
    const int askedBeforeWarning = asked;
    TypeInto(*grid, 0, 4, "50");
    Check(asked == askedBeforeWarning + 1, "the warning was asked");
    Check(grid->GetCellNumber(0, 4) == 50.0, "50 is kept after it [" + grid->GetCellDisplayValue(0, 4) + "]");
    grid->Undo();
    Check(grid->GetCellText(0, 4).empty(), "as one undo step");

    std::printf("\n-- an answer that comes later --\n");
    std::function<void(Answer)> pending;
    grid->onValidationFailed = [&](const UltraCanvasSpreadsheet::ValidationFailure&, std::function<void(Answer)> answer) {
        pending = answer;
    };
    TypeInto(*grid, 0, 0, "77");
    Check(static_cast<bool>(pending), "the question is still open");
    Check(grid->GetCellNumber(0, 0) == 77.0, "the entry stands until it is answered");
    pending(Answer::Cancel);
    Check(grid->GetCellNumber(0, 0) == 3.0, "Cancel puts 3 back");
    pending(Answer::Accept);
    Check(grid->GetCellNumber(0, 0) == 3.0, "a second answer is ignored");

    std::printf("\n-- no alert: anything is kept --\n");
    grid->onValidationFailed = [&](const UltraCanvasSpreadsheet::ValidationFailure&, std::function<void(Answer)> answer) {
        ++asked;
        answer(Answer::Cancel);
    };
    DataValidation quiet = WholeNumber(1, 10, ValidationAlertStyle::Stop);
    quiet.showErrorAlert = false;
    sheet->SetValidation(CellRange(0, 5, 0, 5), quiet);  // F1
    const int askedBefore = asked;
    TypeInto(*grid, 0, 5, "500");
    Check(asked == askedBefore && grid->GetCellNumber(0, 5) == 500.0, "500 is kept without a question");

    std::printf("\n-- a list rule --\n");
    DataValidation colours;
    colours.type = ValidationType::List;
    colours.formula1 = "Red, Green, Blue";
    sheet->SetValidation(CellRange(0, 3, 0, 3), colours);   // D1
    const auto items = grid->GetValidationListItems(0, 3);
    Check(items.size() == 3 && items[1] == "Green", "D1 offers Red, Green, Blue");
    Check(grid->GetValidationListItems(0, 6).empty(), "G1 offers nothing");
    Check(grid->ChooseValidationListItem(0, 3, "Green") && grid->GetCellText(0, 3) == "Green", "picking Green enters it");
    Check(!grid->ChooseValidationListItem(0, 3, "Purple") && grid->GetCellText(0, 3) == "Green", "Purple is not offered");
    grid->Undo();
    Check(grid->GetCellText(0, 3).empty(), "picking is one undo step");
    TypeInto(*grid, 0, 3, "Purple");
    Check(grid->GetCellText(0, 3).empty(), "typing Purple is refused");
    colours.showDropdown = false;
    sheet->SetValidation(CellRange(0, 3, 0, 3), colours);
    Check(grid->GetValidationListItems(0, 3).empty(), "without showDropdown there is no list");

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
