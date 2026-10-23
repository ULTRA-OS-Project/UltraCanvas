// core/UltraCanvasSpreadsheetFormatMenu.cpp
// Cell-formatting menu for UltraCanvasSpreadsheet.
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetFormatMenu.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasModalDialog.h"
#include <algorithm>
#include <map>
#include <cmath>
#include <sstream>


namespace UltraCanvas {

namespace {

// Radio groups within the menu. Alignment and number format are independent
// single-choice sets, so they need distinct group ids.
constexpr int kGroupHAlign = 6101;
constexpr int kGroupVAlign = 6102;
constexpr int kGroupNumber = 6103;
constexpr int kGroupFontSize = 6104;

// The palette offered for text and background colour. Deliberately short: a
// menu is for the common choices, and a full picker belongs in a dialog.
struct NamedColor { const char* name; Color color; };

const std::vector<NamedColor>& PaletteColors() {
    static const std::vector<NamedColor> palette = {
        { "Black",       Color(0, 0, 0) },
        { "Dark grey",   Color(89, 89, 89) },
        { "Grey",        Color(166, 166, 166) },
        { "White",       Color(255, 255, 255) },
        { "Red",         Color(192, 0, 0) },
        { "Orange",      Color(237, 125, 49) },
        { "Yellow",      Color(255, 217, 102) },
        { "Green",       Color(112, 173, 71) },
        { "Blue",        Color(47, 117, 181) },
        { "Light blue",  Color(189, 215, 238) },
        { "Purple",      Color(112, 48, 160) },
    };
    return palette;
}

std::vector<MenuItemData> BuildColorItems(UltraCanvasSpreadsheet* sheet, bool background) {
    std::vector<MenuItemData> items;
    for (const auto& entry : PaletteColors()) {
        Color color = entry.color;
        items.push_back(MenuItemData::Action(entry.name, [sheet, color, background]() {
            if (background) sheet->SetSelectionBackgroundColor(color);
            else            sheet->SetSelectionFontColor(color);
        }));
    }
    return items;
}

std::vector<MenuItemData> BuildAlignmentItems(UltraCanvasSpreadsheet* sheet) {
    const HorizontalAlignment h = sheet->GetActiveCellHAlign();
    const VerticalAlignment   v = sheet->GetActiveCellVAlign();

    auto setH = [sheet](HorizontalAlignment target) {
        return [sheet, target]() { sheet->SetSelectionAlignment(target, sheet->GetActiveCellVAlign()); };
    };
    auto setV = [sheet](VerticalAlignment target) {
        return [sheet, target]() { sheet->SetSelectionAlignment(sheet->GetActiveCellHAlign(), target); };
    };

    std::vector<MenuItemData> items;
    items.push_back(MenuItemData::Radio("General", kGroupHAlign, h == HorizontalAlignment::General,
                                        setH(HorizontalAlignment::General)));
    items.push_back(MenuItemData::Radio("Left", kGroupHAlign, h == HorizontalAlignment::Left,
                                        setH(HorizontalAlignment::Left)));
    items.push_back(MenuItemData::Radio("Center", kGroupHAlign, h == HorizontalAlignment::Center,
                                        setH(HorizontalAlignment::Center)));
    items.push_back(MenuItemData::Radio("Right", kGroupHAlign, h == HorizontalAlignment::Right,
                                        setH(HorizontalAlignment::Right)));
    items.push_back(MenuItemData::Radio("Justify", kGroupHAlign, h == HorizontalAlignment::Justify,
                                        setH(HorizontalAlignment::Justify)));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Radio("Top", kGroupVAlign, v == VerticalAlignment::Top,
                                        setV(VerticalAlignment::Top)));
    items.push_back(MenuItemData::Radio("Middle", kGroupVAlign, v == VerticalAlignment::Middle,
                                        setV(VerticalAlignment::Middle)));
    items.push_back(MenuItemData::Radio("Bottom", kGroupVAlign, v == VerticalAlignment::Bottom,
                                        setV(VerticalAlignment::Bottom)));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Checkbox("Wrap text", sheet->GetActiveCellWrapText(),
                                           [sheet](bool on) { sheet->SetSelectionWrapText(on); }));
    return items;
}

