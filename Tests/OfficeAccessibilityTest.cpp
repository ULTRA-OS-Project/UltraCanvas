// Tests/OfficeAccessibilityTest.cpp
// What the spreadsheet grid and the slide editor tell assistive technology
// (UltraCanvasAccessibility.h), the way the platform bridges ask it:
//  - the grid is a table named by its sheet; its rows and cells are parts
//    (IAccessibleTable, IAccessibleParts), a cell named by its address and
//    the value it shows; the active cell is the focused part and the value
//    text; a move of it is announced with the cell as the event's part, a
//    change of sheet renames the table;
//  - the slide editor is a canvas named by the slide shown, whose parts are
//    the strip (a list of the slides) and the slide's shapes, named by their
//    text's first words or else their kind; the selected shape is focused,
//    and a change of selection or slide is announced.
//
// Runs headless: only the elements' answers are checked, no window or bridge.
// Usage: OfficeAccessibilityTest
// Exit code is the number of failed checks.

#include "UltraCanvasAccessibility.h"
#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasSpreadsheet.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

struct Heard {
    AccessibilityEventType type;
    UltraCanvasUIElement* element;
    std::vector<int> part;
};

} // namespace

int main() {
    std::printf("=== Spreadsheet grid and slide editor to assistive technology ===\n");
    std::vector<Heard> events;
    const int listener = UltraCanvasAccessibility::AddListener([&](const AccessibilityEvent& e) {
        events.push_back({e.type, e.element, e.part});
    });
    auto announced = [&](AccessibilityEventType type, UltraCanvasUIElement* element, const std::vector<int>* part = nullptr) {
        for (const Heard& h : events)
            if (h.type == type && h.element == element && (!part || h.part == *part)) return true;
        return false;
    };

    // ----- the grid -----
    std::printf("\n-- the spreadsheet grid --\n");
    {
        auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
        grid->SetCellValue(0, 0, std::string("Item"));
        grid->SetCellValue(0, 1, std::string("Price"));
        grid->SetCellValue(2, 1, 42.0);
        grid->SetCellFormula(3, 1, "=B3*2");
        Check(grid->GetAccessibleRole() == AccessibleRole::Table, "the grid is a table");
        Check(grid->GetAccessibleName() == grid->GetSheetNames().front(),
              "...named by its sheet [" + grid->GetAccessibleName() + "]");
        IAccessibleTable* table = grid->GetAccessibleTableInterface();
        IAccessibleParts* parts = grid->GetAccessiblePartsInterface();
        Check(table && parts, "...with a table and parts");
        if (!table || !parts) return failures + 1;
        Check(table->GetRowCount() == 4 && table->GetColumnCount() == 2,
              "rows and columns as far as the cells are used [" + std::to_string(table->GetRowCount()) + " x " +
                  std::to_string(table->GetColumnCount()) + "]");
        Check(table->GetColumnHeader(1) == "B" && table->GetRowHeader(2) == "3", "column and row headers");
        const AccessiblePart cell = table->GetCell(2, 1);
        Check(cell.role == AccessibleRole::TableCell && cell.name == "B3 42" && cell.row == 2 && cell.column == 1,
              "a cell is named by its address and value [" + cell.name + "]");
        Check(table->GetCell(1, 0).name == "A2", "an empty cell by its address");
        Check(table->GetCell(3, 1).description == "=B3*2" && table->GetCell(3, 1).name == "B4 84",
              "a formula's value as name, the formula as description [" + table->GetCell(3, 1).name + "]");
        Check(table->GetCell(99, 0).role == AccessibleRole::Unknown, "no cell outside the table");
        Check(parts->GetPartCount({}) == 4 && parts->GetPartCount({0}) == 2, "parts: rows, and each row's cells");
        Check(parts->GetPart({2}).role == AccessibleRole::TableRow && parts->GetPart({2}).name == "3", "a row part");
        Check(parts->GetPart({2, 1}).name == "B3 42", "a cell part is the table's cell");

        grid->SetActiveCell(2, 1);
        int row = -1, column = -1;
        Check(table->GetFocusedCell(row, column) && row == 2 && column == 1, "the active cell is the focused cell");
        Check(parts->GetFocusedPart() == std::vector<int>({2, 1}) && table->GetCell(2, 1).focused,
              "...and the focused part");
        Check(grid->GetAccessibleValueText() == "B3 42", "...and the value text [" + grid->GetAccessibleValueText() + "]");

        events.clear();
        grid->SelectCell(0, 1);
        const std::vector<int> moved = {0, 1};
        Check(announced(AccessibilityEventType::SelectionChanged, grid.get(), &moved),
              "a move of the active cell is announced, with the cell");
        Check(announced(AccessibilityEventType::ValueChanged, grid.get()) && grid->GetAccessibleValueText() == "B1 Price",
              "...and its value text changes");

        events.clear();
        grid->Select(CellRange(0, 0, 1, 1));
        int r0 = 0, c0 = 0, r1 = 0, c1 = 0;
        Check(table->GetSelectedCells(r0, c0, r1, c1) && r0 == 0 && c0 == 0 && r1 == 1 && c1 == 1,
              "the selected block");
        Check(parts->GetSelectedParts().size() == 4 && table->GetCell(1, 1).selected && !table->GetCell(2, 1).selected,
              "the selected cells are selected parts");
        Check(announced(AccessibilityEventType::SelectionChanged, grid.get()), "a new selection is announced");

        grid->AddSheet("Costs");
        events.clear();
        grid->SetActiveSheet(1);
        Check(announced(AccessibilityEventType::NameChanged, grid.get()) && grid->GetAccessibleName() == "Costs",
              "another sheet renames the table");
        grid->SetAccessibleName("Budget");
        Check(grid->GetAccessibleName() == "Budget", "SetAccessibleName replaces the sheet's name");
    }

    // ----- the slide editor -----
    std::printf("\n-- the slide editor --\n");
    {
        auto deck = std::make_shared<PptxPresentation>();
        deck->slideWidth = 12192000;
        deck->slideHeight = 6858000;
        auto edit = CreateSlideEdit("deck", 0, 0, 1400, 800);
        edit->SetDeck(deck);
        UCSlideDeckEditor& editor = edit->Editor();
        editor.NewSlide(-1);
        editor.NewSlide(-1);
        editor.SetCurrentSlide(0);
        editor.SetShapeText(0, "Quarterly report");
        const int box = editor.InsertTextBox(SlideBox{914400, 4 * 914400, 4 * 914400, 914400},
                                             "One two three four five six seven eight nine ten");
        const int table = editor.InsertTable(SlideBox{914400, 914400, 4 * 914400, 914400}, 2, 2);
        editor.ClearSelection();

        Check(edit->GetAccessibleRole() == AccessibleRole::Canvas, "the slide editor is a canvas");
        Check(edit->GetAccessibleName() == "Slide 1 of 2: Quarterly report",
              "...named by the slide shown [" + edit->GetAccessibleName() + "]");
        IAccessibleParts* parts = edit->GetAccessiblePartsInterface();
        Check(parts && parts->GetPartCount({}) == 2, "...with two parts: the strip and the slide");
        if (!parts) return failures + 1;
        Check(parts->GetPart({0}).role == AccessibleRole::List && parts->GetPartCount({0}) == 2,
              "the strip is a list of the slides");
        const AccessiblePart first = parts->GetPart({0, 0});
        Check(first.role == AccessibleRole::ListItem && first.name == "Slide 1 of 2: Quarterly report" && first.selected,
              "a slide in the strip, named by its number and title, the one shown selected [" + first.name + "]");
        const PptxSlide* slide = editor.CurrentSlide();
        Check(parts->GetPartCount({1}) == static_cast<int>(slide->shapes.size()), "the slide's parts are its shapes");
        Check(parts->GetPart({1, 0}).name == "Quarterly report" && parts->GetPart({1, 0}).description == "Title",
              "a placeholder named by its text, its kind as description");
        Check(parts->GetPart({1, 1}).name == "Content", "an empty placeholder named by its kind [" +
                                                           parts->GetPart({1, 1}).name + "]");
        Check(parts->GetPart({1, box}).name == "One two three four five six seven eight...",
              "a text box by its first words [" + parts->GetPart({1, box}).name + "]");
        Check(parts->GetPart({1, table}).role == AccessibleRole::Table, "a table shape is a table");
        const AccessiblePart shapePart = parts->GetPart({1, box});
        Check(shapePart.bounds.width > 0 && shapePart.bounds.height > 0, "a shape has its place on the screen");
        Check(parts->GetFocusedPart().empty(), "nothing selected: no focused part");

        events.clear();
        editor.Select({box});
        const std::vector<int> focused = {1, box};
        Check(parts->GetFocusedPart() == focused && parts->GetPart({1, box}).focused && parts->GetPart({1, box}).selected,
              "the selected shape is the focused part");
        Check(announced(AccessibilityEventType::SelectionChanged, edit.get(), &focused),
              "a change of selection is announced, with the shape");

        events.clear();
        edit->GoToSlide(1);
        Check(announced(AccessibilityEventType::NameChanged, edit.get()) &&
                  edit->GetAccessibleName().rfind("Slide 2 of 2", 0) == 0,
              "another slide shown renames the canvas [" + edit->GetAccessibleName() + "]");
        Check(parts->GetPart({0, 1}).selected && !parts->GetPart({0, 0}).selected, "...and is the selected slide");
    }

    UltraCanvasAccessibility::RemoveListener(listener);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
