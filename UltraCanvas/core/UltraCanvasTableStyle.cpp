// core/UltraCanvasTableStyle.cpp
// Built-in palettes and design presets for the shared table style model.
// Version: 1.0.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework
#include "UltraCanvasTableStyle.h"

namespace UltraCanvas {

// ============================================================================
// PALETTES
// ============================================================================

TablePalette TablePalette::BuiltIn(TablePaletteKind kind) {
    TablePalette p;   // Defaults are the Classic palette
    switch (kind) {
        case TablePaletteKind::Classic:
        case TablePaletteKind::Custom:
            break;
        case TablePaletteKind::Ocean:
            p.accent = Color(0, 141, 151);
            p.bandTint = Color(240, 249, 249);
            p.hover = Color(230, 245, 246);
            p.selection = Color(205, 236, 238);
            p.highlightTint = Color(0, 141, 151, 18);
            break;
        case TablePaletteKind::Olive:
            p.accent = Color(107, 124, 50);
            p.bandTint = Color(246, 248, 238);
            p.hover = Color(240, 244, 228);
            p.selection = Color(226, 234, 202);
            p.highlightTint = Color(107, 124, 50, 18);
            break;
        case TablePaletteKind::Slate:
            p.accent = Color(71, 85, 105);
            p.bandTint = Color(245, 247, 250);
            p.hover = Color(238, 242, 247);
            p.selection = Color(222, 230, 240);
            p.highlightTint = Color(71, 85, 105, 16);
            break;
        case TablePaletteKind::Warm:
            p.accent = Color(226, 55, 60);           // CTA red
            p.bandTint = Color(250, 246, 244);
            p.hover = Color(250, 241, 239);
            p.selection = Color(248, 226, 224);
            p.highlightTint = Color(0, 190, 215, 20); // Cyan wash (screenshot)
            break;
        case TablePaletteKind::Dark:
            p.accent = Color(96, 176, 255);
            p.accentText = Color(18, 22, 28);
            p.text = Color(226, 230, 236);
            p.mutedText = Color(140, 148, 158);
            p.background = Color(24, 28, 34);
            p.bandTint = Color(30, 35, 42);
            p.sectionBackground = Color(38, 44, 52);
            p.sectionText = Color(226, 230, 236);
            p.gridline = Color(52, 58, 66);
            p.hover = Color(36, 44, 54);
            p.selection = Color(44, 62, 82);
            p.highlightTint = Color(96, 176, 255, 26);
            p.positiveGlyph = Color(60, 200, 140);
            p.negativeGlyph = Color(110, 116, 124);
            p.negativeNumber = Color(255, 110, 110);
            break;
    }
    return p;
}

// ============================================================================
// DESIGN PRESETS
// ============================================================================

namespace TableStyles {

namespace {

    // Common ground every preset starts from.
    TableStyleSheet Base(const TablePalette& p) {
        TableStyleSheet s;
        s.palette = p;
        TableRegionStyle& whole = s.EditRegion(TableRegion::WholeTable);
        whole.background = p.background;
        whole.textColor = p.text;
        whole.fontSize = 12.0f;
        whole.hAlign = TextAlignment::Left;

        TableRegionStyle& hover = s.EditRegion(TableRegion::HoverRow);
        hover.background = p.hover;
        TableRegionStyle& sel = s.EditRegion(TableRegion::SelectedRow);
        sel.background = p.selection;

        s.checkGlyphColor = p.positiveGlyph;
        s.crossGlyphColor = p.negativeGlyph;
        s.negativeNumberColor = p.negativeNumber;

        s.button.textColor = p.accent;
        s.button.borderColor = p.accent;
        s.button.background = Colors::Transparent;
        s.buttonHighlight.textColor = p.accentText;
        s.buttonHighlight.borderColor = p.accent;
        s.buttonHighlight.background = p.accent;
        return s;
    }

    // D1 — pricing/feature comparison matrix (the Lexware-style screenshot):
    // hairline horizontal rules only, grey section bands, centered feature
    // columns, tinted highlighted column with a badge, CTA buttons.
    TableStyleSheet Comparison(const TablePalette& p) {
        TableStyleSheet s = Base(p);
        s.name = "Comparison";
        s.rowHeight = 38.0f;
        s.headerHeight = 44.0f;
        s.sectionRowHeight = 32.0f;
        s.totalRowHeight = 52.0f;
        s.outerBorder = TableBorder(1.0f, p.gridline);
        s.horizontalGrid = TableBorder(1.0f, p.gridline);

        TableRegionStyle& whole = s.EditRegion(TableRegion::WholeTable);
        whole.hAlign = TextAlignment::Center;      // Feature/plan columns

        TableRegionStyle& first = s.EditRegion(TableRegion::FirstColumn);
        first.hAlign = TextAlignment::Left;        // Row labels

        TableRegionStyle& header = s.EditRegion(TableRegion::HeaderRow);
        header.fontWeight = FontWeight::Bold;
        header.fontSize = 13.0f;
        header.hAlign = TextAlignment::Center;
        header.borderBottom = TableBorder(1.0f, p.gridline);

        TableRegionStyle& section = s.EditRegion(TableRegion::SectionRow);
        section.background = p.sectionBackground;
        section.textColor = p.sectionText;
        section.fontWeight = FontWeight::Bold;
        section.fontSize = 12.0f;
        section.hAlign = TextAlignment::Left;

        TableRegionStyle& highlight = s.EditRegion(TableRegion::HighlightColumn);
        highlight.background = p.highlightTint;

        TableRegionStyle& total = s.EditRegion(TableRegion::TotalRow);
        total.borderTop = TableBorder(1.0f, p.gridline);
        return s;
    }

