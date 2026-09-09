# UltraCanvas LaTeX Engine — Investigation & Proposal

Status: **Investigation complete; Phases 0 and 1 implemented.** Phase 0,
`UltraCanvasMathFont` ([`UltraCanvasMathFont.md`](UltraCanvasMathFont.md)),
reads the OpenType MATH table, metrics and outlines straight from a font
file through FreeType; `Tests/MathFontTest.cpp` proves it equal to the
`.clm2` the vendored engine reads, glyph for glyph. Phase 1,
`UltraCanvasMathEngine` ([`UltraCanvasMathEngine.md`](UltraCanvasMathEngine.md)):
parser, Appendix G layout on the MATH constants, and an `IRenderContext`
renderer, 5,800 lines, is the LaTeX view's default engine
(`ULTRACANVAS_LATEX_ENGINE=native`); MicroTeX stays in the module as the
oracle `Tests/MathEngineTest.cpp` compares it against over the demo corpus.
Phases 2–4 are not started. This document
answers two questions put to the framework: can UltraCanvas replace the
vendored MicroTeX math engine with an implementation of its own, built on the
framework's vector rendering engine; and how far can such an implementation be
grown towards "LaTeX" as a whole without dragging in a TeX distribution.

Short answer: **yes to a native math engine, no to a native TeX.** A math
typesetter of MicroTeX's scope is a 10–15k-line C++ module that sits naturally
on `IRenderContext` paths, FreeType outlines and the OpenType MATH table that
HarfBuzz already exposes. It would remove the FontForge-generated `.clm2` font
pipeline, allow any OpenType math font, and give the framework a formula it
can lay out inline, hit-test and later edit. Everything "LaTeX" beyond math —
document structure, tables, TikZ, plots — is best served by *importers* that
map a LaTeX subset onto elements the framework already has (rich text,
`VectorStorage` documents, the chart engine), not by executing TeX. A real TeX
engine is where the gigabyte comes from, and it is the one option this
document argues against.

Companion documents:
[`UltraCanvasLaTeXView.md`](UltraCanvasLaTeXView.md) (the element as it ships),
[`UltraCanvasChartEngineProposal.md`](UltraCanvasChartEngineProposal.md),
[`UltraCanvasVectorConverters.md`](UltraCanvasVectorConverters.md).

Author: UltraCanvas Framework
Last Modified: 2026-09-09

---

## 1. Scope and method

Everything below comes from reading the current tree, not from assumption:

* the LaTeX plugin (`Plugins/LaTeX/`, `include/Plugins/LaTeX/`,
  `core/UltraCanvasLaTeXModuleLoader.cpp`) — 1,178 lines
* the vendored engine `third_party/microtex/` — every source file counted, the
  macro, command, environment and predefined-formula tables enumerated
* the font assets in `media/microtex/`
* every caller of `CreateLaTeXView()` and every producer of LaTeX strings
  (the DOCX/ODT importers, the Markdown pipeline, the Filer)
* the rendering surfaces a native engine would build on: `IRenderContext`,
  `VectorStorage` / `VectorRenderer`, the SVG path parser, the chart engine
  element, `UltraCanvasFontFile` (the FreeType precedent), and the CMake
  dependency set

The "graph engine element" named in the request is the **chart engine**
(`UltraCanvasChartEngineElement`, `Plugins/Charts/Engine/`), whose adoption is
still partial: most of the ~40 legacy charts keep their own axis, background
and legend code, and only the parallel coordinate chart is native to the new
engine (see the status block of the chart engine proposal). That does not
touch this work. A typesetter needs paths, glyph outlines and font metrics,
not axes and legends, so the engine described here stands on the layer
*underneath* the charts — the `IRenderContext` path API every element draws
through, the `VectorStorage` document model and `VectorRenderer` of the
Vector plugin, FreeType — and has no dependency on either the chart engine or
the legacy charts. The chart engine appears exactly once, as the target of
the `pgfplots` reader in Tier 3 / Phase 4, which is the last phase and should
be scheduled after the chart migration has gone far enough that new charts
are written against `UltraCanvasChartEngineElement` only.

---

## 2. What exists today

### 2.1 The plugin, measured

| Part | Where | Size |
|---|---|---|
| Engine (vendored MicroTeX 1.0.0, "openmath" branch) | `UltraCanvas/third_party/microtex/lib/` | 20,752 lines in tree; **19,462 compiled** (the C wrapper and `fontsense.cpp` are excluded); 1.1 MB source |
| Backend adapter (MicroTeX `Graphics2D` / `TextLayout` / `PlatformFactory` → `IRenderContext`) | `Plugins/LaTeX/UltraCanvasLaTeXBackend.{h,cpp}` | 165 + 321 lines |
| The element (`UltraCanvasLaTeXViewImpl`) and C ABI | `Plugins/LaTeX/UltraCanvasLaTeXViewImpl.cpp`, `UltraCanvasLaTeXModule.cpp`, `UltraCanvasLaTeXModuleABI.h` | 219 + 52 + 49 lines |
| Core-side loader (the only LaTeX code in the core) | `core/UltraCanvasLaTeXModuleLoader.cpp` | 216 lines |
| Math font | `media/microtex/latinmodern-math.clm2` + `.otf` | 1.36 MB + 0.73 MB |

