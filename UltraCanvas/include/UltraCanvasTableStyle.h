// include/UltraCanvasTableStyle.h
// Region-based table style model shared by UltraCanvasTableView (native
// consumer) and, later, UltraCanvasSpreadsheet::ApplyTableStyle ("Format as
// Table"). See Docs/UltraCanvas/UltraCanvasTableViewProposal.md §4.
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <map>
#include <optional>
#include <string>

namespace UltraCanvas {

// ============================================================================
// PALETTE (orthogonal to the design, like PertChartPalette)
// ============================================================================

enum class TablePaletteKind {
    Classic,        // Blue accent on white
    Ocean,          // Teal accent
    Olive,          // Muted green accent
    Slate,          // Blue-grey accent
    Warm,           // Red/orange accent (the pricing-matrix look)
    Dark,           // Light accents on a dark panel
    Custom          // Palette supplied by the caller
};

struct TablePalette {
    Color accent = Color(0, 120, 215);          // Header fills, buttons, badge
    Color accentText = Colors::White;           // Text on accent fills
    Color text = Color(45, 45, 55);             // Default cell text
    Color mutedText = Color(120, 120, 130);     // Secondary text, cross glyphs
    Color background = Colors::White;           // Table base fill
    Color bandTint = Color(246, 248, 250);      // Zebra stripe fill
    Color sectionBackground = Color(232, 232, 234); // Section band fill
    Color sectionText = Color(45, 45, 55);
    Color gridline = Color(224, 226, 228);
    Color hover = Color(240, 246, 252);
    Color selection = Color(215, 232, 250);
    Color highlightTint = Color(0, 120, 215, 18);   // Highlighted column wash
    Color positiveGlyph = Color(0, 150, 90);    // Check marks
    Color negativeGlyph = Color(180, 182, 186); // Not-included glyphs
    Color negativeNumber = Color(200, 40, 40);  // Negative values (Financial)

    // Built-in palette for `kind`. Custom returns Classic (a custom palette
    // is supplied by the caller, not looked up).
    static TablePalette BuiltIn(TablePaletteKind kind);
};

// ============================================================================
// REGIONS
// ============================================================================

// Style regions layered in resolution order (later overrides earlier, and
// only for the fields a region actually sets — see TableRegionStyle):
// WholeTable -> BandedRows -> FirstColumn/LastColumn -> HeaderRow/SectionRow/
// TotalRow -> HighlightColumn -> HoverRow/SelectedRow.
enum class TableRegion {
    WholeTable,
    HeaderRow,
    SectionRow,      // Full-width group band ("Auftrag & Buchhaltung", …)
    TotalRow,        // Footer/total band
    FirstColumn,     // Row-label column emphasis
    LastColumn,
    BandedRows,      // Second row band (zebra stripe)
    HighlightColumn, // Recommended/current column (with optional badge)
    HoverRow,        // Widget-only interaction states
    SelectedRow
};

// A horizontal or vertical rule attached to one side of a region.
struct TableBorder {
    float width = 0.0f;
    Color color = Colors::Transparent;
    bool doubleRule = false;   // Two parallel hairlines (classic total row)

    TableBorder() = default;
    TableBorder(float w, const Color& c, bool dbl = false)
        : width(w), color(c), doubleRule(dbl) {}
    bool IsVisible() const { return width > 0.0f && color.a > 0; }
};

// One region's contribution. Every field is optional so regions layer:
// an unset field leaves the value from the regions below untouched.
struct TableRegionStyle {
    std::optional<Color> background;
    std::optional<Color> textColor;
    std::optional<float> fontSize;
    std::optional<FontWeight> fontWeight;
    std::optional<FontSlant> fontSlant;
    std::optional<TextAlignment> hAlign;
    std::optional<TableBorder> borderTop;
    std::optional<TableBorder> borderBottom;
    std::optional<TableBorder> borderLeft;
    std::optional<TableBorder> borderRight;
};

// Fully resolved style of one cell after layering (what the renderer uses).
struct TableResolvedCellStyle {
    Color background = Colors::Transparent;
    Color textColor = Colors::Black;
    float fontSize = 12.0f;
    FontWeight fontWeight = FontWeight::Normal;
    FontSlant fontSlant = FontSlant::Normal;
    TextAlignment hAlign = TextAlignment::Left;
    TableBorder borderTop, borderBottom, borderLeft, borderRight;

