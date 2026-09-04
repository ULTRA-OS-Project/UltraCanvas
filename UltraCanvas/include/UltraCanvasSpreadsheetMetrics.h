// include/UltraCanvasSpreadsheetMetrics.h
// Column/row sizing helpers shared by the spreadsheet model, the file importers
// and the exporters: document length conversion and content width estimation.
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

namespace UltraCanvas {

// ============================================================================
// SIZING CONSTANTS
// ============================================================================

struct SpreadsheetAutoFit {
    // Slack added to the measured text so glyphs never touch the gridline. The
    // renderer insets cell text by 3px on each side (see RenderCell), and a
    // couple of extra pixels keep an italic overhang inside the cell.
    static constexpr int Padding = 10;
    // An auto-fitted column never collapses below this, so an empty or
    // single-character column still shows its header letter.
    static constexpr int MinWidth = 32;
    // ...and never runs away on a cell holding a paragraph of text.
    static constexpr int MaxWidth = 420;
    // Rows scanned per column when auto-fitting. Big imports would otherwise
    // walk a million rows per column for a width the user cannot see anyway.
    static constexpr int MaxScanRows = 4000;
};

// ============================================================================
// DOCUMENT LENGTHS
// ============================================================================

// 96 dpi is the reference the grid's pixel sizes are authored against, and the
// unit ODF/OOXML lengths are converted through.
inline constexpr double SpreadsheetPixelsPerInch = 96.0;

// Convert an ODF/CSS length such as "2.5cm", "1in", "72pt", "18mm", "1.5pc" or
// "64px" into grid pixels. Returns `fallbackPixels` when the string is empty,
// unparseable or not positive, so callers can pass the current default width
// and use the result unconditionally.
inline int SpreadsheetLengthToPixels(const std::string& value, int fallbackPixels) {
    // Leading number (ODF always writes a plain decimal, no exponent).
    size_t i = 0;
    while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
    size_t numStart = i;
    if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
    bool sawDigit = false;
    while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) { ++i; sawDigit = true; }
    if (i < value.size() && value[i] == '.') {
        ++i;
        while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) { ++i; sawDigit = true; }
    }
    if (!sawDigit) return fallbackPixels;

    double magnitude = 0.0;
    try {
        magnitude = std::stod(value.substr(numStart, i - numStart));
    } catch (...) {
        return fallbackPixels;
    }
    if (!(magnitude > 0.0)) return fallbackPixels;

    // Unit suffix, lower-cased.
    std::string unit;
    for (; i < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (std::isspace(c)) continue;
        unit += static_cast<char>(std::tolower(c));
    }

    double inches;
    if (unit == "cm")       inches = magnitude / 2.54;
    else if (unit == "mm")  inches = magnitude / 25.4;
    else if (unit == "in")  inches = magnitude;
    else if (unit == "pt")  inches = magnitude / 72.0;
    else if (unit == "pc")  inches = magnitude / 6.0;          // 1 pica = 12pt
    else if (unit == "px" || unit.empty()) return std::max(1, static_cast<int>(magnitude + 0.5));
    else return fallbackPixels;                                 // em/%/unknown: no reliable conversion

    return std::max(1, static_cast<int>(inches * SpreadsheetPixelsPerInch + 0.5));
}

// Inverse of the above: grid pixels as a centimetre length for ODF output.
inline double SpreadsheetPixelsToCentimetres(int pixels) {
    return (pixels / SpreadsheetPixelsPerInch) * 2.54;
}

// ============================================================================
// CONTENT WIDTH ESTIMATION
// ============================================================================

// Number of UTF-8 characters, not bytes. "28.869,80 €" is 11 characters but 13
// bytes, and sizing a column by its byte count makes every accented or currency
// column noticeably too wide.
inline size_t SpreadsheetUtf8Length(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;   // skip continuation bytes
    }
    return count;
}

// Width of `text` in `font`, estimated without a render context. Used when the
// grid is sized before it is attached to a window (a file loaded during page
// construction); the first render redoes the fit with real font metrics.
inline int SpreadsheetEstimateTextWidth(const std::string& text, const CellFont& font) {
    // ~0.6em average advance for the proportional UI fonts the grid defaults
    // to, a little more when bold. Deliberately errs wide: a column slightly
    // too wide reads fine, one slightly too narrow clips the value.
    double perChar = static_cast<double>(font.size <= 0.0f ? 11.0f : font.size) * 0.60;
    if (font.bold) perChar *= 1.06;
    if (font.italic) perChar *= 1.02;
    return static_cast<int>(SpreadsheetUtf8Length(text) * perChar + 0.5);
}

// Clamp a measured content width into the range an auto-fitted column may take.
inline int SpreadsheetClampAutoFitWidth(int contentWidth) {
    return std::clamp(contentWidth + SpreadsheetAutoFit::Padding,
                      SpreadsheetAutoFit::MinWidth, SpreadsheetAutoFit::MaxWidth);
}

} // namespace UltraCanvas