Engine lines by subsystem (the same breakdown a replacement has to cover):

| Subsystem | Lines | What it is |
|---|---|---|
| `atom/` | 4,737 | The math atom tree: chars, rows, fractions, roots, scripts, accents, fences, matrices, operators, side-sets, stacks |
| `unimath/` | 4,104 | Unicode math: symbol classes, font style mapping (bold/italic/script/…), the math font abstraction |
| `macro/` | 2,405 | 267 native macros plus 25 commands and 24 environments defined in TeX syntax (all in `macro_def.cpp`), `\newcommand` |
| `core/` | 2,130 | Tokenizer/parser, formula table (119 predefined formulas: `\sin`, `\lim`, `\dots`, colon relations …), glue |
| `otf/` | 1,868 | The `.clm` metrics reader (MATH constants, variants, assemblies, kerning, ligatures, glyph paths) |
| `box/` | 1,543 | The box model (hbox/vbox/glue/kern/rule/char boxes) |
| `utils/` | 866 | Strings, UTF-8, locale |
| `graphic/` | 651 | The abstract `Graphics2D` backend interface (40 virtuals) and font-style helpers |
| `env/` | 532 | Typesetting environment (style, size, font) |
| `render/` | 336 | `Render` handle: draw, width/height/baseline |

The engine is compiled with `GLYPH_RENDER_TYPE=1`: it never asks the platform
to draw a glyph. Every glyph is emitted as outline commands (`moveTo` /
`lineTo` / `quadTo` / `cubicTo` / `closePath` / `fillPath`) which the adapter
maps one-to-one onto `IRenderContext::MoveTo` / `LineTo` /
`QuadraticCurveTo` / `BezierCurveTo` / `ClosePath` / `Fill`. Only `\text{…}`
runs go through the framework's Pango text layout (`CreateTextLayout`).

**So the formula view already draws through the UltraCanvas vector engine.**
What is *not* ours is everything upstream of the path commands: parsing,
layout, metrics and the font file format.

### 2.2 Where the gigabyte actually is

The concern that "full LaTeX libraries are more than 1 GB" is right about TeX
distributions and wrong about engines. The size is in the shipped macro
packages and fonts, not in the typesetting program:

| Thing | Approximate size | What it contains |
|---|---|---|
| TeX Live full scheme | 7–8 GB | ~4,000 packages, every font, documentation |
| TeX Live "basic" scheme | ~300 MB | pdfTeX + the LaTeX kernel + a handful of packages and fonts |
| The pdfTeX/LuaTeX engine binaries alone | ~5–20 MB | The TeX macro interpreter and page builder; useless without formats and packages |
| **MicroTeX as vendored here** | 1.1 MB source, ~2 MB fonts | A native reimplementation of the math *subset* — no TeX language, no packages |
| KaTeX / MathJax (for scale) | 30–60k lines JS | The same idea in the browser world: reimplement math natively, ship no TeX |

The lesson is that a native engine keeps its size only as long as it
**reimplements a curated subset natively** instead of interpreting TeX. The
moment an implementation executes `\usepackage{amsmath}` for real it needs
the LaTeX kernel and the package sources, and the size follows. §6 draws the
line accordingly.

### 2.3 What the current engine supports

From the macro table and the parser (all verified in the tree):

* fractions and generalised fractions (`\frac`, `\dfrac`, `\tfrac`,
  `\genfrac`, `\over`, `\atop`, `\binom`, `\choose`), roots, scripts,
  `\sideset`, `\prescript`, `\substack`, `\operatorname`, limits control
* delimiters with `\left`/`\middle`/`\right` and the `\big…\Bigg` family
* accents (`\hat`, `\vec`, `\widetilde`, `\overset`, `\underset`, …),
  over/under braces, brackets, parens, arrows, `\xrightarrow`/`\xleftarrow`,
  `\overline`/`\underline`
* environments: `array`, `tabular`, `matrix`, `smallmatrix`, `pmatrix`,
  `bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`, `eqnarray`, `align`, `flalign`,
  `alignat`, `aligned`, `alignedat`, `multline`, `cases`, `rcases`, `split`,
  `gather`, `gathered`, `math`, `displaymath`, `equation` (without numbering),
  plus `\multicolumn`, `\multirow`, `\hline`, `\rowcolor`, `\columncolor`,
  `\cellcolor`
* font styles (`\mathbf`, `\mathcal`, `\mathfrak`, `\mathbb`, `\mathsf`,
  `\mathtt`, `\boldsymbol`, …), 10 size commands, 4 display styles