std::vector<MenuItemData> BuildNumberItems(UltraCanvasSpreadsheet* sheet) {
    const NumberFormat active = sheet->GetActiveCellNumberFormat();

    std::vector<MenuItemData> items;
    for (const auto& preset : SpreadsheetNumberFormatPresets()) {
        if (preset.label.empty()) {
            items.push_back(MenuItemData::Separator());
            continue;
        }
        NumberFormat format = preset.format;
        MenuItemData item = MenuItemData::Radio(
            preset.label, kGroupNumber, SpreadsheetNumberFormatsEqual(active, format),
            [sheet, format]() { sheet->SetSelectionNumberFormat(format); });
        // The sample sits in the shortcut column, so each row reads
        // "Currency (€)        1.234,57 €".
        item.shortcut = preset.sample;
        items.push_back(item);
    }

    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Action("Increase decimals",
                                         [sheet]() { sheet->AdjustSelectionDecimalPlaces(+1); }));
    items.push_back(MenuItemData::Action("Decrease decimals",
                                         [sheet]() { sheet->AdjustSelectionDecimalPlaces(-1); }));
    return items;
}

std::vector<MenuItemData> BuildFontItems(UltraCanvasSpreadsheet* sheet) {
    const CellFont font = sheet->GetActiveCellFont();

    std::vector<MenuItemData> items;
    items.push_back(MenuItemData::Checkbox("Bold", font.bold,
                                           [sheet](bool on) { sheet->SetSelectionBold(on); }));
    items.push_back(MenuItemData::Checkbox("Italic", font.italic,
                                           [sheet](bool on) { sheet->SetSelectionItalic(on); }));
    items.push_back(MenuItemData::Checkbox("Underline", font.underline != UnderlineStyle::NoUnderline,
                                           [sheet](bool on) {
                                               sheet->SetSelectionUnderline(on ? UnderlineStyle::Single
                                                                               : UnderlineStyle::NoUnderline);
                                           }));
    items.push_back(MenuItemData::Separator());
    for (float size : { 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 14.0f, 18.0f, 24.0f }) {
        std::string label = std::to_string(static_cast<int>(size)) + " pt";
        items.push_back(MenuItemData::Radio(label, kGroupFontSize,
                                            std::fabs(font.size - size) < 0.01f,
                                            [sheet, size]() { sheet->SetSelectionFontSize(size); }));
    }
    return items;
}

std::string DescribeSelection(UltraCanvasSpreadsheet* sheet) {
    const CellRange sel = sheet->GetSelection();
    const bool wholeColumns = sel.end.row >= SpreadsheetLimits::MaxRows - 1;
    const bool wholeRows    = sel.end.col >= SpreadsheetLimits::MaxColumns - 1;

    if (wholeColumns && wholeRows) return "the whole sheet";
    if (wholeColumns) {
        const std::string first = CellAddress::ColumnToLetter(sel.start.col);
        return (sel.start.col == sel.end.col)
            ? ("column " + first)
            : ("columns " + first + "-" + CellAddress::ColumnToLetter(sel.end.col));
    }
    if (wholeRows) {
        const std::string first = std::to_string(sel.start.row + 1);
        return (sel.start.row == sel.end.row)
            ? ("row " + first)
            : ("rows " + first + "-" + std::to_string(sel.end.row + 1));
    }
    return sel.ToString();
}

