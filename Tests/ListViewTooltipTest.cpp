// Tests/ListViewTooltipTest.cpp
// Regression test for the tooltip data the ListView reads on hover.
//
// The models always stored a tooltip per item, but nothing read it back
// through ToolTipRole - the view never showed one, so the role had no cover.
// Now that hovering a cell displays it, these are the answers the view depends
// on: a per-column tooltip where a cell has one, the row-wide tooltip
// everywhere else, and the column header's own text from ListColumnDef.
//
// Exercises the real list models with no UI stack (see Tests/CMakeLists.txt).
#include "UltraCanvasListModel.h"

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

// Tooltip text for a cell, exactly as UltraCanvasListView::GetTooltipTextAt
// reads it out of the model.
static std::string TooltipAt(const IListModel& model, int row, int column) {
    return GetStringValue(model.GetData(ListIndex{row, column}, ListDataRole::ToolTipRole));
}

// ---------------------------------------------------------------------------
// Single-column model
// ---------------------------------------------------------------------------
static void TestSimpleModelTooltips() {
    UltraCanvasSimpleListModel model;
    model.AddItem(ListItem("Apple", "", "A common red fruit"));
    model.AddItem(ListItem("Banana", "", "A yellow tropical fruit"));
    model.AddItem("Cherry");  // no tooltip

    CHECK_EQ(TooltipAt(model, 0, 0), std::string("A common red fruit"));
    CHECK_EQ(TooltipAt(model, 1, 0), std::string("A yellow tropical fruit"));
    CHECK_EQ(TooltipAt(model, 2, 0), std::string(""));

    // Out-of-range rows return nothing rather than reading past the items.
    CHECK_EQ(TooltipAt(model, -1, 0), std::string(""));
    CHECK_EQ(TooltipAt(model, 3, 0), std::string(""));

    // Writing through the role is what an editing view would do.
    model.SetData(ListIndex{2, 0}, ListDataRole::ToolTipRole, std::string("Small red stone fruit"));
    CHECK_EQ(TooltipAt(model, 2, 0), std::string("Small red stone fruit"));
}

// ---------------------------------------------------------------------------
// Multi-column model: per-cell tooltips over a row-wide fallback
// ---------------------------------------------------------------------------
static void TestMultiColumnCellTooltips() {
    UltraCanvasMultiColumnListModel model;
    model.AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left,
                                  "Name of the file on disk"));
    model.AddColumn(ListColumnDef("Type", 90, TextAlignment::Left,
                                  "File type, derived from the extension"));
    model.AddColumn(ListColumnDef("Size", 70, TextAlignment::Right));

    MultiColumnListItem item({"main.cpp", "C++ Source", "2.4 KB"});
    item.tooltip = "src/main.cpp";
    item.SetCellTooltip(2, "2,458 bytes");
    model.AddItem(item);

    MultiColumnListItem plain({"utils.h", "C++ Header", "1.1 KB"});
    model.AddItem(plain);  // no tooltips at all

    // The column that has its own text uses it; the others fall back to the row.
    CHECK_EQ(TooltipAt(model, 0, 2), std::string("2,458 bytes"));
    CHECK_EQ(TooltipAt(model, 0, 0), std::string("src/main.cpp"));
    CHECK_EQ(TooltipAt(model, 0, 1), std::string("src/main.cpp"));

    // An item with neither has no tooltip on any column.
    CHECK_EQ(TooltipAt(model, 1, 0), std::string(""));
    CHECK_EQ(TooltipAt(model, 1, 2), std::string(""));

    // A column index outside the model is not a cell.
    CHECK_EQ(TooltipAt(model, 0, 3), std::string(""));
    CHECK_EQ(TooltipAt(model, 0, -1), std::string(""));

    // An empty per-cell entry keeps the row fallback rather than blanking it.
    MultiColumnListItem blanked({"README.md", "Markdown", "3.8 KB"});
    blanked.tooltip = "README.md";
    blanked.SetCellTooltip(1, "");
    model.AddItem(blanked);
    CHECK_EQ(TooltipAt(model, 2, 1), std::string("README.md"));

    // SetData writes the tooltip of that one cell, leaving the rest on the row.
    model.SetData(ListIndex{2, 1}, ListDataRole::ToolTipRole, std::string("Markdown document"));
    CHECK_EQ(TooltipAt(model, 2, 1), std::string("Markdown document"));
    CHECK_EQ(TooltipAt(model, 2, 0), std::string("README.md"));
}

// ---------------------------------------------------------------------------
// Column headers
// ---------------------------------------------------------------------------
static void TestHeaderTooltips() {
    UltraCanvasMultiColumnListModel model;
    model.AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left,
                                  "Name of the file on disk"));
    model.AddColumn(ListColumnDef("Size", 70, TextAlignment::Right));

    CHECK_EQ(model.GetColumnDef(0).tooltip, std::string("Name of the file on disk"));
    CHECK_EQ(model.GetColumnDef(1).tooltip, std::string(""));
    // Widths and alignment still come through the 4-argument constructor.
    CHECK_EQ(model.GetColumnDef(0).width, 170);
    CHECK_EQ(model.GetColumnDef(0).alignment == TextAlignment::Left, true);
    // An unknown column is a default definition, not an out-of-range read.
    CHECK_EQ(model.GetColumnDef(5).tooltip, std::string(""));
}

int main() {
    std::cout << "ListViewTooltipTest\n";
    TestSimpleModelTooltips();
    TestMultiColumnCellTooltips();
    TestHeaderTooltips();

    if (failures == 0) {
        std::cout << "  all checks passed\n";
        return 0;
    }
    std::cerr << "  " << failures << " check(s) failed\n";
    return 1;
}