* colour (`\color`, `\textcolor`, `\colorbox`, `\fcolorbox`, `\definecolor`),
  boxes (`\fbox`, `\boxed`, `\shadowbox`, `\ovalbox`, `\doublebox`),
  `\phantom` family, `\llap`/`\rlap`/`\clap`, spacing, `\cancel` family,
  `\longdiv`, `\resizebox`, `\reflectbox`
* `\newcommand` / `\renewcommand` / `\newenvironment` with optional arguments
* 119 predefined formulas (`\sin` … `\det`, `\dots` variants, colon relations,
  a few logos)
* `\text{…}` with bold/italic/sans/mono through Pango

Not supported and out of the engine's design: document mode (paragraphs,
sections, lists, `tabular`, floats, `\includegraphics`, cross-references,
`\label`/`\ref`, equation numbering), TikZ/pgf, bibliographies, any package
that is not one of the built-ins. The demo (`Apps/DemoApp/UltraCanvasLaTeXExamples.cpp`)
already carries a marker list to refuse such documents and fall back to a
reference image.

### 2.4 Who consumes LaTeX in the framework, and the gap that exists now

| Producer / consumer | What it does today |
|---|---|
| `Apps/DemoApp` LaTeX page | Extracts the math body of each `media/LaTex/*.tex` and typesets it live through `CreateLaTeXView()` — the only caller of the factory |
| DOCX importer (`Plugins/Documents/Word/UltraCanvasDocxFormat.cpp:316`) | Converts `m:oMath` (OMML) to LaTeX with `WordMath::OmmlToLatex` and emits a `$…$` rich-text run |
| ODT importer (`UltraCanvasOdtFormat.cpp:336`) | Same for MathML formula objects via `MathMLToLatex` |
| Markdown pipeline (`core/UltraCanvasTextArea_Markdown.cpp:731`) | Recognises `$…$` but only **substitutes Greek letters and a few commands with Unicode** (`SubstituteGreekLetters`); `$$…$$` is skipped for "a future block pass" |
| Filer | Classifies `.tex` as a LaTeX document; previews it as text |

The consequence is a visible product gap that is independent of which engine
is used: **an equation imported from a Word or ODT document is shown as flat
text with Greek letters swapped in, never typeset.** The importers already do
the hard part (OMML/MathML → LaTeX); what is missing is an inline formula run
that the text pipeline can measure and draw baseline-aligned. That is the first
thing any engine work should deliver (§7, Phase 2), and it argues for an engine
that can be called synchronously from a text layout pass with a proper
baseline, which the present element-only API cannot.

### 2.5 Limitations of the current stack that a native engine would remove

1. **Font pipeline.** MicroTeX does not read OpenType. It reads its own
   `.clm2` container, produced by `prebuilt/otf2clm.py`, which imports
   `fontforge`. Adding or updating a math font means a FontForge install and a
   regenerated binary blob; a user cannot point the framework at STIX Two Math,
   Cambria Math or Libertinus Math at runtime. The glyph outlines are
   duplicated into the `.clm2` (1.36 MB) alongside the `.otf` (0.73 MB) that
   already contains them.
2. **One font, one style.** The engine is initialised once with one
   `FontSrcFile`; there is no per-view font choice and no fallback for
   characters the font lacks (CJK, emoji, arbitrary Unicode in `\text`).
3. **Language level.** The engine is C++17 and the project is C++20; the build
   needs `-fno-char8_t` / `/Zc:char8_t-` for a `u8""` literal in
   `formula_def.cpp`, and a local patch in `utils.cpp` for locale handling.
   Errors are thrown as exceptions and caught at the element boundary.
4. **No integration with the text stack.** A formula is an element with a
   width and height; the baseline is not exposed to callers, so a formula
   cannot sit inline in a `UltraCanvasTextArea` line or a Markdown paragraph
   aligned with the surrounding text.
5. **No structure after layout.** The render is a box tree the engine owns;
   the framework cannot hit-test a sub-expression, select it, edit it, or
   export it as an SVG/PDF path document. `beginPath()` in the adapter returns
   `false` — path caching is unsupported — so every redraw re-emits every
   glyph outline.
6. **Colour is baked at parse time** (`SetTextColor` forces a reparse) and
   `Graphics2D::reset()` has to be neutered so the engine does not clobber the
   element's transform — both signs of an API designed for a different host.
7. **Ownership.** Upstream is a one-maintainer project on an experimental
   branch; the framework carries local patches and cannot shape the API to
   its needs (naming, error reporting, threading, the plugin ABI).

None of these is a bug in MicroTeX. They are the cost of wrapping an engine
built for another host, which is exactly what the framework's "wrapped
engines" rule exists to allow *until* the wrapped thing becomes a design
constraint. For math it now is.

---

## 3. The foundation a native engine would stand on

Everything a math typesetter needs from the host already exists in the
framework or in its required dependencies. Nothing new has to be vendored.

### 3.1 Vector drawing — `IRenderContext`

