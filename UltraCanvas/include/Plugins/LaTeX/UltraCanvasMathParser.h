// include/Plugins/LaTeX/UltraCanvasMathParser.h
// LaTeX math parser of the native math engine: turns a formula's source into
// the atom tree of UltraCanvasMathModel.h.
//
// The parser reads the source directly (no separate token stream): control
// sequences, groups, sub/superscripts, primes, environments and text mode
// are recognised as they come, and user macros (\newcommand, \def,
// \DeclareMathOperator, \newenvironment) are expanded by splicing their
// bodies into the remaining source. Font changes (\mathbf ...) are resolved
// at parse time into the font style of each character atom; style and
// colour scopes (\displaystyle, \color, \small) become atoms whose body is
// the rest of the enclosing group.
//
// Errors never throw: an unknown command becomes an Error atom that the
// renderer shows in red, and every problem is recorded as a diagnostic with
// its source span, so a formula with one typo still renders the rest.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/LaTeX/UltraCanvasMathModel.h"

#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasMathParser {
public:
    UltraCanvasMathParser();
    ~UltraCanvasMathParser();

    // Parses math-mode source. The returned atom is a Row (or an Array of
    // kind Lines when the source contains top-level \\ line breaks).
    MathAtomPtr Parse(const std::string& source, std::vector<MathDiagnostic>& diagnostics);

    // Definitions made with \newcommand etc. persist in the parser, so a
    // preamble can be parsed first and formulas after it.
    void DefineMacro(const std::string& name, int argCount, const std::string& optionalDefault,
                     bool hasOptional, const std::string& body);
    void DefineColor(const std::string& name, MathColor color);

private:
    struct Impl;
    Impl* impl_;
};

// Parses a LaTeX length ("2mm", "0.5em", "3mu", "10pt") to em of the current
// size; bare numbers are points. Returns false when unparsable.
bool ParseMathLength(const std::string& text, float& em);

} // namespace UltraCanvas
