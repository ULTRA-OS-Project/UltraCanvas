// include/UltraCanvasSpreadsheetPivotPanel.h
// A pivot table's field list as a panel (UltraCanvasSpreadsheetPivot.h): the
// source's fields, and the pivot's rows, columns, values and filters - a
// field is added to one with a button or a double-click (numbers go to the
// values, anything else to the rows), removed and moved up or down there.
// For the field picked in an area: its summary and show-as (a value
// field), its order, grouping (by years, quarters, months, or number
// ranges of a step), subtotals and the items it shows (a row, column or
// filter field); for the pivot: the grand totals and whether several value
// fields go one under the other. Every change is reported with the pivot
// as it then is.
//
// ShowSpreadsheetPivotDialog puts it in a dialog; an application docks it
// beside the grid and shows it while a pivot's cell is active, applying
// each change (UltraCanvasSpreadsheet::SetPivotTable) as it comes.
//
// doc-check: std::shared_ptr<UltraCanvasSpreadsheet> grid; int sheetIndex; int pivotIndex;
// doc-check: example
//     auto panel = std::make_shared<UltraCanvasSpreadsheetPivotPanel>("PivotFields", 0, 0, 320, 700);
//     const SpreadsheetPivotTable& pivot = grid->GetActiveSheet()->GetPivotTables()[0];
//     panel->SetPivot(pivot, grid->GetSheetByName(pivot.sourceSheet));
//     panel->onPivotChanged = [g = grid.get(), sheetIndex, pivotIndex](const SpreadsheetPivotTable& changed) {
//         g->SetPivotTable(sheetIndex, pivotIndex, changed);
//     };
//
// Version: 1.0.1 - one above the other (not `wide`), the add buttons go two by two: a narrow panel
//                  shows them whole
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpreadsheetPivot.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasButton;
class UltraCanvasCheckbox;
class UltraCanvasDropdown;
class UltraCanvasLabel;
class UltraCanvasListView;
class UltraCanvasSimpleListModel;
class UltraCanvasTextInput;

class UltraCanvasSpreadsheetPivotPanel : public UltraCanvasContainer {
public:
    // `wide`: the field list and the areas side by side (a dialog); else
    // one above the other (a panel beside the grid), scrolling.
    UltraCanvasSpreadsheetPivotPanel(const std::string& identifier, float x = 0, float y = 0, float w = 320,
                                     float h = 700, bool wide = false);

    // The pivot to edit, and the sheet its source stands on (held raw: it
    // must outlive the panel's use of it - set it again when it goes).
    void SetPivot(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet* source);
    const SpreadsheetPivotTable& GetPivot() const { return pivot_; }
    // The source sheet changed (or the pivot's source block): the fields
    // are read again, the pivot's kept where it fits.
    void SetSource(const SpreadsheetSheet* source, const CellRange& range, const std::string& sheetName);

    // A field, summary or option changed: the pivot as it is now.
    std::function<void(const SpreadsheetPivotTable&)> onPivotChanged;

    // The areas of a pivot, as the panel numbers them.
    enum class Area { Rows, Columns, Values, Filters };
    // Adds field `column` of the source to `area` (what the buttons do).
    void AddField(int column, Area area);
    // The field picked in an area (`index` in its list); -1 picks none.
    void PickField(Area area, int index);

private:
    SpreadsheetPivotTable pivot_;
    const SpreadsheetSheet* source_ = nullptr;
    Area pickedArea_ = Area::Rows;
    int pickedIndex_ = -1;
    bool updating_ = false;

    std::shared_ptr<UltraCanvasSimpleListModel> fieldsModel_;
    UltraCanvasListView* fields_ = nullptr;
    std::shared_ptr<UltraCanvasSimpleListModel> areaModels_[4];
    UltraCanvasListView* areas_[4] = {nullptr, nullptr, nullptr, nullptr};
    UltraCanvasLabel* pickedLabel_ = nullptr;
    UltraCanvasDropdown* summary_ = nullptr;
    UltraCanvasDropdown* showAs_ = nullptr;
    UltraCanvasDropdown* order_ = nullptr;
    UltraCanvasDropdown* grouping_ = nullptr;
    UltraCanvasTextInput* step_ = nullptr;
    UltraCanvasCheckbox* subtotals_ = nullptr;
    UltraCanvasContainer* items_ = nullptr;
    UltraCanvasCheckbox* grandRow_ = nullptr;
    UltraCanvasCheckbox* grandColumn_ = nullptr;
    UltraCanvasCheckbox* valuesInRows_ = nullptr;

    void Build(bool wide);
    void Refresh();             // every list and control from pivot_
    void RefreshPicked();       // the picked field's controls and items
    void Changed();             // pivot_ changed: Refresh, onPivotChanged
    PivotField* PickedAxisField();
    PivotValueField* PickedValueField();
    std::vector<PivotField>* AxisList(Area area);
    int AreaCount(Area area) const;
    void RemovePicked();
    void MovePicked(int by);
    int SelectedSourceField() const;
};

} // namespace UltraCanvas