`include/UltraCanvasRenderContext.h` provides the complete path model a glyph
and rule renderer needs: `MoveTo`, `LineTo`, `QuadraticCurveTo`,
`BezierCurveTo` (and relative variants), `Arc`/`ArcTo`, `ClosePath`,
`Fill`/`Stroke`, `FillPathPreserve`/`StrokePathPreserve`, `SetFillRule`,
`ClipPath`/`ClipRect`, `PushState`/`PopState`, `Translate`/`Scale`/`Rotate`,
`SetLineDash`, `GetPathExtents`, and `GetDeviceScale` for crisp rules at
fractional DPI. This is the same surface the MicroTeX adapter uses today, so
the render side of a native engine is a straight port of the 321-line adapter
minus the impedance mismatch.

### 3.2 A retained path scene — `VectorStorage` + `VectorRenderer`

`Plugins/Vector/UltraCanvasVectorStorage.h` defines a full retained-mode
vector document: `VectorDocument` → layers → groups → elements (`VectorPath`
with `PathData`/`PathCommand`, `VectorRect`, `VectorText`, `VectorTextPath`,
`VectorUse`…), with `Matrix3x3` transforms, fills, gradients, strokes, clip
paths and masks. `VectorRenderer` draws such a document through
`IRenderContext`; `UltraCanvasVectorPathOps.h` flattens paths for geometry.
The Vector converters already turn this model into SVG/EPS/PDF-ish output.

A native math engine that *optionally* materialises its typeset box tree as
a `VectorDocument` (one group per box, glyphs as `VectorPath`, rules as
`VectorRect`, each carrying the source span in its id) gets, for free:
caching (the document is retained, the outlines are not re-emitted per
frame), export to every vector format the converters write, hit-testing
through the existing bounding boxes, and a `UltraCanvasVectorElement` viewer
with zoom and pan. This is the concrete meaning of "using the graph engine
element": the formula becomes a vector scene the framework already knows how
to hold, draw and save.

### 3.3 Fonts — FreeType and HarfBuzz are already required

`UltraCanvas/CMakeLists.txt` makes FreeType, HarfBuzz, Pango and Cairo hard
dependencies of the core; `core/UltraCanvasFontFile.cpp` is the precedent for
going straight at a font file with FreeType, off the Pango path, from any
thread.

* **Glyph outlines:** `FT_Load_Glyph(face, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING)`
  + `FT_Outline_Decompose` yields move/line/conic/cubic callbacks in font
  units — exactly what `IRenderContext` consumes after one scale. This
  replaces the outline copy inside `.clm2`.
* **Math metrics:** the OpenType `MATH` table (constants, italics correction,
  top-accent attachment, extended-shape flags, math kerning, size variants,
  glyph assemblies for stretchy delimiters). **Implemented** in
  `UltraCanvasMathFont`: the table is loaded with `FT_Load_Sfnt_Table` and
  parsed by the framework itself — it is a small, flat binary format, and a
  private reader (about 250 lines of the 620-line source) keeps the module
  independent of the HarfBuzz version a platform ships and of HarfBuzz link
  order in a static core. HarfBuzz's `hb_ot_math_*` API served as the second
  oracle: on Latin Modern Math, STIX Math and the five TeX Gyre math fonts
  (about 30,000 glyphs) every constant, italics correction, top-accent
  attachment, extended-shape flag, variant list, assembly and kern lookup the
  two readers return is identical.
* **Text runs:** `\text{…}` keeps using `CreateTextLayout` (Pango), which
  brings shaping, fallback fonts and bidi that no math engine should
  reimplement.
* **Cmaps and ligatures:** `FT_Get_Char_Index` / HarfBuzz `hb_font_get_glyph`
  for the Unicode-math code points that `unimath/` maps today.

The result is that *every* OpenType font with a `MATH` table works
unmodified, the `.clm2` and `otf2clm.py` go away, and the shipped font can
be any of Latin Modern Math (already bundled), STIX Two Math, Libertinus
Math, Fira Math or the OS's Cambria Math.

### 3.4 Layout and elements

`UltraCanvasUIElement` with CSS layout (`ComputeIntrinsicSizes`,
`MeasureOwnContent`) already hosts the view; `ITextLayout` exposes
`GetBaseline` and `GetLayoutExtents`, which is the contract an inline formula
run needs to match. The on-demand module loader (`UltraCanvasLaTeXModuleLoader.cpp`)
and the versioned C ABI stay as they are — a native engine is a new
implementation behind the same `libUltraCanvasLaTeX` module, so callers
change nothing.

---

## 4. A native math engine: design and size

### 4.1 The five layers