std::vector<MenuItemData> BuildColumnItems(UltraCanvasSpreadsheet* sheet) {
    std::vector<MenuItemData> items;
    items.push_back(MenuItemData::Action("Fit width to content",
                                         [sheet]() { sheet->AutoFitSelectedColumns(); }));
    items.push_back(MenuItemData::Separator());
    for (int width : { 60, 90, 120, 180, 240 }) {
        items.push_back(MenuItemData::Action("Width " + std::to_string(width) + " px",
                                             [sheet, width]() {
                                                 // GetFormattingRange, not
                                                 // GetSelection: a header click
                                                 // selects the sheet's whole
                                                 // extent.
                                                 CellRange sel = sheet->GetFormattingRange();
                                                 for (int c = sel.start.col; c <= sel.end.col; ++c) {
                                                     sheet->SetColumnWidth(c, width);
                                                 }
                                             }));
    }
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Action("Fit row height", [sheet]() { sheet->AutoFitSelectedRows(); }));
    for (int height : { 20, 28, 40 }) {
        items.push_back(MenuItemData::Action("Row height " + std::to_string(height) + " px",
                                             [sheet, height]() {
                                                 CellRange sel = sheet->GetFormattingRange();
                                                 for (int r = sel.start.row; r <= sel.end.row; ++r) {
                                                     sheet->SetRowHeight(r, height);
                                                 }
                                             }));
    }
    return items;
}

// The Sort submenu: sort the selected rows by the active cell's column, or by
// any column of the selection, ascending or descending. Only the selected rows
// move, each keeping its cells together (SortSelectionByColumn); it is offered
// for the selections that show header sort buttons, and a block holding
// formulas asks first, as those buttons do.
std::vector<MenuItemData> BuildSortItems(UltraCanvasSpreadsheet* sheet) {
    std::vector<MenuItemData> items;
    const CellRange sel = sheet->GetSelection();
    if (!sheet->IsSelectionSortable()) {
        MenuItemData hint = MenuItemData::Action("Select a block of two or more rows to sort", []() {});
        hint.enabled = false;
        items.push_back(hint);
        return items;
    }
    const int firstCol = sel.start.col;
    const int lastCol = sel.end.col;

    const int activeCol = std::clamp(sheet->GetActiveCell().col, firstCol, lastCol);
    const std::string activeName = CellAddress::ColumnToLetter(activeCol);
    auto sortBy = [sheet](int column, SortOrder order) {
        return [sheet, column, order]() { sheet->RequestSortSelectionByColumn(column, order); };
    };

    items.push_back(MenuItemData::Action("Ascending by column " + activeName + " (A to Z, 0 to 9)",
                                         sortBy(activeCol, SortOrder::Ascending)));
    items.push_back(MenuItemData::Action("Descending by column " + activeName + " (Z to A, 9 to 0)",
                                         sortBy(activeCol, SortOrder::Descending)));

    if (lastCol > firstCol) {
        std::vector<MenuItemData> ascending, descending;
        for (int col = firstCol; col <= lastCol; ++col) {
            const std::string label = "Column " + CellAddress::ColumnToLetter(col);
            ascending.push_back(MenuItemData::Action(label, sortBy(col, SortOrder::Ascending)));
            descending.push_back(MenuItemData::Action(label, sortBy(col, SortOrder::Descending)));
        }
        items.push_back(MenuItemData::Separator());
        items.push_back(MenuItemData::Submenu("Ascending by", ascending));
        items.push_back(MenuItemData::Submenu("Descending by", descending));
    }
    return items;
}

} // namespace

// ============================================================================
// NUMBER FORMAT PRESETS
// ============================================================================

std::string SpreadsheetFormatSample(const NumberFormat& format, double value) {
    // Format through a real cell so the sample cannot drift from what the grid
    // renders - it is literally the same code path.
    SpreadsheetCell cell;
    cell.SetNumberFormat(format);
    switch (format.category) {
        case NumberFormatCategory::Date:
        case NumberFormatCategory::Time:
        case NumberFormatCategory::DateTime:
            // A date format only takes effect on a date-typed cell; storing the
            // serial as a plain number would show the serial ("46269.65").
            cell.SetDateTime(DateTimeValue(value));
            break;
        case NumberFormatCategory::Text: {
            // "Text" means the entry is kept verbatim, so the sample has to be
            // a text cell - a numeric one would be run through the number
            // formatter and lose digits.
            std::ostringstream typed;
            typed << value;
            cell.SetText(typed.str());
            break;
        }
        default:
            cell.SetNumber(value);
            break;
    }
    return cell.GetDisplayValue();
}

