# UltraCanvasMathEngine

The framework's own LaTeX **math typesetter**: parses a formula, lays it out
by the rules of TeX (The TeXbook, Appendix G) with the metrics of an OpenType
math font, and returns a box tree that `UltraCanvasMathRender` draws through
any `IRenderContext`. It is Phase 1 of
[`UltraCanvasLaTeXEngineProposal.md`](UltraCanvasLaTeXEngineProposal.md) and
the engine behind [`UltraCanvasLaTeXView`](UltraCanvasLaTeXView.md).

| Piece | Header | What it does |
|---|---|---|
| `UltraCanvasMathEngine` | `include/Plugins/LaTeX/UltraCanvasMathEngine.h` | The facade: owns the font, `Typeset()` a string into a `MathTypesetResult` |
| `UltraCanvasMathFont` | `UltraCanvasMathFont.h` | The OpenType MATH font reader ([its own page](UltraCanvasMathFont.md)) |
| `UltraCanvasMathParser` | `UltraCanvasMathParser.h` | LaTeX source to the atom tree |
| `UltraCanvasMathLayout` | `UltraCanvasMathLayout.h` | Atom tree to the box tree |
| model | `UltraCanvasMathModel.h` | Styles, atoms, boxes, diagnostics |
| tables | `UltraCanvasMathSymbols.h` | Symbol commands, math alphabets, operators, colours |
| `DrawMathBox` | `UltraCanvasMathRender.h` | Box tree to `IRenderContext`; the text fallback |

All of it lives in the on-demand `libUltraCanvasLaTeX` module. Nothing but
the renderer touches the UI framework, so parsing and layout run on any
thread and are unit-tested without a window (`Tests/MathEngineTest.cpp`).

## Typesetting a formula

```cpp
#include "Plugins/LaTeX/UltraCanvasMathEngine.h"

UltraCanvas::UltraCanvasMathEngine engine;
engine.LoadFont(GetResourcesDir() + "media/microtex/latinmodern-math.otf");

UltraCanvas::MathTypesetOptions options;
options.fontSize = 24.f;                          // pixels per em
options.style = UltraCanvas::MathStyle::Display(); // or MathStyle::Text() for inline use
UltraCanvas::MathTypesetResult r = engine.Typeset("\\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}", options);

// r.width, r.height (above the baseline), r.depth (below), r.root (the boxes)
for (const auto& d : r.diagnostics) std::cerr << d.message << " at " << d.sourceStart << "\n";
```

`Typeset` never throws. A formula with an unknown command still comes back
with a root box: the recoverable parts are typeset, the offending command is
an `Error` box the renderer shows in red monospace, and every problem is a
`MathDiagnostic` with the byte span in the source. `HasErrors()` is what
`UltraCanvasLaTeXView::IsValid()` reports.

The shared instance the LaTeX view uses is `GetSharedMathEngine()`; the
module loads its font from the same directories as the MicroTeX `.clm2`
(see the LaTeX view page). An engine of your own can load any OpenType font
with a MATH table — Latin Modern Math (bundled), STIX Two Math, the TeX
Gyre math family, Libertinus Math, Cambria Math — and refuses a font
without one (`GetLastError()` says so).

### Options

| Field | Meaning |
|---|---|
| `fontSize` | Pixels per em of the base size; scripts scale by the font's `ScriptPercentScaleDown` constants |
| `style` | `Display()` (as inside `\[ \]`) or `Text()` (as inside `$ $`); also `Script`/`ScriptScript` |
| `color` | Foreground unless `\color` changes it; the box tree carries colour scopes |
| `maxWidth` | > 0 breaks a long top-level row at relations and binary operators into left-aligned lines |
| `textFallback` | An `IMathTextFallback` that measures runs the math font lacks (Cyrillic, CJK, ...); `MathContextTextFallback(ctx)` does it with the render context's text layouts |
| `preamble` | `\newcommand`, `\definecolor`, `\DeclareMathOperator` ... parsed before the formula |

## Drawing