| Layer | Responsibility | Estimated lines | Reference in MicroTeX |
|---|---|---|---|
| **Lexer + macro expander** | TeX tokenisation (catcodes for the math subset), `\newcommand` with optional args, environments, comments, `\def`-lite; a fixed table of built-in commands rather than an interpreter | 1,500–2,000 | `core/parser`, `macro/` (4,535) |
| **Math parser → atom list** | Builds the TeX Appendix G structure: Ord/Op/Bin/Rel/Open/Close/Punct/Inner atoms with nucleus/sub/sup, fractions, radicals, accents, fences, arrays, style changes, colour, boxes | 3,000–4,000 | `atom/` (4,737) |
| **Font layer** | OpenType MATH via FreeType (**done**: `UltraCanvasMathFont`, 620 lines, covers the table, metrics, outlines and caching); still open: the Unicode-math style mapping (Latin/Greek/digit ranges for bold, italic, script, fraktur, double-struck, sans, mono) and a fallback-font chain | 1,500–2,000 | `unimath/` + `otf/` (5,972 — most of it is the `.clm` reader and the mapping tables) |
| **Box builder** | Appendix G rules 1–22: inter-atom spacing table, script placement (σ13–σ22), fraction rules (σ8–σ12 and the MATH constants), radical geometry, delimiter sizing and assembly, big operators with limits, accent skew, `\left…\right`, arrays/alignments with cell glue, stretchy arrows and braces | 3,000–4,000 | `box/` + `env/` (2,075) plus the layout parts of `atom/` |
| **Renderer** | Box tree → `IRenderContext` path commands (immediate) and → `VectorDocument` (retained); baseline, ink and logical extents; error rendering | 500–800 | `render/` + `graphic/` (987) + the 321-line adapter |
| **Total** | | **~10,000–13,000** (actual Phase 1: 5,800 including the font layer) | 19,462 |

The estimate is below MicroTeX because three things it carries are not
needed: the `.clm` reader and glyph-path storage (the font gives us both),
the abstract `Graphics2D`/`PlatformFactory` portability layer (there is one
host), and the second copy of every mapping table that Unicode-math already
defines by code point ranges. A first shippable milestone — Appendix G core,
`amsmath` environments, fonts, colour, `\text` — is realistically the lower
bound; the long tail of convenience macros (`\cancel`, `\longdiv`,
`\shadowbox`, `\stackinset`, …) is what fills the upper bound.

### 4.2 Fidelity: the real risk, and how to contain it

TeX math layout is well specified (The TeXbook, Appendix G; the OpenType
MATH specification for the font-driven constants), so a native engine is not
research. The risk is the accumulation of small placement errors — a
superscript a pixel too low, a stretched brace with the wrong overlap — that
a reader of formulas notices immediately. Two measures contain it:

1. **Use MicroTeX as the oracle while it is still in the tree.** For every
   formula in a corpus (the 24 `media/LaTex/*.tex`, plus an amsmath test set
   drawn from the KaTeX/MathJax public test suites, which are MIT-licensed),
   record the width/height/baseline and the per-glyph positions MicroTeX
   produces through the existing adapter, and assert the native engine
   matches within tolerance. The adapter already receives every glyph as a
   path; capturing its bounding boxes is a 50-line test hook.
2. **Golden metrics tests, not golden pixels.** `Tests/` has the pattern
   (`ChartEngineTest.cpp`, `LabelPlacementTest.cpp`): assert box metrics and
   glyph placements as numbers so tests are font-hinting- and
   platform-independent.

After parity is reached the oracle is dropped together with the vendored
engine.

### 4.3 What the framework gains

* One font pipeline (FreeType/HarfBuzz) for text and math; any OpenType math
  font at runtime; 1.36 MB of `.clm2` and the FontForge dependency gone.
* A formula with a **baseline**, usable inline by `UltraCanvasTextArea`, the
  Markdown renderer and the Word/ODT document views.
* A retained vector scene per formula: cached redraws, hit-testing per atom,
  SVG/PDF/EPS export through the existing converters, and the ground for a
  later formula *editor* (caret between atoms), which no wrapped engine can
  offer.
