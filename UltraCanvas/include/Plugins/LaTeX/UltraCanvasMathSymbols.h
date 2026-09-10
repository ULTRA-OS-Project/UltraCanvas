// include/Plugins/LaTeX/UltraCanvasMathSymbols.h
// The tables of the native math engine: LaTeX symbol commands to Unicode
// code points and atom classes, the unicode-math alphabet mapping (which
// glyph an italic bold 'x' is), the class of a directly typed character, the
// named operators (\sin ...), and the named colours of xcolor.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/LaTeX/UltraCanvasMathModel.h"

#include <string>

namespace UltraCanvas {

struct MathSymbolInfo {
    char32_t codepoint = 0;
    MathAtomClass atomClass = MathAtomClass::Ord;
    bool largeOperator = false;     // \sum-like: display-size variant, limits in display style
    bool combiningAccent = false;   // a combining mark used by \hat, \vec ...
    bool stretchyAccent = false;    // \widehat \widetilde: pick a wider variant for wide bases
};

// A LaTeX symbol command (name without the backslash) or null.
const MathSymbolInfo* LookupMathSymbol(const std::string& name);

// The atom class of a character typed directly ('+' is Bin, '=' is Rel, ...).
MathAtomClass ClassOfCharacter(char32_t cp);

// \sin, \log, \lim, ... : true when `name` is a named operator; `limits` says
// whether it takes limits (\lim, \max, \sup ...) in display style.
bool LookupNamedOperator(const std::string& name, bool& limits);

// The code point of `cp` set in the given alphabet: 'x' in a bold italic
// alphabet is U+1D499. `texDefault` applies TeX's defaults for Auto shape
// (Latin and lower-case Greek italic, digits and upper-case Greek upright).
// Returns `cp` itself when the alphabet has no such letter.
char32_t MapMathAlphanumeric(char32_t cp, const MathFontStyle& style);

// xcolor / dvipsnames / svgnames colour by name (case-insensitive), or false.
bool LookupNamedColor(const std::string& name, MathColor& out);

// Decode one UTF-8 code point starting at `pos`; advances `pos`.
char32_t DecodeUtf8(const std::string& s, size_t& pos);
void AppendUtf8(std::string& out, char32_t cp);

} // namespace UltraCanvas