    void Apply(const TableRegionStyle& r) {
        if (r.background)   background = *r.background;
        if (r.textColor)    textColor = *r.textColor;
        if (r.fontSize)     fontSize = *r.fontSize;
        if (r.fontWeight)   fontWeight = *r.fontWeight;
        if (r.fontSlant)    fontSlant = *r.fontSlant;
        if (r.hAlign)       hAlign = *r.hAlign;
        if (r.borderTop)    borderTop = *r.borderTop;
        if (r.borderBottom) borderBottom = *r.borderBottom;
        if (r.borderLeft)   borderLeft = *r.borderLeft;
        if (r.borderRight)  borderRight = *r.borderRight;
    }
};

// ============================================================================
// BUTTON APPEARANCE (Button cells; Outline for normal columns, Filled for
// the highlighted column — the AUSWÄHLEN pattern)
// ============================================================================

struct TableButtonStyle {
    Color textColor = Color(0, 120, 215);
    Color background = Colors::Transparent;
    Color borderColor = Color(0, 120, 215);
    float borderWidth = 1.0f;
    float cornerRadius = 3.0f;
};

// ============================================================================
// STYLE SHEET
// ============================================================================

// Complete visual description of a table. Design presets fill this in;
// every field can be adjusted afterwards.
struct TableStyleSheet {
    std::string name;
    TablePalette palette;                       // Kept for derived accents
    std::map<TableRegion, TableRegionStyle> regions;

    // ----- Metrics -----
    std::string fontFamily;                     // Empty = system default
    float rowHeight = 36.0f;
    float headerHeight = 42.0f;
    float sectionRowHeight = 32.0f;
    float totalRowHeight = 48.0f;
    float badgeBandHeight = 26.0f;              // Above header, when a badge is set
    float cellPaddingX = 12.0f;
    float cellPaddingY = 6.0f;
    int rowBandSize = 1;                        // Zebra stripe thickness (rows)

    // ----- Table chrome -----
    TableBorder outerBorder;                    // Frame around the whole table
    TableBorder horizontalGrid;                 // Rule under each data row
    TableBorder verticalGrid;                   // Rule after each column
    float cornerRadius = 0.0f;

    // ----- Content accents -----
    Color checkGlyphColor = Color(0, 150, 90);
    Color crossGlyphColor = Color(180, 182, 186);
    Color badgeBackground = Color(0, 190, 215); // Column badge chip
    Color badgeTextColor = Colors::White;
    float badgeFontSize = 10.0f;
    TableButtonStyle button;                    // Button cells (outline)
    TableButtonStyle buttonHighlight;           // Button cells in the highlighted column
    bool negativeNumbersRed = false;            // Financial: negatives in red
    Color negativeNumberColor = Color(200, 40, 40);

    const TableRegionStyle* Region(TableRegion r) const {
        auto it = regions.find(r);
        return it == regions.end() ? nullptr : &it->second;
    }
    TableRegionStyle& EditRegion(TableRegion r) { return regions[r]; }
};

// ============================================================================
// DESIGN PRESETS
// ============================================================================

enum class TableDesign {
    Comparison,     // D1: pricing/feature matrix (section bands, highlight column)
    Professional,   // D2: banded data grid with an accent header
    Minimal,        // D3: booktabs/editorial rules, no fills
    Financial       // D6: statement layout, double-rule total row
};

namespace TableStyles {
    // Preset filled with the design's default palette.
    TableStyleSheet Create(TableDesign design);
    // Preset recolored with an explicit palette (e.g. a brand accent).
    TableStyleSheet Create(TableDesign design, const TablePalette& palette);
}

} // namespace UltraCanvas