bool SpreadsheetNumberFormatsEqual(const NumberFormat& a, const NumberFormat& b) {
    if (a.category != b.category) return false;
    switch (a.category) {
        case NumberFormatCategory::General:
        case NumberFormatCategory::Text:
            return true;
        case NumberFormatCategory::Date:
        case NumberFormatCategory::Time:
        case NumberFormatCategory::DateTime:
            return a.formatCode == b.formatCode;
        case NumberFormatCategory::Currency:
        case NumberFormatCategory::Accounting:
            return a.decimalPlaces == b.decimalPlaces &&
                   a.currencySymbol == b.currencySymbol &&
                   a.currencySymbolAfter == b.currencySymbolAfter;
        default:
            return a.decimalPlaces == b.decimalPlaces &&
                   a.useThousandsSeparator == b.useThousandsSeparator;
    }
}

std::vector<SpreadsheetNumberFormatPreset> SpreadsheetNumberFormatPresets() {
    std::vector<SpreadsheetNumberFormatPreset> presets;
    auto add = [&presets](const std::string& label, const NumberFormat& format) {
        presets.push_back({ label, format, std::string() });
    };
    auto separator = [&presets]() { presets.push_back({ std::string(), NumberFormat::General(), std::string() }); };

    add("General", NumberFormat::General());
    separator();
    add("Number", NumberFormat::Number(2, false));
    add("Number, thousands separator", NumberFormat::Number(2, true));
    add("Integer", NumberFormat::Number(0, true));
    add("Percentage", NumberFormat::Percentage(2));
    add("Scientific", NumberFormat::Scientific(2));
    separator();
    add("Currency, dollar", NumberFormat::Currency("$", 2, /*symbolAfter*/false));
    add("Currency, euro", NumberFormat::Currency("€", 2, /*symbolAfter*/true));
    add("Currency, pound", NumberFormat::Currency("£", 2, false));
    add("Currency, yen", NumberFormat::Currency("¥", 0, false));
    add("Currency, franc", NumberFormat::Currency("CHF", 2, true));
    separator();
    add("Date, ISO", NumberFormat::Date("YYYY-MM-DD"));
    add("Date, day first", NumberFormat::Date("DD.MM.YYYY"));
    add("Date, month first", NumberFormat::Date("MM/DD/YYYY"));
    add("Time", NumberFormat::Time("HH:MM:SS"));
    separator();
    add("Text", NumberFormat::Text());

    for (auto& preset : presets) {
        if (preset.label.empty()) continue;
        switch (preset.format.category) {
            case NumberFormatCategory::General:
                preset.sample = SpreadsheetFormatSample(preset.format);
                break;
            case NumberFormatCategory::Date:
            case NumberFormatCategory::Time:
            case NumberFormatCategory::DateTime:
                // 2026-09-04, 15:30:00 - a value that shows every component.
                preset.sample = SpreadsheetFormatSample(
                    preset.format, DateTimeValue::FromDateTime(2026, 9, 4, 15, 30, 0).serialNumber);
                break;
            case NumberFormatCategory::Percentage:
                preset.sample = SpreadsheetFormatSample(preset.format, 0.1235);
                break;
            default:
                preset.sample = SpreadsheetFormatSample(preset.format);
                break;
        }
    }
    return presets;
}

// ============================================================================
// MENU CONSTRUCTION
// ============================================================================

