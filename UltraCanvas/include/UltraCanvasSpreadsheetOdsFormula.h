// include/UltraCanvasSpreadsheetOdsFormula.h
// Translation between OpenDocument (ODF/ODS) formula reference syntax and the
// spreadsheet engine's native A1 syntax.
//
// ODF stores formulas with a namespace prefix and bracketed references that use
// a leading '.' to mean "the current sheet", e.g.
//     of:=[.D28]-[.E28]
//     of:=SUM([.B28:.B42])
//     of:=['01 2025'.B55]
//     of:=SUM(['01 2026'.B4:.B40])
// The formula engine (FormulaTokenizer / CellAddress::FromString) expects
// unbracketed A1 references, ranges written A1:B2 and sheet-qualified
// references written 'Sheet Name'.A1. Loading an ODS therefore requires
// rewriting every formula from the ODF form to the native form; without it the
// bracketed ranges and cross-sheet references fail to parse.
//
// The reverse direction (native AST -> ODF) is emitted by the ODS saver, which
// has the parsed AST available and can bracket each reference precisely.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#pragma once

#include <string>

namespace UltraCanvas {

// Convert an ODF formula string into the engine's native syntax:
//   * strip the leading namespace prefix on '=' (e.g. "of:=", "oooc:=")
//   * drop the '[' and ']' that wrap references
//   * drop the current-sheet '.' separator (the one with an empty sheet name),
//     while keeping real 'Sheet'.cell / Sheet.cell separators
// Dots that are not inside a bracketed reference (decimal numbers such as
// 199.09) are left untouched.
inline std::string OdsFormulaToNative(const std::string& odsFormula) {
    std::string s = odsFormula;

    // Strip the ODF namespace prefix on the leading '=' (e.g. "of:=", "oooc:=").
    if (!s.empty() && s[0] != '=') {
        std::string::size_type eq = s.find(":=");
        if (eq != std::string::npos) {
            s = s.substr(eq + 1);            // keep from '='
        } else if (s.find('=') == std::string::npos) {
            s = "=" + s;                     // bare expression
        }
    }

    std::string out;
    out.reserve(s.size());
    bool inRef = false;
    std::string::size_type refStart = 0;
    for (char c : s) {
        if (c == '[') { inRef = true; refStart = out.size(); continue; }
        if (c == ']') { inRef = false; continue; }
        if (inRef && c == '.') {
            // The '.' separates an optional sheet name from the cell. If nothing
            // has been emitted for the sheet in this reference segment it means
            // the current sheet, so drop the dot; otherwise keep the separator.
            std::string::size_type segStart = refStart;
            std::string::size_type colon = out.rfind(':');
            if (colon != std::string::npos && colon >= refStart) segStart = colon + 1;
            if (out.size() == segStart) continue;   // current sheet -> drop '.'
            out += '.';                             // real Sheet.cell separator
            continue;
        }
        out += c;
    }
    return out;
}

} // namespace UltraCanvas