* C++20, framework naming, `UltraCanvas::` error results instead of
  exceptions, thread-safe measurement off the render thread (needed by the
  Filer's thumbnailer and by text layout).
* Multi-font fallback for `\text` and for symbols the math font lacks, via
  the same fallback list the text stack uses.

### 4.4 Effort

Roughly the size of the chart engine's model layer plus the label engine:
one engineer, three to five months to parity with the current feature set
including the oracle test suite; the inline-text integration (§7 Phase 2)
adds about a month because it touches the text pipeline. The proposal is
staged so that each phase ships value on its own and the vendored engine is
removed only when the tests say so.

---

## 5. Alternatives considered

| Option | Verdict | Why |
|---|---|---|
| **Keep MicroTeX, replace only its font layer** with `UltraCanvasMathFont` behind MicroTeX's `Otf` class | Viable stop-gap | Removes `.clm2`/FontForge (limitation 1) and allows any math font; `otf/` is 1.9k lines behind one `Otf` class, and the font layer now exists. Leaves limitations 3–7 in place. Worth doing only if Phase 1 is not started within the year; the adapter would be thrown away with MicroTeX. |
| **Keep MicroTeX and add features upstream-style** | Not recommended | Every feature in §6 that is not math is outside its design; the API cannot give the text stack a baseline without forking the engine. |
| **Embed a real TeX** (TeX-in-C, tectonic, or LuaTeX as a library) | Rejected | This is the >1 GB path: the engine needs a format file plus the LaTeX kernel, `amsmath`, fonts and every package the input names. Output is DVI/PDF pages, not element geometry; no inline use, no editing; multi-second start-up; licences and build complexity (Rust toolchain for tectonic). |
| **MathML as the internal model** (LaTeX → MathML → layout) | Partially adopted | The Word importers already come from MathML/OMML. A native engine should keep an atom tree that can be *serialised* to MathML for interchange (ODT export, accessibility), but MathML is a poor layout input: it has no inter-atom spacing rules of its own and browsers embed a TeX-like layouter anyway. |
| **Port KaTeX's layout to C++** | Informative only | KaTeX is MIT-licensed, ~30k lines of JS with an excellent test suite and a well-documented port of Appendix G. Its *tests* and the structure of its `buildHTML` box builder are worth reading; its code is not portable as-is (DOM output, font metrics baked from TeX `.tfm` files). |

---

## 6. Beyond formulas: what "LaTeX demands" can mean, tier by tier

"Cope with LaTeX" splits into four tiers of very different cost. The first
three are recommended; the fourth is the one to refuse.

### Tier 1 — Math, complete (native engine parity and beyond)

Everything in §2.3, plus what users of the demo and of the Word importers
hit first and the engine has no notion of: equation numbering, `\tag` and
`\notag`, `\label`/`\ref`/`\eqref` inside `align` and `equation`,
`\operatorname*` (the starred form is not defined), user font choice per
view, a fallback font for symbols the math font lacks, colour and size
changes without reparsing, and an **inline formula run** with a baseline for
the text stack. This is the core of the proposal (§7 Phases 1–2).

### Tier 2 — Document subset as an importer

The `.tex` files people drop into the demo are `\documentclass{article}`
documents. Rendering them means paragraphs, `\section`, `itemize`/`enumerate`,
`tabular` with `booktabs`, `\includegraphics`, `\caption`, `\footnote`,
`\href`, `\cite` (as text), `\emph`/`\textbf`, verbatim, and display math
with numbering. Every one of these has a home in the framework already: the
rich-text run model the DOCX/ODT importers fill, the Markdown renderer in
`UltraCanvasTextArea`, `UltraCanvasImageElement`, and the formula element.

So Tier 2 is **a LaTeX→rich-text importer** — the mirror image of the
existing `MathToLatex` and `DocxFormat` readers — not a typesetter: it walks
a tokenised LaTeX document, maps a fixed vocabulary of commands and
environments onto runs, paragraphs, tables and embedded formula/image
elements, and hands the result to the same document view the Word formats
use. Unknown commands degrade to their arguments' text with a diagnostic, the
way Pandoc's LaTeX reader does. Estimated 3,000–5,000 lines including
`tabular`, no new dependency, and it reuses the Tier 1 engine for every `$…$`.
What it deliberately does not do: page breaking to A4, hyphenation to TeX's
quality, float placement, `\newpage` semantics, or arbitrary packages.

### Tier 3 — TikZ and pgfplots as subsets onto the vector engine

`tikzpicture` is the second most common thing in a `.tex` file after math,
and the first thing the demo has to refuse today. TikZ proper is a macro
package of well over 100k lines of TeX on top of pgf; nobody reimplements all
of it. But the **TikZ path language** — coordinates, `--`, `..controls..`,
`circle`, `rectangle`, `arc`, `node[...]{text}` with anchors, arrow tips,
`\draw`/`\fill`/`\filldraw`, line styles, colours, `scale`/`shift`,
`\foreach` over simple ranges — is a small grammar that maps directly onto
`VectorStorage` (`VectorPath`, `VectorText`, `VectorGroup` with `Matrix3x3`)
and is drawn by `VectorRenderer` inside a `UltraCanvasVectorElement`. That is
the point where "using the graph engine element" pays off most literally: the
TikZ subset becomes a converter into the vector document model, joining SVG,
EPS, CDR and the rest in `Plugins/Vector/`. A useful subset (what
introductory papers and slides use) is roughly 4,000–6,000 lines; the long
tail (`decorations`, `calc` expressions, `matrix of nodes`, automata
libraries) is opt-in later or never.

`pgfplots` (`\begin{axis}` with `\addplot`) is a different mapping: its
inputs are series, axis ranges, tick specs and legends — exactly the
vocabulary of `UltraCanvasChartEngineElement` (`ChartAxisSet`, series,
label plan, legend). A pgfplots reader that instantiates a chart-engine
element is the right use of the chart engine here, and it is small because
the engine already does the layout.

### Tier 4 — A real TeX engine (not recommended)

Executing arbitrary LaTeX means implementing TeX's macro language (the
expansion machinery, catcodes, registers, `\def` with delimited parameters,
conditionals), the paragraph and page builders, and then **shipping the
LaTeX kernel and the packages as TeX source**, because that is what
`\usepackage` loads. The engine itself is ~25k lines (tex.web) but the
payload is the TeX Live tree: this is the 1 GB the request wants to avoid,
and no native rewrite changes that arithmetic. It also produces pages rather
than elements, so it would not help the inline and document cases above. The
recommendation is to state this boundary in the LaTeX view's documentation
and refuse, with a clear diagnostic, whatever the Tier 1–3 subsets do not
cover — which the demo already does for TikZ today.