```cpp
#include "Plugins/LaTeX/UltraCanvasMathRender.h"

MathContextTextFallback fallback(ctx);   // optional, for \text{} in scripts the font lacks
options.textFallback = &fallback;
MathTypesetResult r = engine.Typeset(src, options);
DrawMathBox(ctx, *r.root, x, y + r.height, Colors::Black);   // (x, baseline)
```

Glyphs are emitted as outline paths (`MoveTo`/`BezierCurveTo`/`Fill`), so
the formula follows the context's transform and stays crisp at any zoom;
rules are rectangles; `\fbox`, `\colorbox`, `\shadowbox`, `\ovalbox`,
`\cancel` and `\rotatebox` are drawn from their box types. The renderer is
about 180 lines; a host that wants the formula as a retained vector scene
walks `MathBox` itself — every box has `width`/`height`/`depth`, children at
`(dx, dy)` relative to the parent's origin (left end of the baseline, y
down), and the source span it came from.

## Inline math for the text stack

The engine reports a baseline, which is what lets a formula sit in a line
of text. From the core, `UltraCanvasInlineMath` (`include/UltraCanvasInlineMath.h`)
reaches the module through its ABI without linking it:

```cpp
#include "UltraCanvasInlineMath.h"

if (UltraCanvasInlineMath::IsAvailable()) {                // loads the module on first call
    auto f = UltraCanvasInlineMath::Typeset("\\frac{a}{b}", 16.f, Colors::Black, /*display*/ false, ctx);
    // reserve f->GetWidth() x (f->GetAscent() + f->GetDescent()) in your layout ...
    f->Draw(ctx, x, baselineY);                             // ... then draw it on the baseline
}
```

`UltraCanvasTextArea`'s Markdown mode does exactly this for `$...$`, `$$...$$`
and `$$` blocks (see [`UltraCanvasTextAreaExamples.md`](UltraCanvasTextAreaExamples.md),
"Math in Markdown mode"), so Word and ODT documents with equations render
them typeset. The module ABI entry points behind it are
`UltraCanvasLaTeXModule_TypesetInline`, `_InlineMetrics`, `_DrawInline` and
`_ReleaseInline` (ABI 3).

## What is supported

The parser understands the LaTeX math subset users actually type — the
whole of the framework's demo corpus (`media/LaTex/*.tex`) renders without
a diagnostic:

* **Structure:** groups, `^` `_` (with the double-script leniency of `x'^2`),
  primes, `\left ... \middle ... \right`, `\big`–`\Bigg` (l/r/m), `\\`
  line breaks at top level, `%` comments, `~`.
* **Fractions and roots:** `\frac`, `\dfrac`, `\tfrac`, `\cfrac`, `\binom`
  family, `\genfrac`, `\over`, `\atop`, `\choose`, `\brace`, `\brack`,
  `\above`, `\sqrt[n]`.
* **Operators:** `\sum`-like large operators with limits in display style,
  integrals with side scripts, `\limits`/`\nolimits`, the named functions
  (`\sin` ... `\lim`), `\operatorname`, `\operatorname*`,
  `\DeclareMathOperator`, `\mathop` and the other class wrappers, `\sideset`,
  `\prescript`, `\substack`, `\bmod`/`\pmod`/`\pod`/`\mod`.
* **Accents and decorations:** `\hat` ... `\vec`, the wide variants,
  `\overline`, `\underline`, `\overbrace`, `\underbrace`, brackets and
  parens, `\overrightarrow` family, `\overset`, `\underset`, `\stackrel`,
  `\xrightarrow[below]{above}` family, `\not`, `\cancel`, `\bcancel`,
  `\xcancel`.
* **Alphabets:** `\mathrm`, `\mathit`, `\mathbf`, `\boldsymbol`, `\mathsf`,
  `\mathtt`, `\mathcal`, `\mathscr`, `\mathfrak`, `\mathbb`, `\mathds` and
  the `\rm`/`\bf`/... switches, mapped to the Unicode mathematical
  alphanumerics with the letterlike-symbol holes; ~600 symbol commands
  (Greek, operators, relations, arrows, delimiters, dots, Hebrew, misc).
* **Text:** `\text`, `\mbox`, `\textbf` ... with `$...$` inside, text
  accents (`\'e`), spaces preserved; characters the math font lacks go to
  the host's text layout.
