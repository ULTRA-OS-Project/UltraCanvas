// Tests/SpreadsheetSortMenuTest.cpp
// The grid's context menu offers Sort, with a submenu: ascending and
// descending by the active cell's column, and "Ascending by" / "Descending by"
// submenus listing every column of the selection. Each entry sorts only the
// selected rows, keeping every row of the block together. For a selection the
// header sort buttons would not sort either - one row, a whole column - the
// submenu holds one disabled hint instead.
//
// The menu is built as the right-click builds it (BuildSpreadsheetFormatMenuItems)
// and its entries are run as a click runs them.
//
// Usage: SpreadsheetSortMenuTest
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

//      A        B     C
//  0   Pear     3     note-0
//  1   Apple    1     note-1
//  2   Plum     4     note-2
//  3   Fig      2     note-3
std::shared_ptr<UltraCanvasSpreadsheet> Seed() {
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 600, 400);
    const char* fruit[] = { "Pear", "Apple", "Plum", "Fig" };
    const double qty[] = { 3, 1, 4, 2 };
    for (int r = 0; r < 4; ++r) {
        grid->SetCellValue(r, 0, std::string(fruit[r]));
        grid->SetCellValue(r, 1, qty[r]);
        grid->SetCellValue(r, 2, "note-" + std::to_string(r));
    }
    return grid;
}

std::string ColumnA(UltraCanvasSpreadsheet& grid) {
    std::string s;
    for (int r = 0; r < 4; ++r) s += (r ? "," : "") + grid.GetCell(r, 0)->GetDisplayValue();
    return s;
}

} // namespace

int main() {
    std::printf("=== Spreadsheet context menu: Sort ===\n");

    std::printf("\n-- the menu has a Sort submenu --\n");
    {
        auto grid = Seed();
        grid->Select(CellRange(0, 0, 3, 1));   // A1:B4, active cell A1
        auto items = BuildSpreadsheetFormatMenuItems(grid.get());
        const MenuItemData* sort = Find(items, "Sort");
        Check(sort && sort->type == MenuItemType::Submenu, "\"Sort\" is a submenu of the context menu");
        if (!sort) return failures;

        const MenuItemData* asc = Find(sort->subItems, "Ascending by column A");
        const MenuItemData* desc = Find(sort->subItems, "Descending by column A");
        Check(asc && desc, "it sorts by the active cell's column, both ways");
        const MenuItemData* byCols = nullptr;
        for (const auto& item : sort->subItems) {
            if (item.label == "Ascending by" && item.type == MenuItemType::Submenu) byCols = &item;
        }
        Check(byCols && byCols->subItems.size() == 2, "\"Ascending by\" lists the selection's two columns");

        if (asc && asc->onClick) asc->onClick();
        Check(ColumnA(*grid) == "Apple,Fig,Pear,Plum", "Ascending by column A sorts the names: " + ColumnA(*grid));
        Check(grid->GetCell(0, 1)->GetDisplayValue() == "1", "and each row's B moves with it");
        Check(grid->GetCell(0, 2)->GetDisplayValue() == "note-0", "column C, outside the selection, stays");

        if (byCols && byCols->subItems.size() == 2) {
            // Rebuild, as the next right-click does, and sort descending by B.
            items = BuildSpreadsheetFormatMenuItems(grid.get());
            sort = Find(items, "Sort");
            const MenuItemData* descBy = nullptr;
            for (const auto& item : sort->subItems) {
                if (item.label == "Descending by" && item.type == MenuItemType::Submenu) descBy = &item;
            }
            Check(descBy && descBy->subItems.size() == 2 && descBy->subItems[1].label == "Column B",
                  "\"Descending by\" offers Column B");
            if (descBy && descBy->subItems.size() == 2 && descBy->subItems[1].onClick) {
                descBy->subItems[1].onClick();
                Check(ColumnA(*grid) == "Plum,Pear,Fig,Apple",
                      "Descending by column B orders the rows 4,3,2,1: " + ColumnA(*grid));
            }
        }
    }

    std::printf("\n-- one row selected --\n");
    {
        auto grid = Seed();
        grid->SelectCell(1, 0);
        auto items = BuildSpreadsheetFormatMenuItems(grid.get());
        const MenuItemData* sort = Find(items, "Sort");
        Check(sort && sort->subItems.size() == 1 && !sort->subItems[0].enabled,
              "the Sort submenu holds one disabled hint");
    }

    std::printf("\n-- a whole column selected --\n");
    {
        auto grid = Seed();
        grid->SelectColumn(0);
        auto items = BuildSpreadsheetFormatMenuItems(grid.get());
        const MenuItemData* sort = Find(items, "Sort");
        Check(sort && sort->subItems.size() == 1 && !sort->subItems[0].enabled,
              "no sort for a whole column (it would take the title row along)");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
