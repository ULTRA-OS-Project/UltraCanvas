// include/Plugins/LaTeX/UltraCanvasMathLayout.h
// The box builder of the native math engine: turns the atom tree into a box
// tree following The TeXbook, Appendix G, with the dimensions taken from the
// font's OpenType MATH table (the same constants LuaTeX, XeTeX and MathJax
// use in place of TeX's font parameters).
//
// Everything is measured in pixels at the formula's font size; the four
// styles scale by the font's ScriptPercentScaleDown constants. A layout
// needs only a loaded UltraCanvasMathFont - no render context - so it can
// run on a worker thread; characters the math font lacks are measured
// through the optional IMathTextFallback and drawn by the host later.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/LaTeX/UltraCanvasMathModel.h"
#include "Plugins/LaTeX/UltraCanvasMathFont.h"

#include <vector>

namespace UltraCanvas {

// Measures runs of text the math font cannot show (CJK, Cyrillic in a Latin
// math font, ...). The renderer draws such runs through the same host with
// the box's `text`, `textStyle` and `fontSize`.
class IMathTextFallback {
public:
    virtual ~IMathTextFallback() = default;
    // Returns false when the host cannot measure either; the layout then
    // draws a placeholder box of about the same size.
    virtual bool MeasureText(const std::string& utf8, const MathFontStyle& style, float fontSizePx,
                             float& width, float& ascent, float& descent) = 0;
};

struct MathLayoutOptions {
    float fontSize = 20.f;                     // pixels per em of the base size
    MathStyle style = MathStyle::Display();    // style of the outermost list
    MathColor color = kMathColorBlack;         // foreground unless \color changes it
    float maxWidth = 0.f;                      // >0: break the top-level row at relations to fit
    IMathTextFallback* textFallback = nullptr;
};

class UltraCanvasMathLayout {
public:
    UltraCanvasMathLayout(const UltraCanvasMathFont& font, const MathLayoutOptions& options,
                          std::vector<MathDiagnostic>& diagnostics);
    ~UltraCanvasMathLayout();

    // Lays out the atom tree the parser produced. The returned box is a List
    // whose origin is the left end of the baseline of the first line.
    MathBoxPtr Layout(const MathAtomPtr& root);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace UltraCanvas
