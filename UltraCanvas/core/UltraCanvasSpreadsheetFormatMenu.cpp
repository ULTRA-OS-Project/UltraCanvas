// core/UltraCanvasSpreadsheetFormatMenu.cpp
// Cell-formatting menu for UltraCanvasSpreadsheet.
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetFormatMenu.h"
#include "UltraCanvasWindow.h"
#include <algorithm>
#include <cmath>
#include <sstream>

// UltraCanvasWindow.h reaches <X11/Xlib.h> on Linux/BSD, whose `None` macro
// collides with UnderlineStyle::None and BorderStyle::None below. Same #undef
// dance UltraCanvasSpreadsheetTypes.h and the chart plugins do.
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif
#ifdef Always
#undef Always
#endif

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
    items.push_back(MenuItemData::Checkbox("Underline", font.underline != UnderlineStyle::None,
                                           [sheet](bool on) {
                                               sheet->SetSelectionUnderline(on ? UnderlineStyle::Single
                                                                               : UnderlineStyle::None);
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
    items.push_back(MenuItemData::Submenu("Column and row size", BuildColumnItems(sheet)));
    items.push_back(MenuItemData::Separator());
    items.push_back(MenuItemData::Action("Merge cells", [sheet]() { sheet->MergeSelection(); }));
    items.push_back(MenuItemData::Action("Unmerge cells", [sheet]() { sheet->UnmergeSelection(); }));
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

} // namespace UltraCanvas
