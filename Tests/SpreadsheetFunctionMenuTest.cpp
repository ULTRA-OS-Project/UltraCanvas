// Tests/SpreadsheetFunctionMenuTest.cpp
// The "fx" button at the end of the formula bar opens a menu of every
// function the formula engine knows: a submenu per category and one with all
// of them A to Z, each entry showing the name and its description. Picking one
// inserts NAME() at the caret of the formula being edited, or starts editing
// the active cell: a formula is wrapped (=A1/B1 becomes =ROUND(A1/B1)), any
// other cell becomes =NAME().
//
// The menu is built as the button builds it (BuildSpreadsheetFunctionMenuItems)
// and its entries are run as a click runs them.
//
// Usage: SpreadsheetFunctionMenuTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetFormatMenu.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

const MenuItemData* Find(const std::vector<MenuItemData>& items, const std::string& prefix) {
    for (const auto& item : items) {
        if (item.label.rfind(prefix, 0) == 0) return &item;
    }
    return nullptr;
}

} // namespace

int main() {
    std::printf("=== Spreadsheet formula bar: function menu ===\n");

    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 600, 400);
    const auto names = grid->GetFormulaEngine()->GetFunctionLibrary().GetFunctionNames();

    std::printf("\n-- the menu lists every function --\n");
    auto items = BuildSpreadsheetFunctionMenuItems(grid.get());
    const MenuItemData* all = Find(items, "All functions A to Z");
    Check(all && all->subItems.size() == names.size(),
          "\"All functions A to Z\" holds all " + std::to_string(names.size()) + " functions");
    size_t inCategories = 0;
    for (const auto& item : items) {
        if (item.type == MenuItemType::Submenu && item.label.rfind("All functions", 0) != 0) {
            inCategories += item.subItems.size();
        }
    }
    Check(inCategories == names.size(), "and each one sits in exactly one category submenu");
    const MenuItemData* math = Find(items, "Math");
    const MenuItemData* sum = math ? Find(math->subItems, "SUM") : nullptr;
    Check(sum && !sum->shortcut.empty() && sum->tooltip == sum->shortcut,
          "SUM is under Math, with its description beside it and as its tooltip");
    if (all && all->subItems.size() > 1) {
        Check(all->subItems[0].label < all->subItems[1].label, "the A to Z list is sorted");
    }
    for (const auto& item : items) {
        if (item.type == MenuItemType::Submenu) std::printf("    %s\n", item.label.c_str());
    }

    std::printf("\n-- picking a function --\n");
    {
        grid->SelectCell(0, 0);                // an empty cell
        if (sum && sum->onClick) sum->onClick();
        Check(grid->IsEditing() && grid->GetEditBuffer() == "=SUM()",
              "an empty cell becomes =SUM(): " + grid->GetEditBuffer());
        grid->CancelEditing();
    }
    {
        grid->SetCellValue(1, 0, std::string("text"));
        grid->SelectCell(1, 0);
        grid->InsertFunction("AVERAGE");
        Check(grid->GetEditBuffer() == "=AVERAGE()", "a text cell becomes =AVERAGE(): " + grid->GetEditBuffer());
        grid->CancelEditing();
    }
    {
        grid->GetCell(2, 0)->SetFormula("=A1/B1");
        grid->SelectCell(2, 0);
        grid->InsertFunction("ROUND");
        Check(grid->GetEditBuffer() == "=ROUND(A1/B1)", "a formula is wrapped: " + grid->GetEditBuffer());
        // The caret sits before the closing parenthesis: the next insert
        // lands there, as typing ", 2" would.
        grid->InsertFunction("PI");
        Check(grid->GetEditBuffer() == "=ROUND(A1/B1PI())",
              "the caret was before the closing parenthesis: " + grid->GetEditBuffer());
        grid->CancelEditing();
    }
    {
        // While editing, the call goes in at the caret.
        grid->SelectCell(3, 0);
        grid->StartEditing();
        grid->InsertFunction("MAX");
        grid->InsertFunction("MIN");
        Check(grid->GetEditBuffer() == "=MAX(MIN())",
              "editing, the call goes in at the caret, between the last parentheses: " + grid->GetEditBuffer());
        grid->CancelEditing();
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
