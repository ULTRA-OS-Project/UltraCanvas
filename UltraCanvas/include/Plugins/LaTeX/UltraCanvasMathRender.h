// include/Plugins/LaTeX/UltraCanvasMathRender.h
// Draws the box tree of the native math engine through IRenderContext:
// glyphs as filled outline paths (crisp at any zoom, like the MicroTeX
// path-glyph mode it replaces), rules as rectangles, frames, cancel strokes,
// transforms and colour scopes. Text runs the math font could not show are
// drawn with the context's own text layout, which is also what measures
// them during layout (MathContextTextFallback).
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/LaTeX/UltraCanvasMathLayout.h"
#include "UltraCanvasRenderContext.h"

namespace UltraCanvas {

// Measures fallback text runs with the render context's text layouts.
class MathContextTextFallback : public IMathTextFallback {
public:
    explicit MathContextTextFallback(IRenderContext* ctx) : ctx_(ctx) {}
    void SetContext(IRenderContext* ctx) { ctx_ = ctx; }
    bool MeasureText(const std::string& utf8, const MathFontStyle& style, float fontSizePx,
                     float& width, float& ascent, float& descent) override;
private:
    IRenderContext* ctx_;
};

// The framework FontStyle a math text run maps to (serif/sans/mono, bold, italic).
FontStyle MathTextRunFontStyle(const MathFontStyle& style, float fontSizePx);

// Draws `box` with its origin (left edge of the baseline) at (x, baselineY)
// in the context's current coordinate space. `color` is the foreground used
// where the tree sets none.
void DrawMathBox(IRenderContext* ctx, const MathBox& box, double x, double baselineY, const Color& color);

} // namespace UltraCanvas