* **Environments:** `array` with full column specs (`l c r | @{} >{} *{n}{}`
  and `\newcolumntype`), `matrix` family incl. `smallmatrix`, `cases`,
  `rcases`, `dcases`, `align`/`aligned`/`alignat`/`split`/`flalign`,
  `gather`, `multline`, `eqnarray`, `subarray`, plus `\hline`,
  `\multicolumn`, `\hdotsfor`, `\intertext`, `\\[len]`, `\rowcolor`,
  `\cellcolor`, and user `\newenvironment`s.
* **Styles, sizes, colour, boxes:** `\displaystyle` ... `\scriptscriptstyle`,
  `\tiny` ... `\Huge`, `\color`, `\textcolor`, `\colorbox`, `\fcolorbox`,
  `\definecolor` (rgb/RGB/gray/cmyk/HTML), `#RRGGBB` and the dvipsnames
  colours, `\fbox`, `\boxed`, `\shadowbox`, `\doublebox`, `\ovalbox`,
  `\cornersize`, `\phantom` family, `\smash`, `\rlap`/`\llap`/`\clap`,
  `\rotatebox`, `\reflectbox`, `\scalebox`, `\resizebox`, `\rule`, spacing
  commands with TeX units, `\longdiv`.
* **Definitions:** `\newcommand`/`\renewcommand`/`\providecommand` with
  optional arguments, `\def`, `\newenvironment`, `\DeclareMathOperator`.

Not supported, by design: document mode (paragraphs, sections, `tabular`
outside math is treated as `array`), TikZ, equation numbering (`\tag`,
`\label`, `\ref` are accepted and ignored), and any package that is not
one of the built-ins. An unknown command is diagnosed, not fatal.

## How the layout works

The box builder follows Appendix G with the OpenType MATH constants in
place of TeX's font parameters:

* inter-atom spacing from the Ord/Op/Bin/Rel/Open/Close/Punct/Inner table,
  with the Bin-to-Ord rules and no medium/thick spaces in script styles;
* italic correction after math-italic characters, folded into superscript
  placement;
* scripts by `SuperscriptShiftUp`, `SubscriptShiftDown`, the gap minimum and
  the `SuperscriptBottomMaxWithSubscript` adjustment; math kerning when the
  font has it;
* fractions on the axis with the numerator/denominator gap minima, stacks
  for `\atop`/`\binom`; radicals with the vertical gap and the degree kerns;
* delimiters by TeX's 90%/5pt rule, choosing the smallest pre-drawn variant
  and assembling extenders beyond the largest; the same for wide accents,
  braces and arrows horizontally;
* large operators take their display variant and sit on the axis; limits
  by the four limit constants;
* arrays with LaTeX's `\arraycolsep`, struts and baseline distances, cells in
  text style (matrices), display style (`align`) or script style
  (`smallmatrix`), vertical rules drawn per cell so `\multicolumn` behaves;
* everything vertically centred on the axis where TeX centres it (`\vcenter`
  for arrays, delimiters, operators).

## Verifying it

`Tests/MathEngineTest.cpp` checks the parser's atom trees, the layout
against the font's numbers (a glyph box is its advance and bounding box, the
fraction rule sits on the axis, scripts obey the constants, `a=b` gets two
thick spaces, display operators grow, delimiters reach their target, arrays
centre on the axis), and then runs the vendored MicroTeX as an **oracle**
over the demo corpus and forty more formulas: both engines must agree on
width and height within a tolerance (mean deviation is about 6% / 7%; the
differences that remain are places where the native engine follows LaTeX
and MicroTeX does not — `\Big` is a fixed size, arrays use text style,
matrix rows are 1.2 em apart, a fraction keeps `\nulldelimiterspace`).

`Tests/MathFontTest.cpp` covers the font layer.

## Limits

* One math font per engine; no fallback *math* font. Text runs fall back
  through the host.
* `\text` uses the math font's upright glyphs (Latin Modern Math has a full
  Latin and Greek text repertoire), not the surrounding UI font.
* Line breaking is the simple greedy split at relations; there is no
  `\allowdisplaybreaks` or penalty model.
* Equation numbers, `\tag` and cross-references are ignored.