std::vector<MenuItemData> BuildSpreadsheetFormatMenuItems(UltraCanvasSpreadsheet* sheet) {
    std::vector<MenuItemData> items;
    if (!sheet) return items;

    // Header naming what the choices below will apply to, so a menu opened over
    // a multi-cell selection cannot be mistaken for a single-cell one. Whole
    // columns and rows are named as such - "C1:C1048576" says nothing useful.
    items.push_back(MenuItemData::Header("Format " + DescribeSelection(sheet)));
    items.push_back(MenuItemData::Submenu("Alignment", BuildAlignmentItems(sheet)));
    items.push_back(MenuItemData::Submenu("Number", BuildNumberItems(sheet)));
    items.push_back(MenuItemData::Submenu("Font", BuildFontItems(sheet)));
    items.push_back(MenuItemData::Submenu("Text colour", BuildColorItems(sheet, /*background*/false)));
    items.push_back(MenuItemData::Submenu("Background", BuildColorItems(sheet, /*background*/true)));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Submenu("Sort", BuildSortItems(sheet)));
    items.push_back(MenuItemData::Submenu("Column and row size", BuildColumnItems(sheet)));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Action("Merge cells", [sheet]() { sheet->MergeSelection(); }));
    items.push_back(MenuItemData::Action("Unmerge cells", [sheet]() { sheet->UnmergeSelection(); }));
    return items;
}

std::vector<MenuItemData> BuildSpreadsheetFunctionMenuItems(UltraCanvasSpreadsheet* sheet) {
    std::vector<MenuItemData> items;
    SpreadsheetFormulaEngine* engine = sheet ? sheet->GetFormulaEngine() : nullptr;
    if (!engine) return items;
    const FormulaFunctionLibrary& library = engine->GetFunctionLibrary();

    // name -> definition, sorted; and the categories they fall in, sorted.
    std::map<std::string, const FunctionDefinition*> byName;
    std::map<std::string, std::vector<const FunctionDefinition*>> byCategory;
    for (const auto& name : library.GetFunctionNames()) {
        const FunctionDefinition* def = library.GetFunction(name);
        if (!def) continue;
        byName[def->name] = def;
    }
    for (const auto& [name, def] : byName) {
        std::string category = def->category.empty() ? std::string("Other") : def->category;
        if (category == "DateTime") category = "Date and time";
        byCategory[category].push_back(def);
    }

    auto entry = [sheet, &library](const FunctionDefinition* def) {
        const std::string name = def->name;
        // The label is the call with its argument names: ROUND(number, num_digits).
        MenuItemData item = MenuItemData::Action(library.GetSignature(name),
                                                 [sheet, name]() { sheet->InsertFunction(name); });
        // The description sits in the right-hand column, where a menu shows
        // shortcuts, and in full in the tooltip.
        item.shortcut = def->description;
        item.tooltip = def->description;
        item.ellipsize = EllipsizeMode::EllipsizeEnd;
        return item;
    };

    items.push_back(MenuItemData::Header("Insert function (" + std::to_string(byName.size()) + ")"));
    for (const auto& [category, defs] : byCategory) {
        std::vector<MenuItemData> sub;
        for (const auto* def : defs) sub.push_back(entry(def));
        items.push_back(MenuItemData::Submenu(category + " (" + std::to_string(defs.size()) + ")", sub));
    }
    items.push_back(MenuItemData::Separator());
    std::vector<MenuItemData> all;
    for (const auto& [name, def] : byName) all.push_back(entry(def));
    items.push_back(MenuItemData::Submenu("All functions A to Z", all));
    return items;
}

void UltraCanvasSpreadsheet::ShowFunctionMenuAt(int windowX, int windowY) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;
    if (!functionMenu_) {
        functionMenu_ = std::make_shared<UltraCanvasMenu>(GetIdentifier() + "_FunctionMenu", 0, 0, 260, 0);
        functionMenu_->SetMenuType(MenuType::PopupMenu);
    }
    // Rebuilt each time: an application may register functions later.
    functionMenu_->Clear();
    for (const auto& item : BuildSpreadsheetFunctionMenuItems(this)) functionMenu_->AddItem(item);

    PopupElementSettings settings;
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    functionMenu_->OpenMenu(Point2Di(windowX, windowY), *window, settings);
}