    // D2 — banded business data grid: accent header, zebra rows, outer frame.
    TableStyleSheet Professional(const TablePalette& p) {
        TableStyleSheet s = Base(p);
        s.name = "Professional";
        s.rowHeight = 30.0f;
        s.headerHeight = 34.0f;
        s.outerBorder = TableBorder(1.0f, p.gridline);
        s.horizontalGrid = TableBorder(1.0f, p.gridline);

        TableRegionStyle& header = s.EditRegion(TableRegion::HeaderRow);
        header.background = p.accent;
        header.textColor = p.accentText;
        header.fontWeight = FontWeight::Bold;

        TableRegionStyle& banded = s.EditRegion(TableRegion::BandedRows);
        banded.background = p.bandTint;

        TableRegionStyle& section = s.EditRegion(TableRegion::SectionRow);
        section.background = p.sectionBackground;
        section.textColor = p.sectionText;
        section.fontWeight = FontWeight::Bold;

        TableRegionStyle& highlight = s.EditRegion(TableRegion::HighlightColumn);
        highlight.background = p.highlightTint;

        TableRegionStyle& total = s.EditRegion(TableRegion::TotalRow);
        total.fontWeight = FontWeight::Bold;
        total.borderTop = TableBorder(1.0f, p.accent);
        return s;
    }

    // D3 — booktabs/editorial: heavy top and bottom rules, thin rule under
    // the header, no fills, no vertical rules, generous padding.
    TableStyleSheet Minimal(const TablePalette& p) {
        TableStyleSheet s = Base(p);
        s.name = "Minimal";
        s.rowHeight = 32.0f;
        s.headerHeight = 38.0f;
        s.cellPaddingX = 16.0f;
        s.outerBorder = TableBorder();             // No frame
        s.horizontalGrid = TableBorder();          // No inner rules

        // The heavy rules live on the header (top) and total row (bottom);
        // the view also draws them on the first/last row when present.
        TableRegionStyle& header = s.EditRegion(TableRegion::HeaderRow);
        header.fontWeight = FontWeight::Bold;
        header.borderTop = TableBorder(2.0f, p.text);
        header.borderBottom = TableBorder(1.0f, p.text);

        TableRegionStyle& section = s.EditRegion(TableRegion::SectionRow);
        section.fontWeight = FontWeight::Bold;
        section.fontSlant = FontSlant::Italic;
        section.borderBottom = TableBorder(1.0f, p.gridline);

        TableRegionStyle& total = s.EditRegion(TableRegion::TotalRow);
        total.fontWeight = FontWeight::Bold;
        total.borderTop = TableBorder(1.0f, p.text);
        total.borderBottom = TableBorder(2.0f, p.text);

        TableRegionStyle& highlight = s.EditRegion(TableRegion::HighlightColumn);
        highlight.background = p.highlightTint;
        return s;
    }

    // D6 — financial statement: right-aligned numerics, indent hierarchy in
    // the label column (caller uses leading spaces or per-cell styles),
    // subtotal bands with a single top rule, double-rule total row,
    // negatives in red.
    TableStyleSheet Financial(const TablePalette& p) {
        TableStyleSheet s = Base(p);
        s.name = "Financial";
        s.rowHeight = 28.0f;
        s.headerHeight = 32.0f;
        s.sectionRowHeight = 30.0f;
        s.totalRowHeight = 36.0f;
        s.negativeNumbersRed = true;
        s.outerBorder = TableBorder();
        s.horizontalGrid = TableBorder();

        TableRegionStyle& whole = s.EditRegion(TableRegion::WholeTable);
        whole.hAlign = TextAlignment::Right;       // Numeric columns

        TableRegionStyle& first = s.EditRegion(TableRegion::FirstColumn);
        first.hAlign = TextAlignment::Left;        // Account labels

        TableRegionStyle& header = s.EditRegion(TableRegion::HeaderRow);
        header.fontWeight = FontWeight::Bold;
        header.hAlign = TextAlignment::Right;
        header.borderBottom = TableBorder(1.0f, p.text);

        TableRegionStyle& section = s.EditRegion(TableRegion::SectionRow);
        section.fontWeight = FontWeight::Bold;
        section.hAlign = TextAlignment::Left;

        TableRegionStyle& total = s.EditRegion(TableRegion::TotalRow);
        total.fontWeight = FontWeight::Bold;
        total.borderTop = TableBorder(1.0f, p.text);
        total.borderBottom = TableBorder(1.0f, p.text, /*doubleRule=*/true);

        TableRegionStyle& highlight = s.EditRegion(TableRegion::HighlightColumn);
        highlight.background = p.highlightTint;
        return s;
    }

} // namespace

TableStyleSheet Create(TableDesign design, const TablePalette& palette) {
    switch (design) {
        case TableDesign::Comparison:   return Comparison(palette);
        case TableDesign::Professional: return Professional(palette);
        case TableDesign::Minimal:      return Minimal(palette);
        case TableDesign::Financial:    return Financial(palette);
    }
    return Base(palette);
}

TableStyleSheet Create(TableDesign design) {
    // Comparison reads best with the Warm palette (the reference screenshot);
    // the other presets default to Classic.
    TablePaletteKind kind = (design == TableDesign::Comparison)
                                ? TablePaletteKind::Warm
                                : TablePaletteKind::Classic;
    return Create(design, TablePalette::BuiltIn(kind));
}

} // namespace TableStyles

} // namespace UltraCanvas