---

## 7. Recommended architecture and phase plan

### 7.1 Module layout

Keep the on-demand module and its public element API unchanged; replace what
is behind it. Names follow the framework's PascalCase convention and are
proposals:

```
UltraCanvas/include/Plugins/LaTeX/
  UltraCanvasLaTeXView.h            (unchanged public element API)
  UltraCanvasLaTeXModuleABI.h       (unchanged C ABI; bump ULTRACANVAS_LATEX_ABI_VERSION when the inline API lands)
  UltraCanvasMathFont.h             OpenType MATH font: metrics, variants, assemblies, outlines (FreeType + HarfBuzz)
  UltraCanvasMathParser.h           LaTeX math tokenizer + macro expander → atom tree
  UltraCanvasMathLayout.h           Appendix G box builder → box tree (measure without a render context)
  UltraCanvasMathRender.h           Box tree → IRenderContext, and → VectorStorage::VectorDocument
  UltraCanvasMathEngine.h           Facade: Typeset(source, options) → UltraCanvasMathResult {boxes, baseline, extents, errors}
UltraCanvas/Plugins/LaTeX/
  the matching .cpp files; UltraCanvasLaTeXViewImpl.cpp switches to the facade
  Tier 2/3 later: UltraCanvasLaTeXDocumentReader.cpp, UltraCanvasTikZConverter.cpp (under Plugins/Vector/), UltraCanvasPgfPlotsReader.cpp (under Plugins/Charts/)
```

The engine has no dependency on the UI element, so `Typeset()` can run on a
worker thread (thumbnails, text layout) and its result can be drawn later.
Errors are returned in the result (`std::vector<UltraCanvasMathDiagnostic>`
with source spans), never thrown; the view draws the first diagnostic the
way it draws `GetLastError()` today.

### 7.2 Phases

| Phase | Deliverable | Exit criterion |
|---|---|---|
| **0 — Font layer** — **done** | `UltraCanvasMathFont` on FreeType (`include/Plugins/LaTeX/UltraCanvasMathFont.h`, built into the LaTeX module); `Tests/MathFontTest.cpp` links `microtex_core` as the oracle and compares the `.otf` against the `.clm2` | Met: all 56 constants, the connector overlap and, over all 4,802 glyphs, every advance/height/depth, 1,002 italics corrections, 2,475 top-accent attachments, 176 variant lists and 114 assemblies are equal; math kerning (absent from every font at hand) is verified on a synthetic table; STIX Math and TeX Gyre Termes Math load and stretch when installed |
| **1 — Native math engine** — **done** | `UltraCanvasMathParser` (2,000 lines: ~180 commands, ~600 symbols, environments, macros, text mode), `UltraCanvasMathLayout` (1,570 lines: Appendix G on the MATH constants), `UltraCanvasMathRender` (180 lines), the `UltraCanvasMathEngine` facade; `UltraCanvasLaTeXView` uses it by default, MicroTeX stays selectable (`ULTRACANVAS_LATEX_ENGINE`) as the oracle | Met except the removal: every shipped `.tex` renders without a diagnostic; `Tests/MathEngineTest.cpp` compares 63 formulas with MicroTeX at mean 6% width / 7% height deviation, and checks the layout against the font's own constants. Deleting `third_party/microtex` and the `.clm2` is left for when the native engine has been in use for a release (§8 decision 4) |
| **2 — Inline math** | `UltraCanvasMathEngine` exposed to the text stack; `$…$` in `UltraCanvasTextArea` Markdown, and the `$latex$` runs from the DOCX/ODT importers, laid out as baseline-aligned inline formulas; `$$…$$` as display blocks | A Word document with an OMML equation shows a typeset equation in the document view; the Markdown demo shows inline and display math |
| **3 — Document subset** | `UltraCanvasLaTeXDocumentReader` producing the rich-text document model; `.tex` in the Filer/MediaViewer opens as a document; demo fallback-image path retired for documents in the subset | The `media/LaTex` set plus a small `article` corpus render; unknown commands produce diagnostics, not blank panes |
| **4 — TikZ / pgfplots subsets** | `UltraCanvasTikZConverter` → `VectorDocument`; `UltraCanvasPgfPlotsReader` → chart engine element | A curated TikZ corpus (shapes, nodes, arrows, `\foreach`) and a pgfplots line/bar/scatter set render; unsupported libraries are diagnosed |