std::vector<MenuItemData> BuildSpreadsheetSheetTabMenuItems(UltraCanvasSpreadsheet* sheet, int sheetIndex) {
    std::vector<MenuItemData> items;
    const int count = sheet->GetSheetCount();

    // Inserts a sheet at `at` and opens it.
    auto insertAt = [sheet](int at) {
        return [sheet, at]() {
            if (sheet->InsertSheet(at)) sheet->SetActiveSheet(at);
        };
    };
    if (sheetIndex < 0 || sheetIndex >= count) {
        items.push_back(MenuItemData::Action("Insert sheet", insertAt(count)));
        return items;
    }

    items.push_back(MenuItemData::Action("Insert sheet before", insertAt(sheetIndex)));
    items.push_back(MenuItemData::Action("Insert sheet after", insertAt(sheetIndex + 1)));
    items.push_back(MenuItemData::Action("Duplicate sheet", [sheet, sheetIndex]() {
        const std::string base = sheet->GetSheet(sheetIndex)->GetName();
        // "Sales (2)", "Sales (3)" ... the first one free and valid.
        std::string name;
        for (int n = 2; n < 1000; ++n) {
            name = base + " (" + std::to_string(n) + ")";
            if (sheet->ValidateSheetName(-1, name).empty()) break;
            name.clear();
        }
        if (sheet->CopySheet(sheetIndex, name)) sheet->SetActiveSheet(sheetIndex + 1);
    }));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Action("Rename sheet...", [sheet, sheetIndex]() {
        sheet->PromptRenameSheet(sheetIndex);
    }));
    items.push_back(MenuItemData::Separator());

    // Moving keeps the moved sheet open.
    auto moveTo = [sheet, sheetIndex](int to) {
        return [sheet, sheetIndex, to]() {
            sheet->MoveSheet(sheetIndex, to);
            sheet->SetActiveSheet(to);
        };
    };
    MenuItemData left = MenuItemData::Action("Move left", moveTo(sheetIndex - 1));
    MenuItemData right = MenuItemData::Action("Move right", moveTo(sheetIndex + 1));
    MenuItemData start = MenuItemData::Action("Move to start", moveTo(0));
    MenuItemData end = MenuItemData::Action("Move to end", moveTo(count - 1));
    left.enabled = start.enabled = sheetIndex > 0;
    right.enabled = end.enabled = sheetIndex < count - 1;
    items.push_back(left);
    items.push_back(right);
    items.push_back(start);
    items.push_back(end);
    items.push_back(MenuItemData::Separator());

    MenuItemData remove = MenuItemData::Action("Delete sheet", [sheet, sheetIndex]() {
        sheet->RequestRemoveSheet(sheetIndex);
    });
    remove.enabled = count > 1;
    items.push_back(remove);
    return items;
}

void PopulateSpreadsheetFormatMenu(UltraCanvasMenu& menu, UltraCanvasSpreadsheet* sheet) {
    menu.Clear();
    for (const auto& item : BuildSpreadsheetFormatMenuItems(sheet)) {
        menu.AddItem(item);
    }
}

std::shared_ptr<UltraCanvasMenu> CreateSpreadsheetFormatMenu(const std::string& identifier,
                                                             UltraCanvasSpreadsheet* sheet) {
    auto menu = std::make_shared<UltraCanvasMenu>(identifier, 0, 0, 260, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    PopulateSpreadsheetFormatMenu(*menu, sheet);
    return menu;
}

// ============================================================================
// GRID INTEGRATION
// ============================================================================

void UltraCanvasSpreadsheet::ShowFormatMenuAt(int windowX, int windowY) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;   // not on screen yet: nothing to anchor the popup to

    if (!formatMenu_) {
        formatMenu_ = CreateSpreadsheetFormatMenu(GetIdentifier() + "_FormatMenu", this);
    } else {
        // Rebuild every time so the ticks match the cell that was just clicked.
        PopulateSpreadsheetFormatMenu(*formatMenu_, this);
    }

    PopupElementSettings settings;
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    formatMenu_->OpenMenu(Point2Di(windowX, windowY), *window, settings);
}

