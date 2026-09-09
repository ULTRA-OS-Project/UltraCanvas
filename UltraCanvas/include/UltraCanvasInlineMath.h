// include/UltraCanvasInlineMath.h
// A LaTeX formula typeset for use inside a line of text: the core's handle
// on the LaTeX module's native math engine (Phase 2 of
// Docs/UltraCanvas/UltraCanvasLaTeXEngineProposal.md).
//
// UltraCanvasTextArea's Markdown mode uses this for $...$ runs: it asks for
// the formula's width, ascent and descent, reserves that box inside its
// Pango layout with a shape attribute on a placeholder character, and after
// drawing the text draws the formula at the placeholder's baseline. Any
// element that lays out text can do the same.
//
// The engine lives in the on-demand LaTeX module; the first call loads it.
// Without the module (not built, not found, no math font) IsAvailable() is
// false and Typeset() returns null, so callers keep their text-only path.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <memory>
#include <string>

namespace UltraCanvas {

class IRenderContext;

class UltraCanvasInlineMath {
public:
    ~UltraCanvasInlineMath();
    UltraCanvasInlineMath(const UltraCanvasInlineMath&) = delete;
    UltraCanvasInlineMath& operator=(const UltraCanvasInlineMath&) = delete;

    // True when the LaTeX module is loaded (loading it on the first call) and
    // exports the inline entry points. Does not guarantee a math font; a
    // Typeset() that finds none returns null.
    static bool IsAvailable();

    // Typesets `latex` (math mode) at `fontSizePx` in `color`; display style
    // sets large operators with limits as inside \[ \], text style as inside
    // $ $. `ctx` (optional) measures characters the math font lacks. Returns
    // null when the module or its font is unavailable.
    static std::shared_ptr<UltraCanvasInlineMath> Typeset(const std::string& latex, float fontSizePx,
                                                          const Color& color, bool displayStyle,
                                                          IRenderContext* ctx = nullptr);

    float GetWidth() const { return width_; }
    float GetAscent() const { return ascent_; }     // above the baseline
    float GetDescent() const { return descent_; }   // below the baseline
    float GetHeight() const { return ascent_ + descent_; }
    // False when the source had problems; the formula still renders with the
    // offending command marked in red, and GetError() names the first one.
    bool IsValid() const { return error_.empty(); }
    const std::string& GetError() const { return error_; }

    // Draws the formula with the left end of its baseline at (x, baselineY).
    void Draw(IRenderContext* ctx, double x, double baselineY) const;

private:
    UltraCanvasInlineMath() = default;
    void* handle_ = nullptr;
    float width_ = 0.f, ascent_ = 0.f, descent_ = 0.f;
    std::string error_;
};

// The character the text stack puts in a layout where a formula goes:
// U+FFFC OBJECT REPLACEMENT CHARACTER, three bytes of UTF-8.
constexpr const char* kInlineMathPlaceholder = "\xEF\xBF\xBC";
constexpr int kInlineMathPlaceholderBytes = 3;

} // namespace UltraCanvas