Phases 0–2 are the proposal proper; 3 and 4 are how the same foundation
grows towards "LaTeX demands" without ever interpreting TeX.

### 7.3 Things to fix regardless of the engine decision

These were found during the investigation and do not depend on the choice
above:

* The Markdown pipeline's `$…$` handling is a Unicode substitution, so
  imported Word/ODT equations are not typeset (§2.4). Phase 2 fixes it; until
  then the document views should at least mark such runs as math.
* `UltraCanvasUIElements.md`, the element catalogue, has no entry for
  `UltraCanvasLaTeXView`; a developer looking for "formula" or "equation"
  will not find it and may hand-roll one.
* `Docs/UltraCanvas/CHANGELOG.md` currently has a `0.3.109` entry above a
  `0.3.110` entry (lines 1 and 45), so the "first line is the version" rule
  yields the older number; a merge left the entries out of order.

---

## 8. Decisions requested

1. **Go / no-go on the native engine** (Phases 0–2). The recommendation is
   go, starting with Phase 0, which is useful on its own and is reused whole
   by Phase 1.
2. **Default math font.** Latin Modern Math (already bundled, GUST/OFL) is the
   TeX look; STIX Two Math (OFL) has wider Unicode coverage. Either works once
   Phase 0 lands; the proposal keeps Latin Modern as the default and makes the
   choice per view.
3. **Scope line for Tier 3.** Whether TikZ is in the framework's remit at
   all, or whether the demo's "reference image" fallback is the accepted
   answer for pictures. The vector engine makes a subset cheap; the tail is
   unbounded and must be declared out of scope in writing.
4. **Removal of MicroTeX.** Whether to keep it as an optional engine after
   parity or delete it. The recommendation is delete: two engines means two
   sets of rendering differences to explain.

---

## Appendix A — Inventory of the current LaTeX stack

| File | Lines | Role |
|---|---|---|
| `include/Plugins/LaTeX/UltraCanvasLaTeXView.h` | 80 | Abstract element + factory declarations |
| `include/Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h` | 76 | Concrete view (module-internal) |
| `include/Plugins/LaTeX/UltraCanvasLaTeXBackend.h` | 165 | MicroTeX backend adapter declarations |
| `include/Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h` | 49 | Versioned C ABI |
| `Plugins/LaTeX/UltraCanvasLaTeXBackend.cpp` | 321 | `Graphics2D`/`TextLayout`/`PlatformFactory` → `IRenderContext`; font search; engine init |
| `Plugins/LaTeX/UltraCanvasLaTeXViewImpl.cpp` | 219 | Parse on demand, intrinsic size, render, error display |
| `Plugins/LaTeX/UltraCanvasLaTeXModule.cpp` | 52 | ABI entry points |
| `core/UltraCanvasLaTeXModuleLoader.cpp` | 216 | `dlopen` search order, factory, error reporting |
| `third_party/microtex/lib/**` | 20,752 (19,462 compiled) | The engine |
| `third_party/microtex/prebuilt/otf2clm.{py,sh}` | — | FontForge-based `.otf` → `.clm2` converter (not built) |
| `media/microtex/latinmodern-math.clm2` | 1.36 MB | Metrics + glyph paths |
| `media/microtex/latinmodern-math.otf` | 0.73 MB | The font (optional in path mode) |
| `media/LaTex/*.tex` | 24 files | Demo corpus: 12 `math-*` classics, 12 `microtex-*` feature showcases |

## Appendix B — Host capabilities a native engine relies on

| Need | Provided by | Where |
|---|---|---|
| Path fill/stroke, transforms, clipping | `IRenderContext` | `include/UltraCanvasRenderContext.h` |
| Retained vector scene, export | `VectorStorage`, `VectorRenderer`, vector converters | `Plugins/Vector/` |
| Glyph outlines, cmap, kerning | FreeType (required dependency; precedent in `UltraCanvasFontFile.cpp`) | `core/UltraCanvasFontFile.cpp` |
| OpenType MATH table | `UltraCanvasMathFont` (own reader on `FT_Load_Sfnt_Table`; HarfBuzz `hb_ot_math_*` used only as a test oracle) | `Plugins/LaTeX/UltraCanvasMathFont.cpp` |
| Shaped text for `\text{…}` | `ITextLayout` via Pango | `CreateTextLayout` |
| Element hosting, intrinsic sizing | `UltraCanvasUIElement` + CSS layout | `UltraCanvasLaTeXViewImpl.cpp` |
| On-demand loading, C ABI | Existing module loader | `core/UltraCanvasLaTeXModuleLoader.cpp` |
| Chart layout for `pgfplots` | `UltraCanvasChartEngineElement` | `include/Plugins/Charts/Engine/` |
| Metric-based tests | `Tests/` conventions | `Tests/ChartEngineTest.cpp`, `Tests/FontFileTest.cpp` |
