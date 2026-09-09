// include/UltraCanvasSpreadsheetFormatMenu.h
// Cell-formatting menu for UltraCanvasSpreadsheet: alignment, number-format
// presets, font style, colours and column/row sizing, applied to the current
// selection. The grid opens it on a right-click; applications can also drop it
// behind a toolbar button.
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasMenu.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// NUMBER FORMAT PRESETS
// ============================================================================

// One entry of the "Number" submenu: a ready-made NumberFormat with a label and
// a live sample rendered through the same formatter the grid uses, so what the
// menu shows is what the cell will show.
struct SpreadsheetNumberFormatPreset {
    std::string label;
    NumberFormat format;
    // Sample of `format` applied to a representative value, for the menu's
    // right-hand column. Filled in by SpreadsheetNumberFormatPresets().
    std::string sample;
};

// The presets offered by the Number submenu, in menu order. An empty label
// marks a separator.
std::vector<SpreadsheetNumberFormatPreset> SpreadsheetNumberFormatPresets();

// Render `value` through `format` exactly as a cell would display it.
std::string SpreadsheetFormatSample(const NumberFormat& format, double value = 1234.5678);

// True when two formats would display identically - used to tick the preset
// matching the active cell.
bool SpreadsheetNumberFormatsEqual(const NumberFormat& a, const NumberFormat& b);

// ============================================================================
// MENU CONSTRUCTION
// ============================================================================

// The formatting menu's items for `sheet`'s current selection, with the
// checkmarks and radio state read from the active cell. Rebuild these each time
// the menu opens so the state stays current.
std::vector<MenuItemData> BuildSpreadsheetFormatMenuItems(UltraCanvasSpreadsheet* sheet);

// Replace `menu`'s contents with the items above.
void PopulateSpreadsheetFormatMenu(UltraCanvasMenu& menu, UltraCanvasSpreadsheet* sheet);

// A ready-to-open popup menu wired to `sheet`. The caller keeps the returned
// shared_ptr alive for as long as the menu may be shown.
std::shared_ptr<UltraCanvasMenu> CreateSpreadsheetFormatMenu(const std::string& identifier,
                                                             UltraCanvasSpreadsheet* sheet);

} // namespace UltraCanvas
