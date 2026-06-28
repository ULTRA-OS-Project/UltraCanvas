# UltraCanvasLaTeXView

Renders a LaTeX (math) formula as a native UltraCanvas UI element, using an
embedded [MicroTeX](https://github.com/NanoMichael/MicroTeX) engine. The formula
is typeset to **vector paths** and drawn through the element's `IRenderContext`,
so it participates in the normal layout / transform / clip pipeline and stays
crisp at any zoom — there is no rasterized surface to blit.

The plugin is built when `ULTRACANVAS_PLUGIN_LATEX` is `ON` (the default). It
bundles the GUST/OFL-licensed **Latin Modern Math** font (Computer-Modern look),
so no external TeX installation is required.

## Header

```cpp
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"
```

## Basic usage

```cpp
using namespace UltraCanvas;

auto formula = CreateLaTeXView("eq1", /*x*/ 20, /*y*/ 20, /*w*/ 0, /*h*/ 0);
formula->SetTextSize(28.0f);
formula->SetTextColor(Colors::Black);
formula->SetLaTeX("\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");

window->AddChild(formula);
```

Passing width/height of `0` lets the element size itself to the formula's
intrinsic dimensions via the CSS layout engine. Place it in a `BoxLayout`,
`GridLayout`, etc. like any other element.

## API

| Method | Description |
| --- | --- |
| `SetLaTeX(const std::string&)` | Set the LaTeX source (math mode). Empty/whitespace input renders nothing. |
| `GetLaTeX()` | Current source string. |
| `SetTextSize(float pixels)` | Font size in pixels (default 20). Drives the intrinsic size. |
| `SetTextColor(const Color&)` | Foreground color (default black). |
| `SetMaxWidth(float pixels)` | Wrap width for inter-line math; `0` (default) = unlimited / intrinsic width. |
| `IsValid()` | `true` if the last `SetLaTeX` produced a render. |
| `GetLastError()` | Reason the last parse failed (engine not initialised, parse error, …). |
| `static SetFontSearchDir(const std::string&)` | Override where the bundled math font is looked up (see below). |

## Font resolution

At first use the engine loads `latinmodern-math.clm2` (+ `.otf`) by searching, in
order:

1. a directory set via `UltraCanvasLaTeXView::SetFontSearchDir(...)`,
2. the `MICROTEX_FONTDIR` environment variable,
3. `<exe>/media/microtex` and `<exe>/../media/microtex`,
4. `<exe>/share/UltraCanvas/media/microtex` (and `../`), the build/install layout.

The top-level CMake copies `media/` into the build/install tree, so a normal
build finds the font automatically. If `IsValid()` is `false` and
`GetLastError()` reports the engine was not initialised, point
`SetFontSearchDir()` at the directory containing `latinmodern-math.clm2`.

## Examples

```cpp
formula->SetLaTeX("\\int_{0}^{\\infty} e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}");
formula->SetLaTeX("\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}");
formula->SetLaTeX("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}");
formula->SetLaTeX("\\text{if } x \\geq 0 \\text{ then } \\sqrt{x}\\in\\mathbb{R}");
```

## Notes

- The engine renders **math-mode** LaTeX (equations, symbols, matrices,
  fractions, roots, big operators, accents). It is not a full document
  typesetter — for whole `.tex` documents a separate pipeline would be needed.
- Glyphs are drawn as filled paths (`GLYPH_RENDER_TYPE=1`); `\text{...}` runs use
  the framework's Pango-based text layout.
- See `THIRD_PARTY_LICENSES.md` for MicroTeX (MIT) and Latin Modern Math
  (GUST/OFL) licensing.