void UltraCanvasSpreadsheet::ShowSheetTabMenuAt(int sheetIndex, int windowX, int windowY) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;
    if (!sheetTabMenu_) {
        sheetTabMenu_ = std::make_shared<UltraCanvasMenu>(GetIdentifier() + "_SheetTabMenu", 0, 0, 200, 0);
        sheetTabMenu_->SetMenuType(MenuType::PopupMenu);
    }
    // Rebuilt each time: what can move or be deleted depends on the tab.
    sheetTabMenu_->Clear();
    for (const auto& item : BuildSpreadsheetSheetTabMenuItems(this, sheetIndex)) sheetTabMenu_->AddItem(item);

    PopupElementSettings settings;
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    sheetTabMenu_->OpenMenu(Point2Di(windowX, windowY), *window, settings);
}

void UltraCanvasSpreadsheet::PromptRenameSheet(int index) {
    const SpreadsheetSheet* target = GetSheet(index);
    if (!target) return;
    if (IsEditing()) StopEditing(true);

    // The dialog may outlive this element, so the callback holds it weakly,
    // and finds the sheet again by name in case the tabs moved meanwhile.
    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    const std::string oldName = target->GetName();
    UltraCanvasDialogManager::ShowInputDialog("New name for sheet \"" + oldName + "\":", "Rename sheet",
        oldName, InputType::Text,
        [weakSelf, oldName](DialogResult result, const std::string& name) {
            if (result != DialogResult::OK) return;
            auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
            if (!self) return;
            int at = -1;
            for (int i = 0; i < self->GetSheetCount(); ++i) {
                if (self->GetSheet(i)->GetName() == oldName) { at = i; break; }
            }
            if (at < 0 || name == oldName) return;
            const std::string problem = self->ValidateSheetName(at, name);
            if (problem.empty()) {
                self->RenameSheet(at, name);
                return;
            }
            // Say why, then ask again.
            std::weak_ptr<UltraCanvasUIElement> weakAgain = self->weak_from_this();
            UltraCanvasDialogManager::ShowMessage(problem, "Rename sheet", DialogType::Warning,
                DialogButtons::OK,
                [weakAgain, oldName](DialogResult) {
                    auto again = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakAgain.lock());
                    if (!again) return;
                    for (int i = 0; i < again->GetSheetCount(); ++i) {
                        if (again->GetSheet(i)->GetName() == oldName) { again->PromptRenameSheet(i); return; }
                    }
                },
                self->GetWindow());
        },
        GetWindow());
}

void UltraCanvasSpreadsheet::RequestRemoveSheet(int index) {
    const SpreadsheetSheet* target = GetSheet(index);
    if (!target || GetSheetCount() <= 1) return;
    if (IsEditing()) StopEditing(true);

    const std::string name = target->GetName();
    bool empty = true;
    target->ForEachCell([&empty](int, int, const SpreadsheetCell& cell) {
        if (!cell.IsEmpty()) empty = false;
    });
    if (empty) {
        RemoveSheet(index);
        return;
    }

    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowMessage(
        "Delete sheet \"" + name + "\" and everything on it?\n\n"
        "Formulas on other sheets that refer to it will show #REF!. "
        "Undo (Ctrl+Z) brings the sheet back.",
        "Delete sheet", DialogType::Warning, DialogButtons::OKCancel,
        [weakSelf, name](DialogResult result) {
            if (result != DialogResult::OK) return;
            auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
            if (!self || self->GetSheetCount() <= 1) return;
            for (int i = 0; i < self->GetSheetCount(); ++i) {
                if (self->GetSheet(i)->GetName() == name) { self->RemoveSheet(i); return; }
            }
        },
        GetWindow());
}

} // namespace UltraCanvas
