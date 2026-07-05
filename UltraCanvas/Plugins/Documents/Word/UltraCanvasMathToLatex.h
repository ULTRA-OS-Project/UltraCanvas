// Plugins/Documents/Word/UltraCanvasMathToLatex.h
// Converts embedded formula markup (ODF MathML, OOXML OMML) to LaTeX.
// Internal to the Word document module — not installed, not public API.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#pragma once
#include <string>
namespace tinyxml2 { class XMLElement; }
namespace UltraCanvas {
namespace WordMath {
// Converts a MathML <math> element (ODF embedded formula, namespace prefixes
// may be "math:" or none) to a LaTeX string (no surrounding $). Returns ""
// if nothing convertible.
std::string MathMLToLatex(const tinyxml2::XMLElement* mathElement);
// Converts an OOXML <m:oMath> element (WordprocessingML embedded math) to a
// LaTeX string (no surrounding $). Returns "" if nothing convertible.
std::string OmmlToLatex(const tinyxml2::XMLElement* oMathElement);
}
}
